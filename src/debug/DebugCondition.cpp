#include "zero_cpu/debug/DebugCondition.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>

namespace zero_cpu::debug {
namespace {

std::string upperCopy(
    const std::string& text
) {
    std::string result = text;

    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](
            unsigned char character
        ) {
            return static_cast<char>(
                std::toupper(character)
            );
        }
    );

    return result;
}

std::int64_t parseSignedValue(
    const std::string& text
) {
    if (text.empty()) {
        throw std::runtime_error(
            "Condition value is empty"
        );
    }

    try {
        std::size_t parsed = 0;

        const long long value =
            std::stoll(
                text,
                &parsed,
                0
            );

        if (parsed != text.size()) {
            throw std::runtime_error(
                "Condition value must be an integer"
            );
        }

        return static_cast<std::int64_t>(
            value
        );
    } catch (const std::runtime_error&) {
        throw;
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Condition value must be an integer"
        );
    }
}

std::size_t parseAddress(
    const std::string& text
) {
    if (
        text.empty()
        || text.front() == '-'
    ) {
        throw std::runtime_error(
            "Condition memory address must be "
            "a non-negative integer"
        );
    }

    try {
        std::size_t parsed = 0;

        const unsigned long long value =
            std::stoull(
                text,
                &parsed,
                0
            );

        if (
            parsed != text.size()
            || value
                > static_cast<unsigned long long>(
                    std::numeric_limits<
                        std::size_t
                    >::max()
                )
        ) {
            throw std::runtime_error(
                "Condition memory address must be "
                "a non-negative integer"
            );
        }

        return static_cast<std::size_t>(
            value
        );
    } catch (const std::runtime_error&) {
        throw;
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Condition memory address must be "
            "a non-negative integer"
        );
    }
}

DebugComparisonOperator parseOperator(
    const std::string& text
) {
    if (text == "==") {
        return DebugComparisonOperator::Equal;
    }

    if (text == "!=") {
        return DebugComparisonOperator::NotEqual;
    }

    if (text == "<") {
        return DebugComparisonOperator::Less;
    }

    if (text == "<=") {
        return DebugComparisonOperator::LessEqual;
    }

    if (text == ">") {
        return DebugComparisonOperator::Greater;
    }

    if (text == ">=") {
        return DebugComparisonOperator::GreaterEqual;
    }

    throw std::runtime_error(
        "Condition operator must be one of "
        "==, !=, <, <=, >, >="
    );
}

bool parseRegister(
    const std::string& source,
    RegisterName& registerName
) {
    if (
        source.size() < 2
        || source.front() != 'R'
    ) {
        return false;
    }

    const std::string indexText =
        source.substr(1);

    if (
        indexText.empty()
        || indexText.find_first_not_of(
            "0123456789"
        ) != std::string::npos
    ) {
        return false;
    }

    std::size_t parsed = 0;

    const unsigned long index =
        std::stoul(
            indexText,
            &parsed,
            10
        );

    if (
        parsed != indexText.size()
        || index
            >= RegisterFile::kRegisterCount
    ) {
        throw std::runtime_error(
            "Condition register is out of range"
        );
    }

    registerName =
        static_cast<RegisterName>(index);

    return true;
}

} // namespace

const char* debugComparisonOperatorToString(
    DebugComparisonOperator operation
) {
    switch (operation) {
    case DebugComparisonOperator::Equal:
        return "==";

    case DebugComparisonOperator::NotEqual:
        return "!=";

    case DebugComparisonOperator::Less:
        return "<";

    case DebugComparisonOperator::LessEqual:
        return "<=";

    case DebugComparisonOperator::Greater:
        return ">";

    case DebugComparisonOperator::GreaterEqual:
        return ">=";
    }

    throw std::runtime_error(
        "Invalid debug comparison operator"
    );
}

std::int64_t DebugCondition::actualValue(
    const CPU& cpu
) const {
    const CPUState& state =
        cpu.state();

    switch (source_kind) {
    case DebugConditionSourceKind::Register:
        return state.registers().get(
            register_name
        );

    case DebugConditionSourceKind::ProgramCounter:
        if (
            state.pc()
            > static_cast<std::size_t>(
                std::numeric_limits<
                    std::int64_t
                >::max()
            )
        ) {
            throw std::runtime_error(
                "PC does not fit in int64"
            );
        }

        return static_cast<std::int64_t>(
            state.pc()
        );

    case DebugConditionSourceKind::StackPointer:
        if (
            state.sp()
            > static_cast<std::size_t>(
                std::numeric_limits<
                    std::int64_t
                >::max()
            )
        ) {
            throw std::runtime_error(
                "SP does not fit in int64"
            );
        }

        return static_cast<std::int64_t>(
            state.sp()
        );

    case DebugConditionSourceKind::FlagsRaw:
        return static_cast<std::int64_t>(
            state.flags().raw()
        );

    case DebugConditionSourceKind::ZeroFlag:
        return state.flags().zero() ? 1 : 0;

    case DebugConditionSourceKind::SignFlag:
        return state.flags().sign() ? 1 : 0;

    case DebugConditionSourceKind::CarryFlag:
        return state.flags().carry() ? 1 : 0;

    case DebugConditionSourceKind::OverflowFlag:
        return state.flags().overflow() ? 1 : 0;

    case DebugConditionSourceKind::MemoryQword:
        validateForCPU(cpu);

        return state.memory().readI64(
            memory_address
        );
    }

    throw std::runtime_error(
        "Invalid debug condition source"
    );
}

