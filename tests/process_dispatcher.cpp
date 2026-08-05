#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/Operand.hpp"
#include "zero_cpu/kernel/ProcessContext.hpp"
#include "zero_cpu/kernel/ProcessDispatcher.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"
#include "zero_cpu/kernel/ProcessTable.hpp"
#include "zero_cpu/kernel/RoundRobinScheduler.hpp"

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

std::vector<zero_cpu::Instruction>
processProgram() {
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
        Instruction(Opcode::NOP),
        Instruction(Opcode::HALT)
    };
}

zero_cpu::CPU makeUserCpu(
    std::int64_t r0,
    std::int64_t r1,
    std::size_t spOffset
) {
    using namespace zero_cpu;

    CPU cpu;
    cpu.loadProgram(processProgram(), {});

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
        + spOffset
    );

    return cpu;
}

struct CpuView {
    std::int64_t r0 = 0;
    std::int64_t r1 = 0;
    std::uint32_t flags = 0;
    std::size_t pc = 0;
    std::size_t sp = 0;
    zero_cpu::PrivilegeLevel privilege =
        zero_cpu::PrivilegeLevel::Kernel;
    bool has_user_code_range = false;
    std::size_t user_code_begin = 0;
    std::size_t user_code_end = 0;
    bool halted = false;
    bool has_error = false;
};

CpuView viewOf(const zero_cpu::CPU& cpu) {
    using namespace zero_cpu;

    CpuView view;
    view.r0 = cpu.state().registers().get(
        RegisterName::R0
    );
    view.r1 = cpu.state().registers().get(
        RegisterName::R1
    );
    view.flags = cpu.state().flags().raw();
    view.pc = cpu.state().pc();
    view.sp = cpu.state().sp();
    view.privilege =
        cpu.state().privilegeLevel();
    view.has_user_code_range =
        cpu.hasUserCodeRange();

    if (view.has_user_code_range) {
        view.user_code_begin =
            cpu.userCodeBegin();
        view.user_code_end =
            cpu.userCodeEndExclusive();
    }

    view.halted = cpu.state().halted();
    view.has_error = cpu.state().hasError();
    return view;
}

bool matches(
    const zero_cpu::CPU& cpu,
    const CpuView& expected
) {
    const CpuView actual = viewOf(cpu);

    return actual.r0 == expected.r0
        && actual.r1 == expected.r1
        && actual.flags == expected.flags
        && actual.pc == expected.pc
        && actual.sp == expected.sp
        && actual.privilege
            == expected.privilege
        && actual.has_user_code_range
            == expected.has_user_code_range
        && (
            !actual.has_user_code_range
            || (
                actual.user_code_begin
                    == expected.user_code_begin
                && actual.user_code_end
                    == expected.user_code_end
            )
        )
        && actual.halted == expected.halted
        && actual.has_error
            == expected.has_error;
}

bool initialActivation(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    CPU processOne = makeUserCpu(
        10,
        100,
        CPU::kStackSlotSize
    );

    processOne.state().flags().setCarry(true);

    CPU processTwo = makeUserCpu(
        20,
        200,
        CPU::kStackSlotSize * 2
    );

    processTwo.state().flags().setZero(true);

    ProcessTable table;

    const ProcessId first =
        table.createProcess(processOne);

    const ProcessId second =
        table.createProcess(processTwo);

    CPU cpu;
    cpu.loadProgram(processProgram(), {});

    cpu.state().registers().set(
        RegisterName::R0,
        -1
    );

    RoundRobinScheduler scheduler;
    ProcessDispatcher dispatcher;

    const ProcessId selected =
        dispatcher.dispatchNext(
            cpu,
            table,
            scheduler
        );

    if (
        selected != first
        || table.runningProcessId() != first
        || table.process(first).state()
            != ProcessState::Running
        || table.process(second).state()
            != ProcessState::Ready
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 10
        || cpu.state().registers().get(
            RegisterName::R1
        ) != 100
        || cpu.state().sp()
            != memory_map::kUserStackBase
                + CPU::kStackSlotSize
        || !cpu.state().flags().carry()
        || !cpu.state().isUserMode()
        || scheduler.lastSelectedProcessId()
            != first
    ) {
        detail = "initial process activation mismatch";
        return false;
    }

    return true;
}

