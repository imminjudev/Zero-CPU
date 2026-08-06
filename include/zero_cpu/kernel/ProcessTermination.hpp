#pragma once

#include <cstdint>

namespace zero_cpu::kernel {

enum class ProcessTerminationKind : std::uint8_t {
    NormalExit = 0,
    CpuFault = 1
};

const char* processTerminationKindToString(
    ProcessTerminationKind kind
);

} // namespace zero_cpu::kernel
