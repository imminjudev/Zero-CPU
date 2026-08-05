#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/PrivilegeLevel.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"
#include "zero_cpu/trace/TraceEvent.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

enum class ExecutionMode {
    Vector,
    Binary
};

enum class InterruptKind {
    Hardware,
    Software
};

std::string modeName(ExecutionMode mode) {
    return mode == ExecutionMode::Vector
        ? "vector"
        : "binary";
}

std::string kindName(InterruptKind kind) {
    return kind == InterruptKind::Hardware
        ? "hardware"
        : "software";
}

zero_cpu::binary::BinaryProgram makeBinaryProgram(
    const std::vector<zero_cpu::Instruction>& instructions
) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code =
        encoder.encodeProgram(instructions, {});

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size =
        static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    return program;
}

void loadProgram(
    zero_cpu::CPU& cpu,
    ExecutionMode mode,
    const std::vector<zero_cpu::Instruction>& instructions
) {
    if (mode == ExecutionMode::Vector) {
        cpu.loadProgram(instructions, {});
        return;
    }

    cpu.loadBinaryProgram(makeBinaryProgram(instructions));
}

std::size_t instructionAddress(
    const zero_cpu::CPU& cpu,
    ExecutionMode mode,
    std::size_t index
) {
    if (mode == ExecutionMode::Vector) {
        return index;
    }

    return cpu.binaryCodeBase()
        + index * zero_cpu::binary::kInstructionSize;
}

std::int64_t packedFlags(
    const zero_cpu::Flags& flags
) {
    std::int64_t value = 0;

    if (flags.zero()) {
        value |= 1LL << 0;
    }

    if (flags.sign()) {
        value |= 1LL << 1;
    }

    if (flags.carry()) {
        value |= 1LL << 2;
    }

    if (flags.overflow()) {
        value |= 1LL << 3;
    }

    return value;
}

struct Snapshot {
    std::size_t pc = 0;
    std::size_t sp = 0;
    std::uint32_t flags = 0;
    zero_cpu::PrivilegeLevel privilege =
        zero_cpu::PrivilegeLevel::Kernel;
    std::vector<std::int64_t> registers;
    std::vector<std::int64_t> memory;
};

Snapshot capture(const zero_cpu::CPU& cpu) {
    using namespace zero_cpu;

    Snapshot snapshot;
    snapshot.pc = cpu.state().pc();
    snapshot.sp = cpu.state().sp();
    snapshot.flags = cpu.state().flags().raw();
    snapshot.privilege =
        cpu.state().privilegeLevel();
    snapshot.memory = cpu.state().memory().snapshot();

    snapshot.registers.reserve(
        RegisterFile::kRegisterCount
    );

    for (std::size_t i = 0;
         i < RegisterFile::kRegisterCount;
         ++i) {
        snapshot.registers.push_back(
            cpu.state().registers().get(
                static_cast<RegisterName>(i)
            )
        );
    }

    return snapshot;
}

bool architectureMatches(
    const zero_cpu::CPU& cpu,
    const Snapshot& expected
) {
    using namespace zero_cpu;

    if (
        cpu.state().pc() != expected.pc
        || cpu.state().sp() != expected.sp
        || cpu.state().flags().raw() != expected.flags
        || cpu.state().privilegeLevel()
            != expected.privilege
        || cpu.state().memory().snapshot()
            != expected.memory
    ) {
        return false;
    }

    for (std::size_t i = 0;
         i < RegisterFile::kRegisterCount;
         ++i) {
        if (
            cpu.state().registers().get(
                static_cast<RegisterName>(i)
            )
            != expected.registers[i]
        ) {
            return false;
        }
    }

    return true;
}

bool configurationValidation(std::string& detail) {
    using namespace zero_cpu;

    CPU cpu;

    if (
        cpu.kernelStackPointer()
            != memory_map::kKernelStackBase
        || cpu.usingKernelInterruptStack()
    ) {
        detail = "default Kernel stack state mismatch";
        return false;
    }

    const std::size_t valid =
        memory_map::kKernelStackBase
        + CPU::kStackSlotSize;

    cpu.setKernelStackPointer(valid);

    if (cpu.kernelStackPointer() != valid) {
        detail = "valid Kernel stack pointer was not stored";
        return false;
    }

    bool rejectedBelow = false;
    bool rejectedAbove = false;
    bool rejectedMisaligned = false;

    try {
        cpu.setKernelStackPointer(
            memory_map::kKernelStackBase
            - CPU::kStackSlotSize
        );
    } catch (const std::exception&) {
        rejectedBelow = true;
    }

    try {
        cpu.setKernelStackPointer(
            memory_map::kKernelStackEndExclusive
            + CPU::kStackSlotSize
        );
    } catch (const std::exception&) {
        rejectedAbove = true;
    }

    try {
        cpu.setKernelStackPointer(
            memory_map::kKernelStackBase + 1
        );
    } catch (const std::exception&) {
        rejectedMisaligned = true;
    }

    if (
        !rejectedBelow
        || !rejectedAbove
        || !rejectedMisaligned
    ) {
        detail = "invalid Kernel stack pointer was accepted";
        return false;
    }

    return true;
}

