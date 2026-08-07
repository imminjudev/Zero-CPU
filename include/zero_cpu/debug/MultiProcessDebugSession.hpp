#pragma once

#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/Memory.hpp"
#include "zero_cpu/debug/DebugCondition.hpp"
#include "zero_cpu/debug/DebugSymbols.hpp"
#include "zero_cpu/kernel/ProcessContext.hpp"
#include "zero_cpu/kernel/ProcessImage.hpp"
#include "zero_cpu/kernel/ProcessLifecycleManager.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"
#include "zero_cpu/kernel/ProcessTable.hpp"
#include "zero_cpu/kernel/ProcessTermination.hpp"
#include "zero_cpu/kernel/RoundRobinScheduler.hpp"
#include "zero_cpu/kernel/TimerPreemptiveScheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace zero_cpu::debug {

struct MultiProcessDebugOptions {
    std::uint8_t timer_vector = 240;
    std::uint64_t quantum = 2;
    std::int64_t timer_payload = 0;

    std::size_t default_continue_steps = 100000;

    std::int64_t normal_exit_code = 0;
    std::int64_t fault_exit_code = -1;
};

enum class ProcessMemoryWatchMode {
    Read,
    Write,
    Access
};

const char* processMemoryWatchModeToString(
    ProcessMemoryWatchMode mode
);

struct ProcessBreakpoint {
    kernel::ProcessId pid = 0;
    std::size_t address = 0;
};

struct ProcessConditionalBreakpoint {
    std::size_t id = 0;
    kernel::ProcessId pid = 0;
    std::size_t address = 0;
    DebugCondition condition;
};

struct ProcessMemoryWatchpoint {
    std::size_t id = 0;
    kernel::ProcessId pid = 0;
    std::size_t address = 0;
    std::size_t size = 1;

    ProcessMemoryWatchMode mode =
        ProcessMemoryWatchMode::Access;

    std::size_t endExclusive() const;
};

enum class MultiProcessDebugStopReason {
    Ready,
    StepComplete,
    Breakpoint,
    ConditionalBreakpoint,
    Watchpoint,
    ProcessTerminated,
    ProcessFaulted,
    RuntimeCompleted,
    Deadlocked,
    StepLimit
};

const char* multiProcessDebugStopReasonToString(
    MultiProcessDebugStopReason reason
);

struct MultiProcessDebugStop {
    MultiProcessDebugStopReason reason =
        MultiProcessDebugStopReason::Ready;

    kernel::ProcessRuntimeState runtime_state =
        kernel::ProcessRuntimeState::Ready;

    std::size_t executed_steps = 0;
    std::size_t total_steps = 0;

    kernel::ProcessId running_pid = 0;
    kernel::ProcessId selected_pid = 0;

    bool has_debug_hit = false;
    kernel::ProcessId hit_pid = 0;
    std::size_t hit_address = 0;

    bool has_conditional_breakpoint = false;
    std::size_t conditional_breakpoint_id = 0;
    std::string conditional_expression;
    std::int64_t conditional_actual_value = 0;

    bool has_watchpoint = false;
    std::size_t watchpoint_id = 0;

    ProcessMemoryWatchMode watchpoint_mode =
        ProcessMemoryWatchMode::Access;

    std::size_t watchpoint_address = 0;
    std::size_t watchpoint_size = 0;

    ProcessMemoryWatchMode access_mode =
        ProcessMemoryWatchMode::Access;

    std::size_t access_address = 0;

    bool process_terminated = false;
    bool process_faulted = false;

    kernel::ProcessId terminated_pid = 0;

    std::string message;
};

struct ProcessDebugSnapshot {
    kernel::ProcessId pid = 0;

    std::string source_name;

    kernel::ProcessState state =
        kernel::ProcessState::Ready;

    kernel::ProcessContext context;
    Memory memory;

    bool has_exit_code = false;
    std::int64_t exit_code = 0;

    kernel::ProcessTerminationKind
        termination_kind =
            kernel::ProcessTerminationKind::NormalExit;

    std::string termination_message;

    bool has_executable_image = false;

    std::size_t code_base = 0;
    std::size_t code_end_exclusive = 0;
    std::size_t entry_point = 0;

    std::size_t data_base = 0;
    std::size_t data_size = 0;

    bool running = false;

    bool terminated() const;
    bool faulted() const;
};

struct ContextSwitchRecord {
    std::size_t lifecycle_step = 0;

    kernel::ProcessId from_pid = 0;
    kernel::ProcessId to_pid = 0;

    bool preempted = false;
    bool caused_by_termination = false;
};

class MultiProcessDebugSession {
public:
    MultiProcessDebugSession(
        const std::vector<std::string>& paths,
        const MultiProcessDebugOptions& options = {}
    );

    MultiProcessDebugSession(
        const std::vector<kernel::ProcessImage>& images,
        const MultiProcessDebugOptions& options = {}
    );

    bool started() const;

    kernel::ProcessRuntimeState
    runtimeState() const;

    std::size_t totalSteps() const;

    kernel::ProcessId runningPid() const;
    kernel::ProcessId selectedPid() const;

    void selectProcess(
        kernel::ProcessId pid
    );

    ProcessDebugSnapshot processSnapshot(
        kernel::ProcessId pid
    ) const;

    ProcessDebugSnapshot
    selectedProcessSnapshot() const;

    std::vector<ProcessDebugSnapshot>
    processSnapshots() const;

    bool hasSymbols(
        kernel::ProcessId pid
    ) const;

    const DebugSymbols& symbols(
        kernel::ProcessId pid
    ) const;

    void loadSymbolsFile(
        kernel::ProcessId pid,
        const std::string& path
    );

