#include "zero_cpu/debug/MultiProcessDebugSession.hpp"

#include "zero_cpu/kernel/ExecutableMetadata.hpp"
#include "zero_cpu/kernel/ProcessControlBlock.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace zero_cpu::debug {
namespace {

void validateOptions(
    const MultiProcessDebugOptions& options
) {
    if (options.quantum == 0) {
        throw std::runtime_error(
            "Multi-process debugger quantum must "
            "be greater than zero"
        );
    }

    if (
        options.default_continue_steps
        == 0
    ) {
        throw std::runtime_error(
            "Multi-process debugger continue limit "
            "must be greater than zero"
        );
    }
}

} // namespace

const char*
multiProcessDebugStopReasonToString(
    MultiProcessDebugStopReason reason
) {
    switch (reason) {
    case MultiProcessDebugStopReason::Ready:
        return "Ready";

    case MultiProcessDebugStopReason::StepComplete:
        return "StepComplete";

    case MultiProcessDebugStopReason::ProcessTerminated:
        return "ProcessTerminated";

    case MultiProcessDebugStopReason::ProcessFaulted:
        return "ProcessFaulted";

    case MultiProcessDebugStopReason::RuntimeCompleted:
        return "RuntimeCompleted";

    case MultiProcessDebugStopReason::Deadlocked:
        return "Deadlocked";

    case MultiProcessDebugStopReason::StepLimit:
        return "StepLimit";
    }

    throw std::runtime_error(
        "Invalid multi-process debugger stop reason"
    );
}

bool ProcessDebugSnapshot::terminated() const {
    return state
        == kernel::ProcessState::Terminated
        && has_exit_code;
}

bool ProcessDebugSnapshot::faulted() const {
    return terminated()
        && termination_kind
            == kernel::ProcessTerminationKind::CpuFault;
}

MultiProcessDebugSession::MultiProcessDebugSession(
    const std::vector<std::string>& paths,
    const MultiProcessDebugOptions& options
)
    : options_(options),
      preemptive_(
          options.timer_vector,
          options.quantum,
          options.timer_payload
      ),
      lifecycle_(
          options.normal_exit_code,
          options.fault_exit_code
      ) {
    validateOptions(options_);

    if (paths.empty()) {
        throw std::runtime_error(
            "Multi-process debugger requires "
            "at least one executable"
        );
    }

    kernel::ProcessImageLoader loader;

    std::vector<kernel::ProcessImage> images;
    images.reserve(paths.size());

    for (const std::string& path : paths) {
        if (path.empty()) {
            throw std::runtime_error(
                "Executable path must not be empty"
            );
        }

        images.push_back(
            loader.loadFile(path)
        );
    }

    initialize(images);
}

MultiProcessDebugSession::MultiProcessDebugSession(
    const std::vector<kernel::ProcessImage>& images,
    const MultiProcessDebugOptions& options
)
    : options_(options),
      preemptive_(
          options.timer_vector,
          options.quantum,
          options.timer_payload
      ),
      lifecycle_(
          options.normal_exit_code,
          options.fault_exit_code
      ) {
    validateOptions(options_);
    initialize(images);
}

bool MultiProcessDebugSession::started() const {
    return started_;
}

kernel::ProcessRuntimeState
MultiProcessDebugSession::runtimeState() const {
    requireStarted();
    return runtime_state_;
}

std::size_t
MultiProcessDebugSession::totalSteps() const {
    requireStarted();
    return total_steps_;
}

kernel::ProcessId
MultiProcessDebugSession::runningPid() const {
    requireStarted();

    if (!table_.hasRunningProcess()) {
        return 0;
    }

    return table_.runningProcessId();
}

kernel::ProcessId
MultiProcessDebugSession::selectedPid() const {
    requireStarted();
    return selected_pid_;
}

void MultiProcessDebugSession::selectProcess(
    kernel::ProcessId pid
) {
    requireStarted();

    if (!table_.contains(pid)) {
        throw std::runtime_error(
            "Unknown process PID "
            + std::to_string(pid)
        );
    }

    selected_pid_ = pid;
}

