#include "zero_cpu/binary/BinaryWriter.hpp"

#include "zero_cpu/binary/BinaryFormat.hpp"

#include <fstream>
#include <stdexcept>

namespace zero_cpu {

namespace binary {

void BinaryWriter::writeFile(
    const std::string& path,
    const BinaryProgram& program
) const {
    const std::vector<std::uint8_t> bytes = writeToBytes(program);

    std::ofstream output(path, std::ios::binary);

    if (!output) {
        throw std::runtime_error("Failed to open binary file for writing: " + path);
    }

    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    if (!output) {
        throw std::runtime_error("Failed to write binary file: " + path);
    }
}

std::vector<std::uint8_t> BinaryWriter::writeToBytes(
    const BinaryProgram& program
) const {
    BinaryHeader header = program.header;
    header.code_size = static_cast<std::uint32_t>(program.code.size());

    std::vector<std::uint8_t> output;
    output.reserve(kHeaderSize + program.code.size());

    writeHeader(output, header);

    output.insert(
        output.end(),
        program.code.begin(),
        program.code.end()
    );

    return output;
}

void BinaryWriter::writeHeader(
    std::vector<std::uint8_t>& output,
    const BinaryHeader& header
) const {
    output.insert(output.end(), kMagic.begin(), kMagic.end());

    output.push_back(header.major_version);
    output.push_back(header.minor_version);
    output.push_back(static_cast<std::uint8_t>(header.endianness));
    output.push_back(0);

    writeU32(output, header.entry_point, header.endianness);
    writeU32(output, header.code_size, header.endianness);
}

void BinaryWriter::writeU32(
    std::vector<std::uint8_t>& output,
    std::uint32_t value,
    BinaryEndianness endian
) const {
    if (endian == BinaryEndianness::Little) {
        for (std::size_t i = 0; i < 4; ++i) {
            output.push_back(
                static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu)
            );
        }

        return;
    }

    for (std::size_t i = 0; i < 4; ++i) {
        const std::size_t shift = 8 * (3 - i);

        output.push_back(
            static_cast<std::uint8_t>((value >> shift) & 0xFFu)
        );
    }
}

} // namespace binary

} // namespace zero_cpu