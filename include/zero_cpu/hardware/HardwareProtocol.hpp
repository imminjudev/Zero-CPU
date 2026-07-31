#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace zero_cpu::hardware {

enum class HardwareProtocolRequestType {
    Ping,
    Read,
    Write
};

struct HardwareProtocolRequest {
    HardwareProtocolRequestType type = HardwareProtocolRequestType::Ping;
    std::size_t offset = 0;
    std::int64_t value = 0;
};

enum class HardwareProtocolResponseType {
    Pong,
    Ok,
    Value,
    Error
};

struct HardwareProtocolResponse {
    HardwareProtocolResponseType type = HardwareProtocolResponseType::Error;
    std::int64_t value = 0;
    std::string message;
};

class HardwareProtocol {
public:
    inline static constexpr const char* kVersion = "ZEROCPU/1";

    static std::string pingRequest();
    static std::string readRequest(std::size_t offset);
    static std::string writeRequest(
        std::size_t offset,
        std::int64_t value
    );

    static std::string pongResponse();
    static std::string okResponse();
    static std::string valueResponse(std::int64_t value);
    static std::string errorResponse(const std::string& message);

    static HardwareProtocolRequest parseRequest(const std::string& text);
    static HardwareProtocolResponse parseResponse(const std::string& text);
};

} // namespace zero_cpu::hardware
