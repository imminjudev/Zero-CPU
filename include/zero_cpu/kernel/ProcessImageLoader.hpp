#pragma once

#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/kernel/ProcessImage.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace zero_cpu::kernel {

class ProcessImageLoader {
public:
    ProcessImage loadProgram(
        const binary::BinaryProgram& program,
        std::string sourceName = "<program>"
    ) const;

    ProcessImage loadFromBytes(
        const std::vector<std::uint8_t>& bytes,
        std::string sourceName = "<bytes>"
    ) const;

    ProcessImage loadFile(
        const std::string& path
    ) const;
};

} // namespace zero_cpu::kernel
