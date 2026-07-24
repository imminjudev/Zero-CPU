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

constexpr bool isDefaultCodeAddress(std::size_t address) {
    return address >= kBinaryCodeBase && address < kDefaultStackBase;
}

constexpr bool isBioOSCodeAddress(std::size_t address) {
    return address >= kBinaryCodeBase && address < kBioOSStackBase;
}

constexpr bool isCodeAddress(std::size_t address) {
    return isDefaultCodeAddress(address);
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

constexpr bool isMmioAddress(std::size_t address) {
    return isDebugOutputAddress(address) || isTimerAddress(address);
}

constexpr bool isSafeLowDataAddress(std::size_t address) {
    return address < kBinaryCodeBase;
}

} // namespace zero_cpu::memory_map
