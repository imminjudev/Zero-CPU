#include "zero_cpu/hardware/MockSerialTransport.hpp"

#include "zero_cpu/hardware/HardwareProtocol.hpp"

#include <stdexcept>
#include <utility>

namespace zero_cpu::hardware {

MockSerialTransport::MockSerialTransport(std::string transportName)
    : name_(std::move(transportName)) {
}

std::string MockSerialTransport::name() const {
    return name_;
}

bool MockSerialTransport::connected() const {
    return connected_;
}

void MockSerialTransport::connect() {
    connected_ = true;
}

void MockSerialTransport::disconnect() {
    connected_ = false;
}

std::string MockSerialTransport::transact(
    const std::string& request,
    std::uint32_t timeoutMilliseconds
) {
    requireConnected();

    if (timeoutMilliseconds == 0) {
        throw std::runtime_error("Serial transaction timeout must be greater than zero");
    }

    requests_.push_back(request);

    if (timeout_next_) {
        timeout_next_ = false;
        throw std::runtime_error("Serial transport timeout");
    }

    if (has_next_raw_response_) {
        has_next_raw_response_ = false;
        return std::exchange(next_raw_response_, {});
    }

    try {
        const HardwareProtocolRequest parsed =
            HardwareProtocol::parseRequest(request);

        switch (parsed.type) {
        case HardwareProtocolRequestType::Ping:
            return HardwareProtocol::pongResponse();

        case HardwareProtocolRequestType::Read: {
            const auto it = registers_.find(parsed.offset);
            const std::int64_t value =
                it == registers_.end() ? 0 : it->second;
            return HardwareProtocol::valueResponse(value);
        }

        case HardwareProtocolRequestType::Write:
            registers_[parsed.offset] = parsed.value;
            return HardwareProtocol::okResponse();
        }
    } catch (const std::exception& ex) {
        return HardwareProtocol::errorResponse(ex.what());
    }

    return HardwareProtocol::errorResponse("unhandled request");
}

void MockSerialTransport::setRegisterValue(
    std::size_t offset,
    std::int64_t value
) {
    registers_[offset] = value;
}

std::int64_t MockSerialTransport::registerValue(std::size_t offset) const {
    const auto it = registers_.find(offset);
    return it == registers_.end() ? 0 : it->second;
}

const std::vector<std::string>& MockSerialTransport::requests() const {
    return requests_;
}

void MockSerialTransport::clearRequests() {
    requests_.clear();
}

void MockSerialTransport::failNextWithError(const std::string& message) {
    setNextRawResponse(HardwareProtocol::errorResponse(message));
}

void MockSerialTransport::setNextRawResponse(const std::string& response) {
    has_next_raw_response_ = true;
    next_raw_response_ = response;
}

void MockSerialTransport::timeoutNextTransaction() {
    timeout_next_ = true;
}

void MockSerialTransport::requireConnected() const {
    if (!connected_) {
        throw std::runtime_error("Mock serial transport is not connected");
    }
}

} // namespace zero_cpu::hardware
