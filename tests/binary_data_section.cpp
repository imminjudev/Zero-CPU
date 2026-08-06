#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryLoader.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/binary/BinaryReader.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/Memory.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"
#include "zero_cpu/kernel/ProcessDispatcher.hpp"
#include "zero_cpu/kernel/ProcessImage.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"
#include "zero_cpu/kernel/ProcessTable.hpp"
#include "zero_cpu/kernel/RoundRobinScheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
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
    std::vector<std::uint8_t> data,
    std::uint32_t dataBase = 0
) {
    using namespace zero_cpu;

    InstructionEncoder encoder;

    binary::BinaryProgram program;

    program.code = encoder.encodeProgram(
        {
            Instruction(Opcode::NOP),
            Instruction(Opcode::NOP)
        },
        {}
    );

    program.data = std::move(data);

    program.header.entry_point = 0;

    program.header.code_size =
        static_cast<std::uint32_t>(
            program.code.size()
        );

    program.header.data_base = dataBase;

    program.header.data_size =
        static_cast<std::uint32_t>(
            program.data.size()
        );

    return program;
}

bool currentFormatRoundTrip(
    std::string& detail
) {
    using namespace zero_cpu;

    binary::BinaryProgram program =
        makeProgram(
            {1, 2, 3, 4, 5},
            32
        );

    binary::BinaryWriter writer;
    binary::BinaryReader reader;

    const std::vector<std::uint8_t> bytes =
        writer.writeToBytes(program);

    if (
        bytes.size()
        != binary::kHeaderSize
            + program.code.size()
            + program.data.size()
    ) {
        detail =
            "v0.3 serialized size mismatch";
        return false;
    }

    const binary::BinaryProgram decoded =
        reader.readFromBytes(bytes);

    if (
        decoded.header.major_version
            != binary::kMajorVersion
        || decoded.header.minor_version
            != binary::kMinorVersion
        || decoded.header.data_base != 32
        || decoded.header.data_size != 5
        || decoded.code != program.code
        || decoded.data != program.data
    ) {
        detail =
            "v0.3 code/data round trip mismatch";
        return false;
    }

    return true;
}

bool legacyFormatCompatibility(
    std::string& detail
) {
    using namespace zero_cpu;

    binary::BinaryProgram legacy =
        makeProgram({});

    legacy.header.minor_version =
        binary::kLegacyMinorVersion;

    legacy.header.data_base = 0;
    legacy.header.data_size = 0;

    binary::BinaryWriter writer;
    binary::BinaryReader reader;

    const std::vector<std::uint8_t> bytes =
        writer.writeToBytes(legacy);

    if (
        bytes.size()
        != binary::kLegacyHeaderSize
            + legacy.code.size()
    ) {
        detail =
            "legacy serialized size mismatch";
        return false;
    }

    const binary::BinaryProgram decoded =
        reader.readFromBytes(bytes);

    if (
        decoded.header.minor_version
            != binary::kLegacyMinorVersion
        || decoded.header.data_base != 0
        || decoded.header.data_size != 0
        || !decoded.data.empty()
        || decoded.code != legacy.code
    ) {
        detail =
            "legacy binary compatibility mismatch";
        return false;
    }

    legacy.data = {7};

    if (
        !throwsRuntimeError(
            [&] {
                (void)writer.writeToBytes(
                    legacy
                );
            }
        )
    ) {
        detail =
            "legacy writer accepted data section";
        return false;
    }

    return true;
}

bool loaderPlacesSectionsAtomically(
    std::string& detail
) {
    using namespace zero_cpu;

    binary::BinaryProgram program =
        makeProgram(
            {9, 8, 7, 6},
            64
        );

    Memory memory;
    memory.writeU8(10, 55);

    binary::BinaryLoader loader;

    const binary::LoadedBinaryImage image =
        loader.loadIntoMemory(
            program,
            memory,
            memory_map::kBinaryCodeBase
        );

    if (
        image.code_base
            != memory_map::kBinaryCodeBase
        || image.code_size
            != program.code.size()
        || image.data_base != 64
        || image.data_size
            != program.data.size()
        || memory.readBytes(
            64,
            program.data.size()
        ) != program.data
        || memory.readBytes(
            memory_map::kBinaryCodeBase,
            program.code.size()
        ) != program.code
        || memory.readU8(10) != 55
    ) {
        detail =
            "BinaryLoader section placement mismatch";
        return false;
    }

    const Memory before = memory;

    binary::BinaryProgram overlapping =
        makeProgram(
            {1, 2, 3},
            static_cast<std::uint32_t>(
                memory_map::kBinaryCodeBase
            )
        );

    if (
        !throwsRuntimeError(
            [&] {
                (void)loader.loadIntoMemory(
                    overlapping,
                    memory,
                    memory_map::kBinaryCodeBase
                );
            }
        )
    ) {
        detail =
            "overlapping sections were accepted";
        return false;
    }

    if (
        memory.snapshot()
        != before.snapshot()
    ) {
        detail =
            "failed section load modified memory";
        return false;
    }

    return true;
}

