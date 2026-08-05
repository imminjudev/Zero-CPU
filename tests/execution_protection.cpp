#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/CPUState.hpp"
#include "zero_cpu/core/InterruptController.hpp"
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
#include <unordered_map>
#include <utility>
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
    const std::vector<zero_cpu::Instruction>& instructions,
    const zero_cpu::CPU::LabelTable& labels
) {
    using namespace zero_cpu;
    using namespace zero_cpu::binary;

    InstructionEncoder encoder;
    std::vector<std::uint8_t> code =
        encoder.encodeProgram(instructions, labels);

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
    const std::vector<zero_cpu::Instruction>& instructions,
    const zero_cpu::CPU::LabelTable& labels = {}
) {
    if (mode == ExecutionMode::Vector) {
        cpu.loadProgram(instructions, labels);
        return;
    }

    cpu.loadBinaryProgram(
        makeBinaryProgram(instructions, labels)
    );
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

void setUserRange(
    zero_cpu::CPU& cpu,
    ExecutionMode mode,
    std::size_t beginIndex,
    std::size_t endIndex
) {
    cpu.setUserCodeRange(
        instructionAddress(cpu, mode, beginIndex),
        instructionAddress(cpu, mode, endIndex)
    );
}

std::string expectedExecutionError(
    std::size_t address
) {
    return
        "Execution protection violation: User mode "
        "cannot execute address "
        + std::to_string(address);
}

struct ArchitecturalSnapshot {
    std::size_t pc = 0;
    std::size_t sp = 0;
    std::uint32_t flags = 0;
    zero_cpu::PrivilegeLevel privilege =
        zero_cpu::PrivilegeLevel::Kernel;
    std::vector<std::int64_t> registers;
    std::vector<std::int64_t> memory;
};

ArchitecturalSnapshot capture(
    const zero_cpu::CPU& cpu
) {
    using namespace zero_cpu;

    ArchitecturalSnapshot snapshot;
    snapshot.pc = cpu.state().pc();
    snapshot.sp = cpu.state().sp();
    snapshot.flags = cpu.state().flags().raw();
    snapshot.privilege =
        cpu.state().privilegeLevel();
    snapshot.memory = cpu.state().memory().snapshot();

    snapshot.registers.reserve(
        RegisterFile::kRegisterCount
    );

    for (std::size_t i = 0;
         i < RegisterFile::kRegisterCount;
         ++i) {
        snapshot.registers.push_back(
            cpu.state().registers().get(
                static_cast<RegisterName>(i)
            )
        );
    }

    return snapshot;
}

bool architectureMatches(
    const zero_cpu::CPU& cpu,
    const ArchitecturalSnapshot& expected
) {
    using namespace zero_cpu;

    if (
        cpu.state().pc() != expected.pc
        || cpu.state().sp() != expected.sp
        || cpu.state().flags().raw() != expected.flags
        || cpu.state().privilegeLevel()
            != expected.privilege
        || cpu.state().memory().snapshot()
            != expected.memory
    ) {
        return false;
    }

    for (std::size_t i = 0;
         i < RegisterFile::kRegisterCount;
         ++i) {
        if (
            cpu.state().registers().get(
                static_cast<RegisterName>(i)
            )
            != expected.registers[i]
        ) {
            return false;
        }
    }

    return true;
}

bool errorTraceMatches(
    const zero_cpu::CPU& cpu,
    const std::string& expectedError
) {
    if (cpu.traceLogger().size() != 1) {
        return false;
    }

    const zero_cpu::TraceEvent& trace =
        cpu.traceLogger().last();

    return trace.hasError()
        && trace.errorMessage() == expectedError;
}

void configureBranchFlags(
    zero_cpu::CPU& cpu,
    zero_cpu::Opcode opcode,
    bool taken
) {
    using namespace zero_cpu;

    cpu.state().flags().reset();

    switch (opcode) {
    case Opcode::JE:
        cpu.state().flags().setZero(taken);
        return;

    case Opcode::JNE:
        cpu.state().flags().setZero(!taken);
        return;

    case Opcode::JG:
        cpu.state().flags().setZero(!taken);
        cpu.state().flags().setSign(false);
        cpu.state().flags().setOverflow(false);
        return;

    case Opcode::JL:
        cpu.state().flags().setSign(taken);
        cpu.state().flags().setOverflow(false);
        return;

    default:
        return;
    }
}

bool defaultRangeCoversProgram(
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
            Instruction(Opcode::HALT)
        }
    );

    const std::size_t expectedBegin =
        instructionAddress(cpu, mode, 0);
    const std::size_t expectedEnd =
        instructionAddress(cpu, mode, 2);

    if (
        !cpu.hasUserCodeRange()
        || cpu.userCodeBegin() != expectedBegin
        || cpu.userCodeEndExclusive() != expectedEnd
    ) {
        detail = "default User code range mismatch";
        return false;
    }

    return true;
}

