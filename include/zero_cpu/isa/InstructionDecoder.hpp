#pragma once

#include "zero_cpu/isa/EncodedInstruction.hpp"

#include <cstdint>
#include <vector>

namespace zero_cpu {

class InstructionDecoder {
public:
    DecodedInstruction decodeInstruction(
        const std::vector<std::uint8_t>& bytes
    ) const;

private:
    EncodedOperandType decodeOperandType(std::uint8_t byte) const;

    std::int64_t readI64LittleEndian(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset
    ) const;
};

} // namespace zero_cpu