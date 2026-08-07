#include "zero_cpu/debug/DebugSymbols.hpp"

#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace zero_cpu::debug {
namespace {

const char* kHeader =
    "ZCPU-SYMBOLS 1";

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

DebugSymbols
DebugSymbols::fromAssembledProgram(
    const AssembledProgram& program,
    std::size_t codeBase
) {
    DebugSymbols symbols;

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

    if (
        !std::getline(input, line)
        || line != kHeader
    ) {
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

        std::string kindText;
        std::string name;
        std::string addressText;
        std::string extra;

        if (
            !(parser
                >> kindText
                >> name
                >> addressText)
            || parser >> extra
        ) {
            throw std::runtime_error(
                "Malformed debug symbol at line "
                + std::to_string(lineNumber)
            );
        }

        symbols.add(
            parseKind(
                kindText,
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

const std::vector<DebugSymbol>&
DebugSymbols::entries() const {
    return entries_;
}

bool DebugSymbols::operator==(
    const DebugSymbols& other
) const {
    return entries_ == other.entries_;
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

std::string debugSymbolsPathForExecutable(
    const std::string& executablePath
) {
    return executablePath + ".zsym";
}

} // namespace zero_cpu::debug
