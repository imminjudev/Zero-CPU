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

void ProtectedSyscallDispatcher::handle(
    std::uint8_t vector,
    CPUState& state,
    MMIOBus* mmioBus
) {
    if (!handles(vector)) {
        setStatus(state, kStatusUnsupported);
        return;
    }

    const std::int64_t syscallNumber =
        state.registers().get(
            RegisterName::R1
        );

    if (
        syscallNumber != kHardwareWriteSyscall
        && syscallNumber != kHardwareReadSyscall
    ) {
        setStatus(state, kStatusUnsupported);
        return;
    }

    const std::int64_t offsetValue =
        state.registers().get(
            RegisterName::R2
        );

    if (!validHardwareOffset(offsetValue)) {
        setStatus(
            state,
            kStatusInvalidHardwareOffset
        );
        return;
    }

    if (mmioBus == nullptr) {
        setStatus(
            state,
            kStatusHardwareUnavailable
        );
        return;
    }

    const std::size_t offset =
        static_cast<std::size_t>(
            offsetValue
        );

    const std::size_t address =
        memory_map::kHardwareBase + offset;

    if (!mmioBus->hasDeviceAt(address)) {
        setStatus(
            state,
            kStatusHardwareUnavailable
        );
        return;
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

            mmioBus->write(address, value);
            setStatus(state, kStatusOk);
            return;
        }

        const std::int64_t value =
            mmioBus->read(address);

        state.registers().set(
            RegisterName::R2,
            value
        );

        setStatus(state, kStatusOk);
    } catch (const std::exception&) {
        setStatus(
            state,
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
