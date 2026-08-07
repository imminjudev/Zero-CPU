#include "zero_cpu/debug/DebugInspector.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/core/Flags.hpp"
#include "zero_cpu/core/Memory.hpp"
#include "zero_cpu/isa/InstructionDecoder.hpp"

#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace zero_cpu::debug {
namespace {

RegisterName registerFromPayload(
    std::int64_t payload
) {
    if (
        payload < 0
        || payload
            >= static_cast<std::int64_t>(
                RegisterFile::kRegisterCount
            )
    ) {
        throw std::runtime_error(
            "Disassembler register payload "
            "is out of range"
        );
    }

    return static_cast<RegisterName>(
        payload
    );
}

std::string registerName(
    RegisterName reg
) {
    return "R"
        + std::to_string(
            static_cast<std::size_t>(reg)
        );
}

} // namespace

RegisterInspection
DebugInspector::inspectRegisters(
    const DebugSession& session
) {
    const CPUState& state =
        session.cpu().state();

    RegisterInspection inspection;

    for (
        std::size_t index = 0;
        index < RegisterFile::kRegisterCount;
        ++index
    ) {
        inspection.registers[index] =
            state.registers().get(
                static_cast<RegisterName>(
                    index
                )
            );
    }

    inspection.pc = state.pc();
    inspection.sp = state.sp();

    inspection.flags =
        state.flags().raw();

    inspection.zero =
        state.flags().zero();

    inspection.sign =
        state.flags().sign();

    inspection.carry =
        state.flags().carry();

    inspection.overflow =
        state.flags().overflow();

    inspection.privilege =
        state.privilegeLevel();

    return inspection;
}

MemoryInspection
DebugInspector::inspectMemory(
    const DebugSession& session,
    std::size_t address,
    std::size_t count
) {
    if (count == 0) {
        throw std::runtime_error(
            "Memory inspection count must "
            "be greater than zero"
        );
    }

    const Memory& memory =
        session.cpu().state().memory();

    if (
        address > memory.size()
        || count > memory.size() - address
    ) {
        throw std::runtime_error(
            "Memory inspection range is "
            "outside process memory"
        );
    }

    MemoryInspection inspection;
    inspection.address = address;

    inspection.bytes =
        memory.readBytes(
            address,
            count
        );

    return inspection;
}

std::int64_t DebugInspector::inspectQword(
    const DebugSession& session,
    std::size_t address
) {
    (void)inspectMemory(
        session,
        address,
        sizeof(std::int64_t)
    );

    return session.cpu()
        .state()
        .memory()
        .readI64(address);
}

std::vector<DisassembledInstruction>
DebugInspector::disassemble(
    const DebugSession& session,
    std::size_t address,
    std::size_t instructionCount
) {
    if (instructionCount == 0) {
        throw std::runtime_error(
            "Disassembly instruction count "
            "must be greater than zero"
        );
    }

    const kernel::ExecutableMetadata& metadata =
        session.metadata();

    if (
        address < metadata.code_base
        || address
            >= metadata.code_end_exclusive
    ) {
        throw std::runtime_error(
            "Disassembly address is outside "
            "the executable code section"
        );
    }

    if (
        (address - metadata.code_base)
            % binary::kInstructionSize
        != 0
    ) {
        throw std::runtime_error(
            "Disassembly address is not "
            "instruction-aligned"
        );
    }

    std::vector<DisassembledInstruction>
        result;

    result.reserve(instructionCount);

    InstructionDecoder decoder;

    const Memory& memory =
        session.cpu().state().memory();

    std::size_t current = address;

    while (
        result.size() < instructionCount
        && current
            < metadata.code_end_exclusive
    ) {
        if (
            metadata.code_end_exclusive
                - current
            < binary::kInstructionSize
        ) {
            throw std::runtime_error(
                "Executable code section ends "
                "with a partial instruction"
            );
        }

        const DecodedInstruction decoded =
            decoder.decodeInstruction(
                memory.readBytes(
                    current,
                    binary::kInstructionSize
                )
            );

        DisassembledInstruction item;
        item.address = current;

        item.opcode = decoded.opcode;

        item.dst_type =
            decoded.dst_type;

        item.src_type =
            decoded.src_type;

        item.dst_payload =
            decoded.dst_payload;

        item.src_payload =
            decoded.src_payload;

        item.text =
            formatInstruction(
                session,
                decoded
            );

        item.current_pc =
            session.cpu().state().pc()
            == current;

        item.breakpoint =
            session.hasBreakpoint(
                current
            );

        result.push_back(
            std::move(item)
        );

        current +=
            binary::kInstructionSize;
    }

    return result;
}

