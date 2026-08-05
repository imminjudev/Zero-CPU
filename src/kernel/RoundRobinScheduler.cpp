#include "zero_cpu/kernel/RoundRobinScheduler.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace zero_cpu::kernel {

bool RoundRobinScheduler::hasLastSelection() const {
    return has_last_selection_;
}

ProcessId
RoundRobinScheduler::lastSelectedProcessId() const {
    if (!has_last_selection_) {
        throw std::runtime_error(
            "Round-robin scheduler has no selection"
        );
    }

    return last_selected_pid_;
}

void RoundRobinScheduler::reset() {
    has_last_selection_ = false;
    last_selected_pid_ = 0;
}

std::vector<ProcessId>
RoundRobinScheduler::readyQueue(
    const ProcessTable& table
) const {
    const std::vector<ProcessId> ready =
        table.processIdsInState(
            ProcessState::Ready
        );

    return rotateAfterCursor(
        ready,
        last_selected_pid_,
        has_last_selection_
    );
}

bool RoundRobinScheduler::hasRunnableProcess(
    const ProcessTable& table
) const {
    return table.hasRunningProcess()
        || !table.processIdsInState(
            ProcessState::Ready
        ).empty();
}

ProcessId RoundRobinScheduler::scheduleNext(
    ProcessTable& table
) {
    const bool hadRunningProcess =
        table.hasRunningProcess();

    const ProcessId currentPid =
        hadRunningProcess
            ? table.runningProcessId()
            : 0;

    std::vector<ProcessId> candidates =
        table.processIdsInState(
            ProcessState::Ready
        );

    if (hadRunningProcess) {
        candidates.push_back(currentPid);
    }

    std::sort(
        candidates.begin(),
        candidates.end()
    );

    candidates.erase(
        std::unique(
            candidates.begin(),
            candidates.end()
        ),
        candidates.end()
    );

    if (candidates.empty()) {
        throw std::runtime_error(
            "Round-robin scheduler has no "
            "runnable process"
        );
    }

    ProcessId cursor = last_selected_pid_;
    bool hasCursor = has_last_selection_;

    if (!hasCursor && hadRunningProcess) {
        cursor = currentPid;
        hasCursor = true;
    }

    const std::vector<ProcessId> ordered =
        rotateAfterCursor(
            candidates,
            cursor,
            hasCursor
        );

    const ProcessId selectedPid =
        ordered.front();

    if (hadRunningProcess) {
        if (selectedPid != currentPid) {
            table.transition(
                currentPid,
                ProcessState::Ready
            );

            try {
                table.transition(
                    selectedPid,
                    ProcessState::Running
                );
            } catch (...) {
                table.transition(
                    currentPid,
                    ProcessState::Running
                );

                throw;
            }
        }
    } else {
        table.transition(
            selectedPid,
            ProcessState::Running
        );
    }

    has_last_selection_ = true;
    last_selected_pid_ = selectedPid;

    return selectedPid;
}

std::vector<ProcessId>
RoundRobinScheduler::rotateAfterCursor(
    const std::vector<ProcessId>& processIds,
    ProcessId cursor,
    bool hasCursor
) const {
    if (processIds.empty() || !hasCursor) {
        return processIds;
    }

    const auto firstAfterCursor =
        std::upper_bound(
            processIds.begin(),
            processIds.end(),
            cursor
        );

    std::vector<ProcessId> ordered;
    ordered.reserve(processIds.size());

    ordered.insert(
        ordered.end(),
        firstAfterCursor,
        processIds.end()
    );

    ordered.insert(
        ordered.end(),
        processIds.begin(),
        firstAfterCursor
    );

    return ordered;
}

} // namespace zero_cpu::kernel
