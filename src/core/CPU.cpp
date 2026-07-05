#include "zero_cpu/core/CPU.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryLoader.hpp"
#include "zero_cpu/isa/InstructionDecoder.hpp"

#include <stdexcept>
#include <vector>

namespace zero_cpu {

CPU::CPU() {
    reset();
}

void CPU::reset() {
    state_.reset();
    program_.clear();
    labels_.clear();
    trace_logger_.clear();

    has_binary_program_ = false;
    binary_code_base_ = 0;
    binary_entry_point_ = 0;
    binary_code_size_ = 0;
}

void CPU::loadProgram(
    const std::vector<Instruction>& program,
    const LabelTable& labels
) {
    state_.reset();
    program_ = program;
    labels_ = labels;
    trace_logger_.clear();

    has_binary_program_ = false;
    binary_code_base_ = 0;
    binary_entry_point_ = 0;
    binary_code_size_ = 0;
}

void CPU::loadBinaryProgram(const binary::BinaryProgram& program) {
    state_.reset();
    program_.clear();
    labels_.clear();
    trace_logger_.clear();

    binary::BinaryLoader loader;
    const binary::LoadedBinaryImage image =
        loader.loadIntoMemory(program, state_.memory());

    state_.setPc(image.entry_point);

    has_binary_program_ = true;
    binary_code_base_ = image.code_base;
    binary_entry_point_ = image.entry_point;
    binary_code_size_ = image.code_size;
}

void CPU::step() {
    if (state_.halted()) {
        return;
    }

    if (has_binary_program_) {
        try {
            stepBinary();
        } catch (const std::exception& ex) {
            setRuntimeError(ex.what());
        }

        return;
    }

    const std::size_t pc = state_.pc();

    if (pc >= program_.size()) {
        setRuntimeError("PC out of program range");
        return;
    }

    try {
        const Instruction& instruction = program_[pc];
        execute(instruction);
    } catch (const std::exception& ex) {
        setRuntimeError(ex.what());
    }
}

void CPU::run(std::size_t maxSteps) {
    std::size_t count = 0;

    while (!state_.halted() && count < maxSteps) {
        step();
        ++count;
    }

    if (!state_.halted() && count >= maxSteps) {
        setRuntimeError("Maximum step count reached");
    }
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

TraceLogger& CPU::traceLogger() {
    return trace_logger_;
}

const TraceLogger& CPU::traceLogger() const {
    return trace_logger_;
}

bool CPU::hasBinaryProgram() const {
    return has_binary_program_;
}

std::size_t CPU::binaryCodeBase() const {
    return binary_code_base_;
}

std::size_t CPU::binaryEntryPoint() const {
    return binary_entry_point_;
}

std::size_t CPU::binaryCodeSize() const {
    return binary_code_size_;
}

void CPU::stepBinary() {
    const std::size_t pc = state_.pc();

    if (!isBinaryPcInCode(pc)) {
        throw std::runtime_error("Binary PC is outside loaded code section");
    }

    const std::vector<std::uint8_t> instructionBytes =
        state_.memory().readBytes(pc, binary::kInstructionSize);

    InstructionDecoder decoder;
    const DecodedInstruction instruction =
        decoder.decodeInstruction(instructionBytes);

    executeBinaryInstruction(instruction);
}

bool CPU::isBinaryPcInCode(std::size_t pc) const {
    const std::size_t begin = binary_code_base_;
    const std::size_t end = binary_code_base_ + binary_code_size_;

    if (pc < begin) {
        return false;
    }

    if (pc >= end) {
        return false;
    }

    if (end - pc < binary::kInstructionSize) {
        return false;
    }

    return ((pc - begin) % binary::kInstructionSize) == 0;
}

void CPU::executeBinaryInstruction(
    const DecodedInstruction& instruction
) {
    switch (instruction.opcode) {
    case Opcode::NOP:
        requireNoBinaryOperands(instruction);
        state_.setPc(state_.pc() + binary::kInstructionSize);
        break;

    case Opcode::HALT:
        requireNoBinaryOperands(instruction);
        state_.halt();
        break;

    default:
        throw std::runtime_error(
            "Binary execution currently supports only NOP and HALT"
        );
    }
}

void CPU::requireNoBinaryOperands(
    const DecodedInstruction& instruction
) const {
    if (
        instruction.dst_type != EncodedOperandType::None ||
        instruction.src_type != EncodedOperandType::None ||
        instruction.dst_payload != 0 ||
        instruction.src_payload != 0
    ) {
        throw std::runtime_error(
            "Binary instruction requires no operands"
        );
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

    case Opcode::AND:
        executeAnd(instruction);
        break;

    case Opcode::OR:
        executeOr(instruction);
        break;

    case Opcode::XOR:
        executeXor(instruction);
        break;

    case Opcode::NOT:
        executeNot(instruction);
        break;

    case Opcode::TEST:
        throw std::runtime_error("TEST is not implemented yet");

    case Opcode::Invalid:
    default:
        throw std::runtime_error("Invalid opcode");
    }
}

void CPU::executeNop(const Instruction& instruction) {
    requireNoOperand(instruction);
    advancePcUnlessHalted();
}

void CPU::executeHalt(const Instruction& instruction) {
    requireNoOperand(instruction);
    state_.halt();
}

void CPU::executeMov(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t value = readOperandValue(instruction.src());
    writeRegisterDestination(instruction.dst(), value);

    state_.flags().updateZeroAndSign(value);
    advancePcUnlessHalted();
}

void CPU::executeLoad(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    if (instruction.src().type() != OperandType::MemoryAddress) {
        throw std::runtime_error("LOAD source must be memory address");
    }

    const std::int64_t value =
        state_.memory().read(instruction.src().asMemoryAddress());

    writeRegisterDestination(instruction.dst(), value);
    state_.flags().updateZeroAndSign(value);

    advancePcUnlessHalted();
}

void CPU::executeStore(const Instruction& instruction) {
    requireTwoOperands(instruction);

    if (instruction.dst().type() != OperandType::MemoryAddress) {
        throw std::runtime_error("STORE destination must be memory address");
    }

    const std::int64_t value = readOperandValue(instruction.src());
    state_.memory().write(instruction.dst().asMemoryAddress(), value);

    state_.flags().updateZeroAndSign(value);
    advancePcUnlessHalted();
}

void CPU::executeAdd(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const std::int64_t result = lhs + rhs;

    writeRegisterDestination(instruction.dst(), result);
    state_.flags().updateZeroAndSign(result);

    advancePcUnlessHalted();
}

void CPU::executeSub(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const std::int64_t result = lhs - rhs;

    writeRegisterDestination(instruction.dst(), result);
    state_.flags().updateZeroAndSign(result);

    advancePcUnlessHalted();
}

void CPU::executeMul(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const std::int64_t result = lhs * rhs;

    writeRegisterDestination(instruction.dst(), result);
    state_.flags().updateZeroAndSign(result);

    advancePcUnlessHalted();
}

void CPU::executeDiv(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());

    if (rhs == 0) {
        throw std::runtime_error("Division by zero");
    }

    const std::int64_t result = lhs / rhs;

    writeRegisterDestination(instruction.dst(), result);
    state_.flags().updateZeroAndSign(result);

    advancePcUnlessHalted();
}

void CPU::executeCmp(const Instruction& instruction) {
    requireTwoOperands(instruction);

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const std::int64_t result = lhs - rhs;

    state_.flags().updateZeroAndSign(result);

    advancePcUnlessHalted();
}

void CPU::executeJmp(const Instruction& instruction) {
    requireSingleOperand(instruction);
    state_.setPc(resolveLabelAddress(instruction.dst()));
}

void CPU::executeJe(const Instruction& instruction) {
    requireSingleOperand(instruction);

    if (state_.flags().zero()) {
        state_.setPc(resolveLabelAddress(instruction.dst()));
    } else {
        advancePcUnlessHalted();
    }
}

void CPU::executeJne(const Instruction& instruction) {
    requireSingleOperand(instruction);

    if (!state_.flags().zero()) {
        state_.setPc(resolveLabelAddress(instruction.dst()));
    } else {
        advancePcUnlessHalted();
    }
}

void CPU::executeJg(const Instruction& instruction) {
    requireSingleOperand(instruction);

    if (!state_.flags().zero() && !state_.flags().sign()) {
        state_.setPc(resolveLabelAddress(instruction.dst()));
    } else {
        advancePcUnlessHalted();
    }
}

void CPU::executeJl(const Instruction& instruction) {
    requireSingleOperand(instruction);

    if (state_.flags().sign()) {
        state_.setPc(resolveLabelAddress(instruction.dst()));
    } else {
        advancePcUnlessHalted();
    }
}

void CPU::executePush(const Instruction& instruction) {
    requireSingleOperand(instruction);

    const std::int64_t value = readOperandValue(instruction.dst());
    pushValue(value);

    advancePcUnlessHalted();
}

void CPU::executePop(const Instruction& instruction) {
    requireSingleOperand(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t value = popValue();

    writeRegisterDestination(instruction.dst(), value);
    state_.flags().updateZeroAndSign(value);

    advancePcUnlessHalted();
}

void CPU::executeCall(const Instruction& instruction) {
    requireSingleOperand(instruction);

    const std::size_t returnAddress = state_.pc() + 1;
    pushValue(static_cast<std::int64_t>(returnAddress));

    state_.setPc(resolveLabelAddress(instruction.dst()));
}

void CPU::executeRet(const Instruction& instruction) {
    requireNoOperand(instruction);

    const std::int64_t returnAddress = popValue();

    if (returnAddress < 0) {
        throw std::runtime_error("Negative return address");
    }

    state_.setPc(static_cast<std::size_t>(returnAddress));
}

void CPU::executeAnd(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const std::int64_t result = lhs & rhs;

    writeRegisterDestination(instruction.dst(), result);
    state_.flags().updateZeroAndSign(result);

    advancePcUnlessHalted();
}

void CPU::executeOr(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const std::int64_t result = lhs | rhs;

    writeRegisterDestination(instruction.dst(), result);
    state_.flags().updateZeroAndSign(result);

    advancePcUnlessHalted();
}

void CPU::executeXor(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const std::int64_t result = lhs ^ rhs;

    writeRegisterDestination(instruction.dst(), result);
    state_.flags().updateZeroAndSign(result);

    advancePcUnlessHalted();
}

void CPU::executeNot(const Instruction& instruction) {
    requireSingleOperand(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t value = readOperandValue(instruction.dst());
    const std::int64_t result = ~value;

    writeRegisterDestination(instruction.dst(), result);
    state_.flags().updateZeroAndSign(result);

    advancePcUnlessHalted();
}

std::int64_t CPU::readOperandValue(const Operand& operand) const {
    switch (operand.type()) {
    case OperandType::Register:
        return state_.registers().get(operand.asRegister());

    case OperandType::Immediate:
        return operand.asImmediate();

    case OperandType::MemoryAddress:
        return state_.memory().read(operand.asMemoryAddress());

    default:
        throw std::runtime_error("Operand cannot be read as a value");
    }
}

void CPU::writeRegisterDestination(
    const Operand& operand,
    std::int64_t value
) {
    requireRegisterDestination(operand);
    state_.registers().set(operand.asRegister(), value);
}

std::size_t CPU::resolveLabelAddress(const Operand& operand) const {
    if (operand.type() != OperandType::Label) {
        throw std::runtime_error("Jump target must be label");
    }

    const auto it = labels_.find(operand.asLabel());

    if (it == labels_.end()) {
        throw std::runtime_error("Undefined label: " + operand.asLabel());
    }

    return it->second;
}

void CPU::pushValue(std::int64_t value) {
    const std::size_t sp = state_.sp();

    state_.memory().write(sp, value);
    state_.setSp(sp + kStackSlotSize);
}

std::int64_t CPU::popValue() {
    const std::size_t sp = state_.sp();

    if (sp < kStackSlotSize) {
        throw std::runtime_error("Stack underflow");
    }

    const std::size_t newSp = sp - kStackSlotSize;
    state_.setSp(newSp);

    return state_.memory().read(newSp);
}

void CPU::requireNoOperand(const Instruction& instruction) const {
    if (
        instruction.dst().type() != OperandType::None ||
        instruction.src().type() != OperandType::None
    ) {
        throw std::runtime_error("Instruction requires no operand");
    }
}

void CPU::requireSingleOperand(const Instruction& instruction) const {
    if (
        instruction.dst().type() == OperandType::None ||
        instruction.src().type() != OperandType::None
    ) {
        throw std::runtime_error("Instruction requires one operand");
    }
}

void CPU::requireTwoOperands(const Instruction& instruction) const {
    if (
        instruction.dst().type() == OperandType::None ||
        instruction.src().type() == OperandType::None
    ) {
        throw std::runtime_error("Instruction requires two operands");
    }
}

void CPU::requireRegisterDestination(const Operand& operand) const {
    if (operand.type() != OperandType::Register) {
        throw std::runtime_error("Destination must be register");
    }
}

void CPU::advancePcUnlessHalted() {
    if (!state_.halted()) {
        state_.advancePc();
    }
}

void CPU::setRuntimeError(const std::string& message) {
    state_.setError(message);
}

} // namespace zero_cpu