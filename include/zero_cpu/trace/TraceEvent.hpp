#pragma once

#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/isa/Instruction.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zero_cpu {

struct RegisterChange {
    std::string name;
    std::int64_t before;
    std::int64_t after;
};

struct FlagChange {
    std::string name;
    bool before;
    bool after;
};

struct MemoryChange {
    std::size_t address;
    std::int64_t before;
    std::int64_t after;
};

struct ALUTraceDetail {
    bool active = false;

    std::string operation;
    std::string destination;

    std::string lhs_text;
    std::string rhs_text;

    std::int64_t lhs = 0;
    std::int64_t rhs = 0;
    std::int64_t result = 0;

    bool has_lhs = false;
    bool has_rhs = false;
    bool has_result = false;

    std::string note;
};

class TraceEvent {
public:
    TraceEvent(
        CPUState before,
        Instruction instruction,
        CPUState after,
        std::string error_message = ""
    );

    const CPUState& before() const;
    const CPUState& after() const;
    const Instruction& instruction() const;

    std::size_t pcBefore() const;
    std::size_t pcAfter() const;

    const std::vector<RegisterChange>& changedRegisters() const;
    const std::vector<FlagChange>& changedFlags() const;
    const std::vector<MemoryChange>& changedMemory() const;

    const std::string& stage() const;
    const std::string& action() const;
    const std::vector<std::string>& datapathNodes() const;
    std::string datapathString() const;

    const ALUTraceDetail& aluDetail() const;
    std::string aluDetailString() const;

    bool hasError() const;
    const std::string& errorMessage() const;

    std::string toCompactString() const;
    std::string toFullString() const;

private:
    CPUState before_;
    Instruction instruction_;
    CPUState after_;

    std::vector<RegisterChange> changed_registers_;
    std::vector<FlagChange> changed_flags_;
    std::vector<MemoryChange> changed_memory_;

    std::string stage_;
    std::string action_;
    std::vector<std::string> datapath_nodes_;
    ALUTraceDetail alu_detail_;

    std::string error_message_;

    void analyzeChanges();

    void analyzeRegisterChanges();
    void analyzeFlagChanges();
    void analyzeMemoryChanges();
    void analyzeVisualMetadata();
    void analyzeAluDetail();

    void addDatapathNode(std::string node);

    static std::string boolToString(bool value);
    static std::string formatPc(std::size_t pc);
    static std::string joinDatapathNodes(
        const std::vector<std::string>& nodes
    );
};

} // namespace zero_cpu
