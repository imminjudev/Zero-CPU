#pragma once

#include <cstdint>
#include <string>

namespace zero_cpu {

enum class PrivilegeLevel {
    Kernel,
    User
};

std::string privilegeLevelToString(PrivilegeLevel level);

std::int64_t privilegeLevelToRaw(PrivilegeLevel level);
PrivilegeLevel privilegeLevelFromRaw(std::int64_t value);

} // namespace zero_cpu
