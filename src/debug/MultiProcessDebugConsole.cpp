#include "zero_cpu/debug/MultiProcessDebugConsole.hpp"

#include "zero_cpu/core/PrivilegeLevel.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"
#include "zero_cpu/kernel/ProcessTermination.hpp"

#include <exception>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

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
            command
            + " requires "
            + name
        );
    }

    return value;
}

std::size_t optionalPositiveCount(
    std::istringstream& parser,
    const std::string& command,
    std::size_t defaultValue
) {
    std::string text;

    if (!(parser >> text)) {
        return defaultValue;
    }

    requireNoExtra(
        parser,
        command
    );

    if (
        text.empty()
        || text.front() == '-'
    ) {
        throw std::runtime_error(
            command
            + " count must be a positive integer"
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

        return static_cast<std::size_t>(
            value
        );
    } catch (const std::runtime_error&) {
        throw;
    } catch (const std::exception&) {
        throw std::runtime_error(
            command
            + " count must be a positive integer"
        );
    }
}

bool parseOptionalPid(
    std::istringstream& parser,
    const std::string& command,
    kernel::ProcessId& pid
) {
    std::string text;

    if (!(parser >> text)) {
        return false;
    }

    requireNoExtra(parser, command);

    if (
        text.empty()
        || text.front() == '-'
    ) {
        throw std::runtime_error(
            command
            + " PID must be a positive integer"
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
            || value == 0
            || value
                > static_cast<unsigned long long>(
                    std::numeric_limits<
                        kernel::ProcessId
                    >::max()
                )
        ) {
            throw std::runtime_error(
                command
                + " PID must be a positive integer"
            );
        }

        pid = static_cast<
            kernel::ProcessId
        >(value);

        return true;
    } catch (const std::runtime_error&) {
        throw;
    } catch (const std::exception&) {
        throw std::runtime_error(
            command
            + " PID must be a positive integer"
        );
    }
}

bool isComment(
    const std::string& command
) {
    return !command.empty()
        && (
            command.front() == '#'
            || command.front() == ';'
        );
}

} // namespace

bool MultiProcessDebugConsoleResult::success()
const {
    return command_error_count == 0;
}

MultiProcessDebugConsole::
MultiProcessDebugConsole(
    MultiProcessDebugSession& session,
    std::istream& input,
    std::ostream& output,
    std::ostream& error,
    MultiProcessDebugConsoleOptions options
)
    : session_(session),
      input_(input),
      output_(output),
      error_(error),
      options_(options) {
    if (
        options_.default_continue_steps
        == 0
    ) {
        throw std::runtime_error(
            "Debugger console continue limit "
            "must be greater than zero"
        );
    }
}

