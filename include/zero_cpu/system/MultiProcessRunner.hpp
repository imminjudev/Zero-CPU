#pragma once

#include "zero_cpu/core/Memory.hpp"
#include "zero_cpu/kernel/ProcessContext.hpp"
#include "zero_cpu/kernel/ProcessImage.hpp"
#include "zero_cpu/kernel/ProcessLifecycleManager.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"
#include "zero_cpu/kernel/ProcessTermination.hpp"
#include "zero_cpu/trace/TraceEvent.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zero_cpu::system {

struct MultiProcessRunOptions {
    std::uint8_t timer_vector = 240;
    std::uint64_t quantum = 2;
    std::int64_t timer_payload = 0;

    std::size_t max_lifecycle_steps = 100000;

    std::int64_t normal_exit_code = 0;
    std::int64_t fault_exit_code = -1;
};

struct ProcessExecutionTraceRecord {
    ProcessExecutionTraceRecord(
        std::size_t lifecycleStep,
        kernel::ProcessId pid,
        const TraceEvent& event
    );

    std::size_t lifecycle_step = 0;
    kernel::ProcessId pid = 0;
    TraceEvent event;
};

struct ProcessContextSwitchTraceRecord {
    std::size_t lifecycle_step = 0;

    kernel::ProcessId from_pid = 0;
    kernel::ProcessId to_pid = 0;

    bool preempted = false;
    bool caused_by_termination = false;
};

struct ProcessRunSummary {
    kernel::ProcessId pid = 0;
    std::string source_name;

    kernel::ProcessState state =
        kernel::ProcessState::Ready;

    bool has_exit_code = false;
    std::int64_t exit_code = 0;

    kernel::ProcessTerminationKind
        termination_kind =
            kernel::ProcessTerminationKind::NormalExit;

    std::string termination_message;

    kernel::ProcessContext final_context;
    Memory final_memory;

    bool has_executable_image = false;

    std::size_t code_end_exclusive = 0;
    std::size_t data_base = 0;
    std::size_t data_size = 0;

    bool terminated() const;
    bool faulted() const;
};

struct MultiProcessRunResult {
    kernel::ProcessRuntimeState runtime_state =
        kernel::ProcessRuntimeState::Ready;

    bool step_limit_reached = false;

    std::size_t lifecycle_steps = 0;
    std::size_t process_count = 0;
    std::size_t termination_count = 0;
    std::size_t fault_count = 0;

    std::uint64_t preemption_count = 0;
    std::uint64_t context_switch_count = 0;

    std::vector<ProcessExecutionTraceRecord>
        execution_trace;

    std::vector<ProcessContextSwitchTraceRecord>
        context_switches;

    std::vector<ProcessRunSummary> processes;

    bool completed() const;
    bool success() const;

    const ProcessRunSummary& process(
        kernel::ProcessId pid
    ) const;
};

class MultiProcessRunner {
public:
    MultiProcessRunResult runFiles(
        const std::vector<std::string>& paths,
        const MultiProcessRunOptions& options = {}
    ) const;

    MultiProcessRunResult runImages(
        const std::vector<kernel::ProcessImage>& images,
        const MultiProcessRunOptions& options = {}
    ) const;
};

} // namespace zero_cpu::system
