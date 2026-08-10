#pragma once

#include "zero_cpu/system/MultiProcessInvariantVerifier.hpp"
#include "zero_cpu/system/MultiProcessRunner.hpp"

#include <cstddef>
#include <string>

namespace zero_cpu::system {

struct MultiProcessTraceJsonMetadata {
    std::string producer = "zero_cpu";
    std::string producer_version;
    std::string execution_mode =
        "ProtectedMultiProcess";
};

class MultiProcessTraceJsonWriter {
public:
    inline static constexpr std::size_t
        kSchemaVersion = 1;

    static std::string toJson(
        const MultiProcessRunResult& result,
        const MultiProcessTraceJsonMetadata& metadata = {}
    );

    static void writeFile(
        const std::string& path,
        const MultiProcessRunResult& result,
        const MultiProcessTraceJsonMetadata& metadata = {}
    );
};

} // namespace zero_cpu::system
