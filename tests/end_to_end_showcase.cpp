#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/hardware/HardwareMMIODevice.hpp"
#include "zero_cpu/hardware/MockHardwareBus.hpp"
#include "zero_cpu/kernel/ProtectedSyscallABI.hpp"
#include "zero_cpu/kernel/ProtectedSyscallDispatcher.hpp"
#include "zero_cpu/system/MultiProcessInvariantVerifier.hpp"
#include "zero_cpu/system/MultiProcessRunner.hpp"
#include "zero_cpu/system/MultiProcessTraceJsonWriter.hpp"
#include "zero_cpu/system/ZeroFS.hpp"
#include "zero_cpu/trace/TraceJsonDiff.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using ABI = zero_cpu::kernel::ProtectedSyscallABI;
using zero_cpu::system::MultiProcessRunResult;
using zero_cpu::system::ProcessSoftwareInterruptTraceRecord;

constexpr std::size_t kBuffer = 80;
constexpr std::size_t kReadStatus = 88;
constexpr std::size_t kReadCount = 96;
constexpr std::size_t kWriteStatus = 104;
constexpr std::size_t kWriteCount = 112;
constexpr std::size_t kSurvivalMarker = 120;

constexpr const char* kGoldenTrace =
    "tests/golden/end_to_end_showcase.json";

zero_cpu::system::ZeroFS::Bytes bytes(const std::string& text) {
    return {text.begin(), text.end()};
}

std::string text(const zero_cpu::system::ZeroFS::Bytes& data) {
    return std::string(data.begin(), data.end());
}

void assembleToBinary(
    const std::string& sourcePath,
    const std::string& binaryPath
) {
    zero_cpu::Assembler assembler;
    zero_cpu::binary::BinaryWriter writer;
    writer.writeFile(
        binaryPath,
        assembler.assembleFile(sourcePath).toBinaryProgram()
    );
}

const ProcessSoftwareInterruptTraceRecord* findInterrupt(
    const MultiProcessRunResult& result,
    zero_cpu::kernel::ProcessId pid,
    std::int64_t service
) {
    for (const auto& record : result.software_interrupts) {
        const auto& observed = record.observation.result;
        if (
            record.pid == pid
            && observed.has_service_number
            && observed.service_number == service
        ) {
            return &record;
        }
    }
    return nullptr;
}

