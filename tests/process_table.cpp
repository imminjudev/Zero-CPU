#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/kernel/ProcessContext.hpp"
#include "zero_cpu/kernel/ProcessControlBlock.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"
#include "zero_cpu/kernel/ProcessTable.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

template <typename Function>
bool throwsRuntimeError(Function&& function) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return true;
    }

    return false;
}

void loadProcessProgram(zero_cpu::CPU& cpu) {
    using namespace zero_cpu;

    cpu.loadProgram(
        {
            Instruction(Opcode::NOP),
            Instruction(Opcode::NOP),
            Instruction(Opcode::NOP),
            Instruction(Opcode::HALT)
        },
        {}
    );

    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );
}

zero_cpu::kernel::ProcessContext makeContext(
    zero_cpu::kernel::ProcessId pid,
    std::int64_t registerSeed = 100
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    CPU cpu;
    loadProcessProgram(cpu);

    for (
        std::size_t index = 0;
        index < RegisterFile::kRegisterCount;
        ++index
    ) {
        cpu.state().registers().set(
            static_cast<RegisterName>(index),
            registerSeed
                + static_cast<std::int64_t>(index)
        );
    }

    cpu.state().setPc(1);
    cpu.state().setSp(
        memory_map::kUserStackBase
        + CPU::kStackSlotSize
    );

    return captureProcessContext(pid, cpu);
}

bool pcbLifecycle(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessContext context = makeContext(7);
    ProcessControlBlock pcb(context);

    if (
        pcb.pid() != 7
        || pcb.state() != ProcessState::Ready
        || pcb.hasExitCode()
    ) {
        detail = "initial PCB state mismatch";
        return false;
    }

    pcb.transitionTo(ProcessState::Running);

    ProcessContext replacement = context;
    replacement.registers[
        static_cast<std::size_t>(RegisterName::R3)
    ] = 777;

    pcb.replaceContext(replacement);

    if (
        pcb.state() != ProcessState::Running
        || pcb.context().registers[
            static_cast<std::size_t>(
                RegisterName::R3
            )
        ] != 777
    ) {
        detail = "PCB context replacement mismatch";
        return false;
    }

    pcb.transitionTo(ProcessState::Blocked);
    pcb.transitionTo(ProcessState::Ready);
    pcb.transitionTo(ProcessState::Running);
    pcb.terminate(-12);

    if (
        pcb.state() != ProcessState::Terminated
        || !pcb.hasExitCode()
        || pcb.exitCode() != -12
    ) {
        detail = "PCB termination metadata mismatch";
        return false;
    }

    if (
        !throwsRuntimeError(
            [&] {
                pcb.transitionTo(
                    ProcessState::Ready
                );
            }
        )
        || !throwsRuntimeError(
            [&] {
                pcb.replaceContext(replacement);
            }
        )
        || !throwsRuntimeError(
            [&] { pcb.terminate(0); }
        )
    ) {
        detail = "terminated PCB accepted mutation";
        return false;
    }

    return true;
}

bool pidAllocationAndLookup(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    CPU firstCpu;
    CPU secondCpu;
    loadProcessProgram(firstCpu);
    loadProcessProgram(secondCpu);

    secondCpu.state().registers().set(
        RegisterName::R0,
        222
    );

    ProcessTable table;

    const ProcessId first =
        table.createProcess(firstCpu);

    ProcessContext secondContext =
        captureProcessContext(999, secondCpu);

    const ProcessId second =
        table.createProcess(secondContext);

    if (
        first != 1
        || second != 2
        || table.empty()
        || table.size() != 2
        || !table.contains(first)
        || !table.contains(second)
        || table.process(first).pid() != first
        || table.process(second).pid() != second
        || table.process(second).context()
            .registers[
                static_cast<std::size_t>(
                    RegisterName::R0
                )
            ] != 222
    ) {
        detail = "PID allocation or lookup mismatch";
        return false;
    }

    const std::vector<ProcessId> expected = {
        first,
        second
    };

    if (table.processIds() != expected) {
        detail = "process ID ordering mismatch";
        return false;
    }

    if (
        !throwsRuntimeError(
            [&] {
                (void)table.process(9999);
            }
        )
    ) {
        detail = "unknown PID lookup succeeded";
        return false;
    }

    return true;
}

bool singleRunningInvariant(std::string& detail) {
    using namespace zero_cpu::kernel;

    ProcessTable table;

    const ProcessId first =
        table.createProcess(makeContext(10));

    const ProcessId second =
        table.createProcess(makeContext(20, 200));

    table.transition(
        first,
        ProcessState::Running
    );

    if (
        !table.hasRunningProcess()
        || table.runningProcessId() != first
    ) {
        detail = "first Running process not tracked";
        return false;
    }

    if (
        !throwsRuntimeError(
            [&] {
                table.transition(
                    second,
                    ProcessState::Running
                );
            }
        )
    ) {
        detail = "second Running process was accepted";
        return false;
    }

    if (
        table.runningProcessId() != first
        || table.process(first).state()
            != ProcessState::Running
        || table.process(second).state()
            != ProcessState::Ready
    ) {
        detail = "failed Running transition was not atomic";
        return false;
    }

    table.transition(
        first,
        ProcessState::Blocked
    );

    table.transition(
        second,
        ProcessState::Running
    );

    if (
        table.runningProcessId() != second
        || table.process(first).state()
            != ProcessState::Blocked
    ) {
        detail = "Running ownership transfer mismatch";
        return false;
    }

    table.transition(
        second,
        ProcessState::Ready
    );

    if (
        table.hasRunningProcess()
        || !throwsRuntimeError(
            [&] {
                (void)table.runningProcessId();
            }
        )
    ) {
        detail = "Running process clear mismatch";
        return false;
    }

    const std::vector<ProcessId> ready = {
        second
    };

    const std::vector<ProcessId> blocked = {
        first
    };

    if (
        table.processIdsInState(
            ProcessState::Ready
        ) != ready
        || table.processIdsInState(
            ProcessState::Blocked
        ) != blocked
    ) {
        detail = "state-filtered PID list mismatch";
        return false;
    }

    return true;
}

