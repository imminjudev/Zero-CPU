#pragma once

#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/core/Memory.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/PrivilegeLevel.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/kernel/ExecutableMetadata.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace zero_cpu::kernel {

struct ProcessImage {
    ExecutableMetadata metadata;
    binary::BinaryProgram executable;

    Memory memory{
        memory_map::kDefaultMemorySize
    };

    std::array<
        std::int64_t,
        RegisterFile::kRegisterCount
    > initial_registers{};

    std::uint32_t initial_flags = 0;

    std::size_t initial_pc = 0;

    std::size_t initial_sp =
        memory_map::kUserStackBase;

    PrivilegeLevel initial_privilege =
        PrivilegeLevel::User;

    std::size_t initial_kernel_stack_pointer =
        memory_map::kKernelStackBase;
};

void validateProcessImage(
    const ProcessImage& image
);

} // namespace zero_cpu::kernel
