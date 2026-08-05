#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"
#include "zero_cpu/kernel/ProcessTable.hpp"
#include "zero_cpu/kernel/RoundRobinScheduler.hpp"

#include <cstddef>
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

zero_cpu::CPU makeProcessCpu(
    std::int64_t seed
) {
    using namespace zero_cpu;

    CPU cpu;

    cpu.loadProgram(
        {
            Instruction(Opcode::NOP),
            Instruction(Opcode::NOP),
            Instruction(Opcode::HALT)
        },
        {}
    );

    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    cpu.state().registers().set(
        RegisterName::R0,
        seed
    );

    cpu.state().setSp(
        memory_map::kUserStackBase
    );

    return cpu;
}

std::vector<zero_cpu::kernel::ProcessId>
createProcesses(
    zero_cpu::kernel::ProcessTable& table,
    std::size_t count
) {
    using namespace zero_cpu::kernel;

    std::vector<ProcessId> result;

    for (
        std::size_t index = 0;
        index < count;
        ++index
    ) {
        zero_cpu::CPU cpu = makeProcessCpu(
            static_cast<std::int64_t>(
                100 + index
            )
        );

        result.push_back(
            table.createProcess(cpu)
        );
    }

    return result;
}

bool queueOrdering(std::string& detail) {
    using namespace zero_cpu::kernel;

    ProcessTable table;
    const std::vector<ProcessId> pids =
        createProcesses(table, 3);

    RoundRobinScheduler scheduler;

    if (
        scheduler.hasLastSelection()
        || scheduler.readyQueue(table) != pids
    ) {
        detail = "initial Ready queue mismatch";
        return false;
    }

    if (scheduler.scheduleNext(table) != pids[0]) {
        detail = "first selection mismatch";
        return false;
    }

    const std::vector<ProcessId> afterFirst = {
        pids[1],
        pids[2]
    };

    if (scheduler.readyQueue(table) != afterFirst) {
        detail = "Ready queue after first selection mismatch";
        return false;
    }

    if (scheduler.scheduleNext(table) != pids[1]) {
        detail = "second selection mismatch";
        return false;
    }

    const std::vector<ProcessId> afterSecond = {
        pids[2],
        pids[0]
    };

    if (scheduler.readyQueue(table) != afterSecond) {
        detail =
            "rotated Ready queue ordering mismatch";
        return false;
    }

    return true;
}

bool fairRotation(std::string& detail) {
    using namespace zero_cpu::kernel;

    ProcessTable table;
    const std::vector<ProcessId> pids =
        createProcesses(table, 3);

    RoundRobinScheduler scheduler;

    const std::vector<ProcessId> expected = {
        pids[0],
        pids[1],
        pids[2],
        pids[0],
        pids[1],
        pids[2]
    };

    for (const ProcessId expectedPid : expected) {
        const ProcessId actual =
            scheduler.scheduleNext(table);

        if (actual != expectedPid) {
            detail = "round-robin sequence mismatch";
            return false;
        }

        const std::vector<ProcessId> running =
            table.processIdsInState(
                ProcessState::Running
            );

        if (
            running.size() != 1
            || running.front() != expectedPid
            || table.runningProcessId()
                != expectedPid
        ) {
            detail =
                "Running process invariant mismatch";
            return false;
        }

        if (
            table.processIdsInState(
                ProcessState::Ready
            ).size() != 2
        ) {
            detail =
                "Ready process count mismatch";
            return false;
        }
    }

    return true;
}

bool blockedAndTerminatedAreSkipped(
    std::string& detail
) {
    using namespace zero_cpu::kernel;

    ProcessTable table;
    const std::vector<ProcessId> pids =
        createProcesses(table, 4);

    RoundRobinScheduler scheduler;

    scheduler.scheduleNext(table);
    scheduler.scheduleNext(table);

    table.transition(
        pids[1],
        ProcessState::Blocked
    );

    if (
        scheduler.scheduleNext(table)
        != pids[2]
    ) {
        detail = "Blocked process was not skipped";
        return false;
    }

    table.terminate(pids[2], 0);

    if (
        scheduler.scheduleNext(table)
        != pids[3]
    ) {
        detail = "Terminated process was not skipped";
        return false;
    }

    table.transition(
        pids[1],
        ProcessState::Ready
    );

    if (
        scheduler.scheduleNext(table)
            != pids[0]
        || scheduler.scheduleNext(table)
            != pids[1]
    ) {
        detail =
            "unblocked process did not re-enter rotation";
        return false;
    }

    if (
        table.process(pids[1]).state()
            != ProcessState::Running
        || table.process(pids[2]).state()
            != ProcessState::Terminated
    ) {
        detail = "final process states mismatch";
        return false;
    }

    return true;
}

