#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/Operand.hpp"
#include "zero_cpu/kernel/ProcessContext.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"
#include "zero_cpu/kernel/ProcessTable.hpp"
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
standardProgram() {
    using namespace zero_cpu;

    return {
        Instruction(
            Opcode::ADD,
            Operand::registerOperand(
                RegisterName::R0
            ),
            Operand::immediate(1)
        ),
        Instruction(
            Opcode::ADD,
            Operand::registerOperand(
                RegisterName::R1
            ),
            Operand::immediate(10)
        ),
        Instruction(
            Opcode::ADD,
            Operand::registerOperand(
                RegisterName::R2
            ),
            Operand::immediate(100)
        ),
        Instruction(
            Opcode::ADD,
            Operand::registerOperand(
                RegisterName::R3
            ),
            Operand::immediate(1000)
        ),
        Instruction(Opcode::NOP),
        Instruction(Opcode::HALT)
    };
}

std::vector<zero_cpu::Instruction>
narrowProgram() {
    using namespace zero_cpu;

    return {
        Instruction(
            Opcode::ADD,
            Operand::registerOperand(
                RegisterName::R0
            ),
            Operand::immediate(1)
        ),
        Instruction(Opcode::NOP),
        Instruction(Opcode::NOP),
        Instruction(Opcode::HALT)
    };
}

