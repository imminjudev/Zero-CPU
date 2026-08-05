#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"
#include "zero_cpu/kernel/ProcessContext.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

enum class ExecutionMode {
    Vector,
    Binary
};

std::string modeName(ExecutionMode mode) {
    return mode == ExecutionMode::Vector
        ? "vector"
        : "binary";
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
    program.header.endianness =
        BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size =
        static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    return program;
}

void loadTestProgram(
    zero_cpu::CPU& cpu,
    ExecutionMode mode
) {
    using namespace zero_cpu;

    const std::vector<Instruction> instructions = {
        Instruction(Opcode::NOP),
        Instruction(Opcode::NOP),
        Instruction(Opcode::NOP),
        Instruction(Opcode::HALT)
    };

    if (mode == ExecutionMode::Vector) {
        cpu.loadProgram(instructions, {});
        return;
    }

    cpu.loadBinaryProgram(
        makeBinaryProgram(instructions)
    );
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

template <typename Function>
bool throwsRuntimeError(Function&& function) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return true;
    }

    return false;
}

bool processStateModel(std::string& detail) {
    using namespace zero_cpu::kernel;

    if (
        std::string(
            processStateToString(ProcessState::Ready)
        ) != "Ready"
        || std::string(
            processStateToString(ProcessState::Running)
        ) != "Running"
        || std::string(
            processStateToString(ProcessState::Blocked)
        ) != "Blocked"
        || std::string(
            processStateToString(
                ProcessState::Terminated
            )
        ) != "Terminated"
    ) {
        detail = "process state string mismatch";
        return false;
    }

    if (
        !isRunnableProcessState(ProcessState::Ready)
        || !isRunnableProcessState(
            ProcessState::Running
        )
        || isRunnableProcessState(
            ProcessState::Blocked
        )
        || !isTerminalProcessState(
            ProcessState::Terminated
        )
    ) {
        detail = "process state classification mismatch";
        return false;
    }

    if (
        !canTransitionProcessState(
            ProcessState::Ready,
            ProcessState::Running
        )
        || !canTransitionProcessState(
            ProcessState::Running,
            ProcessState::Blocked
        )
        || !canTransitionProcessState(
            ProcessState::Blocked,
            ProcessState::Ready
        )
        || !canTransitionProcessState(
            ProcessState::Running,
            ProcessState::Terminated
        )
        || canTransitionProcessState(
            ProcessState::Ready,
            ProcessState::Blocked
        )
        || canTransitionProcessState(
            ProcessState::Terminated,
            ProcessState::Ready
        )
    ) {
        detail = "process state transition matrix mismatch";
        return false;
    }

    return true;
}

struct CpuSnapshot {
    std::vector<std::int64_t> registers;
    std::uint32_t flags = 0;
    std::size_t pc = 0;
    std::size_t sp = 0;

    zero_cpu::PrivilegeLevel privilege =
        zero_cpu::PrivilegeLevel::Kernel;

    bool halted = false;
    bool has_error = false;
    std::string error;

    bool has_user_code_range = false;
    std::size_t user_code_begin = 0;
    std::size_t user_code_end_exclusive = 0;
    std::size_t kernel_stack_pointer = 0;
};

CpuSnapshot captureCpuSnapshot(
    const zero_cpu::CPU& cpu
) {
    using namespace zero_cpu;

    CpuSnapshot snapshot;

    for (
        std::size_t index = 0;
        index < RegisterFile::kRegisterCount;
        ++index
    ) {
        snapshot.registers.push_back(
            cpu.state().registers().get(
                static_cast<RegisterName>(index)
            )
        );
    }

    snapshot.flags = cpu.state().flags().raw();
    snapshot.pc = cpu.state().pc();
    snapshot.sp = cpu.state().sp();
    snapshot.privilege =
        cpu.state().privilegeLevel();
    snapshot.halted = cpu.state().halted();
    snapshot.has_error = cpu.state().hasError();
    snapshot.error = cpu.state().errorMessage();

    snapshot.has_user_code_range =
        cpu.hasUserCodeRange();

    if (snapshot.has_user_code_range) {
        snapshot.user_code_begin =
            cpu.userCodeBegin();
        snapshot.user_code_end_exclusive =
            cpu.userCodeEndExclusive();
    }

    snapshot.kernel_stack_pointer =
        cpu.kernelStackPointer();

    return snapshot;
}

