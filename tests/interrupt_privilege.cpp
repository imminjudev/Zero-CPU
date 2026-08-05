#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/PrivilegeLevel.hpp"
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

enum class InterruptKind {
    Hardware,
    Software
};

std::string modeName(ExecutionMode mode) {
    return mode == ExecutionMode::Vector
        ? "vector"
        : "binary";
}

std::string kindName(InterruptKind kind) {
    return kind == InterruptKind::Hardware
        ? "hardware"
        : "software";
}

zero_cpu::binary::BinaryProgram makeBinaryProgram(
    const std::vector<zero_cpu::Instruction>& instructions
) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code =
        encoder.encodeProgram(instructions, {});

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

void loadProgram(
    zero_cpu::CPU& cpu,
    ExecutionMode mode,
    const std::vector<zero_cpu::Instruction>& instructions
) {
    if (mode == ExecutionMode::Vector) {
        cpu.loadProgram(instructions, {});
        return;
    }

    cpu.loadBinaryProgram(makeBinaryProgram(instructions));
}

std::size_t instructionAddress(
    const zero_cpu::CPU& cpu,
    ExecutionMode mode,
    std::size_t index
) {
    if (mode == ExecutionMode::Vector) {
        return index;
    }

    return cpu.binaryCodeBase()
        + index * zero_cpu::binary::kInstructionSize;
}

bool privilegeEquals(
    const zero_cpu::CPU& cpu,
    zero_cpu::PrivilegeLevel expected
) {
    return cpu.state().privilegeLevel() == expected;
}

std::int64_t packedInterruptFlags(
    const zero_cpu::Flags& flags
) {
    std::int64_t value = 0;

    if (flags.zero()) {
        value |= 1LL << 0;
    }

    if (flags.sign()) {
        value |= 1LL << 1;
    }

    if (flags.carry()) {
        value |= 1LL << 2;
    }

    if (flags.overflow()) {
        value |= 1LL << 3;
    }

    return value;
}

