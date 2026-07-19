#include "zero_cpu/isa/InstructionEncoder.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/isa/EncodedInstruction.hpp"

#include <cstring>
#include <stdexcept>

namespace zero_cpu {

std::uint8_t encodeOpcode(Opcode opcode) {
    switch (opcode) {
    case Opcode::NOP:
        return 0x00;
    case Opcode::HALT:
        return 0x01;

    case Opcode::MOV:
        return 0x10;
    case Opcode::LOAD:
        return 0x11;
    case Opcode::STORE:
        return 0x12;

    case Opcode::ADD:
        return 0x20;
    case Opcode::SUB:
        return 0x21;
    case Opcode::MUL:
        return 0x22;
    case Opcode::DIV:
        return 0x23;

    case Opcode::CMP:
        return 0x30;
    case Opcode::TEST:
        return 0x31;

    case Opcode::JMP:
        return 0x40;
    case Opcode::JE:
        return 0x41;
    case Opcode::JNE:
        return 0x42;
    case Opcode::JG:
        return 0x43;
    case Opcode::JL:
        return 0x44;

    case Opcode::PUSH:
        return 0x50;
    case Opcode::POP:
        return 0x51;
    case Opcode::CALL:
        return 0x52;
    case Opcode::RET:
        return 0x53;
    case Opcode::IRET:
        return 0x54;

    case Opcode::AND:
        return 0x60;
    case Opcode::OR:
        return 0x61;
    case Opcode::XOR:
        return 0x62;
    case Opcode::NOT:
        return 0x63;

    case Opcode::Invalid:
    default:
        throw std::runtime_error("Cannot encode invalid opcode");
    }
}

std::string toString(EncodedOperandType type) {
    switch (type) {
    case EncodedOperandType::None:
        return "None";
    case EncodedOperandType::Register:
        return "Register";
    case EncodedOperandType::Immediate:
        return "Immediate";
    case EncodedOperandType::MemoryAddress:
        return "MemoryAddress";
    case EncodedOperandType::CodeAddress:
        return "CodeAddress";
    default:
        return "Unknown";
    }
}

std::vector<std::uint8_t> InstructionEncoder::encodeInstruction(
    const Instruction& instruction,
    const LabelTable& labels
) const {
    std::vector<std::uint8_t> bytes(binary::kInstructionSize, std::uint8_t{0});

    const auto dst = encodeOperand(instruction.dst(), labels);
    const auto src = encodeOperand(instruction.src(), labels);

    bytes[0] = encodeOpcode(instruction.opcode());
    bytes[1] = dst.type;
    bytes[2] = src.type;
    bytes[3] = 0x00;

    writeI64LittleEndian(bytes, 4, dst.payload);
    writeI64LittleEndian(bytes, 12, src.payload);

    // bytes[20..23] reserved = 0
    return bytes;
}

std::vector<std::uint8_t> InstructionEncoder::encodeProgram(
    const std::vector<Instruction>& instructions,
    const LabelTable& labels
) const {
    std::vector<std::uint8_t> result;
    result.reserve(instructions.size() * binary::kInstructionSize);

    for (const auto& instruction : instructions) {
        const auto encoded = encodeInstruction(instruction, labels);
        result.insert(result.end(), encoded.begin(), encoded.end());
    }

    return result;
}

InstructionEncoder::EncodedOperand InstructionEncoder::encodeOperand(
    const Operand& operand,
    const LabelTable& labels
) const {
    switch (operand.type()) {
    case OperandType::None:
        return {
            static_cast<std::uint8_t>(EncodedOperandType::None),
            0
        };

    case OperandType::Register:
        return {
            static_cast<std::uint8_t>(EncodedOperandType::Register),
            static_cast<std::int64_t>(encodeRegister(operand.asRegister()))
        };

    case OperandType::Immediate:
        return {
            static_cast<std::uint8_t>(EncodedOperandType::Immediate),
            operand.asImmediate()
        };

    case OperandType::MemoryAddress:
        return {
            static_cast<std::uint8_t>(EncodedOperandType::MemoryAddress),
            static_cast<std::int64_t>(operand.asMemoryAddress())
        };

    case OperandType::Label: {
        const auto& label = operand.asLabel();
        const auto it = labels.find(label);

        if (it == labels.end()) {
            throw std::runtime_error("Undefined label: " + label);
        }

        const std::size_t instructionIndex = it->second;
        const std::size_t byteAddress =
            instructionIndex * binary::kInstructionSize;

        return {
            static_cast<std::uint8_t>(EncodedOperandType::CodeAddress),
            static_cast<std::int64_t>(byteAddress)
        };
    }

    default:
        throw std::runtime_error("Unknown operand type");
    }
}

std::uint8_t InstructionEncoder::encodeRegister(RegisterName reg) const {
    switch (reg) {
    case RegisterName::R0:
        return 0;
    case RegisterName::R1:
        return 1;
    case RegisterName::R2:
        return 2;
    case RegisterName::R3:
        return 3;
    case RegisterName::R4:
        return 4;
    case RegisterName::R5:
        return 5;
    case RegisterName::R6:
        return 6;
    case RegisterName::R7:
        return 7;
    default:
        throw std::runtime_error("Invalid register");
    }
}

void InstructionEncoder::writeI64LittleEndian(
    std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::int64_t value
) const {
    if (offset + 8 > bytes.size()) {
        throw std::out_of_range("writeI64LittleEndian out of range");
    }

    std::uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof(value));

    for (std::size_t i = 0; i < 8; ++i) {
        bytes[offset + i] =
            static_cast<std::uint8_t>((raw >> (i * 8)) & 0xFF);
    }
}

} // namespace zero_cpu