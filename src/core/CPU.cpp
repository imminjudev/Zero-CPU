#include "zero_cpu/core/CPU.hpp"

#include "zero_cpu/isa/Opcode.hpp"
#include "zero_cpu/trace/TraceEvent.hpp"

#include <stdexcept>
#include <utility>

namespace zero_cpu {

namespace {

constexpr std::size_t kStackSlotSize = 8;

}

CPU::CPU()
    : state_(),
      program_(),
      labels_(),
      trace_logger_() {
}

void CPU::loadProgram(std::vector<Instruction> program) {
    program_ = std::move(program);
    labels_.clear();
    state_.reset();
    trace_logger_.clear();
}

void CPU::loadProgram(std::vector<Instruction> program, LabelTable labels) {
    program_ = std::move(program);
    labels_ = std::move(labels);
    state_.reset();
    trace_logger_.clear();
}

void CPU::setLabels(LabelTable labels) {
    labels_ = std::move(labels);
}

void CPU::reset() {
    state_.reset();
    trace_logger_.clear();
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

const CPU::LabelTable& CPU::labels() const {
    return labels_;
}

const TraceLogger& CPU::traceLogger() const {
    return trace_logger_;
}

TraceLogger& CPU::traceLogger() {
    return trace_logger_;
}

bool CPU::step() {
    if (state_.halted()) {
        return false;
    }

    if (state_.pc() >= program_.size()) {
        state_.setError("Program counter out of bounds");
        return false;
    }

    const Instruction instruction = program_[state_.pc()];
    const CPUState before = state_;

    try {
        execute(instruction);

        const CPUState after = state_;

        trace_logger_.record(
            TraceEvent(before, instruction, after)
        );
    } catch (const std::exception& ex) {
        state_.setError(ex.what());

        const CPUState after = state_;

        trace_logger_.record(
            TraceEvent(before, instruction, after, ex.what())
        );

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

    case Opcode::LOAD:
        executeLoad(instruction);
        break;

    case Opcode::STORE:
        executeStore(instruction);
        break;

    case Opcode::ADD:
        executeAdd(instruction);
        break;

    case Opcode::SUB:
        executeSub(instruction);
        break;

    case Opcode::MUL:
        executeMul(instruction);
        break;

    case Opcode::DIV:
        executeDiv(instruction);
        break;

    case Opcode::CMP:
        executeCmp(instruction);
        break;

    case Opcode::JMP:
        executeJmp(instruction);
        break;

    case Opcode::JE:
        executeJe(instruction);
        break;

    case Opcode::JNE:
        executeJne(instruction);
        break;

    case Opcode::JG:
        executeJg(instruction);
        break;

    case Opcode::JL:
        executeJl(instruction);
        break;

    case Opcode::PUSH:
        executePush(instruction);
        break;

    case Opcode::POP:
        executePop(instruction);
        break;

    case Opcode::CALL:
        executeCall(instruction);
        break;

    case Opcode::RET:
        executeRet(instruction);
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
    requireRegisterDestination(instruction);
    requireSource(instruction);

    if (!instruction.src().isRegister() && !instruction.src().isImmediate()) {
        throw std::runtime_error("MOV source must be register or immediate");
    }

    const std::int64_t value = readOperandValue(instruction.src());

    writeOperandValue(instruction.dst(), value);

    state_.advancePc();
}

void CPU::executeLoad(const Instruction& instruction) {
    requireRegisterDestination(instruction);
    requireMemorySource(instruction);

    const std::int64_t value = readOperandValue(instruction.src());

    writeOperandValue(instruction.dst(), value);

    state_.advancePc();
}

void CPU::executeStore(const Instruction& instruction) {
    requireMemoryDestination(instruction);
    requireSource(instruction);

    if (!instruction.src().isRegister() && !instruction.src().isImmediate()) {
        throw std::runtime_error("STORE source must be register or immediate");
    }

    const std::int64_t value = readOperandValue(instruction.src());

    writeOperandValue(instruction.dst(), value);

    state_.advancePc();
}

void CPU::executeAdd(const Instruction& instruction) {
    requireRegisterDestination(instruction);
    requireSource(instruction);

    if (!instruction.src().isRegister() && !instruction.src().isImmediate()) {
        throw std::runtime_error("ADD source must be register or immediate");
    }

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

    if (!instruction.src().isRegister() && !instruction.src().isImmediate()) {
        throw std::runtime_error("SUB source must be register or immediate");
    }

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());

    const std::int64_t result = lhs - rhs;

    writeOperandValue(instruction.dst(), result);
    state_.flags().updateZeroAndSign(result);
    state_.advancePc();
}

void CPU::executeMul(const Instruction& instruction) {
    requireRegisterDestination(instruction);
    requireSource(instruction);

    if (!instruction.src().isRegister() && !instruction.src().isImmediate()) {
        throw std::runtime_error("MUL source must be register or immediate");
    }

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());

    const std::int64_t result = lhs * rhs;

    writeOperandValue(instruction.dst(), result);
    state_.flags().updateZeroAndSign(result);
    state_.advancePc();
}

void CPU::executeDiv(const Instruction& instruction) {
    requireRegisterDestination(instruction);
    requireSource(instruction);

    if (!instruction.src().isRegister() && !instruction.src().isImmediate()) {
        throw std::runtime_error("DIV source must be register or immediate");
    }

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());

    if (rhs == 0) {
        throw std::runtime_error("Division by zero");
    }

    const std::int64_t result = lhs / rhs;

    writeOperandValue(instruction.dst(), result);
    state_.flags().updateZeroAndSign(result);
    state_.advancePc();
}

