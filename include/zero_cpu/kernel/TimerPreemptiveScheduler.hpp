#pragma once

#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/TimerDevice.hpp"
#include "zero_cpu/kernel/ProcessDispatcher.hpp"
#include "zero_cpu/kernel/ProcessTable.hpp"
#include "zero_cpu/kernel/RoundRobinScheduler.hpp"

#include <cstdint>
#include <memory>

namespace zero_cpu {

class CPU;

} // namespace zero_cpu

namespace zero_cpu::kernel {

class TimerPreemptiveScheduler {
public:
    TimerPreemptiveScheduler(
        std::uint8_t vector,
        std::uint64_t quantum,
        std::int64_t payload = 0
    );

    void attach(CPU& cpu);

    bool attached() const;
    bool started() const;

    ProcessId start(
        CPU& cpu,
        ProcessTable& table,
        RoundRobinScheduler& scheduler
    );

    ProcessId step(
        CPU& cpu,
        ProcessTable& table,
        RoundRobinScheduler& scheduler
    );

    std::uint64_t preemptionCount() const;
    std::uint64_t contextSwitchCount() const;

    std::shared_ptr<InterruptController>
    interruptController() const;

    std::shared_ptr<TimerDevice>
    timerDevice() const;

private:
    std::shared_ptr<InterruptController>
        controller_;

    std::shared_ptr<TimerDevice>
        timer_;

    ProcessDispatcher dispatcher_;

    CPU* attached_cpu_ = nullptr;
    bool started_ = false;

    std::uint64_t preemption_count_ = 0;
    std::uint64_t context_switch_count_ = 0;

    bool servicePendingPreemption(
        CPU& cpu,
        ProcessTable& table,
        RoundRobinScheduler& scheduler,
        ProcessId& selectedPid
    );
};

} // namespace zero_cpu::kernel
