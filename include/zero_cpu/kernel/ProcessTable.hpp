#pragma once

#include "zero_cpu/kernel/ProcessControlBlock.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace zero_cpu {

class CPU;

} // namespace zero_cpu

namespace zero_cpu::kernel {

class ProcessTable {
public:
    ProcessId createProcess(
        ProcessContext context
    );

    ProcessId createProcess(
        const CPU& cpu
    );

    bool empty() const;
    std::size_t size() const;
    bool contains(ProcessId pid) const;

    const ProcessControlBlock& process(
        ProcessId pid
    ) const;

    std::vector<ProcessId> processIds() const;

    std::vector<ProcessId> processIdsInState(
        ProcessState state
    ) const;

    void updateContext(
        ProcessId pid,
        const ProcessContext& context
    );

    void transition(
        ProcessId pid,
        ProcessState state
    );

    void terminate(
        ProcessId pid,
        std::int64_t exitCode
    );

    bool hasRunningProcess() const;
    ProcessId runningProcessId() const;

private:
    std::map<
        ProcessId,
        ProcessControlBlock
    > processes_;

    ProcessId next_pid_ = 1;
    ProcessId running_pid_ = 0;

    ProcessId requireNextProcessId() const;
    void commitAllocatedProcessId(
        ProcessId pid
    );

    ProcessControlBlock& requireProcess(
        ProcessId pid
    );

    const ProcessControlBlock& requireProcess(
        ProcessId pid
    ) const;
};

} // namespace zero_cpu::kernel
