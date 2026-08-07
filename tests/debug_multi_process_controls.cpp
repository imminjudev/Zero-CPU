#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/debug/DebugCondition.hpp"
#include "zero_cpu/debug/DebugSymbols.hpp"
#include "zero_cpu/debug/MultiProcessDebugConsole.hpp"
#include "zero_cpu/debug/MultiProcessDebugSession.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

const char* kFirstSource = R"ASM(
.entry start

.data
value: .qword 1

.text
start:
    LOAD R0, [value]
work:
    ADD R0, 1
done:
    STORE [value], R0
)ASM";

const char* kSecondSource = R"ASM(
.entry start

.data
value: .qword 10

.text
start:
    LOAD R0, [value]
work:
    ADD R0, 5
done:
    STORE [value], R0
)ASM";

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

std::vector<zero_cpu::kernel::ProcessImage>
normalImages() {
    return {
        makeImage(
            kFirstSource,
            "first.zbin"
        ),
        makeImage(
            kSecondSource,
            "second.zbin"
        )
    };
}

zero_cpu::debug::MultiProcessDebugOptions
debugOptions() {
    zero_cpu::debug::MultiProcessDebugOptions
        options;

    options.quantum = 1;
    options.default_continue_steps = 100;

    return options;
}

struct TemporaryExecutables {
    std::vector<std::string> files;

    TemporaryExecutables() = default;

    TemporaryExecutables(
        const TemporaryExecutables&
    ) = delete;

    TemporaryExecutables& operator=(
        const TemporaryExecutables&
    ) = delete;

    TemporaryExecutables(
        TemporaryExecutables&& other
    ) noexcept
        : files(
              std::move(other.files)
          ) {
        other.files.clear();
    }

    TemporaryExecutables& operator=(
        TemporaryExecutables&& other
    ) noexcept {
        if (this == &other) {
            return *this;
        }

        for (const std::string& file : files) {
            std::remove(file.c_str());
        }

        files = std::move(other.files);
        other.files.clear();

        return *this;
    }

    ~TemporaryExecutables() {
        for (const std::string& file : files) {
            std::remove(file.c_str());
        }
    }
};

TemporaryExecutables writeExecutables(
    const std::string& prefix
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    Assembler assembler;
    binary::BinaryWriter writer;

    TemporaryExecutables temporary;

    const std::vector<std::string> sources = {
        kFirstSource,
        kSecondSource
    };

    for (
        std::size_t index = 0;
        index < sources.size();
        ++index
    ) {
        const AssembledProgram assembled =
            assembler.assembleString(
                sources[index]
            );

        const std::string binaryPath =
            prefix
            + "_"
            + std::to_string(index + 1)
            + ".zbin";

        const std::string symbolsPath =
            debugSymbolsPathForExecutable(
                binaryPath
            );

        writer.writeFile(
            binaryPath,
            assembled.toBinaryProgram()
        );

        DebugSymbols::fromAssembledProgram(
            assembled,
            memory_map::kBinaryCodeBase
        ).writeFile(
            symbolsPath
        );

        temporary.files.push_back(
            binaryPath
        );

        temporary.files.push_back(
            symbolsPath
        );
    }

    return temporary;
}

std::vector<std::string> binaryPaths(
    const TemporaryExecutables& temporary
) {
    return {
        temporary.files.at(0),
        temporary.files.at(2)
    };
}

