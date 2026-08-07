#include "zero_cpu/debug/DebugConsole.hpp"

#include "zero_cpu/debug/DebugInspector.hpp"
#include "zero_cpu/debug/DebugSnapshotJson.hpp"

#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace zero_cpu::debug {
namespace {

void requireNoExtra(
    std::istringstream& parser,
    const std::string& command
) {
    std::string extra;

    if (parser >> extra) {
        throw std::runtime_error(
            command
            + " received an unexpected argument: "
            + extra
        );
    }
}

std::string requireArgument(
    std::istringstream& parser,
    const std::string& command,
    const std::string& name
) {
    std::string value;

    if (!(parser >> value)) {
        throw std::runtime_error(
            command + " requires " + name
        );
    }

    return value;
}

std::size_t parseOptionalCount(
    std::istringstream& parser,
    const std::string& command,
    std::size_t defaultValue
) {
    std::string text;

    if (!(parser >> text)) {
        return defaultValue;
    }

    requireNoExtra(parser, command);

    if (
        text.empty()
        || text.front() == '-'
    ) {
        throw std::runtime_error(
            command
            + " count must be a positive integer"
        );
    }

    std::size_t parsed = 0;
    const unsigned long long value =
        std::stoull(text, &parsed, 0);

    if (
        parsed != text.size()
        || value == 0
        || value
            > static_cast<unsigned long long>(
                std::numeric_limits<
                    std::size_t
                >::max()
            )
    ) {
        throw std::runtime_error(
            command
            + " count must be a positive integer"
        );
    }

    return static_cast<std::size_t>(value);
}

bool isComment(const std::string& command) {
    return !command.empty()
        && (
            command.front() == '#'
            || command.front() == ';'
        );
}

} // namespace

bool DebugConsoleRunResult::success() const {
    return command_error_count == 0;
}

DebugConsole::DebugConsole(
    DebugSession& session,
    std::istream& input,
    std::ostream& output,
    std::ostream& error,
    DebugConsoleOptions options
)
    : session_(session),
      input_(input),
      output_(output),
      error_(error),
      options_(options) {
    if (!session_.loaded()) {
        throw std::runtime_error(
            "Debug console requires a loaded session"
        );
    }

    if (
        options_.default_continue_steps
        == 0
    ) {
        throw std::runtime_error(
            "Debug console continue limit must "
            "be greater than zero"
        );
    }
}

DebugConsoleRunResult DebugConsole::run() {
    DebugConsoleRunResult result;

    if (options_.print_banner) {
        printBanner();
    }

    std::string line;

    while (true) {
        if (options_.show_prompt) {
            output_ << "(zdbg) ";
            output_.flush();
        }

        if (!std::getline(input_, line)) {
            result.eof_reached = true;
            break;
        }

        std::istringstream preview(line);
        std::string command;

        if (!(preview >> command)) {
            continue;
        }

        if (isComment(command)) {
            continue;
        }

        ++result.command_count;

        try {
            if (executeCommand(line)) {
                result.quit_requested = true;
                break;
            }
        } catch (const std::exception& ex) {
            ++result.command_error_count;
            error_
                << "error: "
                << ex.what()
                << "\n";
        }
    }

    return result;
}

