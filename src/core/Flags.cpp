#include "zero_cpu/core/Flags.hpp"

#include <sstream>

namespace zero_cpu {

Flags::Flags()
    : bits_(0) {
}

void Flags::reset() {
    bits_ = 0;
}

std::uint32_t Flags::raw() const {
    return bits_;
}

void Flags::setRaw(std::uint32_t value) {
    bits_ = value;
}

bool Flags::carry() const {
    return test(CF);
}

bool Flags::zero() const {
    return test(ZF);
}

bool Flags::sign() const {
    return test(SF);
}

bool Flags::overflow() const {
    return test(OF);
}

void Flags::setCarry(bool value) {
    set(CF, value);
}

void Flags::setZero(bool value) {
    set(ZF, value);
}

void Flags::setSign(bool value) {
    set(SF, value);
}

void Flags::setOverflow(bool value) {
    set(OF, value);
}

void Flags::updateZeroAndSign(std::int64_t result) {
    setZero(result == 0);
    setSign(result < 0);
}

std::string Flags::toString() const {
    std::ostringstream out;

    out << "ZF=" << (zero() ? 1 : 0)
        << " SF=" << (sign() ? 1 : 0)
        << " OF=" << (overflow() ? 1 : 0)
        << " CF=" << (carry() ? 1 : 0);

    return out.str();
}

bool Flags::test(std::uint32_t mask) const {
    return (bits_ & mask) != 0;
}

void Flags::set(std::uint32_t mask, bool value) {
    if (value) {
        bits_ |= mask;
    } else {
        bits_ &= ~mask;
    }
}

} // namespace zero_cpu