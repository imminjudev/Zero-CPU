#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/PrivilegeLevel.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/kernel/ProcessImage.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include <algorithm>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

template <typename Function>
bool throwsRuntimeError(Function&& function) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return true;
    }

    return false;
}

zero_cpu::binary::BinaryProgram makeProgram(
    std::size_t instructionCount,
    std::size_t entryInstruction = 0
) {
    using namespace zero_cpu;

    binary::BinaryProgram program;

    program.header.entry_point =
        static_cast<std::uint32_t>(
            binary::instructionIndexToAddress(
                entryInstruction
            )
        );

    program.code.resize(
        instructionCount
        * binary::kInstructionSize
    );

    for (
        std::size_t index = 0;
        index < program.code.size();
        ++index
    ) {
        program.code[index] =
            static_cast<std::uint8_t>(
                index % 251
            );
    }

    program.header.code_size =
        static_cast<std::uint32_t>(
            program.code.size()
        );

    return program;
}

bool metadataAndInitialState(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const binary::BinaryProgram program =
        makeProgram(2, 1);

    ProcessImageLoader loader;

    const ProcessImage image =
        loader.loadProgram(
            program,
            "memory:two-instruction"
        );

    const std::size_t expectedCodeSize =
        2 * binary::kInstructionSize;

    const std::size_t expectedEntry =
        memory_map::kBinaryCodeBase
        + binary::kInstructionSize;

    if (
        image.metadata.source_name
            != "memory:two-instruction"
        || image.metadata.format_major_version
            != binary::kMajorVersion
        || image.metadata.format_minor_version
            != binary::kMinorVersion
        || image.metadata.endianness
            != binary::BinaryEndianness::Little
        || image.metadata.memory_size
            != memory_map::kDefaultMemorySize
        || image.metadata.code_base
            != memory_map::kBinaryCodeBase
        || image.metadata.code_size
            != expectedCodeSize
        || image.metadata.code_end_exclusive
            != memory_map::kBinaryCodeBase
                + expectedCodeSize
        || image.metadata.entry_offset
            != binary::kInstructionSize
        || image.metadata.entry_point
            != expectedEntry
        || image.metadata.user_data_begin
            != memory_map::kUserDataBase
        || image.metadata.user_data_end_exclusive
            != memory_map::kUserDataEndExclusive
        || image.metadata.user_stack_begin
            != memory_map::kUserStackBase
        || image.metadata.user_stack_end_exclusive
            != memory_map::kUserStackEndExclusive
        || image.initial_pc != expectedEntry
        || image.initial_sp
            != memory_map::kUserStackBase
        || image.initial_privilege
            != PrivilegeLevel::User
        || image.initial_kernel_stack_pointer
            != memory_map::kKernelStackBase
        || image.initial_flags != 0
        || image.memory.readBytes(
            memory_map::kBinaryCodeBase,
            expectedCodeSize
        ) != program.code
    ) {
        detail =
            "metadata or initial process state mismatch";
        return false;
    }

    const bool allRegistersZero =
        std::all_of(
            image.initial_registers.begin(),
            image.initial_registers.end(),
            [](
                std::int64_t value
            ) {
                return value == 0;
            }
        );

    if (!allRegistersZero) {
        detail = "initial registers are not zero";
        return false;
    }

    return true;
}

bool bytesAndFileLoading(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const binary::BinaryProgram program =
        makeProgram(3, 2);

    binary::BinaryWriter writer;

    const std::vector<std::uint8_t> bytes =
        writer.writeToBytes(program);

    ProcessImageLoader loader;

    const ProcessImage fromBytes =
        loader.loadFromBytes(
            bytes,
            "bytes:test"
        );

    if (
        fromBytes.metadata.source_name
            != "bytes:test"
        || fromBytes.metadata.entry_offset
            != 2 * binary::kInstructionSize
        || fromBytes.executable.code
            != program.code
    ) {
        detail = "byte-array loading mismatch";
        return false;
    }

    const std::string path =
        "zero_process_image_loader_test.zbin";

    struct FileCleanup {
        std::string path;

        ~FileCleanup() {
            std::remove(path.c_str());
        }
    } cleanup{path};

    writer.writeFile(path, program);

    const ProcessImage fromFile =
        loader.loadFile(path);

    if (
        fromFile.metadata.source_name != path
        || fromFile.executable.code
            != program.code
        || fromFile.initial_pc
            != memory_map::kBinaryCodeBase
                + 2 * binary::kInstructionSize
    ) {
        detail = "file loading mismatch";
        return false;
    }

    return true;
}

