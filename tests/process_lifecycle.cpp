#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/Operand.hpp"
#include "zero_cpu/kernel/ProcessContext.hpp"
#include "zero_cpu/kernel/ProcessLifecycleManager.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"
#include "zero_cpu/kernel/ProcessTable.hpp"
#include "zero_cpu/kernel/ProcessTermination.hpp"
#include "zero_cpu/kernel/RoundRobinScheduler.hpp"
#include "zero_cpu/kernel/TimerPreemptiveScheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
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

std::vector<zero_cpu::Instruction>
normalProgram() {
    using namespace zero_cpu;

    return {
        Instruction(
            Opcode::ADD,
            Operand::registerOperand(
                RegisterName::R0
            ),
            Operand::immediate(1)
        ),
        Instruction(Opcode::HALT),
        Instruction(Opcode::NOP)
    };
}

std::vector<zero_cpu::Instruction>
faultProgram() {
    using namespace zero_cpu;

    return {
        Instruction(
            Opcode::DIV,
            Operand::registerOperand(
                RegisterName::R0
            ),
            Operand::registerOperand(
                RegisterName::R1
            )
        ),
        Instruction(Opcode::HALT),
        Instruction(Opcode::NOP)
    };
}

zero_cpu::CPU makeProcessCpu(
    const std::vector<zero_cpu::Instruction>& program,
    zero_cpu::PrivilegeLevel privilege,
    std::int64_t r0,
    std::int64_t r1 = 1
) {
    using namespace zero_cpu;

    CPU cpu;
    cpu.loadProgram(program, {});

    cpu.state().setPrivilegeLevel(privilege);

    cpu.state().registers().set(
        RegisterName::R0,
        r0
    );

    cpu.state().registers().set(
        RegisterName::R1,
        r1
    );

    cpu.state().setSp(
        memory_map::kUserStackBase
    );

    return cpu;
}

bool runtimeStateStrings(std::string& detail) {
    using namespace zero_cpu::kernel;

    if (
        std::string(
            processRuntimeStateToString(
                ProcessRuntimeState::Ready
            )
        ) != "Ready"
        || std::string(
            processRuntimeStateToString(
                ProcessRuntimeState::Running
            )
        ) != "Running"
        || std::string(
            processRuntimeStateToString(
                ProcessRuntimeState::Completed
            )
        ) != "Completed"
        || std::string(
            processRuntimeStateToString(
                ProcessRuntimeState::Deadlocked
            )
        ) != "Deadlocked"
        || std::string(
            processTerminationKindToString(
                ProcessTerminationKind::NormalExit
            )
        ) != "NormalExit"
        || std::string(
            processTerminationKindToString(
                ProcessTerminationKind::CpuFault
            )
        ) != "CpuFault"
    ) {
        detail = "runtime state string mismatch";
        return false;
    }

    return true;
}

