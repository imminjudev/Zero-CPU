#pragma once

#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/core/Memory.hpp"
#include "zero_cpu/kernel/ExecutableMetadata.hpp"
#include "zero_cpu/kernel/ProcessImage.hpp"

#include <optional>

namespace zero_cpu {

class CPU;

} // namespace zero_cpu

namespace zero_cpu::kernel {

class ProcessAddressSpace {
public:
    ProcessAddressSpace();

    explicit ProcessAddressSpace(
        Memory memory
    );

    explicit ProcessAddressSpace(
        const ProcessImage& image
    );

    bool hasExecutableImage() const;

    const ExecutableMetadata&
    executableMetadata() const;

    const binary::BinaryProgram&
    executable() const;

    const Memory& memory() const;

    void replaceMemory(
        const Memory& memory
    );

    void activate(
        CPU& cpu
    ) const;

private:
    Memory memory_;

    std::optional<ExecutableMetadata>
        executable_metadata_;

    std::optional<binary::BinaryProgram>
        executable_;

    void validate() const;
};

} // namespace zero_cpu::kernel
