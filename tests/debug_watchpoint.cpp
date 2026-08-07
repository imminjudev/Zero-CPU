#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/debug/DebugConsole.hpp"
#include "zero_cpu/debug/DebugSession.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

template <typename Function>
bool throwsRuntimeError(Function&& function) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return true;
    }

    return false;
}

zero_cpu::kernel::ProcessImage makeImage() {
    const char* source = R"ASM(
.entry start

.data
value: .qword 41

.text
start:
    LOAD R0, [value]
    ADD R0, 1
    STORE [value], R0
)ASM";

    zero_cpu::Assembler assembler;

    const auto assembled =
        assembler.assembleString(source);

    zero_cpu::kernel::ProcessImageLoader loader;

    return loader.loadProgram(
        assembled.toBinaryProgram(),
        "debug-watchpoint-test.zbin"
    );
}

bool readWatchpoint(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    const std::size_t id =
        session.addWatchpoint(
            memory_map::kUserDataBase,
            8,
            MemoryWatchMode::Read
        );

    const DebugStop stop =
        session.continueExecution(20);

    if (
        id != 1
        || !stop.stoppedAtWatchpoint()
        || !stop.has_watchpoint
        || stop.watchpoint_id != id
        || stop.watchpoint_mode
            != MemoryWatchMode::Read
        || stop.watchpoint_address
            != memory_map::kUserDataBase
        || stop.watchpoint_size != 8
        || stop.access_mode
            != MemoryWatchMode::Read
        || stop.access_address
            != memory_map::kUserDataBase
        || stop.executed_steps != 1
        || stop.total_steps != 1
        || session.cpu().state().pc()
            != memory_map::kBinaryCodeBase
                + 24
        || session.cpu().state()
            .registers().get(
                RegisterName::R0
            ) != 41
    ) {
        detail = "read watchpoint mismatch";
        return false;
    }

    const DebugStop completed =
        session.continueExecution(20);

    if (
        !completed.reachedProgramEnd()
        || session.totalSteps() != 3
        || session.cpu().state()
            .memory().readI64(
                memory_map::kUserDataBase
            ) != 42
    ) {
        detail =
            "execution did not resume after read watchpoint";
        return false;
    }

    return true;
}

bool writeWatchpoint(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    const std::size_t id =
        session.addWatchpoint(
            memory_map::kUserDataBase + 4,
            1,
            MemoryWatchMode::Write
        );

    const DebugStop stop =
        session.continueExecution(20);

    if (
        !stop.stoppedAtWatchpoint()
        || stop.watchpoint_id != id
        || stop.access_mode
            != MemoryWatchMode::Write
        || stop.executed_steps != 3
        || stop.total_steps != 3
        || session.cpu().state()
            .memory().readI64(
                memory_map::kUserDataBase
            ) != 42
    ) {
        detail =
            "overlapping write watchpoint mismatch";
        return false;
    }

    return true;
}

bool managementAndValidation(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    const std::size_t first =
        session.addWatchpoint(
            0,
            8,
            MemoryWatchMode::Access
        );

    const std::size_t duplicate =
        session.addWatchpoint(
            0,
            8,
            MemoryWatchMode::Access
        );

    const std::size_t second =
        session.addWatchpoint(
            8,
            8,
            MemoryWatchMode::Write
        );

    if (
        first != duplicate
        || second == first
        || session.watchpoints().size() != 2
        || !session.removeWatchpoint(first)
        || session.removeWatchpoint(first)
        || session.watchpoints().size() != 1
    ) {
        detail =
            "watchpoint management mismatch";
        return false;
    }

    session.clearWatchpoints();

    if (!session.watchpoints().empty()) {
        detail =
            "watchpoint clear mismatch";
        return false;
    }

    if (
        !throwsRuntimeError(
            [&] {
                (void)session.addWatchpoint(
                    0,
                    0,
                    MemoryWatchMode::Access
                );
            }
        )
        || !throwsRuntimeError(
            [&] {
                (void)session.addWatchpoint(
                    memory_map::kDefaultMemorySize
                        - 4,
                    8,
                    MemoryWatchMode::Access
                );
            }
        )
    ) {
        detail =
            "invalid watchpoint range was accepted";
        return false;
    }

    return true;
}

bool breakpointPrecedence(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    (void)session.addBreakpoint(
        memory_map::kBinaryCodeBase
    );

    (void)session.addWatchpoint(
        memory_map::kUserDataBase,
        8,
        MemoryWatchMode::Read
    );

    const DebugStop breakpoint =
        session.continueExecution(20);

    const DebugStop watchpoint =
        session.continueExecution(20);

    if (
        !breakpoint.stoppedAtBreakpoint()
        || breakpoint.total_steps != 0
        || !watchpoint.stoppedAtWatchpoint()
        || watchpoint.total_steps != 1
    ) {
        detail =
            "breakpoint/watchpoint precedence mismatch";
        return false;
    }

    return true;
}

bool consoleCommands(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    std::istringstream input(
        "watch write 0 8\n"
        "watchpoints\n"
        "continue 20\n"
        "status\n"
        "memory 0 8\n"
        "unwatch 1\n"
        "watchpoints\n"
        "clear-watchpoints\n"
        "quit\n"
    );

    std::ostringstream output;
    std::ostringstream error;

    DebugConsoleOptions options;
    options.show_prompt = false;
    options.print_banner = false;
    options.default_continue_steps = 20;

    DebugConsole console(
        session,
        input,
        output,
        error,
        options
    );

    const DebugConsoleRunResult result =
        console.run();

    const std::string text = output.str();

    if (
        !result.success()
        || !result.quit_requested
        || result.command_count != 9
        || !error.str().empty()
        || session.lastStop().reason
            != DebugStopReason::Watchpoint
        || text.find(
            "Watchpoint 1 added: Write [0, 8)."
        ) == std::string::npos
        || text.find(
            "Stop reason: Watchpoint"
        ) == std::string::npos
        || text.find(
            "Watchpoint ID: 1"
        ) == std::string::npos
        || text.find(
            "Memory access: Write 0"
        ) == std::string::npos
        || text.find(
            "Watchpoint 1 deleted."
        ) == std::string::npos
        || text.find(
            "No watchpoints."
        ) == std::string::npos
    ) {
        detail =
            "debug console watchpoint command mismatch";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Debug Watchpoint Test ===\n\n";

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
            "Read watchpoint",
            readWatchpoint(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Write watchpoint and overlap",
            writeWatchpoint(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Management and validation",
            managementAndValidation(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Breakpoint precedence",
            breakpointPrecedence(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Console watchpoint commands",
            consoleCommands(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Debug watchpoint test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Debug watchpoint test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
