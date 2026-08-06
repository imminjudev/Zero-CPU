#pragma once

#include "zero_cpu/kernel/ProcessContext.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"
#include "zero_cpu/kernel/ProcessTermination.hpp"

#include <cstdint>
#include <string>

namespace zero_cpu::kernel {

class ProcessControlBlock {
public:
    explicit ProcessControlBlock(
        ProcessContext context,
        ProcessState state = ProcessState::Ready
    );

    ProcessId pid() const;

    const ProcessContext& context() const;
    ProcessState state() const;

    bool hasExitCode() const;
    std::int64_t exitCode() const;

    ProcessTerminationKind terminationKind() const;

    const std::string&
    terminationMessage() const;

    void replaceContext(
        const ProcessContext& context
    );

    void replaceFinalContext(
        const ProcessContext& context
    );

    void transitionTo(ProcessState state);

    void terminate(
        std::int64_t exitCode,
        ProcessTerminationKind kind =
            ProcessTerminationKind::NormalExit,
        std::string message = {}
    );

private:
    ProcessContext context_;
    ProcessState state_ = ProcessState::Ready;

    bool has_exit_code_ = false;
    std::int64_t exit_code_ = 0;

    ProcessTerminationKind termination_kind_ =
        ProcessTerminationKind::NormalExit;

    std::string termination_message_;
};

} // namespace zero_cpu::kernel
