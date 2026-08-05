#include "zero_cpu/core/PrivilegeLevel.hpp"

#include <stdexcept>
#include <string>

namespace zero_cpu {

std::string privilegeLevelToString(PrivilegeLevel level) {
    switch (level) {
    case PrivilegeLevel::Kernel:
        return "Kernel";

    case PrivilegeLevel::User:
        return "User";
    }

    return "Unknown";
}

std::int64_t privilegeLevelToRaw(PrivilegeLevel level) {
    switch (level) {
    case PrivilegeLevel::Kernel:
        return 0;

    case PrivilegeLevel::User:
        return 1;
    }

    throw std::runtime_error(
        "Cannot encode unknown privilege level"
    );
}

PrivilegeLevel privilegeLevelFromRaw(std::int64_t value) {
    switch (value) {
    case 0:
        return PrivilegeLevel::Kernel;

    case 1:
        return PrivilegeLevel::User;

    default:
        throw std::runtime_error(
            "Invalid saved privilege level: "
            + std::to_string(value)
        );
    }
}

} // namespace zero_cpu