bool userStackBoundaryIsEnforced(
    ExecutionMode mode,
    std::string& detail
) {
    using namespace zero_cpu;

    CPU cpu;
    loadProgram(
        cpu,
        mode,
        {
            Instruction(
                Opcode::PUSH,
                Operand::immediate(123)
            )
        }
    );

    cpu.state().setSp(
        memory_map::kUserStackEndExclusive
    );
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    const Snapshot before = capture(cpu);
    cpu.step();

    if (
        !cpu.state().hasError()
        || cpu.state().errorMessage() != "Stack overflow"
        || !architectureMatches(cpu, before)
        || cpu.traceLogger().size() != 1
        || !cpu.traceLogger().last().hasError()
    ) {
        detail = "User PUSH crossed into Kernel stack";
        return false;
    }

    return true;
}

bool interruptRoundTrip(
    ExecutionMode mode,
    InterruptKind kind,
    std::string& detail
) {
    using namespace zero_cpu;

    const std::vector<Instruction> instructions =
        kind == InterruptKind::Hardware
            ? std::vector<Instruction>{
                Instruction(Opcode::NOP),
                Instruction(Opcode::NOP),
                Instruction(Opcode::IRET)
            }
            : std::vector<Instruction>{
                Instruction(
                    Opcode::INT,
                    Operand::immediate(32)
                ),
                Instruction(Opcode::NOP),
                Instruction(Opcode::IRET)
            };

    const std::size_t handlerIndex = 2;

    CPU cpu;
    loadProgram(cpu, mode, instructions);

    cpu.setUserCodeRange(
        instructionAddress(cpu, mode, 0),
        instructionAddress(cpu, mode, handlerIndex)
    );

    auto controller =
        std::make_shared<InterruptController>();
    controller->setVectorHandler(
        32,
        instructionAddress(cpu, mode, handlerIndex)
    );
    cpu.setInterruptController(controller);

    const std::size_t userSp =
        memory_map::kUserStackBase
        + CPU::kStackSlotSize * 2;
    const std::size_t kernelSp =
        memory_map::kKernelStackBase;

    cpu.state().setSp(userSp);
    cpu.state().flags().setZero(true);
    cpu.state().flags().setCarry(true);
    cpu.state().flags().setOverflow(true);
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    cpu.state().memory().write(
        memory_map::kUserStackBase,
        111
    );
    cpu.state().memory().write(
        memory_map::kUserStackBase
            + CPU::kStackSlotSize,
        222
    );

    const std::int64_t userSlot0 =
        cpu.state().memory().read(
            memory_map::kUserStackBase
        );
    const std::int64_t userSlot1 =
        cpu.state().memory().read(
            memory_map::kUserStackBase
                + CPU::kStackSlotSize
        );
    const std::int64_t savedFlags =
        packedFlags(cpu.state().flags());
    const std::uint32_t originalFlags =
        cpu.state().flags().raw();

    const std::size_t expectedReturn =
        kind == InterruptKind::Hardware
            ? instructionAddress(cpu, mode, 0)
            : instructionAddress(cpu, mode, 1);

    if (kind == InterruptKind::Hardware) {
        controller->request(
            32,
            77,
            "kernel-stack-test"
        );
    }

    cpu.step();

    if (
        cpu.state().hasError()
        || !cpu.state().isKernelMode()
        || !cpu.usingKernelInterruptStack()
        || cpu.state().sp()
            != kernelSp + CPU::kInterruptFrameSize
        || cpu.kernelStackPointer()
            != kernelSp + CPU::kInterruptFrameSize
        || cpu.state().pc()
            != instructionAddress(cpu, mode, handlerIndex)
    ) {
        detail = "interrupt did not switch to Kernel stack";
        return false;
    }

    if (
        cpu.state().memory().read(kernelSp)
            != static_cast<std::int64_t>(expectedReturn)
        || cpu.state().memory().read(
            kernelSp + CPU::kStackSlotSize
        ) != savedFlags
        || cpu.state().memory().read(
            kernelSp + CPU::kStackSlotSize * 2
        ) != privilegeLevelToRaw(PrivilegeLevel::User)
        || cpu.state().memory().read(
            kernelSp + CPU::kStackSlotSize * 3
        ) != static_cast<std::int64_t>(userSp)
    ) {
        detail = "Kernel interrupt frame layout mismatch";
        return false;
    }

    if (
        cpu.state().memory().read(
            memory_map::kUserStackBase
        ) != userSlot0
        || cpu.state().memory().read(
            memory_map::kUserStackBase
                + CPU::kStackSlotSize
        ) != userSlot1
    ) {
        detail = "interrupt modified User stack";
        return false;
    }

    bool rejectedActiveReconfiguration = false;

    try {
        cpu.setKernelStackPointer(kernelSp);
    } catch (const std::exception&) {
        rejectedActiveReconfiguration = true;
    }

    if (!rejectedActiveReconfiguration) {
        detail = "active Kernel stack was reconfigured";
        return false;
    }

    cpu.state().flags().reset();
    cpu.step();

    if (
        cpu.state().hasError()
        || !cpu.state().isUserMode()
        || cpu.usingKernelInterruptStack()
        || cpu.state().sp() != userSp
        || cpu.kernelStackPointer() != kernelSp
        || cpu.state().pc() != expectedReturn
        || cpu.state().flags().raw()
            != originalFlags
    ) {
        detail = "IRET did not restore User stack";
        return false;
    }

    return true;
}

