#include "zero_cpu/debug/DebugSnapshotJson.hpp"

#include "zero_cpu/core/PrivilegeLevel.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/kernel/ProcessLifecycleManager.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"
#include "zero_cpu/kernel/ProcessTermination.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace zero_cpu::debug {
namespace {

constexpr std::uint32_t kCarryMask =
    static_cast<std::uint32_t>(1u << 0);

constexpr std::uint32_t kZeroMask =
    static_cast<std::uint32_t>(1u << 6);

constexpr std::uint32_t kSignMask =
    static_cast<std::uint32_t>(1u << 7);

constexpr std::uint32_t kOverflowMask =
    static_cast<std::uint32_t>(1u << 11);

void indent(
    std::ostringstream& output,
    int spaces
) {
    for (int index = 0; index < spaces; ++index) {
        output << ' ';
    }
}

std::string escapeJson(
    const std::string& text
) {
    std::ostringstream output;

    for (const unsigned char character : text) {
        switch (character) {
        case '"':
            output << "\\\"";
            break;

        case '\\':
            output << "\\\\";
            break;

        case '\b':
            output << "\\b";
            break;

        case '\f':
            output << "\\f";
            break;

        case '\n':
            output << "\\n";
            break;

        case '\r':
            output << "\\r";
            break;

        case '\t':
            output << "\\t";
            break;

        default:
            if (character < 0x20) {
                output
                    << "\\u"
                    << std::hex
                    << std::setw(4)
                    << std::setfill('0')
                    << static_cast<unsigned int>(
                        character
                    )
                    << std::dec
                    << std::setfill(' ');
            } else {
                output
                    << static_cast<char>(
                        character
                    );
            }

            break;
        }
    }

    return output.str();
}

void finishField(
    std::ostringstream& output,
    bool comma
) {
    if (comma) {
        output << ",";
    }

    output << "\n";
}

void stringField(
    std::ostringstream& output,
    int fieldIndent,
    const std::string& name,
    const std::string& value,
    bool comma = true
) {
    indent(output, fieldIndent);

    output
        << "\""
        << escapeJson(name)
        << "\": \""
        << escapeJson(value)
        << "\"";

    finishField(output, comma);
}

void sizeField(
    std::ostringstream& output,
    int fieldIndent,
    const std::string& name,
    std::size_t value,
    bool comma = true
) {
    indent(output, fieldIndent);

    output
        << "\""
        << escapeJson(name)
        << "\": "
        << value;

    finishField(output, comma);
}

void uint64Field(
    std::ostringstream& output,
    int fieldIndent,
    const std::string& name,
    std::uint64_t value,
    bool comma = true
) {
    indent(output, fieldIndent);

    output
        << "\""
        << escapeJson(name)
        << "\": "
        << value;

    finishField(output, comma);
}

void int64Field(
    std::ostringstream& output,
    int fieldIndent,
    const std::string& name,
    std::int64_t value,
    bool comma = true
) {
    indent(output, fieldIndent);

    output
        << "\""
        << escapeJson(name)
        << "\": "
        << value;

    finishField(output, comma);
}

void boolField(
    std::ostringstream& output,
    int fieldIndent,
    const std::string& name,
    bool value,
    bool comma = true
) {
    indent(output, fieldIndent);

    output
        << "\""
        << escapeJson(name)
        << "\": "
        << (
            value
                ? "true"
                : "false"
        );

    finishField(output, comma);
}

void nullField(
    std::ostringstream& output,
    int fieldIndent,
    const std::string& name,
    bool comma = true
) {
    indent(output, fieldIndent);

    output
        << "\""
        << escapeJson(name)
        << "\": null";

    finishField(output, comma);
}

void appendRegisters(
    std::ostringstream& output,
    const RegisterFile& registers,
    int fieldIndent,
    bool comma
) {
    indent(output, fieldIndent);
    output << "\"registers\": {\n";

    for (
        std::size_t index = 0;
        index < RegisterFile::kRegisterCount;
        ++index
    ) {
        const RegisterName name =
            static_cast<RegisterName>(index);

        int64Field(
            output,
            fieldIndent + 2,
            RegisterFile::registerNameToString(
                name
            ),
            registers.get(name),
            index + 1
                < RegisterFile::kRegisterCount
        );
    }

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

void appendContextRegisters(
    std::ostringstream& output,
    const kernel::ProcessContext& context,
    int fieldIndent,
    bool comma
) {
    indent(output, fieldIndent);
    output << "\"registers\": {\n";

    for (
        std::size_t index = 0;
        index < context.registers.size();
        ++index
    ) {
        const RegisterName name =
            static_cast<RegisterName>(index);

        int64Field(
            output,
            fieldIndent + 2,
            RegisterFile::registerNameToString(
                name
            ),
            context.registers[index],
            index + 1
                < context.registers.size()
        );
    }

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

void appendFlags(
    std::ostringstream& output,
    std::uint32_t raw,
    int fieldIndent,
    bool comma
) {
    indent(output, fieldIndent);
    output << "\"flags\": {\n";

    sizeField(
        output,
        fieldIndent + 2,
        "raw",
        static_cast<std::size_t>(raw)
    );

    boolField(
        output,
        fieldIndent + 2,
        "zero",
        (raw & kZeroMask) != 0
    );

    boolField(
        output,
        fieldIndent + 2,
        "sign",
        (raw & kSignMask) != 0
    );

    boolField(
        output,
        fieldIndent + 2,
        "carry",
        (raw & kCarryMask) != 0
    );

    boolField(
        output,
        fieldIndent + 2,
        "overflow",
        (raw & kOverflowMask) != 0,
        false
    );

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

void appendCPU(
    std::ostringstream& output,
    const CPU& cpu,
    int fieldIndent,
    bool comma
) {
    const CPUState& state =
        cpu.state();

    indent(output, fieldIndent);
    output << "\"cpu\": {\n";

    sizeField(
        output,
        fieldIndent + 2,
        "pc",
        state.pc()
    );

    sizeField(
        output,
        fieldIndent + 2,
        "sp",
        state.sp()
    );

    stringField(
        output,
        fieldIndent + 2,
        "privilege",
        privilegeLevelToString(
            state.privilegeLevel()
        )
    );

    boolField(
        output,
        fieldIndent + 2,
        "halted",
        state.halted()
    );

    boolField(
        output,
        fieldIndent + 2,
        "has_error",
        state.hasError()
    );

    stringField(
        output,
        fieldIndent + 2,
        "error_message",
        state.errorMessage()
    );

    appendRegisters(
        output,
        state.registers(),
        fieldIndent + 2,
        true
    );

    appendFlags(
        output,
        state.flags().raw(),
        fieldIndent + 2,
        false
    );

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

void appendProcessContext(
    std::ostringstream& output,
    const kernel::ProcessContext& context,
    int fieldIndent,
    bool comma
) {
    indent(output, fieldIndent);
    output << "\"context\": {\n";

    sizeField(
        output,
        fieldIndent + 2,
        "pc",
        context.pc
    );

    sizeField(
        output,
        fieldIndent + 2,
        "sp",
        context.sp
    );

    stringField(
        output,
        fieldIndent + 2,
        "privilege",
        privilegeLevelToString(
            context.privilege
        )
    );

    sizeField(
        output,
        fieldIndent + 2,
        "kernel_stack_pointer",
        context.kernel_stack_pointer
    );

    boolField(
        output,
        fieldIndent + 2,
        "has_user_code_range",
        context.has_user_code_range
    );

    sizeField(
        output,
        fieldIndent + 2,
        "user_code_begin",
        context.user_code_begin
    );

    sizeField(
        output,
        fieldIndent + 2,
        "user_code_end_exclusive",
        context.user_code_end_exclusive
    );

    appendContextRegisters(
        output,
        context,
        fieldIndent + 2,
        true
    );

    appendFlags(
        output,
        context.flags,
        fieldIndent + 2,
        false
    );

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

void validateMemoryOptions(
    const Memory& memory,
    const DebugSnapshotOptions& options
) {
    if (!options.include_memory) {
        return;
    }

    if (options.memory_size == 0) {
        throw std::runtime_error(
            "Debug snapshot memory size must be "
            "greater than zero"
        );
    }

    if (
        options.memory_address >= memory.size()
        || options.memory_size
            > memory.size()
                - options.memory_address
    ) {
        throw std::runtime_error(
            "Debug snapshot memory range is "
            "outside process memory"
        );
    }
}

void appendMemory(
    std::ostringstream& output,
    const Memory& memory,
    const DebugSnapshotOptions& options,
    int fieldIndent,
    bool comma
) {
    validateMemoryOptions(
        memory,
        options
    );

    indent(output, fieldIndent);
    output << "\"memory\": {\n";

    boolField(
        output,
        fieldIndent + 2,
        "included",
        options.include_memory
    );

    sizeField(
        output,
        fieldIndent + 2,
        "address",
        options.include_memory
            ? options.memory_address
            : 0
    );

    sizeField(
        output,
        fieldIndent + 2,
        "size",
        options.include_memory
            ? options.memory_size
            : 0
    );

    indent(output, fieldIndent + 2);
    output << "\"bytes\": [";

    if (options.include_memory) {
        const std::vector<std::uint8_t> bytes =
            memory.readBytes(
                options.memory_address,
                options.memory_size
            );

        for (
            std::size_t index = 0;
            index < bytes.size();
            ++index
        ) {
            if (index != 0) {
                output << ", ";
            }

            output
                << static_cast<unsigned int>(
                    bytes[index]
                );
        }
    }

    output << "]\n";

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

void appendExecutable(
    std::ostringstream& output,
    const kernel::ExecutableMetadata& metadata,
    int fieldIndent,
    bool comma
) {
    indent(output, fieldIndent);
    output << "\"executable\": {\n";

    stringField(
        output,
        fieldIndent + 2,
        "source_name",
        metadata.source_name
    );

    sizeField(
        output,
        fieldIndent + 2,
        "memory_size",
        metadata.memory_size
    );

    sizeField(
        output,
        fieldIndent + 2,
        "code_base",
        metadata.code_base
    );

    sizeField(
        output,
        fieldIndent + 2,
        "code_size",
        metadata.code_size
    );

    sizeField(
        output,
        fieldIndent + 2,
        "code_end_exclusive",
        metadata.code_end_exclusive
    );

    sizeField(
        output,
        fieldIndent + 2,
        "entry_point",
        metadata.entry_point
    );

    sizeField(
        output,
        fieldIndent + 2,
        "data_base",
        metadata.data_base
    );

    sizeField(
        output,
        fieldIndent + 2,
        "data_size",
        metadata.data_size
    );

    sizeField(
        output,
        fieldIndent + 2,
        "user_stack_begin",
        metadata.user_stack_begin
    );

    sizeField(
        output,
        fieldIndent + 2,
        "user_stack_end_exclusive",
        metadata.user_stack_end_exclusive,
        false
    );

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

void appendProcessExecutable(
    std::ostringstream& output,
    const ProcessDebugSnapshot& snapshot,
    int fieldIndent,
    bool comma
) {
    indent(output, fieldIndent);
    output << "\"executable\": {\n";

    boolField(
        output,
        fieldIndent + 2,
        "available",
        snapshot.has_executable_image
    );

    sizeField(
        output,
        fieldIndent + 2,
        "code_base",
        snapshot.code_base
    );

    sizeField(
        output,
        fieldIndent + 2,
        "code_end_exclusive",
        snapshot.code_end_exclusive
    );

    sizeField(
        output,
        fieldIndent + 2,
        "entry_point",
        snapshot.entry_point
    );

    sizeField(
        output,
        fieldIndent + 2,
        "data_base",
        snapshot.data_base
    );

    sizeField(
        output,
        fieldIndent + 2,
        "data_size",
        snapshot.data_size,
        false
    );

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

void appendSingleStop(
    std::ostringstream& output,
    const DebugStop& stop,
    int fieldIndent,
    bool comma
) {
    indent(output, fieldIndent);
    output << "\"stop\": {\n";

    stringField(
        output,
        fieldIndent + 2,
        "reason",
        debugStopReasonToString(
            stop.reason
        )
    );

    sizeField(
        output,
        fieldIndent + 2,
        "pc",
        stop.pc
    );

    sizeField(
        output,
        fieldIndent + 2,
        "executed_steps",
        stop.executed_steps
    );

    sizeField(
        output,
        fieldIndent + 2,
        "total_steps",
        stop.total_steps
    );

    stringField(
        output,
        fieldIndent + 2,
        "message",
        stop.message
    );

    boolField(
        output,
        fieldIndent + 2,
        "has_conditional_breakpoint",
        stop.has_conditional_breakpoint
    );

    sizeField(
        output,
        fieldIndent + 2,
        "conditional_breakpoint_id",
        stop.conditional_breakpoint_id
    );

    stringField(
        output,
        fieldIndent + 2,
        "conditional_expression",
        stop.conditional_expression
    );

    int64Field(
        output,
        fieldIndent + 2,
        "conditional_actual_value",
        stop.conditional_actual_value
    );

    boolField(
        output,
        fieldIndent + 2,
        "has_watchpoint",
        stop.has_watchpoint
    );

    sizeField(
        output,
        fieldIndent + 2,
        "watchpoint_id",
        stop.watchpoint_id
    );

    stringField(
        output,
        fieldIndent + 2,
        "watchpoint_mode",
        memoryWatchModeToString(
            stop.watchpoint_mode
        )
    );

    sizeField(
        output,
        fieldIndent + 2,
        "watchpoint_address",
        stop.watchpoint_address
    );

    sizeField(
        output,
        fieldIndent + 2,
        "watchpoint_size",
        stop.watchpoint_size
    );

    stringField(
        output,
        fieldIndent + 2,
        "access_mode",
        memoryWatchModeToString(
            stop.access_mode
        )
    );

    sizeField(
        output,
        fieldIndent + 2,
        "access_address",
        stop.access_address,
        false
    );

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

void appendMultiStop(
    std::ostringstream& output,
    const MultiProcessDebugStop& stop,
    int fieldIndent,
    bool comma
) {
    indent(output, fieldIndent);
    output << "\"stop\": {\n";

    stringField(
        output,
        fieldIndent + 2,
        "reason",
        multiProcessDebugStopReasonToString(
            stop.reason
        )
    );

    stringField(
        output,
        fieldIndent + 2,
        "runtime_state",
        kernel::processRuntimeStateToString(
            stop.runtime_state
        )
    );

    sizeField(
        output,
        fieldIndent + 2,
        "executed_steps",
        stop.executed_steps
    );

    sizeField(
        output,
        fieldIndent + 2,
        "total_steps",
        stop.total_steps
    );

    sizeField(
        output,
        fieldIndent + 2,
        "running_pid",
        stop.running_pid
    );

    sizeField(
        output,
        fieldIndent + 2,
        "selected_pid",
        stop.selected_pid
    );

    boolField(
        output,
        fieldIndent + 2,
        "has_debug_hit",
        stop.has_debug_hit
    );

    sizeField(
        output,
        fieldIndent + 2,
        "hit_pid",
        stop.hit_pid
    );

    sizeField(
        output,
        fieldIndent + 2,
        "hit_address",
        stop.hit_address
    );

    boolField(
        output,
        fieldIndent + 2,
        "has_conditional_breakpoint",
        stop.has_conditional_breakpoint
    );

    sizeField(
        output,
        fieldIndent + 2,
        "conditional_breakpoint_id",
        stop.conditional_breakpoint_id
    );

    stringField(
        output,
        fieldIndent + 2,
        "conditional_expression",
        stop.conditional_expression
    );

    int64Field(
        output,
        fieldIndent + 2,
        "conditional_actual_value",
        stop.conditional_actual_value
    );

    boolField(
        output,
        fieldIndent + 2,
        "has_watchpoint",
        stop.has_watchpoint
    );

    sizeField(
        output,
        fieldIndent + 2,
        "watchpoint_id",
        stop.watchpoint_id
    );

    stringField(
        output,
        fieldIndent + 2,
        "watchpoint_mode",
        processMemoryWatchModeToString(
            stop.watchpoint_mode
        )
    );

    sizeField(
        output,
        fieldIndent + 2,
        "watchpoint_address",
        stop.watchpoint_address
    );

    sizeField(
        output,
        fieldIndent + 2,
        "watchpoint_size",
        stop.watchpoint_size
    );

    stringField(
        output,
        fieldIndent + 2,
        "access_mode",
        processMemoryWatchModeToString(
            stop.access_mode
        )
    );

    sizeField(
        output,
        fieldIndent + 2,
        "access_address",
        stop.access_address
    );

    boolField(
        output,
        fieldIndent + 2,
        "process_terminated",
        stop.process_terminated
    );

    boolField(
        output,
        fieldIndent + 2,
        "process_faulted",
        stop.process_faulted
    );

    sizeField(
        output,
        fieldIndent + 2,
        "terminated_pid",
        stop.terminated_pid
    );

    stringField(
        output,
        fieldIndent + 2,
        "message",
        stop.message,
        false
    );

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

// Patch: v1.2-snapshot-source-location-r2
void appendSourceLocation(
    std::ostringstream& output,
    const DebugSymbols* symbols,
    std::size_t address,
    int fieldIndent,
    bool comma
) {
    bool available = false;
    std::string path;
    std::size_t line = 0;

    if (
        symbols != nullptr
        && symbols->hasSourceLocations()
    ) {
        path = symbols->sourcePath();

        try {
            line =
                symbols->sourceLineForAddress(
                    address
                );

            available = true;
        } catch (const std::exception&) {
            // The executable can legitimately stop just
            // past its final mapped instruction.
            available = false;
        }
    }

    indent(output, fieldIndent);
    output << "\"source_location\": {\n";

    boolField(
        output,
        fieldIndent + 2,
        "available",
        available
    );

    stringField(
        output,
        fieldIndent + 2,
        "path",
        path
    );

    sizeField(
        output,
        fieldIndent + 2,
        "line",
        line
    );

    sizeField(
        output,
        fieldIndent + 2,
        "address",
        address,
        false
    );

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

void appendSingleBreakpoints(
    std::ostringstream& output,
    const DebugSession& session,
    int fieldIndent,
    bool comma
) {
    const std::vector<std::size_t> values =
        session.breakpoints();

    indent(output, fieldIndent);
    output << "\"breakpoints\": [";

    for (
        std::size_t index = 0;
        index < values.size();
        ++index
    ) {
        if (index != 0) {
            output << ", ";
        }

        output << values[index];
    }

    output << "]";

    finishField(output, comma);
}

void appendSingleConditionalBreakpoints(
    std::ostringstream& output,
    const DebugSession& session,
    int fieldIndent,
    bool comma
) {
    const std::vector<ConditionalBreakpoint>
        values =
            session.conditionalBreakpoints();

    indent(output, fieldIndent);
    output
        << "\"conditional_breakpoints\": [\n";

    for (
        std::size_t index = 0;
        index < values.size();
        ++index
    ) {
        const ConditionalBreakpoint& breakpoint =
            values[index];

        indent(output, fieldIndent + 2);
        output << "{\n";

        sizeField(
            output,
            fieldIndent + 4,
            "id",
            breakpoint.id
        );

        sizeField(
            output,
            fieldIndent + 4,
            "address",
            breakpoint.address
        );

        stringField(
            output,
            fieldIndent + 4,
            "expression",
            breakpoint.condition.expression,
            false
        );

        indent(output, fieldIndent + 2);
        output << "}";

        finishField(
            output,
            index + 1 < values.size()
        );
    }

    indent(output, fieldIndent);
    output << "]";

    finishField(output, comma);
}

void appendSingleWatchpoints(
    std::ostringstream& output,
    const DebugSession& session,
    int fieldIndent,
    bool comma
) {
    const std::vector<MemoryWatchpoint> values =
        session.watchpoints();

    indent(output, fieldIndent);
    output << "\"watchpoints\": [\n";

    for (
        std::size_t index = 0;
        index < values.size();
        ++index
    ) {
        const MemoryWatchpoint& watchpoint =
            values[index];

        indent(output, fieldIndent + 2);
        output << "{\n";

        sizeField(
            output,
            fieldIndent + 4,
            "id",
            watchpoint.id
        );

        stringField(
            output,
            fieldIndent + 4,
            "mode",
            memoryWatchModeToString(
                watchpoint.mode
            )
        );

        sizeField(
            output,
            fieldIndent + 4,
            "address",
            watchpoint.address
        );

        sizeField(
            output,
            fieldIndent + 4,
            "size",
            watchpoint.size,
            false
        );

        indent(output, fieldIndent + 2);
        output << "}";

        finishField(
            output,
            index + 1 < values.size()
        );
    }

    indent(output, fieldIndent);
    output << "]";

    finishField(output, comma);
}

void appendSymbolEntries(
    std::ostringstream& output,
    const DebugSymbols& symbols,
    int fieldIndent,
    bool comma
) {
    indent(output, fieldIndent);
    output << "\"entries\": [\n";

    const std::vector<DebugSymbol>& entries =
        symbols.entries();

    for (
        std::size_t index = 0;
        index < entries.size();
        ++index
    ) {
        const DebugSymbol& symbol =
            entries[index];

        indent(output, fieldIndent + 2);
        output << "{\n";

        stringField(
            output,
            fieldIndent + 4,
            "kind",
            debugSymbolKindToString(
                symbol.kind
            )
        );

        stringField(
            output,
            fieldIndent + 4,
            "name",
            symbol.name
        );

        sizeField(
            output,
            fieldIndent + 4,
            "address",
            symbol.address,
            false
        );

        indent(output, fieldIndent + 2);
        output << "}";

        finishField(
            output,
            index + 1 < entries.size()
        );
    }

    indent(output, fieldIndent);
    output << "]";

    finishField(output, comma);
}

void appendSingleSymbols(
    std::ostringstream& output,
    const DebugSession& session,
    const DebugSnapshotOptions& options,
    int fieldIndent,
    bool comma
) {
    indent(output, fieldIndent);
    output << "\"symbols\": {\n";

    const bool included =
        options.include_symbols
        && session.hasSymbols();

    boolField(
        output,
        fieldIndent + 2,
        "included",
        included
    );

    if (included) {
        appendSymbolEntries(
            output,
            session.symbols(),
            fieldIndent + 2,
            false
        );
    } else {
        indent(output, fieldIndent + 2);
        output << "\"entries\": []\n";
    }

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

void appendMultiBreakpoints(
    std::ostringstream& output,
    const MultiProcessDebugSession& session,
    int fieldIndent,
    bool comma
) {
    const std::vector<ProcessBreakpoint> values =
        session.breakpoints();

    indent(output, fieldIndent);
    output << "\"breakpoints\": [\n";

    for (
        std::size_t index = 0;
        index < values.size();
        ++index
    ) {
        indent(output, fieldIndent + 2);
        output << "{\n";

        sizeField(
            output,
            fieldIndent + 4,
            "pid",
            values[index].pid
        );

        sizeField(
            output,
            fieldIndent + 4,
            "address",
            values[index].address,
            false
        );

        indent(output, fieldIndent + 2);
        output << "}";

        finishField(
            output,
            index + 1 < values.size()
        );
    }

    indent(output, fieldIndent);
    output << "]";

    finishField(output, comma);
}

void appendMultiConditionalBreakpoints(
    std::ostringstream& output,
    const MultiProcessDebugSession& session,
    int fieldIndent,
    bool comma
) {
    const std::vector<
        ProcessConditionalBreakpoint
    > values =
        session.conditionalBreakpoints();

    indent(output, fieldIndent);
    output
        << "\"conditional_breakpoints\": [\n";

    for (
        std::size_t index = 0;
        index < values.size();
        ++index
    ) {
        const ProcessConditionalBreakpoint&
            breakpoint =
                values[index];

        indent(output, fieldIndent + 2);
        output << "{\n";

        sizeField(
            output,
            fieldIndent + 4,
            "id",
            breakpoint.id
        );

        sizeField(
            output,
            fieldIndent + 4,
            "pid",
            breakpoint.pid
        );

        sizeField(
            output,
            fieldIndent + 4,
            "address",
            breakpoint.address
        );

        stringField(
            output,
            fieldIndent + 4,
            "expression",
            breakpoint.condition.expression,
            false
        );

        indent(output, fieldIndent + 2);
        output << "}";

        finishField(
            output,
            index + 1 < values.size()
        );
    }

    indent(output, fieldIndent);
    output << "]";

    finishField(output, comma);
}

void appendMultiWatchpoints(
    std::ostringstream& output,
    const MultiProcessDebugSession& session,
    int fieldIndent,
    bool comma
) {
    const std::vector<
        ProcessMemoryWatchpoint
    > values =
        session.watchpoints();

    indent(output, fieldIndent);
    output << "\"watchpoints\": [\n";

    for (
        std::size_t index = 0;
        index < values.size();
        ++index
    ) {
        const ProcessMemoryWatchpoint& watchpoint =
            values[index];

        indent(output, fieldIndent + 2);
        output << "{\n";

        sizeField(
            output,
            fieldIndent + 4,
            "id",
            watchpoint.id
        );

        sizeField(
            output,
            fieldIndent + 4,
            "pid",
            watchpoint.pid
        );

        stringField(
            output,
            fieldIndent + 4,
            "mode",
            processMemoryWatchModeToString(
                watchpoint.mode
            )
        );

        sizeField(
            output,
            fieldIndent + 4,
            "address",
            watchpoint.address
        );

        sizeField(
            output,
            fieldIndent + 4,
            "size",
            watchpoint.size,
            false
        );

        indent(output, fieldIndent + 2);
        output << "}";

        finishField(
            output,
            index + 1 < values.size()
        );
    }

    indent(output, fieldIndent);
    output << "]";

    finishField(output, comma);
}

void appendProcess(
    std::ostringstream& output,
    const ProcessDebugSnapshot& snapshot,
    const DebugSymbols* symbols,
    const DebugSnapshotOptions& options,
    int fieldIndent,
    bool comma
) {
    indent(output, fieldIndent);
    output << "{\n";

    sizeField(
        output,
        fieldIndent + 2,
        "pid",
        snapshot.pid
    );

    stringField(
        output,
        fieldIndent + 2,
        "source_name",
        snapshot.source_name
    );

    stringField(
        output,
        fieldIndent + 2,
        "state",
        kernel::processStateToString(
            snapshot.state
        )
    );

    boolField(
        output,
        fieldIndent + 2,
        "running",
        snapshot.running
    );

    boolField(
        output,
        fieldIndent + 2,
        "terminated",
        snapshot.terminated()
    );

    boolField(
        output,
        fieldIndent + 2,
        "faulted",
        snapshot.faulted()
    );

    boolField(
        output,
        fieldIndent + 2,
        "has_exit_code",
        snapshot.has_exit_code
    );

    int64Field(
        output,
        fieldIndent + 2,
        "exit_code",
        snapshot.exit_code
    );

    stringField(
        output,
        fieldIndent + 2,
        "termination_kind",
        kernel::processTerminationKindToString(
            snapshot.termination_kind
        )
    );

    stringField(
        output,
        fieldIndent + 2,
        "termination_message",
        snapshot.termination_message
    );

    appendProcessContext(
        output,
        snapshot.context,
        fieldIndent + 2,
        true
    );

    appendProcessExecutable(
        output,
        snapshot,
        fieldIndent + 2,
        true
    );

    appendSourceLocation(
        output,
        symbols,
        snapshot.context.pc,
        fieldIndent + 2,
        true
    );

    appendMemory(
        output,
        snapshot.memory,
        options,
        fieldIndent + 2,
        false
    );

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

void appendProcesses(
    std::ostringstream& output,
    const MultiProcessDebugSession& session,
    const DebugSnapshotOptions& options,
    int fieldIndent,
    bool comma
) {
    const std::vector<ProcessDebugSnapshot> values =
        session.processSnapshots();

    indent(output, fieldIndent);
    output << "\"processes\": [\n";

    for (
        std::size_t index = 0;
        index < values.size();
        ++index
    ) {
        const DebugSymbols* symbols =
            session.hasSymbols(
                values[index].pid
            )
                ? &session.symbols(
                    values[index].pid
                )
                : nullptr;

        appendProcess(
            output,
            values[index],
            symbols,
            options,
            fieldIndent + 2,
            index + 1 < values.size()
        );
    }

    indent(output, fieldIndent);
    output << "]";

    finishField(output, comma);
}

void appendMultiSymbols(
    std::ostringstream& output,
    const MultiProcessDebugSession& session,
    const DebugSnapshotOptions& options,
    int fieldIndent,
    bool comma
) {
    const std::vector<ProcessDebugSnapshot> snapshots =
        session.processSnapshots();

    indent(output, fieldIndent);
    output << "\"symbols\": [\n";

    bool first = true;

    if (options.include_symbols) {
        for (
            const ProcessDebugSnapshot& snapshot :
            snapshots
        ) {
            if (!session.hasSymbols(snapshot.pid)) {
                continue;
            }

            if (!first) {
                output << ",\n";
            }

            first = false;

            indent(output, fieldIndent + 2);
            output << "{\n";

            sizeField(
                output,
                fieldIndent + 4,
                "pid",
                snapshot.pid
            );

            appendSymbolEntries(
                output,
                session.symbols(snapshot.pid),
                fieldIndent + 4,
                false
            );

            indent(output, fieldIndent + 2);
            output << "}";
        }
    }

    if (!first) {
        output << "\n";
    }

    indent(output, fieldIndent);
    output << "]";

    finishField(output, comma);
}

void appendContextSwitches(
    std::ostringstream& output,
    const MultiProcessDebugSession& session,
    int fieldIndent,
    bool comma
) {
    const std::vector<ContextSwitchRecord>& values =
        session.contextSwitches();

    indent(output, fieldIndent);
    output << "\"context_switches\": [\n";

    for (
        std::size_t index = 0;
        index < values.size();
        ++index
    ) {
        const ContextSwitchRecord& record =
            values[index];

        indent(output, fieldIndent + 2);
        output << "{\n";

        sizeField(
            output,
            fieldIndent + 4,
            "lifecycle_step",
            record.lifecycle_step
        );

        sizeField(
            output,
            fieldIndent + 4,
            "from_pid",
            record.from_pid
        );

        sizeField(
            output,
            fieldIndent + 4,
            "to_pid",
            record.to_pid
        );

        boolField(
            output,
            fieldIndent + 4,
            "preempted",
            record.preempted
        );

        boolField(
            output,
            fieldIndent + 4,
            "caused_by_termination",
            record.caused_by_termination,
            false
        );

        indent(output, fieldIndent + 2);
        output << "}";

        finishField(
            output,
            index + 1 < values.size()
        );
    }

    indent(output, fieldIndent);
    output << "]";

    finishField(output, comma);
}

void appendLastTrace(
    std::ostringstream& output,
    const CPU& cpu,
    const DebugSnapshotOptions& options,
    int fieldIndent,
    bool comma
) {
    if (
        !options.include_last_trace
        || cpu.traceLogger().empty()
    ) {
        nullField(
            output,
            fieldIndent,
            "last_trace",
            comma
        );

        return;
    }

    indent(output, fieldIndent);
    output << "\"last_trace\": {\n";

    stringField(
        output,
        fieldIndent + 2,
        "compact",
        cpu.traceLogger()
            .last()
            .toCompactString()
    );

    stringField(
        output,
        fieldIndent + 2,
        "full",
        cpu.traceLogger()
            .last()
            .toFullString(),
        false
    );

    indent(output, fieldIndent);
    output << "}";

    finishField(output, comma);
}

void writeJsonFile(
    const std::string& path,
    const std::string& json
) {
    if (path.empty()) {
        throw std::runtime_error(
            "Debug snapshot JSON path must not "
            "be empty"
        );
    }

    std::ofstream file(
        path,
        std::ios::binary
    );

    if (!file) {
        throw std::runtime_error(
            "Failed to open debug snapshot JSON "
            "file: "
            + path
        );
    }

    file << json;

    if (!file) {
        throw std::runtime_error(
            "Failed to write debug snapshot JSON "
            "file: "
            + path
        );
    }
}

} // namespace

std::string DebugSnapshotJsonWriter::toJson(
    const DebugSession& session,
    const DebugSnapshotOptions& options
) {
    if (!session.loaded()) {
        throw std::runtime_error(
            "Cannot snapshot an unloaded "
            "debug session"
        );
    }

    validateMemoryOptions(
        session.cpu().state().memory(),
        options
    );

    std::ostringstream output;

    output << "{\n";

    stringField(
        output,
        2,
        "schema",
        "zero_cpu_debug_snapshot"
    );

    sizeField(
        output,
        2,
        "schema_version",
        kSchemaVersion
    );

    stringField(
        output,
        2,
        "mode",
        "single_process"
    );

    stringField(
        output,
        2,
        "source_name",
        session.sourceName()
    );

    sizeField(
        output,
        2,
        "total_steps",
        session.totalSteps()
    );

    appendSingleStop(
        output,
        session.lastStop(),
        2,
        true
    );

    appendCPU(
        output,
        session.cpu(),
        2,
        true
    );

    appendExecutable(
        output,
        session.metadata(),
        2,
        true
    );

    appendSourceLocation(
        output,
        session.hasSymbols()
            ? &session.symbols()
            : nullptr,
        session.cpu().state().pc(),
        2,
        true
    );

    appendSingleBreakpoints(
        output,
        session,
        2,
        true
    );

    appendSingleConditionalBreakpoints(
        output,
        session,
        2,
        true
    );

    appendSingleWatchpoints(
        output,
        session,
        2,
        true
    );

    appendSingleSymbols(
        output,
        session,
        options,
        2,
        true
    );

    appendMemory(
        output,
        session.cpu().state().memory(),
        options,
        2,
        true
    );

    appendLastTrace(
        output,
        session.cpu(),
        options,
        2,
        false
    );

    output << "}\n";

    return output.str();
}

std::string DebugSnapshotJsonWriter::toJson(
    const MultiProcessDebugSession& session,
    const DebugSnapshotOptions& options
) {
    const std::vector<ProcessDebugSnapshot> snapshots =
        session.processSnapshots();

    for (
        const ProcessDebugSnapshot& snapshot :
        snapshots
    ) {
        validateMemoryOptions(
            snapshot.memory,
            options
        );
    }

    std::ostringstream output;

    output << "{\n";

    stringField(
        output,
        2,
        "schema",
        "zero_cpu_debug_snapshot"
    );

    sizeField(
        output,
        2,
        "schema_version",
        kSchemaVersion
    );

    stringField(
        output,
        2,
        "mode",
        "multi_process"
    );

    stringField(
        output,
        2,
        "runtime_state",
        kernel::processRuntimeStateToString(
            session.runtimeState()
        )
    );

    sizeField(
        output,
        2,
        "total_steps",
        session.totalSteps()
    );

    sizeField(
        output,
        2,
        "running_pid",
        session.runningPid()
    );

    sizeField(
        output,
        2,
        "selected_pid",
        session.selectedPid()
    );

    uint64Field(
        output,
        2,
        "quantum",
        session.quantum()
    );

    uint64Field(
        output,
        2,
        "preemption_count",
        session.preemptionCount()
    );

    uint64Field(
        output,
        2,
        "scheduler_context_switch_count",
        session.schedulerContextSwitchCount()
    );

    appendMultiStop(
        output,
        session.lastStop(),
        2,
        true
    );

    appendProcesses(
        output,
        session,
        options,
        2,
        true
    );

    appendMultiBreakpoints(
        output,
        session,
        2,
        true
    );

    appendMultiConditionalBreakpoints(
        output,
        session,
        2,
        true
    );

    appendMultiWatchpoints(
        output,
        session,
        2,
        true
    );

    appendMultiSymbols(
        output,
        session,
        options,
        2,
        true
    );

    appendContextSwitches(
        output,
        session,
        2,
        true
    );

    appendLastTrace(
        output,
        session.cpu(),
        options,
        2,
        false
    );

    output << "}\n";

    return output.str();
}

void DebugSnapshotJsonWriter::writeFile(
    const std::string& path,
    const DebugSession& session,
    const DebugSnapshotOptions& options
) {
    writeJsonFile(
        path,
        toJson(
            session,
            options
        )
    );
}

void DebugSnapshotJsonWriter::writeFile(
    const std::string& path,
    const MultiProcessDebugSession& session,
    const DebugSnapshotOptions& options
) {
    writeJsonFile(
        path,
        toJson(
            session,
            options
        )
    );
}

} // namespace zero_cpu::debug
