#pragma once

#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/Memory.hpp"
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
#include <string>
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

enum class MultiProcessDebugStopReason {
    Ready,
    StepComplete,
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
    std::size_t code_end_exclusive = 0;
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

    std::vector<ContextSwitchRecord>
        context_switches_;

    MultiProcessDebugStop last_stop_;

    void initialize(
        const std::vector<kernel::ProcessImage>& images
    );

    void requireStarted() const;

    bool reachedExecutableEnd() const;
    void discardCompletionTimerRequest();

    MultiProcessDebugStop makeStop(
        MultiProcessDebugStopReason reason,
        std::size_t executedSteps,
        std::string message = {}
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