bool imagesOwnIndependentMemory(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessImageLoader loader;

    ProcessImage first =
        loader.loadProgram(
            makeProgram(2),
            "first"
        );

    ProcessImage second =
        loader.loadProgram(
            makeProgram(2),
            "second"
        );

    first.memory.writeU8(
        memory_map::kUserDataBase,
        99
    );

    first.memory.writeU8(
        memory_map::kBinaryCodeBase,
        77
    );

    if (
        second.memory.readU8(
            memory_map::kUserDataBase
        ) != 0
        || second.memory.readU8(
            memory_map::kBinaryCodeBase
        ) != second.executable.code.front()
        || first.executable.code.front()
            != second.executable.code.front()
    ) {
        detail =
            "process images share mutable memory";
        return false;
    }

    if (
        !throwsRuntimeError(
            [&] {
                validateProcessImage(first);
            }
        )
    ) {
        detail =
            "mutated code memory passed validation";
        return false;
    }

    validateProcessImage(second);
    return true;
}

bool codeLayoutBoundary(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const std::size_t codeCapacity =
        memory_map::kUserStackBase
        - memory_map::kBinaryCodeBase;

    if (
        codeCapacity
        % binary::kInstructionSize
        != 0
    ) {
        detail =
            "test layout is not instruction-aligned";
        return false;
    }

    const std::size_t maximumInstructionCount =
        codeCapacity
        / binary::kInstructionSize;

    ProcessImageLoader loader;

    const ProcessImage maximumImage =
        loader.loadProgram(
            makeProgram(
                maximumInstructionCount
            ),
            "maximum"
        );

    if (
        maximumImage.metadata.code_end_exclusive
        != memory_map::kUserStackBase
    ) {
        detail =
            "maximum code image did not reach stack boundary";
        return false;
    }

    if (
        !throwsRuntimeError(
            [&] {
                (void)loader.loadProgram(
                    makeProgram(
                        maximumInstructionCount + 1
                    ),
                    "oversized"
                );
            }
        )
    ) {
        detail =
            "code overlapping User stack was accepted";
        return false;
    }

    return true;
}

bool malformedExecutablesAreRejected(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessImageLoader loader;

    binary::BinaryProgram mismatched =
        makeProgram(2);

    mismatched.header.code_size -= 1;

    if (
        !throwsRuntimeError(
            [&] {
                (void)loader.loadProgram(
                    mismatched,
                    "mismatched"
                );
            }
        )
    ) {
        detail =
            "mismatched code size was accepted";
        return false;
    }

    binary::BinaryProgram bigEndian =
        makeProgram(2);

    bigEndian.header.endianness =
        binary::BinaryEndianness::Big;

    if (
        !throwsRuntimeError(
            [&] {
                (void)loader.loadProgram(
                    bigEndian,
                    "big-endian"
                );
            }
        )
    ) {
        detail =
            "big-endian process image was accepted";
        return false;
    }

    binary::BinaryProgram wrongVersion =
        makeProgram(2);

    ++wrongVersion.header.major_version;

    if (
        !throwsRuntimeError(
            [&] {
                (void)loader.loadProgram(
                    wrongVersion,
                    "wrong-version"
                );
            }
        )
    ) {
        detail =
            "unsupported executable version was accepted";
        return false;
    }

    if (
        !throwsRuntimeError(
            [&] {
                (void)loader.loadProgram(
                    makeProgram(2),
                    ""
                );
            }
        )
    ) {
        detail =
            "empty executable source name was accepted";
        return false;
    }

    return true;
}

bool processImageValidation(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessImageLoader loader;

    ProcessImage image =
        loader.loadProgram(
            makeProgram(2, 1),
            "validation"
        );

    image.initial_pc =
        image.metadata.code_end_exclusive;

    if (
        !throwsRuntimeError(
            [&] {
                validateProcessImage(image);
            }
        )
    ) {
        detail =
            "invalid initial PC passed validation";
        return false;
    }

    image.initial_pc =
        image.metadata.entry_point;

    image.initial_registers[
        static_cast<std::size_t>(
            RegisterName::R3
        )
    ] = 1;

    if (
        !throwsRuntimeError(
            [&] {
                validateProcessImage(image);
            }
        )
    ) {
        detail =
            "non-zero initial register passed validation";
        return false;
    }

    image.initial_registers.fill(0);
    validateProcessImage(image);

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Process Image Loader "
           "Test ===\n\n";

    int failures = 0;

    auto report = [&](
        const std::string& name,
        bool passed,
        const std::string& detail
    ) {
        std::cout
            << (passed ? "[PASS] " : "[FAIL] ")
            << name
            << "\n";

        if (!passed) {
            std::cout
                << "       "
                << detail
                << "\n";

            ++failures;
        }
    };

    {
        std::string detail;

        report(
            "Metadata and initial state",
            metadataAndInitialState(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Byte-array and file loading",
            bytesAndFileLoading(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Independent process memory",
            imagesOwnIndependentMemory(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Code layout boundary",
            codeLayoutBoundary(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Malformed executable rejection",
            malformedExecutablesAreRejected(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Process image validation",
            processImageValidation(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Process image loader test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Process image loader test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
