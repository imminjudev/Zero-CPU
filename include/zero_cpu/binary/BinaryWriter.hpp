#pragma once

#include "zero_cpu/binary/BinaryProgram.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace zero_cpu {

namespace binary {

class BinaryWriter {
public:
    void writeFile(
        const std::string& path,
        const BinaryProgram& program
    ) const;

    std::vector<std::uint8_t> writeToBytes(
        const BinaryProgram& program
    ) const;

private:
    void writeHeader(
        std::vector<std::uint8_t>& output,
        const BinaryHeader& header
    ) const;

    void writeU32(
        std::vector<std::uint8_t>& output,
        std::uint32_t value,
        BinaryEndianness endian
    ) const;
};

} // namespace binary

} // namespace zero_cpu