#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"
#include "zero_cpu/kernel/ProcessTermination.hpp"
#include "zero_cpu/kernel/ProtectedSyscallDispatcher.hpp"
#include "zero_cpu/system/MultiProcessRunner.hpp"

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>

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

bool processExitAndSchedulerHandoff(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;
    using namespace zero_cpu::system;

    const char* exitingSource = R"ASM(
.entry start
.text
start:
    MOV R1, 3
    MOV R2, 37
    INT 80

    MOV R5, 999
    STORE [100], R5
)ASM";

    const char* continuingSource = R"ASM(
.entry start
.text
start:
    MOV R7, 88
    MOV R6, 222
    STORE [108], R6
)ASM";

    MultiProcessRunOptions options;
    options.quantum = 100;
    options.max_lifecycle_steps = 100;
    options.normal_exit_code = 0;

    options.software_interrupt_handler =
        std::make_shared<
            ProtectedSyscallDispatcher
        >();

    MultiProcessRunner runner;

    const MultiProcessRunResult result =
        runner.runImages(
            {
                makeImage(
                    exitingSource,
                    "protected-exit-a.zbin"
                ),
                makeImage(
                    continuingSource,
                    "protected-exit-b.zbin"
                )
            },
            options
        );

    if (
        !result.completed()
        || !result.success()
        || result.process_count != 2
        || result.termination_count != 2
        || result.fault_count != 0
    ) {
        detail =
            "multi-process exit workload did not "
            "complete cleanly";
        return false;
    }

    const ProcessRunSummary& exited =
        result.process(1);

    const ProcessRunSummary& continued =
        result.process(2);

    if (
        !exited.terminated()
        || exited.faulted()
        || exited.exit_code != 37
        || exited.termination_kind
            != ProcessTerminationKind::NormalExit
    ) {
        detail =
            "syscall exit code was not recorded "
            "as a normal process termination";
        return false;
    }

    if (
        exited.final_context.registers[
            static_cast<std::size_t>(
                RegisterName::R7
            )
        ] != 37
        || exited.final_context.registers[
            static_cast<std::size_t>(
                RegisterName::R4
            )
        ] != 0
        || exited.final_context.privilege
            != PrivilegeLevel::User
    ) {
        detail =
            "protected exit did not preserve "
            "ABI status or User return context";
        return false;
    }

    if (exited.final_memory.read(100) != 0) {
        detail =
            "instructions after exit syscall "
            "were executed";
        return false;
    }

    if (
        !continued.terminated()
        || continued.faulted()
        || continued.exit_code != 0
        || continued.final_memory.read(108) != 222
    ) {
        detail =
            "next process did not continue after "
            "the exiting process";
        return false;
    }

    if (
        continued.final_context.registers[
            static_cast<std::size_t>(
                RegisterName::R7
            )
        ] != 88
    ) {
        detail =
            "ordinary executable completion lost "
            "its final register state";
        return false;
    }

    if (result.context_switches.size() != 2) {
        detail =
            "unexpected process-exit context "
            "switch count";
        return false;
    }

    const ProcessContextSwitchTraceRecord& handoff =
        result.context_switches[0];

    const ProcessContextSwitchTraceRecord& finish =
        result.context_switches[1];

    if (
        handoff.from_pid != 1
        || handoff.to_pid != 2
        || handoff.preempted
        || !handoff.caused_by_termination
    ) {
        detail =
            "exit syscall did not hand off "
            "directly from PID 1 to PID 2";
        return false;
    }

    if (
        finish.from_pid != 2
        || finish.to_pid != 0
        || finish.preempted
        || !finish.caused_by_termination
    ) {
        detail =
            "final process termination timeline "
            "is incorrect";
        return false;
    }

    return true;
}

bool finalInstructionProcessExit(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;
    using namespace zero_cpu::system;

    const char* exitingSource = R"ASM(
.entry start
.text
start:
    MOV R1, 3
    MOV R2, 41
    INT 80
)ASM";

    const char* peerSource = R"ASM(
.entry start
.text
start:
    MOV R6, 77
)ASM";

    MultiProcessRunOptions options;
    options.quantum = 100;
    options.max_lifecycle_steps = 100;

    options.software_interrupt_handler =
        std::make_shared<
            ProtectedSyscallDispatcher
        >();

    MultiProcessRunner runner;

    const MultiProcessRunResult result =
        runner.runImages(
            {
                makeImage(
                    exitingSource,
                    "protected-final-exit-a.zbin"
                ),
                makeImage(
                    peerSource,
                    "protected-final-exit-b.zbin"
                )
            },
            options
        );

    if (
        !result.completed()
        || !result.success()
        || result.fault_count != 0
        || result.termination_count != 2
    ) {
        detail =
            "final-instruction exit syscall did not "
            "complete cleanly";
        return false;
    }

    const ProcessRunSummary& exited =
        result.process(1);

    const ProcessRunSummary& peer =
        result.process(2);

    if (
        !exited.terminated()
        || exited.faulted()
        || exited.exit_code != 41
        || exited.termination_kind
            != ProcessTerminationKind::NormalExit
        || exited.final_context.privilege
            != PrivilegeLevel::User
    ) {
        detail =
            "final-instruction exit syscall was not "
            "recorded as a normal User termination";
        return false;
    }

    if (
        !peer.terminated()
        || peer.faulted()
        || peer.exit_code != 0
        || peer.final_context.registers[
            static_cast<std::size_t>(
                RegisterName::R6
            )
        ] != 77
    ) {
        detail =
            "scheduler did not continue after "
            "final-instruction process exit";
        return false;
    }

    if (
        result.software_interrupts.size() != 1
        || result.software_interrupts[0].pid != 1
        || result.software_interrupts[0]
            .observation.vector != 80
        || !result.software_interrupts[0]
            .observation.result
            .has_service_number
        || result.software_interrupts[0]
            .observation.result
            .service_number != 3
        || result.software_interrupts[0]
            .observation.result
            .disposition
            != SoftwareInterruptDisposition::
                TerminateProcess
        || result.software_interrupts[0]
            .observation.result.exit_code != 41
    ) {
        detail =
            "final-instruction exit semantic "
            "observation mismatch";
        return false;
    }

    return true;
}

// Patch: v1.4-protected-process-exit-r1

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Protected Process "
           "Exit Test ===\n\n";

    std::string detail;

    const bool passed =
        processExitAndSchedulerHandoff(
            detail
        );

    std::cout
        << (passed ? "[PASS] " : "[FAIL] ")
        << "Protected process exit and "
           "scheduler handoff\n";

    if (!passed) {
        std::cout
            << "       "
            << detail
            << "\n";

        return 1;
    }

    detail.clear();

    const bool finalInstructionPassed =
        finalInstructionProcessExit(
            detail
        );

    std::cout
        << (
            finalInstructionPassed
                ? "[PASS] "
                : "[FAIL] "
        )
        << "Final-instruction protected process exit\n";

    if (!finalInstructionPassed) {
        std::cout
            << "       "
            << detail
            << "\n";

        return 1;
    }

    std::cout
        << "\nProtected process exit test "
           "finished successfully.\n";

    return 0;
}

// Patch: v1.5-final-exit-core-fix-r1
