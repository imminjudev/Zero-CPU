#include "zero_cpu/trace/TraceJsonDiff.hpp"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zero_cpu {

namespace {

enum class JsonType {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

struct JsonValue {
    JsonType type = JsonType::Null;
    bool boolean = false;
    std::int64_t number = 0;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;

    static JsonValue makeNull() {
        return {};
    }

    static JsonValue makeBoolean(bool value) {
        JsonValue result;
        result.type = JsonType::Boolean;
        result.boolean = value;
        return result;
    }

    static JsonValue makeNumber(std::int64_t value) {
        JsonValue result;
        result.type = JsonType::Number;
        result.number = value;
        return result;
    }

    static JsonValue makeString(std::string value) {
        JsonValue result;
        result.type = JsonType::String;
        result.string = std::move(value);
        return result;
    }

    static JsonValue makeArray(std::vector<JsonValue> value) {
        JsonValue result;
        result.type = JsonType::Array;
        result.array = std::move(value);
        return result;
    }

    static JsonValue makeObject(std::map<std::string, JsonValue> value) {
        JsonValue result;
        result.type = JsonType::Object;
        result.object = std::move(value);
        return result;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text)
        : text_(text) {
    }

    JsonValue parse() {
        skipWhitespace();
        JsonValue value = parseValue();
        skipWhitespace();

        if (!atEnd()) {
            fail("Unexpected trailing characters");
        }

        return value;
    }

private:
    const std::string& text_;
    std::size_t position_ = 0;

    bool atEnd() const {
        return position_ >= text_.size();
    }

    char peek() const {
        return atEnd() ? '\0' : text_[position_];
    }

    char take() {
        if (atEnd()) {
            fail("Unexpected end of JSON input");
        }

        return text_[position_++];
    }

    void skipWhitespace() {
        while (!atEnd() &&
               std::isspace(
                   static_cast<unsigned char>(peek())
               )) {
            ++position_;
        }
    }

    [[noreturn]] void fail(const std::string& message) const {
        std::ostringstream oss;
        oss << message << " at byte " << position_;
        throw std::runtime_error(oss.str());
    }

    void expect(char expected) {
        const char actual = take();

        if (actual != expected) {
            std::ostringstream oss;
            oss << "Expected '" << expected
                << "' but found '" << actual << "'";
            fail(oss.str());
        }
    }

    bool consume(char expected) {
        if (peek() != expected) {
            return false;
        }

        ++position_;
        return true;
    }

    JsonValue parseValue() {
        skipWhitespace();

        switch (peek()) {
        case '{':
            return parseObject();
        case '[':
            return parseArray();
        case '"':
            return JsonValue::makeString(parseString());
        case 't':
            parseLiteral("true");
            return JsonValue::makeBoolean(true);
        case 'f':
            parseLiteral("false");
            return JsonValue::makeBoolean(false);
        case 'n':
            parseLiteral("null");
            return JsonValue::makeNull();
        default:
            if (peek() == '-' ||
                std::isdigit(
                    static_cast<unsigned char>(peek())
                )) {
                return JsonValue::makeNumber(parseNumber());
            }

            fail("Unexpected JSON token");
        }
    }

    JsonValue parseObject() {
        expect('{');
        skipWhitespace();

        std::map<std::string, JsonValue> values;

        if (consume('}')) {
            return JsonValue::makeObject(std::move(values));
        }

        while (true) {
            skipWhitespace();

            if (peek() != '"') {
                fail("Object key must be a JSON string");
            }

            const std::string key = parseString();

            skipWhitespace();
            expect(':');
            skipWhitespace();

            const auto inserted = values.emplace(
                key,
                parseValue()
            );

            if (!inserted.second) {
                fail("Duplicate JSON object key: " + key);
            }

            skipWhitespace();

            if (consume('}')) {
                break;
            }

            expect(',');
        }

        return JsonValue::makeObject(std::move(values));
    }

