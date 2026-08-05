#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/ALU.hpp"
#include "zero_cpu/core/ClockedDevice.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/MemoryMap.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryLoader.hpp"
#include "zero_cpu/isa/InstructionDecoder.hpp"
#include "zero_cpu/trace/TraceEvent.hpp"

#include <memory>
#include <stdexcept>
#include <string>
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

bool signedLessThanFromFlags(const Flags& flags) {
    return flags.sign() != flags.overflow();
}

bool signedGreaterThanFromFlags(const Flags& flags) {
    return !flags.zero() && flags.sign() == flags.overflow();
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

    has_user_code_range_ = false;
    user_code_begin_ = 0;
    user_code_end_exclusive_ = 0;

    kernel_stack_pointer_ = memory_map::kKernelStackBase;
    using_kernel_interrupt_stack_ = false;
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

    has_user_code_range_ = !program_.empty();
    user_code_begin_ = 0;
    user_code_end_exclusive_ = program_.size();

    kernel_stack_pointer_ = memory_map::kKernelStackBase;
    using_kernel_interrupt_stack_ = false;
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

    has_user_code_range_ = binary_code_size_ != 0;
    user_code_begin_ = binary_code_base_;
    user_code_end_exclusive_ =
        binary_code_base_ + binary_code_size_;

    kernel_stack_pointer_ = memory_map::kKernelStackBase;
    using_kernel_interrupt_stack_ = false;
}

void CPU::step() {
    if (state_.halted() || state_.hasError()) {
        return;
    }

    const CPUState before_interrupt = state_;

    try {
        if (servicePendingInterruptIfNeeded()) {
            const std::int64_t vector =
                state_.registers().get(RegisterName::R0);

            trace_logger_.record(
                TraceEvent(
                    before_interrupt,
                    Instruction(Opcode::INT, Operand::immediate(vector)),
                    state_
                )
            );

            return;
        }
    } catch (const std::exception& ex) {
        setRuntimeError(ex.what());

        trace_logger_.record(
            TraceEvent(
                before_interrupt,
                Instruction(Opcode::INT),
                state_,
                ex.what()
            )
        );

        return;
    }

    if (has_binary_program_) {
        const CPUState before = state_;
        Instruction instruction(Opcode::Invalid);
        std::string error_message;

        try {
            requireCurrentPcExecutable();
            instruction = traceInstructionForCurrentBinaryPc();
            stepBinary();

            if (!state_.halted() && !state_.hasError()) {
                tickClockedDevices();
            }
        } catch (const std::exception& ex) {
            setRuntimeError(ex.what());
            error_message = ex.what();
        }

        trace_logger_.record(
            TraceEvent(
                before,
                instruction,
                state_,
                error_message
            )
        );

        return;
    }

    const std::size_t pc = state_.pc();
    const CPUState before = state_;
    Instruction instruction(Opcode::Invalid);
    std::string error_message;

    try {
        requireCurrentPcExecutable();

        if (pc >= program_.size()) {
            throw std::runtime_error(
                "PC out of program range"
            );
        }

        instruction = program_[pc];
        execute(instruction);

        if (!state_.halted() && !state_.hasError()) {
            tickClockedDevices();
        }
    } catch (const std::exception& ex) {
        setRuntimeError(ex.what());
        error_message = ex.what();
    }

    trace_logger_.record(
        TraceEvent(
            before,
            instruction,
            state_,
            error_message
        )
    );
}


