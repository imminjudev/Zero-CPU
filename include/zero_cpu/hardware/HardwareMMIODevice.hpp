#pragma once

#include "zero_cpu/core/MMIODevice.hpp"
#include "zero_cpu/hardware/HardwareBus.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace zero_cpu::hardware {

class HardwareMMIODevice final : public MMIODevice {
public:
    explicit HardwareMMIODevice(
        std::shared_ptr<HardwareBus> bus
    );

    std::string name() const override;

    std::int64_t read(std::size_t offset) override;
    void write(
        std::size_t offset,
        std::int64_t value
    ) override;

    const std::shared_ptr<HardwareBus>& bus() const;

private:
    std::shared_ptr<HardwareBus> bus_;

    static void validateOffset(std::size_t offset);
    void requireConnected() const;
};

} // namespace zero_cpu::hardware