bool interruptRoundTrip(
    ExecutionMode mode,
    InterruptKind kind,
    zero_cpu::PrivilegeLevel startingPrivilege,
    std::string& detail
) {
    using namespace zero_cpu;

    const std::vector<Instruction> instructions =
        kind == InterruptKind::Hardware
            ? std::vector<Instruction>{
                Instruction(Opcode::NOP),
                Instruction(Opcode::IRET)
            }
            : std::vector<Instruction>{
                Instruction(
                    Opcode::INT,
                    Operand::immediate(32)
                ),
                Instruction(Opcode::NOP),
                Instruction(Opcode::IRET)
            };

    const std::size_t handlerIndex =
        kind == InterruptKind::Hardware ? 1 : 2;

    CPU cpu;
    loadProgram(cpu, mode, instructions);

    auto controller =
        std::make_shared<InterruptController>();
    controller->setVectorHandler(
        32,
        instructionAddress(cpu, mode, handlerIndex)
    );
    cpu.setInterruptController(controller);

    cpu.state().flags().setZero(true);
    cpu.state().flags().setSign(false);
    cpu.state().flags().setCarry(true);
    cpu.state().flags().setOverflow(true);
    cpu.state().setPrivilegeLevel(startingPrivilege);

    const std::size_t stackBase =
        CPUState::kDefaultStackBase;
    const std::size_t originalPc = cpu.state().pc();
    const std::uint32_t originalFlags =
        cpu.state().flags().raw();
    const std::int64_t expectedSavedFlags =
        packedInterruptFlags(cpu.state().flags());

    const std::size_t expectedReturnPc =
        kind == InterruptKind::Hardware
            ? originalPc
            : instructionAddress(cpu, mode, 1);

    if (kind == InterruptKind::Hardware) {
        controller->request(32, 77, "privilege-test");
    }

    cpu.step();

    if (cpu.state().hasError()) {
        detail = "entry failed: " + cpu.state().errorMessage();
        return false;
    }

    if (!cpu.state().isKernelMode()) {
        detail = "handler did not enter Kernel mode";
        return false;
    }

    if (
        cpu.state().pc()
        != instructionAddress(cpu, mode, handlerIndex)
    ) {
        detail = "handler PC mismatch";
        return false;
    }

    if (
        cpu.state().sp()
        != stackBase + CPU::kInterruptFrameSize
    ) {
        detail = "interrupt frame size mismatch";
        return false;
    }

    if (
        cpu.state().memory().read(stackBase)
        != static_cast<std::int64_t>(expectedReturnPc)
    ) {
        detail = "saved return address mismatch";
        return false;
    }

    if (
        cpu.state().memory().read(
            stackBase + CPU::kStackSlotSize
        )
        != expectedSavedFlags
    ) {
        detail = "saved FLAGS mismatch";
        return false;
    }

    if (
        cpu.state().memory().read(
            stackBase + CPU::kStackSlotSize * 2
        )
        != privilegeLevelToRaw(startingPrivilege)
    ) {
        detail = "saved privilege mismatch";
        return false;
    }

    if (cpu.traceLogger().size() != 1) {
        detail = "entry trace count mismatch";
        return false;
    }

    const TraceEvent& entryTrace =
        cpu.traceLogger().last();

    if (
        entryTrace.before().privilegeLevel()
            != startingPrivilege
        || !entryTrace.after().isKernelMode()
        || entryTrace.hasError()
    ) {
        detail = "entry trace privilege mismatch";
        return false;
    }

    cpu.state().flags().reset();
    cpu.step();

    if (cpu.state().hasError()) {
        detail = "IRET failed: " + cpu.state().errorMessage();
        return false;
    }

    if (!privilegeEquals(cpu, startingPrivilege)) {
        detail = "IRET restored wrong privilege";
        return false;
    }

    if (cpu.state().pc() != expectedReturnPc) {
        detail = "IRET restored wrong PC";
        return false;
    }

    if (cpu.state().sp() != stackBase) {
        detail = "IRET did not restore SP";
        return false;
    }

    if (cpu.state().flags().raw() != originalFlags) {
        detail = "IRET did not restore FLAGS";
        return false;
    }

    if (cpu.traceLogger().size() != 2) {
        detail = "IRET trace count mismatch";
        return false;
    }

    const TraceEvent& iretTrace =
        cpu.traceLogger().last();

    if (
        !iretTrace.before().isKernelMode()
        || iretTrace.after().privilegeLevel()
            != startingPrivilege
        || iretTrace.hasError()
    ) {
        detail = "IRET trace privilege mismatch";
        return false;
    }

    return true;
}

bool nestedInterruptRoundTrip(
    ExecutionMode mode,
    std::string& detail
) {
    using namespace zero_cpu;

    CPU cpu;
    loadProgram(
        cpu,
        mode,
        {
            Instruction(Opcode::NOP),
            Instruction(Opcode::IRET),
            Instruction(Opcode::IRET)
        }
    );

    auto controller =
        std::make_shared<InterruptController>();

    controller->setVectorHandler(
        40,
        instructionAddress(cpu, mode, 1)
    );
    controller->setVectorHandler(
        41,
        instructionAddress(cpu, mode, 2)
    );
    cpu.setInterruptController(controller);

    const std::size_t stackBase =
        CPUState::kDefaultStackBase;
    const std::size_t originalPc = cpu.state().pc();

    cpu.state().setPrivilegeLevel(PrivilegeLevel::User);
    controller->request(40, 1, "outer");
    cpu.step();

    if (
        cpu.state().hasError()
        || !cpu.state().isKernelMode()
        || cpu.state().sp()
            != stackBase + CPU::kInterruptFrameSize
    ) {
        detail = "outer interrupt entry failed";
        return false;
    }

    controller->request(41, 2, "inner");
    cpu.step();

    if (
        cpu.state().hasError()
        || !cpu.state().isKernelMode()
        || cpu.state().sp()
            != stackBase + CPU::kInterruptFrameSize * 2
    ) {
        detail = "inner interrupt entry failed";
        return false;
    }

    const std::size_t outerPrivilegeAddress =
        stackBase + CPU::kStackSlotSize * 2;
    const std::size_t innerPrivilegeAddress =
        stackBase
        + CPU::kInterruptFrameSize
        + CPU::kStackSlotSize * 2;

    if (
        cpu.state().memory().read(outerPrivilegeAddress)
            != privilegeLevelToRaw(PrivilegeLevel::User)
        || cpu.state().memory().read(innerPrivilegeAddress)
            != privilegeLevelToRaw(PrivilegeLevel::Kernel)
    ) {
        detail = "nested saved privilege order mismatch";
        return false;
    }

    cpu.step();

    if (
        cpu.state().hasError()
        || !cpu.state().isKernelMode()
        || cpu.state().pc()
            != instructionAddress(cpu, mode, 1)
        || cpu.state().sp()
            != stackBase + CPU::kInterruptFrameSize
    ) {
        detail = "inner IRET did not restore Kernel frame";
        return false;
    }

    cpu.step();

    if (
        cpu.state().hasError()
        || !cpu.state().isUserMode()
        || cpu.state().pc() != originalPc
        || cpu.state().sp() != stackBase
    ) {
        detail = "outer IRET did not restore User frame";
        return false;
    }

    return true;
}

