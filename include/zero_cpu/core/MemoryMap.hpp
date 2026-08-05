#pragma once

#include <cstddef>

namespace zero_cpu::memory_map {

// Low memory is currently used by examples and CLI tests for simple data.
// Keep ordinary test metadata below kBinaryCodeBase unless a test intentionally
// checks code/data overlap behavior.
inline constexpr std::size_t kLowMemoryBase = 0x0000;
inline constexpr std::size_t kLowMemorySize = 0x0200;
inline constexpr std::size_t kLowMemoryEndExclusive =
    kLowMemoryBase + kLowMemorySize;

// User-mode LOAD/STORE data accesses are restricted to low memory.
// Kernel mode remains unrestricted.
inline constexpr std::size_t kUserDataBase = kLowMemoryBase;
inline constexpr std::size_t kUserDataSize = kLowMemorySize;
inline constexpr std::size_t kUserDataEndExclusive =
    kUserDataBase + kUserDataSize;

// .zbin code is loaded at this address by convention.
inline constexpr std::size_t kBinaryCodeBase = 0x0200;

// The original default CPU stack base.
// This is still used by small examples and unit-style tests.
inline constexpr std::size_t kDefaultStackBase = 0x0800;

// BIO-OS combined examples can grow beyond the original default stack base.
// Keep the integration-demo stack high enough to avoid code/stack overlap,
// but below the current 4 KiB memory boundary so the first PUSH is in range.
inline constexpr std::size_t kBioOSStackBase = 0x0FA0;

// Current default memory size used by the core Memory model.
inline constexpr std::size_t kDefaultMemorySize = 0x1000;

// Protected stack layout. User code keeps the original stack base, while
// privilege transitions use the high BIO-OS stack region as a Kernel-only
// interrupt stack.
inline constexpr std::size_t kUserStackBase = kDefaultStackBase;
inline constexpr std::size_t kUserStackEndExclusive = kBioOSStackBase;
inline constexpr std::size_t kKernelStackBase = kBioOSStackBase;
inline constexpr std::size_t kKernelStackEndExclusive =
    kDefaultMemorySize;

// DebugOutputDevice MMIO region.
inline constexpr std::size_t kDebugOutputBase = 0xF000;
inline constexpr std::size_t kDebugOutputSize = 0x0010;
inline constexpr std::size_t kDebugOutputEndExclusive =
    kDebugOutputBase + kDebugOutputSize;

// TimerDevice MMIO region.
inline constexpr std::size_t kTimerBase = 0xF100;
inline constexpr std::size_t kTimerSize = 0x0030;
inline constexpr std::size_t kTimerEndExclusive =
    kTimerBase + kTimerSize;

// TimerDevice register offsets.
inline constexpr std::size_t kTimerTickCountOffset = 0;
inline constexpr std::size_t kTimerIntervalOffset = 8;
inline constexpr std::size_t kTimerEnabledOffset = 16;
inline constexpr std::size_t kTimerVectorOffset = 24;
inline constexpr std::size_t kTimerPayloadOffset = 32;
inline constexpr std::size_t kTimerInterruptCountOffset = 40;

// External hardware bridge MMIO region.
inline constexpr std::size_t kHardwareBase = 0xF200;
inline constexpr std::size_t kHardwareSize = 0x0030;
inline constexpr std::size_t kHardwareEndExclusive =
    kHardwareBase + kHardwareSize;

inline constexpr std::size_t kHardwareRegisterWidth = 8;

// Hardware bridge register offsets.
inline constexpr std::size_t kHardwareGpioOutputOffset = 0;
inline constexpr std::size_t kHardwareGpioInputOffset = 8;
inline constexpr std::size_t kHardwarePwmOutputOffset = 16;
inline constexpr std::size_t kHardwareAdcInputOffset = 24;
inline constexpr std::size_t kHardwareStatusOffset = 32;
inline constexpr std::size_t kHardwareCommandOffset = 40;

// Common scratch ranges used by examples and tests.
// These are conventions, not hardware-enforced protection boundaries.
inline constexpr std::size_t kExampleScratchBase = 100;
inline constexpr std::size_t kMmioTestScratchBase = 200;
inline constexpr std::size_t kSyscallTestScratchBase = 300;
inline constexpr std::size_t kSoftwareInterruptScratchBase = 400;
inline constexpr std::size_t kLastLowScratchBase = 500;

constexpr bool isLowMemoryAddress(std::size_t address) {
    return address >= kLowMemoryBase && address < kLowMemoryEndExclusive;
}

constexpr bool isUserDataRange(
    std::size_t address,
    std::size_t count
) {
    if (address < kUserDataBase) {
        return false;
    }

    if (address > kUserDataEndExclusive) {
        return false;
    }

    return count <= kUserDataEndExclusive - address;
}

constexpr bool isDefaultCodeAddress(std::size_t address) {
    return address >= kBinaryCodeBase && address < kDefaultStackBase;
}

constexpr bool isBioOSCodeAddress(std::size_t address) {
    return address >= kBinaryCodeBase && address < kBioOSStackBase;
}

constexpr bool isCodeAddress(std::size_t address) {
    return isDefaultCodeAddress(address);
}

constexpr bool isUserStackPointer(std::size_t value) {
    return value >= kUserStackBase
        && value <= kUserStackEndExclusive;
}

constexpr bool isKernelStackPointer(std::size_t value) {
    return value >= kKernelStackBase
        && value <= kKernelStackEndExclusive;
}

constexpr bool isDefaultStackAddress(std::size_t address) {
    return address >= kDefaultStackBase && address < kDebugOutputBase;
}

constexpr bool isBioOSStackAddress(std::size_t address) {
    return address >= kBioOSStackBase && address < kDefaultMemorySize;
}

constexpr bool isStackAddress(std::size_t address) {
    return isDefaultStackAddress(address);
}

constexpr bool isDebugOutputAddress(std::size_t address) {
    return address >= kDebugOutputBase && address < kDebugOutputEndExclusive;
}

constexpr bool isTimerAddress(std::size_t address) {
    return address >= kTimerBase && address < kTimerEndExclusive;
}

constexpr bool isHardwareAddress(std::size_t address) {
    return address >= kHardwareBase &&
        address < kHardwareEndExclusive;
}

constexpr bool isMmioAddress(std::size_t address) {
    return isDebugOutputAddress(address) ||
        isTimerAddress(address) ||
        isHardwareAddress(address);
}

constexpr bool isSafeLowDataAddress(std::size_t address) {
    return address < kBinaryCodeBase;
}

} // namespace zero_cpu::memory_map
