#pragma once

#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/core/Memory.hpp"

#include <cstddef>

namespace zero_cpu {
namespace binary {

struct LoadedBinaryImage {
    std::size_t code_base = 0;
    std::size_t entry_point = 0;
    std::size_t code_size = 0;

    std::size_t data_base = 0;
    std::size_t data_size = 0;
};

class BinaryLoader {
public:
    static constexpr std::size_t kDefaultCodeBase = 0;

    LoadedBinaryImage loadIntoMemory(
        const BinaryProgram& program,
        Memory& memory,
        std::size_t codeBase = kDefaultCodeBase
    ) const;

private:
    void validateProgram(
        const BinaryProgram& program
    ) const;

    void validatePlacement(
        const BinaryProgram& program,
        const Memory& memory,
        std::size_t codeBase
    ) const;
};

} // namespace binary
} // namespace zero_cpu
