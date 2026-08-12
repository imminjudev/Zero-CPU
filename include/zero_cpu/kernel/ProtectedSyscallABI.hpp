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

    // v1.8 protected storage/filesystem service family.
    inline static constexpr std::int64_t
        kFilesystemServiceBase = 30;

    inline static constexpr std::int64_t
        kFilesystemStatSyscall = 30;

    inline static constexpr std::int64_t
        kFilesystemReadSyscall = 31;

    inline static constexpr std::int64_t
        kFilesystemWriteSyscall = 32;

    inline static constexpr std::int64_t
        kFilesystemServiceEndExclusive = 40;

    // Guest-memory qword request layout.
    inline static constexpr std::int64_t
        kFilesystemPathPointerOffset = 0;
    inline static constexpr std::int64_t
        kFilesystemPathLengthOffset = 8;
    inline static constexpr std::int64_t
        kFilesystemFileOffsetOffset = 16;
    inline static constexpr std::int64_t
        kFilesystemBufferPointerOffset = 24;
    inline static constexpr std::int64_t
        kFilesystemTransferCountOffset = 32;

    // FS_STAT output fields reuse the request block.
    inline static constexpr std::int64_t
        kFilesystemStatTypeOffset = 16;
    inline static constexpr std::int64_t
        kFilesystemStatSizeOffset = 24;

    inline static constexpr std::int64_t
        kFilesystemNodeFile = 0;
    inline static constexpr std::int64_t
        kFilesystemNodeDirectory = 1;

    inline static constexpr std::int64_t
        kFilesystemMaxPathLength = 255;

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

    inline static constexpr std::int64_t
        kStatusInvalidGuestMemory = -5;
    inline static constexpr std::int64_t
        kStatusFilesystemInvalidPath = -6;
    inline static constexpr std::int64_t
        kStatusFilesystemNotFound = -7;
    inline static constexpr std::int64_t
        kStatusFilesystemTypeError = -8;
    inline static constexpr std::int64_t
        kStatusFilesystemInvalidOffset = -9;
    inline static constexpr std::int64_t
        kStatusFilesystemError = -10;
};

static_assert(
    ProtectedSyscallABI::kFilesystemStatSyscall
        >= ProtectedSyscallABI::kFilesystemServiceBase
    && ProtectedSyscallABI::kFilesystemWriteSyscall
        < ProtectedSyscallABI::kFilesystemServiceEndExclusive,
    "filesystem syscalls must stay inside 30..39"
);

static_assert(
    ProtectedSyscallABI::kFilesystemServiceEndExclusive
        <= ProtectedSyscallABI::kNetworkServiceBase,
    "protected service families must not overlap"
);

} // namespace zero_cpu::kernel
