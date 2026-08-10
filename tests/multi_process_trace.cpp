#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"
#include "zero_cpu/system/MultiProcessInvariantVerifier.hpp"
#include "zero_cpu/system/MultiProcessRunner.hpp"
#include "zero_cpu/system/MultiProcessTraceJsonWriter.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
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

bool hasViolation(
    const zero_cpu::system::
        MultiProcessInvariantReport& report,
    const std::string& code
) {
    for (
        const zero_cpu::system::
            MultiProcessInvariantViolation&
                violation : report.violations
    ) {
        if (violation.code == code) {
            return true;
        }
    }

    return false;
}

zero_cpu::system::MultiProcessRunResult
makeResult() {
    const char* firstSource = R"ASM(
.entry start
.text
start:
    MOV R0, 1
    ADD R0, 2
    MOV R1, R0
)ASM";

    const char* secondSource = R"ASM(
.entry start
.text
start:
    MOV R0, 10
    ADD R0, 5
    MOV R1, R0
)ASM";

    zero_cpu::system::MultiProcessRunOptions options;
    options.quantum = 1;
    options.max_lifecycle_steps = 100;

    zero_cpu::system::MultiProcessRunner runner;

    return runner.runImages(
        {
            makeImage(
                firstSource,
                "trace-first.zbin"
            ),
            makeImage(
                secondSource,
                "trace-second.zbin"
            )
        },
        options
    );
}

bool validInvariantReport(
    std::string& detail
) {
    using namespace zero_cpu::system;

    const MultiProcessRunResult result =
        makeResult();

    const MultiProcessInvariantReport report =
        MultiProcessInvariantVerifier::verify(
            result
        );

    if (
        !report.passed()
        || report.violationCount() != 0
        || !result.completed()
        || result.execution_trace.empty()
        || result.context_switches.empty()
    ) {
        detail =
            "valid multi-process timeline failed invariants";
        return false;
    }

    return true;
}

bool jsonExport(
    std::string& detail
) {
    using namespace zero_cpu::system;

    const MultiProcessRunResult result =
        makeResult();

    MultiProcessTraceJsonMetadata metadata;
    metadata.producer_version = "v1.3-test";

    const std::string json =
        MultiProcessTraceJsonWriter::toJson(
            result,
            metadata
        );

    const std::string requiredFragments[] = {
        "\"schema\": \"zero_cpu_multiprocess_trace\"",
        "\"schema_version\": 1",
        "\"mode\": \"ProtectedMultiProcess\"",
        "\"runtime_state\": \"Completed\"",
        "\"passed\": true",
        "\"violation_count\": 0",
        "\"execution_trace\": [",
        "\"context_switches\": [",
        "\"lifecycle_step\": 1",
        "\"pid\": 1",
        "\"event\": {",
        "\"state_before\": {",
        "\"state_after\": {",
        "\"processes\": ["
    };

    for (
        const std::string& fragment :
            requiredFragments
    ) {
        if (
            json.find(fragment)
            == std::string::npos
        ) {
            detail =
                "JSON output is missing fragment: "
                + fragment;
            return false;
        }
    }

    const std::string path =
        "multi_process_trace_test.json";

    struct Cleanup {
        std::string path;

        ~Cleanup() {
            std::remove(
                path.c_str()
            );
        }
    } cleanup{path};

    MultiProcessTraceJsonWriter::writeFile(
        path,
        result,
        metadata
    );

    std::ifstream file(
        path,
        std::ios::binary
    );

    if (!file) {
        detail =
            "multi-process JSON file was not created";
        return false;
    }

    std::ostringstream contents;
    contents << file.rdbuf();

    if (contents.str() != json) {
        detail =
            "multi-process JSON file output mismatch";
        return false;
    }

    return true;
}

bool detectsCorruptedTimeline(
    std::string& detail
) {
    using namespace zero_cpu::system;

    MultiProcessRunResult corrupted =
        makeResult();

    if (corrupted.execution_trace.size() < 2) {
        detail =
            "test requires at least two execution events";
        return false;
    }

    corrupted.execution_trace[1].pid = 1;

    const MultiProcessInvariantReport report =
        MultiProcessInvariantVerifier::verify(
            corrupted
        );

    if (
        report.passed()
        || !hasViolation(
            report,
            "execution_pid_mismatch"
        )
    ) {
        detail =
            "corrupted PID timeline was not detected";
        return false;
    }

    const std::string json =
        MultiProcessTraceJsonWriter::toJson(
            corrupted
        );

    if (
        json.find("\"passed\": false")
            == std::string::npos
        || json.find(
            "\"code\": \"execution_pid_mismatch\""
        ) == std::string::npos
    ) {
        detail =
            "failed invariant report was not serialized";
        return false;
    }

    return true;
}

// Patch: v1.3-multiprocess-json-invariants-r1

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Multi-Process Trace Test ===\n\n";

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
            "Multi-process invariant verification",
            validInvariantReport(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Multi-process JSON trace export",
            jsonExport(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Corrupted timeline invariant detection",
            detectsCorruptedTimeline(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Multi-process trace test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Multi-process trace test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
