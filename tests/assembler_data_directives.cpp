#include "zero_cpu/assembler/Assembler.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryReader.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/EncodedInstruction.hpp"
#include "zero_cpu/isa/InstructionDecoder.hpp"

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

const char* kSource = R"ASM(
.data
counter: .qword 41
message: .string "Hi;\n" ; comment outside string
flags: .byte 1, -1, 255
values: .word 4660, -2

.text
start:
    LOAD R0, [counter]
    ADD R0, 1
    STORE [counter], R0
    HALT
)ASM";

bool directivesAndSymbols(
    std::string& detail
) {
    using namespace zero_cpu;

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(kSource);

    const std::vector<std::uint8_t>
        expectedData = {
            41, 0, 0, 0, 0, 0, 0, 0,
            'H', 'i', ';', '\n',
            1, 255, 255,
            0x34, 0x12,
            0xFE, 0xFF
        };

    if (
        assembled.instructions.size() != 4
        || assembled.labels.at("start") != 0
        || assembled.data_base
            != memory_map::kUserDataBase
        || assembled.data_labels.at("counter")
            != memory_map::kUserDataBase
        || assembled.data_labels.at("message")
            != memory_map::kUserDataBase + 8
        || assembled.data_labels.at("flags")
            != memory_map::kUserDataBase + 12
        || assembled.data_labels.at("values")
            != memory_map::kUserDataBase + 15
        || assembled.data != expectedData
    ) {
        detail =
            "directive bytes or symbol addresses mismatch";
        return false;
    }

    return true;
}

bool dataLabelsEncodeAsMemoryAddresses(
    std::string& detail
) {
    using namespace zero_cpu;

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(kSource);

    const binary::BinaryProgram program =
        assembled.toBinaryProgram();

    InstructionDecoder decoder;

    const std::vector<std::uint8_t> loadBytes(
        program.code.begin(),
        program.code.begin()
            + binary::kInstructionSize
    );

    const std::size_t storeOffset =
        2 * binary::kInstructionSize;

    const std::vector<std::uint8_t> storeBytes(
        program.code.begin()
            + static_cast<std::ptrdiff_t>(
                storeOffset
            ),
        program.code.begin()
            + static_cast<std::ptrdiff_t>(
                storeOffset
                + binary::kInstructionSize
            )
    );

    const DecodedInstruction load =
        decoder.decodeInstruction(loadBytes);

    const DecodedInstruction store =
        decoder.decodeInstruction(storeBytes);

    if (
        load.opcode != Opcode::LOAD
        || load.src_type
            != EncodedOperandType::MemoryAddress
        || load.src_payload
            != static_cast<std::int64_t>(
                memory_map::kUserDataBase
            )
        || store.opcode != Opcode::STORE
        || store.dst_type
            != EncodedOperandType::MemoryAddress
        || store.dst_payload
            != static_cast<std::int64_t>(
                memory_map::kUserDataBase
            )
    ) {
        detail =
            "data label did not encode as memory address";
        return false;
    }

    return true;
}

bool binaryEmissionAndExecution(
    std::string& detail
) {
    using namespace zero_cpu;

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(kSource);

    const binary::BinaryProgram program =
        assembled.toBinaryProgram();

    if (
        program.header.major_version
            != binary::kMajorVersion
        || program.header.minor_version
            != binary::kMinorVersion
        || program.header.data_base
            != memory_map::kUserDataBase
        || program.header.data_size
            != assembled.data.size()
        || program.data != assembled.data
    ) {
        detail =
            "assembler binary metadata mismatch";
        return false;
    }

    binary::BinaryWriter writer;
    binary::BinaryReader reader;

    const binary::BinaryProgram decoded =
        reader.readFromBytes(
            writer.writeToBytes(program)
        );

    if (
        decoded.code != program.code
        || decoded.data != program.data
        || decoded.header.data_base
            != program.header.data_base
        || decoded.header.data_size
            != program.header.data_size
    ) {
        detail =
            "assembled v0.3 binary round trip mismatch";
        return false;
    }

    CPU cpu;
    cpu.loadBinaryProgram(decoded);
    cpu.run();

    if (
        cpu.state().hasError()
        || !cpu.state().halted()
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 42
        || cpu.state().memory().readI64(
            memory_map::kUserDataBase
        ) != 42
    ) {
        detail =
            "assembled data program execution mismatch";
        return false;
    }

    return true;
}

bool sectionCompatibility(
    std::string& detail
) {
    using namespace zero_cpu;

    Assembler assembler;

    const AssembledProgram legacy =
        assembler.assembleString(
            "start:\n"
            "    MOV R0, 7\n"
            "    HALT\n"
        );

    if (
        legacy.instructions.size() != 2
        || legacy.labels.at("start") != 0
        || !legacy.data.empty()
        || !legacy.data_labels.empty()
    ) {
        detail =
            "sectionless source compatibility mismatch";
        return false;
    }

    const AssembledProgram alternating =
        assembler.assembleString(
            ".data\n"
            "x: .qword 1\n"
            ".text\n"
            "start: NOP\n"
            ".data\n"
            "y: .byte 2\n"
            ".text\n"
            "HALT\n"
        );

    if (
        alternating.instructions.size() != 2
        || alternating.data.size() != 9
        || alternating.data_labels.at("y")
            != memory_map::kUserDataBase + 8
    ) {
        detail =
            "section switching mismatch";
        return false;
    }

    return true;
}

bool invalidSourceIsRejected(
    std::string& detail
) {
    using namespace zero_cpu;

    Assembler assembler;

    const std::vector<std::string> invalid = {
        ".data\nx: .byte 256\n.text\nHALT\n",
        ".data\nx: .word 65536\n.text\nHALT\n",
        ".data\nx: .string \"bad\\q\"\n.text\nHALT\n",
        ".data\nx: .string \"unterminated\n.text\nHALT\n",
        ".data\nx: .qword 1\n.text\nx: HALT\n",
        ".data\nMOV R0, 1\n.text\nHALT\n",
        ".text\n.byte 1\nHALT\n",
        ".data\nx: .qword 1\n.text\nMOV R0, x\nHALT\n",
        ".text\nLOAD R0, [missing]\nHALT\n",
        "label: .data\n.text\nHALT\n"
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
                "invalid source was accepted at case "
                + std::to_string(index);
            return false;
        }
    }

    std::string oversized =
        ".data\ntext: .string \"";

    oversized.append(
        memory_map::kUserDataSize + 1,
        'A'
    );

    oversized += "\"\n.text\nHALT\n";

    if (
        !throwsRuntimeError(
            [&] {
                (void)assembler.assembleString(
                    oversized
                );
            }
        )
    ) {
        detail =
            "oversized initialized data was accepted";
        return false;
    }

    return true;
}

bool emptyTextCannotEmitBinary(
    std::string& detail
) {
    using namespace zero_cpu;

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(
            ".data\n"
            "value: .qword 1\n"
        );

    if (
        !throwsRuntimeError(
            [&] {
                (void)assembled.toBinaryProgram();
            }
        )
    ) {
        detail =
            "data-only source emitted executable";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Assembler Data "
           "Directives Test ===\n\n";

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
            "Directives and symbols",
            directivesAndSymbols(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Data-label memory encoding",
            dataLabelsEncodeAsMemoryAddresses(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Binary emission and execution",
            binaryEmissionAndExecution(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Section compatibility",
            sectionCompatibility(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Invalid source rejection",
            invalidSourceIsRejected(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Empty text emission guard",
            emptyTextCannotEmitBinary(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Assembler data directives test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Assembler data directives test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
