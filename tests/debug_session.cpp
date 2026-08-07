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
