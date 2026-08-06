#include "zero_cpu/kernel/ProcessTable.hpp"

#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/kernel/ProcessAddressSpace.hpp"
#include "zero_cpu/kernel/ProcessContext.hpp"
#include "zero_cpu/kernel/ProcessImage.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace zero_cpu::kernel {
namespace {

ProcessContext contextFromImage(
    ProcessId pid,
    const ProcessImage& image
) {
    validateProcessImage(image);

    ProcessContext context;
    context.pid = pid;
    context.registers =
        image.initial_registers;
    context.flags =
        image.initial_flags;
    context.pc =
        image.initial_pc;
    context.sp =
        image.initial_sp;
    context.privilege =
        image.initial_privilege;

    context.has_user_code_range = true;
    context.user_code_begin =
        image.metadata.code_base;
    context.user_code_end_exclusive =
        image.metadata.code_end_exclusive;

    context.user_stack_begin =
        image.metadata.user_stack_begin;
    context.user_stack_end_exclusive =
        image.metadata.user_stack_end_exclusive;

    context.kernel_stack_pointer =
        image.initial_kernel_stack_pointer;

    validateProcessContext(context);
    return context;
}

} // namespace

ProcessId ProcessTable::createProcess(
    ProcessContext context
) {
    const ProcessId pid =
        requireNextProcessId();

    context.pid = pid;
    validateProcessContext(context);

    ProcessControlBlock process(
        std::move(context),
        ProcessState::Ready
    );

    const auto inserted = processes_.emplace(
        pid,
        std::move(process)
    );

    if (!inserted.second) {
        throw std::runtime_error(
            "Process table PID collision"
        );
    }

    commitAllocatedProcessId(pid);
    return pid;
}

ProcessId ProcessTable::createProcess(
    const CPU& cpu
) {
    const ProcessId pid =
        requireNextProcessId();

    ProcessContext context =
        captureProcessContext(pid, cpu);

    ProcessAddressSpace addressSpace(
        cpu.state().memory()
    );

    ProcessControlBlock process(
        std::move(context),
        std::move(addressSpace),
        ProcessState::Ready
    );

    const auto inserted = processes_.emplace(
        pid,
        std::move(process)
    );

    if (!inserted.second) {
        throw std::runtime_error(
            "Process table PID collision"
        );
    }

    commitAllocatedProcessId(pid);
    return pid;
}

ProcessId ProcessTable::createProcess(
    const ProcessImage& image
) {
    const ProcessId pid =
        requireNextProcessId();

    ProcessContext context =
        contextFromImage(pid, image);

    ProcessAddressSpace addressSpace(image);

    ProcessControlBlock process(
        std::move(context),
        std::move(addressSpace),
        ProcessState::Ready
    );

    const auto inserted = processes_.emplace(
        pid,
        std::move(process)
    );

    if (!inserted.second) {
        throw std::runtime_error(
            "Process table PID collision"
        );
    }

    commitAllocatedProcessId(pid);
    return pid;
}

bool ProcessTable::empty() const {
    return processes_.empty();
}

std::size_t ProcessTable::size() const {
    return processes_.size();
}

bool ProcessTable::contains(
    ProcessId pid
) const {
    return processes_.find(pid)
        != processes_.end();
}

const ProcessControlBlock&
ProcessTable::process(ProcessId pid) const {
    return requireProcess(pid);
}

std::vector<ProcessId>
ProcessTable::processIds() const {
    std::vector<ProcessId> result;
    result.reserve(processes_.size());

    for (const auto& entry : processes_) {
        result.push_back(entry.first);
    }

    return result;
}

std::vector<ProcessId>
ProcessTable::processIdsInState(
    ProcessState state
) const {
    std::vector<ProcessId> result;

    for (const auto& entry : processes_) {
        if (entry.second.state() == state) {
            result.push_back(entry.first);
        }
    }

    return result;
}

void ProcessTable::updateContext(
    ProcessId pid,
    const ProcessContext& context
) {
    ProcessControlBlock& process =
        requireProcess(pid);

    process.replaceContext(context);
}

void ProcessTable::updateRuntimeState(
    ProcessId pid,
    const ProcessContext& context,
    const Memory& memory
) {
    ProcessControlBlock& process =
        requireProcess(pid);

    process.replaceRuntimeState(
        context,
        memory
    );
}

