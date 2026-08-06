#include "zero_cpu/binary/BinaryWriter.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>

namespace zero_cpu {

namespace binary {

void BinaryWriter::writeFile(
    const std::string& path,
    const BinaryProgram& program
) const {
    const std::vector<std::uint8_t> bytes =
        writeToBytes(program);

    std::ofstream output(
        path,
        std::ios::binary
    );

    if (!output) {
        throw std::runtime_error(
            "Failed to open binary file for writing: "
            + path
        );
    }

    output.write(
        reinterpret_cast<const char*>(
            bytes.data()
        ),
        static_cast<std::streamsize>(
            bytes.size()
        )
    );

    if (!output) {
        throw std::runtime_error(
            "Failed to write binary file: "
            + path
        );
    }
}

std::vector<std::uint8_t>
BinaryWriter::writeToBytes(
    const BinaryProgram& program
) const {
    if (
        !isSupportedVersion(
            program.header.major_version,
            program.header.minor_version
        )
    ) {
        throw std::runtime_error(
            "Cannot write unsupported binary version"
        );
    }

    if (
        program.code.size()
        > std::numeric_limits<std::uint32_t>::max()
        || program.data.size()
            > std::numeric_limits<std::uint32_t>::max()
    ) {
        throw std::runtime_error(
            "Binary section exceeds 32-bit size field"
        );
    }

    BinaryHeader header = program.header;

    header.code_size =
        static_cast<std::uint32_t>(
            program.code.size()
        );

    header.data_size =
        static_cast<std::uint32_t>(
            program.data.size()
        );

    if (
        isLegacyVersion(
            header.major_version,
            header.minor_version
        )
    ) {
        if (!program.data.empty()) {
            throw std::runtime_error(
                "Legacy binary format cannot "
                "contain a data section"
            );
        }

        header.data_base = 0;
        header.data_size = 0;
    }

    const std::size_t headerSize =
        headerSizeForVersion(
            header.major_version,
            header.minor_version
        );

    std::vector<std::uint8_t> output;

    output.reserve(
        headerSize
        + program.code.size()
        + program.data.size()
    );

    writeHeader(output, header);

    output.insert(
        output.end(),
        program.code.begin(),
        program.code.end()
    );

    output.insert(
        output.end(),
        program.data.begin(),
        program.data.end()
    );

    return output;
}

void BinaryWriter::writeHeader(
    std::vector<std::uint8_t>& output,
    const BinaryHeader& header
) const {
    output.insert(
        output.end(),
        kMagic.begin(),
        kMagic.end()
    );

    output.push_back(header.major_version);
    output.push_back(header.minor_version);

    output.push_back(
        static_cast<std::uint8_t>(
            header.endianness
        )
    );

    output.push_back(0);

    writeU32(
        output,
        header.entry_point,
        header.endianness
    );

    writeU32(
        output,
        header.code_size,
        header.endianness
    );

    if (
        isCurrentVersion(
            header.major_version,
            header.minor_version
        )
    ) {
        writeU32(
            output,
            header.data_base,
            header.endianness
        );

        writeU32(
            output,
            header.data_size,
            header.endianness
        );
    }
}

void BinaryWriter::writeU32(
    std::vector<std::uint8_t>& output,
    std::uint32_t value,
    BinaryEndianness endian
) const {
    if (endian == BinaryEndianness::Little) {
        for (
            std::size_t i = 0;
            i < 4;
            ++i
        ) {
            output.push_back(
                static_cast<std::uint8_t>(
                    (value >> (8 * i))
                    & 0xFFu
                )
            );
        }

        return;
    }

    for (
        std::size_t i = 0;
        i < 4;
        ++i
    ) {
        const std::size_t shift =
            8 * (3 - i);

        output.push_back(
            static_cast<std::uint8_t>(
                (value >> shift)
                & 0xFFu
            )
        );
    }
}

} // namespace binary

} // namespace zero_cpu
