#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/ALU.hpp"
#include "zero_cpu/core/ClockedDevice.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/InterruptController.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryLoader.hpp"
#include "zero_cpu/isa/InstructionDecoder.hpp"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace zero_cpu {

CPU::CPU() {
    reset();
}

void applyALUResultToFlags(Flags& flags, const ALUResult& result) {
    flags.setZero(result.zero);
    flags.setSign(result.sign);
    flags.setCarry(result.carry);
    flags.setOverflow(result.overflow);
}

std::int64_t packFlags(const Flags& flags) {
    std::int64_t value = 0;

    if (flags.zero()) {
        value |= 1LL << 0;
    }

    if (flags.sign()) {
        value |= 1LL << 1;
    }

    if (flags.carry()) {
        value |= 1LL << 2;
    }

    if (flags.overflow()) {
        value |= 1LL << 3;
    }

    return value;
}

void restoreFlags(Flags& flags, std::int64_t value) {
    flags.setZero((value & (1LL << 0)) != 0);
    flags.setSign((value & (1LL << 1)) != 0);
    flags.setCarry((value & (1LL << 2)) != 0);
    flags.setOverflow((value & (1LL << 3)) != 0);
}

std::size_t checkedReturnAddress(
    std::int64_t returnAddress,
    const char* context
) {
    if (returnAddress < 0) {
        throw std::runtime_error(context);
    }

    return static_cast<std::size_t>(returnAddress);
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
        loader.loadIntoMemory(
            program,
            state_.memory(),
            kDefaultBinaryCodeBase
        );

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

    try {
        if (servicePendingInterruptIfNeeded()) {
            return;
        }
    } catch (const std::exception& ex) {
        setRuntimeError(ex.what());
        return;
    }

    if (has_binary_program_) {
        try {
            stepBinary();

            if (!state_.halted() && !state_.hasError()) {
                tickClockedDevices();
            }
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

        if (!state_.halted() && !state_.hasError()) {
            tickClockedDevices();
        }
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

void CPU::setMMIOBus(std::shared_ptr<MMIOBus> bus) {
    mmio_bus_ = std::move(bus);
}

void CPU::clearMMIOBus() {
    mmio_bus_.reset();
}

bool CPU::hasMMIOBus() const {
    return static_cast<bool>(mmio_bus_);
}

void CPU::setInterruptController(
    std::shared_ptr<InterruptController> controller
) {
    interrupt_controller_ = std::move(controller);
}

void CPU::clearInterruptController() {
    interrupt_controller_.reset();
}

bool CPU::hasInterruptController() const {
    return static_cast<bool>(interrupt_controller_);
}

void CPU::addClockedDevice(std::shared_ptr<ClockedDevice> device) {
    if (!device) {
        throw std::runtime_error("Clocked device must not be null");
    }

    clocked_devices_.push_back(std::move(device));
}

void CPU::clearClockedDevices() {
    clocked_devices_.clear();
}

std::size_t CPU::clockedDeviceCount() const {
    return clocked_devices_.size();
}

bool CPU::servicePendingInterruptIfNeeded() {
    if (!interrupt_controller_) {
        return false;
    }

    if (!interrupt_controller_->hasPending()) {
        return false;
    }

    const InterruptRequest request = interrupt_controller_->acknowledge();
    const std::size_t handlerAddress =
        interrupt_controller_->vectorHandler(request.vector);

    if (has_binary_program_) {
        if (!isBinaryPcInCode(handlerAddress)) {
            throw std::runtime_error(
                "Interrupt handler is outside loaded binary code section"
            );
        }
    } else {
        if (handlerAddress >= program_.size()) {
            throw std::runtime_error(
                "Interrupt handler is outside loaded program"
            );
        }
    }

    const std::size_t returnAddress = state_.pc();
    pushValue(static_cast<std::int64_t>(returnAddress));
    pushValue(packFlags(state_.flags()));

    state_.registers().set(
        RegisterName::R0,
        static_cast<std::int64_t>(request.vector)
    );
    state_.registers().set(RegisterName::R1, request.payload);

    state_.setPc(handlerAddress);
    return true;
}

void CPU::tickClockedDevices() {
    for (const std::shared_ptr<ClockedDevice>& device : clocked_devices_) {
        if (!device) {
            throw std::runtime_error("Clocked device list contains null device");
        }

        device->tick();
    }
}

std::int64_t CPU::readDataMemory(std::size_t address) {
    if (mmio_bus_ && mmio_bus_->hasDeviceAt(address)) {
        return mmio_bus_->read(address);
    }

    return state_.memory().read(address);
}

void CPU::writeDataMemory(std::size_t address, std::int64_t value) {
    if (mmio_bus_ && mmio_bus_->hasDeviceAt(address)) {
        mmio_bus_->write(address, value);
        return;
    }

    state_.memory().write(address, value);
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
        advanceBinaryPcUnlessHalted();
        break;

    case Opcode::HALT:
        requireNoBinaryOperands(instruction);
        state_.halt();
        break;

    case Opcode::MOV: {
        requireTwoBinaryOperands(instruction);
        requireBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload
        );

        const std::int64_t value =
            readBinaryOperandValue(
                instruction.src_type,
                instruction.src_payload
            );

        writeBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload,
            value
        );

        state_.flags().updateZeroAndSign(value);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::LOAD: {
        requireTwoBinaryOperands(instruction);
        requireBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload
        );

        const std::size_t address =
            readBinaryMemoryAddress(
                instruction.src_type,
                instruction.src_payload
            );

        const std::int64_t value = readDataMemory(address);

        writeBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload,
            value
        );

        state_.flags().updateZeroAndSign(value);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::STORE: {
        requireTwoBinaryOperands(instruction);

        const std::size_t address =
            readBinaryMemoryAddress(
                instruction.dst_type,
                instruction.dst_payload
            );

        const std::int64_t value =
            readBinaryOperandValue(
                instruction.src_type,
                instruction.src_payload
            );

        writeDataMemory(address, value);
        state_.flags().updateZeroAndSign(value);

        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::ADD: {
        requireTwoBinaryOperands(instruction);
        requireBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload
        );

        const std::int64_t lhs =
            readBinaryOperandValue(
                instruction.dst_type,
                instruction.dst_payload
            );

        const std::int64_t rhs =
            readBinaryOperandValue(
                instruction.src_type,
                instruction.src_payload
            );

        const ALUResult result = ALU::add(lhs, rhs);

        writeBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload,
            result.value
        );

        applyALUResultToFlags(state_.flags(), result);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::SUB: {
        requireTwoBinaryOperands(instruction);
        requireBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload
        );

        const std::int64_t lhs =
            readBinaryOperandValue(
                instruction.dst_type,
                instruction.dst_payload
            );

        const std::int64_t rhs =
            readBinaryOperandValue(
                instruction.src_type,
                instruction.src_payload
            );

        const ALUResult result = ALU::sub(lhs, rhs);

        writeBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload,
            result.value
        );

        applyALUResultToFlags(state_.flags(), result);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::MUL: {
        requireTwoBinaryOperands(instruction);
        requireBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload
        );

        const std::int64_t lhs =
            readBinaryOperandValue(
                instruction.dst_type,
                instruction.dst_payload
            );

        const std::int64_t rhs =
            readBinaryOperandValue(
                instruction.src_type,
                instruction.src_payload
            );

        const ALUResult result = ALU::mul(lhs, rhs);

        writeBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload,
            result.value
        );

        applyALUResultToFlags(state_.flags(), result);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::DIV: {
        requireTwoBinaryOperands(instruction);
        requireBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload
        );

        const std::int64_t lhs =
            readBinaryOperandValue(
                instruction.dst_type,
                instruction.dst_payload
            );

        const std::int64_t rhs =
            readBinaryOperandValue(
                instruction.src_type,
                instruction.src_payload
            );

        const ALUResult result = ALU::div(lhs, rhs);

        writeBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload,
            result.value
        );

        applyALUResultToFlags(state_.flags(), result);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::CMP: {
        requireTwoBinaryOperands(instruction);

        const std::int64_t lhs =
            readBinaryOperandValue(
                instruction.dst_type,
                instruction.dst_payload
            );

        const std::int64_t rhs =
            readBinaryOperandValue(
                instruction.src_type,
                instruction.src_payload
            );

        const ALUResult result = ALU::compare(lhs, rhs);

        applyALUResultToFlags(state_.flags(), result);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::TEST: {
        requireTwoBinaryOperands(instruction);

        const std::int64_t lhs =
            readBinaryOperandValue(
                instruction.dst_type,
                instruction.dst_payload
            );

        const std::int64_t rhs =
            readBinaryOperandValue(
                instruction.src_type,
                instruction.src_payload
            );

        const ALUResult result = ALU::test(lhs, rhs);

        applyALUResultToFlags(state_.flags(), result);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::JMP: {
        requireSingleBinaryOperand(instruction);

        const std::size_t target =
            readBinaryCodeAddress(
                instruction.dst_type,
                instruction.dst_payload
            );

        state_.setPc(target);
        break;
    }

    case Opcode::JE: {
        requireSingleBinaryOperand(instruction);

        if (state_.flags().zero()) {
            state_.setPc(
                readBinaryCodeAddress(
                    instruction.dst_type,
                    instruction.dst_payload
                )
            );
        } else {
            advanceBinaryPcUnlessHalted();
        }

        break;
    }

    case Opcode::JNE: {
        requireSingleBinaryOperand(instruction);

        if (!state_.flags().zero()) {
            state_.setPc(
                readBinaryCodeAddress(
                    instruction.dst_type,
                    instruction.dst_payload
                )
            );
        } else {
            advanceBinaryPcUnlessHalted();
        }

        break;
    }

    case Opcode::JG: {
        requireSingleBinaryOperand(instruction);

        if (!state_.flags().zero() && !state_.flags().sign()) {
            state_.setPc(
                readBinaryCodeAddress(
                    instruction.dst_type,
                    instruction.dst_payload
                )
            );
        } else {
            advanceBinaryPcUnlessHalted();
        }

        break;
    }

    case Opcode::JL: {
        requireSingleBinaryOperand(instruction);

        if (state_.flags().sign()) {
            state_.setPc(
                readBinaryCodeAddress(
                    instruction.dst_type,
                    instruction.dst_payload
                )
            );
        } else {
            advanceBinaryPcUnlessHalted();
        }

        break;
    }

    case Opcode::PUSH: {
        requireSingleBinaryOperand(instruction);

        const std::int64_t value =
            readBinaryOperandValue(
                instruction.dst_type,
                instruction.dst_payload
            );

        pushValue(value);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::POP: {
        requireSingleBinaryOperand(instruction);
        requireBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload
        );

        const std::int64_t value = popValue();

        writeBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload,
            value
        );

        state_.flags().updateZeroAndSign(value);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::CALL: {
        requireSingleBinaryOperand(instruction);

        const std::size_t target =
            readBinaryCodeAddress(
                instruction.dst_type,
                instruction.dst_payload
            );

        const std::size_t returnAddress =
            state_.pc() + binary::kInstructionSize;

        pushValue(static_cast<std::int64_t>(returnAddress));
        state_.setPc(target);
        break;
    }

    case Opcode::RET: {
        requireNoBinaryOperands(instruction);

        const std::int64_t returnAddress = popValue();
        state_.setPc(
            checkedReturnAddress(
                returnAddress,
                "Negative binary return address"
            )
        );
        break;
    }

    case Opcode::IRET: {
        requireNoBinaryOperands(instruction);

        const std::int64_t flagsValue = popValue();
        restoreFlags(state_.flags(), flagsValue);

        const std::int64_t returnAddress = popValue();
        state_.setPc(
            checkedReturnAddress(
                returnAddress,
                "Negative binary interrupt return address"
            )
        );
        break;
    }

    case Opcode::AND: {
        requireTwoBinaryOperands(instruction);
        requireBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload
        );

        const std::int64_t lhs =
            readBinaryOperandValue(
                instruction.dst_type,
                instruction.dst_payload
            );

        const std::int64_t rhs =
            readBinaryOperandValue(
                instruction.src_type,
                instruction.src_payload
            );

        const ALUResult result = ALU::bitAnd(lhs, rhs);

        writeBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload,
            result.value
        );

        applyALUResultToFlags(state_.flags(), result);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::OR: {
        requireTwoBinaryOperands(instruction);
        requireBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload
        );

        const std::int64_t lhs =
            readBinaryOperandValue(
                instruction.dst_type,
                instruction.dst_payload
            );

        const std::int64_t rhs =
            readBinaryOperandValue(
                instruction.src_type,
                instruction.src_payload
            );

        const ALUResult result = ALU::bitOr(lhs, rhs);

        writeBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload,
            result.value
        );

        applyALUResultToFlags(state_.flags(), result);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::XOR: {
        requireTwoBinaryOperands(instruction);
        requireBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload
        );

        const std::int64_t lhs =
            readBinaryOperandValue(
                instruction.dst_type,
                instruction.dst_payload
            );

        const std::int64_t rhs =
            readBinaryOperandValue(
                instruction.src_type,
                instruction.src_payload
            );

        const ALUResult result = ALU::bitXor(lhs, rhs);

        writeBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload,
            result.value
        );

        applyALUResultToFlags(state_.flags(), result);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::NOT: {
        requireSingleBinaryOperand(instruction);
        requireBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload
        );

        const std::int64_t value =
            readBinaryOperandValue(
                instruction.dst_type,
                instruction.dst_payload
            );

        const ALUResult result = ALU::bitNot(value);

        writeBinaryRegisterDestination(
            instruction.dst_type,
            instruction.dst_payload,
            result.value
        );

        applyALUResultToFlags(state_.flags(), result);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::Invalid:
    default:
        throw std::runtime_error("Invalid binary opcode");
    }
}

