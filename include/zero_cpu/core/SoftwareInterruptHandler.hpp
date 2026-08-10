#pragma once

#include <cstdint>

namespace zero_cpu {

class CPUState;
class MMIOBus;

class SoftwareInterruptHandler {
public:
    virtual ~SoftwareInterruptHandler() = default;

    virtual bool handles(
        std::uint8_t vector
    ) const = 0;

    virtual void handle(
        std::uint8_t vector,
        CPUState& state,
        MMIOBus* mmioBus
    ) = 0;
};

} // namespace zero_cpu

// Patch: v1.4-protected-syscall-hardware-r1
