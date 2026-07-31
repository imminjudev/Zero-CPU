#pragma once

#include "zero_cpu/hardware/HardwareBus.hpp"
#include "zero_cpu/hardware/SerialTransport.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace zero_cpu::hardware {

class SerialHardwareBus final : public HardwareBus {
public:
    explicit SerialHardwareBus(
        std::shared_ptr<SerialTransport> transport,
        std::uint32_t timeoutMilliseconds = 500
    );

    std::string name() const override;

    bool connected() const override;
    void connect() override;
    void disconnect() override;

    std::int64_t readRegister(std::size_t offset) override;
    void writeRegister(
        std::size_t offset,
        std::int64_t value
    ) override;

    const std::shared_ptr<SerialTransport>& transport() const;
    std::uint32_t timeoutMilliseconds() const;

private:
    std::shared_ptr<SerialTransport> transport_;
    std::uint32_t timeout_milliseconds_ = 500;

    void requireConnected() const;
    std::string transact(const std::string& request);
};

} // namespace zero_cpu::hardware
