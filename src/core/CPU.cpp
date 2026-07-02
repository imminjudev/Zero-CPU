#include "zero_cpu/core/CPU.hpp"

#include "zero_cpu/isa/Opcode.hpp"

#include <stdexcept>
#include <utility>

namespace zero_cpu {

CPU::CPU()
    : state_(),
      program_() {
}

void CPU::loadProgram(std::vector<Instruction> program) {
    program_ = std::move(program);
    state_.reset();
}

void CPU::reset() {
    state_.reset();
}

CPUState& CPU::state() {
    return state_;
}

const CPUState& CPU::state() const {
    return state_;
}

const std::vector<Instruction>& CPU::program() const {
    return program_;
}

bool CPU::step() {
    if (state_.halted()) {
        return false;
    }

    if (state_.pc() >= program_.size()) {
        state_.setError("Program counter out of bounds");
        return false;
    }

    const Instruction& instruction = program_[state_.pc()];

    try {
        execute(instruction);
    } catch (const std::exception& ex) {
        state_.setError(ex.what());
        return false;
    }

    return !state_.halted();
}

void CPU::run(std::size_t max_steps) {
    std::size_t executed_steps = 0;

    while (!state_.halted()) {
        if (executed_steps >= max_steps) {
            state_.setError("Maximum execution step count exceeded");
            return;
        }

        step();
        ++executed_steps;
    }
}

void CPU::execute(const Instruction& instruction) {
    switch (instruction.opcode()) {
    case Opcode::NOP:
        executeNop(instruction);
        break;

    case Opcode::HALT:
        executeHalt(instruction);
        break;

    case Opcode::MOV:
        executeMov(instruction);
        break;

    case Opcode::ADD:
        executeAdd(instruction);
        break;

    case Opcode::SUB:
        executeSub(instruction);
        break;

    default:
        throw std::runtime_error(
            "Unsupported opcode in current CPU execution engine: "
            + opcodeToString(instruction.opcode())
        );
    }
}

void CPU::executeNop(const Instruction& instruction) {
    requireNoOperand(instruction);
    state_.advancePc();
}

void CPU::executeHalt(const Instruction& instruction) {
    requireNoOperand(instruction);
    state_.halt();
}

void CPU::executeMov(const Instruction& instruction) {
    requireDestination(instruction);
    requireSource(instruction);

    const std::int64_t value = readOperandValue(instruction.src());

    writeOperandValue(instruction.dst(), value);

    state_.advancePc();
}

void CPU::executeAdd(const Instruction& instruction) {
    requireRegisterDestination(instruction);
    requireSource(instruction);

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());

    const std::int64_t result = lhs + rhs;

    writeOperandValue(instruction.dst(), result);
    state_.flags().updateZeroAndSign(result);
    state_.advancePc();
}

void CPU::executeSub(const Instruction& instruction) {
    requireRegisterDestination(instruction);
    requireSource(instruction);

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());

    const std::int64_t result = lhs - rhs;

    writeOperandValue(instruction.dst(), result);
    state_.flags().updateZeroAndSign(result);
    state_.advancePc();
}

std::int64_t CPU::readOperandValue(const Operand& operand) const {
    if (operand.isRegister()) {
        return state_.registers().get(operand.asRegister());
    }

    if (operand.isImmediate()) {
        return operand.asImmediate();
    }

    if (operand.isMemoryAddress()) {
        return state_.memory().read(operand.asMemoryAddress());
    }

    if (operand.isLabel()) {
        throw std::runtime_error("Cannot read label operand as value in current execution phase");
    }

    throw std::runtime_error("Cannot read empty operand");
}

void CPU::writeOperandValue(const Operand& operand, std::int64_t value) {
    if (operand.isRegister()) {
        state_.registers().set(operand.asRegister(), value);
        return;
    }

    if (operand.isMemoryAddress()) {
        state_.memory().write(operand.asMemoryAddress(), value);
        return;
    }

    if (operand.isImmediate()) {
        throw std::runtime_error("Cannot write to immediate operand");
    }

    if (operand.isLabel()) {
        throw std::runtime_error("Cannot write to label operand");
    }

    throw std::runtime_error("Cannot write to empty operand");
}

void CPU::requireNoOperand(const Instruction& instruction) const {
    if (instruction.hasDestination() || instruction.hasSource()) {
        throw std::runtime_error(
            opcodeToString(instruction.opcode())
            + " does not accept operands"
        );
    }
}

void CPU::requireDestination(const Instruction& instruction) const {
    if (!instruction.hasDestination()) {
        throw std::runtime_error(
            opcodeToString(instruction.opcode())
            + " requires destination operand"
        );
    }
}

void CPU::requireSource(const Instruction& instruction) const {
    if (!instruction.hasSource()) {
        throw std::runtime_error(
            opcodeToString(instruction.opcode())
            + " requires source operand"
        );
    }
}

void CPU::requireRegisterDestination(const Instruction& instruction) const {
    requireDestination(instruction);

    if (!instruction.dst().isRegister()) {
        throw std::runtime_error(
            opcodeToString(instruction.opcode())
            + " requires register destination operand"
        );
    }
}

void CPU::fail(const char* message) const {
    throw std::runtime_error(message);
}

} // namespace zero_cpu