bool runShowcase(
    std::string& detail,
    bool writeGolden
) {
    using namespace zero_cpu;
    using namespace zero_cpu::system;

    std::filesystem::create_directories("build/showcase");

    const std::string fsBin =
        "build/showcase/showcase_fs_worker.zbin";
    const std::string hwBin =
        "build/showcase/showcase_hardware_fault.zbin";
    const std::string traceJson =
        "build/showcase/showcase_trace.json";

    try {
        assembleToBinary(
            "examples/showcase_fs_worker.zasm",
            fsBin
        );
        assembleToBinary(
            "examples/showcase_hardware_fault.zasm",
            hwBin
        );
    } catch (const std::exception& error) {
        detail = std::string(".zasm -> .zbin failed: ") + error.what();
        return false;
    }

    auto filesystem = std::make_shared<ZeroFS>();
    filesystem->putFile(
        "/data/showcase.txt",
        bytes("HELLO")
    );

    auto hardware =
        std::make_shared<hardware::MockHardwareBus>();
    hardware->connect();

    auto device =
        std::make_shared<hardware::HardwareMMIODevice>(
            hardware
        );

    auto mmio = std::make_shared<MMIOBus>();
    mmio->mapDevice(
        memory_map::kHardwareBase,
        memory_map::kHardwareSize,
        device
    );

    auto dispatcher =
        std::make_shared<
            kernel::ProtectedSyscallDispatcher
        >(filesystem);

    MultiProcessRunOptions options;
    options.quantum = 1;
    options.max_lifecycle_steps = 200;
    options.mmio_bus = mmio;
    options.software_interrupt_handler = dispatcher;

    MultiProcessRunner runner;
    const MultiProcessRunResult result =
        runner.runFiles({fsBin, hwBin}, options);

    if (
        !result.completed()
        || result.step_limit_reached
        || result.process_count != 2
        || result.termination_count != 2
        || result.fault_count != 1
    ) {
        detail = "runtime summary did not show exactly one isolated fault";
        return false;
    }

    const auto& survivor = result.process(1);
    const auto& faulted = result.process(2);

    if (
        !survivor.terminated()
        || survivor.faulted()
        || !survivor.has_exit_code
        || survivor.exit_code != 0
        || !faulted.faulted()
    ) {
        detail = "fault isolation did not preserve PID 1";
        return false;
    }

    if (
        text(filesystem->readFile("/data/showcase.txt"))
        != "HELLOHELLO"
    ) {
        detail = "survivor did not finish ZeroFS read/write";
        return false;
    }

    const auto guest =
        survivor.final_memory.readBytes(kBuffer, 5);

    if (
        std::string(guest.begin(), guest.end()) != "HELLO"
        || survivor.final_memory.readI64(kReadStatus)
            != ABI::kStatusOk
        || survivor.final_memory.readI64(kReadCount) != 5
        || survivor.final_memory.readI64(kWriteStatus)
            != ABI::kStatusOk
        || survivor.final_memory.readI64(kWriteCount) != 5
        || survivor.final_memory.readI64(kSurvivalMarker) != 99
    ) {
        detail = "guest buffer/status/survival markers mismatch";
        return false;
    }

    if (
        hardware->registerValue(
            memory_map::kHardwareGpioOutputOffset
        ) != 42
        || hardware->writeCount() != 1
    ) {
        detail = "protected mock GPIO write was not observed";
        return false;
    }

    if (
        result.preemption_count == 0
        || result.context_switch_count == 0
    ) {
        detail = "timer preemption/context switching was not exercised";
        return false;
    }

    const auto* fsRead =
        findInterrupt(
            result, 1, ABI::kFilesystemReadSyscall
        );
    const auto* fsWrite =
        findInterrupt(
            result, 1, ABI::kFilesystemWriteSyscall
        );
    const auto* hwWrite =
        findInterrupt(
            result, 2, ABI::kHardwareWriteSyscall
        );
    const auto* fsExit =
        findInterrupt(
            result, 1, ABI::kExitSyscall
        );

    if (
        fsRead == nullptr
        || fsWrite == nullptr
        || hwWrite == nullptr
        || fsExit == nullptr
    ) {
        detail = "semantic syscall trace is incomplete";
        return false;
    }

    std::size_t faultStep = 0;
    for (const auto& sw : result.context_switches) {
        if (
            sw.from_pid == 2
            && sw.to_pid == 1
            && sw.caused_by_termination
        ) {
            faultStep = sw.lifecycle_step;
            break;
        }
    }

    bool survivorRanAfterFault = false;
    for (const auto& event : result.execution_trace) {
        if (
            event.pid == 1
            && event.lifecycle_step > faultStep
        ) {
            survivorRanAfterFault = true;
            break;
        }
    }

    if (
        faultStep == 0
        || !survivorRanAfterFault
        || fsRead->lifecycle_step >= faultStep
        || hwWrite->lifecycle_step >= faultStep
        || fsWrite->lifecycle_step <= faultStep
    ) {
        detail =
            "timeline did not prove pre-fault work and post-fault continuation";
        return false;
    }

    const auto invariants =
        MultiProcessInvariantVerifier::verify(result);

    if (!invariants.passed()) {
        detail = "showcase invariant verification failed";
        return false;
    }

    MultiProcessTraceJsonMetadata metadata;
    metadata.producer_version = "v1.9-showcase";

    const std::string json =
        MultiProcessTraceJsonWriter::toJson(
            result,
            metadata
        );

    const std::string fragments[] = {
        "\"fault_count\": 1",
        "\"passed\": true",
        "\"service_number\": 20",
        "\"service_number\": 31",
        "\"service_number\": 32",
        "\"caused_by_termination\": true"
    };

    for (const std::string& fragment : fragments) {
        if (json.find(fragment) == std::string::npos) {
            detail = "trace JSON missing: " + fragment;
            return false;
        }
    }

    MultiProcessTraceJsonWriter::writeFile(
        traceJson,
        result,
        metadata
    );

    if (writeGolden) {
        const std::filesystem::path goldenPath(
            kGoldenTrace
        );

        if (
            goldenPath.has_parent_path()
            && !goldenPath.parent_path().empty()
        ) {
            std::filesystem::create_directories(
                goldenPath.parent_path()
            );
        }

        MultiProcessTraceJsonWriter::writeFile(
            kGoldenTrace,
            result,
            metadata
        );

        std::cout
            << "Golden trace written:\n"
            << "  "
            << kGoldenTrace
            << "\n";
    } else {
        if (!std::filesystem::exists(kGoldenTrace)) {
            detail =
                "showcase golden trace is missing; run "
                "zero_end_to_end_showcase_test "
                "--write-golden once";
            return false;
        }

        const TraceJsonDiffResult diff =
            TraceJsonDiff::compareFiles(
                kGoldenTrace,
                traceJson
            );

        if (!diff.equal) {
            detail =
                "showcase golden trace mismatch: "
                + diff.message
                + "; path="
                + diff.first_path
                + "; expected="
                + diff.expected_value
                + "; actual="
                + diff.actual_value;
            return false;
        }
    }

    std::cout
        << "Artifacts:\n"
        << "  " << fsBin << "\n"
        << "  " << hwBin << "\n"
        << "  " << traceJson << "\n";

    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    bool writeGolden = false;

    if (argc == 2) {
        const std::string option = argv[1];

        if (option != "--write-golden") {
            std::cerr
                << "Unknown option: "
                << option
                << "\n";
            return 2;
        }

        writeGolden = true;
    } else if (argc != 1) {
        std::cerr
            << "Usage: "
            << argv[0]
            << " [--write-golden]\n";
        return 2;
    }

    std::cout
        << "=== Zero-CPU End-to-End Showcase Test ===\n\n";

    std::string detail;
    const bool passed =
        runShowcase(
            detail,
            writeGolden
        );

    std::cout
        << (passed ? "[PASS] " : "[FAIL] ")
        << "Protected multi-process showcase\n";

    if (!passed) {
        std::cout << "       " << detail << "\n";
        return 1;
    }

    std::cout
        << "\nEnd-to-end showcase test finished successfully.\n";

    return 0;
}

// Patch: v1.9-end-to-end-showcase-r1
// Patch: v1.9-showcase-golden-regression-r1