bool nestedInterruptRoundTrip(
    ExecutionMode mode,
    std::string& detail
) {
    using namespace zero_cpu;

    CPU cpu;
    loadProgram(
        cpu,
        mode,
        {
            Instruction(Opcode::NOP),
            Instruction(Opcode::NOP),
            Instruction(Opcode::IRET),
            Instruction(Opcode::IRET)
        }
    );

    cpu.setUserCodeRange(
        instructionAddress(cpu, mode, 0),
        instructionAddress(cpu, mode, 2)
    );

    auto controller =
        std::make_shared<InterruptController>();
    controller->setVectorHandler(
        40,
        instructionAddress(cpu, mode, 2)
    );
    controller->setVectorHandler(
        41,
        instructionAddress(cpu, mode, 3)
    );
    cpu.setInterruptController(controller);

    const std::size_t userSp =
        memory_map::kUserStackBase
        + CPU::kStackSlotSize;
    const std::size_t kernelBase =
        memory_map::kKernelStackBase;

    cpu.state().setSp(userSp);
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    controller->request(40, 1, "outer");
    cpu.step();

    controller->request(41, 2, "inner");
    cpu.step();

    if (
        cpu.state().hasError()
        || !cpu.state().isKernelMode()
        || !cpu.usingKernelInterruptStack()
        || cpu.state().sp()
            != kernelBase
                + CPU::kInterruptFrameSize * 2
        || cpu.state().memory().read(
            kernelBase + CPU::kStackSlotSize * 3
        ) != static_cast<std::int64_t>(userSp)
        || cpu.state().memory().read(
            kernelBase
                + CPU::kInterruptFrameSize
                + CPU::kStackSlotSize * 3
        ) != static_cast<std::int64_t>(
            kernelBase + CPU::kInterruptFrameSize
        )
    ) {
        detail = "nested Kernel frame stack mismatch";
        return false;
    }

    cpu.step();

    if (
        cpu.state().hasError()
        || !cpu.state().isKernelMode()
        || !cpu.usingKernelInterruptStack()
        || cpu.state().sp()
            != kernelBase + CPU::kInterruptFrameSize
        || cpu.state().pc()
            != instructionAddress(cpu, mode, 2)
    ) {
        detail = "inner IRET did not restore Kernel stack";
        return false;
    }

    cpu.step();

    if (
        cpu.state().hasError()
        || !cpu.state().isUserMode()
        || cpu.usingKernelInterruptStack()
        || cpu.state().sp() != userSp
        || cpu.kernelStackPointer() != kernelBase
        || cpu.state().pc()
            != instructionAddress(cpu, mode, 0)
    ) {
        detail = "outer IRET did not restore User stack";
        return false;
    }

    return true;
}

bool kernelOverflowIsAtomic(
    ExecutionMode mode,
    std::string& detail
) {
    using namespace zero_cpu;

    CPU cpu;
    loadProgram(
        cpu,
        mode,
        {
            Instruction(
                Opcode::INT,
                Operand::immediate(50)
            ),
            Instruction(Opcode::NOP),
            Instruction(Opcode::IRET)
        }
    );

    cpu.setUserCodeRange(
        instructionAddress(cpu, mode, 0),
        instructionAddress(cpu, mode, 2)
    );

    auto controller =
        std::make_shared<InterruptController>();
    controller->setVectorHandler(
        50,
        instructionAddress(cpu, mode, 2)
    );
    cpu.setInterruptController(controller);

    const std::size_t kernelSp =
        memory_map::kKernelStackEndExclusive
        - CPU::kStackSlotSize * 3;
    const std::size_t userSp =
        memory_map::kUserStackBase;

    cpu.setKernelStackPointer(kernelSp);
    cpu.state().setSp(userSp);
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    const Snapshot before = capture(cpu);
    cpu.step();

    if (
        !cpu.state().hasError()
        || cpu.state().errorMessage() != "Stack overflow"
        || !architectureMatches(cpu, before)
        || cpu.kernelStackPointer() != kernelSp
        || cpu.usingKernelInterruptStack()
        || cpu.traceLogger().size() != 1
        || !cpu.traceLogger().last().hasError()
    ) {
        detail = "Kernel stack overflow was not atomic";
        return false;
    }

    return true;
}