    JsonValue parseArray() {
        expect('[');
        skipWhitespace();

        std::vector<JsonValue> values;

        if (consume(']')) {
            return JsonValue::makeArray(std::move(values));
        }

        while (true) {
            values.push_back(parseValue());

            skipWhitespace();

            if (consume(']')) {
                break;
            }

            expect(',');
            skipWhitespace();
        }

        return JsonValue::makeArray(std::move(values));
    }

    static void appendUtf8(
        std::string& output,
        std::uint32_t codePoint
    ) {
        if (codePoint <= 0x7F) {
            output.push_back(static_cast<char>(codePoint));
            return;
        }

        if (codePoint <= 0x7FF) {
            output.push_back(
                static_cast<char>(0xC0 | (codePoint >> 6))
            );
            output.push_back(
                static_cast<char>(0x80 | (codePoint & 0x3F))
            );
            return;
        }

        if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
            throw std::runtime_error(
                "JSON surrogate pairs are not supported"
            );
        }

        output.push_back(
            static_cast<char>(0xE0 | (codePoint >> 12))
        );
        output.push_back(
            static_cast<char>(
                0x80 | ((codePoint >> 6) & 0x3F)
            )
        );
        output.push_back(
            static_cast<char>(0x80 | (codePoint & 0x3F))
        );
    }

    static int hexValue(char ch) {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }

        if (ch >= 'a' && ch <= 'f') {
            return 10 + ch - 'a';
        }

        if (ch >= 'A' && ch <= 'F') {
            return 10 + ch - 'A';
        }

        return -1;
    }

    std::string parseString() {
        expect('"');

        std::string result;

        while (true) {
            if (atEnd()) {
                fail("Unterminated JSON string");
            }

            const char ch = take();

            if (ch == '"') {
                break;
            }

            if (static_cast<unsigned char>(ch) < 0x20) {
                fail("Unescaped control character in JSON string");
            }

            if (ch != '\\') {
                result.push_back(ch);
                continue;
            }

            const char escape = take();

            switch (escape) {
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            case '/':
                result.push_back('/');
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u': {
                std::uint32_t codePoint = 0;

                for (int i = 0; i < 4; ++i) {
                    const int value = hexValue(take());

                    if (value < 0) {
                        fail("Invalid Unicode escape");
                    }

                    codePoint =
                        (codePoint << 4) |
                        static_cast<std::uint32_t>(value);
                }

                appendUtf8(result, codePoint);
                break;
            }
            default:
                fail("Invalid JSON string escape");
            }
        }

        return result;
    }

    std::int64_t parseNumber() {
        const std::size_t start = position_;

        consume('-');

        if (!std::isdigit(
                static_cast<unsigned char>(peek())
            )) {
            fail("JSON number requires digits");
        }

        if (peek() == '0') {
            ++position_;
        } else {
            while (std::isdigit(
                       static_cast<unsigned char>(peek())
                   )) {
                ++position_;
            }
        }

        if (peek() == '.' || peek() == 'e' || peek() == 'E') {
            fail("Trace JSON requires integer numbers");
        }

        const std::string token =
            text_.substr(start, position_ - start);

        try {
            std::size_t parsed = 0;
            const long long value =
                std::stoll(token, &parsed, 10);

            if (parsed != token.size()) {
                fail("Invalid integer number");
            }

            return static_cast<std::int64_t>(value);
        } catch (const std::exception&) {
            fail("Integer number is out of range");
        }
    }

    void parseLiteral(const char* literal) {
        for (const char* current = literal;
             *current != '\0';
             ++current) {
            if (take() != *current) {
                fail(
                    std::string("Invalid JSON literal: ") +
                    literal
                );
            }
        }
    }
};

const JsonValue& requiredField(
    const JsonValue& object,
    std::string_view name
) {
    if (object.type != JsonType::Object) {
        throw std::runtime_error(
            "Expected JSON object while reading field: " + std::string(name)
        );
    }

    const std::string fieldName(name);
    const auto it = object.object.find(fieldName);

    if (it == object.object.end()) {
        throw std::runtime_error(
            "Missing required trace JSON field: " + fieldName
        );
    }

    return it->second;
}

