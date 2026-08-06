#include "zero_cpu/kernel/ProcessDispatcher.hpp"

#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/kernel/ProcessAddressSpace.hpp"
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

    CPU stagedCpu = cpu;
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

        stagedTable.updateRuntimeState(
            currentPid,
            currentContext,
            cpu.state().memory()
        );
    }

    const ProcessId selectedPid =
        stagedScheduler.scheduleNext(
            stagedTable
        );

    const ProcessControlBlock& selected =
        stagedTable.process(selectedPid);

    selected.addressSpace().activate(
        stagedCpu
    );

    validateProcessContextForCPU(
        selected.context(),
        stagedCpu
    );

    restoreProcessContext(
        selected.context(),
        stagedCpu
    );

    cpu = std::move(stagedCpu);
    table = std::move(stagedTable);
    scheduler = std::move(stagedScheduler);

    return selectedPid;
}

} // namespace zero_cpu::kernel
