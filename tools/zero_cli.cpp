#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryLoader.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/binary/BinaryReader.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/Memory.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/EncodedInstruction.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/InstructionDecoder.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kDataViewStart = 96;
constexpr std::size_t kDataViewCount = 16;

constexpr std::size_t kStackViewStart = 2048;
constexpr std::size_t kStackViewCount = 32;

constexpr std::size_t kLoadedMemoryPreviewCount = 96;

void printProgram(
    const std::vector<zero_cpu::Instruction>& program,
    const zero_cpu::CPU::LabelTable& labels
) {
    std::cout << "=== Zero-CPU Program ===\n";

    for (std::size_t i = 0; i < program.size(); ++i) {
        for (const auto& entry : labels) {
            if (entry.second == i) {
                std::cout << entry.first << ":\n";
            }
        }

        std::cout << "[" << i << "] "
                  << program[i].toString()
                  << "\n";
    }

    std::cout << "\n";
}

void printMemoryViews(const zero_cpu::CPU& cpu) {
    std::cout << "Memory[96..111]: "
              << cpu.state().memory().dumpRange(
                     kDataViewStart,
                     kDataViewCount
                 )
              << "\n";

    std::cout << "Stack[2048..2079]: "
              << cpu.state().memory().dumpRange(
                     kStackViewStart,
                     kStackViewCount
                 )
              << "\n";
}

void printWatchedMemory(
    const zero_cpu::CPU& cpu,
    const std::vector<std::size_t>& watchAddresses
) {
    if (watchAddresses.empty()) {
        return;
    }

    std::cout << "Watched Memory:\n";

    for (const std::size_t address : watchAddresses) {
        std::cout << "Memory["
                  << address
                  << "] = "
                  << cpu.state().memory().read(address)
                  << "\n";
    }
}

std::size_t parseMemoryAddress(const std::string& text) {
    std::size_t parsedLength = 0;

    const unsigned long long value = std::stoull(
        text,
        &parsedLength,
        0
    );

    if (parsedLength != text.size()) {
        throw std::invalid_argument(
            "Invalid memory address: " + text
        );
    }

    if (value > static_cast<unsigned long long>(
                    std::numeric_limits<std::size_t>::max()
                )) {
        throw std::out_of_range(
            "Memory address is too large: " + text
        );
    }

    return static_cast<std::size_t>(value);
}

std::vector<std::size_t> parseWatchAddresses(
    int argc,
    char* argv[],
    int startIndex
) {
    std::vector<std::size_t> watchAddresses;

    if (startIndex >= argc) {
        return watchAddresses;
    }

    const std::string option = argv[startIndex];

    if (option != "--watch") {
        throw std::invalid_argument(
            "Unknown run-binary option: " + option
        );
    }

    if (startIndex + 1 >= argc) {
        throw std::invalid_argument(
            "--watch requires at least one memory address"
        );
    }

    for (int i = startIndex + 1; i < argc; ++i) {
        watchAddresses.push_back(parseMemoryAddress(argv[i]));
    }

    return watchAddresses;
}

void printFinalCheck(const zero_cpu::CPU& cpu) {
    using namespace zero_cpu;

    const auto finalR1 =
        cpu.state().registers().get(RegisterName::R1);

    const auto finalR2 =
        cpu.state().registers().get(RegisterName::R2);

    std::cout << "Default Final Check (function_call example):\n";
    std::cout << "R1 = " << finalR1 << "\n";
    std::cout << "R2 = " << finalR2 << "\n";
    std::cout << "SP = " << cpu.state().sp() << "\n";

    std::cout << "Memory[100] = "
              << cpu.state().memory().read(100)
              << "\n";

    std::cout << "Memory[2048] = "
              << cpu.state().memory().read(2048)
              << "\n";
}

