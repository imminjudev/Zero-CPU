#include "zero_cpu/assembler/Assembler.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"
#include "zero_cpu/isa/Opcode.hpp"
#include "zero_cpu/isa/Operand.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace zero_cpu {
namespace {

enum class AssemblySection {
    Text,
    Data
};

struct SymbolPassResult {
    std::unordered_map<
        std::string,
        std::size_t
    > code_labels;

    std::unordered_map<
        std::string,
        std::size_t
    > data_labels;

    std::size_t data_size = 0;

    bool has_entry = false;
    std::string entry_label;
    std::size_t entry_line = 0;
    std::size_t entry_instruction = 0;
};

std::string trim(
    const std::string& text
) {
    const auto first = std::find_if_not(
        text.begin(),
        text.end(),
        [](
            unsigned char ch
        ) {
            return std::isspace(ch) != 0;
        }
    );

    if (first == text.end()) {
        return "";
    }

    const auto last = std::find_if_not(
        text.rbegin(),
        text.rend(),
        [](
            unsigned char ch
        ) {
            return std::isspace(ch) != 0;
        }
    ).base();

    return std::string(first, last);
}

std::string lowerCopy(
    const std::string& text
) {
    std::string result = text;

    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](
            unsigned char ch
        ) {
            return static_cast<char>(
                std::tolower(ch)
            );
        }
    );

    return result;
}

std::vector<std::string> splitLines(
    const std::string& source
) {
    std::vector<std::string> lines;
    std::istringstream input(source);
    std::string line;

    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    return lines;
}

std::string removeComment(
    const std::string& line
) {
    bool insideString = false;
    bool escaped = false;

    for (
        std::size_t index = 0;
        index < line.size();
        ++index
    ) {
        const char ch = line[index];

        if (insideString) {
            if (escaped) {
                escaped = false;
                continue;
            }

            if (ch == '\\') {
                escaped = true;
                continue;
            }

            if (ch == '"') {
                insideString = false;
            }

            continue;
        }

        if (ch == '"') {
            insideString = true;
            continue;
        }

        if (ch == ';') {
            return line.substr(0, index);
        }
    }

    return line;
}

bool isRegisterToken(
    const std::string& text
) {
    if (text.size() != 2) {
        return false;
    }

    const char first = static_cast<char>(
        std::toupper(
            static_cast<unsigned char>(
                text[0]
            )
        )
    );

    return first == 'R'
        && text[1] >= '0'
        && text[1] <= '7';
}

RegisterName parseRegister(
    const std::string& text,
    std::size_t lineNumber
) {
    if (!isRegisterToken(text)) {
        throw std::runtime_error(
            "Invalid register '"
            + text
            + "' at line "
            + std::to_string(lineNumber)
        );
    }

    return static_cast<RegisterName>(
        text[1] - '0'
    );
}

bool isValidLabelName(
    const std::string& text
) {
    if (text.empty()) {
        return false;
    }

    const unsigned char first =
        static_cast<unsigned char>(
            text.front()
        );

    if (
        std::isalpha(first) == 0
        && text.front() != '_'
    ) {
        return false;
    }

    for (const char ch : text) {
        const unsigned char current =
            static_cast<unsigned char>(ch);

        if (
            std::isalnum(current) == 0
            && ch != '_'
        ) {
            return false;
        }
    }

    return true;
}

bool isReservedDirectiveName(
    const std::string& text
) {
    const std::string lowered =
        lowerCopy(text);

    return lowered == "text"
        || lowered == "data"
        || lowered == "byte"
        || lowered == "word"
        || lowered == "qword"
        || lowered == "string"
        || lowered == "entry";
}

