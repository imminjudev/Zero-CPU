#include "zero_cpu/hardware/HardwareMMIODevice.hpp"

#include "zero_cpu/core/MemoryMap.hpp"

#include <stdexcept>
#include <utility>

namespace zero_cpu::hardware {

HardwareMMIODevice::HardwareMMIODevice(
    std::shared_ptr<HardwareBus> bus
)
    : bus_(std::move(bus)) {
    if (!bus_) {
        throw std::runtime_error(
            "HardwareMMIODevice requires a hardware bus"
        );
    }
}

std::string HardwareMMIODevice::name() const {
    return "HardwareBridge(" + bus_->name() + ")";
}

std::int64_t HardwareMMIODevice::read(
    std::size_t offset
) {
    validateOffset(offset);
    requireConnected();
    return bus_->readRegister(offset);
}

void HardwareMMIODevice::write(
    std::size_t offset,
    std::int64_t value
) {
    validateOffset(offset);
    requireConnected();
    bus_->writeRegister(offset, value);
}

const std::shared_ptr<HardwareBus>&
HardwareMMIODevice::bus() const {
    return bus_;
}

void HardwareMMIODevice::validateOffset(
    std::size_t offset
) {
    if (offset >= memory_map::kHardwareSize) {
        throw std::runtime_error(
            "Hardware MMIO offset is outside the hardware window"
        );
    }

    if (
        offset % memory_map::kHardwareRegisterWidth != 0
    ) {
        throw std::runtime_error(
            "Hardware MMIO offset must be 8-byte aligned"
        );
    }
}

void HardwareMMIODevice::requireConnected() const {
    if (!bus_->connected()) {
        throw std::runtime_error(
            "Hardware MMIO access requires a connected bus"
        );
    }
}

} // namespace zero_cpu::hardware