void runStepByStep(zero_cpu::CPU& cpu) {
    std::cout << "=== Step Execution With Trace ===\n";

    std::size_t stepCount = 0;

    while (!cpu.state().halted()) {
        const std::size_t pcBefore = cpu.state().pc();

        if (pcBefore >= cpu.program().size()) {
            std::cout << "PC out of program range.\n";
            break;
        }

        const zero_cpu::Instruction& instruction = cpu.program()[pcBefore];

        std::cout << "Step " << stepCount
                  << " | PC=" << pcBefore
                  << " | " << instruction.toString()
                  << "\n";

        cpu.step();

        if (!cpu.traceLogger().empty()) {
            std::cout << cpu.traceLogger().last().toCompactString()
                      << "\n";
        }

        std::cout << "Current State:\n";
        std::cout << cpu.state().summary();
        printMemoryViews(cpu);
        std::cout << "\n";

        ++stepCount;

        if (stepCount > 100) {
            std::cout << "Step limit reached in CLI test.\n";
            break;
        }
    }
}

void printHexBytes(const std::vector<std::uint8_t>& bytes) {
    std::cout << std::hex << std::setfill('0');

    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0) {
            std::cout << " ";
        }

        std::cout << std::setw(2)
                  << static_cast<int>(bytes[i]);
    }

    std::cout << std::dec << "\n";
}

void printBinaryHeader(const zero_cpu::binary::BinaryProgram& program) {
    using namespace zero_cpu::binary;

    std::cout << "=== Binary Header ===\n";
    std::cout << "Magic: " << magicString() << "\n";

    std::cout << "Version: "
              << static_cast<int>(program.header.major_version)
              << "."
              << static_cast<int>(program.header.minor_version)
              << "\n";

    std::cout << "Endianness: "
              << (
                     program.header.endianness == BinaryEndianness::Little
                         ? "Little"
                         : "Big"
                 )
              << "\n";

    std::cout << "Entry Point: "
              << program.header.entry_point
              << "\n";

    std::cout << "Code Size: "
              << program.header.code_size
              << " bytes\n";

    std::cout << "Instruction Count: "
              << program.code.size() / kInstructionSize
              << "\n";
}

void printDecodedInstruction(
    std::size_t index,
    const zero_cpu::DecodedInstruction& instruction
) {
    std::cout << "[" << index << "] ";

    std::cout << "opcode=0x"
              << std::hex
              << std::setw(2)
              << std::setfill('0')
              << static_cast<int>(zero_cpu::encodeOpcode(instruction.opcode))
              << std::dec;

    std::cout << " | dst_type="
              << zero_cpu::toString(instruction.dst_type)
              << " | dst_payload="
              << instruction.dst_payload;

    std::cout << " | src_type="
              << zero_cpu::toString(instruction.src_type)
              << " | src_payload="
              << instruction.src_payload;

    std::cout << "\n";
}

std::string decodedInstructionToString(
    const zero_cpu::DecodedInstruction& instruction
) {
    std::ostringstream oss;

    oss << "opcode=0x"
        << std::hex
        << std::setw(2)
        << std::setfill('0')
        << static_cast<int>(zero_cpu::encodeOpcode(instruction.opcode))
        << std::dec
        << std::setfill(' ');

    oss << " | dst_type="
        << zero_cpu::toString(instruction.dst_type)
        << " | dst_payload="
        << instruction.dst_payload;

    oss << " | src_type="
        << zero_cpu::toString(instruction.src_type)
        << " | src_payload="
        << instruction.src_payload;

    return oss.str();
}

std::string currentBinaryInstructionText(const zero_cpu::CPU& cpu) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    try {
        const std::size_t pc = cpu.state().pc();

        const std::vector<std::uint8_t> instructionBytes =
            cpu.state().memory().readBytes(pc, kInstructionSize);

        InstructionDecoder decoder;
        const DecodedInstruction decoded =
            decoder.decodeInstruction(instructionBytes);

        return decodedInstructionToString(decoded);
    } catch (const std::exception& ex) {
        return std::string("<decode failed: ") + ex.what() + ">";
    }
}

