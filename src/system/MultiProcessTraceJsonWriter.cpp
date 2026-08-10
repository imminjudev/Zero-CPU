#include "zero_cpu/system/MultiProcessTraceJsonWriter.hpp"

#include "zero_cpu/kernel/ProcessLifecycleManager.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"
#include "zero_cpu/kernel/ProcessTermination.hpp"
#include "zero_cpu/trace/TraceJsonWriter.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace zero_cpu::system {
namespace {

void indent(std::ostringstream& oss, int spaces) {
    for (int index = 0; index < spaces; ++index) {
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

void finishField(
    std::ostringstream& oss,
    bool comma
) {
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

void nestedJsonField(
    std::ostringstream& oss,
    int fieldIndent,
    const std::string& name,
    std::string json,
    bool comma
) {
    while (
        !json.empty()
        && (
            json.back() == '\n'
            || json.back() == '\r'
        )
    ) {
        json.pop_back();
    }

    indent(oss, fieldIndent);

    oss << "\""
        << escapeJson(name)
        << "\": ";

    std::istringstream input(json);
    std::string line;
    bool first = true;

    while (std::getline(input, line)) {
        if (!first) {
            oss << "\n";
            indent(oss, fieldIndent);
        }

        oss << line;
        first = false;
    }

    finishField(oss, comma);
}

void appendInvariantReport(
    std::ostringstream& oss,
    const MultiProcessInvariantReport& report
) {
    indent(oss, 2);
    oss << "\"invariants\": {\n";

    boolField(oss, 4, "passed", report.passed());

    sizeField(
        oss,
        4,
        "violation_count",
        report.violationCount()
    );

    indent(oss, 4);
    oss << "\"violations\": [\n";

    for (
        std::size_t index = 0;
        index < report.violations.size();
        ++index
    ) {
        const MultiProcessInvariantViolation& violation =
            report.violations[index];

        indent(oss, 6);
        oss << "{\n";

        stringField(
            oss,
            8,
            "code",
            violation.code
        );

        stringField(
            oss,
            8,
            "message",
            violation.message
        );

        sizeField(
            oss,
            8,
            "lifecycle_step",
            violation.lifecycle_step
        );

        sizeField(
            oss,
            8,
            "pid",
            static_cast<std::size_t>(
                violation.pid
            ),
            false
        );

        indent(oss, 6);
        oss << "}";

        finishField(
            oss,
            index + 1 < report.violations.size()
        );
    }

    indent(oss, 4);
    oss << "]\n";

    indent(oss, 2);
    oss << "},\n";
}

void appendExecutionTrace(
    std::ostringstream& oss,
    const MultiProcessRunResult& result
) {
    sizeField(
        oss,
        2,
        "execution_event_count",
        result.execution_trace.size()
    );

    indent(oss, 2);
    oss << "\"execution_trace\": [\n";

    for (
        std::size_t index = 0;
        index < result.execution_trace.size();
        ++index
    ) {
        const ProcessExecutionTraceRecord& record =
            result.execution_trace[index];

        indent(oss, 4);
        oss << "{\n";

        sizeField(
            oss,
            6,
            "lifecycle_step",
            record.lifecycle_step
        );

        sizeField(
            oss,
            6,
            "pid",
            static_cast<std::size_t>(
                record.pid
            )
        );

        nestedJsonField(
            oss,
            6,
            "event",
            TraceJsonWriter::eventToJson(
                record.event,
                index
            ),
            false
        );

        indent(oss, 4);
        oss << "}";

        finishField(
            oss,
            index + 1
                < result.execution_trace.size()
        );
    }

    indent(oss, 2);
    oss << "],\n";
}

void appendContextSwitches(
    std::ostringstream& oss,
    const MultiProcessRunResult& result
) {
    sizeField(
        oss,
        2,
        "context_switch_event_count",
        result.context_switches.size()
    );

    indent(oss, 2);
    oss << "\"context_switches\": [\n";

    for (
        std::size_t index = 0;
        index < result.context_switches.size();
        ++index
    ) {
        const ProcessContextSwitchTraceRecord& record =
            result.context_switches[index];

        indent(oss, 4);
        oss << "{\n";

        sizeField(
            oss,
            6,
            "lifecycle_step",
            record.lifecycle_step
        );

        sizeField(
            oss,
            6,
            "from_pid",
            static_cast<std::size_t>(
                record.from_pid
            )
        );

        sizeField(
            oss,
            6,
            "to_pid",
            static_cast<std::size_t>(
                record.to_pid
            )
        );

        boolField(
            oss,
            6,
            "preempted",
            record.preempted
        );

        boolField(
            oss,
            6,
            "caused_by_termination",
            record.caused_by_termination,
            false
        );

        indent(oss, 4);
        oss << "}";

        finishField(
            oss,
            index + 1
                < result.context_switches.size()
        );
    }

    indent(oss, 2);
    oss << "],\n";
}

void appendProcesses(
    std::ostringstream& oss,
    const MultiProcessRunResult& result
) {
    indent(oss, 2);
    oss << "\"processes\": [\n";

    for (
        std::size_t index = 0;
        index < result.processes.size();
        ++index
    ) {
        const ProcessRunSummary& process =
            result.processes[index];

        indent(oss, 4);
        oss << "{\n";

        sizeField(
            oss,
            6,
            "pid",
            static_cast<std::size_t>(
                process.pid
            )
        );

        stringField(
            oss,
            6,
            "source_name",
            process.source_name
        );

        stringField(
            oss,
            6,
            "state",
            kernel::processStateToString(
                process.state
            )
        );

        boolField(
            oss,
            6,
            "has_exit_code",
            process.has_exit_code
        );

        int64Field(
            oss,
            6,
            "exit_code",
            process.exit_code
        );

        stringField(
            oss,
            6,
            "termination_kind",
            kernel::processTerminationKindToString(
                process.termination_kind
            )
        );

        stringField(
            oss,
            6,
            "termination_message",
            process.termination_message
        );

        sizeField(
            oss,
            6,
            "pc",
            process.final_context.pc
        );

        sizeField(
            oss,
            6,
            "sp",
            process.final_context.sp
        );

        boolField(
            oss,
            6,
            "has_executable_image",
            process.has_executable_image
        );

        sizeField(
            oss,
            6,
            "code_end_exclusive",
            process.code_end_exclusive
        );

        sizeField(
            oss,
            6,
            "data_base",
            process.data_base
        );

        sizeField(
            oss,
            6,
            "data_size",
            process.data_size,
            false
        );

        indent(oss, 4);
        oss << "}";

        finishField(
            oss,
            index + 1 < result.processes.size()
        );
    }

    indent(oss, 2);
    oss << "]\n";
}

} // namespace

std::string MultiProcessTraceJsonWriter::toJson(
    const MultiProcessRunResult& result,
    const MultiProcessTraceJsonMetadata& metadata
) {
    const MultiProcessInvariantReport report =
        MultiProcessInvariantVerifier::verify(
            result
        );

    std::ostringstream oss;

    oss << "{\n";

    stringField(
        oss,
        2,
        "schema",
        "zero_cpu_multiprocess_trace"
    );

    sizeField(
        oss,
        2,
        "schema_version",
        kSchemaVersion
    );

    stringField(
        oss,
        2,
        "producer",
        metadata.producer
    );

    stringField(
        oss,
        2,
        "producer_version",
        metadata.producer_version
    );

    stringField(
        oss,
        2,
        "mode",
        metadata.execution_mode
    );

    stringField(
        oss,
        2,
        "runtime_state",
        kernel::processRuntimeStateToString(
            result.runtime_state
        )
    );

    boolField(
        oss,
        2,
        "step_limit_reached",
        result.step_limit_reached
    );

    sizeField(
        oss,
        2,
        "lifecycle_steps",
        result.lifecycle_steps
    );

    sizeField(
        oss,
        2,
        "process_count",
        result.process_count
    );

    sizeField(
        oss,
        2,
        "termination_count",
        result.termination_count
    );

    sizeField(
        oss,
        2,
        "fault_count",
        result.fault_count
    );

    sizeField(
        oss,
        2,
        "preemption_count",
        static_cast<std::size_t>(
            result.preemption_count
        )
    );

    sizeField(
        oss,
        2,
        "context_switch_count",
        static_cast<std::size_t>(
            result.context_switch_count
        )
    );

    appendInvariantReport(
        oss,
        report
    );

    appendExecutionTrace(
        oss,
        result
    );

    appendContextSwitches(
        oss,
        result
    );

    appendProcesses(
        oss,
        result
    );

    oss << "}\n";

    return oss.str();
}

void MultiProcessTraceJsonWriter::writeFile(
    const std::string& path,
    const MultiProcessRunResult& result,
    const MultiProcessTraceJsonMetadata& metadata
) {
    std::ofstream file(
        path,
        std::ios::binary
    );

    if (!file) {
        throw std::runtime_error(
            "Failed to open multi-process trace JSON file: "
            + path
        );
    }

    file << toJson(
        result,
        metadata
    );

    if (!file) {
        throw std::runtime_error(
            "Failed to write multi-process trace JSON file: "
            + path
        );
    }
}

} // namespace zero_cpu::system
