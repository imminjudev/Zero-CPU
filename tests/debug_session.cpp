#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/debug/DebugSession.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include <cstdio>
#include <cstddef>
#include <iostream>
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

zero_cpu::kernel::ProcessImage makeImage(
    const std::string& source,
    const std::string& name
) {
    zero_cpu::Assembler assembler;
    const zero_cpu::AssembledProgram assembled =
        assembler.assembleString(source);
    zero_cpu::kernel::ProcessImageLoader loader;
    return loader.loadProgram(assembled.toBinaryProgram(), name);
}

const char* kProgramSource = R"ASM(
.entry start

.data
value: .qword 0

.text
start:
    MOV R0, 1
    ADD R0, 2
    STORE [value], R0
)ASM";


const char* kSourceStepProgram = R"ASM(.entry start
.text
start:
    MOV R0, 1
    CALL worker
    ADD R0, 4
    JMP done
worker:
    ADD R0, 2
    RET
done:
    MOV R1, R0
)ASM";

void writeSourceMappedProgram(
    const std::string& binaryPath,
    const std::string& sourcePath,
    const std::string& source
) {
    using namespace zero_cpu;

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(source);

    binary::BinaryWriter writer;

    writer.writeFile(
        binaryPath,
        assembled.toBinaryProgram()
    );

    debug::DebugSymbols::fromAssembledProgram(
        assembled,
        memory_map::kBinaryCodeBase,
        sourcePath
    ).writeFile(
        debug::debugSymbolsPathForExecutable(
            binaryPath
        )
    );
}

struct SourceMappedProgramCleanup {
    std::string binary_path;

    ~SourceMappedProgramCleanup() {
        std::remove(
            zero_cpu::debug::debugSymbolsPathForExecutable(
                binary_path
            ).c_str()
        );

        std::remove(
            binary_path.c_str()
        );
    }
};

bool stopReasonStrings(std::string& detail) {
    using namespace zero_cpu::debug;

    if (
        std::string(debugStopReasonToString(DebugStopReason::Ready)) != "Ready"
        || std::string(debugStopReasonToString(DebugStopReason::StepComplete)) != "StepComplete"
        || std::string(debugStopReasonToString(DebugStopReason::Breakpoint)) != "Breakpoint"
        || std::string(debugStopReasonToString(DebugStopReason::ProgramEnd)) != "ProgramEnd"
        || std::string(debugStopReasonToString(DebugStopReason::Halted)) != "Halted"
        || std::string(debugStopReasonToString(DebugStopReason::Fault)) != "Fault"
        || std::string(debugStopReasonToString(DebugStopReason::StepLimit)) != "StepLimit"
    ) {
        detail = "debug stop reason string mismatch";
        return false;
    }
    return true;
}

bool loadAndInitialState(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage(kProgramSource, "debug-test.zbin"));

    if (
        !session.loaded()
        || session.sourceName() != "debug-test.zbin"
        || session.cpu().state().pc() != memory_map::kBinaryCodeBase
        || !session.cpu().state().isUserMode()
        || !session.cpu().hasBinaryProgram()
        || session.metadata().entry_point != memory_map::kBinaryCodeBase
        || session.metadata().code_size != 3 * binary::kInstructionSize
        || session.totalSteps() != 0
        || session.lastStop().reason != DebugStopReason::Ready
        || !session.breakpoints().empty()
    ) {
        detail = "debugger initial state mismatch";
        return false;
    }
    return true;
}

bool breakpointManagement(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage(kProgramSource, "breakpoint-test.zbin"));
    const std::size_t breakpoint =
        memory_map::kBinaryCodeBase + binary::kInstructionSize;

    if (
        !session.addBreakpoint(breakpoint)
        || session.addBreakpoint(breakpoint)
        || !session.hasBreakpoint(breakpoint)
        || session.breakpoints().size() != 1
        || session.breakpoints().front() != breakpoint
        || !session.removeBreakpoint(breakpoint)
        || session.removeBreakpoint(breakpoint)
    ) {
        detail = "breakpoint add/remove mismatch";
        return false;
    }

    if (
        !throwsRuntimeError([&] {
            (void)session.addBreakpoint(memory_map::kBinaryCodeBase + 1);
        })
        || !throwsRuntimeError([&] {
            (void)session.addBreakpoint(session.metadata().code_end_exclusive);
        })
    ) {
        detail = "invalid breakpoint address was accepted";
        return false;
    }
    return true;
}

