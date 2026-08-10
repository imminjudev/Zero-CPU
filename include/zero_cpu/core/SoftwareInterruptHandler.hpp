#pragma once

#include <cstdint>

namespace zero_cpu {

class CPUState;
class MMIOBus;

enum class SoftwareInterruptDisposition
    : std::uint8_t {
    ReturnToCaller = 0,
    TerminateProcess = 1
};

inline const char*
softwareInterruptDispositionToString(
    SoftwareInterruptDisposition disposition
) {
    switch (disposition) {
    case SoftwareInterruptDisposition::ReturnToCaller:
        return "ReturnToCaller";
    case SoftwareInterruptDisposition::TerminateProcess:
        return "TerminateProcess";
    }

    return "Unknown";
}

struct SoftwareInterruptResult {
    SoftwareInterruptDisposition disposition =
        SoftwareInterruptDisposition::ReturnToCaller;

    std::int64_t exit_code = 0;

    bool has_service_number = false;
    std::int64_t service_number = 0;

    bool has_argument0 = false;
    std::int64_t argument0 = 0;

    bool has_argument1 = false;
    std::int64_t argument1 = 0;

    bool has_status = false;
    std::int64_t status = 0;

    bool has_result = false;
    std::int64_t result_value = 0;

    static SoftwareInterruptResult
    returnToCaller() {
        return {};
    }

    static SoftwareInterruptResult
    terminateProcess(
        std::int64_t exitCode
    ) {
        SoftwareInterruptResult result;
        result.disposition =
            SoftwareInterruptDisposition::
                TerminateProcess;
        result.exit_code = exitCode;
        return result;
    }
};

struct SoftwareInterruptObservation {
    std::uint8_t vector = 0;
    SoftwareInterruptResult result;
};

class SoftwareInterruptHandler {
public:
    virtual ~SoftwareInterruptHandler() = default;

    virtual bool handles(
        std::uint8_t vector
    ) const = 0;

    virtual SoftwareInterruptResult handle(
        std::uint8_t vector,
        CPUState& state,
        MMIOBus* mmioBus
    ) = 0;
};

} // namespace zero_cpu

// Patch: v1.4-protected-syscall-hardware-r1

// Patch: v1.5-protected-syscall-observability-r1