bool rangeValidationWorks(
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
            Instruction(Opcode::NOP),
            Instruction(Opcode::HALT)
        }
    );

    bool rejectedEmpty = false;
    bool rejectedOutside = false;
    bool rejectedMisaligned =
        mode == ExecutionMode::Vector;

    try {
        const std::size_t begin =
            instructionAddress(cpu, mode, 0);
        cpu.setUserCodeRange(begin, begin);
    } catch (const std::exception&) {
        rejectedEmpty = true;
    }

    try {
        cpu.setUserCodeRange(
            instructionAddress(cpu, mode, 0),
            instructionAddress(cpu, mode, 4)
        );
    } catch (const std::exception&) {
        rejectedOutside = true;
    }

    if (mode == ExecutionMode::Binary) {
        try {
            cpu.setUserCodeRange(
                cpu.binaryCodeBase() + 1,
                instructionAddress(cpu, mode, 2)
            );
        } catch (const std::exception&) {
            rejectedMisaligned = true;
        }
    }

    if (
        !rejectedEmpty
        || !rejectedOutside
        || !rejectedMisaligned
    ) {
        detail = "invalid User code range was accepted";
        return false;
    }

    setUserRange(cpu, mode, 0, 2);

    if (
        cpu.userCodeBegin()
            != instructionAddress(cpu, mode, 0)
        || cpu.userCodeEndExclusive()
            != instructionAddress(cpu, mode, 2)
    ) {
        detail = "valid User code range was not stored";
        return false;
    }

    return true;
}

bool outsideFetchDenied(
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
            Instruction(Opcode::NOP),
            Instruction(Opcode::HALT)
        }
    );
    setUserRange(cpu, mode, 0, 2);

    const std::size_t protectedAddress =
        instructionAddress(cpu, mode, 2);

    cpu.state().setPc(protectedAddress);
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    const ArchitecturalSnapshot before =
        capture(cpu);
    const std::string expectedError =
        expectedExecutionError(protectedAddress);

    cpu.step();

    if (
        !cpu.state().hasError()
        || cpu.state().errorMessage() != expectedError
        || !architectureMatches(cpu, before)
        || !errorTraceMatches(cpu, expectedError)
    ) {
        detail = "outside fetch was not denied atomically";
        return false;
    }

    return true;
}

bool branchToKernelDenied(
    ExecutionMode mode,
    zero_cpu::Opcode opcode,
    std::string& detail
) {
    using namespace zero_cpu;

    const CPU::LabelTable labels = {
        {"kernel", 3}
    };

    CPU cpu;
    loadProgram(
        cpu,
        mode,
        {
            Instruction(
                opcode,
                Operand::label("kernel")
            ),
            Instruction(Opcode::NOP),
            Instruction(Opcode::NOP),
            Instruction(Opcode::HALT)
        },
        labels
    );
    setUserRange(cpu, mode, 0, 3);

    configureBranchFlags(cpu, opcode, true);
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    const std::size_t protectedAddress =
        instructionAddress(cpu, mode, 3);
    const ArchitecturalSnapshot before =
        capture(cpu);
    const std::string expectedError =
        expectedExecutionError(protectedAddress);

    cpu.step();

    if (
        !cpu.state().hasError()
        || cpu.state().errorMessage() != expectedError
        || !architectureMatches(cpu, before)
        || !errorTraceMatches(cpu, expectedError)
    ) {
        detail =
            "branch entered Kernel code or changed state";
        return false;
    }

    return true;
}

