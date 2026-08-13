#pragma once

#include "zero_cpu/system/MultiProcessRunner.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace zero_cpu::system {

struct EndToEndShowcaseOptions {
    std::string filesystem_source_path =
        "examples/showcase_fs_worker.zasm";

    std::string hardware_source_path =
        "examples/showcase_hardware_fault.zasm";

    std::string output_directory =
        "build/showcase";

    std::string golden_trace_path =
        "tests/golden/end_to_end_showcase.json";

    bool write_golden = false;
};

struct EndToEndShowcaseResult {
    MultiProcessRunResult runtime;

    std::string filesystem_binary_path;
    std::string hardware_binary_path;
    std::string trace_json_path;
    std::string golden_trace_path;

    std::string filesystem_before;
    std::string filesystem_after;

    std::int64_t gpio_output = 0;
    std::size_t hardware_write_count = 0;

    std::size_t fault_handoff_step = 0;
    bool survivor_ran_after_fault = false;

    bool observed_filesystem_read = false;
    bool observed_filesystem_write = false;
    bool observed_hardware_write = false;
    bool observed_process_exit = false;

    bool invariants_passed = false;
    bool golden_verified = false;
    bool golden_written = false;

    std::int64_t survivor_exit_code = 0;
    std::string fault_message;

    bool passed = false;
    std::string detail;

    bool success() const {
        return passed;
    }
};

class EndToEndShowcaseRunner {
public:
    EndToEndShowcaseResult run(
        const EndToEndShowcaseOptions& options = {}
    ) const;
};

} // namespace zero_cpu::system
