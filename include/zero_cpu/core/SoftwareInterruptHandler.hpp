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

struct SoftwareInterruptResult {
    SoftwareInterruptDisposition disposition =
        SoftwareInterruptDisposition::ReturnToCaller;

    std::int64_t exit_code = 0;

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
