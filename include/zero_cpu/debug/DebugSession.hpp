#pragma once

#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/kernel/ExecutableMetadata.hpp"
#include "zero_cpu/kernel/ProcessImage.hpp"

#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace zero_cpu::debug {

enum class DebugStopReason {
    Ready,
    StepComplete,
    Breakpoint,
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

    bool stoppedAtBreakpoint() const;
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
    std::size_t total_steps_ = 0;
    DebugStop last_stop_;

    void requireLoaded() const;
    void validateBreakpointAddress(std::size_t address) const;
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