std::string DebugInspector::formatRegisters(
    const RegisterInspection& inspection
) {
    std::ostringstream output;

    for (
        std::size_t index = 0;
        index < inspection.registers.size();
        ++index
    ) {
        if (index != 0) {
            output << " ";
        }

        output
            << "R"
            << index
            << "="
            << inspection.registers[index];
    }

    output
        << "\nPC="
        << inspection.pc
        << " SP="
        << inspection.sp
        << " FLAGS=0x"
        << std::hex
        << std::uppercase
        << inspection.flags
        << std::dec
        << " PRIV="
        << privilegeLevelToString(
            inspection.privilege
        );

    output
        << "\nZF="
        << (inspection.zero ? 1 : 0)
        << " SF="
        << (inspection.sign ? 1 : 0)
        << " CF="
        << (inspection.carry ? 1 : 0)
        << " OF="
        << (inspection.overflow ? 1 : 0);

    return output.str();
}

std::string DebugInspector::formatMemory(
    const MemoryInspection& inspection
) {
    std::ostringstream output;

    output
        << "Memory["
        << inspection.address
        << ".."
        << (
            inspection.address
            + inspection.bytes.size()
        )
        << "):";

    for (
        const std::uint8_t byte :
        inspection.bytes
    ) {
        output
            << " "
            << std::hex
            << std::uppercase
            << std::setw(2)
            << std::setfill('0')
            << static_cast<unsigned int>(
                byte
            );
    }

    return output.str();
}

std::string
DebugInspector::formatDisassembly(
    const std::vector<
        DisassembledInstruction
    >& instructions
) {
    std::ostringstream output;

    for (
        std::size_t index = 0;
        index < instructions.size();
        ++index
    ) {
        const DisassembledInstruction& item =
            instructions[index];

        output
            << (
                item.current_pc
                    ? "=>"
                    : "  "
            )
            << (
                item.breakpoint
                    ? "* "
                    : "  "
            )
            << item.address
            << ": "
            << item.text;

        if (
            index + 1
            < instructions.size()
        ) {
            output << "\n";
        }
    }

    return output.str();
}

std::string DebugInspector::formatInstruction(
    const DebugSession& session,
    const DecodedInstruction& instruction
) {
    std::ostringstream output;

    output
        << opcodeToString(
            instruction.opcode
        );

    const std::string dst =
        formatOperand(
            session,
            instruction.dst_type,
            instruction.dst_payload
        );

    const std::string src =
        formatOperand(
            session,
            instruction.src_type,
            instruction.src_payload
        );

    if (!dst.empty()) {
        output
            << " "
            << dst;
    }

    if (!src.empty()) {
        output
            << ", "
            << src;
    }

    return output.str();
}

std::string DebugInspector::formatOperand(
    const DebugSession& session,
    EncodedOperandType type,
    std::int64_t payload
) {
    switch (type) {
    case EncodedOperandType::None:
        if (payload != 0) {
            throw std::runtime_error(
                "None operand has non-zero payload"
            );
        }

        return "";

    case EncodedOperandType::Register:
        return registerName(
            registerFromPayload(payload)
        );

    case EncodedOperandType::Immediate:
        return std::to_string(payload);

    case EncodedOperandType::MemoryAddress:
        if (payload < 0) {
            throw std::runtime_error(
                "Disassembler memory address "
                "is negative"
            );
        }

        return "["
            + std::to_string(payload)
            + "]";

    case EncodedOperandType::RegisterIndirectAddress:
        return "["
            + registerName(
                registerFromPayload(payload)
            )
            + "]";

    case EncodedOperandType::CodeAddress: {
        if (payload < 0) {
            throw std::runtime_error(
                "Disassembler code offset "
                "is negative"
            );
        }

        const std::size_t base =
            session.metadata().code_base;

        const std::uint64_t offset =
            static_cast<std::uint64_t>(
                payload
            );

        if (
            offset
            > static_cast<std::uint64_t>(
                std::numeric_limits<
                    std::size_t
                >::max()
                - base
            )
        ) {
            throw std::runtime_error(
                "Disassembler code address "
                "overflow"
            );
        }

        return std::to_string(
            base
            + static_cast<std::size_t>(
                offset
            )
        );
    }
    }

    throw std::runtime_error(
        "Unknown encoded operand type"
    );
}

} // namespace zero_cpu::debug
