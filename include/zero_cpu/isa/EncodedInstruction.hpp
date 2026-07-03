#pragma once

#include "zero_cpu/isa/Opcode.hpp"

#include <cstdint>
#include <string>

namespace zero_cpu {

enum class EncodedOperandType : std::uint8_t {
    None = 0x00,
    Register = 0x01,
    Immediate = 0x02,
    MemoryAddress = 0x03,
    CodeAddress = 0x04
};

struct DecodedInstruction {
    Opcode opcode = Opcode::Invalid;

    EncodedOperandType dst_type = EncodedOperandType::None;
    EncodedOperandType src_type = EncodedOperandType::None;

    std::int64_t dst_payload = 0;
    std::int64_t src_payload = 0;
};

std::uint8_t encodeOpcode(Opcode opcode);
Opcode decodeOpcode(std::uint8_t byte);

std::string toString(EncodedOperandType type);

} // namespace zero_cpu