void CPU::run(std::size_t maxSteps) {
    std::size_t count = 0;

    while (
        !state_.halted() &&
        !state_.hasError() &&
        count < maxSteps
    ) {
        step();
        ++count;
    }

    if (
        !state_.halted() &&
        !state_.hasError() &&
        count >= maxSteps
    ) {
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

void CPU::setUserCodeRange(
    std::size_t begin,
    std::size_t endExclusive
) {
    if (begin >= endExclusive) {
        throw std::runtime_error(
            "User code range must be non-empty"
        );
    }

    if (has_binary_program_) {
        const std::size_t codeEnd =
            binary_code_base_ + binary_code_size_;

        if (
            begin < binary_code_base_
            || endExclusive > codeEnd
        ) {
            throw std::runtime_error(
                "User code range is outside loaded binary code"
            );
        }

        if (
            (begin - binary_code_base_)
                % binary::kInstructionSize != 0
            || (endExclusive - binary_code_base_)
                % binary::kInstructionSize != 0
        ) {
            throw std::runtime_error(
                "User binary code range must be "
                "instruction-aligned"
            );
        }
    } else {
        if (
            program_.empty()
            || endExclusive > program_.size()
        ) {
            throw std::runtime_error(
                "User code range is outside loaded program"
            );
        }
    }

    has_user_code_range_ = true;
    user_code_begin_ = begin;
    user_code_end_exclusive_ = endExclusive;
}

bool CPU::hasUserCodeRange() const {
    return has_user_code_range_;
}

std::size_t CPU::userCodeBegin() const {
    return user_code_begin_;
}

std::size_t CPU::userCodeEndExclusive() const {
    return user_code_end_exclusive_;
}

void CPU::setKernelStackPointer(std::size_t value) {
    if (using_kernel_interrupt_stack_) {
        throw std::runtime_error(
            "Cannot change Kernel stack pointer while active"
        );
    }

    requireStackPointerInRange(
        value,
        memory_map::kKernelStackBase,
        memory_map::kKernelStackEndExclusive
    );

    kernel_stack_pointer_ = value;
}

std::size_t CPU::kernelStackPointer() const {
    return kernel_stack_pointer_;
}

bool CPU::usingKernelInterruptStack() const {
    return using_kernel_interrupt_stack_;
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
    pushInterruptFrame(returnAddress);

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

void CPU::requireDataMemoryAccess(
    std::size_t address,
    const char* operation
) const {
    if (!state_.isUserMode()) {
        return;
    }

    constexpr std::size_t kDataAccessSize =
        sizeof(std::int64_t);

    if (
        memory_map::isUserDataRange(
            address,
            kDataAccessSize
        )
    ) {
        return;
    }

    throw std::runtime_error(
        "Memory protection violation: User mode cannot "
        + std::string(operation)
        + " address "
        + std::to_string(address)
    );
}

bool CPU::isLoadedCodeAddress(
    std::size_t address
) const {
    if (has_binary_program_) {
        return isBinaryPcInCode(address);
    }

    return address < program_.size();
}

void CPU::requireExecutionAddress(
    std::size_t address,
    PrivilegeLevel privilege
) const {
    if (privilege != PrivilegeLevel::User) {
        return;
    }

    const bool insideUserRange =
        has_user_code_range_
        && address >= user_code_begin_
        && address < user_code_end_exclusive_;

    if (
        insideUserRange
        && isLoadedCodeAddress(address)
    ) {
        return;
    }

    throw std::runtime_error(
        "Execution protection violation: User mode "
        "cannot execute address "
        + std::to_string(address)
    );
}

void CPU::requireCurrentPcExecutable() const {
    requireExecutionAddress(
        state_.pc(),
        state_.privilegeLevel()
    );
}

void CPU::setPcForExecution(std::size_t address) {
    requireExecutionAddress(
        address,
        state_.privilegeLevel()
    );
    state_.setPc(address);
}

std::int64_t CPU::readDataMemory(std::size_t address) {
    requireDataMemoryAccess(address, "read");

    if (mmio_bus_ && mmio_bus_->hasDeviceAt(address)) {
        return mmio_bus_->read(address);
    }

    return state_.memory().read(address);
}

void CPU::writeDataMemory(std::size_t address, std::int64_t value) {
    requireDataMemoryAccess(address, "write");

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

Instruction CPU::traceInstructionForCurrentBinaryPc() const {
    const std::size_t pc = state_.pc();

    if (!isBinaryPcInCode(pc)) {
        throw std::runtime_error("Binary PC is outside loaded code section");
    }

    const std::vector<std::uint8_t> instructionBytes =
        state_.memory().readBytes(pc, binary::kInstructionSize);

    InstructionDecoder decoder;
    const DecodedInstruction decoded =
        decoder.decodeInstruction(instructionBytes);

    const Operand dst =
        traceOperandFromEncoded(
            decoded.dst_type,
            decoded.dst_payload
        );

    const Operand src =
        traceOperandFromEncoded(
            decoded.src_type,
            decoded.src_payload
        );

    if (dst.isNone()) {
        return Instruction(decoded.opcode);
    }

    if (src.isNone()) {
        return Instruction(decoded.opcode, dst);
    }

    return Instruction(decoded.opcode, dst, src);
}

Operand CPU::traceOperandFromEncoded(
    EncodedOperandType type,
    std::int64_t payload
) const {
    switch (type) {
    case EncodedOperandType::None:
        return Operand::none();

    case EncodedOperandType::Register:
        return Operand::registerOperand(decodeBinaryRegister(payload));

    case EncodedOperandType::Immediate:
        return Operand::immediate(payload);

    case EncodedOperandType::MemoryAddress:
        if (payload < 0) {
            throw std::runtime_error("Negative binary memory address");
        }

        return Operand::memoryAddress(static_cast<std::size_t>(payload));

    case EncodedOperandType::RegisterIndirectAddress:
        return Operand::registerIndirectAddress(decodeBinaryRegister(payload));

    case EncodedOperandType::CodeAddress:
        if (payload < 0) {
            throw std::runtime_error("Negative binary code address");
        }

        return Operand::immediate(
            binary_code_base_ + static_cast<std::size_t>(payload)
        );

    default:
        throw std::runtime_error("Unknown encoded operand type");
    }
}

void CPU::executeBinaryInstruction(
    const DecodedInstruction& instruction
) {
    requireInstructionPrivilege(instruction.opcode);

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

        setPcForExecution(target);
        break;
    }

    case Opcode::JE: {
        requireSingleBinaryOperand(instruction);

        if (state_.flags().zero()) {
            setPcForExecution(
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
            setPcForExecution(
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

        if (signedGreaterThanFromFlags(state_.flags())) {
            setPcForExecution(
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

        if (signedLessThanFromFlags(state_.flags())) {
            setPcForExecution(
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

        requireExecutionAddress(
            target,
            state_.privilegeLevel()
        );
        requireExecutionAddress(
            returnAddress,
            state_.privilegeLevel()
        );

        pushValue(static_cast<std::int64_t>(returnAddress));
        state_.setPc(target);
        break;
    }

    case Opcode::RET: {
        requireNoBinaryOperands(instruction);

        const std::int64_t returnAddressValue =
            peekValue();

        const std::size_t returnAddress =
            checkedReturnAddress(
                returnAddressValue,
                "Negative binary return address"
            );

        requireExecutionAddress(
            returnAddress,
            state_.privilegeLevel()
        );

        (void)popValue();
        state_.setPc(returnAddress);
        break;
    }

    case Opcode::IRET: {
        requireNoBinaryOperands(instruction);
        restoreInterruptFrame(
            "Negative binary interrupt return address"
        );
        break;
    }

    case Opcode::EI: {
        requireNoBinaryOperands(instruction);

        if (!interrupt_controller_) {
            throw std::runtime_error("EI requires interrupt controller");
        }

        interrupt_controller_->setGlobalEnabled(true);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::DI: {
        requireNoBinaryOperands(instruction);

        if (!interrupt_controller_) {
            throw std::runtime_error("DI requires interrupt controller");
        }

        interrupt_controller_->setGlobalEnabled(false);
        advanceBinaryPcUnlessHalted();
        break;
    }

    case Opcode::INT: {
        requireSingleBinaryOperand(instruction);

        if (!interrupt_controller_) {
            throw std::runtime_error("INT requires interrupt controller");
        }

        const std::int64_t vectorValue =
            readBinaryOperandValue(
                instruction.dst_type,
                instruction.dst_payload
            );

        if (vectorValue < 0 || vectorValue > 255) {
            throw std::runtime_error("INT vector must be in range 0..255");
        }

        const std::uint8_t vector =
            static_cast<std::uint8_t>(vectorValue);

        const std::size_t handlerAddress =
            interrupt_controller_->vectorHandler(vector);

        if (has_binary_program_ && !isBinaryPcInCode(handlerAddress)) {
            throw std::runtime_error(
                "INT handler is outside loaded binary code section"
            );
        }

        const std::size_t returnAddress =
            state_.pc() + binary::kInstructionSize;

        pushInterruptFrame(returnAddress);

        state_.registers().set(
            RegisterName::R0,
            static_cast<std::int64_t>(vector)
        );
        state_.setPc(handlerAddress);
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
    case EncodedOperandType::RegisterIndirectAddress:
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
) {
    if (type == EncodedOperandType::MemoryAddress) {
        if (payload < 0) {
            throw std::runtime_error("Negative binary memory address");
        }

        return static_cast<std::size_t>(payload);
    }

    if (type == EncodedOperandType::RegisterIndirectAddress) {
        const RegisterName baseRegister = decodeBinaryRegister(payload);
        const std::int64_t address = state_.registers().get(baseRegister);

        if (address < 0) {
            throw std::runtime_error(
                "Negative binary register-indirect memory address"
            );
        }

        return static_cast<std::size_t>(address);
    }

    throw std::runtime_error("Binary operand must be memory address");
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

void CPU::requireInstructionPrivilege(
    Opcode opcode
) const {
    if (!state_.isUserMode()) {
        return;
    }

    switch (opcode) {
    case Opcode::HALT:
    case Opcode::IRET:
    case Opcode::EI:
    case Opcode::DI:
        throw std::runtime_error(
            "Privilege violation: "
            + opcodeToString(opcode)
            + " requires Kernel mode"
        );

    default:
        return;
    }
}

void CPU::execute(const Instruction& instruction) {
    requireInstructionPrivilege(instruction.opcode());

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

    case Opcode::EI:
        executeEi(instruction);
        break;

    case Opcode::DI:
        executeDi(instruction);
        break;

    case Opcode::INT:
        executeInt(instruction);
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

    const std::size_t address = readMemoryAddress(instruction.src());
    const std::int64_t value = readDataMemory(address);

    writeRegisterDestination(instruction.dst(), value);
    state_.flags().updateZeroAndSign(value);

    advancePcUnlessHalted();
}

void CPU::executeStore(const Instruction& instruction) {
    requireTwoOperands(instruction);

    const std::size_t address = readMemoryAddress(instruction.dst());
    const std::int64_t value = readOperandValue(instruction.src());
    writeDataMemory(address, value);

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
    setPcForExecution(
        resolveLabelAddress(instruction.dst())
    );
}

void CPU::executeJe(const Instruction& instruction) {
    requireSingleOperand(instruction);

    if (state_.flags().zero()) {
        setPcForExecution(
            resolveLabelAddress(instruction.dst())
        );
    } else {
        advancePcUnlessHalted();
    }
}

void CPU::executeJne(const Instruction& instruction) {
    requireSingleOperand(instruction);

    if (!state_.flags().zero()) {
        setPcForExecution(
            resolveLabelAddress(instruction.dst())
        );
    } else {
        advancePcUnlessHalted();
    }
}

void CPU::executeJg(const Instruction& instruction) {
    requireSingleOperand(instruction);

    if (signedGreaterThanFromFlags(state_.flags())) {
        setPcForExecution(
            resolveLabelAddress(instruction.dst())
        );
    } else {
        advancePcUnlessHalted();
    }
}

void CPU::executeJl(const Instruction& instruction) {
    requireSingleOperand(instruction);

    if (signedLessThanFromFlags(state_.flags())) {
        setPcForExecution(
            resolveLabelAddress(instruction.dst())
        );
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

    const std::size_t target =
        resolveLabelAddress(instruction.dst());
    const std::size_t returnAddress = state_.pc() + 1;

    requireExecutionAddress(
        target,
        state_.privilegeLevel()
    );
    requireExecutionAddress(
        returnAddress,
        state_.privilegeLevel()
    );

    pushValue(static_cast<std::int64_t>(returnAddress));
    state_.setPc(target);
}

void CPU::executeRet(const Instruction& instruction) {
    requireNoOperand(instruction);

    const std::int64_t returnAddressValue =
        peekValue();

    const std::size_t returnAddress =
        checkedReturnAddress(
            returnAddressValue,
            "Negative return address"
        );

    requireExecutionAddress(
        returnAddress,
        state_.privilegeLevel()
    );

    (void)popValue();
    state_.setPc(returnAddress);
}

void CPU::executeIret(const Instruction& instruction) {
    requireNoOperand(instruction);
    restoreInterruptFrame(
        "Negative interrupt return address"
    );
}

void CPU::executeEi(const Instruction& instruction) {
    requireNoOperand(instruction);

    if (!interrupt_controller_) {
        throw std::runtime_error("EI requires interrupt controller");
    }

    interrupt_controller_->setGlobalEnabled(true);
    advancePcUnlessHalted();
}

void CPU::executeDi(const Instruction& instruction) {
    requireNoOperand(instruction);

    if (!interrupt_controller_) {
        throw std::runtime_error("DI requires interrupt controller");
    }

    interrupt_controller_->setGlobalEnabled(false);
    advancePcUnlessHalted();
}

void CPU::executeInt(const Instruction& instruction) {
    requireSingleOperand(instruction);

    if (!interrupt_controller_) {
        throw std::runtime_error("INT requires interrupt controller");
    }

    const std::int64_t vectorValue = readOperandValue(instruction.dst());

    if (vectorValue < 0 || vectorValue > 255) {
        throw std::runtime_error("INT vector must be in range 0..255");
    }

    const std::uint8_t vector =
        static_cast<std::uint8_t>(vectorValue);

    const std::size_t handlerAddress =
        interrupt_controller_->vectorHandler(vector);

    if (handlerAddress >= program_.size()) {
        throw std::runtime_error(
            "INT handler is outside loaded program"
        );
    }

    const std::size_t returnAddress = state_.pc() + 1;

    pushInterruptFrame(returnAddress);

    state_.registers().set(
        RegisterName::R0,
        static_cast<std::int64_t>(vector)
    );
    state_.setPc(handlerAddress);
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
    case OperandType::RegisterIndirectAddress:
        return readDataMemory(readMemoryAddress(operand));

    default:
        throw std::runtime_error("Operand cannot be read as a value");
    }
}

std::size_t CPU::readMemoryAddress(const Operand& operand) {
    if (operand.type() == OperandType::MemoryAddress) {
        return operand.asMemoryAddress();
    }

    if (operand.type() == OperandType::RegisterIndirectAddress) {
        const std::int64_t address =
            state_.registers().get(operand.asRegisterIndirectBase());

        if (address < 0) {
            throw std::runtime_error(
                "Negative register-indirect memory address"
            );
        }

        return static_cast<std::size_t>(address);
    }

    throw std::runtime_error("Operand must be memory address");
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

std::size_t CPU::activeStackBase() const {
    if (state_.isUserMode()) {
        return memory_map::kUserStackBase;
    }

    if (using_kernel_interrupt_stack_) {
        return memory_map::kKernelStackBase;
    }

    return CPUState::kDefaultStackBase;
}

std::size_t CPU::activeStackEndExclusive() const {
    if (state_.isUserMode()) {
        return memory_map::kUserStackEndExclusive;
    }

    if (using_kernel_interrupt_stack_) {
        return memory_map::kKernelStackEndExclusive;
    }

    return state_.memory().size();
}

void CPU::requireStackPointerInRange(
    std::size_t sp,
    std::size_t stackBase,
    std::size_t stackEndExclusive
) const {
    if (sp < stackBase) {
        throw std::runtime_error(
            "Stack pointer is below stack base"
        );
    }

    if ((sp - stackBase) % kStackSlotSize != 0) {
        throw std::runtime_error(
            "Stack pointer is not slot-aligned"
        );
    }

    if (sp > stackEndExclusive) {
        throw std::runtime_error(
            "Stack pointer is outside memory"
        );
    }
}

void CPU::requireStackPushSlotsAt(
    std::size_t sp,
    std::size_t stackBase,
    std::size_t stackEndExclusive,
    std::size_t slotCount
) const {
    requireStackPointerInRange(
        sp,
        stackBase,
        stackEndExclusive
    );

    const std::size_t availableSlots =
        (stackEndExclusive - sp) / kStackSlotSize;

    if (slotCount > availableSlots) {
        throw std::runtime_error("Stack overflow");
    }
}

void CPU::requireStackPopSlotsAt(
    std::size_t sp,
    std::size_t stackBase,
    std::size_t stackEndExclusive,
    std::size_t slotCount
) const {
    requireStackPointerInRange(
        sp,
        stackBase,
        stackEndExclusive
    );

    const std::size_t usedSlots =
        (sp - stackBase) / kStackSlotSize;

    if (slotCount > usedSlots) {
        throw std::runtime_error("Stack underflow");
    }
}

void CPU::requireStackPushSlots(
    std::size_t slotCount
) const {
    requireStackPushSlotsAt(
        state_.sp(),
        activeStackBase(),
        activeStackEndExclusive(),
        slotCount
    );
}

void CPU::requireStackPopSlots(
    std::size_t slotCount
) const {
    requireStackPopSlotsAt(
        state_.sp(),
        activeStackBase(),
        activeStackEndExclusive(),
        slotCount
    );
}

void CPU::pushValue(std::int64_t value) {
    requireStackPushSlots(1);

    const std::size_t sp = state_.sp();
    state_.memory().write(sp, value);
    state_.setSp(sp + kStackSlotSize);

    if (using_kernel_interrupt_stack_) {
        kernel_stack_pointer_ = state_.sp();
    }
}

std::int64_t CPU::popValue() {
    requireStackPopSlots(1);

    const std::size_t newSp =
        state_.sp() - kStackSlotSize;
    const std::int64_t value =
        state_.memory().read(newSp);

    state_.setSp(newSp);

    if (using_kernel_interrupt_stack_) {
        kernel_stack_pointer_ = newSp;
    }

    return value;
}

std::int64_t CPU::peekValue() const {
    requireStackPopSlots(1);

    return state_.memory().read(
        state_.sp() - kStackSlotSize
    );
}

void CPU::pushInterruptFrame(std::size_t returnAddress) {
    const PrivilegeLevel interruptedPrivilege =
        state_.privilegeLevel();
    const std::size_t interruptedSp = state_.sp();

    std::size_t frameBase = interruptedSp;

    if (interruptedPrivilege == PrivilegeLevel::User) {
        requireStackPointerInRange(
            interruptedSp,
            memory_map::kUserStackBase,
            memory_map::kUserStackEndExclusive
        );

        frameBase = kernel_stack_pointer_;

        requireStackPushSlotsAt(
            frameBase,
            memory_map::kKernelStackBase,
            memory_map::kKernelStackEndExclusive,
            kInterruptFrameSlotCount
        );
    } else {
        requireStackPushSlots(kInterruptFrameSlotCount);
    }

    const std::int64_t savedPrivilege =
        privilegeLevelToRaw(interruptedPrivilege);

    state_.memory().write(
        frameBase,
        static_cast<std::int64_t>(returnAddress)
    );
    state_.memory().write(
        frameBase + kStackSlotSize,
        packFlags(state_.flags())
    );
    state_.memory().write(
        frameBase + kStackSlotSize * 2,
        savedPrivilege
    );
    state_.memory().write(
        frameBase + kStackSlotSize * 3,
        static_cast<std::int64_t>(interruptedSp)
    );

    state_.setSp(frameBase + kInterruptFrameSize);
    state_.setPrivilegeLevel(PrivilegeLevel::Kernel);

    if (interruptedPrivilege == PrivilegeLevel::User) {
        using_kernel_interrupt_stack_ = true;
    }

    if (using_kernel_interrupt_stack_) {
        kernel_stack_pointer_ = state_.sp();
    }
}

void CPU::restoreInterruptFrame(
    const char* returnAddressError
) {
    requireStackPopSlots(kInterruptFrameSlotCount);

    const std::size_t sp = state_.sp();
    const std::size_t frameBase =
        sp - kInterruptFrameSize;

    const std::size_t returnAddressAddress =
        frameBase;
    const std::size_t flagsAddress =
        frameBase + kStackSlotSize;
    const std::size_t privilegeAddress =
        frameBase + kStackSlotSize * 2;
    const std::size_t savedSpAddress =
        frameBase + kStackSlotSize * 3;

    const std::int64_t returnAddressValue =
        state_.memory().read(returnAddressAddress);
    const std::int64_t flagsValue =
        state_.memory().read(flagsAddress);
    const std::int64_t privilegeValue =
        state_.memory().read(privilegeAddress);
    const std::int64_t savedSpValue =
        state_.memory().read(savedSpAddress);

    const PrivilegeLevel restoredPrivilege =
        privilegeLevelFromRaw(privilegeValue);

    const std::size_t restoredPc =
        checkedReturnAddress(
            returnAddressValue,
            returnAddressError
        );

    if (savedSpValue < 0) {
        throw std::runtime_error(
            "Negative saved stack pointer"
        );
    }

    const std::size_t restoredSp =
        static_cast<std::size_t>(savedSpValue);

    requireExecutionAddress(
        restoredPc,
        restoredPrivilege
    );

    if (restoredPrivilege == PrivilegeLevel::User) {
        requireStackPointerInRange(
            restoredSp,
            memory_map::kUserStackBase,
            memory_map::kUserStackEndExclusive
        );
    } else if (using_kernel_interrupt_stack_) {
        requireStackPointerInRange(
            restoredSp,
            memory_map::kKernelStackBase,
            memory_map::kKernelStackEndExclusive
        );
    } else {
        requireStackPointerInRange(
            restoredSp,
            CPUState::kDefaultStackBase,
            state_.memory().size()
        );
    }

    restoreFlags(state_.flags(), flagsValue);
    state_.setPrivilegeLevel(restoredPrivilege);
    state_.setPc(restoredPc);

    if (restoredPrivilege == PrivilegeLevel::User) {
        if (using_kernel_interrupt_stack_) {
            kernel_stack_pointer_ = frameBase;
        }

        using_kernel_interrupt_stack_ = false;
        state_.setSp(restoredSp);
        return;
    }

    state_.setSp(restoredSp);

    if (using_kernel_interrupt_stack_) {
        kernel_stack_pointer_ = restoredSp;
    }
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