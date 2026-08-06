#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include "zero_cpu/binary/BinaryLoader.hpp"
#include "zero_cpu/binary/BinaryReader.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/kernel/ExecutableMetadata.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace zero_cpu::kernel {
namespace {

ExecutableMetadata makeMetadata(
    const binary::BinaryProgram& program,
    std::string sourceName
) {
    ExecutableMetadata metadata;
    metadata.source_name = std::move(sourceName);

    metadata.format_major_version =
        program.header.major_version;

    metadata.format_minor_version =
        program.header.minor_version;

    metadata.endianness =
        program.header.endianness;

    metadata.memory_size =
        memory_map::kDefaultMemorySize;

    metadata.code_base =
        memory_map::kBinaryCodeBase;

    metadata.code_size =
        static_cast<std::size_t>(
            program.header.code_size
        );

    if (
        metadata.code_size
        > std::numeric_limits<std::size_t>::max()
            - metadata.code_base
    ) {
        throw std::runtime_error(
            "Executable code end address overflow"
        );
    }

    metadata.code_end_exclusive =
        metadata.code_base
        + metadata.code_size;

    metadata.entry_offset =
        static_cast<std::size_t>(
            program.header.entry_point
        );

    if (
        metadata.entry_offset
        > std::numeric_limits<std::size_t>::max()
            - metadata.code_base
    ) {
        throw std::runtime_error(
            "Executable entry point address overflow"
        );
    }

    metadata.entry_point =
        metadata.code_base
        + metadata.entry_offset;

    metadata.user_data_begin =
        memory_map::kUserDataBase;

    metadata.user_data_end_exclusive =
        memory_map::kUserDataEndExclusive;

    metadata.user_stack_begin =
        memory_map::kUserStackBase;

    metadata.user_stack_end_exclusive =
        memory_map::kUserStackEndExclusive;

    return metadata;
}

} // namespace

ProcessImage ProcessImageLoader::loadProgram(
    const binary::BinaryProgram& program,
    std::string sourceName
) const {
    ProcessImage image;

    image.metadata = makeMetadata(
        program,
        std::move(sourceName)
    );

    validateExecutableMetadata(
        image.metadata
    );

    image.executable = program;

    binary::BinaryLoader loader;

    const binary::LoadedBinaryImage loaded =
        loader.loadIntoMemory(
            image.executable,
            image.memory,
            image.metadata.code_base
        );

    if (
        loaded.code_base
            != image.metadata.code_base
        || loaded.code_size
            != image.metadata.code_size
        || loaded.entry_point
            != image.metadata.entry_point
    ) {
        throw std::runtime_error(
            "Binary loader result does not match "
            "process image metadata"
        );
    }

    image.initial_pc =
        image.metadata.entry_point;

    image.initial_sp =
        image.metadata.user_stack_begin;

    image.initial_privilege =
        PrivilegeLevel::User;

    image.initial_kernel_stack_pointer =
        memory_map::kKernelStackBase;

    validateProcessImage(image);
    return image;
}

ProcessImage ProcessImageLoader::loadFromBytes(
    const std::vector<std::uint8_t>& bytes,
    std::string sourceName
) const {
    binary::BinaryReader reader;

    return loadProgram(
        reader.readFromBytes(bytes),
        std::move(sourceName)
    );
}

ProcessImage ProcessImageLoader::loadFile(
    const std::string& path
) const {
    binary::BinaryReader reader;

    return loadProgram(
        reader.readFile(path),
        path
    );
}

} // namespace zero_cpu::kernel
