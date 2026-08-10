#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"
#include "zero_cpu/system/MultiProcessRunner.hpp"
#include "zero_cpu/system/MultiProcessTraceJsonWriter.hpp"
#include "zero_cpu/trace/TraceJsonDiff.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr const char* kGoldenPath =
    "tests/golden/multiprocess_trace_smoke.json";

constexpr const char* kActualPath =
    "build/test-output/multiprocess_trace_smoke_actual.json";

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

zero_cpu::system::MultiProcessRunResult
makeResult(std::uint64_t quantum) {
    const char* faultingSource = R"ASM(
.entry start
.text
start:
    MOV R0, 1
    ADD R0, 1
    DIV R0, R1
)ASM";

    const char* healthySource = R"ASM(
.entry start
.text
start:
    MOV R0, 10
    ADD R0, 5
    MOV R1, R0
)ASM";

    zero_cpu::system::MultiProcessRunOptions options;
    options.quantum = quantum;
    options.max_lifecycle_steps = 100;
    options.fault_exit_code = -77;

    zero_cpu::system::MultiProcessRunner runner;

    return runner.runImages(
        {
            makeImage(
                faultingSource,
                "golden-fault.zbin"
            ),
            makeImage(
                healthySource,
                "golden-healthy.zbin"
            )
        },
        options
    );
}

std::string makeJson(
    std::uint64_t quantum,
    const std::string& producerVersion
) {
    zero_cpu::system::MultiProcessTraceJsonMetadata
        metadata;

    metadata.producer_version =
        producerVersion;

    return
        zero_cpu::system::
            MultiProcessTraceJsonWriter::toJson(
                makeResult(quantum),
                metadata
            );
}

void writeGolden() {
    const std::filesystem::path path(
        kGoldenPath
    );

    if (
        path.has_parent_path()
        && !path.parent_path().empty()
    ) {
        std::filesystem::create_directories(
            path.parent_path()
        );
    }

    zero_cpu::system::MultiProcessTraceJsonMetadata
        metadata;

    metadata.producer_version =
        "v1.3-golden";

    zero_cpu::system::
        MultiProcessTraceJsonWriter::writeFile(
            kGoldenPath,
            makeResult(1),
            metadata
        );

    std::cout
        << "Wrote initial multi-process golden trace:\n"
        << kGoldenPath
        << "\n";
}

bool architecturalMetadataIgnored(
    std::string& detail
) {
    const zero_cpu::TraceJsonDiffResult result =
        zero_cpu::TraceJsonDiff::compareText(
            makeJson(1, "expected-metadata"),
            makeJson(1, "actual-metadata")
        );

    if (
        !result.equal
        || result.difference_count != 0
    ) {
        detail =
            "architectural diff treated producer "
            "metadata as architecture";
        return false;
    }

    return true;
}

bool strictMetadataDetected(
    std::string& detail
) {
    zero_cpu::TraceJsonDiffOptions options;
    options.strict = true;

    const zero_cpu::TraceJsonDiffResult result =
        zero_cpu::TraceJsonDiff::compareText(
            makeJson(1, "expected-metadata"),
            makeJson(1, "actual-metadata"),
            options
        );

    if (
        result.equal
        || result.difference_count == 0
    ) {
        detail =
            "strict diff ignored multi-process "
            "metadata difference";
        return false;
    }

    return true;
}

bool schedulerRegressionDetected(
    std::string& detail
) {
    const zero_cpu::TraceJsonDiffResult result =
        zero_cpu::TraceJsonDiff::compareText(
            makeJson(1, "same"),
            makeJson(2, "same")
        );

    if (
        result.equal
        || result.difference_count == 0
        || result.first_path.empty()
    ) {
        detail =
            "scheduler timeline change was not "
            "detected by architectural diff";
        return false;
    }

    return true;
}

bool goldenRegression(
    std::string& detail
) {
    if (
        !std::filesystem::exists(
            kGoldenPath
        )
    ) {
        detail =
            "golden fixture is missing; run "
            "zero_multi_process_trace_diff_test "
            "--write-golden once";
        return false;
    }

    const std::filesystem::path actualPath(
        kActualPath
    );

    if (
        actualPath.has_parent_path()
        && !actualPath.parent_path().empty()
    ) {
        std::filesystem::create_directories(
            actualPath.parent_path()
        );
    }

    zero_cpu::system::MultiProcessTraceJsonMetadata
        metadata;

    metadata.producer_version =
        "v1.3-actual";

    zero_cpu::system::
        MultiProcessTraceJsonWriter::writeFile(
            kActualPath,
            makeResult(1),
            metadata
        );

    const zero_cpu::TraceJsonDiffResult result =
        zero_cpu::TraceJsonDiff::compareFiles(
            kGoldenPath,
            kActualPath
        );

    if (!result.equal) {
        detail =
            result.message
            + "; expected="
            + result.expected_value
            + "; actual="
            + result.actual_value;
        return false;
    }

    return true;
}

// Patch: v1.3-multiprocess-structural-diff-golden-r1

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2) {
        const std::string command =
            argv[1];

        if (command == "--write-golden") {
            try {
                writeGolden();
                return 0;
            } catch (const std::exception& ex) {
                std::cerr
                    << "Failed to write golden trace: "
                    << ex.what()
                    << "\n";
                return 1;
            }
        }

        std::cerr
            << "Unknown option: "
            << command
            << "\n";
        return 2;
    }

    if (argc != 1) {
        std::cerr
            << "Usage: "
            << argv[0]
            << " [--write-golden]\n";
        return 2;
    }

    std::cout
        << "=== Zero-CPU Multi-Process Trace "
           "Diff Test ===\n\n";

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
            "Multi-process architectural diff",
            architecturalMetadataIgnored(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Multi-process strict metadata diff",
            strictMetadataDetected(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Scheduler regression detection",
            schedulerRegressionDetected(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Multi-process golden trace regression",
            goldenRegression(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Multi-process trace diff test "
               "finished successfully.\n";
        return 0;
    }

    std::cout
        << "Multi-process trace diff test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