const JsonValue* optionalField(
    const JsonValue& object,
    std::string_view name
) {
    if (object.type != JsonType::Object) {
        throw std::runtime_error(
            "Expected JSON object while reading optional field: "
            + std::string(name)
        );
    }

    const auto found =
        object.object.find(
            std::string(name)
        );

    if (found == object.object.end()) {
        return nullptr;
    }

    return &found->second;
}

std::string requiredString(
    const JsonValue& object,
    const std::string& name
) {
    const JsonValue& value = requiredField(object, name);

    if (value.type != JsonType::String) {
        throw std::runtime_error(
            "Trace JSON field must be string: " + name
        );
    }

    return value.string;
}

std::int64_t requiredNumber(
    const JsonValue& object,
    const std::string& name
) {
    const JsonValue& value = requiredField(object, name);

    if (value.type != JsonType::Number) {
        throw std::runtime_error(
            "Trace JSON field must be integer: " + name
        );
    }

    return value.number;
}

enum class TraceDocumentKind {
    SingleProcess,
    MultiProcess
};

TraceDocumentKind validateTraceDocument(
    const JsonValue& root,
    const std::string& label
) {
    if (root.type != JsonType::Object) {
        throw std::runtime_error(
            label + " trace JSON root must be an object"
        );
    }

    const std::string schema =
        requiredString(root, "schema");

    const std::int64_t schemaVersion =
        requiredNumber(root, "schema_version");

    if (schema == "zero_cpu_trace") {
        if (schemaVersion != 3) {
            throw std::runtime_error(
                label
                + " single-process trace uses "
                  "unsupported schema version"
            );
        }

        const JsonValue& events =
            requiredField(root, "events");

        if (events.type != JsonType::Array) {
            throw std::runtime_error(
                label
                + " trace events field must be an array"
            );
        }

        const std::int64_t declaredCount =
            requiredNumber(root, "event_count");

        if (
            declaredCount < 0
            || static_cast<std::size_t>(
                declaredCount
            ) != events.array.size()
        ) {
            throw std::runtime_error(
                label
                + " trace event_count does not match events"
            );
        }

        return TraceDocumentKind::SingleProcess;
    }

    if (schema == "zero_cpu_multiprocess_trace") {
        if (schemaVersion != 1) {
            throw std::runtime_error(
                label
                + " multi-process trace uses "
                  "unsupported schema version"
            );
        }

        const JsonValue& executionTrace =
            requiredField(root, "execution_trace");

        const JsonValue& contextSwitches =
            requiredField(root, "context_switches");

        const JsonValue& processes =
            requiredField(root, "processes");

        if (
            executionTrace.type != JsonType::Array
            || contextSwitches.type != JsonType::Array
            || processes.type != JsonType::Array
        ) {
            throw std::runtime_error(
                label
                + " multi-process trace arrays are invalid"
            );
        }

        const std::int64_t executionCount =
            requiredNumber(
                root,
                "execution_event_count"
            );

        const std::int64_t contextSwitchCount =
            requiredNumber(
                root,
                "context_switch_event_count"
            );

        const std::int64_t processCount =
            requiredNumber(
                root,
                "process_count"
            );

        if (
            executionCount < 0
            || static_cast<std::size_t>(
                executionCount
            ) != executionTrace.array.size()
        ) {
            throw std::runtime_error(
                label
                + " multi-process execution_event_count "
                  "does not match execution_trace"
            );
        }

        if (
            contextSwitchCount < 0
            || static_cast<std::size_t>(
                contextSwitchCount
            ) != contextSwitches.array.size()
        ) {
            throw std::runtime_error(
                label
                + " multi-process "
                  "context_switch_event_count does not "
                  "match context_switches"
            );
        }

        if (
            processCount < 0
            || static_cast<std::size_t>(
                processCount
            ) != processes.array.size()
        ) {
            throw std::runtime_error(
                label
                + " multi-process process_count does not "
                  "match processes"
            );
        }

        const JsonValue* softwareInterrupts =
            optionalField(
                root,
                "software_interrupts"
            );

        const JsonValue* softwareInterruptCount =
            optionalField(
                root,
                "software_interrupt_event_count"
            );

        if (
            (softwareInterrupts == nullptr)
            != (softwareInterruptCount == nullptr)
        ) {
            throw std::runtime_error(
                label
                + " multi-process software interrupt "
                  "count/array fields must appear together"
            );
        }

        if (softwareInterrupts != nullptr) {
            if (
                softwareInterrupts->type
                != JsonType::Array
            ) {
                throw std::runtime_error(
                    label
                    + " multi-process software_interrupts "
                      "field must be an array"
                );
            }

            const std::int64_t declaredCount =
                requiredNumber(
                    root,
                    "software_interrupt_event_count"
                );

            if (
                declaredCount < 0
                || static_cast<std::size_t>(
                    declaredCount
                ) != softwareInterrupts->array.size()
            ) {
                throw std::runtime_error(
                    label
                    + " multi-process "
                      "software_interrupt_event_count "
                      "does not match software_interrupts"
                );
            }
        }

        (void)requiredField(root, "invariants");

        return TraceDocumentKind::MultiProcess;
    }

    throw std::runtime_error(
        label + " trace uses unsupported schema: " + schema
    );
}

