#include "zero_cpu/studio/StudioMultiProcessDebugBackend.hpp"

#include "zero_cpu/debug/DebugCondition.hpp"
#include "zero_cpu/kernel/ProcessLifecycleManager.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

namespace zero_cpu::studio {

void StudioMultiProcessDebugBackend::loadBinaries(
    const std::vector<std::string>& paths,
    const debug::MultiProcessDebugOptions& options
) {
    if (paths.empty()) {
        throw std::runtime_error(
            "Studio multi-process debugger requires at least one binary"
        );
    }

    for (const auto& path : paths) {
        if (path.empty()) {
            throw std::runtime_error(
                "Studio multi-process binary path must not be empty"
            );
        }
    }

    session_ =
        std::make_unique<
            debug::MultiProcessDebugSession
        >(paths, options);
}

void StudioMultiProcessDebugBackend::loadImages(
    const std::vector<kernel::ProcessImage>& images,
    const debug::MultiProcessDebugOptions& options
) {
    if (images.empty()) {
        throw std::runtime_error(
            "Studio multi-process debugger requires at least one image"
        );
    }

    session_ =
        std::make_unique<
            debug::MultiProcessDebugSession
        >(images, options);
}

void StudioMultiProcessDebugBackend::reset() {
    session_.reset();
}

bool StudioMultiProcessDebugBackend::loaded() const {
    return session_ && session_->started();
}

debug::MultiProcessDebugSession&
StudioMultiProcessDebugBackend::session() {
    requireLoaded();
    return *session_;
}

const debug::MultiProcessDebugSession&
StudioMultiProcessDebugBackend::session() const {
    requireLoaded();
    return *session_;
}

const CPU&
StudioMultiProcessDebugBackend::cpu() const {
    requireLoaded();
    return session_->cpu();
}

kernel::ProcessId
StudioMultiProcessDebugBackend::selectedPid() const {
    requireLoaded();
    return session_->selectedPid();
}

kernel::ProcessId
StudioMultiProcessDebugBackend::runningPid() const {
    requireLoaded();
    return session_->runningPid();
}

void StudioMultiProcessDebugBackend::selectProcess(
    kernel::ProcessId pid
) {
    requireLoaded();
    session_->selectProcess(pid);
}

std::size_t
StudioMultiProcessDebugBackend::resolveCodeSymbol(
    const std::string& name
) const {
    requireLoaded();
    return session_->resolveCodeSymbol(
        session_->selectedPid(),
        name
    );
}

std::size_t
StudioMultiProcessDebugBackend::resolveDataSymbol(
    const std::string& name
) const {
    requireLoaded();
    return session_->resolveDataSymbol(
        session_->selectedPid(),
        name
    );
}

bool StudioMultiProcessDebugBackend::addBreakpoint(
    std::size_t address
) {
    requireLoaded();
    return session_->addBreakpoint(
        session_->selectedPid(),
        address
    );
}

void StudioMultiProcessDebugBackend::clearBreakpoints() {
    requireLoaded();
    session_->clearBreakpoints(
        session_->selectedPid()
    );
}

std::size_t
StudioMultiProcessDebugBackend::addConditionalBreakpoint(
    std::size_t address,
    const std::string& source,
    const std::string& operation,
    const std::string& value
) {
    requireLoaded();

    return session_->addConditionalBreakpoint(
        session_->selectedPid(),
        address,
        debug::parseDebugCondition(
            source,
            operation,
            value
        )
    );
}

void
StudioMultiProcessDebugBackend::
clearConditionalBreakpoints() {
    requireLoaded();
    session_->clearConditionalBreakpoints(
        session_->selectedPid()
    );
}

std::size_t
StudioMultiProcessDebugBackend::addWatchpoint(
    std::size_t address,
    std::size_t size,
    debug::ProcessMemoryWatchMode mode
) {
    requireLoaded();

    return session_->addWatchpoint(
        session_->selectedPid(),
        address,
        size,
        mode
    );
}

void StudioMultiProcessDebugBackend::clearWatchpoints() {
    requireLoaded();
    session_->clearWatchpoints(
        session_->selectedPid()
    );
}

debug::MultiProcessDebugStop
StudioMultiProcessDebugBackend::step() {
    requireLoaded();
    return session_->step();
}

debug::MultiProcessDebugStop
StudioMultiProcessDebugBackend::run(
    std::size_t maxSteps
) {
    requireLoaded();

    if (maxSteps == 0) {
        throw std::runtime_error(
            "Studio multi-process run limit must be greater than zero"
        );
    }

    return session_->continueExecution(maxSteps);
}

void StudioMultiProcessDebugBackend::exportSnapshot(
    const std::string& path,
    const debug::DebugSnapshotOptions& options
) const {
    requireLoaded();

    debug::DebugSnapshotJsonWriter::writeFile(
        path,
        *session_,
        options
    );
}

std::string
StudioMultiProcessDebugBackend::statusText() const {
    requireLoaded();

    const auto& stop = session_->lastStop();
    const auto selected = session_->selectedPid();

    std::ostringstream out;

    out
        << "Multi-Process Debug Session\n"
        << "Runtime State = "
        << kernel::processRuntimeStateToString(
            session_->runtimeState()
        )
        << "\n"
        << "Running PID = "
        << session_->runningPid()
        << "\n"
        << "Selected PID = "
        << selected
        << "\n"
        << "Total Steps = "
        << session_->totalSteps()
        << "\n"
        << "Quantum = "
        << session_->quantum()
        << "\n"
        << "Preemptions = "
        << session_->preemptionCount()
        << "\n"
        << "Context Switches = "
        << session_->schedulerContextSwitchCount()
        << "\n"
        << "Last Stop = "
        << debug::multiProcessDebugStopReasonToString(
            stop.reason
        )
        << "\n";

    if (stop.has_debug_hit) {
        out
            << "Hit PID = "
            << stop.hit_pid
            << "\n"
            << "Hit Address = "
            << stop.hit_address
            << "\n";
    }

    if (!stop.message.empty()) {
        out
            << "Stop Message = "
            << stop.message
            << "\n";
    }

    out << "\nProcesses\n";

    for (const auto& snapshot : session_->processSnapshots()) {
        out
            << "PID "
            << snapshot.pid
            << " | "
            << snapshot.source_name
            << " | State="
            << kernel::processStateToString(snapshot.state)
            << " | PC="
            << snapshot.context.pc
            << " | SP="
            << snapshot.context.sp;

        if (snapshot.pid == selected) {
            out << " | <selected>";
        }

        if (snapshot.running) {
            out << " | <running>";
        }

        if (snapshot.has_exit_code) {
            out
                << " | Exit="
                << snapshot.exit_code;
        }

        if (snapshot.faulted()) {
            out << " | <faulted>";
        }

        out << "\n";
    }

    out
        << "\nSelected PID Debug Controls\n"
        << "Breakpoints = "
        << session_->breakpoints(selected).size()
        << "\n"
        << "Conditional Breakpoints = "
        << session_->conditionalBreakpoints(selected).size()
        << "\n"
        << "Watchpoints = "
        << session_->watchpoints(selected).size()
        << "\n"
        << "Symbols Loaded = "
        << (
            session_->hasSymbols(selected)
                ? "true"
                : "false"
        )
        << "\n";

    if (session_->hasSymbols(selected)) {
        out
            << "Symbol Count = "
            << session_->symbols(selected).size()
            << "\n";
    }

    out << "\nRecent Context Switches\n";

    const auto& switches = session_->contextSwitches();

    if (switches.empty()) {
        out << "(none)\n";
    } else {
        constexpr std::size_t visible = 8;

        const std::size_t begin =
            switches.size() > visible
                ? switches.size() - visible
                : 0;

        for (
            std::size_t i = begin;
            i < switches.size();
            ++i
        ) {
            const auto& record = switches[i];

            out
                << "#"
                << record.lifecycle_step
                << " "
                << record.from_pid
                << " -> "
                << record.to_pid;

            if (record.preempted) {
                out << " preempted";
            }

            if (record.caused_by_termination) {
                out << " termination";
            }

            out << "\n";
        }
    }

    return out.str();
}

void
StudioMultiProcessDebugBackend::requireLoaded() const {
    if (!loaded()) {
        throw std::runtime_error(
            "Studio multi-process debugger has no loaded runtime"
        );
    }
}

} // namespace zero_cpu::studio