bool conditionalNotTakenAllowed(
    ExecutionMode mode,
    zero_cpu::Opcode opcode,
    std::string& detail
) {
    using namespace zero_cpu;

    const CPU::LabelTable labels = {
        {"kernel", 3}
    };

    CPU cpu;
    loadProgram(
        cpu,
        mode,
        {
            Instruction(
                opcode,
                Operand::label("kernel")
            ),
            Instruction(Opcode::NOP),
            Instruction(Opcode::NOP),
            Instruction(Opcode::HALT)
        },
        labels
    );
    setUserRange(cpu, mode, 0, 3);

    configureBranchFlags(cpu, opcode, false);
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    cpu.step();

    if (
        cpu.state().hasError()
        || cpu.state().halted()
        || !cpu.state().isUserMode()
        || cpu.state().pc()
            != instructionAddress(cpu, mode, 1)
    ) {
        detail =
            "not-taken branch did not fall through";
        return false;
    }

    return true;
}

bool callToKernelDenied(
    ExecutionMode mode,
    std::string& detail
) {
    using namespace zero_cpu;

    const CPU::LabelTable labels = {
        {"kernel", 3}
    };

    CPU cpu;
    loadProgram(
        cpu,
        mode,
        {
            Instruction(
                Opcode::CALL,
                Operand::label("kernel")
            ),
            Instruction(Opcode::NOP),
            Instruction(Opcode::NOP),
            Instruction(Opcode::HALT)
        },
        labels
    );
    setUserRange(cpu, mode, 0, 3);
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    const std::size_t protectedAddress =
        instructionAddress(cpu, mode, 3);
    const ArchitecturalSnapshot before =
        capture(cpu);
    const std::string expectedError =
        expectedExecutionError(protectedAddress);

    cpu.step();

    if (
        !cpu.state().hasError()
        || cpu.state().errorMessage() != expectedError
        || !architectureMatches(cpu, before)
        || !errorTraceMatches(cpu, expectedError)
    ) {
        detail = "CALL changed stack before rejection";
        return false;
    }

    return true;
}

bool retToKernelDenied(
    ExecutionMode mode,
    std::string& detail
) {
    using namespace zero_cpu;

    CPU cpu;
    loadProgram(
        cpu,
        mode,
        {
            Instruction(Opcode::RET),
            Instruction(Opcode::NOP),
            Instruction(Opcode::NOP),
            Instruction(Opcode::HALT)
        }
    );
    setUserRange(cpu, mode, 0, 3);

    const std::size_t protectedAddress =
        instructionAddress(cpu, mode, 3);
    const std::size_t stackBase =
        CPUState::kDefaultStackBase;

    cpu.state().memory().write(
        stackBase,
        static_cast<std::int64_t>(protectedAddress)
    );
    cpu.state().setSp(
        stackBase + CPU::kStackSlotSize
    );
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    const ArchitecturalSnapshot before =
        capture(cpu);
    const std::string expectedError =
        expectedExecutionError(protectedAddress);

    cpu.step();

    if (
        !cpu.state().hasError()
        || cpu.state().errorMessage() != expectedError
        || !architectureMatches(cpu, before)
        || !errorTraceMatches(cpu, expectedError)
    ) {
        detail = "RET consumed stack before rejection";
        return false;
    }

    return true;
}

