#pragma once

#include "zero_cpu/kernel/ProcessDispatcher.hpp"
#include "zero_cpu/kernel/ProcessTable.hpp"
#include "zero_cpu/kernel/RoundRobinScheduler.hpp"
#include "zero_cpu/kernel/TimerPreemptiveScheduler.hpp"

#include <cstddef>
#include <cstdint>

namespace zero_cpu {

class CPU;

} // namespace zero_cpu

namespace zero_cpu::kernel {

enum class ProcessRuntimeState : std::uint8_t {
    Ready = 0,
    Running = 1,
    Completed = 2,
    Deadlocked = 3
};

const char* processRuntimeStateToString(
    ProcessRuntimeState state
);

struct ProcessLifecycleStepResult {
    ProcessRuntimeState state =
        ProcessRuntimeState::Ready;

    bool process_terminated = false;
    bool process_faulted = false;

    ProcessId terminated_pid = 0;
    ProcessId running_pid = 0;
};

class ProcessLifecycleManager {
public:
    explicit ProcessLifecycleManager(
        std::int64_t normalExitCode = 0,
        std::int64_t faultExitCode = -1
    );

    bool started() const;
    ProcessRuntimeState state() const;

    std::size_t terminationCount() const;
    std::size_t faultCount() const;

    ProcessRuntimeState start(
        CPU& cpu,
        ProcessTable& table,
        RoundRobinScheduler& scheduler,
        TimerPreemptiveScheduler& preemptive
    );

    ProcessLifecycleStepResult step(
        CPU& cpu,
        ProcessTable& table,
        RoundRobinScheduler& scheduler,
        TimerPreemptiveScheduler& preemptive
    );

private:
    ProcessDispatcher dispatcher_;

    std::int64_t normal_exit_code_ = 0;
    std::int64_t fault_exit_code_ = -1;

    bool started_ = false;

    ProcessRuntimeState state_ =
        ProcessRuntimeState::Ready;

    std::size_t termination_count_ = 0;
    std::size_t fault_count_ = 0;

    ProcessRuntimeState classifyWithoutRunning(
        const ProcessTable& table
    ) const;
};

} // namespace zero_cpu::kernel
