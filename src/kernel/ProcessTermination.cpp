#include "zero_cpu/kernel/ProcessTermination.hpp"

#include <stdexcept>

namespace zero_cpu::kernel {

const char* processTerminationKindToString(
    ProcessTerminationKind kind
) {
    switch (kind) {
    case ProcessTerminationKind::NormalExit:
        return "NormalExit";

    case ProcessTerminationKind::CpuFault:
        return "CpuFault";
    }

    throw std::runtime_error(
        "Invalid process termination kind"
    );
}

} // namespace zero_cpu::kernel
