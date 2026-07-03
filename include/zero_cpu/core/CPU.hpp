#pragma once

#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/Operand.hpp"
#include "zero_cpu/trace/TraceLogger.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace zero_cpu {

class CPU {
public:
    using LabelTable = std::unordered_map<std::string, std::size_t>;

    CPU();

    void loadProgram(std::vector<Instruction> program);
    void loadProgram(std::vector<Instruction> program, LabelTable labels);

    void setLabels(LabelTable labels);

    void reset();

    CPUState& state();
    const CPUState& state() const;

    const std::vector<Instruction>& program() const;
    const LabelTable& labels() const;

    const TraceLogger& traceLogger() const;
    TraceLogger& traceLogger();

    bool step();
    void run(std::size_t max_steps = 100000);

private:
    CPUState state_;
    std::vector<Instruction> program_;
    LabelTable labels_;
    TraceLogger trace_logger_;

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

    void branchToLabel(const Operand& operand);
    void branchToLabelIf(const Operand& operand, bool condition);

    std::size_t resolveLabelAddress(const Operand& operand) const;

    std::int64_t readOperandValue(const Operand& operand) const;
    void writeOperandValue(const Operand& operand, std::int64_t value);

    void ensureStackCanPush() const;
    void ensureStackCanPop() const;

    void requireNoOperand(const Instruction& instruction) const;
    void requireDestination(const Instruction& instruction) const;
    void requireSource(const Instruction& instruction) const;

    void requireRegisterDestination(const Instruction& instruction) const;
    void requireMemoryDestination(const Instruction& instruction) const;
    void requireMemorySource(const Instruction& instruction) const;
    void requireLabelDestination(const Instruction& instruction) const;
    void requireSingleOperand(const Instruction& instruction) const;
};

} // namespace zero_cpu