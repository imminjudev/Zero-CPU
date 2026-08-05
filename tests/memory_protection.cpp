#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/core/Flags.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/MMIODevice.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/PrivilegeLevel.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"
#include "zero_cpu/trace/TraceEvent.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

enum class ExecutionMode {
    Vector,
    Binary
};

enum class AccessKind {
    Read,
    Write
};

enum class AddressMode {
    Direct,
    Indirect
};

std::string modeName(ExecutionMode mode) {
    return mode == ExecutionMode::Vector
        ? "vector"
        : "binary";
}

std::string accessName(AccessKind kind) {
    return kind == AccessKind::Read
        ? "read"
        : "write";
}

std::string addressModeName(AddressMode mode) {
    return mode == AddressMode::Direct
        ? "direct"
        : "indirect";
}

class CountingMMIODevice final
    : public zero_cpu::MMIODevice {
public:
    std::string name() const override {
        return "CountingMMIODevice";
    }

    std::int64_t read(std::size_t offset) override {
        ++read_count_;
        last_offset_ = offset;
        return read_value_;
    }

    void write(
        std::size_t offset,
        std::int64_t value
    ) override {
        ++write_count_;
        last_offset_ = offset;
        last_write_value_ = value;
    }

    void setReadValue(std::int64_t value) {
        read_value_ = value;
    }

    std::size_t readCount() const {
        return read_count_;
    }

    std::size_t writeCount() const {
        return write_count_;
    }

    std::size_t lastOffset() const {
        return last_offset_;
    }

    std::int64_t lastWriteValue() const {
        return last_write_value_;
    }

private:
    std::int64_t read_value_ = 0;
    std::size_t read_count_ = 0;
    std::size_t write_count_ = 0;
    std::size_t last_offset_ = 0;
    std::int64_t last_write_value_ = 0;
};

zero_cpu::Instruction makeAccessInstruction(
    AccessKind access,
    AddressMode addressMode,
    std::size_t address
) {
    using namespace zero_cpu;

    const Operand memoryOperand =
        addressMode == AddressMode::Direct
            ? Operand::memoryAddress(address)
            : Operand::registerIndirectAddress(
                RegisterName::R1
            );

    if (access == AccessKind::Read) {
        return Instruction(
            Opcode::LOAD,
            Operand::registerOperand(RegisterName::R2),
            memoryOperand
        );
    }

    return Instruction(
        Opcode::STORE,
        memoryOperand,
        Operand::registerOperand(RegisterName::R2)
    );
}

zero_cpu::binary::BinaryProgram makeBinaryProgram(
    const zero_cpu::Instruction& instruction
) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code =
        encoder.encodeProgram({instruction}, {});

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size =
        static_cast<std::uint32_t>(code.size());
    program.code = std::move(code);
    return program;
}

void loadInstruction(
    zero_cpu::CPU& cpu,
    ExecutionMode mode,
    const zero_cpu::Instruction& instruction
) {
    if (mode == ExecutionMode::Vector) {
        cpu.loadProgram({instruction}, {});
        return;
    }

    cpu.loadBinaryProgram(makeBinaryProgram(instruction));
}

std::size_t expectedNextPc(
    const zero_cpu::CPU& cpu,
    ExecutionMode mode
) {
    if (mode == ExecutionMode::Vector) {
        return 1;
    }

    return cpu.binaryCodeBase()
        + zero_cpu::binary::kInstructionSize;
}

std::vector<std::int64_t> captureRegisters(
    const zero_cpu::CPU& cpu
) {
    using namespace zero_cpu;

    std::vector<std::int64_t> values;
    values.reserve(RegisterFile::kRegisterCount);

    for (std::size_t i = 0;
         i < RegisterFile::kRegisterCount;
         ++i) {
        values.push_back(
            cpu.state().registers().get(
                static_cast<RegisterName>(i)
            )
        );
    }

    return values;
}

