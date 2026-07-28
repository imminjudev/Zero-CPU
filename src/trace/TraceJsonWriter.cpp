#include "zero_cpu/trace/TraceJsonWriter.hpp"

#include "zero_cpu/core/RegisterFile.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace zero_cpu {

namespace {

void indent(std::ostringstream& oss, int spaces) {
    for (int i = 0; i < spaces; ++i) {
        oss << ' ';
    }
}

std::string escapeJson(const std::string& text) {
    std::ostringstream oss;

    for (const unsigned char ch : text) {
        switch (ch) {
        case '"':
            oss << "\\\"";
            break;
        case '\\':
            oss << "\\\\";
            break;
        case '\b':
            oss << "\\b";
            break;
        case '\f':
            oss << "\\f";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        default:
            if (ch < 0x20) {
                oss << "\\u"
                    << std::hex
                    << std::setw(4)
                    << std::setfill('0')
                    << static_cast<int>(ch)
                    << std::dec
                    << std::setfill(' ');
            } else {
                oss << static_cast<char>(ch);
            }
            break;
        }
    }

    return oss.str();
}

void finishField(std::ostringstream& oss, bool comma) {
    if (comma) {
        oss << ",";
    }

    oss << "\n";
}

void stringField(
    std::ostringstream& oss,
    int fieldIndent,
    const std::string& name,
    const std::string& value,
    bool comma = true
) {
    indent(oss, fieldIndent);
    oss << "\""
        << escapeJson(name)
        << "\": \""
        << escapeJson(value)
        << "\"";

    finishField(oss, comma);
}

void sizeField(
    std::ostringstream& oss,
    int fieldIndent,
    const std::string& name,
    std::size_t value,
    bool comma = true
) {
    indent(oss, fieldIndent);
    oss << "\""
        << escapeJson(name)
        << "\": "
        << value;

    finishField(oss, comma);
}

void int64Field(
    std::ostringstream& oss,
    int fieldIndent,
    const std::string& name,
    std::int64_t value,
    bool comma = true
) {
    indent(oss, fieldIndent);
    oss << "\""
        << escapeJson(name)
        << "\": "
        << value;

    finishField(oss, comma);
}

void boolField(
    std::ostringstream& oss,
    int fieldIndent,
    const std::string& name,
    bool value,
    bool comma = true
) {
    indent(oss, fieldIndent);
    oss << "\""
        << escapeJson(name)
        << "\": "
        << (value ? "true" : "false");

    finishField(oss, comma);
}

void appendRegisters(
    std::ostringstream& oss,
    const CPUState& state,
    int fieldIndent,
    bool comma
) {
    indent(oss, fieldIndent);
    oss << "\"registers\": {\n";

    for (std::size_t i = 0; i < RegisterFile::kRegisterCount; ++i) {
        const RegisterName reg = static_cast<RegisterName>(i);

        int64Field(
            oss,
            fieldIndent + 2,
            RegisterFile::registerNameToString(reg),
            state.registers().get(reg),
            i + 1 < RegisterFile::kRegisterCount
        );
    }

    indent(oss, fieldIndent);
    oss << "}";

    finishField(oss, comma);
}

void appendFlags(
    std::ostringstream& oss,
    const CPUState& state,
    int fieldIndent,
    bool comma
) {
    indent(oss, fieldIndent);
    oss << "\"flags\": {\n";

    boolField(oss, fieldIndent + 2, "zero", state.flags().zero());
    boolField(oss, fieldIndent + 2, "sign", state.flags().sign());
    boolField(oss, fieldIndent + 2, "carry", state.flags().carry());
    boolField(oss, fieldIndent + 2, "overflow", state.flags().overflow());
    sizeField(
        oss,
        fieldIndent + 2,
        "raw",
        static_cast<std::size_t>(state.flags().raw()),
        false
    );

    indent(oss, fieldIndent);
    oss << "}";

    finishField(oss, comma);
}

void appendState(
    std::ostringstream& oss,
    const std::string& name,
    const CPUState& state,
    int fieldIndent,
    bool comma
) {
    indent(oss, fieldIndent);
    oss << "\""
        << escapeJson(name)
        << "\": {\n";

    sizeField(oss, fieldIndent + 2, "pc", state.pc());
    sizeField(oss, fieldIndent + 2, "sp", state.sp());
    boolField(oss, fieldIndent + 2, "halted", state.halted());
    boolField(oss, fieldIndent + 2, "has_error", state.hasError());
    stringField(
        oss,
        fieldIndent + 2,
        "error_message",
        state.errorMessage()
    );
    appendRegisters(oss, state, fieldIndent + 2, true);
    appendFlags(oss, state, fieldIndent + 2, false);

    indent(oss, fieldIndent);
    oss << "}";

    finishField(oss, comma);
}

void appendRegisterChanges(
    std::ostringstream& oss,
    const TraceEvent& event,
    int fieldIndent,
    bool comma
) {
    indent(oss, fieldIndent);
    oss << "\"register_changes\": [\n";

    const auto& changes = event.changedRegisters();

    for (std::size_t i = 0; i < changes.size(); ++i) {
        const RegisterChange& change = changes[i];

        indent(oss, fieldIndent + 2);
        oss << "{\n";

        stringField(oss, fieldIndent + 4, "name", change.name);
        int64Field(oss, fieldIndent + 4, "before", change.before);
        int64Field(oss, fieldIndent + 4, "after", change.after, false);

        indent(oss, fieldIndent + 2);
        oss << "}";

        finishField(oss, i + 1 < changes.size());
    }

    indent(oss, fieldIndent);
    oss << "]";

    finishField(oss, comma);
}

void appendFlagChanges(
    std::ostringstream& oss,
    const TraceEvent& event,
    int fieldIndent,
    bool comma
) {
    indent(oss, fieldIndent);
    oss << "\"flag_changes\": [\n";

    const auto& changes = event.changedFlags();

    for (std::size_t i = 0; i < changes.size(); ++i) {
        const FlagChange& change = changes[i];

        indent(oss, fieldIndent + 2);
        oss << "{\n";

        stringField(oss, fieldIndent + 4, "name", change.name);
        boolField(oss, fieldIndent + 4, "before", change.before);
        boolField(oss, fieldIndent + 4, "after", change.after, false);

        indent(oss, fieldIndent + 2);
        oss << "}";

        finishField(oss, i + 1 < changes.size());
    }

    indent(oss, fieldIndent);
    oss << "]";

    finishField(oss, comma);
}

void appendMemoryChanges(
    std::ostringstream& oss,
    const TraceEvent& event,
    int fieldIndent,
    bool comma
) {
    indent(oss, fieldIndent);
    oss << "\"memory_changes\": [\n";

    const auto& changes = event.changedMemory();

    for (std::size_t i = 0; i < changes.size(); ++i) {
        const MemoryChange& change = changes[i];

        indent(oss, fieldIndent + 2);
        oss << "{\n";

        sizeField(oss, fieldIndent + 4, "address", change.address);
        int64Field(oss, fieldIndent + 4, "before", change.before);
        int64Field(oss, fieldIndent + 4, "after", change.after, false);

        indent(oss, fieldIndent + 2);
        oss << "}";

        finishField(oss, i + 1 < changes.size());
    }

    indent(oss, fieldIndent);
    oss << "]";

    finishField(oss, comma);
}

void appendEvent(
    std::ostringstream& oss,
    const TraceEvent& event,
    std::size_t index,
    bool comma
) {
    indent(oss, 4);
    oss << "{\n";

    sizeField(oss, 6, "index", index);
    sizeField(oss, 6, "pc_before", event.pcBefore());
    sizeField(oss, 6, "pc_after", event.pcAfter());
    sizeField(oss, 6, "sp_before", event.before().sp());
    sizeField(oss, 6, "sp_after", event.after().sp());
    stringField(oss, 6, "instruction", event.instruction().toString());

    appendState(oss, "state_before", event.before(), 6, true);
    appendState(oss, "state_after", event.after(), 6, true);

    appendRegisterChanges(oss, event, 6, true);
    appendFlagChanges(oss, event, 6, true);
    appendMemoryChanges(oss, event, 6, true);

    stringField(oss, 6, "stage", event.stage());
    stringField(oss, 6, "action", event.action());
    stringField(oss, 6, "datapath", event.datapathString());
    stringField(oss, 6, "alu_detail", event.aluDetailString());
    stringField(oss, 6, "memory_detail", event.memoryDetailString());
    stringField(oss, 6, "stack_detail", event.stackDetailString());
    stringField(
        oss,
        6,
        "control_flow_detail",
        event.controlFlowDetailString()
    );
    stringField(oss, 6, "compact", event.toCompactString());
    stringField(oss, 6, "full", event.toFullString());
    boolField(oss, 6, "has_error", event.hasError());
    stringField(oss, 6, "error", event.errorMessage(), false);

    indent(oss, 4);
    oss << "}";

    finishField(oss, comma);
}

} // namespace

