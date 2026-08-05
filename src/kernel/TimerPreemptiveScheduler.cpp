#include "zero_cpu/kernel/TimerPreemptiveScheduler.hpp"

#include "zero_cpu/core/CPU.hpp"

#include <stdexcept>

namespace zero_cpu::kernel {

TimerPreemptiveScheduler::TimerPreemptiveScheduler(
    std::uint8_t vector,
    std::uint64_t quantum,
    std::int64_t payload
)
    : controller_(
          std::make_shared<InterruptController>()
      ),
      timer_(
          std::make_shared<TimerDevice>(
              controller_,
              vector,
              quantum,
              payload
          )
      ) {
}

void TimerPreemptiveScheduler::attach(
    CPU& cpu
) {
    if (
        attached_cpu_ != nullptr
        && attached_cpu_ != &cpu
    ) {
        throw std::runtime_error(
            "Timer preemptive scheduler is already "
            "attached to another CPU"
        );
    }

    if (attached_cpu_ == &cpu) {
        return;
    }

    if (cpu.hasInterruptController()) {
        throw std::runtime_error(
            "Timer preemptive scheduler requires a CPU "
            "without an existing InterruptController"
        );
    }

    cpu.setInterruptController(controller_);
    cpu.addClockedDevice(timer_);

    attached_cpu_ = &cpu;
}

bool TimerPreemptiveScheduler::attached() const {
    return attached_cpu_ != nullptr;
}

bool TimerPreemptiveScheduler::started() const {
    return started_;
}

ProcessId TimerPreemptiveScheduler::start(
    CPU& cpu,
    ProcessTable& table,
    RoundRobinScheduler& scheduler
) {
    if (started_) {
        throw std::runtime_error(
            "Timer preemptive scheduler is already started"
        );
    }

    attach(cpu);

    const ProcessId selectedPid =
        dispatcher_.dispatchNext(
            cpu,
            table,
            scheduler
        );

    started_ = true;
    return selectedPid;
}

ProcessId TimerPreemptiveScheduler::step(
    CPU& cpu,
    ProcessTable& table,
    RoundRobinScheduler& scheduler
) {
    if (!started_) {
        throw std::runtime_error(
            "Timer preemptive scheduler is not started"
        );
    }

    if (attached_cpu_ != &cpu) {
        throw std::runtime_error(
            "Timer preemptive scheduler CPU mismatch"
        );
    }

    if (!table.hasRunningProcess()) {
        throw std::runtime_error(
            "Timer preemptive scheduler has no "
            "Running process"
        );
    }

    ProcessId selectedPid =
        table.runningProcessId();

    if (
        servicePendingPreemption(
            cpu,
            table,
            scheduler,
            selectedPid
        )
    ) {
        return selectedPid;
    }

    cpu.step();

    if (
        cpu.state().hasError()
        || cpu.state().halted()
    ) {
        return table.runningProcessId();
    }

    (void)servicePendingPreemption(
        cpu,
        table,
        scheduler,
        selectedPid
    );

    return selectedPid;
}

std::uint64_t
TimerPreemptiveScheduler::preemptionCount() const {
    return preemption_count_;
}

std::uint64_t
TimerPreemptiveScheduler::contextSwitchCount() const {
    return context_switch_count_;
}

std::shared_ptr<InterruptController>
TimerPreemptiveScheduler::interruptController() const {
    return controller_;
}

std::shared_ptr<TimerDevice>
TimerPreemptiveScheduler::timerDevice() const {
    return timer_;
}

bool TimerPreemptiveScheduler::
servicePendingPreemption(
    CPU& cpu,
    ProcessTable& table,
    RoundRobinScheduler& scheduler,
    ProcessId& selectedPid
) {
    constexpr const char* kTimerSource = "timer";

    if (
        !controller_->hasPending(
            timer_->vector(),
            kTimerSource
        )
    ) {
        return false;
    }

    const ProcessId previousPid =
        table.runningProcessId();

    selectedPid = dispatcher_.dispatchNext(
        cpu,
        table,
        scheduler
    );

    const InterruptRequest request =
        controller_->acknowledge(
            timer_->vector(),
            kTimerSource
        );

    if (
        request.vector != timer_->vector()
        || request.source != kTimerSource
    ) {
        throw std::runtime_error(
            "Timer preemption acknowledged "
            "an unexpected interrupt"
        );
    }

    ++preemption_count_;

    if (selectedPid != previousPid) {
        ++context_switch_count_;
    }

    return true;
}

} // namespace zero_cpu::kernel