bool contextUpdateIsAtomic(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessTable table;

    const ProcessId pid =
        table.createProcess(makeContext(50));

    ProcessContext updated =
        table.process(pid).context();

    updated.registers[
        static_cast<std::size_t>(RegisterName::R6)
    ] = 6006;

    table.updateContext(pid, updated);

    if (
        table.process(pid).context().registers[
            static_cast<std::size_t>(
                RegisterName::R6
            )
        ] != 6006
    ) {
        detail = "valid context update failed";
        return false;
    }

    ProcessContext mismatched = updated;
    mismatched.pid = pid + 1;

    if (
        !throwsRuntimeError(
            [&] {
                table.updateContext(
                    pid,
                    mismatched
                );
            }
        )
    ) {
        detail = "mismatched context PID was accepted";
        return false;
    }

    ProcessContext invalid = updated;
    invalid.sp += 1;

    if (
        !throwsRuntimeError(
            [&] {
                table.updateContext(
                    pid,
                    invalid
                );
            }
        )
    ) {
        detail = "invalid context update was accepted";
        return false;
    }

    if (
        table.process(pid).context().pid != pid
        || table.process(pid).context().sp
            != updated.sp
        || table.process(pid).context().registers[
            static_cast<std::size_t>(
                RegisterName::R6
            )
        ] != 6006
    ) {
        detail = "failed context update changed PCB";
        return false;
    }

    return true;
}

bool terminationRetention(std::string& detail) {
    using namespace zero_cpu::kernel;

    ProcessTable table;

    const ProcessId first =
        table.createProcess(makeContext(70));

    const ProcessId second =
        table.createProcess(makeContext(80));

    table.transition(
        first,
        ProcessState::Running
    );

    table.terminate(first, 33);

    if (
        table.size() != 2
        || !table.contains(first)
        || table.hasRunningProcess()
        || table.process(first).state()
            != ProcessState::Terminated
        || !table.process(first).hasExitCode()
        || table.process(first).exitCode() != 33
    ) {
        detail = "terminated PCB was not retained";
        return false;
    }

    const std::vector<ProcessId> terminated = {
        first
    };

    const std::vector<ProcessId> ready = {
        second
    };

    if (
        table.processIdsInState(
            ProcessState::Terminated
        ) != terminated
        || table.processIdsInState(
            ProcessState::Ready
        ) != ready
    ) {
        detail = "post-termination state lists mismatch";
        return false;
    }

    if (
        !throwsRuntimeError(
            [&] {
                table.transition(
                    first,
                    ProcessState::Ready
                );
            }
        )
        || !throwsRuntimeError(
            [&] {
                table.updateContext(
                    first,
                    table.process(first).context()
                );
            }
        )
        || !throwsRuntimeError(
            [&] {
                table.terminate(first, 0);
            }
        )
    ) {
        detail = "terminated process accepted mutation";
        return false;
    }

    return true;
}

bool invalidCreationIsAtomic(std::string& detail) {
    using namespace zero_cpu::kernel;

    ProcessTable table;

    ProcessContext invalid = makeContext(99);
    invalid.sp += 1;

    if (
        !throwsRuntimeError(
            [&] {
                (void)table.createProcess(invalid);
            }
        )
    ) {
        detail = "invalid process creation succeeded";
        return false;
    }

    if (!table.empty()) {
        detail = "failed creation inserted a process";
        return false;
    }

    const ProcessId pid =
        table.createProcess(makeContext(100));

    if (pid != 1 || table.size() != 1) {
        detail = "failed creation consumed a PID";
        return false;
    }

    return true;
}

bool activeInterruptCreationGuard(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

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

    controller->setVectorHandler(44, 1);
    active.setInterruptController(controller);

    controller->request(
        44,
        0,
        "process-table-test"
    );

    active.step();

    if (
        active.state().hasError()
        || !active.usingKernelInterruptStack()
    ) {
        detail = "interrupt setup failed";
        return false;
    }

    ProcessTable table;

    if (
        !throwsRuntimeError(
            [&] {
                (void)table.createProcess(active);
            }
        )
    ) {
        detail =
            "active interrupt context was inserted";
        return false;
    }

    if (!table.empty()) {
        detail =
            "guarded interrupt creation mutated table";
        return false;
    }

    CPU valid;
    loadProcessProgram(valid);

    if (table.createProcess(valid) != 1) {
        detail =
            "guarded creation consumed process ID";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Process Table Test ===\n\n";

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
            "ProcessControlBlock lifecycle",
            pcbLifecycle(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "PID allocation and lookup",
            pidAllocationAndLookup(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Single Running process invariant",
            singleRunningInvariant(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Context update is atomic",
            contextUpdateIsAtomic(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Termination retains PCB",
            terminationRetention(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Invalid creation is atomic",
            invalidCreationIsAtomic(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Active interrupt creation guard",
            activeInterruptCreationGuard(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Process table test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Process table test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
