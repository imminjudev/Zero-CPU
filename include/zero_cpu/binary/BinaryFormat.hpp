#pragma once

#include "zero_cpu/core/Memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace zero_cpu {

namespace binary {

inline constexpr std::array<std::uint8_t, 4> kMagic = {
    static_cast<std::uint8_t>('Z'),
    static_cast<std::uint8_t>('C'),
    static_cast<std::uint8_t>('P'),
    static_cast<std::uint8_t>('U')
};

inline constexpr std::uint8_t kMajorVersion = 0;
inline constexpr std::uint8_t kMinorVersion = 2;

inline constexpr std::size_t kHeaderSize = 16;
inline constexpr std::size_t kInstructionSize = 24;
inline constexpr std::size_t kInstructionAlignment = 4;

inline constexpr std::size_t kMagicOffset = 0;
inline constexpr std::size_t kMajorVersionOffset = 4;
inline constexpr std::size_t kMinorVersionOffset = 5;
inline constexpr std::size_t kEndiannessOffset = 6;
inline constexpr std::size_t kReservedOffset = 7;
inline constexpr std::size_t kEntryPointOffset = 8;
inline constexpr std::size_t kCodeSizeOffset = 12;

enum class BinaryEndianness : std::uint8_t {
    Little = 1,
    Big = 2
};

inline Endianness toMemoryEndianness(BinaryEndianness endian) {
    switch (endian) {
    case BinaryEndianness::Little:
        return Endianness::Little;
    case BinaryEndianness::Big:
        return Endianness::Big;
    }

    return Endianness::Little;
}

inline std::string magicString() {
    return "ZCPU";
}

inline bool isValidInstructionAddress(std::size_t address) {
    return address % kInstructionAlignment == 0;
}

inline std::size_t instructionIndexToAddress(std::size_t index) {
    return index * kInstructionSize;
}

} // namespace binary

} // namespace zero_cpu