void CPU::executeCmp(const Instruction& instruction) {
    requireRegisterDestination(instruction);
    requireSource(instruction);

    if (!instruction.src().isRegister() && !instruction.src().isImmediate()) {
        throw std::runtime_error("CMP source must be register or immediate");
    }

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());

    const std::int64_t result = lhs - rhs;

    state_.flags().updateZeroAndSign(result);
    state_.advancePc();
}

void CPU::executeJmp(const Instruction& instruction) {
    requireLabelDestination(instruction);
    branchToLabel(instruction.dst());
}

void CPU::executeJe(const Instruction& instruction) {
    requireLabelDestination(instruction);
    branchToLabelIf(instruction.dst(), state_.flags().zero());
}

void CPU::executeJne(const Instruction& instruction) {
    requireLabelDestination(instruction);
    branchToLabelIf(instruction.dst(), !state_.flags().zero());
}

void CPU::executeJg(const Instruction& instruction) {
    requireLabelDestination(instruction);

    const bool greater =
        !state_.flags().zero()
        && !state_.flags().sign();

    branchToLabelIf(instruction.dst(), greater);
}

void CPU::executeJl(const Instruction& instruction) {
    requireLabelDestination(instruction);
    branchToLabelIf(instruction.dst(), state_.flags().sign());
}

void CPU::executePush(const Instruction& instruction) {
    requireSingleOperand(instruction);

    const std::int64_t value = readOperandValue(instruction.dst());

    ensureStackCanPush();

    const std::size_t sp = state_.sp();

    state_.memory().write(sp, value);
    state_.setSp(sp + kStackSlotSize);
    state_.advancePc();
}

void CPU::executePop(const Instruction& instruction) {
    requireSingleOperand(instruction);
    requireRegisterDestination(instruction);

    ensureStackCanPop();

    const std::size_t newSp = state_.sp() - kStackSlotSize;
    const std::int64_t value = state_.memory().read(newSp);

    state_.setSp(newSp);
    state_.registers().set(instruction.dst().asRegister(), value);
    state_.advancePc();
}

void CPU::executeCall(const Instruction& instruction) {
    requireSingleOperand(instruction);

    const std::size_t target = resolveLabelAddress(instruction.dst());

    ensureStackCanPush();

    const std::size_t sp = state_.sp();
    const std::size_t returnAddress = state_.pc() + 1;

    state_.memory().write(
        sp,
        static_cast<std::int64_t>(returnAddress)
    );

    state_.setSp(sp + kStackSlotSize);
    state_.setPc(target);
}

void CPU::executeRet(const Instruction& instruction) {
    requireNoOperand(instruction);

    ensureStackCanPop();

    const std::size_t newSp = state_.sp() - kStackSlotSize;
    const std::int64_t returnAddress = state_.memory().read(newSp);

    if (returnAddress < 0) {
        throw std::runtime_error("Invalid return address");
    }

    const auto target = static_cast<std::size_t>(returnAddress);

    if (target >= program_.size()) {
        throw std::runtime_error("Return address out of program range");
    }

    state_.setSp(newSp);
    state_.setPc(target);
}

void CPU::branchToLabel(const Operand& operand) {
    const std::size_t target = resolveLabelAddress(operand);
    state_.setPc(target);
}

void CPU::branchToLabelIf(const Operand& operand, bool condition) {
    if (condition) {
        branchToLabel(operand);
    } else {
        state_.advancePc();
    }
}

std::size_t CPU::resolveLabelAddress(const Operand& operand) const {
    if (!operand.isLabel()) {
        throw std::runtime_error("Branch target must be label operand");
    }

    const std::string& label = operand.asLabel();

    const auto found = labels_.find(label);

    if (found == labels_.end()) {
        throw std::runtime_error("Unknown label: " + label);
    }

    const std::size_t address = found->second;

    if (address >= program_.size()) {
        throw std::runtime_error("Label address out of program range: " + label);
    }

    return address;
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
        throw std::runtime_error("Cannot read label operand as value");
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

void CPU::ensureStackCanPush() const {
    const std::size_t sp = state_.sp();

    if (sp > state_.memory().size()) {
        throw std::runtime_error("Stack pointer out of range");
    }

    if (kStackSlotSize > state_.memory().size() - sp) {
        throw std::runtime_error("Stack overflow");
    }
}

void CPU::ensureStackCanPop() const {
    const std::size_t sp = state_.sp();

    if (sp < CPUState::kDefaultStackBase + kStackSlotSize) {
        throw std::runtime_error("Stack underflow");
    }

    if (sp > state_.memory().size()) {
        throw std::runtime_error("Stack pointer out of range");
    }
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

void CPU::requireMemoryDestination(const Instruction& instruction) const {
    requireDestination(instruction);

    if (!instruction.dst().isMemoryAddress()) {
        throw std::runtime_error(
            opcodeToString(instruction.opcode())
            + " requires memory destination operand"
        );
    }
}

void CPU::requireMemorySource(const Instruction& instruction) const {
    requireSource(instruction);

    if (!instruction.src().isMemoryAddress()) {
        throw std::runtime_error(
            opcodeToString(instruction.opcode())
            + " requires memory source operand"
        );
    }
}

void CPU::requireLabelDestination(const Instruction& instruction) const {
    requireDestination(instruction);

    if (instruction.hasSource()) {
        throw std::runtime_error(
            opcodeToString(instruction.opcode())
            + " does not accept source operand"
        );
    }

    if (!instruction.dst().isLabel()) {
        throw std::runtime_error(
            opcodeToString(instruction.opcode())
            + " requires label destination operand"
        );
    }
}

void CPU::requireSingleOperand(const Instruction& instruction) const {
    requireDestination(instruction);

    if (instruction.hasSource()) {
        throw std::runtime_error(
            opcodeToString(instruction.opcode())
            + " accepts only one operand"
        );
    }
}

} // namespace zero_cpu