bool directUserIretIsDenied(
    ExecutionMode mode,
    std::string& detail
) {
    using namespace zero_cpu;

    CPU cpu;
    loadProgram(
        cpu,
        mode,
        {Instruction(Opcode::IRET)}
    );

    const std::size_t stackBase =
        CPUState::kDefaultStackBase;
    const std::size_t returnPc = cpu.state().pc();

    cpu.state().memory().write(
        stackBase,
        static_cast<std::int64_t>(returnPc)
    );
    cpu.state().memory().write(
        stackBase + CPU::kStackSlotSize,
        0
    );
    cpu.state().memory().write(
        stackBase + CPU::kStackSlotSize * 2,
        privilegeLevelToRaw(PrivilegeLevel::User)
    );
    cpu.state().setSp(
        stackBase + CPU::kInterruptFrameSize
    );
    cpu.state().setPrivilegeLevel(PrivilegeLevel::User);

    const std::size_t spBefore = cpu.state().sp();
    cpu.step();

    const std::string expected =
        "Privilege violation: IRET requires Kernel mode";

    if (
        !cpu.state().hasError()
        || !cpu.state().halted()
        || cpu.state().errorMessage() != expected
    ) {
        detail = "direct User IRET was not denied";
        return false;
    }

    if (
        !cpu.state().isUserMode()
        || cpu.state().sp() != spBefore
        || cpu.state().pc() != returnPc
    ) {
        detail = "denied IRET changed architectural state";
        return false;
    }

    if (
        cpu.traceLogger().size() != 1
        || !cpu.traceLogger().last().before().isUserMode()
        || !cpu.traceLogger().last().after().isUserMode()
        || !cpu.traceLogger().last().hasError()
    ) {
        detail = "denied IRET trace mismatch";
        return false;
    }

    return true;
}

bool invalidPrivilegeFrameIsAtomic(
    ExecutionMode mode,
    std::string& detail
) {
    using namespace zero_cpu;

    CPU cpu;
    loadProgram(
        cpu,
        mode,
        {Instruction(Opcode::IRET)}
    );

    const std::size_t stackBase =
        CPUState::kDefaultStackBase;
    const std::size_t pcBefore = cpu.state().pc();

    cpu.state().flags().setZero(true);
    const std::uint8_t flagsBefore =
        cpu.state().flags().raw();

    cpu.state().memory().write(
        stackBase,
        static_cast<std::int64_t>(pcBefore)
    );
    cpu.state().memory().write(
        stackBase + CPU::kStackSlotSize,
        0
    );
    cpu.state().memory().write(
        stackBase + CPU::kStackSlotSize * 2,
        7
    );
    cpu.state().setSp(
        stackBase + CPU::kInterruptFrameSize
    );

    const std::size_t spBefore = cpu.state().sp();
    cpu.step();

    if (
        !cpu.state().hasError()
        || cpu.state().errorMessage()
            != "Invalid saved privilege level: 7"
    ) {
        detail = "invalid privilege frame was not rejected";
        return false;
    }

    if (
        !cpu.state().isKernelMode()
        || cpu.state().pc() != pcBefore
        || cpu.state().sp() != spBefore
        || cpu.state().flags().raw() != flagsBefore
    ) {
        detail = "invalid frame was partially consumed";
        return false;
    }

    return true;
}