bool pidBreakpointIsolation(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    MultiProcessDebugSession session(
        normalImages(),
        debugOptions()
    );

    if (
        !session.addBreakpoint(
            2,
            memory_map::kBinaryCodeBase
        )
    ) {
        detail =
            "PID breakpoint was not added";
        return false;
    }

    const MultiProcessDebugStop stop =
        session.continueExecution(100);

    if (
        stop.reason
            != MultiProcessDebugStopReason::
                Breakpoint
        || !stop.has_debug_hit
        || stop.hit_pid != 2
        || stop.hit_address
            != memory_map::kBinaryCodeBase
        || session.processSnapshot(1)
            .context.pc
            == memory_map::kBinaryCodeBase
        || session.processSnapshot(2)
            .context.pc
            != memory_map::kBinaryCodeBase
    ) {
        detail =
            "PID breakpoint isolation mismatch";
        return false;
    }

    const MultiProcessDebugStop resumed =
        session.continueExecution(100);

    if (
        resumed.reason
            == MultiProcessDebugStopReason::
                Breakpoint
        && resumed.hit_pid == 2
        && resumed.hit_address
            == memory_map::kBinaryCodeBase
    ) {
        detail =
            "PID breakpoint did not resume";
        return false;
    }

    return true;
}

bool conditionalIsolation(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    MultiProcessDebugSession session(
        normalImages(),
        debugOptions()
    );

    const std::size_t address =
        memory_map::kBinaryCodeBase
        + binary::kInstructionSize;

    const std::size_t id =
        session.addConditionalBreakpoint(
            1,
            address,
            parseDebugCondition(
                "R0",
                "==",
                "1"
            )
        );

    const MultiProcessDebugStop stop =
        session.continueExecution(100);

    if (
        id != 1
        || stop.reason
            != MultiProcessDebugStopReason::
                ConditionalBreakpoint
        || !stop.has_conditional_breakpoint
        || stop.hit_pid != 1
        || stop.hit_address != address
        || stop.conditional_breakpoint_id
            != id
        || stop.conditional_expression
            != "R0 == 1"
        || stop.conditional_actual_value != 1
    ) {
        detail =
            "PID conditional breakpoint mismatch";
        return false;
    }

    return true;
}

bool watchpointIsolation(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    MultiProcessDebugSession session(
        normalImages(),
        debugOptions()
    );

    const std::size_t id =
        session.addWatchpoint(
            1,
            memory_map::kUserDataBase,
            8,
            ProcessMemoryWatchMode::Write
        );

    const MultiProcessDebugStop stop =
        session.continueExecution(100);

    if (
        stop.reason
            != MultiProcessDebugStopReason::
                Watchpoint
        || !stop.has_watchpoint
        || stop.hit_pid != 1
        || stop.watchpoint_id != id
        || stop.watchpoint_mode
            != ProcessMemoryWatchMode::Write
        || stop.access_mode
            != ProcessMemoryWatchMode::Write
        || stop.access_address
            != memory_map::kUserDataBase
        || session.processSnapshot(1)
            .memory.readI64(
                memory_map::kUserDataBase
            ) != 2
        || session.processSnapshot(2)
            .memory.readI64(
                memory_map::kUserDataBase
            ) != 10
    ) {
        detail =
            "PID watchpoint isolation mismatch";
        return false;
    }

    return true;
}

