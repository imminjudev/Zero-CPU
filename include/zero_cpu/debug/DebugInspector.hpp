#pragma once

#include "zero_cpu/core/PrivilegeLevel.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/debug/DebugSession.hpp"
#include "zero_cpu/isa/EncodedInstruction.hpp"
#include "zero_cpu/isa/Opcode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zero_cpu::debug {

struct RegisterInspection {
    std::array<
        std::int64_t,
        RegisterFile::kRegisterCount
    > registers{};

    std::size_t pc = 0;
    std::size_t sp = 0;

    std::uint32_t flags = 0;

    bool zero = false;
    bool sign = false;
    bool carry = false;
    bool overflow = false;

    PrivilegeLevel privilege =
        PrivilegeLevel::User;
};

struct MemoryInspection {
    std::size_t address = 0;
    std::vector<std::uint8_t> bytes;
};

struct DisassembledInstruction {
    std::size_t address = 0;

    Opcode opcode = Opcode::Invalid;

    EncodedOperandType dst_type =
        EncodedOperandType::None;

    EncodedOperandType src_type =
        EncodedOperandType::None;

    std::int64_t dst_payload = 0;
    std::int64_t src_payload = 0;

    std::string text;

    bool current_pc = false;
    bool breakpoint = false;
};

class DebugInspector {
public:
    static RegisterInspection inspectRegisters(
        const DebugSession& session
    );

    static MemoryInspection inspectMemory(
        const DebugSession& session,
        std::size_t address,
        std::size_t count
    );

    static std::int64_t inspectQword(
        const DebugSession& session,
        std::size_t address
    );

    static std::vector<DisassembledInstruction>
    disassemble(
        const DebugSession& session,
        std::size_t address,
        std::size_t instructionCount
    );

    static std::string formatRegisters(
        const RegisterInspection& inspection
    );

    static std::string formatMemory(
        const MemoryInspection& inspection
    );

    static std::string formatDisassembly(
        const std::vector<
            DisassembledInstruction
        >& instructions
    );

private:
    static std::string formatInstruction(
        const DebugSession& session,
        const DecodedInstruction& instruction
    );

    static std::string formatOperand(
        const DebugSession& session,
        EncodedOperandType type,
        std::int64_t payload
    );
};

} // namespace zero_cpu::debug
