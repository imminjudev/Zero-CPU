#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/debug/DebugInspector.hpp"
#include "zero_cpu/debug/DebugSession.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

template <typename Function>
bool throwsRuntimeError(Function&& function) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return true;
    }

    return false;
}

zero_cpu::kernel::ProcessImage makeImage() {
    const char* source = R"ASM(
.entry start

.data
value: .qword 0

.text
start:
    MOV R0, 1
    ADD R0, 2
    STORE [value], R0
)ASM";

    zero_cpu::Assembler assembler;

    const zero_cpu::AssembledProgram assembled =
        assembler.assembleString(source);

    zero_cpu::kernel::ProcessImageLoader loader;

    return loader.loadProgram(
        assembled.toBinaryProgram(),
        "debug-inspector-test.zbin"
    );
}

bool registerInspection(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    const DebugStop stop =
        session.step();

    const RegisterInspection inspection =
        DebugInspector::inspectRegisters(
            session
        );

    if (
        stop.reason
            != DebugStopReason::StepComplete
        || inspection.registers[
            static_cast<std::size_t>(
                RegisterName::R0
            )
        ] != 1
        || inspection.pc
            != memory_map::kBinaryCodeBase
                + binary::kInstructionSize
        || inspection.sp
            != memory_map::kUserStackBase
        || inspection.privilege
            != PrivilegeLevel::User
        || inspection.zero
    ) {
        detail =
            "register inspection mismatch";
        return false;
    }

    return true;
}

bool memoryInspection(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    const MemoryInspection initial =
        DebugInspector::inspectMemory(
            session,
            memory_map::kUserDataBase,
            8
        );

    if (
        initial.bytes.size() != 8
        || DebugInspector::inspectQword(
            session,
            memory_map::kUserDataBase
        ) != 0
    ) {
        detail =
            "initial memory inspection mismatch";
        return false;
    }

    const DebugStop stop =
        session.continueExecution(20);

    if (
        !stop.reachedProgramEnd()
        || DebugInspector::inspectQword(
            session,
            memory_map::kUserDataBase
        ) != 3
    ) {
        detail =
            "final memory inspection mismatch";
        return false;
    }

    return true;
}

bool disassembly(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    const std::size_t second =
        memory_map::kBinaryCodeBase
        + binary::kInstructionSize;

    (void)session.addBreakpoint(second);

    const auto instructions =
        DebugInspector::disassemble(
            session,
            memory_map::kBinaryCodeBase,
            3
        );

    if (
        instructions.size() != 3
        || instructions[0].address
            != memory_map::kBinaryCodeBase
        || instructions[0].text
            != "MOV R0, 1"
        || !instructions[0].current_pc
        || instructions[0].breakpoint
        || instructions[1].address
            != second
        || instructions[1].text
            != "ADD R0, 2"
        || instructions[1].current_pc
        || !instructions[1].breakpoint
        || instructions[2].text
            != "STORE [0], R0"
    ) {
        detail =
            "instruction disassembly mismatch";
        return false;
    }

    return true;
}

bool formatting(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    const std::string registers =
        DebugInspector::formatRegisters(
            DebugInspector::inspectRegisters(
                session
            )
        );

    const std::string memory =
        DebugInspector::formatMemory(
            DebugInspector::inspectMemory(
                session,
                0,
                8
            )
        );

    const std::string disassemblyText =
        DebugInspector::formatDisassembly(
            DebugInspector::disassemble(
                session,
                memory_map::kBinaryCodeBase,
                3
            )
        );

    if (
        registers.find("R0=0")
            == std::string::npos
        || registers.find("PC=512")
            == std::string::npos
        || registers.find("PRIV=User")
            == std::string::npos
        || memory.find("Memory[0..8):")
            == std::string::npos
        || disassemblyText.find(
            "512: MOV R0, 1"
        ) == std::string::npos
        || disassemblyText.find(
            "536: ADD R0, 2"
        ) == std::string::npos
    ) {
        detail =
            "debug inspection formatting mismatch";
        return false;
    }

    return true;
}

bool invalidInspection(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    if (
        !throwsRuntimeError(
            [&] {
                (void)DebugInspector::inspectMemory(
                    session,
                    0,
                    0
                );
            }
        )
        || !throwsRuntimeError(
            [&] {
                (void)DebugInspector::inspectMemory(
                    session,
                    memory_map::kDefaultMemorySize
                        - 4,
                    8
                );
            }
        )
        || !throwsRuntimeError(
            [&] {
                (void)DebugInspector::disassemble(
                    session,
                    memory_map::kBinaryCodeBase
                        + 1,
                    1
                );
            }
        )
        || !throwsRuntimeError(
            [&] {
                (void)DebugInspector::disassemble(
                    session,
                    session.metadata()
                        .code_end_exclusive,
                    1
                );
            }
        )
        || !throwsRuntimeError(
            [&] {
                (void)DebugInspector::disassemble(
                    session,
                    memory_map::kBinaryCodeBase,
                    0
                );
            }
        )
    ) {
        detail =
            "invalid inspection request was accepted";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Debug Inspector "
           "Test ===\n\n";

    int failures = 0;

    auto report = [&](
        const std::string& name,
        bool passed,
        const std::string& detail
    ) {
        std::cout
            << (passed ? "[PASS] " : "[FAIL] ")
            << name
            << "\n";

        if (!passed) {
            std::cout
                << "       "
                << detail
                << "\n";

            ++failures;
        }
    };

    {
        std::string detail;

        report(
            "Register inspection",
            registerInspection(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Memory inspection",
            memoryInspection(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Instruction disassembly",
            disassembly(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Inspection formatting",
            formatting(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Invalid inspection rejection",
            invalidInspection(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Debug inspector test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Debug inspector test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
