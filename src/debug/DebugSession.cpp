#include "zero_cpu/debug/DebugSession.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/kernel/ProcessAddressSpace.hpp"
#include "zero_cpu/kernel/ProcessContext.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace zero_cpu::debug {

const char* memoryWatchModeToString(
    MemoryWatchMode mode
) {
    switch (mode) {
    case MemoryWatchMode::Read:
        return "Read";

    case MemoryWatchMode::Write:
        return "Write";

    case MemoryWatchMode::Access:
        return "Access";
    }

    throw std::runtime_error(
        "Invalid memory watch mode"
    );
}

std::size_t MemoryWatchpoint::endExclusive() const {
    return address + size;
}

const char* debugStopReasonToString(DebugStopReason reason) {
    switch (reason) {
    case DebugStopReason::Ready:
        return "Ready";
    case DebugStopReason::StepComplete:
        return "StepComplete";
    case DebugStopReason::Breakpoint:
        return "Breakpoint";
    case DebugStopReason::Watchpoint:
        return "Watchpoint";
    case DebugStopReason::ProgramEnd:
        return "ProgramEnd";
    case DebugStopReason::Halted:
        return "Halted";
    case DebugStopReason::Fault:
        return "Fault";
    case DebugStopReason::StepLimit:
        return "StepLimit";
    }

    throw std::runtime_error("Invalid debugger stop reason");
}

bool DebugStop::stoppedAtBreakpoint() const {
    return reason == DebugStopReason::Breakpoint;
}

bool DebugStop::stoppedAtWatchpoint() const {
    return reason == DebugStopReason::Watchpoint;
}

bool DebugStop::faulted() const {
    return reason == DebugStopReason::Fault;
}

bool DebugStop::reachedProgramEnd() const {
    return reason == DebugStopReason::ProgramEnd;
}

DebugSession::DebugSession(const kernel::ProcessImage& image) {
    loadImage(image);
}

DebugSession::DebugSession(const std::string& path) {
    loadFile(path);
}

void DebugSession::loadImage(const kernel::ProcessImage& image) {
    kernel::validateProcessImage(image);

    CPU stagedCpu;
    kernel::ProcessAddressSpace addressSpace(image);
    addressSpace.activate(stagedCpu);

    kernel::ProcessContext context;
    context.pid = 1;
    context.registers = image.initial_registers;
    context.flags = image.initial_flags;
    context.pc = image.initial_pc;
    context.sp = image.initial_sp;
    context.privilege = image.initial_privilege;
    context.has_user_code_range = true;
    context.user_code_begin = image.metadata.code_base;
    context.user_code_end_exclusive = image.metadata.code_end_exclusive;
    context.user_stack_begin = image.metadata.user_stack_begin;
    context.user_stack_end_exclusive = image.metadata.user_stack_end_exclusive;
    context.kernel_stack_pointer = image.initial_kernel_stack_pointer;

    kernel::restoreProcessContext(context, stagedCpu);
    stagedCpu.traceLogger().clear();

    cpu_ = std::move(stagedCpu);
    loaded_ = true;
    source_name_ = image.metadata.source_name;
    metadata_ = image.metadata;
    breakpoints_.clear();
    watchpoints_.clear();
    next_watchpoint_id_ = 1;
    total_steps_ = 0;

    last_stop_ = DebugStop{};
    last_stop_.reason = DebugStopReason::Ready;
    last_stop_.pc = cpu_.state().pc();
}

void DebugSession::loadFile(const std::string& path) {
    kernel::ProcessImageLoader loader;
    loadImage(loader.loadFile(path));
}

bool DebugSession::loaded() const {
    return loaded_;
}

const std::string& DebugSession::sourceName() const {
    requireLoaded();
    return source_name_;
}

const kernel::ExecutableMetadata& DebugSession::metadata() const {
    requireLoaded();
    return metadata_;
}

const CPU& DebugSession::cpu() const {
    requireLoaded();
    return cpu_;
}

std::size_t DebugSession::totalSteps() const {
    requireLoaded();
    return total_steps_;
}

const DebugStop& DebugSession::lastStop() const {
    requireLoaded();
    return last_stop_;
}

bool DebugSession::addBreakpoint(std::size_t address) {
    requireLoaded();
    validateBreakpointAddress(address);
    return breakpoints_.insert(address).second;
}

bool DebugSession::removeBreakpoint(std::size_t address) {
    requireLoaded();
    return breakpoints_.erase(address) != 0;
}

bool DebugSession::hasBreakpoint(std::size_t address) const {
    requireLoaded();
    return breakpoints_.find(address) != breakpoints_.end();
}

