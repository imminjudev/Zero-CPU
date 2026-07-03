#pragma once

#include "zero_cpu/isa/Instruction.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace zero_cpu {

class InstructionEncoder {
public:
    using LabelTable = std::unordered_map<std::string, std::size_t>;

    std::vector<std::uint8_t> encodeInstruction(
        const Instruction& instruction,
        const LabelTable& labels
    ) const;

    std::vector<std::uint8_t> encodeProgram(
        const std::vector<Instruction>& instructions,
        const LabelTable& labels
    ) const;

private:
    struct EncodedOperand {
        std::uint8_t type = 0;
        std::int64_t payload = 0;
    };

    EncodedOperand encodeOperand(
        const Operand& operand,
        const LabelTable& labels
    ) const;

    std::uint8_t encodeRegister(RegisterName reg) const;

    void writeI64LittleEndian(
        std::vector<std::uint8_t>& bytes,
        std::size_t offset,
        std::int64_t value
    ) const;
};

} // namespace zero_cpu