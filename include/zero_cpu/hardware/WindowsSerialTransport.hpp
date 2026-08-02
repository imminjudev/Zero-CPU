#pragma once

#include "zero_cpu/hardware/SerialTransport.hpp"

#include <cstdint>
#include <string>

namespace zero_cpu::hardware {

class WindowsSerialTransport final : public SerialTransport {
public:
    explicit WindowsSerialTransport(
        std::string portName,
        std::uint32_t baudRate = 115200
    );

    ~WindowsSerialTransport() override;

    WindowsSerialTransport(const WindowsSerialTransport&) = delete;
    WindowsSerialTransport& operator=(const WindowsSerialTransport&) = delete;

    std::string name() const override;

    bool connected() const override;
    void connect() override;
    void disconnect() override;

    std::string transact(
        const std::string& request,
        std::uint32_t timeoutMilliseconds
    ) override;

    const std::string& portName() const;
    std::uint32_t baudRate() const;

private:
    std::string port_name_;
    std::uint32_t baud_rate_ = 115200;
    void* handle_ = nullptr;

    void requireConnected() const;
    void configurePort();
    void configureTimeout(std::uint32_t timeoutMilliseconds);

    static std::string normalizedPortPath(const std::string& portName);
};

} // namespace zero_cpu::hardware