void DebugSession::clearBreakpoints() {
    requireLoaded();
    breakpoints_.clear();
}

std::vector<std::size_t> DebugSession::breakpoints() const {
    requireLoaded();
    return std::vector<std::size_t>(
        breakpoints_.begin(),
        breakpoints_.end()
    );
}

std::size_t DebugSession::addWatchpoint(
    std::size_t address,
    std::size_t size,
    MemoryWatchMode mode
) {
    requireLoaded();

    validateWatchpointRange(
        address,
        size
    );

    for (
        const MemoryWatchpoint& existing :
        watchpoints_
    ) {
        if (
            existing.address == address
            && existing.size == size
            && existing.mode == mode
        ) {
            return existing.id;
        }
    }

    if (
        next_watchpoint_id_
        == std::numeric_limits<
            std::size_t
        >::max()
    ) {
        throw std::runtime_error(
            "Watchpoint ID space exhausted"
        );
    }

    MemoryWatchpoint watchpoint;
    watchpoint.id = next_watchpoint_id_;
    watchpoint.address = address;
    watchpoint.size = size;
    watchpoint.mode = mode;

    ++next_watchpoint_id_;

    watchpoints_.push_back(watchpoint);

    return watchpoint.id;
}

bool DebugSession::removeWatchpoint(
    std::size_t id
) {
    requireLoaded();

    for (
        auto iterator = watchpoints_.begin();
        iterator != watchpoints_.end();
        ++iterator
    ) {
        if (iterator->id == id) {
            watchpoints_.erase(iterator);
            return true;
        }
    }

    return false;
}

void DebugSession::clearWatchpoints() {
    requireLoaded();
    watchpoints_.clear();
}

std::vector<MemoryWatchpoint>
DebugSession::watchpoints() const {
    requireLoaded();
    return watchpoints_;
}

DebugStop DebugSession::step() {
    requireLoaded();

    const DebugStop stopped =
        classifyStoppedState(0);

    if (
        stopped.reason
        != DebugStopReason::Ready
    ) {
        return stopped;
    }

    cpu_.step();
    ++total_steps_;

    const DebugStop after =
        classifyStoppedState(1);

    if (
        after.reason == DebugStopReason::Fault
        || after.reason == DebugStopReason::Halted
    ) {
        return after;
    }

    WatchpointHit hit;

    if (findWatchpointHit(hit)) {
        return makeWatchpointStop(
            hit,
            1
        );
    }

    if (
        after.reason
        != DebugStopReason::Ready
    ) {
        return after;
    }

    return makeStop(
        DebugStopReason::StepComplete,
        1
    );
}

DebugStop DebugSession::continueExecution(std::size_t maxSteps) {
    requireLoaded();

    if (maxSteps == 0) {
        throw std::runtime_error(
            "Debugger continue step limit must be greater than zero"
        );
    }

    std::size_t executedSteps = 0;
    bool skipInitialBreakpoint =
        last_stop_.reason == DebugStopReason::Breakpoint
        && last_stop_.pc == cpu_.state().pc();

    while (true) {
        const DebugStop stopped = classifyStoppedState(executedSteps);
        if (stopped.reason != DebugStopReason::Ready) {
            return stopped;
        }

        const std::size_t pc = cpu_.state().pc();

        if (hasBreakpoint(pc) && !skipInitialBreakpoint) {
            return makeStop(
                DebugStopReason::Breakpoint,
                executedSteps
            );
        }

        if (executedSteps >= maxSteps) {
            return makeStop(
                DebugStopReason::StepLimit,
                executedSteps,
                "Debugger continue step limit reached"
            );
        }

        cpu_.step();
        ++executedSteps;
        ++total_steps_;
        skipInitialBreakpoint = false;

        const DebugStop after =
            classifyStoppedState(
                executedSteps
            );

        if (
            after.reason
                == DebugStopReason::Fault
            || after.reason
                == DebugStopReason::Halted
        ) {
            return after;
        }

        WatchpointHit hit;

        if (findWatchpointHit(hit)) {
            return makeWatchpointStop(
                hit,
                executedSteps
            );
        }

        if (
            after.reason
            != DebugStopReason::Ready
        ) {
            return after;
        }
    }
}

void DebugSession::requireLoaded() const {
    if (!loaded_) {
        throw std::runtime_error(
            "Debugger session has no loaded executable"
        );
    }
}

void DebugSession::validateBreakpointAddress(std::size_t address) const {
    if (
        address < metadata_.code_base
        || address >= metadata_.code_end_exclusive
    ) {
        throw std::runtime_error(
            "Breakpoint address is outside the executable code section"
        );
    }

    if (
        (address - metadata_.code_base)
            % binary::kInstructionSize != 0
    ) {
        throw std::runtime_error(
            "Breakpoint address is not instruction-aligned"
        );
    }
}

