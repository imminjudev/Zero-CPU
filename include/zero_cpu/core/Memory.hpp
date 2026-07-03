#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace zero_cpu {

enum class Endianness {
    Little,
    Big
};

class Memory {
public:
    static constexpr std::size_t kDefaultMemorySize = 4096;

    explicit Memory(std::size_t size = kDefaultMemorySize);

    std::size_t size() const;

    void reset();

    std::uint8_t readU8(std::size_t address) const;
    void writeU8(std::size_t address, std::uint8_t value);

    std::uint32_t readU32(
        std::size_t address,
        Endianness endian = Endianness::Little
    ) const;

    void writeU32(
        std::size_t address,
        std::uint32_t value,
        Endianness endian = Endianness::Little
    );

    std::uint64_t readU64(
        std::size_t address,
        Endianness endian = Endianness::Little
    ) const;

    void writeU64(
        std::size_t address,
        std::uint64_t value,
        Endianness endian = Endianness::Little
    );

    std::int64_t readI64(
        std::size_t address,
        Endianness endian = Endianness::Little
    ) const;

    void writeI64(
        std::size_t address,
        std::int64_t value,
        Endianness endian = Endianness::Little
    );

    std::vector<std::uint8_t> readBytes(
        std::size_t address,
        std::size_t count
    ) const;

    void writeBytes(
        std::size_t address,
        const std::vector<std::uint8_t>& values
    );

    // Compatibility API for the current interpreter-style CPU.
    // In v0.2, this treats address as a byte address and reads/writes 8 bytes.
    std::int64_t read(std::size_t address) const;
    void write(std::size_t address, std::int64_t value);

    // Compatibility API for the current TraceEvent implementation.
    // Each memory byte is widened to int64_t for snapshot comparison.
    std::vector<std::int64_t> snapshot() const;

    std::string dumpRange(
        std::size_t start,
        std::size_t count
    ) const;

    std::string dumpBytes(
        std::size_t start,
        std::size_t count
    ) const;

private:
    std::vector<std::uint8_t> bytes_;

    void checkRange(
        std::size_t address,
        std::size_t count
    ) const;
};

} // namespace zero_cpu