bool registersEqual(
    const zero_cpu::CPU& cpu,
    const std::vector<std::int64_t>& expected
) {
    using namespace zero_cpu;

    if (expected.size() != RegisterFile::kRegisterCount) {
        return false;
    }

    for (std::size_t i = 0;
         i < RegisterFile::kRegisterCount;
         ++i) {
        if (
            cpu.state().registers().get(
                static_cast<RegisterName>(i)
            )
            != expected[i]
        ) {
            return false;
        }
    }

    return true;
}

struct PreparedCPU {
    zero_cpu::CPU cpu;
    std::shared_ptr<zero_cpu::MMIOBus> bus;
    std::shared_ptr<CountingMMIODevice> device;
};

PreparedCPU prepareCpu(
    ExecutionMode mode,
    AccessKind access,
    AddressMode addressMode,
    std::size_t address
) {
    using namespace zero_cpu;

    PreparedCPU prepared;

    prepared.bus = std::make_shared<MMIOBus>();
    prepared.device =
        std::make_shared<CountingMMIODevice>();

    prepared.bus->mapDevice(
        memory_map::kDebugOutputBase,
        memory_map::kDebugOutputSize,
        prepared.device
    );
    prepared.cpu.setMMIOBus(prepared.bus);

    loadInstruction(
        prepared.cpu,
        mode,
        makeAccessInstruction(
            access,
            addressMode,
            address
        )
    );

    prepared.cpu.state().registers().set(
        RegisterName::R1,
        static_cast<std::int64_t>(address)
    );
    prepared.cpu.state().registers().set(
        RegisterName::R2,
        0x12345678
    );

    prepared.cpu.state().flags().setZero(true);
    prepared.cpu.state().flags().setSign(false);
    prepared.cpu.state().flags().setCarry(true);
    prepared.cpu.state().flags().setOverflow(true);

    return prepared;
}

bool userLowMemoryAllowed(
    ExecutionMode mode,
    AccessKind access,
    AddressMode addressMode,
    std::size_t address,
    std::string& detail
) {
    using namespace zero_cpu;

    PreparedCPU prepared = prepareCpu(
        mode,
        access,
        addressMode,
        address
    );
    CPU& cpu = prepared.cpu;

    constexpr std::int64_t kReadValue = -91;
    constexpr std::int64_t kWriteValue = 0x12345678;

    cpu.state().memory().write(address, kReadValue);
    cpu.state().setPrivilegeLevel(PrivilegeLevel::User);

    cpu.step();

    if (cpu.state().hasError() || cpu.state().halted()) {
        detail = "allowed access failed: "
            + cpu.state().errorMessage();
        return false;
    }

    if (!cpu.state().isUserMode()) {
        detail = "allowed access changed privilege";
        return false;
    }

    if (cpu.state().pc() != expectedNextPc(cpu, mode)) {
        detail = "allowed access did not advance PC";
        return false;
    }

    if (access == AccessKind::Read) {
        if (
            cpu.state().registers().get(RegisterName::R2)
            != kReadValue
        ) {
            detail = "LOAD returned the wrong value";
            return false;
        }
    } else {
        if (cpu.state().memory().read(address) != kWriteValue) {
            detail = "STORE wrote the wrong value";
            return false;
        }
    }

    if (
        prepared.device->readCount() != 0
        || prepared.device->writeCount() != 0
    ) {
        detail = "ordinary memory touched MMIO";
        return false;
    }

    if (
        cpu.traceLogger().size() != 1
        || !cpu.traceLogger().last().before().isUserMode()
        || !cpu.traceLogger().last().after().isUserMode()
        || cpu.traceLogger().last().hasError()
    ) {
        detail = "allowed access trace mismatch";
        return false;
    }

    return true;
}