ProcessDebugSnapshot
MultiProcessDebugSession::processSnapshot(
    kernel::ProcessId pid
) const {
    requireStarted();

    if (!table_.contains(pid)) {
        throw std::runtime_error(
            "Unknown process PID "
            + std::to_string(pid)
        );
    }

    const kernel::ProcessControlBlock& process =
        table_.process(pid);

    ProcessDebugSnapshot snapshot;
    snapshot.pid = pid;
    snapshot.state = process.state();

    snapshot.running =
        table_.hasRunningProcess()
        && table_.runningProcessId() == pid;

    if (snapshot.running) {
        snapshot.context =
            kernel::captureProcessContextSnapshot(
                pid,
                cpu_
            );

        snapshot.memory =
            cpu_.state().memory();
    } else {
        snapshot.context =
            process.context();

        snapshot.memory =
            process.addressSpace().memory();
    }

    snapshot.has_exit_code =
        process.hasExitCode();

    if (snapshot.has_exit_code) {
        snapshot.exit_code =
            process.exitCode();

        snapshot.termination_kind =
            process.terminationKind();

        snapshot.termination_message =
            process.terminationMessage();
    }

    snapshot.has_executable_image =
        process.addressSpace()
            .hasExecutableImage();

    if (snapshot.has_executable_image) {
        const kernel::ExecutableMetadata& metadata =
            process.addressSpace()
                .executableMetadata();

        snapshot.source_name =
            metadata.source_name;

        snapshot.code_end_exclusive =
            metadata.code_end_exclusive;

        snapshot.data_base =
            metadata.data_base;

        snapshot.data_size =
            metadata.data_size;
    }

    return snapshot;
}

ProcessDebugSnapshot
MultiProcessDebugSession::
selectedProcessSnapshot() const {
    requireStarted();

    if (selected_pid_ == 0) {
        throw std::runtime_error(
            "No process is selected"
        );
    }

    return processSnapshot(
        selected_pid_
    );
}

std::vector<ProcessDebugSnapshot>
MultiProcessDebugSession::processSnapshots() const {
    requireStarted();

    std::vector<ProcessDebugSnapshot> result;
    result.reserve(table_.size());

    for (
        const kernel::ProcessId pid :
        table_.processIds()
    ) {
        result.push_back(
            processSnapshot(pid)
        );
    }

    return result;
}

const MultiProcessDebugStop&
MultiProcessDebugSession::lastStop() const {
    requireStarted();
    return last_stop_;
}

MultiProcessDebugStop
MultiProcessDebugSession::step() {
    requireStarted();

    if (
        runtime_state_
        != kernel::ProcessRuntimeState::Running
    ) {
        return makeTerminalStop(0);
    }

    if (!table_.hasRunningProcess()) {
        throw std::runtime_error(
            "Running debugger has no Running process"
        );
    }

    const kernel::ProcessId before =
        table_.runningProcessId();

    const std::uint64_t
        preemptionsBefore =
            preemptive_.preemptionCount();

    if (reachedExecutableEnd()) {
        discardCompletionTimerRequest();
        cpu_.state().setHalted(true);
    }

    const kernel::ProcessLifecycleStepResult result =
        lifecycle_.step(
            cpu_,
            table_,
            scheduler_,
            preemptive_
        );

    ++total_steps_;

    runtime_state_ = result.state;

    const kernel::ProcessId after =
        table_.hasRunningProcess()
            ? table_.runningProcessId()
            : 0;

    const bool preempted =
        preemptive_.preemptionCount()
            > preemptionsBefore;

    if (before != after) {
        recordContextSwitch(
            before,
            after,
            preempted,
            result.process_terminated
        );
    }

    if (result.process_terminated) {
        std::string message =
            "PID "
            + std::to_string(
                result.terminated_pid
            )
            + (
                result.process_faulted
                    ? " faulted"
                    : " terminated"
            );

        if (result.process_faulted) {
            const kernel::ProcessControlBlock&
                process =
                    table_.process(
                        result.terminated_pid
                    );

            if (
                !process
                    .terminationMessage()
                    .empty()
            ) {
                message += ": ";
                message +=
                    process.terminationMessage();
            }
        }

        MultiProcessDebugStop stop =
            makeStop(
                result.process_faulted
                    ? MultiProcessDebugStopReason::
                        ProcessFaulted
                    : MultiProcessDebugStopReason::
                        ProcessTerminated,
                1,
                message
            );

        stop.process_terminated = true;
        stop.process_faulted =
            result.process_faulted;

        stop.terminated_pid =
            result.terminated_pid;

        last_stop_ = stop;
        return last_stop_;
    }

    if (
        runtime_state_
        != kernel::ProcessRuntimeState::Running
    ) {
        return makeTerminalStop(1);
    }

    return makeStop(
        MultiProcessDebugStopReason::StepComplete,
        1
    );
}

MultiProcessDebugStop
MultiProcessDebugSession::continueExecution(
    std::size_t maxSteps
) {
    requireStarted();

    if (maxSteps == 0) {
        throw std::runtime_error(
            "Multi-process debugger continue limit "
            "must be greater than zero"
        );
    }

    if (
        runtime_state_
        != kernel::ProcessRuntimeState::Running
    ) {
        return makeTerminalStop(0);
    }

    std::size_t executed = 0;

    while (executed < maxSteps) {
        MultiProcessDebugStop stop =
            step();

        ++executed;

        if (
            stop.reason
            != MultiProcessDebugStopReason::
                StepComplete
        ) {
            stop.executed_steps = executed;
            last_stop_ = stop;
            return last_stop_;
        }
    }

    return makeStop(
        MultiProcessDebugStopReason::StepLimit,
        executed,
        "Multi-process debugger continue "
        "step limit reached"
    );
}

