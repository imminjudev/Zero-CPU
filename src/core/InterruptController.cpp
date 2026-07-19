#include "zero_cpu/core/InterruptController.hpp"

#include <stdexcept>
#include <utility>

namespace zero_cpu {

InterruptController::InterruptController() {
    clear();
}

void InterruptController::clear() {
    for (std::optional<std::size_t>& handler : vector_table_) {
        handler.reset();
    }

    masks_.fill(false);
    pending_.clear();
    global_enabled_ = true;
}

void InterruptController::setGlobalEnabled(bool enabled) {
    global_enabled_ = enabled;
}

bool InterruptController::globalEnabled() const {
    return global_enabled_;
}

void InterruptController::mask(std::uint8_t vector) {
    masks_[vector] = true;
}

void InterruptController::unmask(std::uint8_t vector) {
    masks_[vector] = false;
}

bool InterruptController::isMasked(std::uint8_t vector) const {
    return masks_[vector];
}

void InterruptController::setVectorHandler(
    std::uint8_t vector,
    std::size_t handlerAddress
) {
    vector_table_[vector] = handlerAddress;
}

void InterruptController::clearVectorHandler(std::uint8_t vector) {
    vector_table_[vector].reset();
}

bool InterruptController::hasVectorHandler(std::uint8_t vector) const {
    return vector_table_[vector].has_value();
}

std::size_t InterruptController::vectorHandler(std::uint8_t vector) const {
    const std::optional<std::size_t>& handler = vector_table_[vector];

    if (!handler.has_value()) {
        throw std::runtime_error("Interrupt vector has no handler");
    }

    return *handler;
}

void InterruptController::request(
    std::uint8_t vector,
    std::int64_t payload,
    std::string source
) {
    InterruptRequest request;
    request.vector = vector;
    request.payload = payload;
    request.source = std::move(source);

    pending_.push_back(std::move(request));
}

bool InterruptController::hasPending() const {
    return findDeliverable() != pending_.end();
}

std::size_t InterruptController::pendingCount() const {
    return pending_.size();
}

std::vector<InterruptRequest> InterruptController::pendingRequests() const {
    return std::vector<InterruptRequest>(pending_.begin(), pending_.end());
}

InterruptRequest InterruptController::acknowledge() {
    auto it = findDeliverable();

    if (it == pending_.end()) {
        throw std::runtime_error("No deliverable interrupt request");
    }

    InterruptRequest request = *it;
    pending_.erase(it);
    return request;
}

std::deque<InterruptRequest>::iterator InterruptController::findDeliverable() {
    if (!global_enabled_) {
        return pending_.end();
    }

    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (!masks_[it->vector] && hasVectorHandler(it->vector)) {
            return it;
        }
    }

    return pending_.end();
}

std::deque<InterruptRequest>::const_iterator InterruptController::findDeliverable() const {
    if (!global_enabled_) {
        return pending_.end();
    }

    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (!masks_[it->vector] && hasVectorHandler(it->vector)) {
            return it;
        }
    }

    return pending_.end();
}

} // namespace zero_cpu
