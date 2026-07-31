#include "zero_cpu/hardware/HardwareProtocol.hpp"

#include <sstream>
#include <stdexcept>

namespace zero_cpu::hardware {

namespace {

std::string trimLineEndings(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return text;
}

void requireNoExtraToken(std::istringstream& stream) {
    std::string extra;
    if (stream >> extra) {
        throw std::runtime_error("Hardware protocol message has extra fields");
    }
}

std::size_t parseOffset(const std::string& text) {
    std::size_t parsed = 0;
    const unsigned long long value = std::stoull(text, &parsed, 10);
    if (parsed != text.size()) {
        throw std::runtime_error("Invalid hardware register offset");
    }
    return static_cast<std::size_t>(value);
}

std::int64_t parseValue(const std::string& text) {
    std::size_t parsed = 0;
    const long long value = std::stoll(text, &parsed, 10);
    if (parsed != text.size()) {
        throw std::runtime_error("Invalid hardware register value");
    }
    return static_cast<std::int64_t>(value);
}

void requireVersion(const std::string& version) {
    if (version != HardwareProtocol::kVersion) {
        throw std::runtime_error("Unsupported hardware protocol version: " + version);
    }
}

} // namespace

std::string HardwareProtocol::pingRequest() {
    return std::string(kVersion) + " PING\n";
}

std::string HardwareProtocol::readRequest(std::size_t offset) {
    return std::string(kVersion) + " READ " + std::to_string(offset) + "\n";
}

std::string HardwareProtocol::writeRequest(
    std::size_t offset,
    std::int64_t value
) {
    return std::string(kVersion) + " WRITE " +
        std::to_string(offset) + " " + std::to_string(value) + "\n";
}

std::string HardwareProtocol::pongResponse() {
    return std::string(kVersion) + " PONG\n";
}

std::string HardwareProtocol::okResponse() {
    return std::string(kVersion) + " OK\n";
}

std::string HardwareProtocol::valueResponse(std::int64_t value) {
    return std::string(kVersion) + " VALUE " + std::to_string(value) + "\n";
}

std::string HardwareProtocol::errorResponse(const std::string& message) {
    return std::string(kVersion) + " ERROR " + message + "\n";
}

HardwareProtocolRequest HardwareProtocol::parseRequest(const std::string& text) {
    std::istringstream stream(trimLineEndings(text));
    std::string version;
    std::string command;

    if (!(stream >> version >> command)) {
        throw std::runtime_error("Malformed hardware protocol request");
    }
    requireVersion(version);

    if (command == "PING") {
        requireNoExtraToken(stream);
        return HardwareProtocolRequest{HardwareProtocolRequestType::Ping, 0, 0};
    }

    if (command == "READ") {
        std::string offsetText;
        if (!(stream >> offsetText)) {
            throw std::runtime_error("READ request requires an offset");
        }
        requireNoExtraToken(stream);
        return HardwareProtocolRequest{
            HardwareProtocolRequestType::Read,
            parseOffset(offsetText),
            0
        };
    }

    if (command == "WRITE") {
        std::string offsetText;
        std::string valueText;
        if (!(stream >> offsetText >> valueText)) {
            throw std::runtime_error("WRITE request requires offset and value");
        }
        requireNoExtraToken(stream);
        return HardwareProtocolRequest{
            HardwareProtocolRequestType::Write,
            parseOffset(offsetText),
            parseValue(valueText)
        };
    }

    throw std::runtime_error("Unknown hardware protocol request: " + command);
}

HardwareProtocolResponse HardwareProtocol::parseResponse(const std::string& text) {
    std::istringstream stream(trimLineEndings(text));
    std::string version;
    std::string command;

    if (!(stream >> version >> command)) {
        throw std::runtime_error("Malformed hardware protocol response");
    }
    requireVersion(version);

    if (command == "PONG") {
        requireNoExtraToken(stream);
        return HardwareProtocolResponse{HardwareProtocolResponseType::Pong, 0, {}};
    }

    if (command == "OK") {
        requireNoExtraToken(stream);
        return HardwareProtocolResponse{HardwareProtocolResponseType::Ok, 0, {}};
    }

    if (command == "VALUE") {
        std::string valueText;
        if (!(stream >> valueText)) {
            throw std::runtime_error("VALUE response requires a value");
        }
        requireNoExtraToken(stream);
        return HardwareProtocolResponse{
            HardwareProtocolResponseType::Value,
            parseValue(valueText),
            {}
        };
    }

    if (command == "ERROR") {
        std::string message;
        std::getline(stream, message);
        if (!message.empty() && message.front() == ' ') {
            message.erase(message.begin());
        }
        if (message.empty()) {
            message = "unspecified device error";
        }
        return HardwareProtocolResponse{
            HardwareProtocolResponseType::Error,
            0,
            message
        };
    }

    throw std::runtime_error("Unknown hardware protocol response: " + command);
}

} // namespace zero_cpu::hardware