bool DebugConsole::executeCommand(
    const std::string& line
) {
    std::istringstream parser(line);
    std::string command;
    parser >> command;

    if (
        command == "help"
        || command == "h"
        || command == "?"
    ) {
        requireNoExtra(parser, command);
        printHelp();
        return false;
    }

    if (
        command == "quit"
        || command == "q"
        || command == "exit"
    ) {
        requireNoExtra(parser, command);
        output_ << "Debugger session closed.\n";
        return true;
    }

    if (
        command == "status"
        || command == "st"
    ) {
        requireNoExtra(parser, command);
        printStatus(session_.lastStop());
        return false;
    }

    if (
        command == "step"
        || command == "s"
    ) {
        const std::size_t count =
            parseOptionalCount(
                parser,
                command,
                1
            );

        DebugStop stop = session_.lastStop();

        for (
            std::size_t index = 0;
            index < count;
            ++index
        ) {
            stop = session_.step();

            if (
                stop.reason
                != DebugStopReason::StepComplete
            ) {
                break;
            }
        }

        printStatus(stop);
        return false;
    }

    if (
        command == "continue"
        || command == "c"
    ) {
        const std::size_t count =
            parseOptionalCount(
                parser,
                command,
                options_.default_continue_steps
            );

        printStatus(
            session_.continueExecution(count)
        );

        return false;
    }

    if (
        command == "break"
        || command == "b"
    ) {
        const std::size_t address =
            parseAddress(
                requireArgument(
                    parser,
                    command,
                    "an address"
                )
            );

        requireNoExtra(parser, command);

        if (session_.addBreakpoint(address)) {
            output_
                << "Breakpoint "
                << address
                << " added.\n";
        } else {
            output_
                << "Breakpoint "
                << address
                << " already exists.\n";
        }

        return false;
    }

    if (
        command == "delete"
        || command == "d"
    ) {
        const std::size_t address =
            parseAddress(
                requireArgument(
                    parser,
                    command,
                    "an address"
                )
            );

        requireNoExtra(parser, command);

        if (session_.removeBreakpoint(address)) {
            output_
                << "Breakpoint "
                << address
                << " deleted.\n";
        } else {
            output_
                << "No breakpoint at "
                << address
                << ".\n";
        }

        return false;
    }

    if (command == "clear") {
        requireNoExtra(parser, command);
        session_.clearBreakpoints();
        output_ << "All breakpoints cleared.\n";
        return false;
    }

    if (
        command == "breakpoints"
        || command == "bl"
    ) {
        requireNoExtra(parser, command);
        printBreakpoints();
        return false;
    }

    if (
        command == "symbols"
        || command == "sym"
    ) {
        requireNoExtra(parser, command);
        printSymbols();
        return false;
    }

    if (
        command == "break-label"
        || command == "bs"
    ) {
        const std::string name =
            requireArgument(
                parser,
                command,
                "a code label"
            );

        requireNoExtra(parser, command);

        const std::size_t address =
            session_.resolveCodeSymbol(
                name
            );

        if (session_.addBreakpoint(address)) {
            output_
                << "Breakpoint at label "
                << name
                << " ("
                << address
                << ") added.\n";
        } else {
            output_
                << "Breakpoint at label "
                << name
                << " ("
                << address
                << ") already exists.\n";
        }

        return false;
    }

    if (command == "break-if-label") {
        const std::string name =
            requireArgument(
                parser,
                command,
                "a code label"
            );

        const std::string source =
            requireArgument(
                parser,
                command,
                "a condition source"
            );

        const std::string operation =
            requireArgument(
                parser,
                command,
                "a comparison operator"
            );

        const std::string value =
            requireArgument(
                parser,
                command,
                "a comparison value"
            );

        requireNoExtra(parser, command);

        const std::size_t address =
            session_.resolveCodeSymbol(
                name
            );

        const DebugCondition condition =
            parseDebugCondition(
                source,
                operation,
                value
            );

        const std::size_t id =
            session_.addConditionalBreakpoint(
                address,
                condition
            );

        output_
            << "Conditional breakpoint "
            << id
            << " added at label "
            << name
            << " ("
            << address
            << "): "
            << condition.expression
            << ".\n";

        return false;
    }

    if (command == "disassemble-label") {
        const std::string name =
            requireArgument(
                parser,
                command,
                "a code label"
            );

        const std::size_t count =
            parsePositiveCount(
                requireArgument(
                    parser,
                    command,
                    "an instruction count"
                ),
                command
            );

        requireNoExtra(parser, command);

        output_
            << DebugInspector::formatDisassembly(
                DebugInspector::disassemble(
                    session_,
                    session_.resolveCodeSymbol(
                        name
                    ),
                    count
                )
            )
            << "\n";

        return false;
    }

    if (command == "memory-label") {
        const std::string name =
            requireArgument(
                parser,
                command,
                "a data label"
            );

        const std::size_t count =
            parsePositiveCount(
                requireArgument(
                    parser,
                    command,
                    "a byte count"
                ),
                command
            );

        requireNoExtra(parser, command);

        output_
            << DebugInspector::formatMemory(
                DebugInspector::inspectMemory(
                    session_,
                    session_.resolveDataSymbol(
                        name
                    ),
                    count
                )
            )
            << "\n";

        return false;
    }

    if (
        command == "break-if"
        || command == "bi"
    ) {
        const std::size_t address =
            parseAddress(
                requireArgument(
                    parser,
                    command,
                    "an address"
                )
            );

        const std::string source =
            requireArgument(
                parser,
                command,
                "a condition source"
            );

        const std::string operation =
            requireArgument(
                parser,
                command,
                "a comparison operator"
            );

        const std::string value =
            requireArgument(
                parser,
                command,
                "a comparison value"
            );

        requireNoExtra(parser, command);

        const DebugCondition condition =
            parseDebugCondition(
                source,
                operation,
                value
            );

        const std::size_t id =
            session_.addConditionalBreakpoint(
                address,
                condition
            );

        output_
            << "Conditional breakpoint "
            << id
            << " added at "
            << address
            << ": "
            << condition.expression
            << ".\n";

        return false;
    }

    if (
        command == "delete-if"
        || command == "di"
    ) {
        const std::size_t id =
            parsePositiveCount(
                requireArgument(
                    parser,
                    command,
                    "a conditional breakpoint ID"
                ),
                command
            );

        requireNoExtra(parser, command);

        if (
            session_.removeConditionalBreakpoint(
                id
            )
        ) {
            output_
                << "Conditional breakpoint "
                << id
                << " deleted.\n";
        } else {
            output_
                << "No conditional breakpoint "
                << "with ID "
                << id
                << ".\n";
        }

        return false;
    }

    if (
        command == "conditions"
        || command == "cl"
    ) {
        requireNoExtra(parser, command);
        printConditionalBreakpoints();
        return false;
    }

    if (command == "clear-conditions") {
        requireNoExtra(parser, command);

        session_.clearConditionalBreakpoints();

        output_
            << "All conditional breakpoints "
            << "cleared.\n";

        return false;
    }

    if (
        command == "watch"
        || command == "w"
    ) {
        const MemoryWatchMode mode =
            parseWatchMode(
                requireArgument(
                    parser,
                    command,
                    "read, write, or access"
                )
            );

        const std::size_t address =
            parseAddress(
                requireArgument(
                    parser,
                    command,
                    "an address"
                )
            );

        const std::size_t size =
            parseOptionalCount(
                parser,
                command,
                sizeof(std::int64_t)
            );

        const std::size_t id =
            session_.addWatchpoint(
                address,
                size,
                mode
            );

        output_
            << "Watchpoint "
            << id
            << " added: "
            << memoryWatchModeToString(mode)
            << " ["
            << address
            << ", "
            << address + size
            << ").\n";

        return false;
    }

    if (
        command == "unwatch"
        || command == "uw"
    ) {
        const std::size_t id =
            parsePositiveCount(
                requireArgument(
                    parser,
                    command,
                    "a watchpoint ID"
                ),
                command
            );

        requireNoExtra(parser, command);

        if (session_.removeWatchpoint(id)) {
            output_
                << "Watchpoint "
                << id
                << " deleted.\n";
        } else {
            output_
                << "No watchpoint with ID "
                << id
                << ".\n";
        }

        return false;
    }

    if (
        command == "watchpoints"
        || command == "wl"
    ) {
        requireNoExtra(parser, command);
        printWatchpoints();
        return false;
    }

    if (command == "clear-watchpoints") {
        requireNoExtra(parser, command);
        session_.clearWatchpoints();

        output_
            << "All watchpoints cleared.\n";

        return false;
    }

    if (
        command == "snapshot-json"
        || command == "snapshot"
    ) {
        const std::string path =
            requireArgument(
                parser,
                command,
                "an output path"
            );

        DebugSnapshotOptions options;

        std::string addressText;

        if (parser >> addressText) {
            options.memory_address =
                parseAddress(addressText);

            options.memory_size =
                parsePositiveCount(
                    requireArgument(
                        parser,
                        command,
                        "a memory byte count"
                    ),
                    command
                );

            requireNoExtra(
                parser,
                command
            );
        }

        DebugSnapshotJsonWriter::writeFile(
            path,
            session_,
            options
        );

        output_
            << "Debug snapshot JSON written: "
            << path
            << "\n";

        return false;
    }

    if (
        command == "registers"
        || command == "r"
    ) {
        requireNoExtra(parser, command);

        output_
            << DebugInspector::formatRegisters(
                DebugInspector::inspectRegisters(
                    session_
                )
            )
            << "\n";

        return false;
    }

    if (
        command == "memory"
        || command == "x"
    ) {
        const std::size_t address =
            parseAddress(
                requireArgument(
                    parser,
                    command,
                    "an address"
                )
            );

        const std::size_t count =
            parsePositiveCount(
                requireArgument(
                    parser,
                    command,
                    "a byte count"
                ),
                command
            );

        requireNoExtra(parser, command);

        output_
            << DebugInspector::formatMemory(
                DebugInspector::inspectMemory(
                    session_,
                    address,
                    count
                )
            )
            << "\n";

        return false;
    }

    if (
        command == "disassemble"
        || command == "disas"
    ) {
        const std::size_t address =
            parseAddress(
                requireArgument(
                    parser,
                    command,
                    "an address"
                )
            );

        const std::size_t count =
            parsePositiveCount(
                requireArgument(
                    parser,
                    command,
                    "an instruction count"
                ),
                command
            );

        requireNoExtra(parser, command);

        output_
            << DebugInspector::formatDisassembly(
                DebugInspector::disassemble(
                    session_,
                    address,
                    count
                )
            )
            << "\n";

        return false;
    }

    if (
        command == "trace"
        || command == "t"
    ) {
        requireNoExtra(parser, command);
        printLastTrace();
        return false;
    }

    throw std::runtime_error(
        "unknown debugger command: "
        + command
    );
}