JsonValue selectObjectFields(
    const JsonValue& source,
    const std::vector<std::string>& names
) {
    if (source.type != JsonType::Object) {
        throw std::runtime_error(
            "Cannot select fields from a non-object JSON value"
        );
    }

    std::map<std::string, JsonValue> selected;

    for (const std::string& name : names) {
        selected.emplace(name, requiredField(source, name));
    }

    return JsonValue::makeObject(std::move(selected));
}

JsonValue traceEventArchitecturalView(
    const JsonValue& event
) {
    const std::vector<std::string> eventFields = {
        "index",
        "pc_before",
        "pc_after",
        "sp_before",
        "sp_after",
        "instruction",
        "state_before",
        "state_after",
        "register_changes",
        "flag_changes",
        "memory_changes",
        "has_error",
        "error"
    };

    return selectObjectFields(
        event,
        eventFields
    );
}

JsonValue singleProcessArchitecturalView(
    const JsonValue& root
) {
    const JsonValue& events =
        requiredField(root, "events");

    std::vector<JsonValue> selectedEvents;
    selectedEvents.reserve(events.array.size());

    for (const JsonValue& event : events.array) {
        selectedEvents.push_back(
            traceEventArchitecturalView(event)
        );
    }

    std::map<std::string, JsonValue> selectedRoot;

    selectedRoot.emplace(
        "schema",
        requiredField(root, "schema")
    );

    selectedRoot.emplace(
        "schema_version",
        requiredField(root, "schema_version")
    );

    selectedRoot.emplace(
        "mode",
        requiredField(root, "mode")
    );

    selectedRoot.emplace(
        "event_count",
        requiredField(root, "event_count")
    );

    selectedRoot.emplace(
        "events",
        JsonValue::makeArray(
            std::move(selectedEvents)
        )
    );

    return JsonValue::makeObject(
        std::move(selectedRoot)
    );
}

JsonValue multiProcessInvariantArchitecturalView(
    const JsonValue& invariants
) {
    const JsonValue& violations =
        requiredField(
            invariants,
            "violations"
        );

    if (violations.type != JsonType::Array) {
        throw std::runtime_error(
            "Multi-process invariant violations "
            "must be an array"
        );
    }

    std::vector<JsonValue> selectedViolations;
    selectedViolations.reserve(
        violations.array.size()
    );

    const std::vector<std::string>
        violationFields = {
            "code",
            "lifecycle_step",
            "pid"
        };

    for (
        const JsonValue& violation :
            violations.array
    ) {
        selectedViolations.push_back(
            selectObjectFields(
                violation,
                violationFields
            )
        );
    }

    std::map<std::string, JsonValue> selected;

    selected.emplace(
        "passed",
        requiredField(
            invariants,
            "passed"
        )
    );

    selected.emplace(
        "violation_count",
        requiredField(
            invariants,
            "violation_count"
        )
    );

    selected.emplace(
        "violations",
        JsonValue::makeArray(
            std::move(selectedViolations)
        )
    );

    return JsonValue::makeObject(
        std::move(selected)
    );
}