void printDecodedInstructions(const std::vector<std::uint8_t>& code) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::cout << "=== Decoded Instructions ===\n";

    InstructionDecoder decoder;

    for (
        std::size_t offset = 0;
        offset < code.size();
        offset += kInstructionSize
    ) {
        std::vector<std::uint8_t> instructionBytes(
            code.begin() + offset,
            code.begin() + offset + kInstructionSize
        );

        const DecodedInstruction decoded =
            decoder.decodeInstruction(instructionBytes);

        const std::size_t index = offset / kInstructionSize;
        printDecodedInstruction(index, decoded);
    }
}

int runBinaryTest(const std::string& outputPath) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    std::vector<Instruction> instructions;
    instructions.emplace_back(Opcode::NOP);
    instructions.emplace_back(Opcode::HALT);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> encodedCode =
        encoder.encodeProgram(instructions, {});

    BinaryProgram program;

    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size =
        static_cast<std::uint32_t>(encodedCode.size());
    program.code = std::move(encodedCode);

    BinaryWriter writer;
    writer.writeFile(outputPath, program);

    std::cout << "Wrote binary file: " << outputPath << "\n";

    BinaryReader reader;
    BinaryProgram loaded = reader.readFile(outputPath);

    std::cout << "Read binary file successfully.\n\n";

    printBinaryHeader(loaded);
    std::cout << "\n";

    std::cout << "=== Code Bytes ===\n";
    printHexBytes(loaded.code);
    std::cout << "\n";

    printDecodedInstructions(loaded.code);
    std::cout << "\n";

    std::cout << "Binary encoder/writer/reader/decoder test finished successfully.\n";

    return 0;
}

int dumpBinaryFile(const std::string& inputPath) {
    using namespace zero_cpu::binary;

    BinaryReader reader;
    BinaryProgram loaded = reader.readFile(inputPath);

    std::cout << "Input binary file: " << inputPath << "\n\n";

    printBinaryHeader(loaded);
    std::cout << "\n";

    std::cout << "=== Code Bytes ===\n";
    printHexBytes(loaded.code);
    std::cout << "\n";

    printDecodedInstructions(loaded.code);
    std::cout << "\n";

    std::cout << "Binary dump finished successfully.\n";

    return 0;
}

int loadBinaryFile(const std::string& inputPath) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    BinaryReader reader;
    BinaryProgram program = reader.readFile(inputPath);

    Memory memory;

    BinaryLoader loader;
    LoadedBinaryImage image = loader.loadIntoMemory(program, memory);

    std::cout << "Input binary file: " << inputPath << "\n\n";

    printBinaryHeader(program);
    std::cout << "\n";

    std::cout << "=== Loaded Binary Image ===\n";
    std::cout << "Code Base: "
              << image.code_base
              << "\n";

    std::cout << "Entry Point: "
              << image.entry_point
              << "\n";

    std::cout << "Code Size: "
              << image.code_size
              << " bytes\n\n";

    const std::size_t previewCount =
        std::min(image.code_size, kLoadedMemoryPreviewCount);

    std::cout << "=== Memory Preview ===\n";
    std::cout << "Memory[0.."
              << (previewCount == 0 ? 0 : previewCount - 1)
              << "]: "
              << memory.dumpRange(0, previewCount)
              << "\n\n";

    printDecodedInstructions(program.code);
    std::cout << "\n";

    std::cout << "Binary load test finished successfully.\n";

    return 0;
}

