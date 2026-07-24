#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zero_cpu::system {

struct BioOSRunOptions {
    std::string directory = "examples\\bio_os";
    std::string combined_source_path;
    std::string combined_binary_path;
    std::size_t max_steps = 5000;
    bool write_generated_files = true;
};

struct BioOSRunResult {
    std::string directory;
    std::string combined_source_path;
    std::string combined_binary_path;
    std::string combined_source;

    std::size_t instruction_count = 0;
    std::size_t code_size = 0;
    std::size_t syscall_handler_pc = 0;
    std::size_t timer_handler_pc = 0;
    std::size_t stack_base = 0;
    std::size_t step_count = 0;

    bool halted = false;
    bool hit_step_limit = false;
    bool has_error = false;
    std::string error_message;

    std::size_t final_pc = 0;
    std::size_t final_sp = 0;
    std::int64_t exit_code = 0;

    std::vector<std::int64_t> debug_writes;
    std::string debug_ascii;

    std::int64_t timer_tick_count = 0;
    std::int64_t timer_interval = 0;
    int timer_vector = 0;
    std::int64_t timer_payload = 0;
    std::int64_t timer_interrupt_count = 0;
    bool timer_enabled = false;

    bool success() const {
        return halted && !has_error && !hit_step_limit;
    }
};

std::string bioOSDebugOutputAsAscii(
    const std::vector<std::int64_t>& writes
);

class BioOSRunner {
public:
    BioOSRunResult run(const BioOSRunOptions& options = {}) const;
};

} // namespace zero_cpu::system