bool continueAndResume(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage(kProgramSource, "continue-test.zbin"));
    const std::size_t breakpoint =
        memory_map::kBinaryCodeBase + binary::kInstructionSize;
    (void)session.addBreakpoint(breakpoint);

    const DebugStop first = session.continueExecution(20);
    if (
        !first.stoppedAtBreakpoint()
        || first.pc != breakpoint
        || first.executed_steps != 1
        || first.total_steps != 1
        || session.cpu().state().registers().get(RegisterName::R0) != 1
    ) {
        detail = "continue did not stop before breakpoint";
        return false;
    }

    const DebugStop resumed = session.continueExecution(20);
    if (
        !resumed.reachedProgramEnd()
        || resumed.executed_steps != 2
        || resumed.total_steps != 3
        || session.cpu().state().pc() != session.metadata().code_end_exclusive
        || session.cpu().state().registers().get(RegisterName::R0) != 3
        || session.cpu().state().memory().readI64(memory_map::kUserDataBase) != 3
    ) {
        detail = "continue did not resume past breakpoint";
        return false;
    }
    return true;
}

bool singleStepExecution(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage(kProgramSource, "step-test.zbin"));
    const DebugStop first = session.step();
    const DebugStop second = session.step();
    const DebugStop third = session.step();

    if (
        first.reason != DebugStopReason::StepComplete
        || first.executed_steps != 1
        || first.total_steps != 1
        || second.reason != DebugStopReason::StepComplete
        || second.total_steps != 2
        || !third.reachedProgramEnd()
        || third.total_steps != 3
        || session.cpu().traceLogger().size() != 3
        || session.cpu().state().registers().get(RegisterName::R0) != 3
    ) {
        detail = "single-step execution mismatch";
        return false;
    }
    return true;
}


bool sourceLineStepping(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    const std::string path =
        "debug_source_step_test.zbin";

    SourceMappedProgramCleanup cleanup{
        path
    };

    writeSourceMappedProgram(
        path,
        "debug_source_step_test.zasm",
        kSourceStepProgram
    );

    DebugSession session(path);

    if (
        !session.symbols().hasSourceLocations()
        || session.symbols()
            .sourceLineForAddress(
                session.cpu().state().pc()
            ) != 4
    ) {
        detail =
            "source-step initial mapping mismatch";
        return false;
    }

    const DebugStop first =
        session.stepSourceLine(20);

    if (
        first.reason
            != DebugStopReason::StepComplete
        || first.executed_steps != 1
        || first.total_steps != 1
        || session.symbols()
            .sourceLineForAddress(
                session.cpu().state().pc()
            ) != 5
        || session.cpu().state()
            .registers()
            .get(RegisterName::R0) != 1
    ) {
        detail =
            "source step MOV -> CALL mismatch";
        return false;
    }

    const DebugStop callStep =
        session.stepSourceLine(20);

    if (
        callStep.reason
            != DebugStopReason::StepComplete
        || callStep.executed_steps != 1
        || callStep.total_steps != 2
        || session.symbols()
            .sourceLineForAddress(
                session.cpu().state().pc()
            ) != 9
    ) {
        detail =
            "source step CALL -> callee mismatch";
        return false;
    }

    const DebugStop workerStep =
        session.stepSourceLine(20);

    if (
        workerStep.reason
            != DebugStopReason::StepComplete
        || workerStep.total_steps != 3
        || session.symbols()
            .sourceLineForAddress(
                session.cpu().state().pc()
            ) != 10
        || session.cpu().state()
            .registers()
            .get(RegisterName::R0) != 3
    ) {
        detail =
            "source step callee body mismatch";
        return false;
    }

    const DebugStop returnStep =
        session.stepSourceLine(20);

    if (
        returnStep.reason
            != DebugStopReason::StepComplete
        || returnStep.total_steps != 4
        || session.symbols()
            .sourceLineForAddress(
                session.cpu().state().pc()
            ) != 6
    ) {
        detail =
            "source step RET -> caller mismatch";
        return false;
    }

    return true;
}

bool sourceLineStepBreakpointResume(
    std::string& detail
) {
    using namespace zero_cpu::debug;

    const std::string path =
        "debug_source_step_breakpoint_test.zbin";

    SourceMappedProgramCleanup cleanup{
        path
    };

    writeSourceMappedProgram(
        path,
        "debug_source_step_breakpoint_test.zasm",
        kSourceStepProgram
    );

    DebugSession session(path);

    const std::size_t callAddress =
        session.symbols()
            .resolveSourceLine(5);

    (void)session.addBreakpoint(
        callAddress
    );

    const DebugStop breakpoint =
        session.continueExecution(20);

    if (
        breakpoint.reason
            != DebugStopReason::Breakpoint
        || breakpoint.pc != callAddress
        || breakpoint.total_steps != 1
    ) {
        detail =
            "source-step breakpoint setup mismatch";
        return false;
    }

    const DebugStop stepped =
        session.stepSourceLine(20);

    if (
        stepped.reason
            != DebugStopReason::StepComplete
        || stepped.executed_steps != 1
        || stepped.total_steps != 2
        || session.symbols()
            .sourceLineForAddress(
                session.cpu().state().pc()
            ) != 9
    ) {
        detail =
            "source step did not resume through current breakpoint";
        return false;
    }

    return true;
}