int cpuLoadBinaryFile(const std::string& inputPath) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    BinaryReader reader;
    BinaryProgram program = reader.readFile(inputPath);

    CPU cpu;
    cpu.loadBinaryProgram(program);

    std::cout << "Input binary file: " << inputPath << "\n\n";

    printBinaryHeader(program);
    std::cout << "\n";

    std::cout << "=== CPU Binary Load State ===\n";
    std::cout << "Has Binary Program: "
              << (cpu.hasBinaryProgram() ? "true" : "false")
              << "\n";

    std::cout << "Code Base: "
              << cpu.binaryCodeBase()
              << "\n";

    std::cout << "Entry Point: "
              << cpu.binaryEntryPoint()
              << "\n";

    std::cout << "Code Size: "
              << cpu.binaryCodeSize()
              << " bytes\n";

    std::cout << "Current PC: "
              << cpu.state().pc()
              << "\n\n";

    std::cout << "=== CPU State ===\n";
    std::cout << cpu.state().summary();

    const std::size_t previewCount =
        std::min(cpu.binaryCodeSize(), kLoadedMemoryPreviewCount);

    std::cout << "=== CPU Memory Preview ===\n";
    std::cout << "Memory["
              << cpu.binaryCodeBase()
              << ".."
              << (previewCount == 0
                      ? cpu.binaryCodeBase()
                      : cpu.binaryCodeBase() + previewCount - 1)
              << "]: "
              << cpu.state().memory().dumpRange(
                     cpu.binaryCodeBase(),
                     previewCount
                 )
              << "\n\n";

    printDecodedInstructions(program.code);
    std::cout << "\n";

    std::cout << "CPU binary load test finished successfully.\n";

    return 0;
}

int runBinaryFile(
    const std::string& inputPath,
    const std::vector<std::size_t>& watchAddresses
) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    BinaryReader reader;
    BinaryProgram program = reader.readFile(inputPath);

    CPU cpu;
    cpu.loadBinaryProgram(program);

    std::cout << "Input binary file: " << inputPath << "\n\n";

    printBinaryHeader(program);
    std::cout << "\n";

    if (!watchAddresses.empty()) {
        std::cout << "Memory Watch Addresses:";

        for (const std::size_t address : watchAddresses) {
            std::cout << " " << address;
        }

        std::cout << "\n\n";
    }

    std::cout << "=== Binary Execution ===\n";

    std::size_t stepCount = 0;

    while (!cpu.state().halted()) {
        std::cout << "Step " << stepCount
                  << " | PC=" << cpu.state().pc()
                  << " | "
                  << currentBinaryInstructionText(cpu)
                  << "\n";

        cpu.step();

        std::cout << cpu.state().summary()
                  << "\n";

        if (cpu.state().hasError()) {
            std::cout << "Execution failed: "
                      << cpu.state().errorMessage()
                      << "\n\n";

            printWatchedMemory(cpu, watchAddresses);
            if (!watchAddresses.empty()) {
                std::cout << "\n";
            }

            printFinalCheck(cpu);
            return 1;
        }

        ++stepCount;

        if (stepCount > 1000) {
            std::cout << "Step limit reached in binary execution.\n\n";

            printWatchedMemory(cpu, watchAddresses);
            if (!watchAddresses.empty()) {
                std::cout << "\n";
            }

            printFinalCheck(cpu);
            return 1;
        }
    }

    std::cout << "=== Binary Final CPU State ===\n";
    std::cout << cpu.state().summary();
    printMemoryViews(cpu);
    std::cout << "\n";

    printWatchedMemory(cpu, watchAddresses);
    if (!watchAddresses.empty()) {
        std::cout << "\n";
    }

    printFinalCheck(cpu);
    std::cout << "\n";

    std::cout << "Binary execution finished successfully.\n";

    return 0;
}

int assembleToBinary(
    const std::string& inputPath,
    const std::string& outputPath
) {
    using namespace zero_cpu;

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(inputPath);

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code = encoder.encodeProgram(
        assembled.instructions,
        assembled.labels
    );

    binary::BinaryProgram program;

    program.header.major_version = binary::kMajorVersion;
    program.header.minor_version = binary::kMinorVersion;
    program.header.endianness = binary::BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size = static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);

    binary::BinaryWriter writer;
    writer.writeFile(outputPath, program);

    binary::BinaryReader reader;
    const binary::BinaryProgram verified = reader.readFile(outputPath);

    std::cout << "Assemble completed successfully.\n";
    std::cout << "Input: " << inputPath << "\n";
    std::cout << "Output: " << outputPath << "\n";
    std::cout << "Instruction count: "
              << assembled.instructions.size()
              << "\n";
    std::cout << "Code size: "
              << verified.header.code_size
              << " bytes\n";
    std::cout << "Entry point: "
              << verified.header.entry_point
              << "\n";

    return 0;
}