    std::size_t resolveCodeSymbol(
        kernel::ProcessId pid,
        const std::string& name
    ) const;

    std::size_t resolveDataSymbol(
        kernel::ProcessId pid,
        const std::string& name
    ) const;

    bool addBreakpoint(
        kernel::ProcessId pid,
        std::size_t address
    );

    bool removeBreakpoint(
        kernel::ProcessId pid,
        std::size_t address
    );

    void clearBreakpoints();
    void clearBreakpoints(
        kernel::ProcessId pid
    );

    std::vector<ProcessBreakpoint>
    breakpoints() const;

    std::vector<ProcessBreakpoint>
    breakpoints(
        kernel::ProcessId pid
    ) const;

    std::size_t addConditionalBreakpoint(
        kernel::ProcessId pid,
        std::size_t address,
        const DebugCondition& condition
    );

    bool removeConditionalBreakpoint(
        std::size_t id
    );

    void clearConditionalBreakpoints();

    void clearConditionalBreakpoints(
        kernel::ProcessId pid
    );

    std::vector<ProcessConditionalBreakpoint>
    conditionalBreakpoints() const;

    std::vector<ProcessConditionalBreakpoint>
    conditionalBreakpoints(
        kernel::ProcessId pid
    ) const;

    std::size_t addWatchpoint(
        kernel::ProcessId pid,
        std::size_t address,
        std::size_t size,
        ProcessMemoryWatchMode mode
    );

    bool removeWatchpoint(
        std::size_t id
    );

    void clearWatchpoints();

    void clearWatchpoints(
        kernel::ProcessId pid
    );

    std::vector<ProcessMemoryWatchpoint>
    watchpoints() const;

    std::vector<ProcessMemoryWatchpoint>
    watchpoints(
        kernel::ProcessId pid
    ) const;

    const MultiProcessDebugStop&
    lastStop() const;

    MultiProcessDebugStop step();

    MultiProcessDebugStop continueExecution(
        std::size_t maxSteps
    );

    std::uint64_t preemptionCount() const;
    std::uint64_t schedulerContextSwitchCount() const;

    const std::vector<ContextSwitchRecord>&
    contextSwitches() const;

    const CPU& cpu() const;

    std::uint64_t quantum() const;

private:
    MultiProcessDebugOptions options_;

    CPU cpu_;
    kernel::ProcessTable table_;
    kernel::RoundRobinScheduler scheduler_;

    kernel::TimerPreemptiveScheduler
        preemptive_;

    kernel::ProcessLifecycleManager
        lifecycle_;

    bool started_ = false;

    kernel::ProcessRuntimeState runtime_state_ =
        kernel::ProcessRuntimeState::Ready;

    std::size_t total_steps_ = 0;

    kernel::ProcessId selected_pid_ = 0;

    std::map<
        kernel::ProcessId,
        DebugSymbols
    > symbols_;

    std::set<
        std::pair<
            kernel::ProcessId,
            std::size_t
        >
    > breakpoints_;

    std::vector<ProcessConditionalBreakpoint>
        conditional_breakpoints_;

    std::size_t next_conditional_breakpoint_id_ = 1;

    std::vector<ProcessMemoryWatchpoint>
        watchpoints_;

    std::size_t next_watchpoint_id_ = 1;

    bool resume_skip_active_ = false;
    kernel::ProcessId resume_skip_pid_ = 0;
    std::size_t resume_skip_address_ = 0;

    std::vector<ContextSwitchRecord>
        context_switches_;

    MultiProcessDebugStop last_stop_;

    struct ConditionalBreakpointHit {
        ProcessConditionalBreakpoint breakpoint;
        std::int64_t actual_value = 0;
    };

    struct WatchpointHit {
        ProcessMemoryWatchpoint watchpoint;

        ProcessMemoryWatchMode access_mode =
            ProcessMemoryWatchMode::Access;

        std::size_t access_address = 0;
    };

    void initialize(
        const std::vector<kernel::ProcessImage>& images
    );

    void loadAutomaticSymbols(
        const std::vector<std::string>& paths
    );

    void requireStarted() const;

    const kernel::ProcessControlBlock&
    requireProcess(
        kernel::ProcessId pid
    ) const;

    void validateCodeAddress(
        kernel::ProcessId pid,
        std::size_t address
    ) const;

    void validateMemoryRange(
        kernel::ProcessId pid,
        std::size_t address,
        std::size_t size
    ) const;

    bool findConditionalBreakpointHit(
        kernel::ProcessId pid,
        std::size_t address,
        ConditionalBreakpointHit& hit
    ) const;

    bool findWatchpointHit(
        kernel::ProcessId pid,
        WatchpointHit& hit
    ) const;

    void armResumeSkipFromLastStop();
    void refreshResumeSkip();

    bool reachedExecutableEnd() const;
    void discardCompletionTimerRequest();

    MultiProcessDebugStop makeStop(
        MultiProcessDebugStopReason reason,
        std::size_t executedSteps,
        std::string message = {}
    );

    MultiProcessDebugStop makeBreakpointStop(
        kernel::ProcessId pid,
        std::size_t address,
        std::size_t executedSteps
    );

    MultiProcessDebugStop
    makeConditionalBreakpointStop(
        const ConditionalBreakpointHit& hit,
        std::size_t executedSteps
    );

    MultiProcessDebugStop makeWatchpointStop(
        kernel::ProcessId pid,
        const WatchpointHit& hit,
        std::size_t executedSteps
    );

    MultiProcessDebugStop makeTerminalStop(
        std::size_t executedSteps
    );

    void recordContextSwitch(
        kernel::ProcessId before,
        kernel::ProcessId after,
        bool preempted,
        bool causedByTermination
    );
};

} // namespace zero_cpu::debug
