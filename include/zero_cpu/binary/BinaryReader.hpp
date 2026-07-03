#pragma once

#include "zero_cpu/binary/BinaryProgram.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zero_cpu {

namespace binary {

class BinaryReader {
public:
    BinaryProgram readFile(const std::string& path) const;
    BinaryProgram readFromBytes(const std::vector<std::uint8_t>& bytes) const;

private:
    BinaryHeader readHeader(const std::vector<std::uint8_t>& bytes) const;

    std::uint32_t readU32(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset,
        BinaryEndianness endian
    ) const;

    void validateMagic(const std::vector<std::uint8_t>& bytes) const;
    void validateHeader(const BinaryHeader& header) const;
};

} // namespace binary

} // namespace zero_cpu