MultiProcessDebugConsoleResult
MultiProcessDebugConsole::run() {
    MultiProcessDebugConsoleResult result;

    if (options_.print_banner) {
        printBanner();
    }

    std::string line;

    while (true) {
        if (options_.show_prompt) {
            output_ << "(zmpdbg) ";
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

bool MultiProcessDebugConsole::executeCommand(
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

        output_
            << "Multi-process debugger closed.\n";

        return true;
    }

    if (
        command == "processes"
        || command == "ps"
    ) {
        requireNoExtra(parser, command);
        printProcesses();
        return false;
    }

    if (
        command == "process"
        || command == "p"
    ) {
        const kernel::ProcessId pid =
            parsePid(
                requireArgument(
                    parser,
                    command,
                    "a PID"
                )
            );

        requireNoExtra(parser, command);

        session_.selectProcess(pid);

        output_
            << "Selected PID "
            << pid
            << ".\n";

        printSelectedProcess();
        return false;
    }

    if (
        command == "selected"
        || command == "sel"
    ) {
        requireNoExtra(parser, command);
        printSelectedProcess();
        return false;
    }

    if (
        command == "symbols"
        || command == "sym"
    ) {
        const kernel::ProcessId pid =
            parsePid(
                requireArgument(
                    parser,
                    command,
                    "a PID"
                )
            );

        requireNoExtra(parser, command);
        printSymbols(pid);
        return false;
    }

    if (
        command == "break"
        || command == "b"
    ) {
        const kernel::ProcessId pid =
            parsePid(
                requireArgument(
                    parser,
                    command,
                    "a PID"
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

        requireNoExtra(parser, command);

        if (
            session_.addBreakpoint(
                pid,
                address
            )
        ) {
            output_
                << "PID "
                << pid
                << " breakpoint "
                << address
                << " added.\n";
        } else {
            output_
                << "PID "
                << pid
                << " breakpoint "
                << address
                << " already exists.\n";
        }

        return false;
    }

    if (
        command == "break-label"
        || command == "bs"
    ) {
        const kernel::ProcessId pid =
            parsePid(
                requireArgument(
                    parser,
                    command,
                    "a PID"
                )
            );

        const std::string label =
            requireArgument(
                parser,
                command,
                "a code label"
            );

        requireNoExtra(parser, command);

        const std::size_t address =
            session_.resolveCodeSymbol(
                pid,
                label
            );

        if (
            session_.addBreakpoint(
                pid,
                address
            )
        ) {
            output_
                << "PID "
                << pid
                << " breakpoint at label "
                << label
                << " ("
                << address
                << ") added.\n";
        } else {
            output_
                << "PID "
                << pid
                << " breakpoint at label "
                << label
                << " ("
                << address
                << ") already exists.\n";
        }

        return false;
    }

    if (
        command == "delete"
        || command == "d"
    ) {
        const kernel::ProcessId pid =
            parsePid(
                requireArgument(
                    parser,
                    command,
                    "a PID"
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

        requireNoExtra(parser, command);

        if (
            session_.removeBreakpoint(
                pid,
                address
            )
        ) {
            output_
                << "PID "
                << pid
                << " breakpoint "
                << address
                << " deleted.\n";
        } else {
            output_
                << "No PID "
                << pid
                << " breakpoint at "
                << address
                << ".\n";
        }

        return false;
    }

    if (
        command == "breakpoints"
        || command == "bl"
    ) {
        kernel::ProcessId pid = 0;

        if (
            parseOptionalPid(
                parser,
                command,
                pid
            )
        ) {
            printBreakpoints(
                session_.breakpoints(pid)
            );
        } else {
            printBreakpoints(
                session_.breakpoints()
            );
        }

        return false;
    }

    if (command == "clear-breakpoints") {
        kernel::ProcessId pid = 0;

        if (
            parseOptionalPid(
                parser,
                command,
                pid
            )
        ) {
            session_.clearBreakpoints(pid);

            output_
                << "PID "
                << pid
                << " breakpoints cleared.\n";
        } else {
            session_.clearBreakpoints();

            output_
                << "All process breakpoints "
                << "cleared.\n";
        }

        return false;
    }

    if (
        command == "break-if"
        || command == "bi"
    ) {
        const kernel::ProcessId pid =
            parsePid(
                requireArgument(
                    parser,
                    command,
                    "a PID"
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
                pid,
                address,
                condition
            );

        output_
            << "Conditional breakpoint "
            << id
            << " added for PID "
            << pid
            << " at "
            << address
            << ": "
            << condition.expression
            << ".\n";

        return false;
    }

    if (command == "break-if-label") {
        const kernel::ProcessId pid =
            parsePid(
                requireArgument(
                    parser,
                    command,
                    "a PID"
                )
            );

        const std::string label =
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
                pid,
                label
            );

        const DebugCondition condition =
            parseDebugCondition(
                source,
                operation,
                value
            );

        const std::size_t id =
            session_.addConditionalBreakpoint(
                pid,
                address,
                condition
            );

        output_
            << "Conditional breakpoint "
            << id
            << " added for PID "
            << pid
            << " at label "
            << label
            << " ("
            << address
            << "): "
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
        kernel::ProcessId pid = 0;

        if (
            parseOptionalPid(
                parser,
                command,
                pid
            )
        ) {
            printConditionalBreakpoints(
                session_.conditionalBreakpoints(
                    pid
                )
            );
        } else {
            printConditionalBreakpoints(
                session_.conditionalBreakpoints()
            );
        }

        return false;
    }

    if (command == "clear-conditions") {
        kernel::ProcessId pid = 0;

        if (
            parseOptionalPid(
                parser,
                command,
                pid
            )
        ) {
            session_.clearConditionalBreakpoints(
                pid
            );

            output_
                << "PID "
                << pid
                << " conditional breakpoints "
                << "cleared.\n";
        } else {
            session_.clearConditionalBreakpoints();

            output_
                << "All process conditional "
                << "breakpoints cleared.\n";
        }

        return false;
    }

    if (
        command == "watch"
        || command == "w"
    ) {
        const kernel::ProcessId pid =
            parsePid(
                requireArgument(
                    parser,
                    command,
                    "a PID"
                )
            );

        const ProcessMemoryWatchMode mode =
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
            optionalPositiveCount(
                parser,
                command,
                sizeof(std::int64_t)
            );

        const std::size_t id =
            session_.addWatchpoint(
                pid,
                address,
                size,
                mode
            );

        output_
            << "Watchpoint "
            << id
            << " added for PID "
            << pid
            << ": "
            << processMemoryWatchModeToString(
                mode
            )
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
        kernel::ProcessId pid = 0;

        if (
            parseOptionalPid(
                parser,
                command,
                pid
            )
        ) {
            printWatchpoints(
                session_.watchpoints(pid)
            );
        } else {
            printWatchpoints(
                session_.watchpoints()
            );
        }

        return false;
    }

    if (command == "clear-watchpoints") {
        kernel::ProcessId pid = 0;

        if (
            parseOptionalPid(
                parser,
                command,
                pid
            )
        ) {
            session_.clearWatchpoints(pid);

            output_
                << "PID "
                << pid
                << " watchpoints cleared.\n";
        } else {
            session_.clearWatchpoints();

            output_
                << "All process watchpoints "
                << "cleared.\n";
        }

        return false;
    }

    if (
        command == "step"
        || command == "s"
    ) {
        const std::size_t count =
            optionalPositiveCount(
                parser,
                command,
                1
            );

        MultiProcessDebugStop stop =
            session_.lastStop();

        for (
            std::size_t index = 0;
            index < count;
            ++index
        ) {
            stop = session_.step();

            if (
                stop.reason
                != MultiProcessDebugStopReason::
                    StepComplete
            ) {
                break;
            }
        }

        printStop(stop);
        return false;
    }

    if (
        command == "continue"
        || command == "c"
    ) {
        const std::size_t count =
            optionalPositiveCount(
                parser,
                command,
                options_.default_continue_steps
            );

        printStop(
            session_.continueExecution(
                count
            )
        );

        return false;
    }

    if (
        command == "status"
        || command == "st"
    ) {
        requireNoExtra(parser, command);
        printStop(session_.lastStop());
        return false;
    }

    if (
        command == "scheduler"
        || command == "sched"
    ) {
        requireNoExtra(parser, command);
        printScheduler();
        return false;
    }

    if (
        command == "registers"
        || command == "r"
    ) {
        requireNoExtra(parser, command);
        printRegisters();
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

        printMemory(address, count);
        return false;
    }

    if (
        command == "trace"
        || command == "t"
    ) {
        requireNoExtra(parser, command);
        printTrace();
        return false;
    }

    throw std::runtime_error(
        "unknown multi-process debugger command: "
        + command
    );
}

void MultiProcessDebugConsole::printBanner() {
    output_
        << "Zero-CPU Multi-Process Debugger\n"
        << "Processes: "
        << session_.processSnapshots().size()
        << "\n"
        << "Quantum: "
        << session_.quantum()
        << "\n"
        << "Running PID: "
        << session_.runningPid()
        << "\n"
        << "Type 'help' for commands.\n";
}

void MultiProcessDebugConsole::printHelp() {
    output_
        << "Commands:\n"
        << "  help\n"
        << "  processes\n"
        << "  process <PID>\n"
        << "  selected\n"
        << "  symbols <PID>\n"
        << "  break <PID> <address>\n"
        << "  break-label <PID> <label>\n"
        << "  delete <PID> <address>\n"
        << "  breakpoints [PID]\n"
        << "  clear-breakpoints [PID]\n"
        << "  break-if <PID> <address> <source> <op> <value>\n"
        << "  break-if-label <PID> <label> <source> <op> <value>\n"
        << "  delete-if <ID>\n"
        << "  conditions [PID]\n"
        << "  clear-conditions [PID]\n"
        << "  watch <PID> <read|write|access> <address> [bytes]\n"
        << "  unwatch <ID>\n"
        << "  watchpoints [PID]\n"
        << "  clear-watchpoints [PID]\n"
        << "  step [count]\n"
        << "  continue [max-steps]\n"
        << "  status\n"
        << "  scheduler\n"
        << "  registers\n"
        << "  memory <address> <bytes>\n"
        << "  trace\n"
        << "  quit\n";
}

void MultiProcessDebugConsole::printStop(
    const MultiProcessDebugStop& stop
) {
    output_
        << "Stop reason: "
        << multiProcessDebugStopReasonToString(
            stop.reason
        )
        << "\n"
        << "Runtime state: "
        << kernel::processRuntimeStateToString(
            stop.runtime_state
        )
        << "\n"
        << "Running PID: "
        << stop.running_pid
        << "\n"
        << "Selected PID: "
        << stop.selected_pid
        << "\n"
        << "Executed steps: "
        << stop.executed_steps
        << "\n"
        << "Total steps: "
        << stop.total_steps
        << "\n";

    if (stop.has_debug_hit) {
        output_
            << "Hit PID: "
            << stop.hit_pid
            << "\n"
            << "Hit address: "
            << stop.hit_address
            << "\n";
    }

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
            << processMemoryWatchModeToString(
                stop.watchpoint_mode
            )
            << "\n"
            << "Memory access: "
            << processMemoryWatchModeToString(
                stop.access_mode
            )
            << " "
            << stop.access_address
            << "\n";
    }

    if (stop.process_terminated) {
        output_
            << "Terminated PID: "
            << stop.terminated_pid
            << "\n"
            << "Faulted: "
            << (
                stop.process_faulted
                    ? "true"
                    : "false"
            )
            << "\n";
    }

    if (!stop.message.empty()) {
        output_
            << "Message: "
            << stop.message
            << "\n";
    }
}

void MultiProcessDebugConsole::printProcesses() {
    output_ << "Processes:\n";

    for (
        const ProcessDebugSnapshot& snapshot :
        session_.processSnapshots()
    ) {
        output_
            << (
                snapshot.running
                    ? "> "
                    : "  "
            )
            << (
                snapshot.pid
                    == session_.selectedPid()
                    ? "* "
                    : "  "
            )
            << "PID "
            << snapshot.pid
            << " "
            << kernel::processStateToString(
                snapshot.state
            )
            << " PC="
            << snapshot.context.pc
            << " SP="
            << snapshot.context.sp;

        if (!snapshot.source_name.empty()) {
            output_
                << " "
                << snapshot.source_name;
        }

        if (snapshot.has_exit_code) {
            output_
                << " exit="
                << snapshot.exit_code
                << " "
                << kernel::
                    processTerminationKindToString(
                        snapshot.termination_kind
                    );
        }

        output_ << "\n";
    }
}

void MultiProcessDebugConsole::
printSelectedProcess() {
    printSnapshot(
        session_.selectedProcessSnapshot()
    );
}

void MultiProcessDebugConsole::printSnapshot(
    const ProcessDebugSnapshot& snapshot
) {
    output_
        << "PID "
        << snapshot.pid
        << "\n"
        << "  Source: "
        << (
            snapshot.source_name.empty()
                ? "<unknown>"
                : snapshot.source_name
        )
        << "\n"
        << "  State: "
        << kernel::processStateToString(
            snapshot.state
        )
        << "\n"
        << "  Running: "
        << (
            snapshot.running
                ? "true"
                : "false"
        )
        << "\n"
        << "  PC: "
        << snapshot.context.pc
        << "\n"
        << "  SP: "
        << snapshot.context.sp
        << "\n";

    if (snapshot.has_executable_image) {
        output_
            << "  Code range: ["
            << snapshot.code_base
            << ", "
            << snapshot.code_end_exclusive
            << ")\n"
            << "  Entry point: "
            << snapshot.entry_point
            << "\n";
    }

    if (snapshot.has_exit_code) {
        output_
            << "  Exit code: "
            << snapshot.exit_code
            << "\n"
            << "  Termination: "
            << kernel::
                processTerminationKindToString(
                    snapshot.termination_kind
                )
            << "\n";

        if (
            !snapshot
                .termination_message
                .empty()
        ) {
            output_
                << "  Message: "
                << snapshot
                    .termination_message
                << "\n";
        }
    }
}

void MultiProcessDebugConsole::printSymbols(
    kernel::ProcessId pid
) {
    if (!session_.hasSymbols(pid)) {
        output_
            << "No debug symbols loaded for PID "
            << pid
            << ".\n";

        return;
    }

    output_
        << "Debug symbols for PID "
        << pid
        << ":\n";

    for (
        const DebugSymbol& symbol :
        session_.symbols(pid).entries()
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

void MultiProcessDebugConsole::printBreakpoints(
    const std::vector<ProcessBreakpoint>& values
) {
    if (values.empty()) {
        output_
            << "No process breakpoints.\n";

        return;
    }

    output_
        << "Process breakpoints:\n";

    for (
        const ProcessBreakpoint& breakpoint :
        values
    ) {
        output_
            << "  PID "
            << breakpoint.pid
            << " @"
            << breakpoint.address
            << "\n";
    }
}

void MultiProcessDebugConsole::
printConditionalBreakpoints(
    const std::vector<
        ProcessConditionalBreakpoint
    >& values
) {
    if (values.empty()) {
        output_
            << "No process conditional "
            << "breakpoints.\n";

        return;
    }

    output_
        << "Process conditional breakpoints:\n";

    for (
        const ProcessConditionalBreakpoint&
            breakpoint :
                values
    ) {
        output_
            << "  "
            << breakpoint.id
            << " PID "
            << breakpoint.pid
            << " @"
            << breakpoint.address
            << " when "
            << breakpoint.condition.expression
            << "\n";
    }
}

void MultiProcessDebugConsole::printWatchpoints(
    const std::vector<
        ProcessMemoryWatchpoint
    >& values
) {
    if (values.empty()) {
        output_
            << "No process watchpoints.\n";

        return;
    }

    output_
        << "Process watchpoints:\n";

    for (
        const ProcessMemoryWatchpoint& watchpoint :
        values
    ) {
        output_
            << "  "
            << watchpoint.id
            << " PID "
            << watchpoint.pid
            << " "
            << processMemoryWatchModeToString(
                watchpoint.mode
            )
            << " ["
            << watchpoint.address
            << ", "
            << watchpoint.endExclusive()
            << ")\n";
    }
}

void MultiProcessDebugConsole::printScheduler() {
    output_
        << "Scheduler:\n"
        << "  Runtime state: "
        << kernel::processRuntimeStateToString(
            session_.runtimeState()
        )
        << "\n"
        << "  Quantum: "
        << session_.quantum()
        << "\n"
        << "  Running PID: "
        << session_.runningPid()
        << "\n"
        << "  Selected PID: "
        << session_.selectedPid()
        << "\n"
        << "  Preemptions: "
        << session_.preemptionCount()
        << "\n"
        << "  Scheduler context switches: "
        << session_
            .schedulerContextSwitchCount()
        << "\n"
        << "  Observed switches: "
        << session_.contextSwitches().size()
        << "\n";

    if (!session_.contextSwitches().empty()) {
        const ContextSwitchRecord& last =
            session_.contextSwitches().back();

        output_
            << "  Last switch: step "
            << last.lifecycle_step
            << " PID "
            << last.from_pid
            << " -> "
            << last.to_pid
            << " reason="
            << (
                last.caused_by_termination
                    ? "termination"
                    : (
                        last.preempted
                            ? "preemption"
                            : "dispatch"
                    )
            )
            << "\n";
    }
}

void MultiProcessDebugConsole::printRegisters() {
    const ProcessDebugSnapshot snapshot =
        session_.selectedProcessSnapshot();

    output_
        << "Registers for PID "
        << snapshot.pid
        << ":\n";

    for (
        std::size_t index = 0;
        index < snapshot.context
            .registers
            .size();
        ++index
    ) {
        if (index != 0) {
            output_ << " ";
        }

        output_
            << "R"
            << index
            << "="
            << snapshot.context
                .registers[index];
    }

    output_
        << "\nPC="
        << snapshot.context.pc
        << " SP="
        << snapshot.context.sp
        << " FLAGS=0x"
        << std::hex
        << std::uppercase
        << snapshot.context.flags
        << std::dec
        << " PRIV="
        << privilegeLevelToString(
            snapshot.context.privilege
        )
        << "\n";
}

void MultiProcessDebugConsole::printMemory(
    std::size_t address,
    std::size_t count
) {
    const ProcessDebugSnapshot snapshot =
        session_.selectedProcessSnapshot();

    if (
        count == 0
        || address >= snapshot.memory.size()
        || count
            > snapshot.memory.size() - address
    ) {
        throw std::runtime_error(
            "Memory inspection range is outside "
            "selected process memory"
        );
    }

    const std::vector<std::uint8_t> bytes =
        snapshot.memory.readBytes(
            address,
            count
        );

    output_
        << "PID "
        << snapshot.pid
        << " Memory["
        << address
        << ".."
        << address + count
        << "):";

    for (const std::uint8_t byte : bytes) {
        output_
            << " "
            << std::hex
            << std::uppercase
            << std::setw(2)
            << std::setfill('0')
            << static_cast<unsigned int>(
                byte
            );
    }

    output_
        << std::dec
        << std::setfill(' ')
        << "\n";
}

void MultiProcessDebugConsole::printTrace() {
    if (
        session_.cpu()
            .traceLogger()
            .empty()
    ) {
        output_
            << "No trace events.\n";

        return;
    }

    output_
        << "Running PID "
        << session_.runningPid()
        << " last trace:\n"
        << session_.cpu()
            .traceLogger()
            .last()
            .toCompactString()
        << "\n";
}

ProcessMemoryWatchMode
MultiProcessDebugConsole::parseWatchMode(
    const std::string& text
) {
    if (text == "read" || text == "r") {
        return ProcessMemoryWatchMode::Read;
    }

    if (text == "write" || text == "w") {
        return ProcessMemoryWatchMode::Write;
    }

    if (
        text == "access"
        || text == "a"
        || text == "rw"
    ) {
        return ProcessMemoryWatchMode::Access;
    }

    throw std::runtime_error(
        "watch mode must be read, write, or access"
    );
}

kernel::ProcessId
MultiProcessDebugConsole::parsePid(
    const std::string& text
) {
    const std::size_t value =
        parsePositiveCount(
            text,
            "process"
        );

    if (
        value
        > static_cast<std::size_t>(
            std::numeric_limits<
                kernel::ProcessId
            >::max()
        )
    ) {
        throw std::runtime_error(
            "PID is out of range"
        );
    }

    return static_cast<
        kernel::ProcessId
    >(value);
}

std::size_t
MultiProcessDebugConsole::parseAddress(
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
                "address must be a non-negative integer"
            );
        }

        return static_cast<std::size_t>(
            value
        );
    } catch (const std::runtime_error&) {
        throw;
    } catch (const std::exception&) {
        throw std::runtime_error(
            "address must be a non-negative integer"
        );
    }
}

std::size_t
MultiProcessDebugConsole::parsePositiveCount(
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

        return static_cast<std::size_t>(
            value
        );
    } catch (const std::runtime_error&) {
        throw;
    } catch (const std::exception&) {
        throw std::runtime_error(
            context
            + " count must be a positive integer"
        );
    }
}

} // namespace zero_cpu::debug
