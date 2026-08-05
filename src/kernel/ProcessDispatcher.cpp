#include "zero_cpu/kernel/ProcessDispatcher.hpp"

#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/kernel/ProcessContext.hpp"

#include <stdexcept>
#include <utility>

namespace zero_cpu::kernel {

ProcessId ProcessDispatcher::dispatchNext(
    CPU& cpu,
    ProcessTable& table,
    RoundRobinScheduler& scheduler
) const {
    if (cpu.usingKernelInterruptStack()) {
        throw std::runtime_error(
            "Cannot dispatch while Kernel "
            "interrupt stack is active"
        );
    }

    if (!scheduler.hasRunnableProcess(table)) {
        throw std::runtime_error(
            "Process dispatcher has no runnable process"
        );
    }

    ProcessTable stagedTable = table;
    RoundRobinScheduler stagedScheduler =
        scheduler;

    if (table.hasRunningProcess()) {
        const ProcessId currentPid =
            table.runningProcessId();

        const ProcessContext currentContext =
            captureProcessContext(
                currentPid,
                cpu
            );

        stagedTable.updateContext(
            currentPid,
            currentContext
        );
    }

    const ProcessId selectedPid =
        stagedScheduler.scheduleNext(
            stagedTable
        );

    const ProcessContext selectedContext =
        stagedTable.process(
            selectedPid
        ).context();

    validateProcessContextForCPU(
        selectedContext,
        cpu
    );

    restoreProcessContext(
        selectedContext,
        cpu
    );

    table = std::move(stagedTable);
    scheduler = std::move(stagedScheduler);

    return selectedPid;
}

} // namespace zero_cpu::kernel
