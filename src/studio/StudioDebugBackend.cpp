#include "zero_cpu/studio/StudioDebugBackend.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

namespace zero_cpu::studio {

void StudioDebugBackend::loadBinary(
    const std::string& path
) {
    if (path.empty()) {
        throw std::runtime_error(
            "Studio binary path must not be empty"
        );
    }

    session_.loadFile(path);
}

void StudioDebugBackend::loadImage(
    const kernel::ProcessImage& image
) {
    session_.loadImage(image);
}

void StudioDebugBackend::reset() {
    session_ = debug::DebugSession{};
}

bool StudioDebugBackend::loaded() const {
    return session_.loaded();
}

debug::DebugSession&
StudioDebugBackend::session() {
    requireLoaded();
    return session_;
}

const debug::DebugSession&
StudioDebugBackend::session() const {
    requireLoaded();
    return session_;
}

CPU& StudioDebugBackend::cpu() {
    requireLoaded();
    return session_.cpu();
}

const CPU& StudioDebugBackend::cpu() const {
    requireLoaded();
    return session_.cpu();
}

bool StudioDebugBackend::addBreakpoint(
    std::size_t address
) {
    requireLoaded();

    return session_.addBreakpoint(
        address
    );
}

bool StudioDebugBackend::removeBreakpoint(
    std::size_t address
) {
    requireLoaded();

    return session_.removeBreakpoint(
        address
    );
}

void StudioDebugBackend::clearBreakpoints() {
    requireLoaded();
    session_.clearBreakpoints();
}

debug::DebugStop StudioDebugBackend::step() {
    requireLoaded();
    return session_.step();
}

debug::DebugStop StudioDebugBackend::run(
    std::size_t maxSteps
) {
    requireLoaded();

    if (maxSteps == 0) {
        throw std::runtime_error(
            "Studio debugger run limit must be "
            "greater than zero"
        );
    }

    return session_.continueExecution(
        maxSteps
    );
}

void StudioDebugBackend::exportSnapshot(
    const std::string& path,
    const debug::DebugSnapshotOptions& options
) const {
    requireLoaded();

    debug::DebugSnapshotJsonWriter::writeFile(
        path,
        session_,
        options
    );
}

std::string StudioDebugBackend::statusText() const {
    requireLoaded();

    const debug::DebugStop& stop =
        session_.lastStop();

    std::ostringstream output;

    output
        << "Binary Debug Session\n"
        << "Stop Reason = "
        << debug::debugStopReasonToString(
            stop.reason
        )
        << "\n"
        << "Stop PC = "
        << stop.pc
        << "\n"
        << "Executed Steps = "
        << stop.executed_steps
        << "\n"
        << "Total Steps = "
        << stop.total_steps
        << "\n"
        << "Breakpoints = "
        << session_.breakpoints().size()
        << "\n"
        << "Conditional Breakpoints = "
        << session_
            .conditionalBreakpoints()
            .size()
        << "\n"
        << "Watchpoints = "
        << session_.watchpoints().size()
        << "\n"
        << "Symbols Loaded = "
        << (
            session_.hasSymbols()
                ? "true"
                : "false"
        )
        << "\n";

    if (session_.hasSymbols()) {
        output
            << "Symbol Count = "
            << session_.symbols().size()
            << "\n";
    }

    if (!stop.message.empty()) {
        output
            << "Stop Message = "
            << stop.message
            << "\n";
    }

    return output.str();
}

void StudioDebugBackend::requireLoaded() const {
    if (!session_.loaded()) {
        throw std::runtime_error(
            "Studio debugger has no loaded binary"
        );
    }
}

} // namespace zero_cpu::studio