bool symbolsAndManagement(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    const TemporaryExecutables temporary =
        writeExecutables(
            "debug_multi_controls"
        );

    MultiProcessDebugSession session(
        binaryPaths(temporary),
        debugOptions()
    );

    const std::size_t start =
        memory_map::kBinaryCodeBase;

    if (
        !session.hasSymbols(1)
        || !session.hasSymbols(2)
        || session.resolveCodeSymbol(
            1,
            "work"
        ) != start
            + binary::kInstructionSize
        || session.resolveDataSymbol(
            2,
            "value"
        ) != memory_map::kUserDataBase
    ) {
        detail =
            "PID symbol loading mismatch";
        return false;
    }

    (void)session.addBreakpoint(
        1,
        start
    );

    (void)session.addBreakpoint(
        2,
        start
    );

    const std::size_t conditionId =
        session.addConditionalBreakpoint(
            1,
            start,
            parseDebugCondition(
                "PC",
                "==",
                "512"
            )
        );

    const std::size_t watchId =
        session.addWatchpoint(
            2,
            0,
            8,
            ProcessMemoryWatchMode::Access
        );

    if (
        session.breakpoints().size() != 2
        || session.breakpoints(1).size() != 1
        || session.conditionalBreakpoints()
            .size() != 1
        || session.watchpoints().size() != 1
        || !session.removeConditionalBreakpoint(
            conditionId
        )
        || !session.removeWatchpoint(watchId)
    ) {
        detail =
            "PID debug control management mismatch";
        return false;
    }

    session.clearBreakpoints(1);

    if (
        !session.breakpoints(1).empty()
        || session.breakpoints(2).size() != 1
    ) {
        detail =
            "PID filtered clear mismatch";
        return false;
    }

    if (
        !throwsRuntimeError(
            [&] {
                (void)session.addBreakpoint(
                    99,
                    start
                );
            }
        )
        || !throwsRuntimeError(
            [&] {
                (void)session.addBreakpoint(
                    1,
                    start + 1
                );
            }
        )
        || !throwsRuntimeError(
            [&] {
                (void)session.addWatchpoint(
                    1,
                    memory_map::kDefaultMemorySize
                        - 4,
                    8,
                    ProcessMemoryWatchMode::Access
                );
            }
        )
    ) {
        detail =
            "invalid PID debug control was accepted";
        return false;
    }

    return true;
}

bool consoleWorkflow(
    std::string& detail
) {
    using namespace zero_cpu::debug;

    const TemporaryExecutables temporary =
        writeExecutables(
            "debug_multi_console_controls"
        );

    MultiProcessDebugSession session(
        binaryPaths(temporary),
        debugOptions()
    );

    std::istringstream input(
        "symbols 1\n"
        "break-if-label 1 start PC == 512\n"
        "conditions 1\n"
        "continue 100\n"
        "delete-if 1\n"
        "break-label 2 start\n"
        "breakpoints 2\n"
        "continue 100\n"
        "delete 2 512\n"
        "watch 1 write 0 8\n"
        "watchpoints 1\n"
        "continue 100\n"
        "status\n"
        "unwatch 1\n"
        "quit\n"
    );

    std::ostringstream output;
    std::ostringstream error;

    MultiProcessDebugConsoleOptions options;
    options.show_prompt = false;
    options.print_banner = false;
    options.default_continue_steps = 100;

    MultiProcessDebugConsole console(
        session,
        input,
        output,
        error,
        options
    );

    const MultiProcessDebugConsoleResult result =
        console.run();

    const std::string text =
        output.str();

    if (
        !result.success()
        || !result.quit_requested
        || result.command_count != 15
        || !error.str().empty()
        || text.find(
            "Debug symbols for PID 1:"
        ) == std::string::npos
        || text.find(
            "Conditional breakpoint 1 added for PID 1 at label start (512): PC == 512."
        ) == std::string::npos
        || text.find(
            "Stop reason: ConditionalBreakpoint"
        ) == std::string::npos
        || text.find(
            "PID 2 breakpoint at label start (512) added."
        ) == std::string::npos
        || text.find(
            "Stop reason: Breakpoint"
        ) == std::string::npos
        || text.find(
            "Watchpoint 1 added for PID 1: Write [0, 8)."
        ) == std::string::npos
        || text.find(
            "Stop reason: Watchpoint"
        ) == std::string::npos
        || text.find(
            "Hit PID: 1"
        ) == std::string::npos
        || text.find(
            "Memory access: Write 0"
        ) == std::string::npos
    ) {
        detail =
            "PID debug console workflow mismatch";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Multi-Process "
           "Debug Controls Test ===\n\n";

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
            "PID breakpoint isolation",
            pidBreakpointIsolation(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "PID conditional breakpoint",
            conditionalIsolation(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "PID watchpoint isolation",
            watchpointIsolation(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "PID symbols and management",
            symbolsAndManagement(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "PID debug console workflow",
            consoleWorkflow(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Multi-process debug controls test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Multi-process debug controls test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