bool sequentialEscapeDeniedOnFetch(
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
            Instruction(Opcode::NOP)
        }
    );
    setUserRange(cpu, mode, 0, 1);
    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    cpu.step();

    const std::size_t protectedAddress =
        instructionAddress(cpu, mode, 1);

    if (
        cpu.state().hasError()
        || cpu.state().pc() != protectedAddress
        || cpu.traceLogger().size() != 1
        || cpu.traceLogger().last().hasError()
    ) {
        detail =
            "last allowed instruction did not complete";
        return false;
    }

    const ArchitecturalSnapshot beforeSecondStep =
        capture(cpu);
    const std::string expectedError =
        expectedExecutionError(protectedAddress);

    cpu.step();

    if (
        !cpu.state().hasError()
        || cpu.state().errorMessage() != expectedError
        || !architectureMatches(
            cpu,
            beforeSecondStep
        )
        || cpu.traceLogger().size() != 2
        || !cpu.traceLogger().last().hasError()
    ) {
        detail =
            "sequential escape was not denied on fetch";
        return false;
    }

    return true;
}

bool interruptRoundTripAllowed(
    ExecutionMode mode,
    bool software,
    std::string& detail
) {
    using namespace zero_cpu;

    const std::vector<Instruction> instructions = {
        software
            ? Instruction(
                Opcode::INT,
                Operand::immediate(32)
            )
            : Instruction(Opcode::NOP),
        Instruction(Opcode::NOP),
        Instruction(Opcode::NOP),
        Instruction(Opcode::IRET)
    };

    CPU cpu;
    loadProgram(cpu, mode, instructions);
    setUserRange(cpu, mode, 0, 3);

    auto controller =
        std::make_shared<InterruptController>();
    controller->setVectorHandler(
        32,
        instructionAddress(cpu, mode, 3)
    );
    cpu.setInterruptController(controller);

    cpu.state().setPrivilegeLevel(
        PrivilegeLevel::User
    );

    const std::size_t expectedReturn =
        instructionAddress(
            cpu,
            mode,
            software ? 1 : 0
        );

    if (!software) {
        controller->request(
            32,
            0,
            "execution-protection-test"
        );
    }

    cpu.step();

    if (
        cpu.state().hasError()
        || !cpu.state().isKernelMode()
        || cpu.state().pc()
            != instructionAddress(cpu, mode, 3)
    ) {
        detail = "interrupt did not enter Kernel code";
        return false;
    }

    cpu.step();

    if (
        cpu.state().hasError()
        || !cpu.state().isUserMode()
        || cpu.state().pc() != expectedReturn
    ) {
        detail =
            "IRET did not restore valid User execution";
        return false;
    }

    return true;
}

bool iretToKernelAsUserDenied(
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
            Instruction(Opcode::NOP),
            Instruction(Opcode::NOP),
            Instruction(Opcode::IRET)
        }
    );
    setUserRange(cpu, mode, 0, 3);

    const std::size_t protectedAddress =
        instructionAddress(cpu, mode, 3);
    const std::size_t stackBase =
        CPUState::kDefaultStackBase;

    cpu.state().setPc(protectedAddress);
    cpu.state().memory().write(
        stackBase,
        static_cast<std::int64_t>(protectedAddress)
    );
    cpu.state().memory().write(
        stackBase + CPU::kStackSlotSize,
        0
    );
    cpu.state().memory().write(
        stackBase + CPU::kStackSlotSize * 2,
        privilegeLevelToRaw(PrivilegeLevel::User)
    );
    cpu.state().memory().write(
        stackBase + CPU::kStackSlotSize * 3,
        static_cast<std::int64_t>(stackBase)
    );
    cpu.state().setSp(
        stackBase + CPU::kInterruptFrameSize
    );

    const ArchitecturalSnapshot before =
        capture(cpu);
    const std::string expectedError =
        expectedExecutionError(protectedAddress);

    cpu.step();

    if (
        !cpu.state().hasError()
        || cpu.state().errorMessage() != expectedError
        || !architectureMatches(cpu, before)
        || !errorTraceMatches(cpu, expectedError)
    ) {
        detail =
            "IRET consumed invalid User return frame";
        return false;
    }

    return true;
}

