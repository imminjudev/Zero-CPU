#pragma once

#include "zero_cpu/debug/MultiProcessDebugSession.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace zero_cpu::debug {

struct MultiProcessDebugConsoleOptions {
    std::size_t default_continue_steps =
        100000;

    bool show_prompt = true;
    bool print_banner = true;
};

struct MultiProcessDebugConsoleResult {
    std::size_t command_count = 0;
    std::size_t command_error_count = 0;

    bool quit_requested = false;
    bool eof_reached = false;

    bool success() const;
};

class MultiProcessDebugConsole {
public:
    MultiProcessDebugConsole(
        MultiProcessDebugSession& session,
        std::istream& input,
        std::ostream& output,
        std::ostream& error,
        MultiProcessDebugConsoleOptions options = {}
    );

    MultiProcessDebugConsoleResult run();

private:
    MultiProcessDebugSession& session_;

    std::istream& input_;
    std::ostream& output_;
    std::ostream& error_;

    MultiProcessDebugConsoleOptions options_;

    bool executeCommand(
        const std::string& line
    );

    void printBanner();
    void printHelp();

    void printStop(
        const MultiProcessDebugStop& stop
    );

    void printProcesses();
    void printSelectedProcess();

    void printSnapshot(
        const ProcessDebugSnapshot& snapshot
    );

    void printSymbols(
        kernel::ProcessId pid
    );

    void printBreakpoints(
        const std::vector<ProcessBreakpoint>& values
    );

    void printConditionalBreakpoints(
        const std::vector<
            ProcessConditionalBreakpoint
        >& values
    );

    void printWatchpoints(
        const std::vector<
            ProcessMemoryWatchpoint
        >& values
    );

    void printScheduler();
    void printRegisters();

    void printMemory(
        std::size_t address,
        std::size_t count
    );

    void printTrace();

    static ProcessMemoryWatchMode
    parseWatchMode(
        const std::string& text
    );

    static kernel::ProcessId parsePid(
        const std::string& text
    );

    static std::size_t parseAddress(
        const std::string& text
    );

    static std::size_t parsePositiveCount(
        const std::string& text,
        const std::string& context
    );
};

} // namespace zero_cpu::debug
