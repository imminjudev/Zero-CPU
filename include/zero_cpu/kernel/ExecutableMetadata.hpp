#pragma once

#include "zero_cpu/binary/BinaryFormat.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace zero_cpu::kernel {

struct ExecutableMetadata {
    std::string source_name;

    std::uint8_t format_major_version =
        binary::kMajorVersion;

    std::uint8_t format_minor_version =
        binary::kMinorVersion;

    binary::BinaryEndianness endianness =
        binary::BinaryEndianness::Little;

    std::size_t memory_size = 0;

    std::size_t code_base = 0;
    std::size_t code_size = 0;
    std::size_t code_end_exclusive = 0;

    std::size_t entry_offset = 0;
    std::size_t entry_point = 0;

    std::size_t user_data_begin = 0;
    std::size_t user_data_end_exclusive = 0;

    std::size_t user_stack_begin = 0;
    std::size_t user_stack_end_exclusive = 0;
};

void validateExecutableMetadata(
    const ExecutableMetadata& metadata
);

} // namespace zero_cpu::kernel