JsonValue multiProcessArchitecturalView(
    const JsonValue& root
) {
    const JsonValue& executionTrace =
        requiredField(
            root,
            "execution_trace"
        );

    const JsonValue& contextSwitches =
        requiredField(
            root,
            "context_switches"
        );

    const JsonValue& processes =
        requiredField(
            root,
            "processes"
        );

    const JsonValue* softwareInterrupts =
        optionalField(
            root,
            "software_interrupts"
        );

    std::vector<JsonValue> selectedExecution;
    selectedExecution.reserve(
        executionTrace.array.size()
    );

    for (
        const JsonValue& record :
            executionTrace.array
    ) {
        std::map<std::string, JsonValue> selected;

        selected.emplace(
            "lifecycle_step",
            requiredField(
                record,
                "lifecycle_step"
            )
        );

        selected.emplace(
            "pid",
            requiredField(
                record,
                "pid"
            )
        );

        selected.emplace(
            "event",
            traceEventArchitecturalView(
                requiredField(
                    record,
                    "event"
                )
            )
        );

        selectedExecution.push_back(
            JsonValue::makeObject(
                std::move(selected)
            )
        );
    }

    std::vector<JsonValue>
        selectedSoftwareInterrupts;

    if (softwareInterrupts != nullptr) {
        selectedSoftwareInterrupts.reserve(
            softwareInterrupts->array.size()
        );

        const std::vector<std::string>
            softwareInterruptFields = {
                "lifecycle_step",
                "pid",
                "vector",
                "has_service_number",
                "service_number",
                "has_argument0",
                "argument0",
                "has_argument1",
                "argument1",
                "has_status",
                "status",
                "has_result",
                "result_value",
                "disposition",
                "exit_code"
            };

        for (
            const JsonValue& record :
                softwareInterrupts->array
        ) {
            selectedSoftwareInterrupts.push_back(
                selectObjectFields(
                    record,
                    softwareInterruptFields
                )
            );
        }
    }

    std::vector<JsonValue>
        selectedContextSwitches;

    selectedContextSwitches.reserve(
        contextSwitches.array.size()
    );

    const std::vector<std::string>
        contextSwitchFields = {
            "lifecycle_step",
            "from_pid",
            "to_pid",
            "preempted",
            "caused_by_termination"
        };

    for (
        const JsonValue& record :
            contextSwitches.array
    ) {
        selectedContextSwitches.push_back(
            selectObjectFields(
                record,
                contextSwitchFields
            )
        );
    }

    std::vector<JsonValue> selectedProcesses;
    selectedProcesses.reserve(
        processes.array.size()
    );

    const std::vector<std::string>
        processFields = {
            "pid",
            "state",
            "has_exit_code",
            "exit_code",
            "termination_kind",
            "termination_message",
            "pc",
            "sp",
            "has_executable_image",
            "code_end_exclusive",
            "data_base",
            "data_size"
        };

    for (
        const JsonValue& process :
            processes.array
    ) {
        selectedProcesses.push_back(
            selectObjectFields(
                process,
                processFields
            )
        );
    }

    std::map<std::string, JsonValue> selectedRoot;

    const std::vector<std::string> rootFields = {
        "schema",
        "schema_version",
        "mode",
        "runtime_state",
        "step_limit_reached",
        "lifecycle_steps",
        "process_count",
        "termination_count",
        "fault_count",
        "preemption_count",
        "context_switch_count",
        "execution_event_count",
        "context_switch_event_count"
    };

    for (const std::string& field : rootFields) {
        selectedRoot.emplace(
            field,
            requiredField(root, field)
        );
    }

    selectedRoot.emplace(
        "software_interrupt_event_count",
        softwareInterrupts != nullptr
            ? requiredField(
                root,
                "software_interrupt_event_count"
            )
            : JsonValue::makeNumber(0)
    );

    selectedRoot.emplace(
        "invariants",
        multiProcessInvariantArchitecturalView(
            requiredField(
                root,
                "invariants"
            )
        )
    );

    selectedRoot.emplace(
        "execution_trace",
        JsonValue::makeArray(
            std::move(selectedExecution)
        )
    );

    selectedRoot.emplace(
        "software_interrupts",
        JsonValue::makeArray(
            std::move(
                selectedSoftwareInterrupts
            )
        )
    );

    selectedRoot.emplace(
        "context_switches",
        JsonValue::makeArray(
            std::move(selectedContextSwitches)
        )
    );

    selectedRoot.emplace(
        "processes",
        JsonValue::makeArray(
            std::move(selectedProcesses)
        )
    );

    return JsonValue::makeObject(
        std::move(selectedRoot)
    );
}

