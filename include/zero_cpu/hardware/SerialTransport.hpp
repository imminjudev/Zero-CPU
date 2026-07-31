#pragma once

#include <cstdint>
#include <string>

namespace zero_cpu::hardware {

class SerialTransport {
public:
    virtual ~SerialTransport() = default;

    virtual std::string name() const = 0;

    virtual bool connected() const = 0;
    virtual void connect() = 0;
    virtual void disconnect() = 0;

    virtual std::string transact(
        const std::string& request,
        std::uint32_t timeoutMilliseconds
    ) = 0;
};

} // namespace zero_cpu::hardware
