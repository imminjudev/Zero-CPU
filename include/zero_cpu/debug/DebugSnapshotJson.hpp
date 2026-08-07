#pragma once

#include "zero_cpu/debug/DebugSession.hpp"
#include "zero_cpu/debug/MultiProcessDebugSession.hpp"

#include <cstddef>
#include <string>

namespace zero_cpu::debug {

struct DebugSnapshotOptions {
    bool include_memory = true;
    std::size_t memory_address = 0;
    std::size_t memory_size = 64;

    bool include_symbols = true;
    bool include_last_trace = true;
};

class DebugSnapshotJsonWriter {
public:
    static constexpr std::size_t
        kSchemaVersion = 1;

    static std::string toJson(
        const DebugSession& session,
        const DebugSnapshotOptions& options = {}
    );

    static std::string toJson(
        const MultiProcessDebugSession& session,
        const DebugSnapshotOptions& options = {}
    );

    static void writeFile(
        const std::string& path,
        const DebugSession& session,
        const DebugSnapshotOptions& options = {}
    );

    static void writeFile(
        const std::string& path,
        const MultiProcessDebugSession& session,
        const DebugSnapshotOptions& options = {}
    );
};

} // namespace zero_cpu::debug
