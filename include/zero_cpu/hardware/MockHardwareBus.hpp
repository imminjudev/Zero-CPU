#pragma once

#include "zero_cpu/hardware/HardwareBus.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace zero_cpu::hardware {

enum class HardwareAccessType {
    Read,
    Write
};

struct HardwareTransaction {
    HardwareAccessType type = HardwareAccessType::Read;
    std::size_t offset = 0;
    std::int64_t value = 0;
};

class MockHardwareBus final : public HardwareBus {
public:
    explicit MockHardwareBus(
        std::string busName = "mock-hardware"
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

    void setRegisterValue(
        std::size_t offset,
        std::int64_t value
    );

    std::int64_t registerValue(std::size_t offset) const;

    const std::vector<HardwareTransaction>& transactions() const;
    void clearTransactions();

    std::size_t readCount() const;
    std::size_t writeCount() const;

private:
    std::string name_;
    bool connected_ = false;

    std::map<std::size_t, std::int64_t> registers_;
    std::vector<HardwareTransaction> transactions_;

    void requireConnected() const;
};

} // namespace zero_cpu::hardware
