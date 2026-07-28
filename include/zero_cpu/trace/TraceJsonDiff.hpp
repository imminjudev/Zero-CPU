#pragma once

#include <cstddef>
#include <string>

namespace zero_cpu {

struct TraceJsonDiffOptions {
    bool strict = false;
};

struct TraceJsonDiffResult {
    bool equal = false;
    std::size_t difference_count = 0;

    std::string first_path;
    std::string expected_value;
    std::string actual_value;
    std::string message;
};

class TraceJsonDiff {
public:
    static TraceJsonDiffResult compareText(
        const std::string& expectedJson,
        const std::string& actualJson,
        const TraceJsonDiffOptions& options = {}
    );

    static TraceJsonDiffResult compareFiles(
        const std::string& expectedPath,
        const std::string& actualPath,
        const TraceJsonDiffOptions& options = {}
    );
};

} // namespace zero_cpu