int runAssemblyProgram(const std::string& inputPath) {
    using namespace zero_cpu;

    Assembler assembler;
    AssembledProgram assembled = assembler.assembleFile(inputPath);

    CPU cpu;
    cpu.loadProgram(assembled.instructions, assembled.labels);

    std::cout << "Input file: " << inputPath << "\n\n";

    printProgram(cpu.program(), cpu.labels());

    std::cout << "=== Initial CPU State ===\n";
    std::cout << cpu.state().summary();
    printMemoryViews(cpu);
    std::cout << "\n";

    runStepByStep(cpu);

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary();
    printMemoryViews(cpu);
    std::cout << "\n";

    if (cpu.state().hasError()) {
        std::cout << "Execution failed: "
                  << cpu.state().errorMessage()
                  << "\n";

        return 1;
    }

    std::cout << "=== Compact Trace Log ===\n";
    std::cout << cpu.traceLogger().compactString() << "\n";

    printFinalCheck(cpu);

    std::cout << "\nExecution finished successfully.\n";

    return 0;
}

void printUsage() {
    std::cout << "Zero-CPU CLI\n\n";
    std::cout << "Usage:\n";
    std::cout << "  zero_cli\n";
    std::cout << "  zero_cli <input.zasm>\n";
    std::cout << "  zero_cli binary-test [output.zbin]\n";
    std::cout << "  zero_cli assemble <input.zasm> <output.zbin>\n";
    std::cout << "  zero_cli dump-binary <input.zbin>\n";
    std::cout << "  zero_cli load-binary <input.zbin>\n";
    std::cout << "  zero_cli cpu-load-binary <input.zbin>\n";
    std::cout << "  zero_cli run-binary <input.zbin> [--watch <addr>...]\n";
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc >= 2) {
            const std::string command = argv[1];

            if (command == "help" || command == "--help" || command == "-h") {
                printUsage();
                return 0;
            }

            if (command == "binary-test") {
                const std::string outputPath =
                    argc >= 3
                        ? argv[2]
                        : "examples/binary_test.zbin";

                return runBinaryTest(outputPath);
            }

            if (command == "assemble") {
                if (argc != 4) {
                    std::cerr << "Invalid assemble command.\n\n";
                    printUsage();
                    return 1;
                }

                return assembleToBinary(argv[2], argv[3]);
            }

            if (command == "dump-binary") {
                if (argc != 3) {
                    std::cerr << "Invalid dump-binary command.\n\n";
                    printUsage();
                    return 1;
                }

                return dumpBinaryFile(argv[2]);
            }

            if (command == "load-binary") {
                if (argc != 3) {
                    std::cerr << "Invalid load-binary command.\n\n";
                    printUsage();
                    return 1;
                }

                return loadBinaryFile(argv[2]);
            }

            if (command == "cpu-load-binary") {
                if (argc != 3) {
                    std::cerr << "Invalid cpu-load-binary command.\n\n";
                    printUsage();
                    return 1;
                }

                return cpuLoadBinaryFile(argv[2]);
            }

            if (command == "run-binary") {
                if (argc < 3) {
                    std::cerr << "Invalid run-binary command.\n\n";
                    printUsage();
                    return 1;
                }

                const std::vector<std::size_t> watchAddresses =
                    parseWatchAddresses(argc, argv, 3);

                return runBinaryFile(argv[2], watchAddresses);
            }
        }

        const std::string inputPath =
            argc >= 2
                ? argv[1]
                : "examples/function_call.zasm";

        return runAssemblyProgram(inputPath);
    } catch (const std::exception& ex) {
        std::cerr << "Error: "
                  << ex.what()
                  << "\n";

        return 1;
    }
}
