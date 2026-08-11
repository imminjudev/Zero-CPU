#include "zero_cpu/debug/MultiProcessDebugSession.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/kernel/ExecutableMetadata.hpp"
#include "zero_cpu/kernel/ProcessControlBlock.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
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

const char* processMemoryWatchModeToString(
    ProcessMemoryWatchMode mode
) {
    switch (mode) {
    case ProcessMemoryWatchMode::Read:
        return "Read";

    case ProcessMemoryWatchMode::Write:
        return "Write";

    case ProcessMemoryWatchMode::Access:
        return "Access";
    }

    throw std::runtime_error(
        "Invalid process memory watch mode"
    );
}

std::size_t
ProcessMemoryWatchpoint::endExclusive() const {
    return address + size;
}

const char*
multiProcessDebugStopReasonToString(
    MultiProcessDebugStopReason reason
) {
    switch (reason) {
    case MultiProcessDebugStopReason::Ready:
        return "Ready";

    case MultiProcessDebugStopReason::StepComplete:
        return "StepComplete";

    case MultiProcessDebugStopReason::Breakpoint:
        return "Breakpoint";

    case MultiProcessDebugStopReason::
        ConditionalBreakpoint:
        return "ConditionalBreakpoint";

    case MultiProcessDebugStopReason::Watchpoint:
        return "Watchpoint";

    case MultiProcessDebugStopReason::
        ProcessTerminated:
        return "ProcessTerminated";

    case MultiProcessDebugStopReason::
        ProcessFaulted:
        return "ProcessFaulted";

    case MultiProcessDebugStopReason::
        RuntimeCompleted:
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
    loadAutomaticSymbols(paths);
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
    (void)requireProcess(pid);
    selected_pid_ = pid;
}

ProcessDebugSnapshot
MultiProcessDebugSession::processSnapshot(
    kernel::ProcessId pid
) const {
    requireStarted();

    const kernel::ProcessControlBlock& process =
        requireProcess(pid);

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

        snapshot.code_base =
            metadata.code_base;

        snapshot.code_end_exclusive =
            metadata.code_end_exclusive;

        snapshot.entry_point =
            metadata.entry_point;

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

bool MultiProcessDebugSession::hasSymbols(
    kernel::ProcessId pid
) const {
    requireStarted();
    (void)requireProcess(pid);

    const auto found =
        symbols_.find(pid);

    return found != symbols_.end()
        && !found->second.empty();
}

const DebugSymbols&
MultiProcessDebugSession::symbols(
    kernel::ProcessId pid
) const {
    requireStarted();
    (void)requireProcess(pid);

    const auto found =
        symbols_.find(pid);

    if (found == symbols_.end()) {
        throw std::runtime_error(
            "No debug symbols loaded for PID "
            + std::to_string(pid)
        );
    }

    return found->second;
}

void MultiProcessDebugSession::loadSymbolsFile(
    kernel::ProcessId pid,
    const std::string& path
) {
    requireStarted();
    (void)requireProcess(pid);

    symbols_[pid] =
        DebugSymbols::readFile(path);
}

std::size_t
MultiProcessDebugSession::resolveCodeSymbol(
    kernel::ProcessId pid,
    const std::string& name
) const {
    return symbols(pid).resolveCode(name);
}

std::size_t
MultiProcessDebugSession::resolveDataSymbol(
    kernel::ProcessId pid,
    const std::string& name
) const {
    return symbols(pid).resolveData(name);
}

bool MultiProcessDebugSession::addBreakpoint(
    kernel::ProcessId pid,
    std::size_t address
) {
    requireStarted();
    validateCodeAddress(pid, address);

    return breakpoints_.insert(
        {pid, address}
    ).second;
}

bool MultiProcessDebugSession::removeBreakpoint(
    kernel::ProcessId pid,
    std::size_t address
) {
    requireStarted();

    return breakpoints_.erase(
        {pid, address}
    ) != 0;
}

void MultiProcessDebugSession::clearBreakpoints() {
    requireStarted();
    breakpoints_.clear();
}

void MultiProcessDebugSession::clearBreakpoints(
    kernel::ProcessId pid
) {
    requireStarted();
    (void)requireProcess(pid);

    for (
        auto iterator = breakpoints_.begin();
        iterator != breakpoints_.end();
    ) {
        if (iterator->first == pid) {
            iterator =
                breakpoints_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

std::vector<ProcessBreakpoint>
MultiProcessDebugSession::breakpoints() const {
    requireStarted();

    std::vector<ProcessBreakpoint> result;
    result.reserve(breakpoints_.size());

    for (const auto& entry : breakpoints_) {
        ProcessBreakpoint breakpoint;
        breakpoint.pid = entry.first;
        breakpoint.address = entry.second;

        result.push_back(
            breakpoint
        );
    }

    return result;
}

std::vector<ProcessBreakpoint>
MultiProcessDebugSession::breakpoints(
    kernel::ProcessId pid
) const {
    requireStarted();
    (void)requireProcess(pid);

    std::vector<ProcessBreakpoint> result;

    for (const auto& entry : breakpoints_) {
        if (entry.first != pid) {
            continue;
        }

        ProcessBreakpoint breakpoint;
        breakpoint.pid = entry.first;
        breakpoint.address = entry.second;

        result.push_back(
            breakpoint
        );
    }

    return result;
}

std::size_t
MultiProcessDebugSession::
addConditionalBreakpoint(
    kernel::ProcessId pid,
    std::size_t address,
    const DebugCondition& condition
) {
    requireStarted();
    validateCodeAddress(pid, address);
    condition.validateForCPU(cpu_);

    for (
        const ProcessConditionalBreakpoint&
            existing :
                conditional_breakpoints_
    ) {
        if (
            existing.pid == pid
            && existing.address == address
            && existing.condition.expression
                == condition.expression
        ) {
            return existing.id;
        }
    }

    if (
        next_conditional_breakpoint_id_
        == std::numeric_limits<
            std::size_t
        >::max()
    ) {
        throw std::runtime_error(
            "Conditional breakpoint ID "
            "space exhausted"
        );
    }

    ProcessConditionalBreakpoint breakpoint;

    breakpoint.id =
        next_conditional_breakpoint_id_;

    breakpoint.pid = pid;
    breakpoint.address = address;
    breakpoint.condition = condition;

    ++next_conditional_breakpoint_id_;

    conditional_breakpoints_.push_back(
        breakpoint
    );

    return breakpoint.id;
}

bool MultiProcessDebugSession::
removeConditionalBreakpoint(
    std::size_t id
) {
    requireStarted();

    for (
        auto iterator =
            conditional_breakpoints_.begin();
        iterator
            != conditional_breakpoints_.end();
        ++iterator
    ) {
        if (iterator->id == id) {
            conditional_breakpoints_.erase(
                iterator
            );

            return true;
        }
    }

    return false;
}

void MultiProcessDebugSession::
clearConditionalBreakpoints() {
    requireStarted();
    conditional_breakpoints_.clear();
}

void MultiProcessDebugSession::
clearConditionalBreakpoints(
    kernel::ProcessId pid
) {
    requireStarted();
    (void)requireProcess(pid);

    conditional_breakpoints_.erase(
        std::remove_if(
            conditional_breakpoints_.begin(),
            conditional_breakpoints_.end(),
            [pid](
                const ProcessConditionalBreakpoint&
                    breakpoint
            ) {
                return breakpoint.pid == pid;
            }
        ),
        conditional_breakpoints_.end()
    );
}

std::vector<ProcessConditionalBreakpoint>
MultiProcessDebugSession::
conditionalBreakpoints() const {
    requireStarted();
    return conditional_breakpoints_;
}

std::vector<ProcessConditionalBreakpoint>
MultiProcessDebugSession::
conditionalBreakpoints(
    kernel::ProcessId pid
) const {
    requireStarted();
    (void)requireProcess(pid);

    std::vector<ProcessConditionalBreakpoint>
        result;

    for (
        const ProcessConditionalBreakpoint&
            breakpoint :
                conditional_breakpoints_
    ) {
        if (breakpoint.pid == pid) {
            result.push_back(
                breakpoint
            );
        }
    }

    return result;
}

std::size_t
MultiProcessDebugSession::addWatchpoint(
    kernel::ProcessId pid,
    std::size_t address,
    std::size_t size,
    ProcessMemoryWatchMode mode
) {
    requireStarted();
    validateMemoryRange(
        pid,
        address,
        size
    );

    for (
        const ProcessMemoryWatchpoint& existing :
        watchpoints_
    ) {
        if (
            existing.pid == pid
            && existing.address == address
            && existing.size == size
            && existing.mode == mode
        ) {
            return existing.id;
        }
    }

    if (
        next_watchpoint_id_
        == std::numeric_limits<
            std::size_t
        >::max()
    ) {
        throw std::runtime_error(
            "Watchpoint ID space exhausted"
        );
    }

    ProcessMemoryWatchpoint watchpoint;

    watchpoint.id =
        next_watchpoint_id_;

    watchpoint.pid = pid;
    watchpoint.address = address;
    watchpoint.size = size;
    watchpoint.mode = mode;

    ++next_watchpoint_id_;

    watchpoints_.push_back(
        watchpoint
    );

    return watchpoint.id;
}

bool MultiProcessDebugSession::removeWatchpoint(
    std::size_t id
) {
    requireStarted();

    for (
        auto iterator = watchpoints_.begin();
        iterator != watchpoints_.end();
        ++iterator
    ) {
        if (iterator->id == id) {
            watchpoints_.erase(iterator);
            return true;
        }
    }

    return false;
}

void MultiProcessDebugSession::clearWatchpoints() {
    requireStarted();
    watchpoints_.clear();
}

void MultiProcessDebugSession::clearWatchpoints(
    kernel::ProcessId pid
) {
    requireStarted();
    (void)requireProcess(pid);

    watchpoints_.erase(
        std::remove_if(
            watchpoints_.begin(),
            watchpoints_.end(),
            [pid](
                const ProcessMemoryWatchpoint&
                    watchpoint
            ) {
                return watchpoint.pid == pid;
            }
        ),
        watchpoints_.end()
    );
}

std::vector<ProcessMemoryWatchpoint>
MultiProcessDebugSession::watchpoints() const {
    requireStarted();
    return watchpoints_;
}

std::vector<ProcessMemoryWatchpoint>
MultiProcessDebugSession::watchpoints(
    kernel::ProcessId pid
) const {
    requireStarted();
    (void)requireProcess(pid);

    std::vector<ProcessMemoryWatchpoint>
        result;

    for (
        const ProcessMemoryWatchpoint& watchpoint :
        watchpoints_
    ) {
        if (watchpoint.pid == pid) {
            result.push_back(
                watchpoint
            );
        }
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

    const std::size_t traceSizeBefore =
        cpu_.traceLogger().size();

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

    if (
        cpu_.hasSoftwareInterruptObservation()
    ) {
        ProcessSoftwareInterruptDebugRecord
            record;

        record.lifecycle_step = total_steps_;
        record.pid = before;
        record.observation =
            cpu_.softwareInterruptObservation();

        software_interrupts_.push_back(
            std::move(record)
        );

        cpu_.clearSoftwareInterruptObservation();
    }

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

    refreshResumeSkip();

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
        cpu_.traceLogger().size()
        > traceSizeBefore
    ) {
        WatchpointHit hit;

        if (findWatchpointHit(before, hit)) {
            return makeWatchpointStop(
                before,
                hit,
                1
            );
        }
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
MultiProcessDebugSession::stepSelectedSourceLine(
    std::size_t maxSteps
) {
    requireStarted();

    if (maxSteps == 0) {
        throw std::runtime_error(
            "Multi-process debugger source step limit "
            "must be greater than zero"
        );
    }

    if (
        runtime_state_
        != kernel::ProcessRuntimeState::Running
    ) {
        return makeTerminalStop(0);
    }

    if (selected_pid_ == 0) {
        throw std::runtime_error(
            "No process is selected"
        );
    }

    const ProcessDebugSnapshot initial =
        selectedProcessSnapshot();

    if (initial.terminated()) {
        throw std::runtime_error(
            "Cannot source-step terminated PID "
            + std::to_string(selected_pid_)
        );
    }

    const auto symbolsFound =
        symbols_.find(selected_pid_);

    if (
        symbolsFound == symbols_.end()
        || !symbolsFound->second
            .hasSourceLocations()
    ) {
        throw std::runtime_error(
            "Selected PID "
            + std::to_string(selected_pid_)
            + " has no source map"
        );
    }

    const DebugSymbols& selectedSymbols =
        symbolsFound->second;

    std::size_t sourceLineBefore = 0;

    try {
        sourceLineBefore =
            selectedSymbols.sourceLineForAddress(
                initial.context.pc
            );
    } catch (const std::runtime_error&) {
        throw std::runtime_error(
            "Selected PID "
            + std::to_string(selected_pid_)
            + " current PC has no source mapping"
        );
    }

    const std::size_t sourceStartAddress =
        initial.context.pc;

    // Source stepping is explicit stepping: ignore a
    // breakpoint at the selected PID's starting PC once.
    bool sourceStartSkipActive = true;
    bool sourceLineChanged = false;

    armResumeSkipFromLastStop();

    std::size_t executed = 0;

    while (true) {
        if (
            runtime_state_
            != kernel::ProcessRuntimeState::Running
        ) {
            return makeTerminalStop(executed);
        }

        if (!table_.hasRunningProcess()) {
            return makeTerminalStop(executed);
        }

        refreshResumeSkip();

        const kernel::ProcessId runningPid =
            table_.runningProcessId();

        const std::size_t address =
            cpu_.state().pc();

        const bool skipResumeBreakpoint =
            resume_skip_active_
            && resume_skip_pid_ == runningPid
            && resume_skip_address_ == address;

        const bool skipSourceStartBreakpoint =
            sourceStartSkipActive
            && runningPid == selected_pid_
            && address == sourceStartAddress;

        if (
            !skipResumeBreakpoint
            && !skipSourceStartBreakpoint
        ) {
            if (
                breakpoints_.find(
                    {runningPid, address}
                ) != breakpoints_.end()
            ) {
                return makeBreakpointStop(
                    runningPid,
                    address,
                    executed
                );
            }

            ConditionalBreakpointHit hit;

            if (
                findConditionalBreakpointHit(
                    runningPid,
                    address,
                    hit
                )
            ) {
                return makeConditionalBreakpointStop(
                    hit,
                    executed
                );
            }
        }

        // A newly scheduled debugger stop wins over
        // reporting source-line completion.
        if (sourceLineChanged) {
            return makeStop(
                MultiProcessDebugStopReason::
                    StepComplete,
                executed
            );
        }

        if (executed >= maxSteps) {
            return makeStop(
                MultiProcessDebugStopReason::StepLimit,
                executed,
                "Multi-process debugger source step "
                "limit reached"
            );
        }

        MultiProcessDebugStop stop = step();
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

        const ProcessDebugSnapshot selected =
            selectedProcessSnapshot();

        sourceStartSkipActive =
            !selected.terminated()
            && selected.context.pc
                == sourceStartAddress;

        if (selected.terminated()) {
            continue;
        }

        try {
            const std::size_t sourceLineAfter =
                selectedSymbols.sourceLineForAddress(
                    selected.context.pc
                );

            sourceLineChanged =
                sourceLineAfter
                != sourceLineBefore;
        } catch (const std::runtime_error&) {
            // Keep following the real scheduler through
            // executable addresses absent from a partial map.
            sourceLineChanged = false;
        }
    }
}

// Patch: v1.2-multiprocess-source-step-core-r1

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

    armResumeSkipFromLastStop();

    std::size_t executed = 0;

    while (executed < maxSteps) {
        if (!table_.hasRunningProcess()) {
            return makeTerminalStop(executed);
        }

        refreshResumeSkip();

        const kernel::ProcessId pid =
            table_.runningProcessId();

        const std::size_t address =
            cpu_.state().pc();

        const bool skipCurrent =
            resume_skip_active_
            && resume_skip_pid_ == pid
            && resume_skip_address_
                == address;

        if (!skipCurrent) {
            if (
                breakpoints_.find(
                    {pid, address}
                ) != breakpoints_.end()
            ) {
                return makeBreakpointStop(
                    pid,
                    address,
                    executed
                );
            }

            ConditionalBreakpointHit hit;

            if (
                findConditionalBreakpointHit(
                    pid,
                    address,
                    hit
                )
            ) {
                return
                    makeConditionalBreakpointStop(
                        hit,
                        executed
                    );
            }
        }

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

const std::vector<
    ProcessSoftwareInterruptDebugRecord
>&
MultiProcessDebugSession::softwareInterrupts()
const {
    requireStarted();
    return software_interrupts_;
}

std::vector<ProcessSoftwareInterruptDebugRecord>
MultiProcessDebugSession::softwareInterrupts(
    kernel::ProcessId pid
) const {
    requireStarted();
    (void)requireProcess(pid);

    std::vector<
        ProcessSoftwareInterruptDebugRecord
    > result;

    for (
        const ProcessSoftwareInterruptDebugRecord&
            record :
                software_interrupts_
    ) {
        if (record.pid == pid) {
            result.push_back(record);
        }
    }

    return result;
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

    if (options_.mmio_bus) {
        cpu_.setMMIOBus(
            options_.mmio_bus
        );
    }

    if (options_.software_interrupt_handler) {
        cpu_.setSoftwareInterruptHandler(
            options_.software_interrupt_handler
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

void MultiProcessDebugSession::
loadAutomaticSymbols(
    const std::vector<std::string>& paths
) {
    const std::vector<kernel::ProcessId> pids =
        table_.processIds();

    if (pids.size() != paths.size()) {
        throw std::runtime_error(
            "Process and executable path count mismatch"
        );
    }

    for (
        std::size_t index = 0;
        index < paths.size();
        ++index
    ) {
        const std::string symbolsPath =
            debugSymbolsPathForExecutable(
                paths[index]
            );

        std::ifstream probe(symbolsPath);

        if (!probe.good()) {
            continue;
        }

        probe.close();

        symbols_[pids[index]] =
            DebugSymbols::readFile(
                symbolsPath
            );
    }
}

void MultiProcessDebugSession::requireStarted()
const {
    if (!started_) {
        throw std::runtime_error(
            "Multi-process debugger is not started"
        );
    }
}

const kernel::ProcessControlBlock&
MultiProcessDebugSession::requireProcess(
    kernel::ProcessId pid
) const {
    if (!table_.contains(pid)) {
        throw std::runtime_error(
            "Unknown process PID "
            + std::to_string(pid)
        );
    }

    return table_.process(pid);
}

void MultiProcessDebugSession::
validateCodeAddress(
    kernel::ProcessId pid,
    std::size_t address
) const {
    const kernel::ProcessControlBlock& process =
        requireProcess(pid);

    if (
        !process.addressSpace()
            .hasExecutableImage()
    ) {
        throw std::runtime_error(
            "PID "
            + std::to_string(pid)
            + " has no executable image"
        );
    }

    const kernel::ExecutableMetadata& metadata =
        process.addressSpace()
            .executableMetadata();

    if (
        address < metadata.code_base
        || address
            >= metadata.code_end_exclusive
        || (
            address - metadata.code_base
        ) % binary::kInstructionSize != 0
    ) {
        throw std::runtime_error(
            "Breakpoint address must be inside "
            "the PID code range and "
            "instruction-aligned"
        );
    }
}

void MultiProcessDebugSession::
validateMemoryRange(
    kernel::ProcessId pid,
    std::size_t address,
    std::size_t size
) const {
    const kernel::ProcessControlBlock& process =
        requireProcess(pid);

    if (size == 0) {
        throw std::runtime_error(
            "Watchpoint size must be "
            "greater than zero"
        );
    }

    const std::size_t memorySize =
        process.addressSpace()
            .memory()
            .size();

    if (
        address >= memorySize
        || size > memorySize - address
    ) {
        throw std::runtime_error(
            "Watchpoint range is outside "
            "the PID process memory"
        );
    }
}

bool MultiProcessDebugSession::
findConditionalBreakpointHit(
    kernel::ProcessId pid,
    std::size_t address,
    ConditionalBreakpointHit& hit
) const {
    for (
        const ProcessConditionalBreakpoint&
            breakpoint :
                conditional_breakpoints_
    ) {
        if (
            breakpoint.pid != pid
            || breakpoint.address != address
        ) {
            continue;
        }

        breakpoint.condition.validateForCPU(
            cpu_
        );

        const std::int64_t actual =
            breakpoint.condition.actualValue(
                cpu_
            );

        if (
            breakpoint.condition.evaluate(
                cpu_
            )
        ) {
            hit.breakpoint = breakpoint;
            hit.actual_value = actual;
            return true;
        }
    }

    return false;
}

bool MultiProcessDebugSession::findWatchpointHit(
    kernel::ProcessId pid,
    WatchpointHit& hit
) const {
    if (
        watchpoints_.empty()
        || cpu_.traceLogger().empty()
    ) {
        return false;
    }

    const MemoryTraceDetail& detail =
        cpu_.traceLogger()
            .last()
            .memoryDetail();

    if (
        !detail.active
        || !detail.has_address
        || (
            !detail.is_read
            && !detail.is_write
        )
    ) {
        return false;
    }

    const ProcessMemoryWatchMode accessMode =
        detail.is_write
            ? ProcessMemoryWatchMode::Write
            : ProcessMemoryWatchMode::Read;

    constexpr std::size_t accessSize =
        sizeof(std::int64_t);

    const std::size_t accessBegin =
        detail.address;

    const std::size_t accessEnd =
        accessBegin + accessSize;

    for (
        const ProcessMemoryWatchpoint& watchpoint :
        watchpoints_
    ) {
        if (watchpoint.pid != pid) {
            continue;
        }

        const bool modeMatches =
            watchpoint.mode
                == ProcessMemoryWatchMode::Access
            || watchpoint.mode
                == accessMode;

        const bool rangeMatches =
            watchpoint.address < accessEnd
            && accessBegin
                < watchpoint.endExclusive();

        if (
            modeMatches
            && rangeMatches
        ) {
            hit.watchpoint = watchpoint;
            hit.access_mode = accessMode;
            hit.access_address = accessBegin;
            return true;
        }
    }

    return false;
}

void MultiProcessDebugSession::
armResumeSkipFromLastStop() {
    if (
        last_stop_.reason
            == MultiProcessDebugStopReason::
                Breakpoint
        || last_stop_.reason
            == MultiProcessDebugStopReason::
                ConditionalBreakpoint
    ) {
        resume_skip_active_ = true;
        resume_skip_pid_ =
            last_stop_.hit_pid;

        resume_skip_address_ =
            last_stop_.hit_address;
    }
}

void MultiProcessDebugSession::
refreshResumeSkip() {
    if (!resume_skip_active_) {
        return;
    }

    if (!table_.contains(resume_skip_pid_)) {
        resume_skip_active_ = false;
        return;
    }

    const ProcessDebugSnapshot snapshot =
        processSnapshot(
            resume_skip_pid_
        );

    if (
        snapshot.terminated()
        || snapshot.context.pc
            != resume_skip_address_
    ) {
        resume_skip_active_ = false;
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

    last_stop_.has_debug_hit = false;
    last_stop_.hit_pid = 0;
    last_stop_.hit_address = 0;

    last_stop_.has_conditional_breakpoint =
        false;

    last_stop_.conditional_breakpoint_id = 0;
    last_stop_.conditional_expression.clear();
    last_stop_.conditional_actual_value = 0;

    last_stop_.has_watchpoint = false;
    last_stop_.watchpoint_id = 0;

    last_stop_.watchpoint_mode =
        ProcessMemoryWatchMode::Access;

    last_stop_.watchpoint_address = 0;
    last_stop_.watchpoint_size = 0;

    last_stop_.access_mode =
        ProcessMemoryWatchMode::Access;

    last_stop_.access_address = 0;

    last_stop_.process_terminated = false;
    last_stop_.process_faulted = false;
    last_stop_.terminated_pid = 0;

    last_stop_.message =
        std::move(message);

    return last_stop_;
}

MultiProcessDebugStop
MultiProcessDebugSession::makeBreakpointStop(
    kernel::ProcessId pid,
    std::size_t address,
    std::size_t executedSteps
) {
    MultiProcessDebugStop stop =
        makeStop(
            MultiProcessDebugStopReason::Breakpoint,
            executedSteps,
            "PID "
                + std::to_string(pid)
                + " breakpoint at "
                + std::to_string(address)
        );

    stop.has_debug_hit = true;
    stop.hit_pid = pid;
    stop.hit_address = address;

    last_stop_ = stop;
    return last_stop_;
}

MultiProcessDebugStop
MultiProcessDebugSession::
makeConditionalBreakpointStop(
    const ConditionalBreakpointHit& hit,
    std::size_t executedSteps
) {
    MultiProcessDebugStop stop =
        makeStop(
            MultiProcessDebugStopReason::
                ConditionalBreakpoint,
            executedSteps,
            "PID "
                + std::to_string(
                    hit.breakpoint.pid
                )
                + " conditional breakpoint "
                + std::to_string(
                    hit.breakpoint.id
                )
                + " matched: "
                + hit.breakpoint
                    .condition
                    .expression
        );

    stop.has_debug_hit = true;

    stop.hit_pid =
        hit.breakpoint.pid;

    stop.hit_address =
        hit.breakpoint.address;

    stop.has_conditional_breakpoint = true;

    stop.conditional_breakpoint_id =
        hit.breakpoint.id;

    stop.conditional_expression =
        hit.breakpoint.condition.expression;

    stop.conditional_actual_value =
        hit.actual_value;

    last_stop_ = stop;
    return last_stop_;
}

MultiProcessDebugStop
MultiProcessDebugSession::makeWatchpointStop(
    kernel::ProcessId pid,
    const WatchpointHit& hit,
    std::size_t executedSteps
) {
    MultiProcessDebugStop stop =
        makeStop(
            MultiProcessDebugStopReason::Watchpoint,
            executedSteps,
            "PID "
                + std::to_string(pid)
                + " watchpoint "
                + std::to_string(
                    hit.watchpoint.id
                )
                + " matched "
                + processMemoryWatchModeToString(
                    hit.access_mode
                )
                + " at address "
                + std::to_string(
                    hit.access_address
                )
        );

    stop.has_debug_hit = true;
    stop.hit_pid = pid;

    stop.hit_address =
        hit.access_address;

    stop.has_watchpoint = true;

    stop.watchpoint_id =
        hit.watchpoint.id;

    stop.watchpoint_mode =
        hit.watchpoint.mode;

    stop.watchpoint_address =
        hit.watchpoint.address;

    stop.watchpoint_size =
        hit.watchpoint.size;

    stop.access_mode =
        hit.access_mode;

    stop.access_address =
        hit.access_address;

    last_stop_ = stop;
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

// Patch: v1.4-protected-debug-runtime-r1

} // namespace zero_cpu::debug

// Patch: v1.5-debugger-syscall-observability-r1
