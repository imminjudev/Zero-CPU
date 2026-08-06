#include "zero_cpu/binary/BinaryLoader.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"

#include <stdexcept>
#include <utility>

namespace zero_cpu {
namespace binary {
namespace {

bool rangesOverlap(
    std::size_t firstBegin,
    std::size_t firstSize,
    std::size_t secondBegin,
    std::size_t secondSize
) {
    if (
        firstSize == 0
        || secondSize == 0
    ) {
        return false;
    }

    const std::size_t firstEnd =
        firstBegin + firstSize;

    const std::size_t secondEnd =
        secondBegin + secondSize;

    return firstBegin < secondEnd
        && secondBegin < firstEnd;
}

} // namespace

LoadedBinaryImage BinaryLoader::loadIntoMemory(
    const BinaryProgram& program,
    Memory& memory,
    std::size_t codeBase
) const {
    validateProgram(program);

    validatePlacement(
        program,
        memory,
        codeBase
    );

    Memory stagedMemory = memory;

    if (!program.data.empty()) {
        stagedMemory.writeBytes(
            static_cast<std::size_t>(
                program.header.data_base
            ),
            program.data
        );
    }

    stagedMemory.writeBytes(
        codeBase,
        program.code
    );

    memory = std::move(stagedMemory);

    LoadedBinaryImage image;
    image.code_base = codeBase;

    image.entry_point =
        codeBase
        + static_cast<std::size_t>(
            program.header.entry_point
        );

    image.code_size =
        program.code.size();

    image.data_base =
        static_cast<std::size_t>(
            program.header.data_base
        );

    image.data_size =
        program.data.size();

    return image;
}

void BinaryLoader::validateProgram(
    const BinaryProgram& program
) const {
    if (
        !isSupportedVersion(
            program.header.major_version,
            program.header.minor_version
        )
    ) {
        throw std::runtime_error(
            "BinaryLoader does not support "
            "this binary version"
        );
    }

    if (
        program.header.endianness
        != BinaryEndianness::Little
    ) {
        throw std::runtime_error(
            "BinaryLoader currently supports only "
            "little-endian binaries"
        );
    }

    if (program.code.empty()) {
        throw std::runtime_error(
            "Binary code section is empty"
        );
    }

    if (
        static_cast<std::size_t>(
            program.header.code_size
        )
        != program.code.size()
    ) {
        throw std::runtime_error(
            "Binary header code_size does not match "
            "actual code size"
        );
    }

    if (
        static_cast<std::size_t>(
            program.header.data_size
        )
        != program.data.size()
    ) {
        throw std::runtime_error(
            "Binary header data_size does not match "
            "actual data size"
        );
    }

    if (
        program.code.size()
        % kInstructionSize
        != 0
    ) {
        throw std::runtime_error(
            "Binary code size must be a multiple "
            "of instruction size"
        );
    }

    if (
        program.header.entry_point
        >= program.header.code_size
    ) {
        throw std::runtime_error(
            "Binary entry point is outside "
            "the code section"
        );
    }

    if (
        program.header.entry_point
        % kInstructionAlignment
        != 0
    ) {
        throw std::runtime_error(
            "Binary entry point is not "
            "instruction-aligned"
        );
    }

    if (
        isLegacyVersion(
            program.header.major_version,
            program.header.minor_version
        )
        && (
            !program.data.empty()
            || program.header.data_base != 0
            || program.header.data_size != 0
        )
    ) {
        throw std::runtime_error(
            "Legacy binary format cannot "
            "contain a data section"
        );
    }
}

void BinaryLoader::validatePlacement(
    const BinaryProgram& program,
    const Memory& memory,
    std::size_t codeBase
) const {
    if (
        codeBase > memory.size()
        || program.code.size()
            > memory.size() - codeBase
    ) {
        throw std::runtime_error(
            "Binary code section is outside memory"
        );
    }

    const std::size_t dataBase =
        static_cast<std::size_t>(
            program.header.data_base
        );

    if (
        dataBase > memory.size()
        || program.data.size()
            > memory.size() - dataBase
    ) {
        throw std::runtime_error(
            "Binary data section is outside memory"
        );
    }

    if (
        rangesOverlap(
            codeBase,
            program.code.size(),
            dataBase,
            program.data.size()
        )
    ) {
        throw std::runtime_error(
            "Binary code and data sections overlap"
        );
    }
}

} // namespace binary
} // namespace zero_cpu