void ProcessTable::transition(
    ProcessId pid,
    ProcessState state
) {
    if (state == ProcessState::Terminated) {
        throw std::runtime_error(
            "Use terminate() to terminate a process"
        );
    }

    ProcessControlBlock& process =
        requireProcess(pid);

    const ProcessState previousState =
        process.state();

    if (
        state == ProcessState::Running
        && running_pid_ != 0
        && running_pid_ != pid
    ) {
        throw std::runtime_error(
            "Another process is already Running"
        );
    }

    process.transitionTo(state);

    if (
        previousState == ProcessState::Running
        && state != ProcessState::Running
    ) {
        running_pid_ = 0;
    }

    if (state == ProcessState::Running) {
        running_pid_ = pid;
    }
}

void ProcessTable::terminate(
    ProcessId pid,
    std::int64_t exitCode
) {
    ProcessControlBlock& process =
        requireProcess(pid);

    const bool wasRunning =
        process.state() == ProcessState::Running;

    process.terminate(exitCode);

    if (wasRunning) {
        running_pid_ = 0;
    }
}

void ProcessTable::terminate(
    ProcessId pid,
    const ProcessContext& finalContext,
    std::int64_t exitCode
) {
    ProcessControlBlock& process =
        requireProcess(pid);

    const bool wasRunning =
        process.state() == ProcessState::Running;

    ProcessControlBlock staged = process;

    staged.replaceFinalContext(finalContext);

    staged.terminate(
        exitCode,
        ProcessTerminationKind::NormalExit
    );

    process = std::move(staged);

    if (wasRunning) {
        running_pid_ = 0;
    }
}

void ProcessTable::terminate(
    ProcessId pid,
    const ProcessContext& finalContext,
    const Memory& finalMemory,
    std::int64_t exitCode
) {
    ProcessControlBlock& process =
        requireProcess(pid);

    const bool wasRunning =
        process.state() == ProcessState::Running;

    ProcessControlBlock staged = process;

    staged.replaceFinalState(
        finalContext,
        finalMemory
    );

    staged.terminate(
        exitCode,
        ProcessTerminationKind::NormalExit
    );

    process = std::move(staged);

    if (wasRunning) {
        running_pid_ = 0;
    }
}

void ProcessTable::fault(
    ProcessId pid,
    const ProcessContext& finalContext,
    std::int64_t exitCode,
    const std::string& message
) {
    ProcessControlBlock& process =
        requireProcess(pid);

    const bool wasRunning =
        process.state() == ProcessState::Running;

    ProcessControlBlock staged = process;

    staged.replaceFinalContext(finalContext);

    staged.terminate(
        exitCode,
        ProcessTerminationKind::CpuFault,
        message
    );

    process = std::move(staged);

    if (wasRunning) {
        running_pid_ = 0;
    }
}

void ProcessTable::fault(
    ProcessId pid,
    const ProcessContext& finalContext,
    const Memory& finalMemory,
    std::int64_t exitCode,
    const std::string& message
) {
    ProcessControlBlock& process =
        requireProcess(pid);

    const bool wasRunning =
        process.state() == ProcessState::Running;

    ProcessControlBlock staged = process;

    staged.replaceFinalState(
        finalContext,
        finalMemory
    );

    staged.terminate(
        exitCode,
        ProcessTerminationKind::CpuFault,
        message
    );

    process = std::move(staged);

    if (wasRunning) {
        running_pid_ = 0;
    }
}

bool ProcessTable::hasRunningProcess() const {
    return running_pid_ != 0;
}

ProcessId ProcessTable::runningProcessId() const {
    if (!hasRunningProcess()) {
        throw std::runtime_error(
            "Process table has no Running process"
        );
    }

    return running_pid_;
}

ProcessId
ProcessTable::requireNextProcessId() const {
    if (next_pid_ == 0) {
        throw std::runtime_error(
            "Process ID space exhausted"
        );
    }

    return next_pid_;
}

void ProcessTable::commitAllocatedProcessId(
    ProcessId pid
) {
    if (
        pid
        == std::numeric_limits<ProcessId>::max()
    ) {
        next_pid_ = 0;
        return;
    }

    next_pid_ = pid + 1;
}

ProcessControlBlock&
ProcessTable::requireProcess(ProcessId pid) {
    const auto found = processes_.find(pid);

    if (found == processes_.end()) {
        throw std::runtime_error(
            "Unknown process PID: "
            + std::to_string(pid)
        );
    }

    return found->second;
}

const ProcessControlBlock&
ProcessTable::requireProcess(
    ProcessId pid
) const {
    const auto found = processes_.find(pid);

    if (found == processes_.end()) {
        throw std::runtime_error(
            "Unknown process PID: "
            + std::to_string(pid)
        );
    }

    return found->second;
}

} // namespace zero_cpu::kernel
