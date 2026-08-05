#include "zero_cpu/core/PrivilegeLevel.hpp"

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

} // namespace zero_cpu
