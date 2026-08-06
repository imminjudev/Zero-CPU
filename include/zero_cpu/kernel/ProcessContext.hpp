#pragma once

#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/PrivilegeLevel.hpp"
#include "zero_cpu/core/RegisterFile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace zero_cpu {

class CPU;

} // namespace zero_cpu

namespace zero_cpu::kernel {

using ProcessId = std::uint32_t;

struct ProcessContext {
    ProcessId pid = 0;

    std::array<
        std::int64_t,
        RegisterFile::kRegisterCount
    > registers{};

    std::uint32_t flags = 0;
    std::size_t pc = 0;
    std::size_t sp = memory_map::kUserStackBase;

    PrivilegeLevel privilege = PrivilegeLevel::User;

    bool has_user_code_range = false;
    std::size_t user_code_begin = 0;
    std::size_t user_code_end_exclusive = 0;

    std::size_t user_stack_begin =
        memory_map::kUserStackBase;
    std::size_t user_stack_end_exclusive =
        memory_map::kUserStackEndExclusive;

    std::size_t kernel_stack_pointer =
        memory_map::kKernelStackBase;
};

void validateProcessContextSnapshot(
    const ProcessContext& context
);

void validateProcessContext(
    const ProcessContext& context
);

void validateProcessContextForCPU(
    const ProcessContext& context,
    const CPU& cpu
);

ProcessContext captureProcessContextSnapshot(
    ProcessId pid,
    const CPU& cpu
);

ProcessContext captureProcessContext(
    ProcessId pid,
    const CPU& cpu
);

void restoreProcessContext(
    const ProcessContext& context,
    CPU& cpu
);

} // namespace zero_cpu::kernel
