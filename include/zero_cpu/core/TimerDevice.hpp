#pragma once

#include "zero_cpu/core/ClockedDevice.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/MMIODevice.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace zero_cpu {

class TimerDevice final : public MMIODevice, public ClockedDevice {
public:
    static constexpr std::size_t kTickCountOffset = 0;
    static constexpr std::size_t kIntervalOffset = 8;
    static constexpr std::size_t kEnabledOffset = 16;
    static constexpr std::size_t kVectorOffset = 24;
    static constexpr std::size_t kPayloadOffset = 32;
    static constexpr std::size_t kInterruptCountOffset = 40;

    TimerDevice(
        std::shared_ptr<InterruptController> controller,
        std::uint8_t vector,
        std::uint64_t interval,
        std::int64_t payload = 0
    );

    std::string name() const override;

    std::int64_t read(std::size_t offset) override;
    void write(std::size_t offset, std::int64_t value) override;

    void tick() override;
    void tick(std::uint64_t count);

    void reset();

    bool enabled() const;
    void setEnabled(bool enabled);

    std::uint64_t tickCount() const;
    std::uint64_t interval() const;
    void setInterval(std::uint64_t interval);

    std::uint8_t vector() const;
    void setVector(std::uint8_t vector);

    std::int64_t payload() const;
    void setPayload(std::int64_t payload);

    std::uint64_t interruptCount() const;

private:
    std::shared_ptr<InterruptController> controller_;
    std::uint8_t vector_ = 0;
    std::uint64_t interval_ = 1;
    std::uint64_t tick_count_ = 0;
    std::uint64_t interrupt_count_ = 0;
    std::int64_t payload_ = 0;
    bool enabled_ = true;

    void validateController() const;
    void maybeRequestInterrupt();
};

} // namespace zero_cpu
