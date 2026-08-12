#include "zero_cpu/kernel/ProtectedSyscallDispatcher.hpp"

#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>

namespace zero_cpu::kernel {

using ABI = ProtectedSyscallABI;

bool ProtectedSyscallDispatcher::handles(
    std::uint8_t vector
) const {
    return vector == ABI::kSyscallVector;
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
            ABI::kStatusUnsupported
        );
    }

    const std::int64_t syscallNumber =
        state.registers().get(
            ABI::kServiceRegister
        );

    result.has_service_number = true;
    result.service_number = syscallNumber;

    if (syscallNumber == ABI::kExitSyscall) {
        const std::int64_t exitCode =
            state.registers().get(
                ABI::kArgument0ResultRegister
            );

        result.has_argument0 = true;
        result.argument0 = exitCode;

        result.disposition =
            SoftwareInterruptDisposition::
                TerminateProcess;

        result.exit_code = exitCode;

        state.registers().set(
            ABI::kExitValueRegister,
            exitCode
        );

        return finish(ABI::kStatusOk);
    }

    if (
        syscallNumber != ABI::kHardwareWriteSyscall
        && syscallNumber != ABI::kHardwareReadSyscall
    ) {
        return finish(
            ABI::kStatusUnsupported
        );
    }

    const std::int64_t offsetValue =
        state.registers().get(
            ABI::kArgument0ResultRegister
        );

    result.has_argument0 = true;
    result.argument0 = offsetValue;

    if (!validHardwareOffset(offsetValue)) {
        return finish(
            ABI::kStatusInvalidHardwareOffset
        );
    }

    if (mmioBus == nullptr) {
        return finish(
            ABI::kStatusHardwareUnavailable
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
            ABI::kStatusHardwareUnavailable
        );
    }

    try {
        if (
            syscallNumber
            == ABI::kHardwareWriteSyscall
        ) {
            const std::int64_t value =
                state.registers().get(
                    ABI::kArgument1Register
                );

            result.has_argument1 = true;
            result.argument1 = value;

            mmioBus->write(address, value);
            return finish(ABI::kStatusOk);
        }

        const std::int64_t value =
            mmioBus->read(address);

        state.registers().set(
            ABI::kArgument0ResultRegister,
            value
        );

        result.has_result = true;
        result.result_value = value;

        return finish(ABI::kStatusOk);
    } catch (const std::exception&) {
        return finish(
            ABI::kStatusHardwareError
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
        ABI::kStatusRegister,
        status
    );
}

} // namespace zero_cpu::kernel

// Patch: v1.4-protected-syscall-hardware-r1

// Patch: v1.5-protected-syscall-observability-r1
