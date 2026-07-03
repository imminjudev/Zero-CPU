#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/binary/BinaryReader.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/Instruction.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kDataViewStart = 96;
constexpr std::size_t kDataViewCount = 16;

constexpr std::size_t kStackViewStart = 2048;
constexpr std::size_t kStackViewCount = 32;

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

int runBinaryTest(const std::string& outputPath) {
    using namespace zero_cpu::binary;

    BinaryProgram program;

    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;

    program.code.resize(kInstructionSize * 2, 0);

    // Instruction 0: NOP
    program.code[0] = 0x00;

    // Instruction 1: HALT
    program.code[kInstructionSize] = 0x01;

    BinaryWriter writer;
    writer.writeFile(outputPath, program);

    std::cout << "Wrote binary file: " << outputPath << "\n";

    BinaryReader reader;
    BinaryProgram loaded = reader.readFile(outputPath);

    std::cout << "Read binary file successfully.\n\n";

    std::cout << "=== Binary Header ===\n";
    std::cout << "Magic: " << magicString() << "\n";
    std::cout << "Version: "
              << static_cast<int>(loaded.header.major_version)
              << "."
              << static_cast<int>(loaded.header.minor_version)
              << "\n";

    std::cout << "Endianness: "
              << (
                     loaded.header.endianness == BinaryEndianness::Little
                         ? "Little"
                         : "Big"
                 )
              << "\n";

    std::cout << "Entry Point: "
              << loaded.header.entry_point
              << "\n";

    std::cout << "Code Size: "
              << loaded.header.code_size
              << " bytes\n";

    std::cout << "Instruction Count: "
              << loaded.code.size() / kInstructionSize
              << "\n\n";

    std::cout << "=== Code Bytes ===\n";
    printHexBytes(loaded.code);

    std::cout << "\nBinary writer/reader test finished successfully.\n";

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

    const auto finalR1 =
        cpu.state().registers().get(RegisterName::R1);

    const auto finalR2 =
        cpu.state().registers().get(RegisterName::R2);

    std::cout << "Final Check:\n";
    std::cout << "R1 = " << finalR1 << "\n";
    std::cout << "R2 = " << finalR2 << "\n";
    std::cout << "SP = " << cpu.state().sp() << "\n";

    std::cout << "Memory[100] = "
              << cpu.state().memory().read(100)
              << "\n";

    std::cout << "Memory[2048] = "
              << cpu.state().memory().read(2048)
              << "\n";

    std::cout << "\nExecution finished successfully.\n";

    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc >= 2 && std::string(argv[1]) == "binary-test") {
            const std::string outputPath =
                argc >= 3
                    ? argv[2]
                    : "examples/binary_test.zbin";

            return runBinaryTest(outputPath);
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