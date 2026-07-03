#include "zero_cpu/assembler/Assembler.hpp"

#include "zero_cpu/isa/Opcode.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace zero_cpu {

AssembledProgram Assembler::assembleFile(const std::string& path) const {
    std::ifstream input(path);

    if (!input) {
        throw std::runtime_error("Failed to open assembly file: " + path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();

    return assembleString(buffer.str());
}

AssembledProgram Assembler::assembleString(const std::string& source) const {
    const auto lines = splitLines(source);

    AssembledProgram result;

    collectLabels(lines, result.labels);

    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string line = trim(removeComment(lines[i]));
        std::string label;

        while (tryConsumeLabel(line, label)) {
            line = trim(line);
        }

        if (line.empty()) {
            continue;
        }

        result.instructions.push_back(
            parseInstructionLine(line, i + 1)
        );
    }

    return result;
}

std::vector<std::string> Assembler::splitLines(const std::string& source) {
    std::vector<std::string> lines;
    std::istringstream input(source);
    std::string line;

    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    return lines;
}

std::string Assembler::removeComment(const std::string& line) {
    const auto commentPosition = line.find(';');

    if (commentPosition == std::string::npos) {
        return line;
    }

    return line.substr(0, commentPosition);
}

std::string Assembler::trim(const std::string& text) {
    const auto first = std::find_if_not(
        text.begin(),
        text.end(),
        [](unsigned char ch) {
            return std::isspace(ch);
        }
    );

    if (first == text.end()) {
        return "";
    }

    const auto last = std::find_if_not(
        text.rbegin(),
        text.rend(),
        [](unsigned char ch) {
            return std::isspace(ch);
        }
    ).base();

    return std::string(first, last);
}

bool Assembler::tryConsumeLabel(std::string& line, std::string& label) {
    line = trim(line);

    const auto colonPosition = line.find(':');

    if (colonPosition == std::string::npos) {
        return false;
    }

    std::string candidate = trim(line.substr(0, colonPosition));

    if (!isValidLabelName(candidate)) {
        return false;
    }

    label = candidate;
    line = trim(line.substr(colonPosition + 1));

    return true;
}

void Assembler::collectLabels(
    const std::vector<std::string>& lines,
    std::unordered_map<std::string, std::size_t>& labels
) {
    std::size_t instructionAddress = 0;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string line = trim(removeComment(lines[i]));
        std::string label;

        while (tryConsumeLabel(line, label)) {
            requireValidLabelName(label, i + 1);

            if (labels.find(label) != labels.end()) {
                throw std::runtime_error(
                    "Duplicate label '" + label
                    + "' at line "
                    + std::to_string(i + 1)
                );
            }

            labels[label] = instructionAddress;
        }

        if (!line.empty()) {
            ++instructionAddress;
        }
    }
}

Instruction Assembler::parseInstructionLine(
    const std::string& line,
    std::size_t lineNumber
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

    const Opcode opcode = opcodeFromString(opcodeText);

    if (!isValidOpcode(opcode)) {
        throw std::runtime_error(
            "Invalid opcode '" + opcodeText
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

    const auto operands = splitOperands(operandText);

    if (operands.size() == 1) {
        return Instruction(
            opcode,
            parseOperand(operands[0], lineNumber)
        );
    }

    if (operands.size() == 2) {
        return Instruction(
            opcode,
            parseOperand(operands[0], lineNumber),
            parseOperand(operands[1], lineNumber)
        );
    }

    throw std::runtime_error(
        "Too many operands at line "
        + std::to_string(lineNumber)
        + ": "
        + line
    );
}

std::vector<std::string> Assembler::splitOperands(const std::string& text) {
    std::vector<std::string> operands;
    std::string current;

    bool insideMemoryAddress = false;

    for (char ch : text) {
        if (ch == '[') {
            insideMemoryAddress = true;
            current.push_back(ch);
            continue;
        }

        if (ch == ']') {
            insideMemoryAddress = false;
            current.push_back(ch);
            continue;
        }

        if (ch == ',' && !insideMemoryAddress) {
            operands.push_back(trim(current));
            current.clear();
            continue;
        }

        current.push_back(ch);
    }

    if (!current.empty()) {
        operands.push_back(trim(current));
    }

    return operands;
}

Operand Assembler::parseOperand(
    const std::string& text,
    std::size_t lineNumber
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
            parseRegister(operand, lineNumber)
        );
    }

    if (
        operand.size() >= 3
        && operand.front() == '['
        && operand.back() == ']'
    ) {
        const std::string inner = trim(
            operand.substr(1, operand.size() - 2)
        );

        const std::int64_t address = parseInteger(inner, lineNumber);

        if (address < 0) {
            throw std::runtime_error(
                "Memory address cannot be negative at line "
                + std::to_string(lineNumber)
            );
        }

        return Operand::memoryAddress(
            static_cast<std::size_t>(address)
        );
    }

    if (isIntegerToken(operand)) {
        return Operand::immediate(
            parseInteger(operand, lineNumber)
        );
    }

    requireValidLabelName(operand, lineNumber);

    return Operand::label(operand);
}