bool cpuMatchesSnapshot(
    const zero_cpu::CPU& cpu,
    const CpuSnapshot& expected
) {
    using namespace zero_cpu;

    if (
        cpu.state().flags().raw() != expected.flags
        || cpu.state().pc() != expected.pc
        || cpu.state().sp() != expected.sp
        || cpu.state().privilegeLevel()
            != expected.privilege
        || cpu.state().halted() != expected.halted
        || cpu.state().hasError()
            != expected.has_error
        || cpu.state().errorMessage()
            != expected.error
        || cpu.hasUserCodeRange()
            != expected.has_user_code_range
        || cpu.kernelStackPointer()
            != expected.kernel_stack_pointer
    ) {
        return false;
    }

    if (
        expected.has_user_code_range
        && (
            cpu.userCodeBegin()
                != expected.user_code_begin
            || cpu.userCodeEndExclusive()
                != expected.user_code_end_exclusive
        )
    ) {
        return false;
    }

    for (
        std::size_t index = 0;
        index < RegisterFile::kRegisterCount;
        ++index
    ) {
        if (
            cpu.state().registers().get(
                static_cast<RegisterName>(index)
            )
            != expected.registers[index]
        ) {
            return false;
        }
    }

    return true;
}

bool contextRoundTrip(
    ExecutionMode mode,
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    CPU cpu;
    loadTestProgram(cpu, mode);

    const std::size_t codeBegin =
        instructionAddress(cpu, mode, 0);
    const std::size_t codeEnd =
        instructionAddress(cpu, mode, 3);
    const std::size_t savedPc =
        instructionAddress(cpu, mode, 1);

    const std::size_t savedSp =
        memory_map::kUserStackBase
        + CPU::kStackSlotSize * 2;

    const std::size_t savedKernelSp =
        memory_map::kKernelStackBase
        + CPU::kStackSlotSize;

    cpu.setUserCodeRange(codeBegin, codeEnd);
    cpu.setKernelStackPointer(savedKernelSp);

    for (
        std::size_t index = 0;
        index < RegisterFile::kRegisterCount;
        ++index
    ) {
        cpu.state().registers().set(
            static_cast<RegisterName>(index),
            static_cast<std::int64_t>(
                1000 + index * 17
            )
        );
    }

    cpu.state().flags().setCarry(true);
    cpu.state().flags().setZero(true);
    cpu.state().flags().setSign(false);
    cpu.state().flags().setOverflow(true);
    cpu.state().setPc(savedPc);
    cpu.state().setSp(savedSp);
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    const ProcessContext context =
        captureProcessContext(7, cpu);

    if (
        context.pid != 7
        || context.flags
            != cpu.state().flags().raw()
        || context.pc != savedPc
        || context.sp != savedSp
        || context.privilege
            != PrivilegeLevel::User
        || !context.has_user_code_range
        || context.user_code_begin != codeBegin
        || context.user_code_end_exclusive
            != codeEnd
        || context.user_stack_begin
            != memory_map::kUserStackBase
        || context.user_stack_end_exclusive
            != memory_map::kUserStackEndExclusive
        || context.kernel_stack_pointer
            != savedKernelSp
    ) {
        detail = "captured context metadata mismatch";
        return false;
    }

    for (
        std::size_t index = 0;
        index < RegisterFile::kRegisterCount;
        ++index
    ) {
        if (
            context.registers[index]
            != static_cast<std::int64_t>(
                1000 + index * 17
            )
        ) {
            detail = "captured register mismatch";
            return false;
        }

        cpu.state().registers().set(
            static_cast<RegisterName>(index),
            -1
        );
    }

    cpu.state().flags().reset();
    cpu.state().setPc(
        instructionAddress(cpu, mode, 0)
    );
    cpu.state().setSp(
        memory_map::kUserStackBase
    );
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::Kernel
    );
    cpu.setKernelStackPointer(
        memory_map::kKernelStackBase
    );
    cpu.state().setError("stale process error");
    cpu.state().setHalted(true);

    restoreProcessContext(context, cpu);

    if (
        cpu.state().hasError()
        || cpu.state().halted()
        || cpu.state().flags().raw()
            != context.flags
        || cpu.state().pc() != savedPc
        || cpu.state().sp() != savedSp
        || !cpu.state().isUserMode()
        || cpu.userCodeBegin() != codeBegin
        || cpu.userCodeEndExclusive() != codeEnd
        || cpu.kernelStackPointer()
            != savedKernelSp
    ) {
        detail = "restored CPU metadata mismatch";
        return false;
    }

    for (
        std::size_t index = 0;
        index < RegisterFile::kRegisterCount;
        ++index
    ) {
        if (
            cpu.state().registers().get(
                static_cast<RegisterName>(index)
            )
            != context.registers[index]
        ) {
            detail = "restored register mismatch";
            return false;
        }
    }

    return true;
}

