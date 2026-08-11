#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/debug/MultiProcessDebugConsole.hpp"
#include "zero_cpu/debug/MultiProcessDebugSession.hpp"
#include "zero_cpu/hardware/HardwareMMIODevice.hpp"
#include "zero_cpu/hardware/MockHardwareBus.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"
#include "zero_cpu/kernel/ProtectedSyscallDispatcher.hpp"

#include <cstddef>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

zero_cpu::kernel::ProcessImage makeImage(
    const std::string& source,
    const std::string& name
) {
    zero_cpu::Assembler assembler;
    zero_cpu::kernel::ProcessImageLoader loader;

    return loader.loadProgram(
        assembler.assembleString(
            source
        ).toBinaryProgram(),
        name
    );
}

bool protectedRuntimeServices(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;
    using namespace zero_cpu::hardware;
    using namespace zero_cpu::kernel;

    auto hardware =
        std::make_shared<MockHardwareBus>(
            "debug-protected-hardware"
        );

    hardware->connect();

    auto device =
        std::make_shared<HardwareMMIODevice>(
            hardware
        );

    auto mmio =
        std::make_shared<MMIOBus>();

    mmio->mapDevice(
        memory_map::kHardwareBase,
        memory_map::kHardwareSize,
        device
    );

    const char* firstSource = R"ASM(
.entry start
.text
start:
    MOV R1, 20
    MOV R2, 0
    MOV R3, 42
    INT 80

    MOV R1, 21
    MOV R2, 0
    INT 80
    STORE [100], R2

    MOV R1, 3
    MOV R2, 7
    INT 80

    MOV R5, 999
    STORE [108], R5
)ASM";

    const char* secondSource = R"ASM(
