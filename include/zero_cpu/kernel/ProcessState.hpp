#pragma once

#include <cstdint>

namespace zero_cpu::kernel {

enum class ProcessState : std::uint8_t {
    Ready = 0,
    Running = 1,
    Blocked = 2,
    Terminated = 3
};

const char* processStateToString(ProcessState state);

bool isRunnableProcessState(ProcessState state);
bool isTerminalProcessState(ProcessState state);

bool canTransitionProcessState(
    ProcessState from,
    ProcessState to
);

} // namespace zero_cpu::kernel
