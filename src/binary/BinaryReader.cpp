#include "zero_cpu/binary/BinaryReader.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>

namespace zero_cpu {

namespace binary {

BinaryProgram BinaryReader::readFile(
    const std::string& path
) const {
    std::ifstream input(
        path,
        std::ios::binary | std::ios::ate
    );

    if (!input) {
        throw std::runtime_error(
            "Failed to open binary file for reading: "
            + path
        );
    }

    const std::streamsize fileSize =
        input.tellg();

    if (fileSize < 0) {
        throw std::runtime_error(
            "Failed to determine binary file size: "
            + path
        );
    }

    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(fileSize)
    );

    if (!bytes.empty()) {
        input.read(
            reinterpret_cast<char*>(
                bytes.data()
            ),
            fileSize
        );
    }

    if (!input && fileSize > 0) {
        throw std::runtime_error(
            "Failed to read binary file: "
            + path
        );
    }

    return readFromBytes(bytes);
}

BinaryProgram BinaryReader::readFromBytes(
    const std::vector<std::uint8_t>& bytes
) const {
    if (bytes.size() < kLegacyHeaderSize) {
        throw std::runtime_error(
            "Binary file is smaller than "
            "the minimum header size"
        );
    }

    validateMagic(bytes);

    BinaryProgram program;
    program.header = readHeader(bytes);

    validateHeader(program.header);

    const std::size_t headerSize =
        headerSizeForVersion(
            program.header.major_version,
            program.header.minor_version
        );

    const std::size_t codeSize =
        static_cast<std::size_t>(
            program.header.code_size
        );

    const std::size_t dataSize =
        static_cast<std::size_t>(
            program.header.data_size
        );

    if (
        codeSize
        > std::numeric_limits<std::size_t>::max()
            - headerSize
    ) {
        throw std::runtime_error(
            "Binary file size overflow"
        );
    }

    const std::size_t codeEnd =
        headerSize + codeSize;

    if (
        dataSize
        > std::numeric_limits<std::size_t>::max()
            - codeEnd
    ) {
        throw std::runtime_error(
            "Binary file size overflow"
        );
    }

    const std::size_t expectedSize =
        codeEnd + dataSize;

    if (bytes.size() != expectedSize) {
        throw std::runtime_error(
            "Binary file size does not match "
            "section sizes in header"
        );
    }

    program.code.assign(
        bytes.begin()
            + static_cast<std::ptrdiff_t>(
                headerSize
            ),
        bytes.begin()
            + static_cast<std::ptrdiff_t>(
                codeEnd
            )
    );

    program.data.assign(
        bytes.begin()
            + static_cast<std::ptrdiff_t>(
                codeEnd
            ),
        bytes.end()
    );

    return program;
}

BinaryHeader BinaryReader::readHeader(
    const std::vector<std::uint8_t>& bytes
) const {
    BinaryHeader header;

    header.major_version =
        bytes[kMajorVersionOffset];

    header.minor_version =
        bytes[kMinorVersionOffset];

    const std::size_t headerSize =
        headerSizeForVersion(
            header.major_version,
            header.minor_version
        );

    if (headerSize == 0) {
        throw std::runtime_error(
            "Unsupported binary version"
        );
    }

    if (bytes.size() < headerSize) {
        throw std::runtime_error(
            "Binary file is smaller than "
            "its versioned header"
        );
    }

    const auto rawEndianness =
        bytes[kEndiannessOffset];

    if (
        rawEndianness
        == static_cast<std::uint8_t>(
            BinaryEndianness::Little
        )
    ) {
        header.endianness =
            BinaryEndianness::Little;
    } else if (
        rawEndianness
        == static_cast<std::uint8_t>(
            BinaryEndianness::Big
        )
    ) {
        header.endianness =
            BinaryEndianness::Big;
    } else {
        throw std::runtime_error(
            "Unsupported binary endianness"
        );
    }

    if (bytes[kReservedOffset] != 0) {
        throw std::runtime_error(
            "Invalid binary header: "
            "reserved byte must be zero"
        );
    }

    header.entry_point = readU32(
        bytes,
        kEntryPointOffset,
        header.endianness
    );

    header.code_size = readU32(
        bytes,
        kCodeSizeOffset,
        header.endianness
    );

    if (
        isCurrentVersion(
            header.major_version,
            header.minor_version
        )
    ) {
        header.data_base = readU32(
            bytes,
            kDataBaseOffset,
            header.endianness
        );

        header.data_size = readU32(
            bytes,
            kDataSizeOffset,
            header.endianness
        );
    } else {
        header.data_base = 0;
        header.data_size = 0;
    }

    return header;
}

std::uint32_t BinaryReader::readU32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    BinaryEndianness endian
) const {
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error(
            "readU32 out of range"
        );
    }

    std::uint32_t value = 0;

    if (endian == BinaryEndianness::Little) {
        for (std::size_t i = 0; i < 4; ++i) {
            value |=
                static_cast<std::uint32_t>(
                    bytes[offset + i]
                )
                << (8 * i);
        }

        return value;
    }

    for (std::size_t i = 0; i < 4; ++i) {
        value <<= 8;
        value |= static_cast<std::uint32_t>(
            bytes[offset + i]
        );
    }

    return value;
}

void BinaryReader::validateMagic(
    const std::vector<std::uint8_t>& bytes
) const {
    if (bytes.size() < kMagic.size()) {
        throw std::runtime_error(
            "Binary file is too small to contain "
            "magic number"
        );
    }

    for (
        std::size_t i = 0;
        i < kMagic.size();
        ++i
    ) {
        if (bytes[i] != kMagic[i]) {
            throw std::runtime_error(
                "Invalid binary magic number"
            );
        }
    }
}

void BinaryReader::validateHeader(
    const BinaryHeader& header
) const {
    if (
        !isSupportedVersion(
            header.major_version,
            header.minor_version
        )
    ) {
        throw std::runtime_error(
            "Unsupported binary version"
        );
    }

    if (
        !isValidInstructionAddress(
            header.entry_point
        )
    ) {
        throw std::runtime_error(
            "Entry point is not "
            "instruction-aligned"
        );
    }

    if (
        header.code_size
        % kInstructionSize
        != 0
    ) {
        throw std::runtime_error(
            "Code size is not a multiple "
            "of instruction size"
        );
    }

    if (
        header.entry_point
        >= header.code_size
    ) {
        throw std::runtime_error(
            "Entry point is outside code section"
        );
    }

    if (
        isLegacyVersion(
            header.major_version,
            header.minor_version
        )
        && (
            header.data_base != 0
            || header.data_size != 0
        )
    ) {
        throw std::runtime_error(
            "Legacy binary contains data metadata"
        );
    }
}

} // namespace binary

} // namespace zero_cpu
