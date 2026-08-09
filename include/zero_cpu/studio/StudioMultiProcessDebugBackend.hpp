#pragma once

#include "zero_cpu/debug/DebugSnapshotJson.hpp"
#include "zero_cpu/debug/MultiProcessDebugSession.hpp"
#include "zero_cpu/kernel/ProcessImage.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace zero_cpu::studio {

class StudioMultiProcessDebugBackend {
public:
    void loadBinaries(
        const std::vector<std::string>& paths,
        const debug::MultiProcessDebugOptions& options = {}
    );

    void loadImages(
        const std::vector<kernel::ProcessImage>& images,
        const debug::MultiProcessDebugOptions& options = {}
    );

    void reset();
    bool loaded() const;

    debug::MultiProcessDebugSession& session();
    const debug::MultiProcessDebugSession& session() const;

    const CPU& cpu() const;

    kernel::ProcessId selectedPid() const;
    kernel::ProcessId runningPid() const;

    void selectProcess(kernel::ProcessId pid);

    std::size_t resolveCodeSymbol(
        const std::string& name
    ) const;

    std::size_t resolveDataSymbol(
        const std::string& name
    ) const;

    bool hasSourceMap() const;

    const std::string& sourcePath() const;

    std::size_t resolveSourceLine(
        std::size_t line
    ) const;

    std::size_t currentSourceLine() const;

    bool addBreakpoint(std::size_t address);
    void clearBreakpoints();

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
        debug::ProcessMemoryWatchMode mode
    );

    void clearWatchpoints();

    debug::MultiProcessDebugStop step();
    debug::MultiProcessDebugStop run(
        std::size_t maxSteps
    );

    void exportSnapshot(
        const std::string& path,
        const debug::DebugSnapshotOptions& options = {}
    ) const;

    std::string statusText() const;

private:
    std::unique_ptr<
        debug::MultiProcessDebugSession
    > session_;

    void requireLoaded() const;
};

} // namespace zero_cpu::studio