bool DebugCondition::evaluate(
    const CPU& cpu
) const {
    const std::int64_t actual =
        actualValue(cpu);

    switch (operation) {
    case DebugComparisonOperator::Equal:
        return actual == expected_value;

    case DebugComparisonOperator::NotEqual:
        return actual != expected_value;

    case DebugComparisonOperator::Less:
        return actual < expected_value;

    case DebugComparisonOperator::LessEqual:
        return actual <= expected_value;

    case DebugComparisonOperator::Greater:
        return actual > expected_value;

    case DebugComparisonOperator::GreaterEqual:
        return actual >= expected_value;
    }

    throw std::runtime_error(
        "Invalid debug comparison operator"
    );
}

void DebugCondition::validateForCPU(
    const CPU& cpu
) const {
    if (
        source_kind
        != DebugConditionSourceKind::MemoryQword
    ) {
        return;
    }

    const std::size_t memorySize =
        cpu.state().memory().size();

    constexpr std::size_t qwordSize =
        sizeof(std::int64_t);

    if (
        memory_address >= memorySize
        || qwordSize
            > memorySize - memory_address
    ) {
        throw std::runtime_error(
            "Condition memory qword is outside "
            "process memory"
        );
    }
}

DebugCondition parseDebugCondition(
    const std::string& source,
    const std::string& operation,
    const std::string& value
) {
    if (source.empty()) {
        throw std::runtime_error(
            "Condition source is empty"
        );
    }

    DebugCondition condition;

    condition.operation =
        parseOperator(operation);

    condition.expected_value =
        parseSignedValue(value);

    const std::string upper =
        upperCopy(source);

    RegisterName registerName =
        RegisterName::R0;

    if (parseRegister(upper, registerName)) {
        condition.source_kind =
            DebugConditionSourceKind::Register;

        condition.register_name =
            registerName;

        condition.source_text = upper;
    } else if (upper == "PC") {
        condition.source_kind =
            DebugConditionSourceKind::ProgramCounter;

        condition.source_text = "PC";
    } else if (upper == "SP") {
        condition.source_kind =
            DebugConditionSourceKind::StackPointer;

        condition.source_text = "SP";
    } else if (upper == "FLAGS") {
        condition.source_kind =
            DebugConditionSourceKind::FlagsRaw;

        condition.source_text = "FLAGS";
    } else if (upper == "ZF") {
        condition.source_kind =
            DebugConditionSourceKind::ZeroFlag;

        condition.source_text = "ZF";
    } else if (upper == "SF") {
        condition.source_kind =
            DebugConditionSourceKind::SignFlag;

        condition.source_text = "SF";
    } else if (upper == "CF") {
        condition.source_kind =
            DebugConditionSourceKind::CarryFlag;

        condition.source_text = "CF";
    } else if (upper == "OF") {
        condition.source_kind =
            DebugConditionSourceKind::OverflowFlag;

        condition.source_text = "OF";
    } else {
        const std::string prefix =
            "MEMORY[";

        if (
            upper.size() <= prefix.size()
            || upper.rfind(prefix, 0) != 0
            || upper.back() != ']'
        ) {
            throw std::runtime_error(
                "Condition source must be R0-R7, "
                "PC, SP, FLAGS, ZF, SF, CF, OF, "
                "or memory[address]"
            );
        }

        const std::string addressText =
            source.substr(
                prefix.size(),
                source.size()
                    - prefix.size()
                    - 1
            );

        condition.source_kind =
            DebugConditionSourceKind::MemoryQword;

        condition.memory_address =
            parseAddress(addressText);

        condition.source_text =
            "memory["
            + std::to_string(
                condition.memory_address
            )
            + "]";
    }

    condition.expression =
        condition.source_text
        + " "
        + debugComparisonOperatorToString(
            condition.operation
        )
        + " "
        + std::to_string(
            condition.expected_value
        );

    return condition;
}

} // namespace zero_cpu::debug
