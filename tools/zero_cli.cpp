#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/Opcode.hpp"
#include "zero_cpu/isa/Operand.hpp"

#include <iostream>
#include <vector>

namespace {

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
    std::cout << "Memory[96..103]: "
              << cpu.state().memory().dumpRange(96, 8)
              << "\n";

    std::cout << "Stack[252..263]: "
              << cpu.state().memory().dumpRange(252, 12)
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

int main() {
    using namespace zero_cpu;

    std::vector<Instruction> program = {
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(10)
        ),

        Instruction(
            Opcode::CALL,
            Operand::label("double_value")
        ),

        Instruction(
            Opcode::STORE,
            Operand::memoryAddress(100),
            Operand::registerOperand(RegisterName::R1)
        ),

        Instruction(
            Opcode::PUSH,
            Operand::registerOperand(RegisterName::R1)
        ),

        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(0)
        ),

        Instruction(
            Opcode::POP,
            Operand::registerOperand(RegisterName::R2)
        ),

        Instruction(Opcode::HALT),

        Instruction(
            Opcode::ADD,
            Operand::registerOperand(RegisterName::R1),
            Operand::registerOperand(RegisterName::R1)
        ),

        Instruction(Opcode::RET)
    };

    CPU::LabelTable labels = {
        {"double_value", 7}
    };

    CPU cpu;
    cpu.loadProgram(program, labels);

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
                  << "\n\n";

        return 1;
    }

    std::cout << "=== Compact Trace Log ===\n";
    std::cout << cpu.traceLogger().compactString() << "\n";

    std::cout << "=== Full Trace Log ===\n";
    std::cout << cpu.traceLogger().fullString() << "\n";

    const auto finalR1 = cpu.state().registers().get(RegisterName::R1);
    const auto finalR2 = cpu.state().registers().get(RegisterName::R2);

    std::cout << "Final Check:\n";
    std::cout << "R1 = " << finalR1 << "\n";
    std::cout << "R2 = " << finalR2 << "\n";
    std::cout << "SP = " << cpu.state().sp() << "\n";
    std::cout << "Memory[100] = "
              << cpu.state().memory().read(100)
              << "\n";
    std::cout << "Memory[256] = "
              << cpu.state().memory().read(256)
              << "\n";

    std::cout << "\nExecution finished successfully.\n";

    return 0;
}