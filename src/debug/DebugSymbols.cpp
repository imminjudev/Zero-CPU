#include "zero_cpu/debug/DebugSymbols.hpp"

#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace zero_cpu::debug {
namespace {

const char* kHeader =
    "ZCPU-SYMBOLS 2";

const char* kLegacyHeader =
    "ZCPU-SYMBOLS 1";

// Patch: v1.2-source-debug-map-core-r1

bool isValidSymbolName(
    const std::string& name
) {
    if (name.empty()) {
        return false;
    }

    const unsigned char first =
        static_cast<unsigned char>(
            name.front()
        );

    if (
        std::isalpha(first) == 0
        && name.front() != '_'
    ) {
        return false;
    }

    for (const char character : name) {
        const unsigned char current =
            static_cast<unsigned char>(
                character
            );

        if (
            std::isalnum(current) == 0
            && character != '_'
        ) {
            return false;
        }
    }

    return true;
}

std::size_t parseAddress(
    const std::string& text,
    std::size_t lineNumber
) {
    if (
        text.empty()
        || text.front() == '-'
    ) {
        throw std::runtime_error(
            "Invalid symbol address at line "
            + std::to_string(lineNumber)
        );
    }

    try {
        std::size_t parsed = 0;

        const unsigned long long value =
            std::stoull(
                text,
                &parsed,
                10
            );

        if (
            parsed != text.size()
            || value
                > static_cast<unsigned long long>(
                    std::numeric_limits<
                        std::size_t
                    >::max()
                )
        ) {
            throw std::runtime_error(
                "Invalid symbol address at line "
                + std::to_string(lineNumber)
            );
        }

        return static_cast<std::size_t>(
            value
        );
    } catch (const std::runtime_error&) {
        throw;
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Invalid symbol address at line "
            + std::to_string(lineNumber)
        );
    }
}

std::size_t parseSourceLine(
    const std::string& text,
    std::size_t fileLineNumber
) {
    const std::size_t value =
        parseAddress(text, fileLineNumber);

    if (value == 0) {
        throw std::runtime_error(
            "Source line must be greater than zero at line "
            + std::to_string(fileLineNumber)
        );
    }

    return value;
}

DebugSymbolKind parseKind(
    const std::string& text,
    std::size_t lineNumber
) {
    if (text == "CODE") {
        return DebugSymbolKind::Code;
    }

    if (text == "DATA") {
        return DebugSymbolKind::Data;
    }

    throw std::runtime_error(
        "Invalid symbol kind at line "
        + std::to_string(lineNumber)
    );
}

} // namespace

const char* debugSymbolKindToString(
    DebugSymbolKind kind
) {
    switch (kind) {
    case DebugSymbolKind::Code:
        return "CODE";

    case DebugSymbolKind::Data:
        return "DATA";
    }

    throw std::runtime_error(
        "Invalid debug symbol kind"
    );
}

bool DebugSymbol::operator==(
    const DebugSymbol& other
) const {
    return kind == other.kind
        && name == other.name
        && address == other.address;
}

bool DebugSourceLocation::operator==(
    const DebugSourceLocation& other
) const {
    return address == other.address
        && line == other.line;
}

DebugSymbols
DebugSymbols::fromAssembledProgram(
    const AssembledProgram& program,
    std::size_t codeBase,
    const std::string& sourcePath
) {
    DebugSymbols symbols;
    symbols.source_path_ = sourcePath;

    if (
        !program.instruction_source_lines.empty()
        && program.instruction_source_lines.size()
            != program.instructions.size()
    ) {
        throw std::runtime_error(
            "Instruction source-line mapping size mismatch"
        );
    }

    std::vector<
        std::pair<std::string, std::size_t>
    > codeLabels(
        program.labels.begin(),
        program.labels.end()
    );

    std::sort(
        codeLabels.begin(),
        codeLabels.end()
    );

    for (const auto& label : codeLabels) {
        if (
            label.second
            > (
                std::numeric_limits<
                    std::size_t
                >::max()
                - codeBase
            ) / binary::kInstructionSize
        ) {
            throw std::runtime_error(
                "Code symbol address overflow"
            );
        }

        symbols.add(
            DebugSymbolKind::Code,
            label.first,
            codeBase
                + label.second
                    * binary::kInstructionSize
        );
    }

    std::vector<
        std::pair<std::string, std::size_t>
    > dataLabels(
        program.data_labels.begin(),
        program.data_labels.end()
    );

    std::sort(
        dataLabels.begin(),
        dataLabels.end()
    );

    for (const auto& label : dataLabels) {
        symbols.add(
            DebugSymbolKind::Data,
            label.first,
            label.second
        );
    }

    for (
        std::size_t index = 0;
        index < program.instruction_source_lines.size();
        ++index
    ) {
        if (
            index
            > (
                std::numeric_limits<std::size_t>::max()
                - codeBase
            ) / binary::kInstructionSize
        ) {
            throw std::runtime_error(
                "Source location address overflow"
            );
        }

        symbols.addSourceLocation(
            codeBase
                + index * binary::kInstructionSize,
            program.instruction_source_lines[index]
        );
    }

    return symbols;
}

