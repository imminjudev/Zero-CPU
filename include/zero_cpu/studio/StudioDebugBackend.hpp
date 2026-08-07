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