bool sourceLineStepDestinationBreakpoint(
    std::string& detail
) {
    using namespace zero_cpu::debug;

    const std::string path =
        "debug_source_step_destination_breakpoint_test.zbin";

    SourceMappedProgramCleanup cleanup{
        path
    };

    writeSourceMappedProgram(
        path,
        "debug_source_step_destination_breakpoint_test.zasm",
        kSourceStepProgram
    );

    DebugSession session(path);

    const std::size_t callAddress =
        session.symbols()
            .resolveSourceLine(5);

    (void)session.addBreakpoint(
        callAddress
    );

    const DebugStop stopped =
        session.stepSourceLine(20);

    if (
        stopped.reason
            != DebugStopReason::Breakpoint
        || stopped.pc != callAddress
        || stopped.executed_steps != 1
        || stopped.total_steps != 1
    ) {
        detail =
            "source step did not honor destination breakpoint";
        return false;
    }

    const DebugStop resumed =
        session.stepSourceLine(20);

    if (
        resumed.reason
            != DebugStopReason::StepComplete
        || resumed.executed_steps != 1
        || resumed.total_steps != 2
        || session.symbols()
            .sourceLineForAddress(
                session.cpu().state().pc()
            ) != 9
    ) {
        detail =
            "source step did not resume from destination breakpoint";
        return false;
    }

    return true;
}

bool sourceLineStepWatchpointPriority(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    const char* source =
        R"ASM(.entry start
.data
value: .qword 0
.text
start:
    MOV R0, 7
    STORE [value], R0
    MOV R1, R0
)ASM";

    const std::string path =
        "debug_source_step_watchpoint_test.zbin";

    SourceMappedProgramCleanup cleanup{
        path
    };

    writeSourceMappedProgram(
        path,
        "debug_source_step_watchpoint_test.zasm",
        source
    );

    DebugSession session(path);

    const std::size_t watchpointId =
        session.addWatchpoint(
            memory_map::kUserDataBase,
            sizeof(std::int64_t),
            MemoryWatchMode::Write
        );

    const DebugStop first =
        session.stepSourceLine(20);

    if (
        first.reason
            != DebugStopReason::StepComplete
        || first.total_steps != 1
    ) {
        detail =
            "watchpoint source-step setup mismatch";
        return false;
    }

    const DebugStop watched =
        session.stepSourceLine(20);

    if (
        watched.reason
            != DebugStopReason::Watchpoint
        || !watched.has_watchpoint
        || watched.watchpoint_id
            != watchpointId
        || watched.executed_steps != 1
        || watched.total_steps != 2
        || watched.access_mode
            != MemoryWatchMode::Write
        || watched.access_address
            != memory_map::kUserDataBase
        || session.cpu().state()
            .memory()
            .readI64(
                memory_map::kUserDataBase
            ) != 7
    ) {
        detail =
            "source-step watchpoint priority mismatch";
        return false;
    }

    return true;
}

// Patch: v1.2-debug-session-source-step-stop-priority-r1

bool sourceLineStepValidationAndLimit(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    {
        DebugSession noMap(
            makeImage(
                kProgramSource,
                "source-step-no-map.zbin"
            )
        );

        if (
            !throwsRuntimeError(
                [&] {
                    (void)noMap
                        .stepSourceLine(10);
                }
            )
        ) {
            detail =
                "source step accepted a session without a source map";
            return false;
        }
    }

    const char* loopSource =
        R"ASM(.entry loop
.text
loop:
    JMP loop
)ASM";

    const std::string path =
        "debug_source_step_limit_test.zbin";

    SourceMappedProgramCleanup cleanup{
        path
    };

    writeSourceMappedProgram(
        path,
        "debug_source_step_limit_test.zasm",
        loopSource
    );

    DebugSession loop(path);

    if (
        !throwsRuntimeError(
            [&] {
                (void)loop
                    .stepSourceLine(0);
            }
        )
    ) {
        detail =
            "zero source-step limit was accepted";
        return false;
    }

    const DebugStop limited =
        loop.stepSourceLine(5);

    if (
        limited.reason
            != DebugStopReason::StepLimit
        || limited.executed_steps != 5
        || limited.total_steps != 5
        || limited.message.empty()
        || loop.cpu().state().pc()
            != memory_map::kBinaryCodeBase
    ) {
        detail =
            "source-step limit behavior mismatch";
        return false;
    }

    return true;
}

// Patch: v1.2-debug-session-source-step-r1

