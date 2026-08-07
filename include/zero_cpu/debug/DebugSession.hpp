#pragma once

#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/kernel/ExecutableMetadata.hpp"
#include "zero_cpu/kernel/ProcessImage.hpp"

#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace zero_cpu::debug {

enum class MemoryWatchMode {
    Read,
    Write,
    Access
};

const char* memoryWatchModeToString(
    MemoryWatchMode mode
);

struct MemoryWatchpoint {
    std::size_t id = 0;
    std::size_t address = 0;
    std::size_t size = 1;

    MemoryWatchMode mode =
        MemoryWatchMode::Access;

    std::size_t endExclusive() const;
};

enum class DebugStopReason {
    Ready,
    StepComplete,
    Breakpoint,
    Watchpoint,
    ProgramEnd,
    Halted,
    Fault,
    StepLimit
};

const char* debugStopReasonToString(
    DebugStopReason reason
);

struct DebugStop {
    DebugStopReason reason = DebugStopReason::Ready;
    std::size_t pc = 0;
    std::size_t executed_steps = 0;
    std::size_t total_steps = 0;
    std::string message;

    bool has_watchpoint = false;
    std::size_t watchpoint_id = 0;

    MemoryWatchMode watchpoint_mode =
        MemoryWatchMode::Access;

    std::size_t watchpoint_address = 0;
    std::size_t watchpoint_size = 0;

    MemoryWatchMode access_mode =
        MemoryWatchMode::Access;

    std::size_t access_address = 0;

    bool stoppedAtBreakpoint() const;
    bool stoppedAtWatchpoint() const;
    bool faulted() const;
    bool reachedProgramEnd() const;
};

class DebugSession {
public:
    DebugSession() = default;
    explicit DebugSession(const kernel::ProcessImage& image);
    explicit DebugSession(const std::string& path);

    void loadImage(const kernel::ProcessImage& image);
    void loadFile(const std::string& path);

    bool loaded() const;
    const std::string& sourceName() const;
    const kernel::ExecutableMetadata& metadata() const;
    const CPU& cpu() const;
    std::size_t totalSteps() const;
    const DebugStop& lastStop() const;

    bool addBreakpoint(std::size_t address);
    bool removeBreakpoint(std::size_t address);
    bool hasBreakpoint(std::size_t address) const;
    void clearBreakpoints();
    std::vector<std::size_t> breakpoints() const;

    std::size_t addWatchpoint(
        std::size_t address,
        std::size_t size,
        MemoryWatchMode mode
    );

    bool removeWatchpoint(
        std::size_t id
    );

    void clearWatchpoints();

    std::vector<MemoryWatchpoint>
    watchpoints() const;

    DebugStop step();
    DebugStop continueExecution(
        std::size_t maxSteps = CPU::kDefaultMaxSteps
    );

private:
    CPU cpu_;
    bool loaded_ = false;
    std::string source_name_;
    kernel::ExecutableMetadata metadata_;
    std::set<std::size_t> breakpoints_;

    std::vector<MemoryWatchpoint>
        watchpoints_;

    std::size_t next_watchpoint_id_ = 1;

    std::size_t total_steps_ = 0;
    DebugStop last_stop_;

    struct WatchpointHit {
        MemoryWatchpoint watchpoint;

        MemoryWatchMode access_mode =
            MemoryWatchMode::Access;

        std::size_t access_address = 0;
    };

    void requireLoaded() const;
    void validateBreakpointAddress(std::size_t address) const;

    void validateWatchpointRange(
        std::size_t address,
        std::size_t size
    ) const;

    bool findWatchpointHit(
        WatchpointHit& hit
    ) const;

    DebugStop makeWatchpointStop(
        const WatchpointHit& hit,
        std::size_t executedSteps
    );

    bool atProgramEnd() const;

    DebugStop makeStop(
        DebugStopReason reason,
        std::size_t executedSteps,
        std::string message = {}
    );

    DebugStop classifyStoppedState(
        std::size_t executedSteps
    );
};

} // namespace zero_cpu::debug