bool invalidContextValidation(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    CPU cpu;
    loadTestProgram(cpu, ExecutionMode::Vector);
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    const ProcessContext valid =
        captureProcessContext(9, cpu);

    ProcessContext invalid = valid;
    invalid.pid = 0;

    if (
        !throwsRuntimeError(
            [&] { validateProcessContext(invalid); }
        )
    ) {
        detail = "zero PID was accepted";
        return false;
    }

    invalid = valid;
    invalid.pc =
        invalid.user_code_end_exclusive;

    if (
        !throwsRuntimeError(
            [&] { validateProcessContext(invalid); }
        )
    ) {
        detail = "User PC outside code was accepted";
        return false;
    }

    invalid = valid;
    invalid.sp += 1;

    if (
        !throwsRuntimeError(
            [&] { validateProcessContext(invalid); }
        )
    ) {
        detail = "misaligned User SP was accepted";
        return false;
    }

    invalid = valid;
    invalid.user_stack_end_exclusive =
        memory_map::kKernelStackBase
        + CPU::kStackSlotSize;

    if (
        !throwsRuntimeError(
            [&] { validateProcessContext(invalid); }
        )
    ) {
        detail =
            "User stack crossing Kernel stack accepted";
        return false;
    }

    invalid = valid;
    invalid.kernel_stack_pointer =
        memory_map::kKernelStackBase + 1;

    if (
        !throwsRuntimeError(
            [&] { validateProcessContext(invalid); }
        )
    ) {
        detail = "misaligned Kernel SP was accepted";
        return false;
    }

    return true;
}

bool invalidRestoreIsAtomic(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    CPU cpu;
    loadTestProgram(cpu, ExecutionMode::Vector);
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );
    cpu.state().registers().set(
        RegisterName::R3,
        777
    );
    cpu.state().flags().setCarry(true);

    ProcessContext invalid =
        captureProcessContext(11, cpu);
    invalid.sp += 1;

    const CpuSnapshot before =
        captureCpuSnapshot(cpu);

    if (
        !throwsRuntimeError(
            [&] {
                restoreProcessContext(
                    invalid,
                    cpu
                );
            }
        )
    ) {
        detail = "invalid context restore succeeded";
        return false;
    }

    if (!cpuMatchesSnapshot(cpu, before)) {
        detail = "invalid restore changed CPU state";
        return false;
    }

    return true;
}

bool activeInterruptStackIsRejected(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    CPU source;
    loadTestProgram(
        source,
        ExecutionMode::Vector
    );
    source.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    const ProcessContext valid =
        captureProcessContext(13, source);

    CPU active;
    active.loadProgram(
        {
            Instruction(Opcode::NOP),
            Instruction(Opcode::IRET)
        },
        {}
    );
    active.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    auto controller =
        std::make_shared<InterruptController>();

    controller->setVectorHandler(40, 1);
    active.setInterruptController(controller);

    controller->request(
        40,
        0,
        "process-context-test"
    );

    active.step();

    if (
        active.state().hasError()
        || !active.usingKernelInterruptStack()
    ) {
        detail = "test interrupt entry failed";
        return false;
    }

    const bool captureRejected =
        throwsRuntimeError(
            [&] {
                (void)captureProcessContext(
                    14,
                    active
                );
            }
        );

    const bool restoreRejected =
        throwsRuntimeError(
            [&] {
                restoreProcessContext(
                    valid,
                    active
                );
            }
        );

    if (!captureRejected || !restoreRejected) {
        detail =
            "active Kernel interrupt stack was not guarded";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Process Context Test ===\n\n";

    int failures = 0;

    auto report = [&](
        const std::string& name,
        bool passed,
        const std::string& detail
    ) {
        std::cout
            << (passed ? "[PASS] " : "[FAIL] ")
            << name
            << "\n";

        if (!passed) {
            std::cout
                << "       "
                << detail
                << "\n";

            ++failures;
        }
    };

    {
        std::string detail;

        report(
            "Process state model",
            processStateModel(detail),
            detail
        );
    }

    for (
        const ExecutionMode mode
        : {
            ExecutionMode::Vector,
            ExecutionMode::Binary
        }
    ) {
        std::string detail;

        report(
            modeName(mode)
                + " process context round trip",
            contextRoundTrip(mode, detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Invalid context validation",
            invalidContextValidation(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Invalid restore is atomic",
            invalidRestoreIsAtomic(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Active interrupt stack guard",
            activeInterruptStackIsRejected(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Process context test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Process context test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