JsonValue architecturalView(
    const JsonValue& root,
    TraceDocumentKind kind
) {
    switch (kind) {
    case TraceDocumentKind::SingleProcess:
        return singleProcessArchitecturalView(
            root
        );

    case TraceDocumentKind::MultiProcess:
        return multiProcessArchitecturalView(
            root
        );
    }

    throw std::runtime_error(
        "Invalid trace document kind"
    );
}

std::string typeName(JsonType type) {
    switch (type) {
    case JsonType::Null:
        return "null";
    case JsonType::Boolean:
        return "boolean";
    case JsonType::Number:
        return "number";
    case JsonType::String:
        return "string";
    case JsonType::Array:
        return "array";
    case JsonType::Object:
        return "object";
    }

    return "unknown";
}

std::string summarize(const JsonValue& value) {
    std::ostringstream oss;

    switch (value.type) {
    case JsonType::Null:
        return "null";

    case JsonType::Boolean:
        return value.boolean ? "true" : "false";

    case JsonType::Number:
        return std::to_string(value.number);

    case JsonType::String: {
        constexpr std::size_t kMaxLength = 120;
        std::string text = value.string;

        if (text.size() > kMaxLength) {
            text.resize(kMaxLength);
            text += "...";
        }

        oss << '"' << text << '"';
        return oss.str();
    }

    case JsonType::Array:
        oss << "<array size=" << value.array.size() << ">";
        return oss.str();

    case JsonType::Object:
        oss << "<object fields=" << value.object.size() << ">";
        return oss.str();
    }

    return "<unknown>";
}

std::string childPath(
    const std::string& parent,
    const std::string& key
) {
    bool identifier =
        !key.empty() &&
        (std::isalpha(
             static_cast<unsigned char>(key[0])
         ) ||
         key[0] == '_');

    for (std::size_t i = 1;
         identifier && i < key.size();
         ++i) {
        const unsigned char ch =
            static_cast<unsigned char>(key[i]);

        if (!std::isalnum(ch) && ch != '_') {
            identifier = false;
        }
    }

    if (identifier) {
        return parent + "." + key;
    }

    return parent + "[\"" + key + "\"]";
}

struct DiffAccumulator {
    std::size_t count = 0;
    std::string firstPath;
    std::string firstExpected;
    std::string firstActual;

    void add(
        const std::string& path,
        const std::string& expected,
        const std::string& actual
    ) {
        ++count;

        if (firstPath.empty()) {
            firstPath = path;
            firstExpected = expected;
            firstActual = actual;
        }
    }
};

