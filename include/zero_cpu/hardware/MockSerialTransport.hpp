#pragma once

#include "zero_cpu/hardware/SerialTransport.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace zero_cpu::hardware {

class MockSerialTransport final : public SerialTransport {
public:
    explicit MockSerialTransport(
        std::string transportName = "mock-serial"
    );

    std::string name() const override;

    bool connected() const override;
    void connect() override;
    void disconnect() override;

    std::string transact(
        const std::string& request,
        std::uint32_t timeoutMilliseconds
    ) override;

    void setRegisterValue(
        std::size_t offset,
        std::int64_t value
    );
    std::int64_t registerValue(std::size_t offset) const;

    const std::vector<std::string>& requests() const;
    void clearRequests();

    void failNextWithError(const std::string& message);
    void setNextRawResponse(const std::string& response);
    void timeoutNextTransaction();

private:
    std::string name_;
    bool connected_ = false;
    bool timeout_next_ = false;
    bool has_next_raw_response_ = false;
    std::string next_raw_response_;

    std::map<std::size_t, std::int64_t> registers_;
    std::vector<std::string> requests_;

    void requireConnected() const;
};

} // namespace zero_cpu::hardware
