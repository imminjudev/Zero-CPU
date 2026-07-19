#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace zero_cpu {

struct InterruptRequest {
    std::uint8_t vector = 0;
    std::int64_t payload = 0;
    std::string source;
};

class InterruptController {
public:
    static constexpr std::size_t kVectorCount = 256;

    InterruptController();

    void clear();

    void setGlobalEnabled(bool enabled);
    bool globalEnabled() const;

    void mask(std::uint8_t vector);
    void unmask(std::uint8_t vector);
    bool isMasked(std::uint8_t vector) const;

    void setVectorHandler(std::uint8_t vector, std::size_t handlerAddress);
    void clearVectorHandler(std::uint8_t vector);
    bool hasVectorHandler(std::uint8_t vector) const;
    std::size_t vectorHandler(std::uint8_t vector) const;

    void request(
        std::uint8_t vector,
        std::int64_t payload = 0,
        std::string source = {}
    );

    bool hasPending() const;
    std::size_t pendingCount() const;
    std::vector<InterruptRequest> pendingRequests() const;

    InterruptRequest acknowledge();

private:
    std::array<std::optional<std::size_t>, kVectorCount> vector_table_;
    std::array<bool, kVectorCount> masks_;
    std::deque<InterruptRequest> pending_;
    bool global_enabled_ = true;

    std::deque<InterruptRequest>::iterator findDeliverable();
    std::deque<InterruptRequest>::const_iterator findDeliverable() const;
};

} // namespace zero_cpu