void compareValues(
    const JsonValue& expected,
    const JsonValue& actual,
    const std::string& path,
    DiffAccumulator& differences
) {
    if (expected.type != actual.type) {
        differences.add(
            path,
            "<" + typeName(expected.type) + "> " +
                summarize(expected),
            "<" + typeName(actual.type) + "> " +
                summarize(actual)
        );
        return;
    }

    switch (expected.type) {
    case JsonType::Null:
        return;

    case JsonType::Boolean:
        if (expected.boolean != actual.boolean) {
            differences.add(
                path,
                summarize(expected),
                summarize(actual)
            );
        }
        return;

    case JsonType::Number:
        if (expected.number != actual.number) {
            differences.add(
                path,
                summarize(expected),
                summarize(actual)
            );
        }
        return;

    case JsonType::String:
        if (expected.string != actual.string) {
            differences.add(
                path,
                summarize(expected),
                summarize(actual)
            );
        }
        return;

    case JsonType::Array: {
        if (expected.array.size() != actual.array.size()) {
            differences.add(
                path + ".length",
                std::to_string(expected.array.size()),
                std::to_string(actual.array.size())
            );
        }

        const std::size_t commonSize =
            expected.array.size() < actual.array.size()
                ? expected.array.size()
                : actual.array.size();

        for (std::size_t i = 0; i < commonSize; ++i) {
            compareValues(
                expected.array[i],
                actual.array[i],
                path + "[" + std::to_string(i) + "]",
                differences
            );
        }

        return;
    }

    case JsonType::Object: {
        for (const auto& entry : expected.object) {
            const std::string nextPath =
                childPath(path, entry.first);

            const auto actualIt =
                actual.object.find(entry.first);

            if (actualIt == actual.object.end()) {
                differences.add(
                    nextPath,
                    summarize(entry.second),
                    "<missing>"
                );
                continue;
            }

            compareValues(
                entry.second,
                actualIt->second,
                nextPath,
                differences
            );
        }

        for (const auto& entry : actual.object) {
            if (expected.object.find(entry.first) !=
                expected.object.end()) {
                continue;
            }

            differences.add(
                childPath(path, entry.first),
                "<missing>",
                summarize(entry.second)
            );
        }

        return;
    }
    }
}

std::string readFileText(const std::string& path) {
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error(
            "Failed to open trace JSON file: " + path
        );
    }

    std::ostringstream oss;
    oss << file.rdbuf();

    if (!file.good() && !file.eof()) {
        throw std::runtime_error(
            "Failed to read trace JSON file: " + path
        );
    }

    return oss.str();
}

} // namespace

TraceJsonDiffResult TraceJsonDiff::compareText(
    const std::string& expectedJson,
    const std::string& actualJson,
    const TraceJsonDiffOptions& options
) {
    JsonParser expectedParser(expectedJson);
    JsonParser actualParser(actualJson);

    const JsonValue expectedRoot = expectedParser.parse();
    const JsonValue actualRoot = actualParser.parse();

    const TraceDocumentKind expectedKind =
        validateTraceDocument(
            expectedRoot,
            "Expected"
        );

    const TraceDocumentKind actualKind =
        validateTraceDocument(
            actualRoot,
            "Actual"
        );

    if (expectedKind != actualKind) {
        throw std::runtime_error(
            "Trace schemas are not comparable"
        );
    }

    const JsonValue expectedView =
        options.strict
            ? expectedRoot
            : architecturalView(
                expectedRoot,
                expectedKind
            );

    const JsonValue actualView =
        options.strict
            ? actualRoot
            : architecturalView(
                actualRoot,
                actualKind
            );

    DiffAccumulator differences;

    compareValues(
        expectedView,
        actualView,
        "$",
        differences
    );

    TraceJsonDiffResult result;
    result.equal = differences.count == 0;
    result.difference_count = differences.count;
    result.first_path = differences.firstPath;
    result.expected_value = differences.firstExpected;
    result.actual_value = differences.firstActual;

    if (result.equal) {
        result.message = options.strict
            ? "Strict trace match"
            : "Architectural trace match";
    } else {
        std::ostringstream oss;
        oss << differences.count
            << " trace difference(s); first at "
            << differences.firstPath;
        result.message = oss.str();
    }

    return result;
}

TraceJsonDiffResult TraceJsonDiff::compareFiles(
    const std::string& expectedPath,
    const std::string& actualPath,
    const TraceJsonDiffOptions& options
) {
    return compareText(
        readFileText(expectedPath),
        readFileText(actualPath),
        options
    );
}

// Patch: v1.3-multiprocess-structural-diff-golden-r1

} // namespace zero_cpu

// Patch: v1.5-protected-syscall-trace-diff-r1