bool interruptOverflowIsAtomic(std::string& detail) {
    using namespace zero_cpu;

    CPU cpu;
    cpu.loadProgram(
        {
            Instruction(Opcode::NOP),
            Instruction(Opcode::IRET)
        },
        {}
    );

    auto controller =
        std::make_shared<InterruptController>();
    controller->setVectorHandler(50, 1);
    cpu.setInterruptController(controller);

    const std::size_t memorySize =
        cpu.state().memory().size();
    const std::size_t spBefore =
        memorySize - CPU::kStackSlotSize * 2;
    const std::size_t pcBefore = cpu.state().pc();

    cpu.state().setSp(spBefore);
    cpu.state().setPrivilegeLevel(PrivilegeLevel::User);

    const std::int64_t firstBefore =
        cpu.state().memory().read(spBefore);
    const std::int64_t secondBefore =
        cpu.state().memory().read(
            spBefore + CPU::kStackSlotSize
        );

    controller->request(50, 0, "overflow");
    cpu.step();

    if (
        !cpu.state().hasError()
        || cpu.state().errorMessage() != "Stack overflow"
    ) {
        detail = "interrupt frame overflow was not rejected";
        return false;
    }

    if (
        !cpu.state().isUserMode()
        || cpu.state().sp() != spBefore
        || cpu.state().pc() != pcBefore
        || cpu.state().memory().read(spBefore)
            != firstBefore
        || cpu.state().memory().read(
            spBefore + CPU::kStackSlotSize
        ) != secondBefore
    ) {
        detail = "overflow partially wrote interrupt frame";
        return false;
    }

    return true;
}

} // namespace

int main() {
    using namespace zero_cpu;

    std::cout
        << "=== Zero-CPU Interrupt Privilege Test ===\n\n";

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

    const std::vector<ExecutionMode> modes = {
        ExecutionMode::Vector,
        ExecutionMode::Binary
    };

    const std::vector<InterruptKind> kinds = {
        InterruptKind::Hardware,
        InterruptKind::Software
    };

    for (const ExecutionMode mode : modes) {
        for (const InterruptKind kind : kinds) {
            std::string detail;
            const bool passed = interruptRoundTrip(
                mode,
                kind,
                PrivilegeLevel::User,
                detail
            );

            report(
                modeName(mode)
                    + " "
                    + kindName(kind)
                    + " User->Kernel->User",
                passed,
                detail
            );
        }

        {
            std::string detail;
            const bool passed = interruptRoundTrip(
                mode,
                InterruptKind::Hardware,
                PrivilegeLevel::Kernel,
                detail
            );

            report(
                modeName(mode)
                    + " hardware Kernel->Kernel->Kernel",
                passed,
                detail
            );
        }

        {
            std::string detail;
            const bool passed =
                nestedInterruptRoundTrip(mode, detail);

            report(
                modeName(mode)
                    + " nested privilege restoration",
                passed,
                detail
            );
        }

        {
            std::string detail;
            const bool passed =
                directUserIretIsDenied(mode, detail);

            report(
                modeName(mode)
                    + " direct User IRET denied",
                passed,
                detail
            );
        }

        {
            std::string detail;
            const bool passed =
                invalidPrivilegeFrameIsAtomic(mode, detail);

            report(
                modeName(mode)
                    + " invalid frame is atomic",
                passed,
                detail
            );
        }
    }

    {
        std::string detail;
        const bool passed = interruptOverflowIsAtomic(detail);

        report(
            "hardware interrupt frame overflow is atomic",
            passed,
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Interrupt privilege test "
               "finished successfully.\n";
        return 0;
    }

    std::cout
        << "Interrupt privilege test failed. "
        << "Failure count: "
        << failures
        << "\n";
    return 1;
}