void requireValidLabelName(
    const std::string& text,
    std::size_t lineNumber
) {
    if (!isValidLabelName(text)) {
        throw std::runtime_error(
            "Invalid label name '"
            + text
            + "' at line "
            + std::to_string(lineNumber)
        );
    }

    if (isRegisterToken(text)) {
        throw std::runtime_error(
            "Label name cannot be register name '"
            + text
            + "' at line "
            + std::to_string(lineNumber)
        );
    }

    if (
        isValidOpcode(
            opcodeFromString(text)
        )
    ) {
        throw std::runtime_error(
            "Label name cannot be opcode name '"
            + text
            + "' at line "
            + std::to_string(lineNumber)
        );
    }

    if (isReservedDirectiveName(text)) {
        throw std::runtime_error(
            "Label name cannot be directive name '"
            + text
            + "' at line "
            + std::to_string(lineNumber)
        );
    }
}

bool tryConsumeLabel(
    std::string& line,
    std::string& label
) {
    line = trim(line);

    const std::size_t colonPosition =
        line.find(':');

    if (
        colonPosition
        == std::string::npos
    ) {
        return false;
    }

    const std::string candidate = trim(
        line.substr(0, colonPosition)
    );

    if (!isValidLabelName(candidate)) {
        return false;
    }

    label = candidate;

    line = trim(
        line.substr(colonPosition + 1)
    );

    return true;
}