bool processImageInitializesData(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    const binary::BinaryProgram program =
        makeProgram(
            {0x11, 0x22, 0x33, 0x44},
            80
        );

    ProcessImageLoader loader;

    const ProcessImage image =
        loader.loadProgram(
            program,
            "data-image.zbin"
        );

    if (
        image.metadata.data_base != 80
        || image.metadata.data_size != 4
        || image.metadata.data_end_exclusive
            != 84
        || image.memory.readBytes(80, 4)
            != program.data
    ) {
        detail =
            "ProcessImage data metadata mismatch";
        return false;
    }

    validateProcessImage(image);
    return true;
}

bool initializedDataIsIsolated(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessImageLoader loader;

    const ProcessImage firstImage =
        loader.loadProgram(
            makeProgram({1, 1, 1, 1}, 96),
            "first-data.zbin"
        );

    const ProcessImage secondImage =
        loader.loadProgram(
            makeProgram({2, 2, 2, 2}, 96),
            "second-data.zbin"
        );

    ProcessTable table;

    const ProcessId first =
        table.createProcess(firstImage);

    const ProcessId second =
        table.createProcess(secondImage);

    CPU cpu;
    RoundRobinScheduler scheduler;
    ProcessDispatcher dispatcher;

    if (
        dispatcher.dispatchNext(
            cpu,
            table,
            scheduler
        ) != first
        || cpu.state().memory().readBytes(96, 4)
            != firstImage.executable.data
    ) {
        detail =
            "first initialized data activation mismatch";
        return false;
    }

    cpu.state().memory().writeU8(96, 9);

    if (
        dispatcher.dispatchNext(
            cpu,
            table,
            scheduler
        ) != second
        || cpu.state().memory().readBytes(96, 4)
            != secondImage.executable.data
    ) {
        detail =
            "second initialized data activation mismatch";
        return false;
    }

    cpu.state().memory().writeU8(96, 8);

    if (
        dispatcher.dispatchNext(
            cpu,
            table,
            scheduler
        ) != first
        || cpu.state().memory().readU8(96) != 9
        || table.process(second)
            .addressSpace().memory().readU8(96)
            != 8
    ) {
        detail =
            "initialized data was not isolated";
        return false;
    }

    return true;
}

bool invalidDataLayoutIsRejected(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessImageLoader loader;

    const binary::BinaryProgram outsideUserData =
        makeProgram(
            {1, 2, 3, 4},
            static_cast<std::uint32_t>(
                memory_map::kUserDataEndExclusive
                - 2
            )
        );

    if (
        !throwsRuntimeError(
            [&] {
                (void)loader.loadProgram(
                    outsideUserData,
                    "outside-user-data.zbin"
                );
            }
        )
    ) {
        detail =
            "data outside User range was accepted";
        return false;
    }

    binary::BinaryProgram mismatch =
        makeProgram({1, 2, 3}, 0);

    ++mismatch.header.data_size;

    if (
        !throwsRuntimeError(
            [&] {
                (void)loader.loadProgram(
                    mismatch,
                    "mismatched-data.zbin"
                );
            }
        )
    ) {
        detail =
            "mismatched data_size was accepted";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Binary Data Section "
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
            "Current format round trip",
            currentFormatRoundTrip(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Legacy format compatibility",
            legacyFormatCompatibility(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Atomic section loading",
            loaderPlacesSectionsAtomically(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "ProcessImage data initialization",
            processImageInitializesData(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Initialized data isolation",
            initializedDataIsIsolated(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Invalid data layout rejection",
            invalidDataLayoutIsRejected(
                detail
            ),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Binary data section test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Binary data section test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
