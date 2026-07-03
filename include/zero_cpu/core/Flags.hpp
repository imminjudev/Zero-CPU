#pragma once

#include <cstdint>
#include <string>

namespace zero_cpu {

class Flags {
public:
    enum Bit : std::uint32_t {
        CF = 1u << 0,
        ZF = 1u << 6,
        SF = 1u << 7,
        OF = 1u << 11
    };

    Flags();

    void reset();

    std::uint32_t raw() const;
    void setRaw(std::uint32_t value);

    bool carry() const;
    bool zero() const;
    bool sign() const;
    bool overflow() const;

    void setCarry(bool value);
    void setZero(bool value);
    void setSign(bool value);
    void setOverflow(bool value);

    void updateZeroAndSign(std::int64_t result);

    std::string toString() const;

private:
    std::uint32_t bits_;

    bool test(std::uint32_t mask) const;
    void set(std::uint32_t mask, bool value);
};

} // namespace zero_cpu