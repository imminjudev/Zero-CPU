#pragma once

#include "zero_cpu/system/MultiProcessRunner.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace zero_cpu::system {

struct MultiProcessInvariantViolation {
    std::string code;
    std::string message;
    std::size_t lifecycle_step = 0;
    kernel::ProcessId pid = 0;
};

struct MultiProcessInvariantReport {
    std::vector<MultiProcessInvariantViolation> violations;

    bool passed() const;
    std::size_t violationCount() const;
};

class MultiProcessInvariantVerifier {
public:
    static MultiProcessInvariantReport verify(
        const MultiProcessRunResult& result
    );
};

} // namespace zero_cpu::system