bool userProtectedAccessDenied(
    ExecutionMode mode,
    AccessKind access,
    AddressMode addressMode,
    std::size_t address,
    std::string& detail
) {
    using namespace zero_cpu;

    PreparedCPU prepared = prepareCpu(
        mode,
        access,
        addressMode,
        address
    );
    CPU& cpu = prepared.cpu;

    prepared.device->setReadValue(777);
    cpu.state().setPrivilegeLevel(PrivilegeLevel::User);

    const std::size_t pcBefore = cpu.state().pc();
    const std::size_t spBefore = cpu.state().sp();
    const std::uint32_t flagsBefore =
        cpu.state().flags().raw();

    const std::vector<std::int64_t> registersBefore =
        captureRegisters(cpu);
    const std::vector<std::int64_t> memoryBefore =
        cpu.state().memory().snapshot();

    const std::string expectedError =
        "Memory protection violation: User mode cannot "
        + accessName(access)
        + " address "
        + std::to_string(address);

    cpu.step();

    if (
        !cpu.state().hasError()
        || !cpu.state().halted()
        || cpu.state().errorMessage() != expectedError
    ) {
        detail = "unexpected error: "
            + cpu.state().errorMessage();
        return false;
    }

    if (!cpu.state().isUserMode()) {
        detail = "denied access changed privilege";
        return false;
    }

    if (
        cpu.state().pc() != pcBefore
        || cpu.state().sp() != spBefore
        || cpu.state().flags().raw() != flagsBefore
        || !registersEqual(cpu, registersBefore)
        || cpu.state().memory().snapshot() != memoryBefore
    ) {
        detail = "denied access changed architectural state";
        return false;
    }

    if (
        prepared.device->readCount() != 0
        || prepared.device->writeCount() != 0
    ) {
        detail = "denied access reached MMIO device";
        return false;
    }

    if (cpu.traceLogger().size() != 1) {
        detail = "expected one error trace";
        return false;
    }

    const TraceEvent& trace = cpu.traceLogger().last();

    if (
        !trace.before().isUserMode()
        || !trace.after().isUserMode()
        || !trace.hasError()
        || trace.errorMessage() != expectedError
    ) {
        detail = "denied access trace mismatch";
        return false;
    }

    const std::size_t traceCount =
        cpu.traceLogger().size();

    cpu.step();

    if (
        cpu.traceLogger().size() != traceCount
        || cpu.state().pc() != pcBefore
        || cpu.state().sp() != spBefore
        || cpu.state().errorMessage() != expectedError
    ) {
        detail = "terminal error state changed";
        return false;
    }

    return true;
}

bool kernelMemoryAllowed(
    ExecutionMode mode,
    AccessKind access,
    AddressMode addressMode,
    std::string& detail
) {
    using namespace zero_cpu;

    const std::size_t address =
        CPUState::kDefaultStackBase;

    PreparedCPU prepared = prepareCpu(
        mode,
        access,
        addressMode,
        address
    );
    CPU& cpu = prepared.cpu;

    constexpr std::int64_t kReadValue = -1234;
    constexpr std::int64_t kWriteValue = 0x12345678;

    cpu.state().memory().write(address, kReadValue);
    cpu.step();

    if (cpu.state().hasError() || cpu.state().halted()) {
        detail = "Kernel memory access failed: "
            + cpu.state().errorMessage();
        return false;
    }

    if (!cpu.state().isKernelMode()) {
        detail = "Kernel memory access changed privilege";
        return false;
    }

    if (access == AccessKind::Read) {
        if (
            cpu.state().registers().get(RegisterName::R2)
            != kReadValue
        ) {
            detail = "Kernel LOAD returned wrong value";
            return false;
        }
    } else {
        if (cpu.state().memory().read(address) != kWriteValue) {
            detail = "Kernel STORE wrote wrong value";
            return false;
        }
    }

    return true;
}

bool kernelMmioAllowed(
    ExecutionMode mode,
    AccessKind access,
    AddressMode addressMode,
    std::string& detail
) {
    using namespace zero_cpu;

    const std::size_t address =
        memory_map::kDebugOutputBase;

    PreparedCPU prepared = prepareCpu(
        mode,
        access,
        addressMode,
        address
    );
    CPU& cpu = prepared.cpu;

    constexpr std::int64_t kReadValue = -777;
    constexpr std::int64_t kWriteValue = 0x12345678;

    prepared.device->setReadValue(kReadValue);
    cpu.step();

    if (cpu.state().hasError() || cpu.state().halted()) {
        detail = "Kernel MMIO access failed: "
            + cpu.state().errorMessage();
        return false;
    }

    if (access == AccessKind::Read) {
        if (
            prepared.device->readCount() != 1
            || prepared.device->writeCount() != 0
            || prepared.device->lastOffset() != 0
            || cpu.state().registers().get(RegisterName::R2)
                != kReadValue
        ) {
            detail = "Kernel MMIO read mismatch";
            return false;
        }
    } else {
        if (
            prepared.device->readCount() != 0
            || prepared.device->writeCount() != 1
            || prepared.device->lastOffset() != 0
            || prepared.device->lastWriteValue()
                != kWriteValue
        ) {
            detail = "Kernel MMIO write mismatch";
            return false;
        }
    }

    return true;
}

} // namespace

