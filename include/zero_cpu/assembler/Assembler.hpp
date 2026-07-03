#pragma once

#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/core/RegisterFile.hpp"

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace zero_cpu {

struct AssembledProgram {
    std::vector<Instruction> instructions;
    std::unordered_map<std::string, std::size_t> labels;
};

class Assembler {
public:
    AssembledProgram assembleFile(const std::string& path) const;
    AssembledProgram assembleString(const std::string& source) const;

private:
    static std::vector<std::string> splitLines(const std::string& source);

    static std::string removeComment(const std::string& line);
    static std::string trim(const std::string& text);

    static bool tryConsumeLabel(std::string& line, std::string& label);

    static void collectLabels(
        const std::vector<std::string>& lines,
        std::unordered_map<std::string, std::size_t>& labels
    );

    static Instruction parseInstructionLine(
        const std::string& line,
        std::size_t lineNumber
    );

    static std::vector<std::string> splitOperands(const std::string& text);

    static Operand parseOperand(
        const std::string& text,
        std::size_t lineNumber
    );

    static bool isRegisterToken(const std::string& text);

    static RegisterName parseRegister(
        const std::string& text,
        std::size_t lineNumber
    );

    static bool isIntegerToken(const std::string& text);

    static std::int64_t parseInteger(
        const std::string& text,
        std::size_t lineNumber
    );

    static bool isValidLabelName(const std::string& text);

    static void requireValidLabelName(
        const std::string& text,
        std::size_t lineNumber
    );
};

} // namespace zero_cpu