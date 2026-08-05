#pragma once

#include "zero_cpu/kernel/ProcessContext.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"

#include <cstdint>

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

    void replaceContext(
        const ProcessContext& context
    );

    void transitionTo(ProcessState state);
    void terminate(std::int64_t exitCode);

private:
    ProcessContext context_;
    ProcessState state_ = ProcessState::Ready;

    bool has_exit_code_ = false;
    std::int64_t exit_code_ = 0;
};

} // namespace zero_cpu::kernel
