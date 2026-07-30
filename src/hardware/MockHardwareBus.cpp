#include "zero_cpu/hardware/MockHardwareBus.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace zero_cpu::hardware {

MockHardwareBus::MockHardwareBus(std::string busName)
    : name_(std::move(busName)) {
    if (name_.empty()) {
        throw std::runtime_error(
            "Mock hardware bus name must not be empty"
        );
    }
}

std::string MockHardwareBus::name() const {
    return name_;
}

bool MockHardwareBus::connected() const {
    return connected_;
}

void MockHardwareBus::connect() {
    connected_ = true;
}

void MockHardwareBus::disconnect() {
    connected_ = false;
}

std::int64_t MockHardwareBus::readRegister(
    std::size_t offset
) {
    requireConnected();

    const std::int64_t value = registerValue(offset);

    transactions_.push_back(
        HardwareTransaction{
            HardwareAccessType::Read,
            offset,
            value
        }
    );

    return value;
}

void MockHardwareBus::writeRegister(
    std::size_t offset,
    std::int64_t value
) {
    requireConnected();

    registers_[offset] = value;

    transactions_.push_back(
        HardwareTransaction{
            HardwareAccessType::Write,
            offset,
            value
        }
    );
}

void MockHardwareBus::setRegisterValue(
    std::size_t offset,
    std::int64_t value
) {
    registers_[offset] = value;
}

std::int64_t MockHardwareBus::registerValue(
    std::size_t offset
) const {
    const auto it = registers_.find(offset);

    if (it == registers_.end()) {
        return 0;
    }

    return it->second;
}

const std::vector<HardwareTransaction>&
MockHardwareBus::transactions() const {
    return transactions_;
}

void MockHardwareBus::clearTransactions() {
    transactions_.clear();
}

std::size_t MockHardwareBus::readCount() const {
    return static_cast<std::size_t>(
        std::count_if(
            transactions_.begin(),
            transactions_.end(),
            [](const HardwareTransaction& transaction) {
                return transaction.type ==
                    HardwareAccessType::Read;
            }
        )
    );
}

std::size_t MockHardwareBus::writeCount() const {
    return static_cast<std::size_t>(
        std::count_if(
            transactions_.begin(),
            transactions_.end(),
            [](const HardwareTransaction& transaction) {
                return transaction.type ==
                    HardwareAccessType::Write;
            }
        )
    );
}

void MockHardwareBus::requireConnected() const {
    if (!connected_) {
        throw std::runtime_error(
            "Hardware bus is not connected: " + name_
        );
    }
}

} // namespace zero_cpu::hardware
