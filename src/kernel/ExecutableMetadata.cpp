#include "zero_cpu/kernel/ExecutableMetadata.hpp"

#include "zero_cpu/core/MemoryMap.hpp"

#include <limits>
#include <stdexcept>

namespace zero_cpu::kernel {

void validateExecutableMetadata(
    const ExecutableMetadata& metadata
) {
    if (metadata.source_name.empty()) {
        throw std::runtime_error(
            "Executable source name must not be empty"
        );
    }

    if (
        metadata.format_major_version
        != binary::kMajorVersion
    ) {
        throw std::runtime_error(
            "Unsupported executable major version"
        );
    }

    if (
        metadata.format_minor_version
        != binary::kMinorVersion
    ) {
        throw std::runtime_error(
            "Unsupported executable minor version"
        );
    }

    if (
        metadata.endianness
        != binary::BinaryEndianness::Little
    ) {
        throw std::runtime_error(
            "Process images currently require "
            "little-endian executables"
        );
    }

    if (
        metadata.memory_size
        != memory_map::kDefaultMemorySize
    ) {
        throw std::runtime_error(
            "Process image memory size does not match "
            "the protected Zero-CPU memory layout"
        );
    }

    if (
        metadata.code_base
        != memory_map::kBinaryCodeBase
    ) {
        throw std::runtime_error(
            "Process image code base does not match "
            "the current executable loading convention"
        );
    }

    if (metadata.code_size == 0) {
        throw std::runtime_error(
            "Executable code section is empty"
        );
    }

    if (
        metadata.code_size
        % binary::kInstructionSize
        != 0
    ) {
        throw std::runtime_error(
            "Executable code size is not a multiple "
            "of instruction size"
        );
    }

    if (
        metadata.code_base
        > metadata.memory_size
        || metadata.code_size
            > metadata.memory_size
                - metadata.code_base
    ) {
        throw std::runtime_error(
            "Executable code section is outside "
            "process memory"
        );
    }

    const std::size_t expectedCodeEnd =
        metadata.code_base
        + metadata.code_size;

    if (
        metadata.code_end_exclusive
        != expectedCodeEnd
    ) {
        throw std::runtime_error(
            "Executable code end does not match "
            "code base and size"
        );
    }

    if (
        metadata.code_end_exclusive
        > memory_map::kUserStackBase
    ) {
        throw std::runtime_error(
            "Executable code section overlaps "
            "the protected User stack"
        );
    }

    if (
        metadata.entry_offset
        >= metadata.code_size
    ) {
        throw std::runtime_error(
            "Executable entry point is outside "
            "the code section"
        );
    }

    if (
        metadata.entry_offset
        % binary::kInstructionAlignment
        != 0
    ) {
        throw std::runtime_error(
            "Executable entry point is not "
            "instruction-aligned"
        );
    }

    if (
        metadata.code_base
        > std::numeric_limits<std::size_t>::max()
            - metadata.entry_offset
    ) {
        throw std::runtime_error(
            "Executable entry point address overflow"
        );
    }

    if (
        metadata.entry_point
        != metadata.code_base
            + metadata.entry_offset
    ) {
        throw std::runtime_error(
            "Executable entry point does not match "
            "code base and entry offset"
        );
    }

    if (
        metadata.user_data_begin
            != memory_map::kUserDataBase
        || metadata.user_data_end_exclusive
            != memory_map::kUserDataEndExclusive
    ) {
        throw std::runtime_error(
            "Executable User data range does not match "
            "the protected memory layout"
        );
    }

    if (
        metadata.user_data_end_exclusive
        > metadata.code_base
    ) {
        throw std::runtime_error(
            "Executable User data range overlaps code"
        );
    }

    if (
        metadata.user_stack_begin
            != memory_map::kUserStackBase
        || metadata.user_stack_end_exclusive
            != memory_map::kUserStackEndExclusive
    ) {
        throw std::runtime_error(
            "Executable User stack range does not match "
            "the protected memory layout"
        );
    }

    if (
        metadata.user_stack_begin
        >= metadata.user_stack_end_exclusive
    ) {
        throw std::runtime_error(
            "Executable User stack range is empty"
        );
    }
}

} // namespace zero_cpu::kernel
