#pragma once

#include "zero_cpu/core/SoftwareInterruptHandler.hpp"
#include "zero_cpu/kernel/ProtectedRuntimeService.hpp"
#include "zero_cpu/kernel/ProtectedSyscallABI.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace zero_cpu::kernel {

class ProtectedSyscallDispatcher final
    : public SoftwareInterruptHandler {
public:
    ProtectedSyscallDispatcher();

    bool handles(
        std::uint8_t vector
    ) const override;

    SoftwareInterruptResult handle(
        std::uint8_t vector,
        CPUState& state,
        MMIOBus* mmioBus
    ) override;

    void addService(
        std::shared_ptr<ProtectedRuntimeService> service
    );

    std::size_t serviceCount() const;

private:
    std::vector<std::shared_ptr<ProtectedRuntimeService>>
        services_;

    ProtectedRuntimeService* findService(
        std::int64_t serviceNumber
    ) const;

    static void setStatus(
        CPUState& state,
        std::int64_t status
    );
};

} // namespace zero_cpu::kernel

// Patch: v1.4-protected-syscall-hardware-r1

// Patch: v1.8-protected-runtime-service-r1
