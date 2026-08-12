#pragma once

#include "zero_cpu/core/SoftwareInterruptHandler.hpp"
#include "zero_cpu/kernel/ProtectedSyscallABI.hpp"

#include <cstdint>

namespace zero_cpu::kernel {

class ProtectedSyscallDispatcher final
    : public SoftwareInterruptHandler {
public:
    bool handles(
        std::uint8_t vector
    ) const override;

    SoftwareInterruptResult handle(
        std::uint8_t vector,
        CPUState& state,
        MMIOBus* mmioBus
    ) override;

private:
    static bool validHardwareOffset(
        std::int64_t offset
    );

    static void setStatus(
        CPUState& state,
        std::int64_t status
    );
};

} // namespace zero_cpu::kernel

// Patch: v1.4-protected-syscall-hardware-r1
