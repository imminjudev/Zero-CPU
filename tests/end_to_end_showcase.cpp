#include "zero_cpu/system/EndToEndShowcase.hpp"

#include <iostream>
#include <string>

namespace {

bool runShowcase(
    std::string& detail,
    bool writeGolden
) {
    using namespace zero_cpu::system;

    EndToEndShowcaseOptions options;
    options.write_golden = writeGolden;

    EndToEndShowcaseRunner runner;

    const EndToEndShowcaseResult result =
        runner.run(options);

    if (!result.success()) {
        detail = result.detail;
        return false;
    }

    if (
        result.filesystem_before != "HELLO"
        || result.filesystem_after
            != "HELLOHELLO"
        || result.gpio_output != 42
        || result.hardware_write_count != 1
        || result.runtime.process_count != 2
        || result.runtime.fault_count != 1
        || result.survivor_exit_code != 0
        || result.fault_handoff_step == 0
        || !result.survivor_ran_after_fault
        || !result.observed_filesystem_read
        || !result.observed_filesystem_write
        || !result.observed_hardware_write
        || !result.observed_process_exit
        || !result.invariants_passed
        || !result.golden_verified
    ) {
        detail =
            "showcase runner summary fields mismatch";
        return false;
    }

    if (writeGolden) {
        std::cout
            << "Golden trace written:\n"
            << "  "
            << result.golden_trace_path
            << "\n";
    }

    std::cout
        << "Artifacts:\n"
        << "  "
        << result.filesystem_binary_path
        << "\n"
        << "  "
        << result.hardware_binary_path
        << "\n"
        << "  "
        << result.trace_json_path
        << "\n";

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
        std::cout
            << "       "
            << detail
            << "\n";
        return 1;
    }

    std::cout
        << "\nEnd-to-end showcase test finished successfully.\n";

    return 0;
}

// Patch: v1.9-end-to-end-showcase-r1
// Patch: v1.9-showcase-golden-regression-r1
// Patch: v2.0-single-command-showcase-r1