bool singleRunnableKeepsRunning(
    std::string& detail
) {
    using namespace zero_cpu::kernel;

    ProcessTable table;
    const ProcessId pid =
        createProcesses(table, 1).front();

    RoundRobinScheduler scheduler;

    if (
        scheduler.scheduleNext(table) != pid
        || scheduler.scheduleNext(table) != pid
        || table.runningProcessId() != pid
        || table.process(pid).state()
            != ProcessState::Running
    ) {
        detail =
            "single runnable process was disturbed";
        return false;
    }

    return true;
}

bool noRunnableFailureIsAtomic(
    std::string& detail
) {
    using namespace zero_cpu::kernel;

    ProcessTable table;
    const ProcessId pid =
        createProcesses(table, 1).front();

    RoundRobinScheduler scheduler;
    scheduler.scheduleNext(table);

    table.transition(
        pid,
        ProcessState::Blocked
    );

    const ProcessId cursorBefore =
        scheduler.lastSelectedProcessId();

    if (
        scheduler.hasRunnableProcess(table)
        || !scheduler.readyQueue(table).empty()
    ) {
        detail =
            "Blocked-only table reported runnable work";
        return false;
    }

    if (
        !throwsRuntimeError(
            [&] {
                (void)scheduler.scheduleNext(table);
            }
        )
    ) {
        detail =
            "scheduler accepted table with no runnable process";
        return false;
    }

    if (
        table.hasRunningProcess()
        || table.process(pid).state()
            != ProcessState::Blocked
        || scheduler.lastSelectedProcessId()
            != cursorBefore
    ) {
        detail =
            "failed scheduling attempt changed state";
        return false;
    }

    return true;
}

bool adoptsExistingRunningProcess(
    std::string& detail
) {
    using namespace zero_cpu::kernel;

    ProcessTable table;
    const std::vector<ProcessId> pids =
        createProcesses(table, 3);

    table.transition(
        pids[1],
        ProcessState::Running
    );

    RoundRobinScheduler scheduler;

    if (
        scheduler.scheduleNext(table)
        != pids[2]
    ) {
        detail =
            "existing Running process cursor was ignored";
        return false;
    }

    if (
        table.process(pids[1]).state()
            != ProcessState::Ready
        || table.process(pids[2]).state()
            != ProcessState::Running
    ) {
        detail =
            "existing Running process was not rotated";
        return false;
    }

    return true;
}

bool resetClearsHistory(std::string& detail) {
    using namespace zero_cpu::kernel;

    ProcessTable table;
    const std::vector<ProcessId> pids =
        createProcesses(table, 3);

    RoundRobinScheduler scheduler;

    scheduler.scheduleNext(table);
    scheduler.scheduleNext(table);

    if (
        !scheduler.hasLastSelection()
        || scheduler.lastSelectedProcessId()
            != pids[1]
    ) {
        detail = "selection history mismatch";
        return false;
    }

    scheduler.reset();

    if (
        scheduler.hasLastSelection()
        || !throwsRuntimeError(
            [&] {
                (void)scheduler
                    .lastSelectedProcessId();
            }
        )
    ) {
        detail = "scheduler reset did not clear history";
        return false;
    }

    if (
        scheduler.scheduleNext(table)
        != pids[2]
    ) {
        detail =
            "reset did not adopt current Running cursor";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Round-Robin "
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
            "Ready queue ordering",
            queueOrdering(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Fair round-robin rotation",
            fairRotation(detail),
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
            "Single runnable process",
            singleRunnableKeepsRunning(detail),
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
            "Adopt existing Running process",
            adoptsExistingRunningProcess(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Scheduler reset",
            resetClearsHistory(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Round-robin scheduler test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Round-robin scheduler test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
