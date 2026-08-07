#pragma once

#include "zero_cpu/debug/DebugSession.hpp"
#include "zero_cpu/debug/DebugSnapshotJson.hpp"
#include "zero_cpu/kernel/ProcessImage.hpp"

#include <cstddef>
#include <string>

namespace zero_cpu::studio {

class StudioDebugBackend {
public:
    void loadBinary(
        const std::string& path
    );

    void loadImage(
        const kernel::ProcessImage& image
    );

    void reset();

    bool loaded() const;

    debug::DebugSession& session();
    const debug::DebugSession& session() const;

    CPU& cpu();
    const CPU& cpu() const;

    bool addBreakpoint(
        std::size_t address
    );

    bool removeBreakpoint(
        std::size_t address
    );

    void clearBreakpoints();

    std::size_t resolveCodeSymbol(
        const std::string& name
    ) const;

    std::size_t resolveDataSymbol(
        const std::string& name
    ) const;

    std::size_t addConditionalBreakpoint(
        std::size_t address,
        const std::string& source,
        const std::string& operation,
        const std::string& value
    );

    void clearConditionalBreakpoints();

    std::size_t addWatchpoint(
        std::size_t address,
        std::size_t size,
        debug::MemoryWatchMode mode
    );

    void clearWatchpoints();

    debug::DebugStop step();

    debug::DebugStop run(
        std::size_t maxSteps
    );

    void exportSnapshot(
        const std::string& path,
        const debug::DebugSnapshotOptions& options = {}
    ) const;

    std::string statusText() const;

private:
    debug::DebugSession session_;

    void requireLoaded() const;
};

} // namespace zero_cpu::studio