bool tryParseInteger(
    const std::string& text,
    std::int64_t& value
) {
    if (text.empty()) {
        return false;
    }

    try {
        std::size_t parsed = 0;

        const long long result =
            std::stoll(
                text,
                &parsed,
                0
            );

        if (parsed != text.size()) {
            return false;
        }

        value = static_cast<std::int64_t>(
            result
        );

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::int64_t parseInteger(
    const std::string& text,
    std::size_t lineNumber
) {
    std::int64_t value = 0;

    if (!tryParseInteger(text, value)) {
        throw std::runtime_error(
            "Invalid or out-of-range integer '"
            + text
            + "' at line "
            + std::to_string(lineNumber)
        );
    }

    return value;
}

std::vector<std::string> splitOperands(
    const std::string& text
) {
    std::vector<std::string> operands;
    std::string current;

    bool insideMemoryAddress = false;

    for (const char ch : text) {
        if (ch == '[') {
            if (insideMemoryAddress) {
                throw std::runtime_error(
                    "Nested memory address"
                );
            }

            insideMemoryAddress = true;
            current.push_back(ch);
            continue;
        }

        if (ch == ']') {
            if (!insideMemoryAddress) {
                throw std::runtime_error(
                    "Unmatched memory-address bracket"
                );
            }

            insideMemoryAddress = false;
            current.push_back(ch);
            continue;
        }

        if (
            ch == ','
            && !insideMemoryAddress
        ) {
            const std::string value =
                trim(current);

            if (value.empty()) {
                throw std::runtime_error(
                    "Empty comma-separated value"
                );
            }

            operands.push_back(value);
            current.clear();
            continue;
        }

        current.push_back(ch);
    }

    if (insideMemoryAddress) {
        throw std::runtime_error(
            "Unclosed memory-address bracket"
        );
    }

    const std::string finalValue =
        trim(current);

    if (!finalValue.empty()) {
        operands.push_back(finalValue);
    }

    return operands;
}

std::vector<std::uint8_t> parseStringLiteral(
    const std::string& text,
    std::size_t lineNumber
) {
    const std::string literal = trim(text);

    if (
        literal.empty()
        || literal.front() != '"'
    ) {
        throw std::runtime_error(
            ".string requires a quoted string "
            "at line "
            + std::to_string(lineNumber)
        );
    }

    std::vector<std::uint8_t> result;

    for (
        std::size_t index = 1;
        index < literal.size();
        ++index
    ) {
        const char ch = literal[index];

        if (ch == '"') {
            if (
                !trim(
                    literal.substr(index + 1)
                ).empty()
            ) {
                throw std::runtime_error(
                    "Unexpected text after .string "
                    "literal at line "
                    + std::to_string(lineNumber)
                );
            }

            return result;
        }

        if (ch != '\\') {
            result.push_back(
                static_cast<std::uint8_t>(
                    static_cast<unsigned char>(ch)
                )
            );

            continue;
        }

        ++index;

        if (index >= literal.size()) {
            throw std::runtime_error(
                "Incomplete string escape at line "
                + std::to_string(lineNumber)
            );
        }

        switch (literal[index]) {
        case 'n':
            result.push_back('\n');
            break;

        case 'r':
            result.push_back('\r');
            break;

        case 't':
            result.push_back('\t');
            break;

        case '0':
            result.push_back(0);
            break;

        case '\\':
            result.push_back('\\');
            break;

        case '"':
            result.push_back('"');
            break;

        default:
            throw std::runtime_error(
                "Unsupported string escape at line "
                + std::to_string(lineNumber)
            );
        }
    }

    throw std::runtime_error(
        "Unterminated string literal at line "
        + std::to_string(lineNumber)
    );
}

void appendLittleEndian(
    std::vector<std::uint8_t>& output,
    std::uint64_t value,
    std::size_t width
) {
    for (
        std::size_t index = 0;
        index < width;
        ++index
    ) {
        output.push_back(
            static_cast<std::uint8_t>(
                (value >> (8 * index))
                & 0xFFu
            )
        );
    }
}

std::vector<std::uint8_t>
parseNumericDirective(
    const std::string& arguments,
    std::size_t width,
    std::int64_t minimum,
    std::uint64_t maximum,
    std::size_t lineNumber
) {
    const std::vector<std::string> values =
        splitOperands(arguments);

    if (values.empty()) {
        throw std::runtime_error(
            "Data directive requires at least "
            "one value at line "
            + std::to_string(lineNumber)
        );
    }

    std::vector<std::uint8_t> result;

    if (
        values.size()
        > std::numeric_limits<std::size_t>::max()
            / width
    ) {
        throw std::runtime_error(
            "Data directive size overflow at line "
            + std::to_string(lineNumber)
        );
    }

    result.reserve(values.size() * width);

    for (const std::string& valueText : values) {
        const std::int64_t value =
            parseInteger(
                valueText,
                lineNumber
            );

        if (
            value < minimum
            || (
                value >= 0
                && static_cast<std::uint64_t>(
                    value
                ) > maximum
            )
        ) {
            throw std::runtime_error(
                "Data value out of range '"
                + valueText
                + "' at line "
                + std::to_string(lineNumber)
            );
        }

        std::uint64_t raw = 0;
        std::memcpy(
            &raw,
            &value,
            sizeof(value)
        );

        appendLittleEndian(
            result,
            raw,
            width
        );
    }

    return result;
}

std::vector<std::uint8_t> parseDataDirective(
    const std::string& line,
    std::size_t lineNumber
) {
    std::istringstream input(line);

    std::string directive;
    input >> directive;

    std::string arguments;
    std::getline(input, arguments);
    arguments = trim(arguments);

    const std::string lowered =
        lowerCopy(directive);

    if (lowered == ".byte") {
        return parseNumericDirective(
            arguments,
            1,
            -128,
            255,
            lineNumber
        );
    }

    if (lowered == ".word") {
        return parseNumericDirective(
            arguments,
            2,
            -32768,
            65535,
            lineNumber
        );
    }

    if (lowered == ".qword") {
        return parseNumericDirective(
            arguments,
            8,
            std::numeric_limits<
                std::int64_t
            >::min(),
            std::numeric_limits<
                std::uint64_t
            >::max(),
            lineNumber
        );
    }

    if (lowered == ".string") {
        return parseStringLiteral(
            arguments,
            lineNumber
        );
    }

    throw std::runtime_error(
        "Unknown data directive '"
        + directive
        + "' at line "
        + std::to_string(lineNumber)
    );
}

bool isSectionDirective(
    const std::string& line
) {
    const std::string lowered =
        lowerCopy(trim(line));

    return lowered == ".text"
        || lowered == ".data";
}

AssemblySection sectionFromDirective(
    const std::string& line
) {
    return lowerCopy(trim(line)) == ".data"
        ? AssemblySection::Data
        : AssemblySection::Text;
}

bool isEntryDirective(
    const std::string& line
) {
    std::istringstream input(trim(line));

    std::string directive;
    input >> directive;

    return lowerCopy(directive) == ".entry";
}

std::string parseEntryDirective(
    const std::string& line,
    std::size_t lineNumber
) {
    std::istringstream input(trim(line));

    std::string directive;
    std::string label;
    std::string extra;

    input >> directive;
    input >> label;
    input >> extra;

    if (
        lowerCopy(directive) != ".entry"
        || label.empty()
        || !extra.empty()
    ) {
        throw std::runtime_error(
            ".entry requires exactly one code label "
            "at line "
            + std::to_string(lineNumber)
        );
    }

    requireValidLabelName(
        label,
        lineNumber
    );

    return label;
}

void requireUniqueSymbol(
    const std::string& label,
    std::size_t lineNumber,
    const SymbolPassResult& symbols
) {
    if (
        symbols.code_labels.find(label)
            != symbols.code_labels.end()
        || symbols.data_labels.find(label)
            != symbols.data_labels.end()
    ) {
        throw std::runtime_error(
            "Duplicate label '"
            + label
            + "' at line "
            + std::to_string(lineNumber)
        );
    }
}

SymbolPassResult collectSymbols(
    const std::vector<std::string>& lines
) {
    SymbolPassResult symbols;

    AssemblySection section =
        AssemblySection::Text;

    std::size_t instructionIndex = 0;
    std::size_t dataOffset = 0;

    for (
        std::size_t index = 0;
        index < lines.size();
        ++index
    ) {
        const std::size_t lineNumber =
            index + 1;

        std::string line = trim(
            removeComment(lines[index])
        );

        std::string label;
        bool hadLabel = false;

        while (tryConsumeLabel(line, label)) {
            hadLabel = true;

            requireValidLabelName(
                label,
                lineNumber
            );

            requireUniqueSymbol(
                label,
                lineNumber,
                symbols
            );

            if (
                section
                == AssemblySection::Text
            ) {
                symbols.code_labels[label] =
                    instructionIndex;
            } else {
                symbols.data_labels[label] =
                    memory_map::kUserDataBase
                    + dataOffset;
            }

            line = trim(line);
        }

        if (line.empty()) {
            continue;
        }

        if (isSectionDirective(line)) {
            if (hadLabel) {
                throw std::runtime_error(
                    "Section directive cannot share "
                    "a line with a label at line "
                    + std::to_string(lineNumber)
                );
            }

            section =
                sectionFromDirective(line);

            continue;
        }

        if (isEntryDirective(line)) {
            if (hadLabel) {
                throw std::runtime_error(
                    ".entry cannot share a line "
                    "with a label at line "
                    + std::to_string(lineNumber)
                );
            }

            if (symbols.has_entry) {
                throw std::runtime_error(
                    "Duplicate .entry directive at line "
                    + std::to_string(lineNumber)
                    + "; first declared at line "
                    + std::to_string(
                        symbols.entry_line
                    )
                );
            }

            symbols.has_entry = true;
            symbols.entry_label =
                parseEntryDirective(
                    line,
                    lineNumber
                );
            symbols.entry_line = lineNumber;

            continue;
        }

        if (
            section
            == AssemblySection::Data
        ) {
            if (line.front() != '.') {
                throw std::runtime_error(
                    "Instruction is not allowed in "
                    ".data at line "
                    + std::to_string(lineNumber)
                );
            }

            const std::vector<std::uint8_t>
                bytes = parseDataDirective(
                    line,
                    lineNumber
                );

            if (
                bytes.size()
                > memory_map::kUserDataSize
                    - dataOffset
            ) {
                throw std::runtime_error(
                    "Initialized data exceeds "
                    "the User data region at line "
                    + std::to_string(lineNumber)
                );
            }

            dataOffset += bytes.size();
            continue;
        }

        if (line.front() == '.') {
            throw std::runtime_error(
                "Data directive is only allowed in "
                ".data at line "
                + std::to_string(lineNumber)
            );
        }

        ++instructionIndex;
    }

    symbols.data_size = dataOffset;

    if (symbols.has_entry) {
        const auto dataEntry =
            symbols.data_labels.find(
                symbols.entry_label
            );

        if (
            dataEntry
            != symbols.data_labels.end()
        ) {
            throw std::runtime_error(
                "Executable entry label '"
                + symbols.entry_label
                + "' refers to initialized data"
            );
        }

        const auto codeEntry =
            symbols.code_labels.find(
                symbols.entry_label
            );

        if (
            codeEntry
            == symbols.code_labels.end()
        ) {
            throw std::runtime_error(
                "Undefined executable entry label: "
                + symbols.entry_label
            );
        }

        symbols.entry_instruction =
            codeEntry->second;
    }

    return symbols;
}

Operand parseOperand(
    const std::string& text,
    std::size_t lineNumber,
    const std::unordered_map<
        std::string,
        std::size_t
    >& dataLabels
) {
    const std::string operand = trim(text);

    if (operand.empty()) {
        throw std::runtime_error(
            "Empty operand at line "
            + std::to_string(lineNumber)
        );
    }

    if (isRegisterToken(operand)) {
        return Operand::registerOperand(
            parseRegister(
                operand,
                lineNumber
            )
        );
    }

    if (
        operand.size() >= 3
        && operand.front() == '['
        && operand.back() == ']'
    ) {
        const std::string inner = trim(
            operand.substr(
                1,
                operand.size() - 2
            )
        );

        if (inner.empty()) {
            throw std::runtime_error(
                "Empty memory address at line "
                + std::to_string(lineNumber)
            );
        }

        if (isRegisterToken(inner)) {
            return Operand::
                registerIndirectAddress(
                    parseRegister(
                        inner,
                        lineNumber
                    )
                );
        }

        std::int64_t address = 0;

        if (tryParseInteger(inner, address)) {
            if (address < 0) {
                throw std::runtime_error(
                    "Memory address cannot be "
                    "negative at line "
                    + std::to_string(lineNumber)
                );
            }

            return Operand::memoryAddress(
                static_cast<std::size_t>(
                    address
                )
            );
        }

        requireValidLabelName(
            inner,
            lineNumber
        );

        const auto found =
            dataLabels.find(inner);

        if (found == dataLabels.end()) {
            throw std::runtime_error(
                "Undefined data label '"
                + inner
                + "' at line "
                + std::to_string(lineNumber)
            );
        }

        return Operand::memoryAddress(
            found->second
        );
    }

    std::int64_t immediate = 0;

    if (
        tryParseInteger(
            operand,
            immediate
        )
    ) {
        return Operand::immediate(immediate);
    }

    requireValidLabelName(
        operand,
        lineNumber
    );

    if (
        dataLabels.find(operand)
        != dataLabels.end()
    ) {
        throw std::runtime_error(
            "Data label '"
            + operand
            + "' must be used as ["
            + operand
            + "] at line "
            + std::to_string(lineNumber)
        );
    }

    return Operand::label(operand);
}

Instruction parseInstructionLine(
    const std::string& line,
    std::size_t lineNumber,
    const std::unordered_map<
        std::string,
        std::size_t
    >& dataLabels
) {
    std::istringstream input(line);

    std::string opcodeText;
    input >> opcodeText;

    if (opcodeText.empty()) {
        throw std::runtime_error(
            "Missing opcode at line "
            + std::to_string(lineNumber)
        );
    }

    const Opcode opcode =
        opcodeFromString(opcodeText);

    if (!isValidOpcode(opcode)) {
        throw std::runtime_error(
            "Invalid opcode '"
            + opcodeText
            + "' at line "
            + std::to_string(lineNumber)
        );
    }

    std::string operandText;
    std::getline(input, operandText);
    operandText = trim(operandText);

    if (operandText.empty()) {
        return Instruction(opcode);
    }

    const std::vector<std::string>
        operands = splitOperands(
            operandText
        );

    if (operands.size() == 1) {
        return Instruction(
            opcode,
            parseOperand(
                operands[0],
                lineNumber,
                dataLabels
            )
        );
    }

    if (operands.size() == 2) {
        return Instruction(
            opcode,
            parseOperand(
                operands[0],
                lineNumber,
                dataLabels
            ),
            parseOperand(
                operands[1],
                lineNumber,
                dataLabels
            )
        );
    }

    throw std::runtime_error(
        "Too many operands at line "
        + std::to_string(lineNumber)
        + ": "
        + line
    );
}

AssembledProgram emitProgram(
    const std::vector<std::string>& lines,
    SymbolPassResult symbols
) {
    AssembledProgram result;

    result.labels =
        std::move(symbols.code_labels);

    result.data_labels =
        std::move(symbols.data_labels);

    result.has_explicit_entry =
        symbols.has_entry;

    result.entry_label =
        std::move(symbols.entry_label);

    result.entry_instruction =
        symbols.entry_instruction;

    result.data_base =
        memory_map::kUserDataBase;

    result.data.reserve(
        symbols.data_size
    );

    AssemblySection section =
        AssemblySection::Text;

    for (
        std::size_t index = 0;
        index < lines.size();
        ++index
    ) {
        const std::size_t lineNumber =
            index + 1;

        std::string line = trim(
            removeComment(lines[index])
        );

        std::string label;

        while (tryConsumeLabel(line, label)) {
            line = trim(line);
        }

        if (line.empty()) {
            continue;
        }

        if (isSectionDirective(line)) {
            section =
                sectionFromDirective(line);

            continue;
        }

        if (isEntryDirective(line)) {
            continue;
        }

        if (
            section
            == AssemblySection::Data
        ) {
            const std::vector<std::uint8_t>
                bytes = parseDataDirective(
                    line,
                    lineNumber
                );

            result.data.insert(
                result.data.end(),
                bytes.begin(),
                bytes.end()
            );

            continue;
        }

        result.instructions.push_back(
            parseInstructionLine(
                line,
                lineNumber,
                result.data_labels
            )
        );
    }

    return result;
}

} // namespace

std::size_t
AssembledProgram::resolvedEntryInstruction() const {
    return entry_instruction;
}

binary::BinaryProgram
AssembledProgram::toBinaryProgram() const {
    return toBinaryProgram(
        resolvedEntryInstruction()
    );
}

binary::BinaryProgram
AssembledProgram::toBinaryProgram(
    std::size_t entryInstruction
) const {
    if (instructions.empty()) {
        throw std::runtime_error(
            "Cannot emit an executable with "
            "an empty text section"
        );
    }

    if (
        entryInstruction
        >= instructions.size()
    ) {
        throw std::runtime_error(
            "Executable entry instruction is "
            "outside the text section"
        );
    }

    InstructionEncoder encoder;

    binary::BinaryProgram program;

    program.code = encoder.encodeProgram(
        instructions,
        labels
    );

    program.data = data;

    if (
        program.code.size()
        > std::numeric_limits<
            std::uint32_t
        >::max()
        || program.data.size()
            > std::numeric_limits<
                std::uint32_t
            >::max()
        || data_base
            > std::numeric_limits<
                std::uint32_t
            >::max()
    ) {
        throw std::runtime_error(
            "Assembled executable exceeds "
            "binary format limits"
        );
    }

    program.header.major_version =
        binary::kMajorVersion;

    program.header.minor_version =
        binary::kMinorVersion;

    program.header.endianness =
        binary::BinaryEndianness::Little;

    program.header.entry_point =
        static_cast<std::uint32_t>(
            binary::instructionIndexToAddress(
                entryInstruction
            )
        );

    program.header.code_size =
        static_cast<std::uint32_t>(
            program.code.size()
        );

    program.header.data_base =
        static_cast<std::uint32_t>(
            data_base
        );

    program.header.data_size =
        static_cast<std::uint32_t>(
            program.data.size()
        );

    return program;
}

AssembledProgram Assembler::assembleFile(
    const std::string& path
) const {
    std::ifstream input(path);

    if (!input) {
        throw std::runtime_error(
            "Failed to open assembly file: "
            + path
        );
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();

    return assembleString(buffer.str());
}

AssembledProgram Assembler::assembleString(
    const std::string& source
) const {
    const std::vector<std::string> lines =
        splitLines(source);

    SymbolPassResult symbols =
        collectSymbols(lines);

    return emitProgram(
        lines,
        std::move(symbols)
    );
}

} // namespace zero_cpu
