#pragma once

#include "zero_cpu/binary/BinaryFormat.hpp"

#include <cstdint>
#include <vector>

namespace zero_cpu {

namespace binary {

struct BinaryHeader {
    std::uint8_t major_version = kMajorVersion;
    std::uint8_t minor_version = kMinorVersion;
    BinaryEndianness endianness = BinaryEndianness::Little;
    std::uint32_t entry_point = 0;
    std::uint32_t code_size = 0;
};

struct BinaryProgram {
    BinaryHeader header;
    std::vector<std::uint8_t> code;
};

} // namespace binary

} // namespace zero_cpu