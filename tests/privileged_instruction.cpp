#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/PrivilegeLevel.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"
#include "zero_cpu/isa/Opcode.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

enum class ExecutionMode {
    Vector,
    Binary
};

std::string modeName(ExecutionMode mode) {
    return mode == ExecutionMode::Vector
        ? "vector"
        : "binary";
}

zero_cpu::binary::BinaryProgram makeBinaryProgram(
    zero_cpu::Opcode opcode
) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    InstructionEncoder encoder;
    const std::vector<std::uint8_t> code =
        encoder.encodeProgram(
            {Instruction(opcode)},
            {}
        );

    BinaryProgram program;
    program.header.major_version = kMajorVersion;
    program.header.minor_version = kMinorVersion;
    program.header.endianness = BinaryEndianness::Little;
    program.header.entry_point = 0;
    program.header.code_size =
        static_cast<std::uint32_t>(code.size());
    program.code = code;
    return program;
}

void loadSingleInstruction(
    zero_cpu::CPU& cpu,
    zero_cpu::Opcode opcode,
    ExecutionMode mode
) {
    using namespace zero_cpu;

    if (mode == ExecutionMode::Vector) {
        cpu.loadProgram({Instruction(opcode)}, {});
        return;
    }

    cpu.loadBinaryProgram(makeBinaryProgram(opcode));
}

std::size_t expectedAdvance(
    const zero_cpu::CPU& cpu,
    ExecutionMode mode
) {
    if (mode == ExecutionMode::Vector) {
        return 1;
    }

    return cpu.binaryCodeBase()
        + zero_cpu::binary::kInstructionSize;
}

bool registerStateEquals(
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
        const RegisterName reg =
            static_cast<RegisterName>(i);

        if (cpu.state().registers().get(reg) != expected[i]) {
            return false;
        }
    }

    return true;
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

bool kernelInstructionIsAllowed(
    zero_cpu::Opcode opcode,
    ExecutionMode mode,
    std::string& detail
) {
    using namespace zero_cpu;

    CPU cpu;
    loadSingleInstruction(cpu, opcode, mode);

    auto controller =
        std::make_shared<InterruptController>();

    const bool initialInterruptState =
        opcode == Opcode::EI ? false : true;

    controller->setGlobalEnabled(initialInterruptState);
    cpu.setInterruptController(controller);

    cpu.step();

    if (cpu.state().hasError()) {
        detail = cpu.state().errorMessage();
        return false;
    }

    if (!cpu.state().isKernelMode()) {
        detail = "privilege changed out of Kernel mode";
        return false;
    }

    if (cpu.traceLogger().size() != 1) {
        detail = "expected exactly one trace event";
        return false;
    }

    const TraceEvent& event = cpu.traceLogger().last();

    if (
        !event.before().isKernelMode()
        || !event.after().isKernelMode()
    ) {
        detail = "trace did not preserve Kernel privilege";
        return false;
    }

    if (event.hasError()) {
        detail = "trace unexpectedly contains an error";
        return false;
    }

    if (opcode == Opcode::HALT) {
        if (!cpu.state().halted()) {
            detail = "HALT did not halt in Kernel mode";
            return false;
        }

        if (controller->globalEnabled() != initialInterruptState) {
            detail = "HALT changed interrupt state";
            return false;
        }

        return true;
    }

    if (cpu.state().halted()) {
        detail = "EI/DI unexpectedly halted the CPU";
        return false;
    }

    if (cpu.state().pc() != expectedAdvance(cpu, mode)) {
        detail = "EI/DI did not advance PC";
        return false;
    }

    const bool expectedInterruptState =
        opcode == Opcode::EI;

    if (
        controller->globalEnabled()
        != expectedInterruptState
    ) {
        detail = "EI/DI did not update interrupt state";
        return false;
    }

    return true;
}

