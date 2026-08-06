#pragma once

#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/isa/Instruction.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace zero_cpu {

struct AssembledProgram {
    std::vector<Instruction> instructions;

    // Code labels use instruction indices so they remain
    // compatible with vector execution and InstructionEncoder.
    std::unordered_map<
        std::string,
        std::size_t
    > labels;

    // Data labels contain absolute process-memory addresses.
    std::unordered_map<
        std::string,
        std::size_t
    > data_labels;

    std::size_t data_base = 0;
    std::vector<std::uint8_t> data;

    bool has_explicit_entry = false;
    std::string entry_label;
    std::size_t entry_instruction = 0;

    std::size_t resolvedEntryInstruction() const;

    binary::BinaryProgram toBinaryProgram() const;

    binary::BinaryProgram toBinaryProgram(
        std::size_t entryInstruction
    ) const;
};

class Assembler {
public:
    AssembledProgram assembleFile(
        const std::string& path
    ) const;

    AssembledProgram assembleString(
        const std::string& source
    ) const;
};

} // namespace zero_cpu
