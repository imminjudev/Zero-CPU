#include "zero_cpu/system/EndToEndShowcase.hpp"

#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/hardware/HardwareMMIODevice.hpp"
#include "zero_cpu/hardware/MockHardwareBus.hpp"
#include "zero_cpu/kernel/ProtectedSyscallABI.hpp"
#include "zero_cpu/kernel/ProtectedSyscallDispatcher.hpp"
#include "zero_cpu/system/MultiProcessInvariantVerifier.hpp"
#include "zero_cpu/system/MultiProcessTraceJsonWriter.hpp"
#include "zero_cpu/system/ZeroFS.hpp"
#include "zero_cpu/trace/TraceJsonDiff.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

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

zero_cpu::system::ZeroFS::Bytes bytes(
    const std::string& value
) {
    return {value.begin(), value.end()};
}

std::string text(
    const zero_cpu::system::ZeroFS::Bytes& data
) {
    return std::string(data.begin(), data.end());
}

std::string childPath(
    const std::string& directory,
    const std::string& filename
) {
    return (
        std::filesystem::path(directory) / filename
    ).generic_string();
}

void assembleToBinary(
    const std::string& sourcePath,
    const std::string& binaryPath
) {
    zero_cpu::Assembler assembler;
    zero_cpu::binary::BinaryWriter writer;

    writer.writeFile(
        binaryPath,
        assembler
            .assembleFile(sourcePath)
            .toBinaryProgram()
    );
}

