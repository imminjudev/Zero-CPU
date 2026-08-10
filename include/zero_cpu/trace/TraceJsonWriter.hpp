#pragma once

#include "zero_cpu/trace/TraceEvent.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace zero_cpu {

struct TraceJsonMetadata {
    std::string producer = "zero_cpu";
    std::string producer_version;
    std::string execution_mode;
    std::string loaded_path;
};

class TraceJsonWriter {
public:
    inline static constexpr std::size_t kSchemaVersion = 3;

    static std::string eventToJson(
        const TraceEvent& event,
        std::size_t index = 0
    );

    static std::string toJson(
        const std::vector<TraceEvent>& events,
        const TraceJsonMetadata& metadata = {}
    );

    static void writeFile(
        const std::string& path,
        const std::vector<TraceEvent>& events,
        const TraceJsonMetadata& metadata = {}
    );
};

} // namespace zero_cpu
