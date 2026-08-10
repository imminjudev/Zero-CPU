#pragma once

#include "zero_cpu/core/SoftwareInterruptHandler.hpp"

#include <cstdint>

namespace zero_cpu::kernel {

class ProtectedSyscallDispatcher final
    : public SoftwareInterruptHandler {
public:
    inline static constexpr std::uint8_t
        kSyscallVector = 80;

    inline static constexpr std::int64_t
        kExitSyscall = 3;

    inline static constexpr std::int64_t
        kHardwareWriteSyscall = 20;

    inline static constexpr std::int64_t
        kHardwareReadSyscall = 21;

    inline static constexpr std::int64_t
        kStatusOk = 0;

    inline static constexpr std::int64_t
        kStatusUnsupported = -1;

    inline static constexpr std::int64_t
        kStatusInvalidHardwareOffset = -2;

    inline static constexpr std::int64_t
        kStatusHardwareUnavailable = -3;

    inline static constexpr std::int64_t
        kStatusHardwareError = -4;

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