bool executionFlowRoundTrip(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessTable table;

    CPU firstSource = makeUserCpu(
        10,
        100,
        CPU::kStackSlotSize
    );

    CPU secondSource = makeUserCpu(
        20,
        200,
        CPU::kStackSlotSize * 2
    );

    const ProcessId first =
        table.createProcess(firstSource);

    const ProcessId second =
        table.createProcess(secondSource);

    CPU cpu;
    cpu.loadProgram(processProgram(), {});

    RoundRobinScheduler scheduler;
    ProcessDispatcher dispatcher;

    dispatcher.dispatchNext(
        cpu,
        table,
        scheduler
    );

    cpu.step();

    if (
        cpu.state().registers().get(
            RegisterName::R0
        ) != 11
        || cpu.state().pc() != 1
    ) {
        detail = "first process did not execute";
        return false;
    }

    dispatcher.dispatchNext(
        cpu,
        table,
        scheduler
    );

    if (
        table.runningProcessId() != second
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
        || cpu.state().registers().get(
            RegisterName::R1
        ) != 200
        || cpu.state().pc() != 0
    ) {
        detail =
            "first-to-second context switch mismatch";
        return false;
    }

    cpu.step();

    dispatcher.dispatchNext(
        cpu,
        table,
        scheduler
    );

    if (
        table.runningProcessId() != first
        || table.process(second).context()
            .registers[
                static_cast<std::size_t>(
                    RegisterName::R0
                )
            ] != 21
        || table.process(second).context().pc != 1
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 11
        || cpu.state().registers().get(
            RegisterName::R1
        ) != 100
        || cpu.state().pc() != 1
    ) {
        detail =
            "second-to-first context switch mismatch";
        return false;
    }

    cpu.step();

    dispatcher.dispatchNext(
        cpu,
        table,
        scheduler
    );

    if (
        table.runningProcessId() != second
        || table.process(first).context()
            .registers[
                static_cast<std::size_t>(
                    RegisterName::R1
                )
            ] != 110
        || table.process(first).context().pc != 2
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 21
        || cpu.state().registers().get(
            RegisterName::R1
        ) != 200
        || cpu.state().pc() != 1
    ) {
        detail =
            "independent execution flow was not preserved";
        return false;
    }

    return true;
}

bool blockedProcessIsSkipped(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessTable table;

    CPU firstSource = makeUserCpu(1, 10, 0);
    CPU secondSource = makeUserCpu(
        2,
        20,
        CPU::kStackSlotSize
    );

    const ProcessId first =
        table.createProcess(firstSource);

    const ProcessId second =
        table.createProcess(secondSource);

    table.transition(
        second,
        ProcessState::Running
    );

    table.transition(
        second,
        ProcessState::Blocked
    );

    CPU cpu;
    cpu.loadProgram(processProgram(), {});

    RoundRobinScheduler scheduler;
    ProcessDispatcher dispatcher;

    if (
        dispatcher.dispatchNext(
            cpu,
            table,
            scheduler
        ) != first
    ) {
        detail = "first runnable selection mismatch";
        return false;
    }

    cpu.state().registers().set(
        RegisterName::R6,
        606
    );

    if (
        dispatcher.dispatchNext(
            cpu,
            table,
            scheduler
        ) != first
        || table.runningProcessId() != first
        || table.process(second).state()
            != ProcessState::Blocked
        || cpu.state().registers().get(
            RegisterName::R6
        ) != 606
    ) {
        detail =
            "Blocked process entered dispatcher rotation";
        return false;
    }

    return true;
}

bool invalidTargetIsAtomic(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    CPU wideSource;
    wideSource.loadProgram(
        {
            Instruction(Opcode::NOP),
            Instruction(Opcode::NOP),
            Instruction(Opcode::NOP),
            Instruction(Opcode::HALT)
        },
        {}
    );

    wideSource.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    ProcessTable table;
    const ProcessId pid =
        table.createProcess(wideSource);

    CPU narrowCpu;
    narrowCpu.loadProgram(
        {
            Instruction(Opcode::NOP),
            Instruction(Opcode::HALT)
        },
        {}
    );

    narrowCpu.state().registers().set(
        RegisterName::R0,
        777
    );

    const CpuView before = viewOf(narrowCpu);

    RoundRobinScheduler scheduler;
    ProcessDispatcher dispatcher;

    if (
        !throwsRuntimeError(
            [&] {
                (void)dispatcher.dispatchNext(
                    narrowCpu,
                    table,
                    scheduler
                );
            }
        )
    ) {
        detail =
            "CPU-incompatible process context was accepted";
        return false;
    }

    if (
        !matches(narrowCpu, before)
        || table.hasRunningProcess()
        || table.process(pid).state()
            != ProcessState::Ready
        || scheduler.hasLastSelection()
    ) {
        detail =
            "failed dispatch changed CPU or scheduler state";
        return false;
    }

    return true;
}

