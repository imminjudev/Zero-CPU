#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace zero_cpu::hardware {

class HardwareBus {
public:
    virtual ~HardwareBus() = default;

    virtual std::string name() const = 0;

    virtual bool connected() const = 0;
    virtual void connect() = 0;
    virtual void disconnect() = 0;

    virtual std::int64_t readRegister(std::size_t offset) = 0;
    virtual void writeRegister(
        std::size_t offset,
        std::int64_t value
    ) = 0;
};

} // namespace zero_cpu::hardware
