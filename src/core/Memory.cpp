#include "zero_cpu/core/Memory.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace zero_cpu {

Memory::Memory(std::size_t size)
    : bytes_(size, std::uint8_t{0}) {
}

std::size_t Memory::size() const {
    return bytes_.size();
}

void Memory::reset() {
    std::fill(bytes_.begin(), bytes_.end(), std::uint8_t{0});
}

std::uint8_t Memory::readU8(std::size_t address) const {
    checkRange(address, 1);

    return bytes_[address];
}

void Memory::writeU8(std::size_t address, std::uint8_t value) {
    checkRange(address, 1);

    bytes_[address] = value;
}

std::uint32_t Memory::readU32(
    std::size_t address,
    Endianness endian
) const {
    checkRange(address, 4);

    std::uint32_t value = 0;

    if (endian == Endianness::Little) {
        for (std::size_t i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(bytes_[address + i])
                     << (8 * i);
        }
    } else {
        for (std::size_t i = 0; i < 4; ++i) {
            value <<= 8;
            value |= static_cast<std::uint32_t>(bytes_[address + i]);
        }
    }

    return value;
}

void Memory::writeU32(
    std::size_t address,
    std::uint32_t value,
    Endianness endian
) {
    checkRange(address, 4);

    if (endian == Endianness::Little) {
        for (std::size_t i = 0; i < 4; ++i) {
            bytes_[address + i] =
                static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
        }
    } else {
        for (std::size_t i = 0; i < 4; ++i) {
            const std::size_t shift = 8 * (3 - i);

            bytes_[address + i] =
                static_cast<std::uint8_t>((value >> shift) & 0xFFu);
        }
    }
}

std::uint64_t Memory::readU64(
    std::size_t address,
    Endianness endian
) const {
    checkRange(address, 8);

    std::uint64_t value = 0;

    if (endian == Endianness::Little) {
        for (std::size_t i = 0; i < 8; ++i) {
            value |= static_cast<std::uint64_t>(bytes_[address + i])
                     << (8 * i);
        }
    } else {
        for (std::size_t i = 0; i < 8; ++i) {
            value <<= 8;
            value |= static_cast<std::uint64_t>(bytes_[address + i]);
        }
    }

    return value;
}

void Memory::writeU64(
    std::size_t address,
    std::uint64_t value,
    Endianness endian
) {
    checkRange(address, 8);

    if (endian == Endianness::Little) {
        for (std::size_t i = 0; i < 8; ++i) {
            bytes_[address + i] =
                static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
        }
    } else {
        for (std::size_t i = 0; i < 8; ++i) {
            const std::size_t shift = 8 * (7 - i);

            bytes_[address + i] =
                static_cast<std::uint8_t>((value >> shift) & 0xFFu);
        }
    }
}

std::int64_t Memory::readI64(
    std::size_t address,
    Endianness endian
) const {
    return static_cast<std::int64_t>(
        readU64(address, endian)
    );
}

void Memory::writeI64(
    std::size_t address,
    std::int64_t value,
    Endianness endian
) {
    writeU64(
        address,
        static_cast<std::uint64_t>(value),
        endian
    );
}

std::vector<std::uint8_t> Memory::readBytes(
    std::size_t address,
    std::size_t count
) const {
    checkRange(address, count);

    return std::vector<std::uint8_t>(
        bytes_.begin() + static_cast<std::ptrdiff_t>(address),
        bytes_.begin() + static_cast<std::ptrdiff_t>(address + count)
    );
}

void Memory::writeBytes(
    std::size_t address,
    const std::vector<std::uint8_t>& values
) {
    checkRange(address, values.size());

    std::copy(
        values.begin(),
        values.end(),
        bytes_.begin() + static_cast<std::ptrdiff_t>(address)
    );
}

std::int64_t Memory::read(std::size_t address) const {
    return readI64(address, Endianness::Little);
}

void Memory::write(std::size_t address, std::int64_t value) {
    writeI64(address, value, Endianness::Little);
}

std::vector<std::int64_t> Memory::snapshot() const {
    std::vector<std::int64_t> result;
    result.reserve(bytes_.size());

    for (std::uint8_t byte : bytes_) {
        result.push_back(static_cast<std::int64_t>(byte));
    }

    return result;
}

std::string Memory::dumpRange(
    std::size_t start,
    std::size_t count
) const {
    checkRange(start, count);

    std::ostringstream out;

    out << "[";

    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) {
            out << ", ";
        }

        const std::size_t address = start + i;

        out << address << "="
            << static_cast<int>(bytes_[address]);
    }

    out << "]";

    return out.str();
}

std::string Memory::dumpBytes(
    std::size_t start,
    std::size_t count
) const {
    checkRange(start, count);

    std::ostringstream out;

    out << std::hex << std::setfill('0');

    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) {
            out << " ";
        }

        out << std::setw(2)
            << static_cast<int>(bytes_[start + i]);
    }

    return out.str();
}

void Memory::checkRange(
    std::size_t address,
    std::size_t count
) const {
    if (address > bytes_.size()) {
        throw std::out_of_range("Memory address out of range");
    }

    if (count > bytes_.size() - address) {
        throw std::out_of_range("Memory access out of range");
    }
}

} // namespace zero_cpu