bool normalExitAndAutomaticDispatch(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const std::vector<Instruction> program =
        normalProgram();

    ProcessTable table;

    const ProcessId first =
        table.createProcess(
            makeProcessCpu(
                program,
                PrivilegeLevel::Kernel,
                10
            )
        );

    const ProcessId second =
        table.createProcess(
            makeProcessCpu(
                program,
                PrivilegeLevel::Kernel,
                20
            )
        );

    CPU cpu;
    cpu.loadProgram(program, {});

    RoundRobinScheduler roundRobin;

    TimerPreemptiveScheduler preemptive(
        60,
        100
    );

    ProcessLifecycleManager lifecycle(
        0,
        -99
    );

    if (
        lifecycle.start(
            cpu,
            table,
            roundRobin,
            preemptive
        ) != ProcessRuntimeState::Running
        || table.runningProcessId() != first
    ) {
        detail = "runtime start mismatch";
        return false;
    }

    ProcessLifecycleStepResult result =
        lifecycle.step(
            cpu,
            table,
            roundRobin,
            preemptive
        );

    if (
        result.process_terminated
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 11
        || cpu.state().pc() != 1
    ) {
        detail = "first process execution mismatch";
        return false;
    }

    result = lifecycle.step(
        cpu,
        table,
        roundRobin,
        preemptive
    );

    const ProcessControlBlock& firstPcb =
        table.process(first);

    if (
        !result.process_terminated
        || result.process_faulted
        || result.terminated_pid != first
        || result.running_pid != second
        || result.state
            != ProcessRuntimeState::Running
        || table.runningProcessId() != second
        || firstPcb.state()
            != ProcessState::Terminated
        || firstPcb.exitCode() != 0
        || firstPcb.terminationKind()
            != ProcessTerminationKind::NormalExit
        || !firstPcb.terminationMessage().empty()
        || firstPcb.context().registers[
            static_cast<std::size_t>(
                RegisterName::R0
            )
        ] != 11
        || firstPcb.context().pc != 1
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 20
        || cpu.state().pc() != 0
        || cpu.state().halted()
        || cpu.state().hasError()
    ) {
        detail =
            "normal termination or automatic dispatch mismatch";
        return false;
    }

    (void)lifecycle.step(
        cpu,
        table,
        roundRobin,
        preemptive
    );

    result = lifecycle.step(
        cpu,
        table,
        roundRobin,
        preemptive
    );

    if (
        result.state
            != ProcessRuntimeState::Completed
        || !result.process_terminated
        || result.terminated_pid != second
        || table.hasRunningProcess()
        || table.process(second).state()
            != ProcessState::Terminated
        || table.process(second).exitCode() != 0
        || lifecycle.terminationCount() != 2
        || lifecycle.faultCount() != 0
        || !cpu.state().halted()
    ) {
        detail = "runtime completion mismatch";
        return false;
    }

    return true;
}

bool cpuFaultAndRecovery(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const std::vector<Instruction> program =
        faultProgram();

    ProcessTable table;

    const ProcessId faultedPid =
        table.createProcess(
            makeProcessCpu(
                program,
                PrivilegeLevel::User,
                10,
                0
            )
        );

    const ProcessId healthyPid =
        table.createProcess(
            makeProcessCpu(
                program,
                PrivilegeLevel::Kernel,
                20,
                2
            )
        );

    CPU cpu;
    cpu.loadProgram(program, {});

    RoundRobinScheduler roundRobin;

    TimerPreemptiveScheduler preemptive(
        61,
        100
    );

    ProcessLifecycleManager lifecycle(
        0,
        -77
    );

    (void)lifecycle.start(
        cpu,
        table,
        roundRobin,
        preemptive
    );

    const ProcessLifecycleStepResult faultResult =
        lifecycle.step(
            cpu,
            table,
            roundRobin,
            preemptive
        );

    const ProcessControlBlock& faulted =
        table.process(faultedPid);

    if (
        !faultResult.process_terminated
        || !faultResult.process_faulted
        || faultResult.terminated_pid
            != faultedPid
        || faultResult.running_pid
            != healthyPid
        || faulted.state()
            != ProcessState::Terminated
        || faulted.exitCode() != -77
        || faulted.terminationKind()
            != ProcessTerminationKind::CpuFault
        || faulted.terminationMessage().empty()
        || faulted.context().pc != 0
        || table.runningProcessId()
            != healthyPid
        || cpu.state().hasError()
        || cpu.state().halted()
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 20
        || lifecycle.faultCount() != 1
    ) {
        detail =
            "CPU fault termination or recovery mismatch";
        return false;
    }

    (void)lifecycle.step(
        cpu,
        table,
        roundRobin,
        preemptive
    );

    const ProcessLifecycleStepResult finishResult =
        lifecycle.step(
            cpu,
            table,
            roundRobin,
            preemptive
        );

    if (
        finishResult.state
            != ProcessRuntimeState::Completed
        || table.process(healthyPid)
            .terminationKind()
            != ProcessTerminationKind::NormalExit
        || lifecycle.terminationCount() != 2
        || lifecycle.faultCount() != 1
    ) {
        detail =
            "healthy process did not finish after fault";
        return false;
    }

    return true;
}