RegisterName CPU::decodeBinaryRegister(std::int64_t payload) const {
    switch (payload) {
    case 0:
        return RegisterName::R0;
    case 1:
        return RegisterName::R1;
    case 2:
        return RegisterName::R2;
    case 3:
        return RegisterName::R3;
    case 4:
        return RegisterName::R4;
    case 5:
        return RegisterName::R5;
    case 6:
        return RegisterName::R6;
    case 7:
        return RegisterName::R7;
    default:
        throw std::runtime_error("Invalid binary register payload");
    }
}

std::int64_t CPU::readBinaryOperandValue(
    EncodedOperandType type,
    std::int64_t payload
) {
    switch (type) {
    case EncodedOperandType::Register:
        return state_.registers().get(decodeBinaryRegister(payload));

    case EncodedOperandType::Immediate:
        return payload;

    case EncodedOperandType::MemoryAddress:
        return readDataMemory(
            readBinaryMemoryAddress(type, payload)
        );

    default:
        throw std::runtime_error(
            "Binary operand cannot be read as a value"
        );
    }
}

void CPU::writeBinaryRegisterDestination(
    EncodedOperandType type,
    std::int64_t payload,
    std::int64_t value
) {
    requireBinaryRegisterDestination(type, payload);
    state_.registers().set(decodeBinaryRegister(payload), value);
}

