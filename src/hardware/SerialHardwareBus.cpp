#include "zero_cpu/hardware/SerialHardwareBus.hpp"

#include "zero_cpu/hardware/HardwareProtocol.hpp"

#include <stdexcept>
#include <utility>

namespace zero_cpu::hardware {

SerialHardwareBus::SerialHardwareBus(
    std::shared_ptr<SerialTransport> transport,
    std::uint32_t timeoutMilliseconds
) : transport_(std::move(transport)),
    timeout_milliseconds_(timeoutMilliseconds) {
    if (!transport_) {
        throw std::runtime_error("SerialHardwareBus requires a transport");
    }
    if (timeout_milliseconds_ == 0) {
        throw std::runtime_error("SerialHardwareBus timeout must be greater than zero");
    }
}

std::string SerialHardwareBus::name() const {
    return "serial-hardware(" + transport_->name() + ")";
}

bool SerialHardwareBus::connected() const {
    return transport_ && transport_->connected();
}

void SerialHardwareBus::connect() {
    if (connected()) {
        return;
    }

    transport_->connect();

    try {
        const HardwareProtocolResponse response =
            HardwareProtocol::parseResponse(
                transact(HardwareProtocol::pingRequest())
            );

        if (response.type == HardwareProtocolResponseType::Error) {
            throw std::runtime_error(
                "Hardware handshake failed: " + response.message
            );
        }

        if (response.type != HardwareProtocolResponseType::Pong) {
            throw std::runtime_error("Hardware handshake expected PONG");
        }
    } catch (...) {
        transport_->disconnect();
        throw;
    }
}

void SerialHardwareBus::disconnect() {
    if (transport_) {
        transport_->disconnect();
    }
}

std::int64_t SerialHardwareBus::readRegister(std::size_t offset) {
    requireConnected();

    const HardwareProtocolResponse response =
        HardwareProtocol::parseResponse(
            transact(HardwareProtocol::readRequest(offset))
        );

    if (response.type == HardwareProtocolResponseType::Error) {
        throw std::runtime_error("Hardware read failed: " + response.message);
    }

    if (response.type != HardwareProtocolResponseType::Value) {
        throw std::runtime_error("Hardware read expected VALUE response");
    }

    return response.value;
}

void SerialHardwareBus::writeRegister(
    std::size_t offset,
    std::int64_t value
) {
    requireConnected();

    const HardwareProtocolResponse response =
        HardwareProtocol::parseResponse(
            transact(HardwareProtocol::writeRequest(offset, value))
        );

    if (response.type == HardwareProtocolResponseType::Error) {
        throw std::runtime_error("Hardware write failed: " + response.message);
    }

    if (response.type != HardwareProtocolResponseType::Ok) {
        throw std::runtime_error("Hardware write expected OK response");
    }
}

const std::shared_ptr<SerialTransport>& SerialHardwareBus::transport() const {
    return transport_;
}

std::uint32_t SerialHardwareBus::timeoutMilliseconds() const {
    return timeout_milliseconds_;
}

void SerialHardwareBus::requireConnected() const {
    if (!connected()) {
        throw std::runtime_error("Serial hardware bus is not connected");
    }
}

std::string SerialHardwareBus::transact(const std::string& request) {
    return transport_->transact(request, timeout_milliseconds_);
}

} // namespace zero_cpu::hardware
