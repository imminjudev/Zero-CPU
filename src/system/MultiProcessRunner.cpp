#include "zero_cpu/system/MultiProcessRunner.hpp"

#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/kernel/ProcessControlBlock.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"
#include "zero_cpu/kernel/ProcessTable.hpp"
#include "zero_cpu/kernel/RoundRobinScheduler.hpp"
#include "zero_cpu/kernel/TimerPreemptiveScheduler.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace zero_cpu::system {
namespace {

void validateOptions(
    const MultiProcessRunOptions& options
) {
    if (options.quantum == 0) {
        throw std::runtime_error(
            "Multi-process quantum must be greater "
            "than zero"
        );
    }

    if (options.max_lifecycle_steps == 0) {
        throw std::runtime_error(
            "Multi-process step limit must be greater "
            "than zero"
        );
    }
}

bool reachedExecutableEnd(
    const CPU& cpu,
    const kernel::ProcessTable& table
) {
    if (
        !table.hasRunningProcess()
        || cpu.state().hasError()
    ) {
        return false;
    }

    const kernel::ProcessControlBlock& process =
        table.process(
            table.runningProcessId()
        );

    if (
        !process.addressSpace()
            .hasExecutableImage()
    ) {
        return false;
    }

    return cpu.state().pc()
        == process.addressSpace()
            .executableMetadata()
            .code_end_exclusive;
}

void discardCompletionTimerRequest(
    kernel::TimerPreemptiveScheduler& preemptive
) {
    constexpr const char* kTimerSource = "timer";

    const auto controller =
        preemptive.interruptController();

    const auto timer =
        preemptive.timerDevice();

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

ProcessRunSummary makeSummary(
    kernel::ProcessId pid,
    const kernel::ProcessControlBlock& process,
    const CPU& cpu,
    bool isRunning
) {
    ProcessRunSummary summary;
    summary.pid = pid;
    summary.state = process.state();

    summary.has_exit_code =
        process.hasExitCode();

    if (summary.has_exit_code) {
        summary.exit_code =
            process.exitCode();

        summary.termination_kind =
            process.terminationKind();

        summary.termination_message =
            process.terminationMessage();
    }

    if (isRunning) {
        summary.final_context =
            kernel::captureProcessContextSnapshot(
                pid,
                cpu
            );

        summary.final_memory =
            cpu.state().memory();
    } else {
        summary.final_context =
            process.context();

        summary.final_memory =
            process.addressSpace().memory();
    }

    summary.has_executable_image =
        process.addressSpace()
            .hasExecutableImage();

    if (summary.has_executable_image) {
        const kernel::ExecutableMetadata& metadata =
            process.addressSpace()
                .executableMetadata();

        summary.source_name =
            metadata.source_name;

        summary.code_end_exclusive =
            metadata.code_end_exclusive;

        summary.data_base =
            metadata.data_base;

        summary.data_size =
            metadata.data_size;
    }

    return summary;
}

} // namespace

ProcessExecutionTraceRecord::
ProcessExecutionTraceRecord(
    std::size_t lifecycleStep,
    kernel::ProcessId processId,
    const TraceEvent& traceEvent
)
    : lifecycle_step(lifecycleStep),
      pid(processId),
      event(traceEvent) {
}

bool ProcessRunSummary::terminated() const {
    return state == kernel::ProcessState::Terminated
        && has_exit_code;
}

bool ProcessRunSummary::faulted() const {
    return terminated()
        && termination_kind
            == kernel::ProcessTerminationKind::CpuFault;
}

bool MultiProcessRunResult::completed() const {
    return !step_limit_reached
        && runtime_state
            == kernel::ProcessRuntimeState::Completed;
}

bool MultiProcessRunResult::success() const {
    return completed()
        && fault_count == 0;
}

const ProcessRunSummary&
MultiProcessRunResult::process(
    kernel::ProcessId pid
) const {
    const auto found = std::find_if(
        processes.begin(),
        processes.end(),
        [pid](
            const ProcessRunSummary& summary
        ) {
            return summary.pid == pid;
        }
    );

    if (found == processes.end()) {
        throw std::runtime_error(
            "Multi-process result does not contain PID "
            + std::to_string(pid)
        );
    }

    return *found;
}

MultiProcessRunResult
MultiProcessRunner::runFiles(
    const std::vector<std::string>& paths,
    const MultiProcessRunOptions& options
) const {
    if (paths.empty()) {
        throw std::runtime_error(
            "runFiles requires at least one executable"
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

    return runImages(images, options);
}

MultiProcessRunResult
MultiProcessRunner::runImages(
    const std::vector<kernel::ProcessImage>& images,
    const MultiProcessRunOptions& options
) const {
    validateOptions(options);

    if (images.empty()) {
        throw std::runtime_error(
            "runImages requires at least one process image"
        );
    }

    kernel::ProcessTable table;

    for (const kernel::ProcessImage& image : images) {
        kernel::validateProcessImage(image);
        (void)table.createProcess(image);
    }

    CPU cpu;

    if (options.mmio_bus) {
        cpu.setMMIOBus(
            options.mmio_bus
        );
    }

    if (options.software_interrupt_handler) {
        cpu.setSoftwareInterruptHandler(
            options.software_interrupt_handler
        );
    }

    kernel::RoundRobinScheduler scheduler;

    kernel::TimerPreemptiveScheduler preemptive(
        options.timer_vector,
        options.quantum,
        options.timer_payload
    );

    kernel::ProcessLifecycleManager lifecycle(
        options.normal_exit_code,
        options.fault_exit_code
    );

    kernel::ProcessRuntimeState runtimeState =
        lifecycle.start(
            cpu,
            table,
            scheduler,
            preemptive
        );

    std::size_t lifecycleSteps = 0;

    std::vector<ProcessExecutionTraceRecord>
        executionTrace;

    std::vector<ProcessContextSwitchTraceRecord>
        contextSwitches;

    while (
        runtimeState
            == kernel::ProcessRuntimeState::Running
        && lifecycleSteps
            < options.max_lifecycle_steps
    ) {
        if (!table.hasRunningProcess()) {
            throw std::runtime_error(
                "Running multi-process runtime has "
                "no Running process"
            );
        }

        const kernel::ProcessId beforePid =
            table.runningProcessId();

        const std::size_t traceSizeBefore =
            cpu.traceLogger().size();

        const std::uint64_t preemptionsBefore =
            preemptive.preemptionCount();

        if (reachedExecutableEnd(cpu, table)) {
            discardCompletionTimerRequest(
                preemptive
            );

            cpu.state().setHalted(true);
        }

        const kernel::ProcessLifecycleStepResult
            stepResult = lifecycle.step(
                cpu,
                table,
                scheduler,
                preemptive
            );

        runtimeState = stepResult.state;
        ++lifecycleSteps;

        if (
            cpu.traceLogger().size()
            > traceSizeBefore
        ) {
            executionTrace.emplace_back(
                lifecycleSteps,
                beforePid,
                cpu.traceLogger().last()
            );
        }

        const kernel::ProcessId afterPid =
            table.hasRunningProcess()
                ? table.runningProcessId()
                : 0;

        if (beforePid != afterPid) {
            ProcessContextSwitchTraceRecord record;
            record.lifecycle_step = lifecycleSteps;
            record.from_pid = beforePid;
            record.to_pid = afterPid;

            record.preempted =
                preemptive.preemptionCount()
                    > preemptionsBefore;

            record.caused_by_termination =
                stepResult.process_terminated;

            contextSwitches.push_back(
                record
            );
        }
    }

    MultiProcessRunResult result;
    result.runtime_state = runtimeState;

    result.step_limit_reached =
        runtimeState
            == kernel::ProcessRuntimeState::Running
        && lifecycleSteps
            >= options.max_lifecycle_steps;

    result.lifecycle_steps = lifecycleSteps;
    result.process_count = table.size();

    result.termination_count =
        lifecycle.terminationCount();

    result.fault_count =
        lifecycle.faultCount();

    result.preemption_count =
        preemptive.preemptionCount();

    result.context_switch_count =
        preemptive.contextSwitchCount();

    result.execution_trace =
        std::move(executionTrace);

    result.context_switches =
        std::move(contextSwitches);

    result.processes.reserve(table.size());

    for (
        const kernel::ProcessId pid :
        table.processIds()
    ) {
        const bool isRunning =
            table.hasRunningProcess()
            && table.runningProcessId() == pid;

        result.processes.push_back(
            makeSummary(
                pid,
                table.process(pid),
                cpu,
                isRunning
            )
        );
    }

    return result;
}

// Patch: v1.4-protected-syscall-hardware-r1

} // namespace zero_cpu::system
