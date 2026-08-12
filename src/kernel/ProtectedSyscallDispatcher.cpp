#include "zero_cpu/kernel/ProtectedSyscallDispatcher.hpp"

#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

namespace zero_cpu::kernel {

using ABI = ProtectedSyscallABI;

namespace {

SoftwareInterruptResult statusResult(
    std::int64_t status
) {
    SoftwareInterruptResult result;
    result.has_status = true;
    result.status = status;
    return result;
}

class ProcessExitRuntimeService final
    : public ProtectedRuntimeService {
public:
    bool handles(std::int64_t serviceNumber) const override {
        return serviceNumber == ABI::kExitSyscall;
    }

    SoftwareInterruptResult handle(
        std::int64_t serviceNumber,
        CPUState& state,
        MMIOBus*
    ) override {
        if (!handles(serviceNumber)) {
            return statusResult(ABI::kStatusUnsupported);
        }

        const std::int64_t exitCode = state.registers().get(
            ABI::kArgument0ResultRegister
        );

        SoftwareInterruptResult result = statusResult(ABI::kStatusOk);
        result.has_argument0 = true;
        result.argument0 = exitCode;
        result.disposition = SoftwareInterruptDisposition::TerminateProcess;
        result.exit_code = exitCode;

        state.registers().set(
            ABI::kExitValueRegister,
            exitCode
        );

        return result;
    }
};

class HardwareRuntimeService final
    : public ProtectedRuntimeService {
public:
    bool handles(std::int64_t serviceNumber) const override {
        return serviceNumber == ABI::kHardwareWriteSyscall
            || serviceNumber == ABI::kHardwareReadSyscall;
    }

    SoftwareInterruptResult handle(
        std::int64_t serviceNumber,
        CPUState& state,
        MMIOBus* mmioBus
    ) override {
        if (!handles(serviceNumber)) {
            return statusResult(ABI::kStatusUnsupported);
        }

        const std::int64_t offsetValue = state.registers().get(
            ABI::kArgument0ResultRegister
        );

        SoftwareInterruptResult result;
        result.has_argument0 = true;
        result.argument0 = offsetValue;

        auto finish = [&](std::int64_t status) {
            result.has_status = true;
            result.status = status;
            return result;
        };

        if (!validHardwareOffset(offsetValue)) {
            return finish(ABI::kStatusInvalidHardwareOffset);
        }

        if (mmioBus == nullptr) {
            return finish(ABI::kStatusHardwareUnavailable);
        }

        const std::size_t offset = static_cast<std::size_t>(offsetValue);
        const std::size_t address = memory_map::kHardwareBase + offset;

        if (!mmioBus->hasDeviceAt(address)) {
            return finish(ABI::kStatusHardwareUnavailable);
        }

        try {
            if (serviceNumber == ABI::kHardwareWriteSyscall) {
                const std::int64_t value = state.registers().get(
                    ABI::kArgument1Register
                );
                result.has_argument1 = true;
                result.argument1 = value;
                mmioBus->write(address, value);
                return finish(ABI::kStatusOk);
            }

            const std::int64_t value = mmioBus->read(address);
            state.registers().set(
                ABI::kArgument0ResultRegister,
                value
            );
            result.has_result = true;
            result.result_value = value;
            return finish(ABI::kStatusOk);
        } catch (const std::exception&) {
            return finish(ABI::kStatusHardwareError);
        }
    }

private:
    static bool validHardwareOffset(std::int64_t offset) {
        if (offset < 0) {
            return false;
        }
        const std::size_t value = static_cast<std::size_t>(offset);
        return value < memory_map::kHardwareSize
            && value % memory_map::kHardwareRegisterWidth == 0;
    }
};

} // namespace

ProtectedSyscallDispatcher::ProtectedSyscallDispatcher() {
    addService(std::make_shared<ProcessExitRuntimeService>());
    addService(std::make_shared<HardwareRuntimeService>());
}

bool ProtectedSyscallDispatcher::handles(
    std::uint8_t vector
) const {
    return vector == ABI::kSyscallVector;
}

SoftwareInterruptResult ProtectedSyscallDispatcher::handle(
    std::uint8_t vector,
    CPUState& state,
    MMIOBus* mmioBus
) {
    auto finish = [&](SoftwareInterruptResult result) {
        if (!result.has_status) {
            result.has_status = true;
            result.status = ABI::kStatusOk;
        }
        setStatus(state, result.status);
        return result;
    };

    if (!handles(vector)) {
        return finish(statusResult(ABI::kStatusUnsupported));
    }

    const std::int64_t serviceNumber = state.registers().get(
        ABI::kServiceRegister
    );

    ProtectedRuntimeService* service = findService(serviceNumber);

    if (service == nullptr) {
        SoftwareInterruptResult result = statusResult(
            ABI::kStatusUnsupported
        );
        result.has_service_number = true;
        result.service_number = serviceNumber;
        return finish(std::move(result));
    }

    SoftwareInterruptResult result = service->handle(
        serviceNumber,
        state,
        mmioBus
    );
    result.has_service_number = true;
    result.service_number = serviceNumber;
    return finish(std::move(result));
}

void ProtectedSyscallDispatcher::addService(
    std::shared_ptr<ProtectedRuntimeService> service
) {
    if (!service) {
        throw std::invalid_argument("Protected runtime service is null");
    }
    services_.push_back(std::move(service));
}

std::size_t ProtectedSyscallDispatcher::serviceCount() const {
    return services_.size();
}

ProtectedRuntimeService* ProtectedSyscallDispatcher::findService(
    std::int64_t serviceNumber
) const {
    for (const auto& service : services_) {
        if (service && service->handles(serviceNumber)) {
            return service.get();
        }
    }
    return nullptr;
}

void ProtectedSyscallDispatcher::setStatus(
    CPUState& state,
    std::int64_t status
) {
    state.registers().set(
        ABI::kStatusRegister,
        status
    );
}

} // namespace zero_cpu::kernel

// Patch: v1.4-protected-syscall-hardware-r1

// Patch: v1.5-protected-syscall-observability-r1

// Patch: v1.8-protected-runtime-service-r1
