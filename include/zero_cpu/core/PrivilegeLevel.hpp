#pragma once

#include <string>

namespace zero_cpu {

enum class PrivilegeLevel {
    Kernel,
    User
};

std::string privilegeLevelToString(PrivilegeLevel level);

} // namespace zero_cpu
