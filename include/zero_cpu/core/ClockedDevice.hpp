#pragma once

#include <string>

namespace zero_cpu {

class ClockedDevice {
public:
    virtual ~ClockedDevice() = default;

    virtual std::string name() const = 0;
    virtual void tick() = 0;
};

} // namespace zero_cpu