DebugSymbols DebugSymbols::readFile(
    const std::string& path
) {
    std::ifstream input(path);

    if (!input) {
        throw std::runtime_error(
            "Cannot open debug symbols file: "
            + path
        );
    }

    std::string line;

    if (!std::getline(input, line)) {
        throw std::runtime_error(
            "Invalid debug symbols header: "
            + path
        );
    }

    const bool legacy =
        line == kLegacyHeader;

    if (!legacy && line != kHeader) {
        throw std::runtime_error(
            "Invalid debug symbols header: "
            + path
        );
    }

    DebugSymbols symbols;
    std::size_t lineNumber = 1;

    while (std::getline(input, line)) {
        ++lineNumber;

        if (line.empty()) {
            continue;
        }

        std::istringstream parser(line);

        std::string recordType;
        parser >> recordType;

        if (!legacy && recordType == "SOURCE") {
            std::string sourcePath;
            std::string extra;

            if (
                !(parser >> std::quoted(sourcePath))
                || parser >> extra
            ) {
                throw std::runtime_error(
                    "Malformed source path at line "
                    + std::to_string(lineNumber)
                );
            }

            if (!symbols.source_path_.empty()) {
                throw std::runtime_error(
                    "Duplicate source path at line "
                    + std::to_string(lineNumber)
                );
            }

            symbols.source_path_ =
                std::move(sourcePath);
            continue;
        }

        if (!legacy && recordType == "LINE") {
            std::string addressText;
            std::string sourceLineText;
            std::string extra;

            if (
                !(parser
                    >> addressText
                    >> sourceLineText)
                || parser >> extra
            ) {
                throw std::runtime_error(
                    "Malformed source location at line "
                    + std::to_string(lineNumber)
                );
            }

            symbols.addSourceLocation(
                parseAddress(
                    addressText,
                    lineNumber
                ),
                parseSourceLine(
                    sourceLineText,
                    lineNumber
                )
            );
            continue;
        }

        std::string name;
        std::string addressText;
        std::string extra;

        if (
            !(parser >> name >> addressText)
            || parser >> extra
        ) {
            throw std::runtime_error(
                "Malformed debug symbol at line "
                + std::to_string(lineNumber)
            );
        }

        symbols.add(
            parseKind(
                recordType,
                lineNumber
            ),
            name,
            parseAddress(
                addressText,
                lineNumber
            )
        );
    }

    return symbols;
}

void DebugSymbols::writeFile(
    const std::string& path
) const {
    std::ofstream output(
        path,
        std::ios::trunc
    );

    if (!output) {
        throw std::runtime_error(
            "Cannot write debug symbols file: "
            + path
        );
    }

    output << kHeader << "\n";

    if (!source_path_.empty()) {
        output
            << "SOURCE "
            << std::quoted(source_path_)
            << "\n";
    }

    for (
        const DebugSymbol& symbol :
        entries_
    ) {
        output
            << debugSymbolKindToString(
                symbol.kind
            )
            << " "
            << symbol.name
            << " "
            << symbol.address
            << "\n";
    }

    for (
        const DebugSourceLocation& location :
        source_locations_
    ) {
        output
            << "LINE "
            << location.address
            << " "
            << location.line
            << "\n";
    }

    if (!output) {
        throw std::runtime_error(
            "Failed while writing debug symbols file: "
            + path
        );
    }
}

bool DebugSymbols::empty() const {
    return entries_.empty();
}

std::size_t DebugSymbols::size() const {
    return entries_.size();
}

bool DebugSymbols::has(
    const std::string& name
) const {
    return indices_.find(name)
        != indices_.end();
}

