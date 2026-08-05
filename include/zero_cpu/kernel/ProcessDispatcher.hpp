#pragma once

#include "zero_cpu/kernel/ProcessTable.hpp"
#include "zero_cpu/kernel/RoundRobinScheduler.hpp"

namespace zero_cpu {

class CPU;

} // namespace zero_cpu

namespace zero_cpu::kernel {

class ProcessDispatcher {
public:
    ProcessId dispatchNext(
        CPU& cpu,
        ProcessTable& table,
        RoundRobinScheduler& scheduler
    ) const;
};

} // namespace zero_cpu::kernel
