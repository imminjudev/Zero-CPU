#include "zero_cpu/system/MultiProcessInvariantVerifier.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>

namespace zero_cpu::system {
namespace {

void addViolation(
    MultiProcessInvariantReport& report,
    std::string code,
    std::string message,
    std::size_t lifecycleStep = 0,
    kernel::ProcessId pid = 0
) {
    MultiProcessInvariantViolation violation;
    violation.code = std::move(code);
    violation.message = std::move(message);
    violation.lifecycle_step = lifecycleStep;
    violation.pid = pid;
    report.violations.push_back(std::move(violation));
}

} // namespace

bool MultiProcessInvariantReport::passed() const {
    return violations.empty();
}

std::size_t MultiProcessInvariantReport::violationCount() const {
    return violations.size();
}

MultiProcessInvariantReport
MultiProcessInvariantVerifier::verify(
    const MultiProcessRunResult& result
) {
    MultiProcessInvariantReport report;

    if (result.process_count != result.processes.size()) {
        addViolation(
            report,
            "process_count_mismatch",
            "process_count does not match process summaries"
        );
    }

    std::set<kernel::ProcessId> processIds;
    std::size_t terminatedCount = 0;
    std::size_t faultedCount = 0;

    for (const ProcessRunSummary& process : result.processes) {
        if (
            process.pid == 0
            || !processIds.insert(process.pid).second
        ) {
            addViolation(
                report,
                "invalid_process_id",
                "process summaries contain a zero or duplicate PID",
                0,
                process.pid
            );
        }

        if (process.terminated()) {
            ++terminatedCount;
        }

        if (process.faulted()) {
            ++faultedCount;
        }
    }

    if (result.termination_count != terminatedCount) {
        addViolation(
            report,
            "termination_count_mismatch",
            "termination_count does not match terminal processes"
        );
    }

    if (result.fault_count != faultedCount) {
        addViolation(
            report,
            "fault_count_mismatch",
            "fault_count does not match faulted processes"
        );
    }

    if (
        result.fault_count > result.termination_count
        || result.termination_count > result.process_count
    ) {
        addViolation(
            report,
            "invalid_termination_counts",
            "fault/termination counts violate process-count bounds"
        );
    }

    if (result.context_switch_count > result.preemption_count) {
        addViolation(
            report,
            "invalid_preemption_counts",
            "context-switch count exceeds preemption count"
        );
    }

    if (
        result.step_limit_reached
        && result.runtime_state
            != kernel::ProcessRuntimeState::Running
    ) {
        addViolation(
            report,
            "invalid_step_limit_state",
            "step-limit result must remain in Running state"
        );
    }

    std::map<kernel::ProcessId, std::size_t> lastPcAfter;
    std::size_t previousExecutionStep = 0;

    for (
        std::size_t index = 0;
        index < result.execution_trace.size();
        ++index
    ) {
        const ProcessExecutionTraceRecord& record =
            result.execution_trace[index];

        if (
            record.lifecycle_step == 0
            || record.lifecycle_step > result.lifecycle_steps
        ) {
            addViolation(
                report,
                "execution_step_out_of_range",
                "execution event lifecycle step is out of range",
                record.lifecycle_step,
                record.pid
            );
        }

        if (
            index != 0
            && record.lifecycle_step <= previousExecutionStep
        ) {
            addViolation(
                report,
                "execution_steps_not_strict",
                "execution events are not strictly ordered",
                record.lifecycle_step,
                record.pid
            );
        }

        previousExecutionStep = record.lifecycle_step;

        if (processIds.find(record.pid) == processIds.end()) {
            addViolation(
                report,
                "execution_unknown_pid",
                "execution event references an unknown PID",
                record.lifecycle_step,
                record.pid
            );
        }

        const auto previous = lastPcAfter.find(record.pid);

        if (
            previous != lastPcAfter.end()
            && record.event.pcBefore() != previous->second
        ) {
            addViolation(
                report,
                "execution_pc_discontinuity",
                "PID execution PC does not resume from its prior event",
                record.lifecycle_step,
                record.pid
            );
        }

        lastPcAfter[record.pid] = record.event.pcAfter();
    }

    std::size_t previousSwitchStep = 0;
    std::size_t preemptedSwitches = 0;
    std::size_t terminationSwitches = 0;

    for (
        std::size_t index = 0;
        index < result.context_switches.size();
        ++index
    ) {
        const ProcessContextSwitchTraceRecord& record =
            result.context_switches[index];

        if (
            record.lifecycle_step == 0
            || record.lifecycle_step > result.lifecycle_steps
        ) {
            addViolation(
                report,
                "switch_step_out_of_range",
                "context-switch lifecycle step is out of range",
                record.lifecycle_step,
                record.from_pid
            );
        }

        if (
            index != 0
            && record.lifecycle_step <= previousSwitchStep
        ) {
            addViolation(
                report,
                "switch_steps_not_strict",
                "context switches are not strictly ordered",
                record.lifecycle_step,
                record.from_pid
            );
        }

        previousSwitchStep = record.lifecycle_step;

        if (processIds.find(record.from_pid) == processIds.end()) {
            addViolation(
                report,
                "switch_unknown_from_pid",
                "context switch references an unknown source PID",
                record.lifecycle_step,
                record.from_pid
            );
        }

        if (
            record.to_pid != 0
            && processIds.find(record.to_pid) == processIds.end()
        ) {
            addViolation(
                report,
                "switch_unknown_to_pid",
                "context switch references an unknown destination PID",
                record.lifecycle_step,
                record.to_pid
            );
        }

        if (record.from_pid == record.to_pid) {
            addViolation(
                report,
                "switch_same_pid",
                "context-switch record does not change PID",
                record.lifecycle_step,
                record.from_pid
            );
        }

        if (
            record.preempted
            && record.caused_by_termination
        ) {
            addViolation(
                report,
                "switch_conflicting_cause",
                "context switch cannot be both preemption and termination",
                record.lifecycle_step,
                record.from_pid
            );
        }

        if (
            record.to_pid == 0
            && !record.caused_by_termination
        ) {
            addViolation(
                report,
                "switch_to_zero_without_termination",
                "PID 0 destination requires process termination",
                record.lifecycle_step,
                record.from_pid
            );
        }

        if (record.preempted) {
            ++preemptedSwitches;
        }

        if (record.caused_by_termination) {
            ++terminationSwitches;
        }
    }

    if (preemptedSwitches != result.context_switch_count) {
        addViolation(
            report,
            "preempted_switch_count_mismatch",
            "preempted switch records do not match scheduler count"
        );
    }

    if (terminationSwitches != result.termination_count) {
        addViolation(
            report,
            "termination_switch_count_mismatch",
            "termination switch records do not match termination count"
        );
    }

    std::size_t executionIndex = 0;
    std::size_t switchIndex = 0;
    kernel::ProcessId currentPid = 0;

    for (
        std::size_t lifecycleStep = 1;
        lifecycleStep <= result.lifecycle_steps;
        ++lifecycleStep
    ) {
        const bool hasExecution =
            executionIndex < result.execution_trace.size()
            && result.execution_trace[
                executionIndex
            ].lifecycle_step == lifecycleStep;

        const bool hasSwitch =
            switchIndex < result.context_switches.size()
            && result.context_switches[
                switchIndex
            ].lifecycle_step == lifecycleStep;

        if (currentPid == 0) {
            if (hasExecution) {
                currentPid =
                    result.execution_trace[executionIndex].pid;
            } else if (hasSwitch) {
                currentPid =
                    result.context_switches[switchIndex].from_pid;
            }
        }

        if (hasExecution) {
            const ProcessExecutionTraceRecord& record =
                result.execution_trace[executionIndex];

            if (
                currentPid != 0
                && record.pid != currentPid
            ) {
                addViolation(
                    report,
                    "execution_pid_mismatch",
                    "execution event PID does not match running timeline PID",
                    lifecycleStep,
                    record.pid
                );
            }

            ++executionIndex;
        }

        if (hasSwitch) {
            const ProcessContextSwitchTraceRecord& record =
                result.context_switches[switchIndex];

            if (
                currentPid != 0
                && record.from_pid != currentPid
            ) {
                addViolation(
                    report,
                    "switch_from_pid_mismatch",
                    "context switch source does not match running timeline PID",
                    lifecycleStep,
                    record.from_pid
                );
            }

            currentPid = record.to_pid;
            ++switchIndex;
        }
    }

    if (
        executionIndex != result.execution_trace.size()
        || switchIndex != result.context_switches.size()
    ) {
        addViolation(
            report,
            "timeline_unconsumed_records",
            "timeline contains records outside lifecycle ordering"
        );
    }

    if (
        result.runtime_state == kernel::ProcessRuntimeState::Running
        && result.lifecycle_steps != 0
        && currentPid == 0
    ) {
        addViolation(
            report,
            "running_without_pid",
            "Running runtime timeline ends without a running PID"
        );
    }

    if (
        result.runtime_state == kernel::ProcessRuntimeState::Completed
        && currentPid != 0
    ) {
        addViolation(
            report,
            "completed_with_running_pid",
            "Completed runtime timeline still has a running PID",
            result.lifecycle_steps,
            currentPid
        );
    }

    return report;
}

} // namespace zero_cpu::system