bool Assembler::isRegisterToken(const std::string& text) {
    if (text.size() != 2) {
        return false;
    }

    const char first = static_cast<char>(
        std::toupper(static_cast<unsigned char>(text[0]))
    );

    if (first != 'R') {
        return false;
    }

    return text[1] >= '0' && text[1] <= '7';
}

RegisterName Assembler::parseRegister(
    const std::string& text,
    std::size_t lineNumber
) {
    if (!isRegisterToken(text)) {
        throw std::runtime_error(
            "Invalid register '" + text
            + "' at line "
            + std::to_string(lineNumber)
        );
    }

    switch (text[1]) {
    case '0':
        return RegisterName::R0;
    case '1':
        return RegisterName::R1;
    case '2':
        return RegisterName::R2;
    case '3':
        return RegisterName::R3;
    case '4':
        return RegisterName::R4;
    case '5':
        return RegisterName::R5;
    case '6':
        return RegisterName::R6;
    case '7':
        return RegisterName::R7;
    default:
        break;
    }

    throw std::runtime_error(
        "Invalid register '" + text
        + "' at line "
        + std::to_string(lineNumber)
    );
}

bool Assembler::isIntegerToken(const std::string& text) {
    if (text.empty()) {
        return false;
    }

    std::size_t index = 0;

    if (text[index] == '+' || text[index] == '-') {
        ++index;
    }

    if (index >= text.size()) {
        return false;
    }

    for (; index < text.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(text[index]))) {
            return false;
        }
    }

    return true;
}

std::int64_t Assembler::parseInteger(
    const std::string& text,
    std::size_t lineNumber
) {
    if (!isIntegerToken(text)) {
        throw std::runtime_error(
            "Invalid integer '" + text
            + "' at line "
            + std::to_string(lineNumber)
        );
    }

    try {
        return static_cast<std::int64_t>(std::stoll(text));
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Integer out of range '" + text
            + "' at line "
            + std::to_string(lineNumber)
        );
    }
}

bool Assembler::isValidLabelName(const std::string& text) {
    if (text.empty()) {
        return false;
    }

    const unsigned char first =
        static_cast<unsigned char>(text.front());

    if (!std::isalpha(first) && text.front() != '_') {
        return false;
    }

    for (char ch : text) {
        const unsigned char current =
            static_cast<unsigned char>(ch);

        if (!std::isalnum(current) && ch != '_') {
            return false;
        }
    }

    return true;
}

void Assembler::requireValidLabelName(
    const std::string& text,
    std::size_t lineNumber
) {
    if (!isValidLabelName(text)) {
        throw std::runtime_error(
            "Invalid label name '" + text
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

    if (isValidOpcode(opcodeFromString(text))) {
        throw std::runtime_error(
            "Label name cannot be opcode name '"
            + text
            + "' at line "
            + std::to_string(lineNumber)
        );
    }
}

} // namespace zero_cpu