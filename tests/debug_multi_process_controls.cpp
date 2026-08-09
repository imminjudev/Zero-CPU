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


TemporaryExecutables writeSingleExecutable(
    const std::string& prefix,
    const std::string& source
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    Assembler assembler;
    binary::BinaryWriter writer;

    const AssembledProgram assembled =
        assembler.assembleString(source);

    const std::string binaryPath =
        prefix + ".zbin";

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

    TemporaryExecutables temporary;
    temporary.files.push_back(binaryPath);
    temporary.files.push_back(symbolsPath);
    return temporary;
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


bool selectedPidSourceStepping(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    const TemporaryExecutables temporary =
        writeExecutables(
            "debug_multi_source_step_scheduler"
        );

    MultiProcessDebugSession session(
        binaryPaths(temporary),
        debugOptions()
    );

    session.selectProcess(2);

    const ProcessDebugSnapshot initial =
        session.selectedProcessSnapshot();

    const std::size_t nextAddress =
        initial.context.pc
        + binary::kInstructionSize;

    const std::size_t sourceLineBefore =
        session.symbols(2)
            .sourceLineForAddress(
                initial.context.pc
            );

    const std::size_t expectedLine =
        session.symbols(2)
            .sourceLineForAddress(
                nextAddress
            );

    const MultiProcessDebugStop stop =
        session.stepSelectedSourceLine(20);

    const ProcessDebugSnapshot selected =
        session.selectedProcessSnapshot();

    const std::size_t sourceLineAfter =
        session.symbols(2)
            .sourceLineForAddress(
                selected.context.pc
            );

    if (
        stop.reason
            != MultiProcessDebugStopReason::
                StepComplete
        || stop.executed_steps == 0
        || stop.executed_steps
            != session.totalSteps()
        || session.selectedPid() != 2
        || selected.context.pc
            != nextAddress
        || sourceLineBefore == sourceLineAfter
        || sourceLineAfter != expectedLine
        || selected.context.registers[
            static_cast<std::size_t>(
                RegisterName::R0
            )
        ] != 10
        || session.contextSwitches().empty()
    ) {
        detail =
            "selected PID source stepping did not "
            "follow the real scheduler";
        return false;
    }

    return true;
}

bool sourceStepStopPriority(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    const TemporaryExecutables temporary =
        writeExecutables(
            "debug_multi_source_step_stops"
        );

    MultiProcessDebugOptions options =
        debugOptions();

    options.quantum = 100;

    MultiProcessDebugSession session(
        binaryPaths(temporary),
        options
    );

    const ProcessDebugSnapshot initial =
        session.selectedProcessSnapshot();

    const std::size_t secondAddress =
        initial.context.pc
        + binary::kInstructionSize;

    const std::size_t thirdAddress =
        secondAddress
        + binary::kInstructionSize;

    (void)session.addBreakpoint(
        1,
        secondAddress
    );

    const MultiProcessDebugStop breakpoint =
        session.stepSelectedSourceLine(20);

    if (
        breakpoint.reason
            != MultiProcessDebugStopReason::
                Breakpoint
        || !breakpoint.has_debug_hit
        || breakpoint.hit_pid != 1
        || breakpoint.hit_address
            != secondAddress
        || breakpoint.executed_steps != 1
        || session.selectedProcessSnapshot()
            .context.pc != secondAddress
    ) {
        detail =
            "source step did not honor destination "
            "breakpoint";
        return false;
    }

    const MultiProcessDebugStop resumed =
        session.stepSelectedSourceLine(20);

    if (
        resumed.reason
            != MultiProcessDebugStopReason::
                StepComplete
        || resumed.executed_steps != 1
        || session.selectedProcessSnapshot()
            .context.pc != thirdAddress
    ) {
        detail =
            "source step did not resume through the "
            "current breakpoint";
        return false;
    }

    const std::size_t watchpointId =
        session.addWatchpoint(
            1,
            memory_map::kUserDataBase,
            sizeof(std::int64_t),
            ProcessMemoryWatchMode::Write
        );

    const MultiProcessDebugStop watched =
        session.stepSelectedSourceLine(20);

    if (
        watched.reason
            != MultiProcessDebugStopReason::
                Watchpoint
        || !watched.has_watchpoint
        || watched.hit_pid != 1
        || watched.watchpoint_id
            != watchpointId
        || watched.access_mode
            != ProcessMemoryWatchMode::Write
        || watched.access_address
            != memory_map::kUserDataBase
        || session.processSnapshot(1)
            .memory.readI64(
                memory_map::kUserDataBase
            ) != 2
    ) {
        detail =
            "source-step watchpoint priority mismatch";
        return false;
    }

    return true;
}

bool sourceStepValidationAndLimit(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    {
        MultiProcessDebugSession noMap(
            normalImages(),
            debugOptions()
        );

        if (
            !throwsRuntimeError(
                [&] {
                    (void)noMap
                        .stepSelectedSourceLine(10);
                }
            )
        ) {
            detail =
                "source step accepted a selected PID "
                "without a source map";
            return false;
        }
    }

    const TemporaryExecutables temporary =
        writeExecutables(
            "debug_multi_source_step_validation"
        );

    MultiProcessDebugSession mapped(
        binaryPaths(temporary),
        debugOptions()
    );

    if (
        !throwsRuntimeError(
            [&] {
                (void)mapped
                    .stepSelectedSourceLine(0);
            }
        )
    ) {
        detail =
            "zero multi-process source-step limit "
            "was accepted";
        return false;
    }

    const char* loopSource =
        R"ASM(.entry loop
.text
loop:
    JMP loop
)ASM";

    const TemporaryExecutables loopTemporary =
        writeSingleExecutable(
            "debug_multi_source_step_limit",
            loopSource
        );

    MultiProcessDebugOptions loopOptions =
        debugOptions();

    loopOptions.quantum = 100;

    MultiProcessDebugSession loop(
        {loopTemporary.files.at(0)},
        loopOptions
    );

    const MultiProcessDebugStop limited =
        loop.stepSelectedSourceLine(5);

    if (
        limited.reason
            != MultiProcessDebugStopReason::
                StepLimit
        || limited.executed_steps != 5
        || limited.total_steps != 5
        || limited.message.empty()
        || loop.selectedProcessSnapshot()
            .context.pc
            != memory_map::kBinaryCodeBase
    ) {
        detail =
            "multi-process source-step limit "
            "behavior mismatch";
        return false;
    }

    return true;
}

