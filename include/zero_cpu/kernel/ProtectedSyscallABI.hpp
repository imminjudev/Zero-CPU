#pragma once

#include "zero_cpu/core/RegisterFile.hpp"

#include <cstdint>

namespace zero_cpu::kernel {

// Canonical protected host-runtime syscall ABI.
//
// This type owns only the stable User/Kernel contract: interrupt vector,
// register roles, service numbers, status values, and reserved service ranges.
// Runtime behavior belongs to ProtectedSyscallDispatcher and later runtime
// service implementations.
struct ProtectedSyscallABI final {
    inline static constexpr std::uint8_t
        kSyscallVector = 80;

    inline static constexpr RegisterName
        kVectorRegister = RegisterName::R0;

    inline static constexpr RegisterName
        kServiceRegister = RegisterName::R1;

    inline static constexpr RegisterName
        kArgument0ResultRegister = RegisterName::R2;

    inline static constexpr RegisterName
        kArgument1Register = RegisterName::R3;

    inline static constexpr RegisterName
        kStatusRegister = RegisterName::R4;

    inline static constexpr RegisterName
        kExitValueRegister = RegisterName::R7;

    inline static constexpr std::int64_t
        kExitSyscall = 3;

    inline static constexpr std::int64_t
        kHardwareWriteSyscall = 20;

    inline static constexpr std::int64_t
        kHardwareReadSyscall = 21;

    // Reserved for the v1.8 storage/filesystem service family.
    inline static constexpr std::int64_t
        kFilesystemServiceBase = 30;

    inline static constexpr std::int64_t
        kFilesystemServiceEndExclusive = 40;

    // Reserved for the v1.9 network/web service family.
    inline static constexpr std::int64_t
        kNetworkServiceBase = 40;

    inline static constexpr std::int64_t
        kNetworkServiceEndExclusive = 50;

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
};

static_assert(
    ProtectedSyscallABI::kFilesystemServiceEndExclusive
        <= ProtectedSyscallABI::kNetworkServiceBase,
    "protected service families must not overlap"
);

} // namespace zero_cpu::kernel
