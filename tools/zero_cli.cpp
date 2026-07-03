#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/Instruction.hpp"

#include <exception>
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

} // namespace

int main(int argc, char* argv[]) {
    using namespace zero_cpu;

    const std::string inputPath =
        argc >= 2
            ? argv[1]
            : "examples/function_call.zasm";

    try {
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
    } catch (const std::exception& ex) {
        std::cerr << "Assembler or execution error: "
                  << ex.what()
                  << "\n";

        return 1;
    }
}