bool userInstructionIsDenied(
    zero_cpu::Opcode opcode,
    ExecutionMode mode,
    std::string& detail
) {
    using namespace zero_cpu;

    CPU cpu;
    loadSingleInstruction(cpu, opcode, mode);

    auto controller =
        std::make_shared<InterruptController>();

    const bool initialInterruptState =
        opcode == Opcode::EI ? false : true;

    controller->setGlobalEnabled(initialInterruptState);
    cpu.setInterruptController(controller);

    cpu.state().registers().set(RegisterName::R1, 123);
    cpu.state().memory().write(96, 456);
    cpu.state().flags().setZero(true);
    cpu.state().flags().setSign(true);
    cpu.state().setPrivilegeLevel(PrivilegeLevel::User);

    const std::size_t pcBefore = cpu.state().pc();
    const std::size_t spBefore = cpu.state().sp();
    const std::uint8_t flagsBefore =
        cpu.state().flags().raw();
    const std::int64_t memoryBefore =
        cpu.state().memory().read(96);
    const std::vector<std::int64_t> registersBefore =
        captureRegisters(cpu);

    cpu.step();

    const std::string expectedError =
        "Privilege violation: "
        + opcodeToString(opcode)
        + " requires Kernel mode";

    if (!cpu.state().hasError()) {
        detail = "instruction did not produce an error";
        return false;
    }

    if (!cpu.state().halted()) {
        detail = "privilege error did not halt the CPU";
        return false;
    }

    if (cpu.state().errorMessage() != expectedError) {
        detail = "unexpected error: "
            + cpu.state().errorMessage();
        return false;
    }

    if (!cpu.state().isUserMode()) {
        detail = "privilege violation changed CPU mode";
        return false;
    }

    if (
        cpu.state().pc() != pcBefore
        || cpu.state().sp() != spBefore
        || cpu.state().flags().raw() != flagsBefore
        || cpu.state().memory().read(96) != memoryBefore
        || !registerStateEquals(cpu, registersBefore)
    ) {
        detail =
            "architectural state changed before rejection";
        return false;
    }

    if (
        controller->globalEnabled()
        != initialInterruptState
    ) {
        detail = "denied instruction changed interrupt state";
        return false;
    }

    if (cpu.traceLogger().size() != 1) {
        detail = "expected exactly one error trace";
        return false;
    }

    const TraceEvent& event = cpu.traceLogger().last();

    if (
        !event.before().isUserMode()
        || !event.after().isUserMode()
    ) {
        detail = "trace did not preserve User privilege";
        return false;
    }

    if (
        !event.hasError()
        || event.errorMessage() != expectedError
    ) {
        detail = "trace error did not match CPU error";
        return false;
    }

    const std::size_t traceCountAfterError =
        cpu.traceLogger().size();

    cpu.step();

    if (
        cpu.traceLogger().size() != traceCountAfterError
        || cpu.state().pc() != pcBefore
        || cpu.state().sp() != spBefore
        || cpu.state().errorMessage() != expectedError
        || !cpu.state().isUserMode()
        || controller->globalEnabled()
            != initialInterruptState
    ) {
        detail = "terminal error state changed after step";
        return false;
    }

    return true;
}

bool userNopIsAllowed(
    ExecutionMode mode,
    std::string& detail
) {
    using namespace zero_cpu;

    CPU cpu;
    loadSingleInstruction(cpu, Opcode::NOP, mode);
    cpu.state().setPrivilegeLevel(PrivilegeLevel::User);

    cpu.step();

    if (cpu.state().hasError() || cpu.state().halted()) {
        detail = "NOP was rejected in User mode";
        return false;
    }

    if (!cpu.state().isUserMode()) {
        detail = "NOP changed privilege mode";
        return false;
    }

    if (cpu.state().pc() != expectedAdvance(cpu, mode)) {
        detail = "NOP did not advance PC";
        return false;
    }

    if (cpu.traceLogger().size() != 1) {
        detail = "expected exactly one NOP trace";
        return false;
    }

    const TraceEvent& event = cpu.traceLogger().last();

    if (
        !event.before().isUserMode()
        || !event.after().isUserMode()
        || event.hasError()
    ) {
        detail = "NOP trace privilege/error mismatch";
        return false;
    }

    return true;
}

} // namespace

int main() {
    using namespace zero_cpu;

    std::cout
        << "=== Zero-CPU Privileged Instruction Test ===\n\n";

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

    const std::vector<Opcode> privilegedOpcodes = {
        Opcode::HALT,
        Opcode::EI,
        Opcode::DI
    };

    const std::vector<ExecutionMode> modes = {
        ExecutionMode::Vector,
        ExecutionMode::Binary
    };

    for (const ExecutionMode mode : modes) {
        for (const Opcode opcode : privilegedOpcodes) {
            std::string kernelDetail;
            const bool kernelPassed =
                kernelInstructionIsAllowed(
                    opcode,
                    mode,
                    kernelDetail
                );

            report(
                modeName(mode)
                    + " Kernel "
                    + opcodeToString(opcode)
                    + " allowed",
                kernelPassed,
                kernelDetail
            );

            std::string userDetail;
            const bool userPassed =
                userInstructionIsDenied(
                    opcode,
                    mode,
                    userDetail
                );

            report(
                modeName(mode)
                    + " User "
                    + opcodeToString(opcode)
                    + " denied",
                userPassed,
                userDetail
            );
        }

        std::string nopDetail;
        const bool nopPassed =
            userNopIsAllowed(mode, nopDetail);

        report(
            modeName(mode) + " User NOP allowed",
            nopPassed,
            nopDetail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Privileged instruction test "
               "finished successfully.\n";
        return 0;
    }

    std::cout
        << "Privileged instruction test failed. "
        << "Failure count: "
        << failures
        << "\n";
    return 1;
}