const ProcessSoftwareInterruptTraceRecord*
findInterrupt(
    const MultiProcessRunResult& result,
    zero_cpu::kernel::ProcessId pid,
    std::int64_t service
) {
    for (const auto& record : result.software_interrupts) {
        const auto& observed =
            record.observation.result;

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

zero_cpu::system::EndToEndShowcaseResult failResult(
    zero_cpu::system::EndToEndShowcaseResult result,
    const std::string& detail
) {
    result.passed = false;
    result.detail = detail;
    return result;
}

} // namespace

namespace zero_cpu::system {

EndToEndShowcaseResult EndToEndShowcaseRunner::run(
    const EndToEndShowcaseOptions& options
) const {
    EndToEndShowcaseResult result;

    result.filesystem_binary_path =
        childPath(
            options.output_directory,
            "showcase_fs_worker.zbin"
        );

    result.hardware_binary_path =
        childPath(
            options.output_directory,
            "showcase_hardware_fault.zbin"
        );

    result.trace_json_path =
        childPath(
            options.output_directory,
            "showcase_trace.json"
        );

    result.golden_trace_path =
        options.golden_trace_path;

    result.filesystem_before = "HELLO";

    try {
        std::filesystem::create_directories(
            options.output_directory
        );

        assembleToBinary(
            options.filesystem_source_path,
            result.filesystem_binary_path
        );

        assembleToBinary(
            options.hardware_source_path,
            result.hardware_binary_path
        );

        auto filesystem =
            std::make_shared<ZeroFS>();

        filesystem->putFile(
            "/data/showcase.txt",
            bytes(result.filesystem_before)
        );

        auto hardware =
            std::make_shared<
                hardware::MockHardwareBus
            >();

        hardware->connect();

        auto device =
            std::make_shared<
                hardware::HardwareMMIODevice
            >(hardware);

        auto mmio =
            std::make_shared<MMIOBus>();

        mmio->mapDevice(
            memory_map::kHardwareBase,
            memory_map::kHardwareSize,
            device
        );

        auto dispatcher =
            std::make_shared<
                kernel::ProtectedSyscallDispatcher
            >(filesystem);

        MultiProcessRunOptions runOptions;
        runOptions.quantum = 1;
        runOptions.max_lifecycle_steps = 200;
        runOptions.mmio_bus = mmio;
        runOptions.software_interrupt_handler =
            dispatcher;

        MultiProcessRunner runner;

        result.runtime =
            runner.runFiles(
                {
                    result.filesystem_binary_path,
                    result.hardware_binary_path
                },
                runOptions
            );

        if (
            !result.runtime.completed()
            || result.runtime.step_limit_reached
            || result.runtime.process_count != 2
            || result.runtime.termination_count != 2
            || result.runtime.fault_count != 1
        ) {
            return failResult(
                std::move(result),
                "runtime summary did not show exactly one isolated fault"
            );
        }

        const auto& survivor =
            result.runtime.process(1);

        const auto& faulted =
            result.runtime.process(2);

        result.survivor_exit_code =
            survivor.has_exit_code
                ? survivor.exit_code
                : 0;

        result.fault_message =
            faulted.termination_message;

        if (
            !survivor.terminated()
            || survivor.faulted()
            || !survivor.has_exit_code
            || survivor.exit_code != 0
            || !faulted.faulted()
        ) {
            return failResult(
                std::move(result),
                "fault isolation did not preserve PID 1"
            );
        }

        result.filesystem_after =
            text(
                filesystem->readFile(
                    "/data/showcase.txt"
                )
            );

        if (
            result.filesystem_after
            != "HELLOHELLO"
        ) {
            return failResult(
                std::move(result),
                "survivor did not finish ZeroFS read/write"
            );
        }

        const auto guest =
            survivor.final_memory.readBytes(
                kBuffer,
                5
            );

        if (
            std::string(
                guest.begin(),
                guest.end()
            ) != "HELLO"
            || survivor.final_memory.readI64(
                   kReadStatus
               ) != ABI::kStatusOk
            || survivor.final_memory.readI64(
                   kReadCount
               ) != 5
            || survivor.final_memory.readI64(
                   kWriteStatus
               ) != ABI::kStatusOk
            || survivor.final_memory.readI64(
                   kWriteCount
               ) != 5
            || survivor.final_memory.readI64(
                   kSurvivalMarker
               ) != 99
        ) {
            return failResult(
                std::move(result),
                "guest buffer/status/survival markers mismatch"
            );
        }

        result.gpio_output =
            hardware->registerValue(
                memory_map::
                    kHardwareGpioOutputOffset
            );

        result.hardware_write_count =
            hardware->writeCount();

        if (
            result.gpio_output != 42
            || result.hardware_write_count != 1
        ) {
            return failResult(
                std::move(result),
                "protected mock GPIO write was not observed"
            );
        }

        if (
            result.runtime.preemption_count == 0
            || result.runtime.context_switch_count == 0
        ) {
            return failResult(
                std::move(result),
                "timer preemption/context switching was not exercised"
            );
        }

        const auto* fsRead =
            findInterrupt(
                result.runtime,
                1,
                ABI::kFilesystemReadSyscall
            );

        const auto* fsWrite =
            findInterrupt(
                result.runtime,
                1,
                ABI::kFilesystemWriteSyscall
            );

        const auto* hwWrite =
            findInterrupt(
                result.runtime,
                2,
                ABI::kHardwareWriteSyscall
            );

        const auto* fsExit =
            findInterrupt(
                result.runtime,
                1,
                ABI::kExitSyscall
            );

        result.observed_filesystem_read =
            fsRead != nullptr;

        result.observed_filesystem_write =
            fsWrite != nullptr;

        result.observed_hardware_write =
            hwWrite != nullptr;

        result.observed_process_exit =
            fsExit != nullptr;

        if (
            !result.observed_filesystem_read
            || !result.observed_filesystem_write
            || !result.observed_hardware_write
            || !result.observed_process_exit
        ) {
            return failResult(
                std::move(result),
                "semantic syscall trace is incomplete"
            );
        }

        for (const auto& sw :
             result.runtime.context_switches) {
            if (
                sw.from_pid == 2
                && sw.to_pid == 1
                && sw.caused_by_termination
            ) {
                result.fault_handoff_step =
                    sw.lifecycle_step;
                break;
            }
        }

        for (const auto& event :
             result.runtime.execution_trace) {
            if (
                event.pid == 1
                && event.lifecycle_step
                    > result.fault_handoff_step
            ) {
                result.survivor_ran_after_fault =
                    true;
                break;
            }
        }

        if (
            result.fault_handoff_step == 0
            || !result.survivor_ran_after_fault
            || fsRead->lifecycle_step
                >= result.fault_handoff_step
            || hwWrite->lifecycle_step
                >= result.fault_handoff_step
            || fsWrite->lifecycle_step
                <= result.fault_handoff_step
        ) {
            return failResult(
                std::move(result),
                "timeline did not prove pre-fault work and post-fault continuation"
            );
        }

        const auto invariants =
            MultiProcessInvariantVerifier::verify(
                result.runtime
            );

        result.invariants_passed =
            invariants.passed();

        if (!result.invariants_passed) {
            return failResult(
                std::move(result),
                "showcase invariant verification failed"
            );
        }

        MultiProcessTraceJsonMetadata metadata;
        metadata.producer_version =
            "v1.9-showcase";

        const std::string json =
            MultiProcessTraceJsonWriter::toJson(
                result.runtime,
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

        for (const std::string& fragment :
             fragments) {
            if (
                json.find(fragment)
                == std::string::npos
            ) {
                return failResult(
                    std::move(result),
                    "trace JSON missing: "
                        + fragment
                );
            }
        }

        MultiProcessTraceJsonWriter::writeFile(
            result.trace_json_path,
            result.runtime,
            metadata
        );

        if (options.write_golden) {
            const std::filesystem::path
                goldenPath(
                    result.golden_trace_path
                );

            if (
                goldenPath.has_parent_path()
                && !goldenPath
                    .parent_path()
                    .empty()
            ) {
                std::filesystem::
                    create_directories(
                        goldenPath.parent_path()
                    );
            }

            MultiProcessTraceJsonWriter::writeFile(
                result.golden_trace_path,
                result.runtime,
                metadata
            );

            result.golden_written = true;
            result.golden_verified = true;
        } else {
            if (
                !std::filesystem::exists(
                    result.golden_trace_path
                )
            ) {
                return failResult(
                    std::move(result),
                    "showcase golden trace is missing"
                );
            }

            const TraceJsonDiffResult diff =
                TraceJsonDiff::compareFiles(
                    result.golden_trace_path,
                    result.trace_json_path
                );

            result.golden_verified =
                diff.equal;

            if (!diff.equal) {
                return failResult(
                    std::move(result),
                    "showcase golden trace mismatch: "
                        + diff.message
                        + "; path="
                        + diff.first_path
                        + "; expected="
                        + diff.expected_value
                        + "; actual="
                        + diff.actual_value
                );
            }
        }

        result.passed = true;
        result.detail =
            "protected multi-process showcase passed";

        return result;
    } catch (const std::exception& error) {
        return failResult(
            std::move(result),
            std::string(
                "showcase execution failed: "
            ) + error.what()
        );
    }
}

} // namespace zero_cpu::system
