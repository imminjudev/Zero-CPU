#pragma once

#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/RegisterFile.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace zero_cpu::debug {

enum class DebugConditionSourceKind {
    Register,
    ProgramCounter,
    StackPointer,
    FlagsRaw,
    ZeroFlag,
    SignFlag,
    CarryFlag,
    OverflowFlag,
    MemoryQword
};

enum class DebugComparisonOperator {
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual
};

const char* debugComparisonOperatorToString(
    DebugComparisonOperator operation
);

struct DebugCondition {
    DebugConditionSourceKind source_kind =
        DebugConditionSourceKind::Register;

    RegisterName register_name =
        RegisterName::R0;

    std::size_t memory_address = 0;

    DebugComparisonOperator operation =
        DebugComparisonOperator::Equal;

    std::int64_t expected_value = 0;

    std::string source_text;
    std::string expression;

    std::int64_t actualValue(
        const CPU& cpu
    ) const;

    bool evaluate(
        const CPU& cpu
    ) const;

    void validateForCPU(
        const CPU& cpu
    ) const;
};

DebugCondition parseDebugCondition(
    const std::string& source,
    const std::string& operation,
    const std::string& value
);

} // namespace zero_cpu::debug
