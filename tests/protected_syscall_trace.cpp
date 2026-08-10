#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/SoftwareInterruptHandler.hpp"
#include "zero_cpu/hardware/HardwareMMIODevice.hpp"
#include "zero_cpu/hardware/MockHardwareBus.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"
#include "zero_cpu/kernel/ProtectedSyscallDispatcher.hpp"
#include "zero_cpu/system/MultiProcessInvariantVerifier.hpp"
#include "zero_cpu/system/MultiProcessRunner.hpp"
#include "zero_cpu/system/MultiProcessTraceJsonWriter.hpp"

#include <algorithm>
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

bool protectedSyscallObservability(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::hardware;
    using namespace zero_cpu::kernel;
    using namespace zero_cpu::system;

    auto hardware =
        std::make_shared<MockHardwareBus>(
            "syscall-trace-hardware"
        );

    hardware->connect();

    hardware->setRegisterValue(
        memory_map::kHardwareGpioInputOffset,
        77
    );

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
    MOV R2, 8
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

    MultiProcessRunOptions options;
    options.quantum = 100;
    options.max_lifecycle_steps = 100;
    options.mmio_bus = mmio;
    options.software_interrupt_handler =
        std::make_shared<
            ProtectedSyscallDispatcher
        >();

    MultiProcessRunner runner;

    const MultiProcessRunResult result =
        runner.runImages(
            {
                makeImage(
                    firstSource,
                    "syscall-trace-a.zbin"
                ),
                makeImage(
                    secondSource,
                    "syscall-trace-b.zbin"
                )
            },
            options
        );

    if (
        !result.success()
        || result.fault_count != 0
        || result.software_interrupts.size() != 3
    ) {
        detail =
            "protected workload did not produce "
            "exactly three software interrupt records";
        return false;
    }

    const ProcessSoftwareInterruptTraceRecord& write =
        result.software_interrupts[0];

    const ProcessSoftwareInterruptTraceRecord& read =
        result.software_interrupts[1];

    const ProcessSoftwareInterruptTraceRecord& exit =
        result.software_interrupts[2];

    if (
        write.pid != 1
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
            "hardware write syscall trace metadata mismatch";
        return false;
    }

    if (
        read.pid != 1
        || read.observation.vector != 80
        || !read.observation.result.has_service_number
        || read.observation.result.service_number != 21
        || !read.observation.result.has_argument0
        || read.observation.result.argument0 != 8
        || read.observation.result.has_argument1
        || !read.observation.result.has_status
        || read.observation.result.status != 0
        || !read.observation.result.has_result
        || read.observation.result.result_value != 77
    ) {
        detail =
            "hardware read syscall trace metadata mismatch";
        return false;
    }

    if (
        exit.pid != 1
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
            "process exit syscall trace metadata mismatch";
        return false;
    }

    if (
        !(write.lifecycle_step < read.lifecycle_step)
        || !(read.lifecycle_step < exit.lifecycle_step)
    ) {
        detail =
            "software interrupt lifecycle ordering is invalid";
        return false;
    }

    if (
        result.process(1).final_memory.read(100) != 77
        || result.process(1).final_memory.read(108) != 0
        || result.process(1).exit_code != 7
        || result.process(2).final_memory.read(116) != 222
    ) {
        detail =
            "observable syscall workload final state mismatch";
        return false;
    }

    const MultiProcessInvariantReport report =
        MultiProcessInvariantVerifier::verify(
            result
        );

    if (!report.passed()) {
        detail =
            "valid protected syscall trace violated "
            "multi-process invariants";
        return false;
    }

    const std::string json =
        MultiProcessTraceJsonWriter::toJson(
            result
        );

    const char* requiredJson[] = {
        "\"software_interrupt_event_count\": 3",
        "\"software_interrupts\": [",
        "\"service_number\": 20",
        "\"service_number\": 21",
        "\"service_number\": 3",
        "\"argument1\": 42",
        "\"result_value\": 77",
        "\"disposition\": \"TerminateProcess\"",
        "\"exit_code\": 7"
    };

    for (const char* token : requiredJson) {
        if (json.find(token) == std::string::npos) {
            detail =
                std::string(
                    "multi-process trace JSON missing token: "
                )
                + token;
            return false;
        }
    }

    MultiProcessRunResult corrupted = result;
    corrupted.software_interrupts[0].pid = 999;

    const MultiProcessInvariantReport corruptReport =
        MultiProcessInvariantVerifier::verify(
            corrupted
        );

    const bool detected =
        std::any_of(
            corruptReport.violations.begin(),
            corruptReport.violations.end(),
            [](const MultiProcessInvariantViolation& violation) {
                return violation.code
                    == "software_interrupt_unknown_pid";
            }
        );

    if (!detected) {
        detail =
            "invariant verifier did not detect "
            "corrupt software interrupt PID";
        return false;
    }

    return true;
}

// Patch: v1.5-protected-syscall-observability-r1

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Protected Syscall "
           "Trace Test ===\n\n";

    std::string detail;

    const bool passed =
        protectedSyscallObservability(
            detail
        );

    std::cout
        << (passed ? "[PASS] " : "[FAIL] ")
        << "Protected syscall observability\n";

    if (!passed) {
        std::cout
            << "       "
            << detail
            << "\n";
        return 1;
    }

    std::cout
        << "\nProtected syscall trace test "
           "finished successfully.\n";

    return 0;
}
