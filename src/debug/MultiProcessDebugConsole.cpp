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
