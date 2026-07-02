#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/Opcode.hpp"
#include "zero_cpu/isa/Operand.hpp"

#include <iostream>
#include <vector>

int main() {
    using namespace zero_cpu;

    std::vector<Instruction> program = {
        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(10)
        ),

        Instruction(
            Opcode::MOV,
            Operand::registerOperand(RegisterName::R2),
            Operand::immediate(20)
        ),

        Instruction(
            Opcode::ADD,
            Operand::registerOperand(RegisterName::R1),
            Operand::registerOperand(RegisterName::R2)
        ),

        Instruction(
            Opcode::SUB,
            Operand::registerOperand(RegisterName::R1),
            Operand::immediate(5)
        ),

        Instruction(Opcode::HALT)
    };

    CPU cpu;
    cpu.loadProgram(program);

    std::cout << "=== Zero-CPU Program ===\n";

    const auto& loadedProgram = cpu.program();

    for (std::size_t i = 0; i < loadedProgram.size(); ++i) {
        std::cout << "[" << i << "] "
                  << loadedProgram[i].toString()
                  << "\n";
    }

    std::cout << "\n";

    std::cout << "=== Initial CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";

    std::cout << "=== Step Execution ===\n";

    std::size_t stepCount = 0;

    while (!cpu.state().halted()) {
        const std::size_t pcBefore = cpu.state().pc();

        if (pcBefore >= cpu.program().size()) {
            std::cout << "PC out of program range.\n";
            break;
        }

        const Instruction& instruction = cpu.program()[pcBefore];

        std::cout << "Step " << stepCount
                  << " | PC=" << pcBefore
                  << " | " << instruction.toString()
                  << "\n";

        cpu.step();

        std::cout << cpu.state().summary() << "\n";

        ++stepCount;
    }

    std::cout << "=== Final CPU State ===\n";
    std::cout << cpu.state().summary() << "\n";

    if (cpu.state().hasError()) {
        std::cout << "Execution failed: "
                  << cpu.state().errorMessage()
                  << "\n";

        return 1;
    }

    std::cout << "Execution finished successfully.\n";

    return 0;
}