void DebugConsole::printBanner() {
    output_
        << "Zero-CPU Interactive Debugger\n"
        << "Executable: "
        << session_.sourceName()
        << "\n"
        << "Code range: ["
        << session_.metadata().code_base
        << ", "
        << session_.metadata()
            .code_end_exclusive
        << ")\n"
        << "Entry point: "
        << session_.metadata().entry_point
        << "\n"
        << "Type 'help' for commands.\n";
}

void DebugConsole::printHelp() {
    output_
        << "Commands:\n"
        << "  help\n"
        << "  status\n"
        << "  step [count]\n"
        << "  continue [max-steps]\n"
        << "  break <address>\n"
        << "  delete <address>\n"
        << "  clear\n"
        << "  breakpoints\n"
        << "  symbols\n"
        << "  break-label <code-label>\n"
        << "  break-if-label <code-label> <source> <op> <value>\n"
        << "  disassemble-label <code-label> <instructions>\n"
        << "  memory-label <data-label> <bytes>\n"
        << "  break-if <address> <source> <op> <value>\n"
        << "  delete-if <ID>\n"
        << "  conditions\n"
        << "  clear-conditions\n"
        << "  watch <read|write|access> <address> [bytes]\n"
        << "  unwatch <ID>\n"
        << "  watchpoints\n"
        << "  clear-watchpoints\n"
        << "  registers\n"
        << "  memory <address> <bytes>\n"
        << "  disassemble <address> <instructions>\n"
        << "  snapshot-json <path> [memory-address bytes]\n"
        << "  trace\n"
        << "  quit\n";
}