bool DebugSymbols::hasCode(
    const std::string& name
) const {
    const auto iterator =
        indices_.find(name);

    return iterator != indices_.end()
        && entries_.at(
            iterator->second
        ).kind
            == DebugSymbolKind::Code;
}

bool DebugSymbols::hasData(
    const std::string& name
) const {
    const auto iterator =
        indices_.find(name);

    return iterator != indices_.end()
        && entries_.at(
            iterator->second
        ).kind
            == DebugSymbolKind::Data;
}

std::size_t DebugSymbols::resolveCode(
    const std::string& name
) const {
    const auto iterator =
        indices_.find(name);

    if (iterator == indices_.end()) {
        throw std::runtime_error(
            "Unknown debug symbol: "
            + name
        );
    }

    const DebugSymbol& symbol =
        entries_.at(iterator->second);

    if (
        symbol.kind
        != DebugSymbolKind::Code
    ) {
        throw std::runtime_error(
            "Debug symbol is not a code label: "
            + name
        );
    }

    return symbol.address;
}

std::size_t DebugSymbols::resolveData(
    const std::string& name
) const {
    const auto iterator =
        indices_.find(name);

    if (iterator == indices_.end()) {
        throw std::runtime_error(
            "Unknown debug symbol: "
            + name
        );
    }

    const DebugSymbol& symbol =
        entries_.at(iterator->second);

    if (
        symbol.kind
        != DebugSymbolKind::Data
    ) {
        throw std::runtime_error(
            "Debug symbol is not a data label: "
            + name
        );
    }

    return symbol.address;
}

bool DebugSymbols::hasSourceLocations() const {
    return !source_locations_.empty();
}

const std::string&
DebugSymbols::sourcePath() const {
    return source_path_;
}

std::size_t DebugSymbols::resolveSourceLine(
    std::size_t line
) const {
    if (line == 0) {
        throw std::runtime_error(
            "Source line must be greater than zero"
        );
    }

    for (
        const DebugSourceLocation& location :
        source_locations_
    ) {
        if (location.line == line) {
            return location.address;
        }
    }

    throw std::runtime_error(
        "Unknown executable source line: "
        + std::to_string(line)
    );
}

std::size_t DebugSymbols::sourceLineForAddress(
    std::size_t address
) const {
    for (
        const DebugSourceLocation& location :
        source_locations_
    ) {
        if (location.address == address) {
            return location.line;
        }
    }

    throw std::runtime_error(
        "No source line for executable address: "
        + std::to_string(address)
    );
}

const std::vector<DebugSourceLocation>&
DebugSymbols::sourceLocations() const {
    return source_locations_;
}

const std::vector<DebugSymbol>&
DebugSymbols::entries() const {
    return entries_;
}

bool DebugSymbols::operator==(
    const DebugSymbols& other
) const {
    return entries_ == other.entries_
        && source_path_ == other.source_path_
        && source_locations_
            == other.source_locations_;
}

void DebugSymbols::add(
    DebugSymbolKind kind,
    const std::string& name,
    std::size_t address
) {
    if (!isValidSymbolName(name)) {
        throw std::runtime_error(
            "Invalid debug symbol name: "
            + name
        );
    }

    if (has(name)) {
        throw std::runtime_error(
            "Duplicate debug symbol: "
            + name
        );
    }

    DebugSymbol symbol;
    symbol.kind = kind;
    symbol.name = name;
    symbol.address = address;

    indices_.emplace(
        name,
        entries_.size()
    );

    entries_.push_back(
        std::move(symbol)
    );
}

void DebugSymbols::addSourceLocation(
    std::size_t address,
    std::size_t line
) {
    if (line == 0) {
        throw std::runtime_error(
            "Source line must be greater than zero"
        );
    }

    for (
        const DebugSourceLocation& existing :
        source_locations_
    ) {
        if (existing.address == address) {
            throw std::runtime_error(
                "Duplicate source address: "
                + std::to_string(address)
            );
        }

        if (existing.line == line) {
            throw std::runtime_error(
                "Duplicate executable source line: "
                + std::to_string(line)
            );
        }
    }

    source_locations_.push_back(
        DebugSourceLocation{
            address,
            line
        }
    );
}

std::string debugSymbolsPathForExecutable(
    const std::string& executablePath
) {
    return executablePath + ".zsym";
}

} // namespace zero_cpu::debug
