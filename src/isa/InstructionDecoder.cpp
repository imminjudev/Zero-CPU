#include "zero_cpu/isa/InstructionDecoder.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"

#include <cstring>
#include <stdexcept>

namespace zero_cpu {

Opcode decodeOpcode(std::uint8_t byte) {
    switch (byte) {
    case 0x00:
        return Opcode::NOP;
    case 0x01:
        return Opcode::HALT;

    case 0x10:
        return Opcode::MOV;
    case 0x11:
        return Opcode::LOAD;
    case 0x12:
        return Opcode::STORE;

    case 0x20:
        return Opcode::ADD;
    case 0x21:
        return Opcode::SUB;
    case 0x22:
        return Opcode::MUL;
    case 0x23:
        return Opcode::DIV;

    case 0x30:
        return Opcode::CMP;
    case 0x31:
        return Opcode::TEST;

    case 0x40:
        return Opcode::JMP;
    case 0x41:
        return Opcode::JE;
    case 0x42:
        return Opcode::JNE;
    case 0x43:
        return Opcode::JG;
    case 0x44:
        return Opcode::JL;

    case 0x50:
        return Opcode::PUSH;
    case 0x51:
        return Opcode::POP;
    case 0x52:
        return Opcode::CALL;
    case 0x53:
        return Opcode::RET;
    case 0x54:
        return Opcode::IRET;

    case 0x60:
        return Opcode::AND;
    case 0x61:
        return Opcode::OR;
    case 0x62:
        return Opcode::XOR;
    case 0x63:
        return Opcode::NOT;

    default:
        return Opcode::Invalid;
    }
}

DecodedInstruction InstructionDecoder::decodeInstruction(
    const std::vector<std::uint8_t>& bytes
) const {
    if (bytes.size() != binary::kInstructionSize) {
        throw std::runtime_error("Invalid encoded instruction size");
    }

    if (bytes[3] != 0x00) {
        throw std::runtime_error("Instruction reserved byte must be zero");
    }

    if (bytes[20] != 0x00 ||
        bytes[21] != 0x00 ||
        bytes[22] != 0x00 ||
        bytes[23] != 0x00) {
        throw std::runtime_error("Instruction reserved tail bytes must be zero");
    }

    DecodedInstruction instruction;

    instruction.opcode = decodeOpcode(bytes[0]);
    if (instruction.opcode == Opcode::Invalid) {
        throw std::runtime_error("Invalid opcode byte");
    }

    instruction.dst_type = decodeOperandType(bytes[1]);
    instruction.src_type = decodeOperandType(bytes[2]);

    instruction.dst_payload = readI64LittleEndian(bytes, 4);
    instruction.src_payload = readI64LittleEndian(bytes, 12);

    return instruction;
}

EncodedOperandType InstructionDecoder::decodeOperandType(
    std::uint8_t byte
) const {
    switch (byte) {
    case 0x00:
        return EncodedOperandType::None;
    case 0x01:
        return EncodedOperandType::Register;
    case 0x02:
        return EncodedOperandType::Immediate;
    case 0x03:
        return EncodedOperandType::MemoryAddress;
    case 0x04:
        return EncodedOperandType::CodeAddress;
    default:
        throw std::runtime_error("Invalid encoded operand type");
    }
}

std::int64_t InstructionDecoder::readI64LittleEndian(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset
) const {
    if (offset + 8 > bytes.size()) {
        throw std::out_of_range("readI64LittleEndian out of range");
    }

    std::uint64_t raw = 0;

    for (std::size_t i = 0; i < 8; ++i) {
        raw |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8);
    }

    std::int64_t value = 0;
    std::memcpy(&value, &raw, sizeof(value));

    return value;
}

} // namespace zero_cpu