zero_cpu::CPU makeUserCpu(
    const std::vector<zero_cpu::Instruction>& program,
    std::int64_t r0,
    std::int64_t r1
) {
    using namespace zero_cpu;

    CPU cpu;
    cpu.loadProgram(program, {});

    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

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

bool constructorAndAttachValidation(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    if (
        !throwsRuntimeError(
            [] {
                TimerPreemptiveScheduler invalid(
                    50,
                    0
                );
            }
        )
    ) {
        detail = "zero quantum was accepted";
        return false;
    }

    CPU first;
    first.loadProgram(standardProgram(), {});

    TimerPreemptiveScheduler preemptive(
        50,
        2,
        500
    );

    preemptive.attach(first);
    preemptive.attach(first);

    if (
        !preemptive.attached()
        || first.clockedDeviceCount() != 1
        || !first.hasInterruptController()
        || preemptive.timerDevice()->interval() != 2
        || preemptive.timerDevice()->vector() != 50
        || preemptive.timerDevice()->payload() != 500
    ) {
        detail = "scheduler attachment mismatch";
        return false;
    }

    CPU second;
    second.loadProgram(standardProgram(), {});

    if (
        !throwsRuntimeError(
            [&] {
                preemptive.attach(second);
            }
        )
    ) {
        detail = "scheduler attached to a second CPU";
        return false;
    }

    return true;
}

bool quantumPreemptionRoundTrip(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const std::vector<Instruction> program =
        standardProgram();

    CPU firstSource = makeUserCpu(
        program,
        10,
        100
    );

    CPU secondSource = makeUserCpu(
        program,
        20,
        200
    );

    ProcessTable table;

    const ProcessId first =
        table.createProcess(firstSource);

    const ProcessId second =
        table.createProcess(secondSource);

    CPU cpu;
    cpu.loadProgram(program, {});

    RoundRobinScheduler roundRobin;
    TimerPreemptiveScheduler preemptive(
        51,
        2,
        777
    );

    if (
        preemptive.start(
            cpu,
            table,
            roundRobin
        ) != first
        || !preemptive.started()
    ) {
        detail = "initial process activation failed";
        return false;
    }

    if (
        preemptive.step(
            cpu,
            table,
            roundRobin
        ) != first
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 11
        || cpu.state().pc() != 1
        || preemptive.preemptionCount() != 0
    ) {
        detail = "first quantum tick mismatch";
        return false;
    }

    if (
        preemptive.step(
            cpu,
            table,
            roundRobin
        ) != second
        || table.runningProcessId() != second
        || table.process(first).context()
            .registers[
                static_cast<std::size_t>(
                    RegisterName::R0
                )
            ] != 11
        || table.process(first).context()
            .registers[
                static_cast<std::size_t>(
                    RegisterName::R1
                )
            ] != 110
        || table.process(first).context().pc != 2
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 20
        || cpu.state().registers().get(
            RegisterName::R1
        ) != 200
        || cpu.state().pc() != 0
        || preemptive.preemptionCount() != 1
        || preemptive.contextSwitchCount() != 1
        || preemptive.timerDevice()
            ->interruptCount() != 1
        || preemptive.interruptController()
            ->pendingCount() != 0
    ) {
        detail = "first timer preemption mismatch";
        return false;
    }

    (void)preemptive.step(
        cpu,
        table,
        roundRobin
    );

    if (
        preemptive.step(
            cpu,
            table,
            roundRobin
        ) != first
        || table.runningProcessId() != first
        || table.process(second).context()
            .registers[
                static_cast<std::size_t>(
                    RegisterName::R0
                )
            ] != 21
        || table.process(second).context()
            .registers[
                static_cast<std::size_t>(
                    RegisterName::R1
                )
            ] != 210
        || table.process(second).context().pc != 2
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 11
        || cpu.state().registers().get(
            RegisterName::R1
        ) != 110
        || cpu.state().pc() != 2
        || preemptive.preemptionCount() != 2
        || preemptive.contextSwitchCount() != 2
    ) {
        detail = "second timer preemption mismatch";
        return false;
    }

    return true;
}

bool blockedAndTerminatedAreSkipped(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const std::vector<Instruction> program =
        standardProgram();

    ProcessTable table;

    const ProcessId first =
        table.createProcess(
            makeUserCpu(program, 1, 10)
        );

    const ProcessId blocked =
        table.createProcess(
            makeUserCpu(program, 2, 20)
        );

    const ProcessId terminated =
        table.createProcess(
            makeUserCpu(program, 3, 30)
        );

    table.transition(
        blocked,
        ProcessState::Running
    );

    table.transition(
        blocked,
        ProcessState::Blocked
    );

    table.terminate(terminated, 0);

    CPU cpu;
    cpu.loadProgram(program, {});

    RoundRobinScheduler roundRobin;
    TimerPreemptiveScheduler preemptive(
        52,
        1
    );

    preemptive.start(
        cpu,
        table,
        roundRobin
    );

    if (
        preemptive.step(
            cpu,
            table,
            roundRobin
        ) != first
        || table.runningProcessId() != first
        || table.process(blocked).state()
            != ProcessState::Blocked
        || table.process(terminated).state()
            != ProcessState::Terminated
        || preemptive.preemptionCount() != 1
        || preemptive.contextSwitchCount() != 0
    ) {
        detail =
            "non-runnable process entered preemption";
        return false;
    }

    return true;
}

bool interruptDisableAndMaskAreHonored(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const std::vector<Instruction> program =
        standardProgram();

    ProcessTable table;

    const ProcessId first =
        table.createProcess(
            makeUserCpu(program, 10, 100)
        );

    const ProcessId second =
        table.createProcess(
            makeUserCpu(program, 20, 200)
        );

    CPU cpu;
    cpu.loadProgram(program, {});

    RoundRobinScheduler roundRobin;
    TimerPreemptiveScheduler preemptive(
        53,
        2
    );

    preemptive.start(
        cpu,
        table,
        roundRobin
    );

    const auto controller =
        preemptive.interruptController();

    controller->setGlobalEnabled(false);

    (void)preemptive.step(
        cpu,
        table,
        roundRobin
    );

    (void)preemptive.step(
        cpu,
        table,
        roundRobin
    );

    if (
        table.runningProcessId() != first
        || preemptive.preemptionCount() != 0
        || controller->pendingCount() != 1
    ) {
        detail =
            "global interrupt disable was ignored";
        return false;
    }

    controller->setGlobalEnabled(true);

    if (
        preemptive.step(
            cpu,
            table,
            roundRobin
        ) != second
        || cpu.state().pc() != 0
        || preemptive.preemptionCount() != 1
        || controller->pendingCount() != 0
    ) {
        detail =
            "deferred timer interrupt was not serviced";
        return false;
    }

    controller->mask(53);

    (void)preemptive.step(
        cpu,
        table,
        roundRobin
    );

    (void)preemptive.step(
        cpu,
        table,
        roundRobin
    );

    if (
        table.runningProcessId() != second
        || controller->pendingCount() != 1
        || preemptive.preemptionCount() != 1
    ) {
        detail = "masked timer interrupt was serviced";
        return false;
    }

    controller->unmask(53);

    if (
        preemptive.step(
            cpu,
            table,
            roundRobin
        ) != first
        || preemptive.preemptionCount() != 2
        || controller->pendingCount() != 0
    ) {
        detail =
            "unmasked timer interrupt was not serviced";
        return false;
    }

    return true;
}

bool unrelatedInterruptIsPreserved(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const std::vector<Instruction> program =
        standardProgram();

    ProcessTable table;

    const ProcessId first =
        table.createProcess(
            makeUserCpu(program, 1, 10)
        );

    const ProcessId second =
        table.createProcess(
            makeUserCpu(program, 2, 20)
        );

    CPU cpu;
    cpu.loadProgram(program, {});

    RoundRobinScheduler roundRobin;
    TimerPreemptiveScheduler preemptive(
        54,
        1
    );

    preemptive.start(
        cpu,
        table,
        roundRobin
    );

    const auto controller =
        preemptive.interruptController();

    controller->request(
        99,
        1234,
        "external"
    );

    if (
        preemptive.step(
            cpu,
            table,
            roundRobin
        ) != second
        || table.runningProcessId() != second
        || controller->pendingCount() != 1
    ) {
        detail =
            "timer preemption consumed unrelated interrupt";
        return false;
    }

    const std::vector<InterruptRequest> pending =
        controller->pendingRequests();

    if (
        pending.size() != 1
        || pending.front().vector != 99
        || pending.front().payload != 1234
        || pending.front().source != "external"
        || preemptive.preemptionCount() != 1
        || first == second
    ) {
        detail = "unrelated interrupt metadata changed";
        return false;
    }

    return true;
}

bool failedPreemptionIsAtomicAndRetryable(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const std::vector<Instruction> narrow =
        narrowProgram();

    ProcessTable table;

    const ProcessId first =
        table.createProcess(
            makeUserCpu(narrow, 10, 100)
        );

    const ProcessId invalidTarget =
        table.createProcess(
            makeUserCpu(
                standardProgram(),
                20,
                200
            )
        );

    CPU cpu;
    cpu.loadProgram(narrow, {});

    RoundRobinScheduler roundRobin;
    TimerPreemptiveScheduler preemptive(
        55,
        1
    );

    preemptive.start(
        cpu,
        table,
        roundRobin
    );

    if (
        !throwsRuntimeError(
            [&] {
                (void)preemptive.step(
                    cpu,
                    table,
                    roundRobin
                );
            }
        )
    ) {
        detail =
            "CPU-incompatible target was preempted into";
        return false;
    }

    const auto controller =
        preemptive.interruptController();

    if (
        table.runningProcessId() != first
        || table.process(first).state()
            != ProcessState::Running
        || table.process(invalidTarget).state()
            != ProcessState::Ready
        || roundRobin.lastSelectedProcessId()
            != first
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 11
        || cpu.state().pc() != 1
        || controller->pendingCount() != 1
        || preemptive.preemptionCount() != 0
        || preemptive.contextSwitchCount() != 0
    ) {
        detail =
            "failed preemption corrupted scheduler state";
        return false;
    }

    CPU validReplacement =
        makeUserCpu(narrow, 20, 200);

    ProcessContext replacement =
        captureProcessContext(
            invalidTarget,
            validReplacement
        );

    table.updateContext(
        invalidTarget,
        replacement
    );

    if (
        preemptive.step(
            cpu,
            table,
            roundRobin
        ) != invalidTarget
        || table.runningProcessId()
            != invalidTarget
        || table.process(first).context()
            .registers[
                static_cast<std::size_t>(
                    RegisterName::R0
                )
            ] != 11
        || table.process(first).context().pc != 1
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 20
        || cpu.state().pc() != 0
        || controller->pendingCount() != 0
        || preemptive.preemptionCount() != 1
        || preemptive.contextSwitchCount() != 1
    ) {
        detail =
            "retained timer interrupt was not retryable";
        return false;
    }

    return true;
}

bool disabledTimerDoesNotPreempt(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const std::vector<Instruction> program =
        standardProgram();

    ProcessTable table;

    const ProcessId first =
        table.createProcess(
            makeUserCpu(program, 1, 10)
        );

    const ProcessId second =
        table.createProcess(
            makeUserCpu(program, 2, 20)
        );

    CPU cpu;
    cpu.loadProgram(program, {});

    RoundRobinScheduler roundRobin;
    TimerPreemptiveScheduler preemptive(
        56,
        1
    );

    preemptive.start(
        cpu,
        table,
        roundRobin
    );

    preemptive.timerDevice()->setEnabled(false);

    (void)preemptive.step(
        cpu,
        table,
        roundRobin
    );

    (void)preemptive.step(
        cpu,
        table,
        roundRobin
    );

    if (
        table.runningProcessId() != first
        || table.process(second).state()
            != ProcessState::Ready
        || preemptive.timerDevice()
            ->interruptCount() != 0
        || preemptive.preemptionCount() != 0
    ) {
        detail = "disabled timer caused preemption";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Timer Preemptive "
           "Scheduler Test ===\n\n";

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
            "Constructor and attachment validation",
            constructorAndAttachValidation(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Quantum preemption round trip",
            quantumPreemptionRoundTrip(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Blocked and Terminated exclusion",
            blockedAndTerminatedAreSkipped(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Interrupt disable and mask handling",
            interruptDisableAndMaskAreHonored(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Unrelated interrupt preservation",
            unrelatedInterruptIsPreserved(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Failed preemption is retryable",
            failedPreemptionIsAtomicAndRetryable(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Disabled timer",
            disabledTimerDoesNotPreempt(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Timer preemptive scheduler test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Timer preemptive scheduler test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