bool corruptSavedUserSpIsAtomic(
    ExecutionMode mode,
    std::string& detail
) {
    using namespace zero_cpu;

    CPU cpu;
    loadProgram(
        cpu,
        mode,
        {
            Instruction(
                Opcode::INT,
                Operand::immediate(60)
            ),
            Instruction(Opcode::NOP),
            Instruction(Opcode::IRET)
        }
    );

    cpu.setUserCodeRange(
        instructionAddress(cpu, mode, 0),
        instructionAddress(cpu, mode, 2)
    );

    auto controller =
        std::make_shared<InterruptController>();
    controller->setVectorHandler(
        60,
        instructionAddress(cpu, mode, 2)
    );
    cpu.setInterruptController(controller);

    cpu.state().setSp(memory_map::kUserStackBase);
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    cpu.step();

    if (
        cpu.state().hasError()
        || !cpu.usingKernelInterruptStack()
    ) {
        detail = "setup interrupt entry failed";
        return false;
    }

    const std::size_t savedSpAddress =
        memory_map::kKernelStackBase
        + CPU::kStackSlotSize * 3;

    cpu.state().memory().write(
        savedSpAddress,
        static_cast<std::int64_t>(
            memory_map::kKernelStackBase
            + CPU::kStackSlotSize
        )
    );

    const Snapshot beforeIret = capture(cpu);
    const std::size_t kernelSpBefore =
        cpu.kernelStackPointer();

    cpu.step();

    if (
        !cpu.state().hasError()
        || cpu.state().errorMessage()
            != "Stack pointer is outside memory"
        || !architectureMatches(cpu, beforeIret)
        || !cpu.usingKernelInterruptStack()
        || cpu.kernelStackPointer() != kernelSpBefore
        || cpu.traceLogger().size() != 2
        || !cpu.traceLogger().last().hasError()
    ) {
        detail = "invalid saved User SP partially consumed frame";
        return false;
    }

    return true;
}

} // namespace

int main() {
    using namespace zero_cpu;

    std::cout
        << "=== Zero-CPU Kernel Stack Test ===\n\n";

    int failures = 0;

    auto report = [&](
        const std::string& name,
        bool passed,
        const std::string& detail
    ) {
        std::cout << (passed ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!passed) {
            std::cout << "       " << detail << "\n";
            ++failures;
        }
    };

    {
        std::string detail;
        report(
            "Kernel stack configuration validation",
            configurationValidation(detail),
            detail
        );
    }

    const std::vector<ExecutionMode> modes = {
        ExecutionMode::Vector,
        ExecutionMode::Binary
    };

    const std::vector<InterruptKind> kinds = {
        InterruptKind::Hardware,
        InterruptKind::Software
    };

    for (const ExecutionMode mode : modes) {
        {
            std::string detail;
            report(
                modeName(mode)
                    + " User stack boundary enforced",
                userStackBoundaryIsEnforced(
                    mode,
                    detail
                ),
                detail
            );
        }

        for (const InterruptKind kind : kinds) {
            std::string detail;
            report(
                modeName(mode)
                    + " "
                    + kindName(kind)
                    + " separate stack round trip",
                interruptRoundTrip(
                    mode,
                    kind,
                    detail
                ),
                detail
            );
        }

        {
            std::string detail;
            report(
                modeName(mode)
                    + " nested Kernel stack round trip",
                nestedInterruptRoundTrip(
                    mode,
                    detail
                ),
                detail
            );
        }

        {
            std::string detail;
            report(
                modeName(mode)
                    + " Kernel stack overflow atomic",
                kernelOverflowIsAtomic(
                    mode,
                    detail
                ),
                detail
            );
        }

        {
            std::string detail;
            report(
                modeName(mode)
                    + " corrupt saved User SP atomic",
                corruptSavedUserSpIsAtomic(
                    mode,
                    detail
                ),
                detail
            );
        }
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Kernel stack test "
               "finished successfully.\n";
        return 0;
    }

    std::cout
        << "Kernel stack test failed. "
        << "Failure count: "
        << failures
        << "\n";
    return 1;
}
