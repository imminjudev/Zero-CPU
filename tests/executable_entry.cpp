#include "zero_cpu/assembler/Assembler.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryReader.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"

#include <cstdio>
#include <cstddef>
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

bool explicitEntryResolvesForwardLabel(
    std::string& detail
) {
    using namespace zero_cpu;

    const std::string source =
        ".entry start\n"
        ".data\n"
        "value: .qword 7\n"
        ".text\n"
        "prefix:\n"
        "    HALT\n"
        "start:\n"
        "    LOAD R1, [value]\n"
        "    HALT\n";

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(source);

    if (
        !assembled.has_explicit_entry
        || assembled.entry_label != "start"
        || assembled.entry_instruction != 1
        || assembled.resolvedEntryInstruction() != 1
    ) {
        detail =
            "explicit entry metadata mismatch";
        return false;
    }

    const binary::BinaryProgram program =
        assembled.toBinaryProgram();

    if (
        program.header.entry_point
            != binary::kInstructionSize
        || program.header.data_base
            != memory_map::kUserDataBase
        || program.header.data_size != 8
    ) {
        detail =
            "explicit entry binary header mismatch";
        return false;
    }

    CPU cpu;
    cpu.loadBinaryProgram(program);
    cpu.run();

    if (
        cpu.state().hasError()
        || !cpu.state().halted()
        || cpu.state().registers().get(
            RegisterName::R1
        ) != 7
    ) {
        detail =
            "binary did not start at explicit entry";
        return false;
    }

    return true;
}

bool defaultEntryRemainsFirstInstruction(
    std::string& detail
) {
    using namespace zero_cpu;

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(
            ".text\n"
            "first:\n"
            "    MOV R0, 11\n"
            "    HALT\n"
        );

    const binary::BinaryProgram program =
        assembled.toBinaryProgram();

    if (
        assembled.has_explicit_entry
        || !assembled.entry_label.empty()
        || assembled.entry_instruction != 0
        || assembled.resolvedEntryInstruction() != 0
        || program.header.entry_point != 0
    ) {
        detail =
            "default first-instruction entry mismatch";
        return false;
    }

    return true;
}

bool explicitOverrideRemainsAvailable(
    std::string& detail
) {
    using namespace zero_cpu;

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(
            ".entry second\n"
            ".text\n"
            "first: NOP\n"
            "second: HALT\n"
        );

    const binary::BinaryProgram declared =
        assembled.toBinaryProgram();

    const binary::BinaryProgram overridden =
        assembled.toBinaryProgram(0);

    if (
        declared.header.entry_point
            != binary::kInstructionSize
        || overridden.header.entry_point != 0
    ) {
        detail =
            "explicit entry override mismatch";
        return false;
    }

    return true;
}

bool binaryFileRoundTripPreservesEntry(
    std::string& detail
) {
    using namespace zero_cpu;

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(
            ".entry start\n"
            ".data\n"
            "message: .string \"OK\\n\"\n"
            ".text\n"
            "skip: NOP\n"
            "start: HALT\n"
        );

    const binary::BinaryProgram program =
        assembled.toBinaryProgram();

    const std::string path =
        "zero_executable_entry_test.zbin";

    struct Cleanup {
        std::string path;

        ~Cleanup() {
            std::remove(path.c_str());
        }
    } cleanup{path};

    binary::BinaryWriter writer;
    writer.writeFile(path, program);

    binary::BinaryReader reader;

    const binary::BinaryProgram verified =
        reader.readFile(path);

    if (
        verified.header.entry_point
            != binary::kInstructionSize
        || verified.header.code_size
            != program.header.code_size
        || verified.header.data_base
            != program.header.data_base
        || verified.header.data_size
            != program.header.data_size
        || verified.code != program.code
        || verified.data != program.data
    ) {
        detail =
            "verified executable entry round trip mismatch";
        return false;
    }

    return true;
}

bool invalidEntryDirectivesAreRejected(
    std::string& detail
) {
    using namespace zero_cpu;

    Assembler assembler;

    const std::vector<std::string> invalid = {
        ".entry\n.text\nHALT\n",
        ".entry start extra\n.text\nstart: HALT\n",
        ".entry missing\n.text\nHALT\n",
        ".entry start\n.entry other\n"
        ".text\nstart: HALT\nother: HALT\n",
        ".data\nvalue: .qword 1\n"
        ".entry value\n.text\nHALT\n",
        ".text\nlabel: .entry start\nstart: HALT\n",
        ".entry R0\n.text\nHALT\n",
        ".entry 123\n.text\nHALT\n"
    };

    for (
        std::size_t index = 0;
        index < invalid.size();
        ++index
    ) {
        if (
            !throwsRuntimeError(
                [&] {
                    (void)assembler.assembleString(
                        invalid[index]
                    );
                }
            )
        ) {
            detail =
                "invalid entry directive accepted at case "
                + std::to_string(index);
            return false;
        }
    }

    return true;
}

bool directAssemblyExecutionCanUseEntry(
    std::string& detail
) {
    using namespace zero_cpu;

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(
            ".entry start\n"
            ".text\n"
            "prefix: HALT\n"
            "start: MOV R3, 333\n"
            "HALT\n"
        );

    CPU cpu;

    cpu.loadProgram(
        assembled.instructions,
        assembled.labels
    );

    cpu.state().setPc(
        assembled.resolvedEntryInstruction()
    );

    cpu.run();

    if (
        cpu.state().hasError()
        || !cpu.state().halted()
        || cpu.state().registers().get(
            RegisterName::R3
        ) != 333
    ) {
        detail =
            "direct assembly execution ignored entry";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Executable Entry "
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
            "Explicit entry resolution",
            explicitEntryResolvesForwardLabel(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Default entry compatibility",
            defaultEntryRemainsFirstInstruction(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Entry override compatibility",
            explicitOverrideRemainsAvailable(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Binary entry round trip",
            binaryFileRoundTripPreservesEntry(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Invalid entry rejection",
            invalidEntryDirectivesAreRejected(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Direct assembly entry",
            directAssemblyExecutionCanUseEntry(
                detail
            ),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Executable entry test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Executable entry test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
