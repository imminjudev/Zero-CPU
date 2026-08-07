#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/debug/DebugConsole.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {

zero_cpu::kernel::ProcessImage makeImage(
    const std::string& source,
    const std::string& name
) {
    zero_cpu::Assembler assembler;
    const auto assembled =
        assembler.assembleString(source);

    zero_cpu::kernel::ProcessImageLoader loader;

    return loader.loadProgram(
        assembled.toBinaryProgram(),
        name
    );
}

zero_cpu::kernel::ProcessImage normalImage() {
    return makeImage(
        R"ASM(
.entry start
.data
value: .qword 0
.text
start:
    MOV R0, 1
    ADD R0, 2
    STORE [value], R0
)ASM",
        "debug-console-test.zbin"
    );
}

zero_cpu::debug::DebugConsoleOptions options() {
    zero_cpu::debug::DebugConsoleOptions value;
    value.default_continue_steps = 20;
    value.show_prompt = false;
    value.print_banner = true;
    return value;
}

bool scriptedWorkflow(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(normalImage());

    std::istringstream input(
        "help\n"
        "break 536\n"
        "breakpoints\n"
        "continue 20\n"
        "status\n"
        "step 1\n"
        "trace\n"
        "continue 20\n"
        "registers\n"
        "memory 0 8\n"
        "disassemble 512 3\n"
        "delete 536\n"
        "breakpoints\n"
        "quit\n"
    );

    std::ostringstream output;
    std::ostringstream error;

    DebugConsole console(
        session,
        input,
        output,
        error,
        options()
    );

    const DebugConsoleRunResult result =
        console.run();

    const std::string text = output.str();

    if (
        !result.success()
        || !result.quit_requested
        || result.eof_reached
        || result.command_count != 14
        || result.command_error_count != 0
        || !error.str().empty()
        || session.lastStop().reason
            != DebugStopReason::ProgramEnd
        || session.totalSteps() != 3
        || session.cpu().state()
            .registers().get(
                RegisterName::R0
            ) != 3
        || session.cpu().state()
            .memory().readI64(
                memory_map::kUserDataBase
            ) != 3
        || !session.breakpoints().empty()
        || text.find(
            "Breakpoint 536 added."
        ) == std::string::npos
        || text.find(
            "Stop reason: Breakpoint"
        ) == std::string::npos
        || text.find(
            "Stop reason: ProgramEnd"
        ) == std::string::npos
        || text.find("R0=3")
            == std::string::npos
        || text.find("Memory[0..8):")
            == std::string::npos
        || text.find("512: MOV R0, 1")
            == std::string::npos
        || text.find("No breakpoints.")
            == std::string::npos
    ) {
        detail = "scripted workflow mismatch";
        return false;
    }

    return true;
}

bool errorsRecover(std::string& detail) {
    using namespace zero_cpu::debug;

    DebugSession session(normalImage());

    std::istringstream input(
        "unknown-command\n"
        "break 513\n"
        "memory 4094 8\n"
        "status\n"
        "quit\n"
    );

    std::ostringstream output;
    std::ostringstream error;

    DebugConsole console(
        session,
        input,
        output,
        error,
        options()
    );

    const DebugConsoleRunResult result =
        console.run();

    if (
        result.success()
        || !result.quit_requested
        || result.command_count != 5
        || result.command_error_count != 3
        || output.str().find(
            "Stop reason: Ready"
        ) == std::string::npos
        || error.str().find(
            "unknown debugger command"
        ) == std::string::npos
        || error.str().find(
            "instruction-aligned"
        ) == std::string::npos
        || error.str().find(
            "outside process memory"
        ) == std::string::npos
    ) {
        detail = "error recovery mismatch";
        return false;
    }

    return true;
}

bool defaultLimit(std::string& detail) {
    using namespace zero_cpu::debug;

    DebugSession session(
        makeImage(
            R"ASM(
.entry loop
.text
loop:
    JMP loop
)ASM",
            "debug-console-loop.zbin"
        )
    );

    std::istringstream input(
        "continue\n"
        "status\n"
        "quit\n"
    );

    std::ostringstream output;
    std::ostringstream error;

    auto configured = options();
    configured.default_continue_steps = 4;

    DebugConsole console(
        session,
        input,
        output,
        error,
        configured
    );

    const DebugConsoleRunResult result =
        console.run();

    if (
        !result.success()
        || session.lastStop().reason
            != DebugStopReason::StepLimit
        || session.totalSteps() != 4
        || output.str().find(
            "Stop reason: StepLimit"
        ) == std::string::npos
    ) {
        detail = "default continue limit mismatch";
        return false;
    }

    return true;
}

bool eofAndAliases(std::string& detail) {
    using namespace zero_cpu::debug;

    DebugSession session(normalImage());

    std::istringstream input(
        "# comment\n"
        "; comment\n"
        "b 0x218\n"
        "bl\n"
        "s 1\n"
        "r\n"
        "x 0 8\n"
        "disas 512 2\n"
        "t\n"
        "clear\n"
        "bl\n"
    );

    std::ostringstream output;
    std::ostringstream error;

    auto configured = options();
    configured.print_banner = false;

    DebugConsole console(
        session,
        input,
        output,
        error,
        configured
    );

    const DebugConsoleRunResult result =
        console.run();

    if (
        !result.success()
        || result.quit_requested
        || !result.eof_reached
        || result.command_count != 9
        || session.totalSteps() != 1
        || !session.breakpoints().empty()
        || output.str().find(
            "Breakpoint 536 added."
        ) == std::string::npos
        || output.str().find(
            "All breakpoints cleared."
        ) == std::string::npos
        || output.str().find(
            "No breakpoints."
        ) == std::string::npos
    ) {
        detail = "EOF or aliases mismatch";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Debug Console Test ===\n\n";

    int failures = 0;

    auto report = [&](
        const std::string& name,
        bool passed,
        const std::string& detail
    ) {
        std::cout
            << (passed ? "[PASS] " : "[FAIL] ")
            << name
            << "\n";

        if (!passed) {
            std::cout
                << "       "
                << detail
                << "\n";
            ++failures;
        }
    };

    {
        std::string detail;
        report(
            "Scripted debugger workflow",
            scriptedWorkflow(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Command errors recover",
            errorsRecover(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Default continue limit",
            defaultLimit(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "EOF and aliases",
            eofAndAliases(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Debug console test "
               "finished successfully.\n";
        return 0;
    }

    std::cout
        << "Debug console test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