bool invalidPcFaultSnapshot(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const std::vector<Instruction> program =
        normalProgram();

    ProcessTable table;

    const ProcessId pid =
        table.createProcess(
            makeProcessCpu(
                program,
                PrivilegeLevel::User,
                5
            )
        );

    CPU cpu;
    cpu.loadProgram(program, {});

    RoundRobinScheduler roundRobin;

    TimerPreemptiveScheduler preemptive(
        62,
        100
    );

    ProcessLifecycleManager lifecycle;

    (void)lifecycle.start(
        cpu,
        table,
        roundRobin,
        preemptive
    );

    const std::size_t invalidPc =
        cpu.userCodeEndExclusive();

    cpu.state().setPc(invalidPc);

    const ProcessLifecycleStepResult result =
        lifecycle.step(
            cpu,
            table,
            roundRobin,
            preemptive
        );

    const ProcessControlBlock& pcb =
        table.process(pid);

    if (
        result.state
            != ProcessRuntimeState::Completed
        || !result.process_faulted
        || pcb.context().pc != invalidPc
        || pcb.terminationKind()
            != ProcessTerminationKind::CpuFault
        || pcb.terminationMessage().empty()
        || table.hasRunningProcess()
        || !cpu.state().hasError()
        || !cpu.state().halted()
    ) {
        detail =
            "invalid PC terminal snapshot was not preserved";
        return false;
    }

    if (
        !throwsRuntimeError(
            [&] {
                restoreProcessContext(
                    pcb.context(),
                    cpu
                );
            }
        )
    ) {
        detail =
            "invalid terminal snapshot was restored";
        return false;
    }

    return true;
}

bool deadlockClassification(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const std::vector<Instruction> program =
        normalProgram();

    ProcessTable table;

    const ProcessId first =
        table.createProcess(
            makeProcessCpu(
                program,
                PrivilegeLevel::User,
                1
            )
        );

    const ProcessId second =
        table.createProcess(
            makeProcessCpu(
                program,
                PrivilegeLevel::User,
                2
            )
        );

    table.transition(
        first,
        ProcessState::Running
    );

    table.transition(
        first,
        ProcessState::Blocked
    );

    table.transition(
        second,
        ProcessState::Running
    );

    table.transition(
        second,
        ProcessState::Blocked
    );

    CPU cpu;
    cpu.loadProgram(program, {});

    RoundRobinScheduler roundRobin;

    TimerPreemptiveScheduler preemptive(
        63,
        10
    );

    ProcessLifecycleManager lifecycle;

    if (
        lifecycle.start(
            cpu,
            table,
            roundRobin,
            preemptive
        ) != ProcessRuntimeState::Deadlocked
        || lifecycle.state()
            != ProcessRuntimeState::Deadlocked
        || !lifecycle.started()
        || preemptive.started()
        || table.hasRunningProcess()
        || !cpu.state().halted()
        || lifecycle.terminationCount() != 0
    ) {
        detail = "deadlock classification mismatch";
        return false;
    }

    const ProcessLifecycleStepResult result =
        lifecycle.step(
            cpu,
            table,
            roundRobin,
            preemptive
        );

    if (
        result.state
            != ProcessRuntimeState::Deadlocked
        || result.process_terminated
    ) {
        detail =
            "deadlocked runtime step was not stable";
        return false;
    }

    return true;
}

bool preterminatedTableCompletes(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const std::vector<Instruction> program =
        normalProgram();

    ProcessTable table;

    const ProcessId pid =
        table.createProcess(
            makeProcessCpu(
                program,
                PrivilegeLevel::Kernel,
                1
            )
        );

    table.terminate(pid, 12);

    CPU cpu;
    cpu.loadProgram(program, {});

    RoundRobinScheduler roundRobin;

    TimerPreemptiveScheduler preemptive(
        64,
        10
    );

    ProcessLifecycleManager lifecycle;

    if (
        lifecycle.start(
            cpu,
            table,
            roundRobin,
            preemptive
        ) != ProcessRuntimeState::Completed
        || preemptive.started()
        || !cpu.state().halted()
        || table.process(pid).exitCode() != 12
    ) {
        detail =
            "pre-terminated table completion mismatch";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Process Lifecycle "
           "Test ===\n\n";

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
            "Runtime state and termination strings",
            runtimeStateStrings(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Normal exit and automatic dispatch",
            normalExitAndAutomaticDispatch(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "CPU fault and recovery",
            cpuFaultAndRecovery(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Invalid PC fault snapshot",
            invalidPcFaultSnapshot(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Deadlock classification",
            deadlockClassification(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Pre-terminated table completion",
            preterminatedTableCompletes(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Process lifecycle test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Process lifecycle test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