.entry start
.text
start:
    MOV R6, 222
    STORE [116], R6
)ASM";

    MultiProcessDebugOptions options;
    options.quantum = 100;
    options.default_continue_steps = 100;
    options.mmio_bus = mmio;
    options.software_interrupt_handler =
        std::make_shared<
            ProtectedSyscallDispatcher
        >();

    MultiProcessDebugSession session(
        std::vector<ProcessImage>{
            makeImage(
                firstSource,
                "debug-protected-a.zbin"
            ),
            makeImage(
                secondSource,
                "debug-protected-b.zbin"
            )
        },
        options
    );

    if (
        !session.started()
        || session.runtimeState()
            != ProcessRuntimeState::Running
        || session.runningPid() != 1
    ) {
        detail =
            "protected debug runtime did not "
            "start on PID 1";
        return false;
    }

    const MultiProcessDebugStop firstStop =
        session.continueExecution(100);

    if (
        firstStop.reason
            != MultiProcessDebugStopReason::
                ProcessTerminated
        || !firstStop.process_terminated
        || firstStop.process_faulted
        || firstStop.terminated_pid != 1
        || session.runningPid() != 2
    ) {
        detail =
            "syscall exit did not stop debugger "
            "and hand off to PID 2";
        return false;
    }

    const std::vector<
        ProcessSoftwareInterruptDebugRecord
    >& softwareInterrupts =
        session.softwareInterrupts();

    if (softwareInterrupts.size() != 3) {
        detail =
            "debugger did not retain exactly three "
            "protected syscall observations";
        return false;
    }

    const ProcessSoftwareInterruptDebugRecord&
        write = softwareInterrupts[0];

    const ProcessSoftwareInterruptDebugRecord&
        read = softwareInterrupts[1];

    const ProcessSoftwareInterruptDebugRecord&
        exit = softwareInterrupts[2];

    if (
        write.pid != 1
        || write.lifecycle_step == 0
        || write.observation.vector != 80
        || !write.observation.result.has_service_number
        || write.observation.result.service_number != 20
        || !write.observation.result.has_argument0
        || write.observation.result.argument0 != 0
        || !write.observation.result.has_argument1
        || write.observation.result.argument1 != 42
        || !write.observation.result.has_status
        || write.observation.result.status != 0
        || write.observation.result.has_result
        || write.observation.result.disposition
            != SoftwareInterruptDisposition::
                ReturnToCaller
    ) {
        detail =
            "debugger hardware-write syscall "
            "observation mismatch";
        return false;
    }

    if (
        read.pid != 1
        || read.lifecycle_step <= write.lifecycle_step
        || read.observation.vector != 80
        || !read.observation.result.has_service_number
        || read.observation.result.service_number != 21
        || !read.observation.result.has_argument0
        || read.observation.result.argument0 != 0
        || read.observation.result.has_argument1
        || !read.observation.result.has_status
        || read.observation.result.status != 0
        || !read.observation.result.has_result
        || read.observation.result.result_value != 42
        || read.observation.result.disposition
            != SoftwareInterruptDisposition::
                ReturnToCaller
    ) {
        detail =
            "debugger hardware-read syscall "
            "observation mismatch";
        return false;
    }

    if (
        exit.pid != 1
        || exit.lifecycle_step <= read.lifecycle_step
        || exit.observation.vector != 80
        || !exit.observation.result.has_service_number
        || exit.observation.result.service_number != 3
        || !exit.observation.result.has_argument0
        || exit.observation.result.argument0 != 7
        || !exit.observation.result.has_status
        || exit.observation.result.status != 0
        || exit.observation.result.disposition
            != SoftwareInterruptDisposition::
                TerminateProcess
        || exit.observation.result.exit_code != 7
    ) {
        detail =
            "debugger process-exit syscall "
            "observation mismatch";
        return false;
    }

    const std::vector<
        ProcessSoftwareInterruptDebugRecord
    > pid1SoftwareInterrupts =
        session.softwareInterrupts(1);

    const std::vector<
        ProcessSoftwareInterruptDebugRecord
    > pid2SoftwareInterrupts =
        session.softwareInterrupts(2);

    if (
        pid1SoftwareInterrupts.size() != 3
        || !pid2SoftwareInterrupts.empty()
    ) {
        detail =
            "debugger syscall PID filtering mismatch";
        return false;
    }

    const ProcessDebugSnapshot first =
        session.processSnapshot(1);

    if (
        !first.terminated()
        || first.faulted()
        || first.exit_code != 7
        || first.context.privilege
            != PrivilegeLevel::User
    ) {
        detail =
            "terminated PID 1 snapshot is invalid";
        return false;
    }

    if (
        first.memory.read(100) != 42
        || first.memory.read(108) != 0
    ) {
        detail =
            "debugger snapshot did not preserve "
            "protected syscall memory effects";
        return false;
    }

    if (
        hardware->registerValue(
            memory_map::kHardwareGpioOutputOffset
        ) != 42
        || hardware->writeCount() != 1
        || hardware->readCount() != 1
    ) {
        detail =
            "debug session did not route protected "
            "syscalls through HardwareBus";
        return false;
    }

    const MultiProcessDebugStop secondStop =
        session.continueExecution(100);

    if (
        secondStop.reason
            != MultiProcessDebugStopReason::
                ProcessTerminated
        || secondStop.terminated_pid != 2
    ) {
        detail =
            "PID 2 did not terminate after "
            "scheduler handoff";
        return false;
    }

    if (
        session.softwareInterrupts().size() != 3
    ) {
        detail =
            "debugger duplicated syscall observations "
            "while running PID 2";
        return false;
    }

    const ProcessDebugSnapshot second =
        session.processSnapshot(2);

    if (
        !second.terminated()
        || second.faulted()
        || second.exit_code != 0
        || second.memory.read(116) != 222
    ) {
        detail =
            "PID 2 final debugger snapshot mismatch";
        return false;
    }

    const MultiProcessDebugStop completed =
        session.continueExecution(1);

    if (
        completed.reason
            != MultiProcessDebugStopReason::
                RuntimeCompleted
        || session.runtimeState()
            != ProcessRuntimeState::Completed
    ) {
        detail =
            "debug runtime did not report completion";
        return false;
    }

    const std::vector<ContextSwitchRecord>& switches =
        session.contextSwitches();

    if (
        switches.size() != 2
        || switches[0].from_pid != 1
        || switches[0].to_pid != 2
        || !switches[0].caused_by_termination
        || switches[1].from_pid != 2
        || switches[1].to_pid != 0
        || !switches[1].caused_by_termination
    ) {
        detail =
            "debugger context-switch timeline mismatch";
        return false;
    }

    std::istringstream consoleInput(
        "syscalls\n"
        "syscalls 1\n"
        "syscalls 2\n"
        "quit\n"
    );

    std::ostringstream consoleOutput;
    std::ostringstream consoleError;

    MultiProcessDebugConsoleOptions consoleOptions;
    consoleOptions.show_prompt = false;
    consoleOptions.print_banner = false;

    MultiProcessDebugConsole console(
        session,
        consoleInput,
        consoleOutput,
        consoleError,
        consoleOptions
    );

    const MultiProcessDebugConsoleResult
        consoleResult =
            console.run();

    const std::string consoleText =
        consoleOutput.str();

    if (
        !consoleResult.success()
        || !consoleResult.quit_requested
        || consoleResult.command_count != 4
        || !consoleError.str().empty()
        || consoleText.find(
            "Software interrupts (3):"
        ) == std::string::npos
        || consoleText.find("pid=1")
            == std::string::npos
        || consoleText.find("vector=80")
            == std::string::npos
        || consoleText.find("service=20")
            == std::string::npos
        || consoleText.find("arg1=42")
            == std::string::npos
        || consoleText.find("service=21")
            == std::string::npos
        || consoleText.find("result=42")
            == std::string::npos
        || consoleText.find("service=3")
            == std::string::npos
        || consoleText.find(
            "disposition=TerminateProcess"
        ) == std::string::npos
        || consoleText.find("exit=7")
            == std::string::npos
        || consoleText.find(
            "No observed software interrupts."
        ) == std::string::npos
    ) {
        detail =
            "protected syscall console output mismatch";
        return false;
    }

    return true;
}

// Patch: v1.4-protected-debug-runtime-r1

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Protected Debug Runtime "
           "Test ===\n\n";

    std::string detail;

    const bool passed =
        protectedRuntimeServices(
            detail
        );

    std::cout
        << (passed ? "[PASS] " : "[FAIL] ")
        << "Protected services in multi-process debugger\n";

    if (!passed) {
        std::cout
            << "       "
            << detail
            << "\n";
        return 1;
    }

    std::cout
        << "\nProtected debug runtime test "
           "finished successfully.\n";

    return 0;
}

// Patch: v1.5-debugger-syscall-observability-r1

// Patch: v1.5-debugger-syscall-console-r1