void DebugSession::validateWatchpointRange(
    std::size_t address,
    std::size_t size
) const {
    if (size == 0) {
        throw std::runtime_error(
            "Watchpoint size must be "
            "greater than zero"
        );
    }

    const std::size_t memorySize =
        cpu_.state().memory().size();

    if (
        address >= memorySize
        || size > memorySize - address
    ) {
        throw std::runtime_error(
            "Watchpoint range is outside "
            "process memory"
        );
    }
}

bool DebugSession::findWatchpointHit(
    WatchpointHit& hit
) const {
    if (
        watchpoints_.empty()
        || cpu_.traceLogger().empty()
    ) {
        return false;
    }

    const MemoryTraceDetail& detail =
        cpu_.traceLogger()
            .last()
            .memoryDetail();

    if (
        !detail.active
        || !detail.has_address
        || (
            !detail.is_read
            && !detail.is_write
        )
    ) {
        return false;
    }

    const MemoryWatchMode accessMode =
        detail.is_write
            ? MemoryWatchMode::Write
            : MemoryWatchMode::Read;

    constexpr std::size_t accessSize =
        sizeof(std::int64_t);

    const std::size_t accessBegin =
        detail.address;

    const std::size_t accessEnd =
        accessBegin + accessSize;

    for (
        const MemoryWatchpoint& watchpoint :
        watchpoints_
    ) {
        const bool modeMatches =
            watchpoint.mode
                == MemoryWatchMode::Access
            || watchpoint.mode
                == accessMode;

        const bool rangeMatches =
            watchpoint.address < accessEnd
            && accessBegin
                < watchpoint.endExclusive();

        if (
            modeMatches
            && rangeMatches
        ) {
            hit.watchpoint = watchpoint;
            hit.access_mode = accessMode;
            hit.access_address = accessBegin;
            return true;
        }
    }

    return false;
}

DebugStop DebugSession::makeWatchpointStop(
    const WatchpointHit& hit,
    std::size_t executedSteps
) {
    DebugStop stop = makeStop(
        DebugStopReason::Watchpoint,
        executedSteps,
        "Watchpoint "
            + std::to_string(
                hit.watchpoint.id
            )
            + " matched "
            + memoryWatchModeToString(
                hit.access_mode
            )
            + " at address "
            + std::to_string(
                hit.access_address
            )
    );

    stop.has_watchpoint = true;

    stop.watchpoint_id =
        hit.watchpoint.id;

    stop.watchpoint_mode =
        hit.watchpoint.mode;

    stop.watchpoint_address =
        hit.watchpoint.address;

    stop.watchpoint_size =
        hit.watchpoint.size;

    stop.access_mode =
        hit.access_mode;

    stop.access_address =
        hit.access_address;

    last_stop_ = stop;
    return last_stop_;
}

bool DebugSession::atProgramEnd() const {
    return cpu_.state().pc() == metadata_.code_end_exclusive;
}

DebugStop DebugSession::makeStop(
    DebugStopReason reason,
    std::size_t executedSteps,
    std::string message
) {
    last_stop_.reason = reason;
    last_stop_.pc = cpu_.state().pc();
    last_stop_.executed_steps = executedSteps;
    last_stop_.total_steps = total_steps_;
    last_stop_.message = std::move(message);

    last_stop_.has_watchpoint = false;
    last_stop_.watchpoint_id = 0;

    last_stop_.watchpoint_mode =
        MemoryWatchMode::Access;

    last_stop_.watchpoint_address = 0;
    last_stop_.watchpoint_size = 0;

    last_stop_.access_mode =
        MemoryWatchMode::Access;

    last_stop_.access_address = 0;

    return last_stop_;
}

DebugStop DebugSession::classifyStoppedState(
    std::size_t executedSteps
) {
    if (cpu_.state().hasError()) {
        return makeStop(
            DebugStopReason::Fault,
            executedSteps,
            cpu_.state().errorMessage()
        );
    }

    if (cpu_.state().halted()) {
        return makeStop(DebugStopReason::Halted, executedSteps);
    }

    if (atProgramEnd()) {
        return makeStop(DebugStopReason::ProgramEnd, executedSteps);
    }

    DebugStop ready;
    ready.reason = DebugStopReason::Ready;
    ready.pc = cpu_.state().pc();
    ready.executed_steps = executedSteps;
    ready.total_steps = total_steps_;
    return ready;
}

} // namespace zero_cpu::debug
