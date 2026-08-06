#include "zero_cpu/kernel/ProcessControlBlock.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace zero_cpu::kernel {

ProcessControlBlock::ProcessControlBlock(
    ProcessContext context,
    ProcessState state
)
    : ProcessControlBlock(
          std::move(context),
          ProcessAddressSpace(),
          state
      ) {
}

ProcessControlBlock::ProcessControlBlock(
    ProcessContext context,
    ProcessAddressSpace addressSpace,
    ProcessState state
)
    : context_(std::move(context)),
      address_space_(std::move(addressSpace)),
      state_(state) {
    validateProcessContext(context_);

    if (state_ == ProcessState::Terminated) {
        throw std::runtime_error(
            "ProcessControlBlock cannot start Terminated"
        );
    }
}

ProcessId ProcessControlBlock::pid() const {
    return context_.pid;
}

const ProcessContext&
ProcessControlBlock::context() const {
    return context_;
}

const ProcessAddressSpace&
ProcessControlBlock::addressSpace() const {
    return address_space_;
}

ProcessState ProcessControlBlock::state() const {
    return state_;
}

bool ProcessControlBlock::hasExitCode() const {
    return has_exit_code_;
}

std::int64_t ProcessControlBlock::exitCode() const {
    if (!has_exit_code_) {
        throw std::runtime_error(
            "Process has no exit code"
        );
    }

    return exit_code_;
}

ProcessTerminationKind
ProcessControlBlock::terminationKind() const {
    if (!has_exit_code_) {
        throw std::runtime_error(
            "Process has no termination metadata"
        );
    }

    return termination_kind_;
}

const std::string&
ProcessControlBlock::terminationMessage() const {
    if (!has_exit_code_) {
        throw std::runtime_error(
            "Process has no termination metadata"
        );
    }

    return termination_message_;
}

void ProcessControlBlock::replaceContext(
    const ProcessContext& context
) {
    if (state_ == ProcessState::Terminated) {
        throw std::runtime_error(
            "Cannot update a terminated process context"
        );
    }

    validateProcessContext(context);

    if (context.pid != context_.pid) {
        throw std::runtime_error(
            "Process context PID mismatch"
        );
    }

    context_ = context;
}

void ProcessControlBlock::replaceRuntimeState(
    const ProcessContext& context,
    const Memory& memory
) {
    if (state_ == ProcessState::Terminated) {
        throw std::runtime_error(
            "Cannot update a terminated process state"
        );
    }

    validateProcessContext(context);

    if (context.pid != context_.pid) {
        throw std::runtime_error(
            "Process runtime state PID mismatch"
        );
    }

    ProcessAddressSpace stagedAddressSpace =
        address_space_;

    stagedAddressSpace.replaceMemory(memory);

    context_ = context;
    address_space_ =
        std::move(stagedAddressSpace);
}

void ProcessControlBlock::replaceFinalContext(
    const ProcessContext& context
) {
    if (state_ == ProcessState::Terminated) {
        throw std::runtime_error(
            "Cannot update a terminated process context"
        );
    }

    validateProcessContextSnapshot(context);

    if (context.pid != context_.pid) {
        throw std::runtime_error(
            "Process final context PID mismatch"
        );
    }

    context_ = context;
}

void ProcessControlBlock::replaceFinalState(
    const ProcessContext& context,
    const Memory& memory
) {
    if (state_ == ProcessState::Terminated) {
        throw std::runtime_error(
            "Cannot update a terminated process state"
        );
    }

    validateProcessContextSnapshot(context);

    if (context.pid != context_.pid) {
        throw std::runtime_error(
            "Process final state PID mismatch"
        );
    }

    ProcessAddressSpace stagedAddressSpace =
        address_space_;

    stagedAddressSpace.replaceMemory(memory);

    context_ = context;
    address_space_ =
        std::move(stagedAddressSpace);
}

void ProcessControlBlock::transitionTo(
    ProcessState state
) {
    if (state == ProcessState::Terminated) {
        throw std::runtime_error(
            "Use terminate() to terminate a process"
        );
    }

    if (!canTransitionProcessState(state_, state)) {
        throw std::runtime_error(
            "Invalid process state transition: "
            + std::string(processStateToString(state_))
            + " -> "
            + processStateToString(state)
        );
    }

    state_ = state;
}

void ProcessControlBlock::terminate(
    std::int64_t exitCode,
    ProcessTerminationKind kind,
    std::string message
) {
    if (state_ == ProcessState::Terminated) {
        throw std::runtime_error(
            "Process is already terminated"
        );
    }

    if (
        !canTransitionProcessState(
            state_,
            ProcessState::Terminated
        )
    ) {
        throw std::runtime_error(
            "Process cannot transition to Terminated"
        );
    }

    state_ = ProcessState::Terminated;
    has_exit_code_ = true;
    exit_code_ = exitCode;
    termination_kind_ = kind;
    termination_message_ = std::move(message);
}

} // namespace zero_cpu::kernel
