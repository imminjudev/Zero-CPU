#include "zero_cpu/kernel/ProcessLifecycleManager.hpp"

#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/kernel/ProcessContext.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace zero_cpu::kernel {

const char* processRuntimeStateToString(
    ProcessRuntimeState state
) {
    switch (state) {
    case ProcessRuntimeState::Ready:
        return "Ready";

    case ProcessRuntimeState::Running:
        return "Running";

    case ProcessRuntimeState::Completed:
        return "Completed";

    case ProcessRuntimeState::Deadlocked:
        return "Deadlocked";
    }

    throw std::runtime_error(
        "Invalid process runtime state"
    );
}

ProcessLifecycleManager::ProcessLifecycleManager(
    std::int64_t normalExitCode,
    std::int64_t faultExitCode
)
    : normal_exit_code_(normalExitCode),
      fault_exit_code_(faultExitCode) {
}

bool ProcessLifecycleManager::started() const {
    return started_;
}

ProcessRuntimeState
ProcessLifecycleManager::state() const {
    return state_;
}

std::size_t
ProcessLifecycleManager::terminationCount() const {
    return termination_count_;
}

std::size_t
ProcessLifecycleManager::faultCount() const {
    return fault_count_;
}

ProcessRuntimeState
ProcessLifecycleManager::start(
    CPU& cpu,
    ProcessTable& table,
    RoundRobinScheduler& scheduler,
    TimerPreemptiveScheduler& preemptive
) {
    if (started_) {
        throw std::runtime_error(
            "Process lifecycle manager is already started"
        );
    }

    if (!scheduler.hasRunnableProcess(table)) {
        state_ = classifyWithoutRunning(table);
        started_ = true;
        cpu.state().setHalted(true);
        return state_;
    }

    (void)preemptive.start(
        cpu,
        table,
        scheduler
    );

    started_ = true;
    state_ = ProcessRuntimeState::Running;
    return state_;
}

ProcessLifecycleStepResult
ProcessLifecycleManager::step(
    CPU& cpu,
    ProcessTable& table,
    RoundRobinScheduler& scheduler,
    TimerPreemptiveScheduler& preemptive
) {
    if (!started_) {
        throw std::runtime_error(
            "Process lifecycle manager is not started"
        );
    }

    ProcessLifecycleStepResult result;
    result.state = state_;

    if (state_ != ProcessRuntimeState::Running) {
        return result;
    }

    if (!table.hasRunningProcess()) {
        throw std::runtime_error(
            "Running lifecycle has no Running process"
        );
    }

    (void)preemptive.step(
        cpu,
        table,
        scheduler
    );

    if (
        !cpu.state().halted()
        && !cpu.state().hasError()
    ) {
        result.state = state_;
        result.running_pid =
            table.runningProcessId();
        return result;
    }

    const ProcessId terminatedPid =
        table.runningProcessId();

    const bool faulted =
        cpu.state().hasError();

    const std::string faultMessage =
        faulted
            ? cpu.state().errorMessage()
            : std::string();

    const ProcessContext finalContext =
        captureProcessContextSnapshot(
            terminatedPid,
            cpu
        );

    ProcessTable stagedTable = table;
    RoundRobinScheduler stagedScheduler =
        scheduler;

    if (faulted) {
        stagedTable.fault(
            terminatedPid,
            finalContext,
            fault_exit_code_,
            faultMessage
        );
    } else {
        stagedTable.terminate(
            terminatedPid,
            finalContext,
            normal_exit_code_
        );
    }

    ProcessId nextPid = 0;

    if (
        !stagedTable.processIdsInState(
            ProcessState::Ready
        ).empty()
    ) {
        nextPid = dispatcher_.dispatchNext(
            cpu,
            stagedTable,
            stagedScheduler
        );
    }

    table = std::move(stagedTable);
    scheduler = std::move(stagedScheduler);

    ++termination_count_;

    if (faulted) {
        ++fault_count_;
    }

    result.process_terminated = true;
    result.process_faulted = faulted;
    result.terminated_pid = terminatedPid;

    if (nextPid != 0) {
        state_ = ProcessRuntimeState::Running;
        result.running_pid = nextPid;
    } else {
        state_ = classifyWithoutRunning(table);
        cpu.state().setHalted(true);
    }

    result.state = state_;
    return result;
}

ProcessRuntimeState
ProcessLifecycleManager::classifyWithoutRunning(
    const ProcessTable& table
) const {
    if (
        !table.processIdsInState(
            ProcessState::Ready
        ).empty()
    ) {
        return ProcessRuntimeState::Ready;
    }

    if (
        !table.processIdsInState(
            ProcessState::Blocked
        ).empty()
    ) {
        return ProcessRuntimeState::Deadlocked;
    }

    return ProcessRuntimeState::Completed;
}

} // namespace zero_cpu::kernel
