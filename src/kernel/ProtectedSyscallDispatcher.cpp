#include "zero_cpu/kernel/ProtectedSyscallDispatcher.hpp"

#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>

namespace zero_cpu::kernel {

bool ProtectedSyscallDispatcher::handles(
    std::uint8_t vector
) const {
    return vector == kSyscallVector;
}

SoftwareInterruptResult
ProtectedSyscallDispatcher::handle(
    std::uint8_t vector,
    CPUState& state,
    MMIOBus* mmioBus
) {
    SoftwareInterruptResult result;

    auto finish = [&](
        std::int64_t status
    ) {
        setStatus(state, status);
        result.has_status = true;
        result.status = status;
        return result;
    };

    if (!handles(vector)) {
        return finish(
            kStatusUnsupported
        );
    }

    const std::int64_t syscallNumber =
        state.registers().get(
            RegisterName::R1
        );

    result.has_service_number = true;
    result.service_number = syscallNumber;

    if (syscallNumber == kExitSyscall) {
        const std::int64_t exitCode =
            state.registers().get(
                RegisterName::R2
            );

        result.has_argument0 = true;
        result.argument0 = exitCode;

        result.disposition =
            SoftwareInterruptDisposition::
                TerminateProcess;

        result.exit_code = exitCode;

        state.registers().set(
            RegisterName::R7,
            exitCode
        );

        return finish(kStatusOk);
    }

    if (
        syscallNumber != kHardwareWriteSyscall
        && syscallNumber != kHardwareReadSyscall
    ) {
        return finish(
            kStatusUnsupported
        );
    }

    const std::int64_t offsetValue =
        state.registers().get(
            RegisterName::R2
        );

    result.has_argument0 = true;
    result.argument0 = offsetValue;

    if (!validHardwareOffset(offsetValue)) {
        return finish(
            kStatusInvalidHardwareOffset
        );
    }

    if (mmioBus == nullptr) {
        return finish(
            kStatusHardwareUnavailable
        );
    }

    const std::size_t offset =
        static_cast<std::size_t>(
            offsetValue
        );

    const std::size_t address =
        memory_map::kHardwareBase + offset;

    if (!mmioBus->hasDeviceAt(address)) {
        return finish(
            kStatusHardwareUnavailable
        );
    }

    try {
        if (
            syscallNumber
            == kHardwareWriteSyscall
        ) {
            const std::int64_t value =
                state.registers().get(
                    RegisterName::R3
                );

            result.has_argument1 = true;
            result.argument1 = value;

            mmioBus->write(address, value);
            return finish(kStatusOk);
        }

        const std::int64_t value =
            mmioBus->read(address);

        state.registers().set(
            RegisterName::R2,
            value
        );

        result.has_result = true;
        result.result_value = value;

        return finish(kStatusOk);
    } catch (const std::exception&) {
        return finish(
            kStatusHardwareError
        );
    }
}

bool ProtectedSyscallDispatcher::
validHardwareOffset(
    std::int64_t offset
) {
    if (offset < 0) {
        return false;
    }

    const std::size_t value =
        static_cast<std::size_t>(offset);

    return value < memory_map::kHardwareSize
        && value
            % memory_map::kHardwareRegisterWidth
            == 0;
}

void ProtectedSyscallDispatcher::setStatus(
    CPUState& state,
    std::int64_t status
) {
    state.registers().set(
        RegisterName::R4,
        status
    );
}

} // namespace zero_cpu::kernel

// Patch: v1.4-protected-syscall-hardware-r1

// Patch: v1.5-protected-syscall-observability-r1
