#pragma once

#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/isa/EncodedInstruction.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/InstructionDecoder.hpp"
#include "zero_cpu/trace/TraceLogger.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace zero_cpu {

class CPU {
public:
    using LabelTable = std::unordered_map<std::string, std::size_t>;

    static constexpr std::size_t kStackSlotSize = 8;
    static constexpr std::size_t kDefaultBinaryCodeBase = 512;
    static constexpr std::size_t kDefaultMaxSteps = 100000;

    CPU();

    void reset();

    void loadProgram(
        const std::vector<Instruction>& program,
        const LabelTable& labels
    );

    void loadBinaryProgram(const binary::BinaryProgram& program);

    void step();
    void run(std::size_t maxSteps = kDefaultMaxSteps);

    CPUState& state();
    const CPUState& state() const;

    const std::vector<Instruction>& program() const;
    const LabelTable& labels() const;

    TraceLogger& traceLogger();
    const TraceLogger& traceLogger() const;

    bool hasBinaryProgram() const;
    std::size_t binaryCodeBase() const;
    std::size_t binaryEntryPoint() const;
    std::size_t binaryCodeSize() const;

    void setMMIOBus(std::shared_ptr<MMIOBus> bus);
    void clearMMIOBus();
    bool hasMMIOBus() const;

    void setInterruptController(
        std::shared_ptr<InterruptController> controller
    );
    void clearInterruptController();
    bool hasInterruptController() const;

private:
    CPUState state_;
    std::vector<Instruction> program_;
    LabelTable labels_;
    TraceLogger trace_logger_;

    bool has_binary_program_ = false;
    std::size_t binary_code_base_ = 0;
    std::size_t binary_entry_point_ = 0;
    std::size_t binary_code_size_ = 0;

    std::shared_ptr<MMIOBus> mmio_bus_;
    std::shared_ptr<InterruptController> interrupt_controller_;

    bool servicePendingInterruptIfNeeded();

    std::int64_t readDataMemory(std::size_t address);
    void writeDataMemory(std::size_t address, std::int64_t value);

    void stepBinary();
    bool isBinaryPcInCode(std::size_t pc) const;

    void executeBinaryInstruction(
        const DecodedInstruction& instruction
    );

    RegisterName decodeBinaryRegister(std::int64_t payload) const;

    std::int64_t readBinaryOperandValue(
        EncodedOperandType type,
        std::int64_t payload
    );

    void writeBinaryRegisterDestination(
        EncodedOperandType type,
        std::int64_t payload,
        std::int64_t value
    );

    std::size_t readBinaryMemoryAddress(
        EncodedOperandType type,
        std::int64_t payload
    ) const;

    std::size_t readBinaryCodeAddress(
        EncodedOperandType type,
        std::int64_t payload
    ) const;

    void requireNoBinaryOperands(
        const DecodedInstruction& instruction
    ) const;

    void requireSingleBinaryOperand(
        const DecodedInstruction& instruction
    ) const;

    void requireTwoBinaryOperands(
        const DecodedInstruction& instruction
    ) const;

    void requireBinaryRegisterDestination(
        EncodedOperandType type,
        std::int64_t payload
    ) const;

    void advanceBinaryPcUnlessHalted();

    void execute(const Instruction& instruction);

    void executeNop(const Instruction& instruction);
    void executeHalt(const Instruction& instruction);
    void executeMov(const Instruction& instruction);
    void executeLoad(const Instruction& instruction);
    void executeStore(const Instruction& instruction);
    void executeAdd(const Instruction& instruction);
    void executeSub(const Instruction& instruction);
    void executeMul(const Instruction& instruction);
    void executeDiv(const Instruction& instruction);
    void executeCmp(const Instruction& instruction);
    void executeJmp(const Instruction& instruction);
    void executeJe(const Instruction& instruction);
    void executeJne(const Instruction& instruction);
    void executeJg(const Instruction& instruction);
    void executeJl(const Instruction& instruction);
    void executePush(const Instruction& instruction);
    void executePop(const Instruction& instruction);
    void executeCall(const Instruction& instruction);
    void executeRet(const Instruction& instruction);
    void executeAnd(const Instruction& instruction);
    void executeOr(const Instruction& instruction);
    void executeXor(const Instruction& instruction);
    void executeNot(const Instruction& instruction);

    std::int64_t readOperandValue(const Operand& operand);

    void writeRegisterDestination(
        const Operand& operand,
        std::int64_t value
    );

    std::size_t resolveLabelAddress(const Operand& operand) const;

    void pushValue(std::int64_t value);
    std::int64_t popValue();

    void requireNoOperand(const Instruction& instruction) const;
    void requireSingleOperand(const Instruction& instruction) const;
    void requireTwoOperands(const Instruction& instruction) const;
    void requireRegisterDestination(const Operand& operand) const;

    void advancePcUnlessHalted();
    void setRuntimeError(const std::string& message);
};

} // namespace zero_cpu