bool kernelOutsideRangeAllowed(
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
            Instruction(Opcode::NOP),
            Instruction(Opcode::HALT)
        }
    );
    setUserRange(cpu, mode, 0, 2);

    cpu.state().setPc(
        instructionAddress(cpu, mode, 2)
    );

    cpu.step();

    if (
        cpu.state().hasError()
        || !cpu.state().isKernelMode()
        || !cpu.state().halted()
    ) {
        detail =
            "Kernel execution was restricted by User range";
        return false;
    }

    return true;
}

} // namespace

int main() {
    using namespace zero_cpu;

    std::cout
        << "=== Zero-CPU Execution Protection Test ===\n\n";

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

    const std::vector<Opcode> branchOpcodes = {
        Opcode::JMP,
        Opcode::JE,
        Opcode::JNE,
        Opcode::JG,
        Opcode::JL
    };

    const std::vector<Opcode> conditionalOpcodes = {
        Opcode::JE,
        Opcode::JNE,
        Opcode::JG,
        Opcode::JL
    };

    for (const ExecutionMode mode : modes) {
        {
            std::string detail;
            report(
                modeName(mode)
                    + " default range covers code",
                defaultRangeCoversProgram(mode, detail),
                detail
            );
        }

        {
            std::string detail;
            report(
                modeName(mode)
                    + " range validation",
                rangeValidationWorks(mode, detail),
                detail
            );
        }

        {
            std::string detail;
            report(
                modeName(mode)
                    + " outside fetch denied",
                outsideFetchDenied(mode, detail),
                detail
            );
        }

        for (const Opcode opcode : branchOpcodes) {
            std::string detail;
            report(
                modeName(mode)
                    + " User "
                    + opcodeToString(opcode)
                    + " to Kernel denied",
                branchToKernelDenied(
                    mode,
                    opcode,
                    detail
                ),
                detail
            );
        }

        for (const Opcode opcode : conditionalOpcodes) {
            std::string detail;
            report(
                modeName(mode)
                    + " User "
                    + opcodeToString(opcode)
                    + " not-taken allowed",
                conditionalNotTakenAllowed(
                    mode,
                    opcode,
                    detail
                ),
                detail
            );
        }

        {
            std::string detail;
            report(
                modeName(mode)
                    + " User CALL to Kernel denied",
                callToKernelDenied(mode, detail),
                detail
            );
        }

        {
            std::string detail;
            report(
                modeName(mode)
                    + " User RET to Kernel denied",
                retToKernelDenied(mode, detail),
                detail
            );
        }

        {
            std::string detail;
            report(
                modeName(mode)
                    + " sequential escape denied",
                sequentialEscapeDeniedOnFetch(
                    mode,
                    detail
                ),
                detail
            );
        }

        {
            std::string detail;
            report(
                modeName(mode)
                    + " hardware interrupt entry allowed",
                interruptRoundTripAllowed(
                    mode,
                    false,
                    detail
                ),
                detail
            );
        }

        {
            std::string detail;
            report(
                modeName(mode)
                    + " software INT entry allowed",
                interruptRoundTripAllowed(
                    mode,
                    true,
                    detail
                ),
                detail
            );
        }

        {
            std::string detail;
            report(
                modeName(mode)
                    + " IRET to protected User PC denied",
                iretToKernelAsUserDenied(
                    mode,
                    detail
                ),
                detail
            );
        }

        {
            std::string detail;
            report(
                modeName(mode)
                    + " Kernel outside range allowed",
                kernelOutsideRangeAllowed(
                    mode,
                    detail
                ),
                detail
            );
        }
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Execution protection test "
               "finished successfully.\n";
        return 0;
    }

    std::cout
        << "Execution protection test failed. "
        << "Failure count: "
        << failures
        << "\n";
    return 1;
}