std::string TraceJsonWriter::toJson(
    const std::vector<TraceEvent>& events,
    const TraceJsonMetadata& metadata
) {
    std::ostringstream oss;

    oss << "{\n";
    stringField(oss, 2, "schema", "zero_cpu_trace");
    sizeField(oss, 2, "schema_version", kSchemaVersion);
    stringField(oss, 2, "producer", metadata.producer);
    stringField(oss, 2, "producer_version", metadata.producer_version);
    stringField(oss, 2, "mode", metadata.execution_mode);
    stringField(oss, 2, "loaded_path", metadata.loaded_path);
    sizeField(oss, 2, "event_count", events.size());

    indent(oss, 2);
    oss << "\"events\": [\n";

    for (std::size_t i = 0; i < events.size(); ++i) {
        appendEvent(oss, events[i], i, i + 1 < events.size());
    }

    indent(oss, 2);
    oss << "]\n";
    oss << "}\n";

    return oss.str();
}

void TraceJsonWriter::writeFile(
    const std::string& path,
    const std::vector<TraceEvent>& events,
    const TraceJsonMetadata& metadata
) {
    std::ofstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error(
            "Failed to open trace JSON file: " + path
        );
    }

    file << toJson(events, metadata);

    if (!file) {
        throw std::runtime_error(
            "Failed to write trace JSON file: " + path
        );
    }
}

} // namespace zero_cpu