void DebugConsole::printStatus(
    const DebugStop& stop
) {
    output_
        << "Stop reason: "
        << debugStopReasonToString(stop.reason)
        << "\n"
        << "PC: "
        << stop.pc
        << "\n"
        << "Executed steps: "
        << stop.executed_steps
        << "\n"
        << "Total steps: "
        << stop.total_steps
        << "\n";

    if (stop.has_conditional_breakpoint) {
        output_
            << "Conditional breakpoint ID: "
            << stop.conditional_breakpoint_id
            << "\n"
            << "Condition: "
            << stop.conditional_expression
            << "\n"
            << "Condition actual value: "
            << stop.conditional_actual_value
            << "\n";
    }

    if (stop.has_watchpoint) {
        output_
            << "Watchpoint ID: "
            << stop.watchpoint_id
            << "\n"
            << "Watch range: ["
            << stop.watchpoint_address
            << ", "
            << (
                stop.watchpoint_address
                + stop.watchpoint_size
            )
            << ") "
            << memoryWatchModeToString(
                stop.watchpoint_mode
            )
            << "\n"
            << "Memory access: "
            << memoryWatchModeToString(
                stop.access_mode
            )
            << " "
            << stop.access_address
            << "\n";
    }

    if (!stop.message.empty()) {
        output_
            << "Message: "
            << stop.message
            << "\n";
    }
}

