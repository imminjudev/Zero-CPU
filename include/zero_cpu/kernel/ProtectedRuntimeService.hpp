#pragma once

#include "zero_cpu/core/SoftwareInterruptHandler.hpp"

#include <cstdint>

namespace zero_cpu {
class CPUState;
class MMIOBus;
}

namespace zero_cpu::kernel {

// Extension boundary for protected host-runtime services.
// The dispatcher owns ABI decoding/normalization; each service owns semantics.
class ProtectedRuntimeService {
public:
    virtual ~ProtectedRuntimeService() = default;

    virtual bool handles(
        std::int64_t serviceNumber
    ) const = 0;

    virtual SoftwareInterruptResult handle(
        std::int64_t serviceNumber,
        CPUState& state,
        MMIOBus* mmioBus
    ) = 0;
};

} // namespace zero_cpu::kernel
