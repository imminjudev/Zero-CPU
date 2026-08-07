#pragma once

#include "zero_cpu/debug/DebugSession.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>

namespace zero_cpu::debug {

struct DebugConsoleOptions {
    std::size_t default_continue_steps =
        CPU::kDefaultMaxSteps;
    bool show_prompt = true;
    bool print_banner = true;
};

struct DebugConsoleRunResult {
    std::size_t command_count = 0;
    std::size_t command_error_count = 0;
    bool quit_requested = false;
    bool eof_reached = false;

    bool success() const;
};

class DebugConsole {
public:
    DebugConsole(
        DebugSession& session,
        std::istream& input,
        std::ostream& output,
        std::ostream& error,
        DebugConsoleOptions options = {}
    );

    DebugConsoleRunResult run();

private:
    DebugSession& session_;
    std::istream& input_;
    std::ostream& output_;
    std::ostream& error_;
    DebugConsoleOptions options_;

    bool executeCommand(const std::string& line);

    void printBanner();
    void printHelp();
    void printStatus(const DebugStop& stop);
    void printBreakpoints();
    void printLastTrace();

    static std::size_t parseAddress(
        const std::string& text
    );

    static std::size_t parsePositiveCount(
        const std::string& text,
        const std::string& context
    );
};

} // namespace zero_cpu::debug
