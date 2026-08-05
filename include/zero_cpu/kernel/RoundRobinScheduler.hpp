#pragma once

#include "zero_cpu/kernel/ProcessTable.hpp"

#include <vector>

namespace zero_cpu::kernel {

class RoundRobinScheduler {
public:
    bool hasLastSelection() const;
    ProcessId lastSelectedProcessId() const;

    void reset();

    std::vector<ProcessId> readyQueue(
        const ProcessTable& table
    ) const;

    bool hasRunnableProcess(
        const ProcessTable& table
    ) const;

    ProcessId scheduleNext(
        ProcessTable& table
    );

private:
    bool has_last_selection_ = false;
    ProcessId last_selected_pid_ = 0;

    std::vector<ProcessId> rotateAfterCursor(
        const std::vector<ProcessId>& processIds,
        ProcessId cursor,
        bool hasCursor
    ) const;
};

} // namespace zero_cpu::kernel
