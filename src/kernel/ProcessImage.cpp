#include "zero_cpu/kernel/ProcessImage.hpp"

#include <algorithm>
#include <stdexcept>

namespace zero_cpu::kernel {

void validateProcessImage(
    const ProcessImage& image
) {
    validateExecutableMetadata(image.metadata);

    if (
        image.executable.header.major_version
            != image.metadata.format_major_version
        || image.executable.header.minor_version
            != image.metadata.format_minor_version
        || image.executable.header.endianness
            != image.metadata.endianness
    ) {
        throw std::runtime_error(
            "Process image executable header does not "
            "match its metadata"
        );
    }

    if (
        static_cast<std::size_t>(
            image.executable.header.code_size
        )
            != image.metadata.code_size
        || image.executable.code.size()
            != image.metadata.code_size
    ) {
        throw std::runtime_error(
            "Process image executable code size does not "
            "match its metadata"
        );
    }

    if (
        static_cast<std::size_t>(
            image.executable.header.entry_point
        )
        != image.metadata.entry_offset
    ) {
        throw std::runtime_error(
            "Process image executable entry point does "
            "not match its metadata"
        );
    }

    if (
        static_cast<std::size_t>(
            image.executable.header.data_base
        )
            != image.metadata.data_base
        || static_cast<std::size_t>(
            image.executable.header.data_size
        )
            != image.metadata.data_size
        || image.executable.data.size()
            != image.metadata.data_size
    ) {
        throw std::runtime_error(
            "Process image executable data section does "
            "not match its metadata"
        );
    }

    if (
        image.memory.size()
        != image.metadata.memory_size
    ) {
        throw std::runtime_error(
            "Process image memory size does not match "
            "its metadata"
        );
    }

    if (
        image.memory.readBytes(
            image.metadata.code_base,
            image.metadata.code_size
        )
        != image.executable.code
    ) {
        throw std::runtime_error(
            "Process image memory does not contain "
            "the executable code section"
        );
    }

    if (
        image.memory.readBytes(
            image.metadata.data_base,
            image.metadata.data_size
        )
        != image.executable.data
    ) {
        throw std::runtime_error(
            "Process image memory does not contain "
            "the initialized data section"
        );
    }

    const bool hasNonZeroRegister =
        std::any_of(
            image.initial_registers.begin(),
            image.initial_registers.end(),
            [](
                std::int64_t value
            ) {
                return value != 0;
            }
        );

    if (hasNonZeroRegister) {
        throw std::runtime_error(
            "Process image initial registers "
            "must be zero"
        );
    }

    if (image.initial_flags != 0) {
        throw std::runtime_error(
            "Process image initial FLAGS must be zero"
        );
    }

    if (
        image.initial_pc
        != image.metadata.entry_point
    ) {
        throw std::runtime_error(
            "Process image initial PC does not match "
            "the executable entry point"
        );
    }

    if (
        image.initial_sp
        != image.metadata.user_stack_begin
    ) {
        throw std::runtime_error(
            "Process image initial SP does not match "
            "the User stack base"
        );
    }

    if (
        image.initial_privilege
        != PrivilegeLevel::User
    ) {
        throw std::runtime_error(
            "Process image initial privilege "
            "must be User"
        );
    }

    if (
        image.initial_kernel_stack_pointer
        != memory_map::kKernelStackBase
    ) {
        throw std::runtime_error(
            "Process image initial Kernel SP does not "
            "match the Kernel stack base"
        );
    }
}

} // namespace zero_cpu::kernel