bool faultStop(std::string& detail) {
    using namespace zero_cpu::debug;

    const char* source = R"ASM(
.entry start
.text
start:
    DIV R0, R1
)ASM";

    DebugSession session(makeImage(source, "fault-test.zbin"));
    const DebugStop stop = session.continueExecution(10);

    if (
        !stop.faulted()
        || stop.message.empty()
        || stop.executed_steps != 1
        || stop.total_steps != 1
        || !session.cpu().state().hasError()
    ) {
        detail = "CPU fault stop mismatch";
        return false;
    }
    return true;
}

bool stepLimitStop(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    const char* source = R"ASM(
.entry loop
.text
loop:
    JMP loop
)ASM";

    DebugSession session(makeImage(source, "loop-test.zbin"));
    const DebugStop first = session.continueExecution(5);
    const DebugStop second = session.continueExecution(3);

    if (
        first.reason != DebugStopReason::StepLimit
        || first.executed_steps != 5
        || first.total_steps != 5
        || first.message.empty()
        || second.reason != DebugStopReason::StepLimit
        || second.executed_steps != 3
        || second.total_steps != 8
        || session.cpu().state().pc() != memory_map::kBinaryCodeBase
    ) {
        detail = "debugger step-limit stop mismatch";
        return false;
    }
    return true;
}

bool fileLoading(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    const std::string path = "debug_session_test.zbin";
    struct Cleanup {
        std::string path;
        ~Cleanup() { std::remove(path.c_str()); }
    } cleanup{path};

    Assembler assembler;
    binary::BinaryWriter writer;
    writer.writeFile(
        path,
        assembler.assembleString(kProgramSource).toBinaryProgram()
    );

    DebugSession session(path);
    const DebugStop stop = session.continueExecution(20);

    if (
        session.sourceName() != path
        || !stop.reachedProgramEnd()
        || session.totalSteps() != 3
    ) {
        detail = "debugger file loading mismatch";
        return false;
    }
    return true;
}

bool invalidSessionUse(std::string& detail) {
    using namespace zero_cpu::debug;

    DebugSession session;
    if (
        !throwsRuntimeError([&] { (void)session.step(); })
        || !throwsRuntimeError([&] { (void)session.continueExecution(1); })
    ) {
        detail = "unloaded debugger session was usable";
        return false;
    }

    DebugSession loaded(makeImage(kProgramSource, "invalid-limit-test.zbin"));
    if (!throwsRuntimeError([&] { (void)loaded.continueExecution(0); })) {
        detail = "zero debugger step limit was accepted";
        return false;
    }
    return true;
}

} // namespace

int main() {
    std::cout << "=== Zero-CPU Debug Session Test ===\n\n";
    int failures = 0;

    auto report = [&](const std::string& name, bool passed, const std::string& detail) {
        std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << "\n";
        if (!passed) {
            std::cout << "       " << detail << "\n";
            ++failures;
        }
    };

    {
        std::string detail;
        report("Stop reason strings", stopReasonStrings(detail), detail);
    }
    {
        std::string detail;
        report("Load and initial state", loadAndInitialState(detail), detail);
    }
    {
        std::string detail;
        report("Breakpoint management", breakpointManagement(detail), detail);
    }
    {
        std::string detail;
        report("Continue and resume", continueAndResume(detail), detail);
    }
    {
        std::string detail;
        report("Single-step execution", singleStepExecution(detail), detail);
    }
    {
        std::string detail;
        report(
            "Source-line stepping",
            sourceLineStepping(detail),
            detail
        );
    }
    {
        std::string detail;
        report(
            "Source-line breakpoint resume",
            sourceLineStepBreakpointResume(detail),
            detail
        );
    }
    {
        std::string detail;
        report(
            "Source-line destination breakpoint",
            sourceLineStepDestinationBreakpoint(detail),
            detail
        );
    }
    {
        std::string detail;
        report(
            "Source-line watchpoint priority",
            sourceLineStepWatchpointPriority(detail),
            detail
        );
    }
    {
        std::string detail;
        report(
            "Source-line validation and limit",
            sourceLineStepValidationAndLimit(detail),
            detail
        );
    }
    {
        std::string detail;
        report("CPU fault stop", faultStop(detail), detail);
    }
    {
        std::string detail;
        report("Step-limit stop", stepLimitStop(detail), detail);
    }
    {
        std::string detail;
        report("File loading", fileLoading(detail), detail);
    }
    {
        std::string detail;
        report("Invalid session use", invalidSessionUse(detail), detail);
    }

    std::cout << "\n";
    if (failures == 0) {
        std::cout << "Debug session test finished successfully.\n";
        return 0;
    }

    std::cout << "Debug session test failed. Failure count: "
              << failures << "\n";
    return 1;
}