std::size_t CPU::readBinaryMemoryAddress(
    EncodedOperandType type,
    std::int64_t payload
) const {
    if (type != EncodedOperandType::MemoryAddress) {
        throw std::runtime_error("Binary operand must be memory address");
    }

    if (payload < 0) {
        throw std::runtime_error("Negative binary memory address");
    }

    return static_cast<std::size_t>(payload);
}

std::size_t CPU::readBinaryCodeAddress(
    EncodedOperandType type,
    std::int64_t payload
) const {
    if (type != EncodedOperandType::CodeAddress) {
        throw std::runtime_error("Binary operand must be code address");
    }

    if (payload < 0) {
        throw std::runtime_error("Negative binary code address");
    }

    const std::size_t target =
        binary_code_base_ + static_cast<std::size_t>(payload);

    if (!isBinaryPcInCode(target)) {
        throw std::runtime_error(
            "Binary code target is outside loaded code section"
        );
    }

    return target;
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

void CPU::requireSingleBinaryOperand(
    const DecodedInstruction& instruction
) const {
    if (
        instruction.dst_type == EncodedOperandType::None ||
        instruction.src_type != EncodedOperandType::None ||
        instruction.src_payload != 0
    ) {
        throw std::runtime_error(
            "Binary instruction requires one operand"
        );
    }
}

void CPU::requireTwoBinaryOperands(
    const DecodedInstruction& instruction
) const {
    if (
        instruction.dst_type == EncodedOperandType::None ||
        instruction.src_type == EncodedOperandType::None
    ) {
        throw std::runtime_error(
            "Binary instruction requires two operands"
        );
    }
}

void CPU::requireBinaryRegisterDestination(
    EncodedOperandType type,
    std::int64_t payload
) const {
    if (type != EncodedOperandType::Register) {
        throw std::runtime_error(
            "Binary destination must be register"
        );
    }

    decodeBinaryRegister(payload);
}

void CPU::advanceBinaryPcUnlessHalted() {
    if (!state_.halted()) {
        state_.setPc(state_.pc() + binary::kInstructionSize);
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

    case Opcode::IRET:
        executeIret(instruction);
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

    case Opcode::TEST: {
        requireTwoOperands(instruction);

        const std::int64_t lhs = readOperandValue(instruction.dst());
        const std::int64_t rhs = readOperandValue(instruction.src());
        const ALUResult result = ALU::test(lhs, rhs);

        applyALUResultToFlags(state_.flags(), result);
        advancePcUnlessHalted();
        break;
    }

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
        readDataMemory(instruction.src().asMemoryAddress());

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
    writeDataMemory(instruction.dst().asMemoryAddress(), value);

    state_.flags().updateZeroAndSign(value);
    advancePcUnlessHalted();
}

void CPU::executeAdd(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const ALUResult result = ALU::add(lhs, rhs);

    writeRegisterDestination(instruction.dst(), result.value);
    applyALUResultToFlags(state_.flags(), result);

    advancePcUnlessHalted();
}

void CPU::executeSub(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const ALUResult result = ALU::sub(lhs, rhs);

    writeRegisterDestination(instruction.dst(), result.value);
    applyALUResultToFlags(state_.flags(), result);

    advancePcUnlessHalted();
}

void CPU::executeMul(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const ALUResult result = ALU::mul(lhs, rhs);

    writeRegisterDestination(instruction.dst(), result.value);
    applyALUResultToFlags(state_.flags(), result);

    advancePcUnlessHalted();
}

void CPU::executeDiv(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());

    const ALUResult result = ALU::div(lhs, rhs);

    writeRegisterDestination(instruction.dst(), result.value);
    applyALUResultToFlags(state_.flags(), result);

    advancePcUnlessHalted();
}

void CPU::executeCmp(const Instruction& instruction) {
    requireTwoOperands(instruction);

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const ALUResult result = ALU::compare(lhs, rhs);

    applyALUResultToFlags(state_.flags(), result);

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
    state_.setPc(
        checkedReturnAddress(
            returnAddress,
            "Negative return address"
        )
    );
}

void CPU::executeIret(const Instruction& instruction) {
    requireNoOperand(instruction);

    const std::int64_t flagsValue = popValue();
    restoreFlags(state_.flags(), flagsValue);

    const std::int64_t returnAddress = popValue();
    state_.setPc(
        checkedReturnAddress(
            returnAddress,
            "Negative interrupt return address"
        )
    );
}

void CPU::executeAnd(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const ALUResult result = ALU::bitAnd(lhs, rhs);

    writeRegisterDestination(instruction.dst(), result.value);
    applyALUResultToFlags(state_.flags(), result);

    advancePcUnlessHalted();
}

void CPU::executeOr(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const ALUResult result = ALU::bitOr(lhs, rhs);

    writeRegisterDestination(instruction.dst(), result.value);
    applyALUResultToFlags(state_.flags(), result);

    advancePcUnlessHalted();
}

void CPU::executeXor(const Instruction& instruction) {
    requireTwoOperands(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t lhs = readOperandValue(instruction.dst());
    const std::int64_t rhs = readOperandValue(instruction.src());
    const ALUResult result = ALU::bitXor(lhs, rhs);

    writeRegisterDestination(instruction.dst(), result.value);
    applyALUResultToFlags(state_.flags(), result);

    advancePcUnlessHalted();
}

void CPU::executeNot(const Instruction& instruction) {
    requireSingleOperand(instruction);
    requireRegisterDestination(instruction.dst());

    const std::int64_t value = readOperandValue(instruction.dst());
    const ALUResult result = ALU::bitNot(value);

    writeRegisterDestination(instruction.dst(), result.value);
    applyALUResultToFlags(state_.flags(), result);

    advancePcUnlessHalted();
}

std::int64_t CPU::readOperandValue(const Operand& operand) {
    switch (operand.type()) {
    case OperandType::Register:
        return state_.registers().get(operand.asRegister());

    case OperandType::Immediate:
        return operand.asImmediate();

    case OperandType::MemoryAddress:
        return readDataMemory(operand.asMemoryAddress());

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