std::uint64_t
MultiProcessDebugSession::preemptionCount() const {
    requireStarted();

    return preemptive_.preemptionCount();
}

std::uint64_t
MultiProcessDebugSession::
schedulerContextSwitchCount() const {
    requireStarted();

    return preemptive_.contextSwitchCount();
}

const std::vector<ContextSwitchRecord>&
MultiProcessDebugSession::contextSwitches() const {
    requireStarted();
    return context_switches_;
}

const CPU& MultiProcessDebugSession::cpu() const {
    requireStarted();
    return cpu_;
}

std::uint64_t
MultiProcessDebugSession::quantum() const {
    return options_.quantum;
}

void MultiProcessDebugSession::initialize(
    const std::vector<kernel::ProcessImage>& images
) {
    if (images.empty()) {
        throw std::runtime_error(
            "Multi-process debugger requires "
            "at least one process image"
        );
    }

    for (
        const kernel::ProcessImage& image :
        images
    ) {
        kernel::validateProcessImage(image);

        (void)table_.createProcess(
            image
        );
    }

    runtime_state_ =
        lifecycle_.start(
            cpu_,
            table_,
            scheduler_,
            preemptive_
        );

    started_ = true;

    selected_pid_ =
        table_.hasRunningProcess()
            ? table_.runningProcessId()
            : table_.processIds().front();

    last_stop_ = makeStop(
        MultiProcessDebugStopReason::Ready,
        0
    );
}

void MultiProcessDebugSession::requireStarted() const {
    if (!started_) {
        throw std::runtime_error(
            "Multi-process debugger is not started"
        );
    }
}

bool
MultiProcessDebugSession::reachedExecutableEnd()
const {
    if (
        !table_.hasRunningProcess()
        || cpu_.state().hasError()
    ) {
        return false;
    }

    const kernel::ProcessControlBlock& process =
        table_.process(
            table_.runningProcessId()
        );

    if (
        !process.addressSpace()
            .hasExecutableImage()
    ) {
        return false;
    }

    return cpu_.state().pc()
        == process.addressSpace()
            .executableMetadata()
            .code_end_exclusive;
}

void MultiProcessDebugSession::
discardCompletionTimerRequest() {
    constexpr const char* kTimerSource =
        "timer";

    const auto controller =
        preemptive_.interruptController();

    const auto timer =
        preemptive_.timerDevice();

    while (
        controller->hasPending(
            timer->vector(),
            kTimerSource
        )
    ) {
        (void)controller->acknowledge(
            timer->vector(),
            kTimerSource
        );
    }
}

MultiProcessDebugStop
MultiProcessDebugSession::makeStop(
    MultiProcessDebugStopReason reason,
    std::size_t executedSteps,
    std::string message
) {
    last_stop_.reason = reason;

    last_stop_.runtime_state =
        runtime_state_;

    last_stop_.executed_steps =
        executedSteps;

    last_stop_.total_steps =
        total_steps_;

    last_stop_.running_pid =
        table_.hasRunningProcess()
            ? table_.runningProcessId()
            : 0;

    last_stop_.selected_pid =
        selected_pid_;

    last_stop_.process_terminated = false;
    last_stop_.process_faulted = false;
    last_stop_.terminated_pid = 0;

    last_stop_.message =
        std::move(message);

    return last_stop_;
}

MultiProcessDebugStop
MultiProcessDebugSession::makeTerminalStop(
    std::size_t executedSteps
) {
    switch (runtime_state_) {
    case kernel::ProcessRuntimeState::Completed:
        return makeStop(
            MultiProcessDebugStopReason::
                RuntimeCompleted,
            executedSteps,
            "All processes completed"
        );

    case kernel::ProcessRuntimeState::Deadlocked:
        return makeStop(
            MultiProcessDebugStopReason::Deadlocked,
            executedSteps,
            "Process runtime is deadlocked"
        );

    case kernel::ProcessRuntimeState::Ready:
        return makeStop(
            MultiProcessDebugStopReason::Ready,
            executedSteps
        );

    case kernel::ProcessRuntimeState::Running:
        return makeStop(
            MultiProcessDebugStopReason::
                StepComplete,
            executedSteps
        );
    }

    throw std::runtime_error(
        "Invalid process runtime state"
    );
}

void MultiProcessDebugSession::recordContextSwitch(
    kernel::ProcessId before,
    kernel::ProcessId after,
    bool preempted,
    bool causedByTermination
) {
    ContextSwitchRecord record;

    record.lifecycle_step =
        total_steps_;

    record.from_pid = before;
    record.to_pid = after;
    record.preempted = preempted;

    record.caused_by_termination =
        causedByTermination;

    context_switches_.push_back(
        record
    );
}

} // namespace zero_cpu::debug