// Patch: v1.2-multiprocess-source-step-core-r1


bool consoleSourceStepWorkflow(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    const TemporaryExecutables temporary =
        writeExecutables(
            "debug_multi_console_source_step"
        );

    MultiProcessDebugSession session(
        binaryPaths(temporary),
        debugOptions()
    );

    const ProcessDebugSnapshot before =
        session.processSnapshot(2);

    const std::size_t expectedPc =
        before.context.pc
        + binary::kInstructionSize;

    std::istringstream input(
        "help\n"
        "process 2\n"
        "step-line 20\n"
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

    const ProcessDebugSnapshot after =
        session.processSnapshot(2);

    const std::string text =
        output.str();

    if (
        !result.success()
        || !result.quit_requested
        || result.command_count != 4
        || result.command_error_count != 0
        || !error.str().empty()
        || session.selectedPid() != 2
        || after.context.pc != expectedPc
        || after.context.registers[
            static_cast<std::size_t>(
                RegisterName::R0
            )
        ] != 10
        || session.contextSwitches().empty()
        || text.find(
            "step-line [max-steps]"
        ) == std::string::npos
        || text.find(
            "Selected PID 2."
        ) == std::string::npos
        || text.find(
            "Stop reason: StepComplete"
        ) == std::string::npos
        || text.find(
            "Selected PID: 2"
        ) == std::string::npos
    ) {
        detail =
            "multi-process source-step console "
            "workflow mismatch";
        return false;
    }

    return true;
}

// Patch: v1.2-debug-console-source-step-r1

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
            "Selected PID source stepping",
            selectedPidSourceStepping(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Source-step stop priority",
            sourceStepStopPriority(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Source-step validation and limit",
            sourceStepValidationAndLimit(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "PID source-step console workflow",
            consoleSourceStepWorkflow(detail),
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
