#include "zero_cpu/kernel/ProcessState.hpp"

#include <stdexcept>

namespace zero_cpu::kernel {

const char* processStateToString(ProcessState state) {
    switch (state) {
    case ProcessState::Ready:
        return "Ready";

    case ProcessState::Running:
        return "Running";

    case ProcessState::Blocked:
        return "Blocked";

    case ProcessState::Terminated:
        return "Terminated";
    }

    throw std::runtime_error("Invalid process state");
}

bool isRunnableProcessState(ProcessState state) {
    return state == ProcessState::Ready
        || state == ProcessState::Running;
}

bool isTerminalProcessState(ProcessState state) {
    return state == ProcessState::Terminated;
}

bool canTransitionProcessState(
    ProcessState from,
    ProcessState to
) {
    if (from == to) {
        return true;
    }

    switch (from) {
    case ProcessState::Ready:
        return to == ProcessState::Running
            || to == ProcessState::Terminated;

    case ProcessState::Running:
        return to == ProcessState::Ready
            || to == ProcessState::Blocked
            || to == ProcessState::Terminated;

    case ProcessState::Blocked:
        return to == ProcessState::Ready
            || to == ProcessState::Terminated;

    case ProcessState::Terminated:
        return false;
    }

    return false;
}

} // namespace zero_cpu::kernel
