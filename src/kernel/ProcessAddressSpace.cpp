#include "zero_cpu/kernel/ProcessAddressSpace.hpp"

#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/trace/TraceLogger.hpp"

#include <stdexcept>
#include <utility>

namespace zero_cpu::kernel {

ProcessAddressSpace::ProcessAddressSpace()
    : memory_(
          memory_map::kDefaultMemorySize
      ) {
    validate();
}

ProcessAddressSpace::ProcessAddressSpace(
    Memory memory
)
    : memory_(std::move(memory)) {
    validate();
}

ProcessAddressSpace::ProcessAddressSpace(
    const ProcessImage& image
)
    : memory_(image.memory),
      executable_metadata_(image.metadata),
      executable_(image.executable) {
    validateProcessImage(image);
    validate();
}

bool ProcessAddressSpace::
hasExecutableImage() const {
    return executable_.has_value();
}

const ExecutableMetadata&
ProcessAddressSpace::executableMetadata() const {
    if (!hasExecutableImage()) {
        throw std::runtime_error(
            "Process address space has no "
            "executable metadata"
        );
    }

    return *executable_metadata_;
}

const binary::BinaryProgram&
ProcessAddressSpace::executable() const {
    if (!hasExecutableImage()) {
        throw std::runtime_error(
            "Process address space has no executable"
        );
    }

    return *executable_;
}

const Memory&
ProcessAddressSpace::memory() const {
    return memory_;
}

void ProcessAddressSpace::replaceMemory(
    const Memory& memory
) {
    ProcessAddressSpace staged = *this;
    staged.memory_ = memory;
    staged.validate();

    *this = std::move(staged);
}

void ProcessAddressSpace::activate(
    CPU& cpu
) const {
    validate();

    CPU stagedCpu = cpu;

    if (hasExecutableImage()) {
        TraceLogger trace =
            stagedCpu.traceLogger();

        stagedCpu.loadBinaryProgram(
            *executable_
        );

        stagedCpu.traceLogger() =
            std::move(trace);
    }

    stagedCpu.state().memory() = memory_;
    cpu = std::move(stagedCpu);
}

void ProcessAddressSpace::validate() const {
    if (
        memory_.size()
        != memory_map::kDefaultMemorySize
    ) {
        throw std::runtime_error(
            "Process address space memory size does "
            "not match the protected memory layout"
        );
    }

    if (
        executable_metadata_.has_value()
        != executable_.has_value()
    ) {
        throw std::runtime_error(
            "Process address space executable "
            "metadata is incomplete"
        );
    }

    if (!hasExecutableImage()) {
        return;
    }

    validateExecutableMetadata(
        *executable_metadata_
    );

    const ExecutableMetadata& metadata =
        *executable_metadata_;

    const binary::BinaryProgram& program =
        *executable_;

    if (
        program.header.major_version
            != metadata.format_major_version
        || program.header.minor_version
            != metadata.format_minor_version
        || program.header.endianness
            != metadata.endianness
    ) {
        throw std::runtime_error(
            "Process address space executable header "
            "does not match metadata"
        );
    }

    if (
        static_cast<std::size_t>(
            program.header.code_size
        )
            != metadata.code_size
        || program.code.size()
            != metadata.code_size
    ) {
        throw std::runtime_error(
            "Process address space executable code "
            "size does not match metadata"
        );
    }

    if (
        static_cast<std::size_t>(
            program.header.entry_point
        )
        != metadata.entry_offset
    ) {
        throw std::runtime_error(
            "Process address space executable entry "
            "point does not match metadata"
        );
    }

    if (
        static_cast<std::size_t>(
            program.header.data_base
        )
            != metadata.data_base
        || static_cast<std::size_t>(
            program.header.data_size
        )
            != metadata.data_size
        || program.data.size()
            != metadata.data_size
    ) {
        throw std::runtime_error(
            "Process address space executable data "
            "section does not match metadata"
        );
    }

    if (
        memory_.readBytes(
            metadata.code_base,
            metadata.code_size
        )
        != program.code
    ) {
        throw std::runtime_error(
            "Process address space code memory was "
            "modified"
        );
    }
}

} // namespace zero_cpu::kernel