int main() {
    using namespace zero_cpu;

    std::cout
        << "=== Zero-CPU Memory Protection Test ===\n\n";

    int failures = 0;

    auto report = [&](
        const std::string& name,
        bool passed,
        const std::string& detail
    ) {
        std::cout << (passed ? "[PASS] " : "[FAIL] ")
                  << name
                  << "\n";

        if (!passed) {
            std::cout << "       " << detail << "\n";
            ++failures;
        }
    };

    const std::vector<ExecutionMode> executionModes = {
        ExecutionMode::Vector,
        ExecutionMode::Binary
    };

    const std::vector<AccessKind> accessKinds = {
        AccessKind::Read,
        AccessKind::Write
    };

    const std::vector<AddressMode> addressModes = {
        AddressMode::Direct,
        AddressMode::Indirect
    };

    const std::vector<std::pair<std::string, std::size_t>>
        protectedAddresses = {
            {
                "crosses user boundary",
                memory_map::kUserDataEndExclusive - 7
            },
            {
                "code boundary",
                memory_map::kBinaryCodeBase
            },
            {
                "kernel stack",
                CPUState::kDefaultStackBase
            },
            {
                "MMIO",
                memory_map::kDebugOutputBase
            }
        };

    for (const ExecutionMode executionMode
         : executionModes) {
        for (const AccessKind access : accessKinds) {
            for (const AddressMode addressMode
                 : addressModes) {
                for (
                    const std::size_t allowedAddress
                    : {
                        std::size_t{100},
                        memory_map::kUserDataEndExclusive - 8
                    }
                ) {
                    std::string detail;
                    const bool passed =
                        userLowMemoryAllowed(
                            executionMode,
                            access,
                            addressMode,
                            allowedAddress,
                            detail
                        );

                    report(
                        modeName(executionMode)
                            + " User "
                            + addressModeName(addressMode)
                            + " "
                            + accessName(access)
                            + " allowed @"
                            + std::to_string(allowedAddress),
                        passed,
                        detail
                    );
                }

                for (const auto& protectedAddress
                     : protectedAddresses) {
                    std::string detail;
                    const bool passed =
                        userProtectedAccessDenied(
                            executionMode,
                            access,
                            addressMode,
                            protectedAddress.second,
                            detail
                        );

                    report(
                        modeName(executionMode)
                            + " User "
                            + addressModeName(addressMode)
                            + " "
                            + accessName(access)
                            + " denied: "
                            + protectedAddress.first,
                        passed,
                        detail
                    );
                }

                {
                    std::string detail;
                    const bool passed =
                        kernelMemoryAllowed(
                            executionMode,
                            access,
                            addressMode,
                            detail
                        );

                    report(
                        modeName(executionMode)
                            + " Kernel "
                            + addressModeName(addressMode)
                            + " "
                            + accessName(access)
                            + " protected memory allowed",
                        passed,
                        detail
                    );
                }

                {
                    std::string detail;
                    const bool passed =
                        kernelMmioAllowed(
                            executionMode,
                            access,
                            addressMode,
                            detail
                        );

                    report(
                        modeName(executionMode)
                            + " Kernel "
                            + addressModeName(addressMode)
                            + " "
                            + accessName(access)
                            + " MMIO allowed",
                        passed,
                        detail
                    );
                }
            }
        }
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Memory protection test "
               "finished successfully.\n";
        return 0;
    }

    std::cout
        << "Memory protection test failed. "
        << "Failure count: "
        << failures
        << "\n";
    return 1;
}