bool activeInterruptGuardIsAtomic(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    CPU source = makeUserCpu(9, 90, 0);

    ProcessTable table;
    const ProcessId pid =
        table.createProcess(source);

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

    controller->setVectorHandler(48, 1);
    active.setInterruptController(controller);

    controller->request(
        48,
        0,
        "process-dispatcher-test"
    );

    active.step();

    if (
        active.state().hasError()
        || !active.usingKernelInterruptStack()
    ) {
        detail = "interrupt setup failed";
        return false;
    }

    const CpuView before = viewOf(active);

    RoundRobinScheduler scheduler;
    ProcessDispatcher dispatcher;

    if (
        !throwsRuntimeError(
            [&] {
                (void)dispatcher.dispatchNext(
                    active,
                    table,
                    scheduler
                );
            }
        )
    ) {
        detail =
            "dispatch succeeded on active interrupt stack";
        return false;
    }

    if (
        !matches(active, before)
        || table.hasRunningProcess()
        || table.process(pid).state()
            != ProcessState::Ready
        || scheduler.hasLastSelection()
    ) {
        detail =
            "interrupt-guard failure changed state";
        return false;
    }

    return true;
}

bool noRunnableFailureIsAtomic(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    CPU source = makeUserCpu(5, 50, 0);

    ProcessTable table;
    const ProcessId pid =
        table.createProcess(source);

    table.transition(
        pid,
        ProcessState::Running
    );

    table.transition(
        pid,
        ProcessState::Blocked
    );

    CPU cpu;
    cpu.loadProgram(processProgram(), {});
    cpu.state().registers().set(
        RegisterName::R0,
        5150
    );

    const CpuView before = viewOf(cpu);

    RoundRobinScheduler scheduler;
    ProcessDispatcher dispatcher;

    if (
        !throwsRuntimeError(
            [&] {
                (void)dispatcher.dispatchNext(
                    cpu,
                    table,
                    scheduler
                );
            }
        )
    ) {
        detail =
            "dispatcher accepted no-runnable table";
        return false;
    }

    if (
        !matches(cpu, before)
        || table.hasRunningProcess()
        || table.process(pid).state()
            != ProcessState::Blocked
        || scheduler.hasLastSelection()
    ) {
        detail =
            "no-runnable failure changed state";
        return false;
    }

    return true;
}

bool kernelContextClearsUserRange(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessContext kernelContext;
    kernelContext.pid = 99;
    kernelContext.privilege =
        PrivilegeLevel::Kernel;
    kernelContext.has_user_code_range = false;
    kernelContext.pc = 0;
    kernelContext.sp =
        memory_map::kUserStackBase;
    kernelContext.kernel_stack_pointer =
        memory_map::kKernelStackBase;

    ProcessTable table;
    const ProcessId pid =
        table.createProcess(kernelContext);

    CPU cpu;
    cpu.loadProgram(processProgram(), {});

    if (!cpu.hasUserCodeRange()) {
        detail =
            "test CPU did not start with User range";
        return false;
    }

    RoundRobinScheduler scheduler;
    ProcessDispatcher dispatcher;

    if (
        dispatcher.dispatchNext(
            cpu,
            table,
            scheduler
        ) != pid
        || cpu.hasUserCodeRange()
        || !cpu.state().isKernelMode()
    ) {
        detail =
            "Kernel context retained stale User code range";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Process Dispatcher "
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
            "Initial process activation",
            initialActivation(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Independent execution flow round trip",
            executionFlowRoundTrip(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Blocked process exclusion",
            blockedProcessIsSkipped(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Invalid target failure is atomic",
            invalidTargetIsAtomic(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Active interrupt guard is atomic",
            activeInterruptGuardIsAtomic(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "No-runnable failure is atomic",
            noRunnableFailureIsAtomic(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Kernel context clears User range",
            kernelContextClearsUserRange(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Process dispatcher test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Process dispatcher test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