void DebugConsole::printBreakpoints() {
    const std::vector<std::size_t> values =
        session_.breakpoints();

    if (values.empty()) {
        output_ << "No breakpoints.\n";
        return;
    }

    output_ << "Breakpoints:";

    for (const std::size_t address : values) {
        output_
            << " "
            << address;
    }

    output_ << "\n";
}

void DebugConsole::printConditionalBreakpoints() {
    const std::vector<ConditionalBreakpoint>
        values =
            session_.conditionalBreakpoints();

    if (values.empty()) {
        output_
            << "No conditional breakpoints.\n";

        return;
    }

    output_
        << "Conditional breakpoints:\n";

    for (
        const ConditionalBreakpoint& breakpoint :
        values
    ) {
        output_
            << "  "
            << breakpoint.id
            << " @"
            << breakpoint.address
            << " when "
            << breakpoint.condition.expression
            << "\n";
    }
}

void DebugConsole::printSymbols() {
    if (!session_.hasSymbols()) {
        output_
            << "No debug symbols loaded.\n";

        return;
    }

    output_ << "Debug symbols:\n";

    for (
        const DebugSymbol& symbol :
        session_.symbols().entries()
    ) {
        output_
            << "  "
            << debugSymbolKindToString(
                symbol.kind
            )
            << " "
            << symbol.name
            << " = "
            << symbol.address
            << "\n";
    }
}

void DebugConsole::printWatchpoints() {
    const std::vector<MemoryWatchpoint> values =
        session_.watchpoints();

    if (values.empty()) {
        output_ << "No watchpoints.\n";
        return;
    }

    output_ << "Watchpoints:\n";

    for (
        const MemoryWatchpoint& watchpoint :
        values
    ) {
        output_
            << "  "
            << watchpoint.id
            << " "
            << memoryWatchModeToString(
                watchpoint.mode
            )
            << " ["
            << watchpoint.address
            << ", "
            << watchpoint.endExclusive()
            << ")\n";
    }
}

void DebugConsole::printLastTrace() {
    if (
        session_.cpu()
            .traceLogger()
            .empty()
    ) {
        output_ << "No trace events.\n";
        return;
    }

    output_
        << session_.cpu()
            .traceLogger()
            .last()
            .toCompactString()
        << "\n";
}

MemoryWatchMode DebugConsole::parseWatchMode(
    const std::string& text
) {
    if (text == "read" || text == "r") {
        return MemoryWatchMode::Read;
    }

    if (text == "write" || text == "w") {
        return MemoryWatchMode::Write;
    }

    if (
        text == "access"
        || text == "a"
        || text == "rw"
    ) {
        return MemoryWatchMode::Access;
    }

    throw std::runtime_error(
        "watch mode must be read, write, or access"
    );
}

std::size_t DebugConsole::parseAddress(
    const std::string& text
) {
    if (
        text.empty()
        || text.front() == '-'
    ) {
        throw std::runtime_error(
            "address must be a non-negative integer"
        );
    }

    std::size_t parsed = 0;
    const unsigned long long value =
        std::stoull(text, &parsed, 0);

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
            "address must be a non-negative integer"
        );
    }

    return static_cast<std::size_t>(value);
}

std::size_t DebugConsole::parsePositiveCount(
    const std::string& text,
    const std::string& context
) {
    if (
        text.empty()
        || text.front() == '-'
    ) {
        throw std::runtime_error(
            context
            + " count must be a positive integer"
        );
    }

    std::size_t parsed = 0;
    const unsigned long long value =
        std::stoull(text, &parsed, 0);

    if (
        parsed != text.size()
        || value == 0
        || value
            > static_cast<unsigned long long>(
                std::numeric_limits<
                    std::size_t
                >::max()
            )
    ) {
        throw std::runtime_error(
            context
            + " count must be a positive integer"
        );
    }

    return static_cast<std::size_t>(value);
}

} // namespace zero_cpu::debug
