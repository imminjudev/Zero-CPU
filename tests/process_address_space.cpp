#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/isa/Instruction.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"
#include "zero_cpu/isa/Operand.hpp"
#include "zero_cpu/kernel/ProcessAddressSpace.hpp"
#include "zero_cpu/kernel/ProcessControlBlock.hpp"
#include "zero_cpu/kernel/ProcessDispatcher.hpp"
#include "zero_cpu/kernel/ProcessImage.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"
#include "zero_cpu/kernel/ProcessLifecycleManager.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"
#include "zero_cpu/kernel/ProcessTable.hpp"
#include "zero_cpu/kernel/ProcessTermination.hpp"
#include "zero_cpu/kernel/RoundRobinScheduler.hpp"
#include "zero_cpu/kernel/TimerPreemptiveScheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

zero_cpu::binary::BinaryProgram
makeAddProgram(
    std::int64_t r0Increment,
    std::int64_t r1Increment = 0
) {
    using namespace zero_cpu;

    const std::vector<Instruction> instructions = {
        Instruction(
            Opcode::ADD,
            Operand::registerOperand(
                RegisterName::R0
            ),
            Operand::immediate(r0Increment)
        ),
        Instruction(
            Opcode::ADD,
            Operand::registerOperand(
                RegisterName::R1
            ),
            Operand::immediate(r1Increment)
        ),
        Instruction(Opcode::NOP)
    };

    InstructionEncoder encoder;

    binary::BinaryProgram program;
    program.code = encoder.encodeProgram(
        instructions,
        {}
    );

    program.header.entry_point = 0;
    program.header.code_size =
        static_cast<std::uint32_t>(
            program.code.size()
        );

    return program;
}

zero_cpu::binary::BinaryProgram
makeFaultProgram() {
    using namespace zero_cpu;

    const std::vector<Instruction> instructions = {
        Instruction(
            Opcode::DIV,
            Operand::registerOperand(
                RegisterName::R0
            ),
            Operand::registerOperand(
                RegisterName::R1
            )
        ),
        Instruction(Opcode::NOP)
    };

    InstructionEncoder encoder;

    binary::BinaryProgram program;
    program.code = encoder.encodeProgram(
        instructions,
        {}
    );

    program.header.entry_point = 0;
    program.header.code_size =
        static_cast<std::uint32_t>(
            program.code.size()
        );

    return program;
}

bool imageProcessCreation(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessImageLoader loader;

    ProcessImage invalid =
        loader.loadProgram(
            makeAddProgram(1),
            "invalid"
        );

    invalid.initial_pc =
        invalid.metadata.code_end_exclusive;

    ProcessTable table;

    if (
        !throwsRuntimeError(
            [&] {
                (void)table.createProcess(
                    invalid
                );
            }
        )
    ) {
        detail =
            "invalid ProcessImage was accepted";
        return false;
    }

    const ProcessImage valid =
        loader.loadProgram(
            makeAddProgram(1, 10),
            "valid"
        );

    const ProcessId pid =
        table.createProcess(valid);

    const ProcessControlBlock& process =
        table.process(pid);

    if (
        pid != 1
        || process.state()
            != ProcessState::Ready
        || !process.addressSpace()
            .hasExecutableImage()
        || process.addressSpace()
            .executableMetadata().source_name
            != "valid"
        || process.context().pc
            != valid.metadata.entry_point
        || process.context().sp
            != valid.metadata.user_stack_begin
        || process.context().privilege
            != PrivilegeLevel::User
        || process.context().user_code_begin
            != valid.metadata.code_base
        || process.context()
            .user_code_end_exclusive
            != valid.metadata.code_end_exclusive
        || process.addressSpace()
            .memory().readBytes(
                valid.metadata.code_base,
                valid.metadata.code_size
            )
            != valid.executable.code
    ) {
        detail =
            "ProcessImage to PCB conversion mismatch";
        return false;
    }

    return true;
}

bool independentExecutableSwitching(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessImageLoader loader;

    const ProcessImage firstImage =
        loader.loadProgram(
            makeAddProgram(1, 10),
            "first.zbin"
        );

    const ProcessImage secondImage =
        loader.loadProgram(
            makeAddProgram(100, 1000),
            "second.zbin"
        );

    ProcessTable table;

    const ProcessId first =
        table.createProcess(firstImage);

    const ProcessId second =
        table.createProcess(secondImage);

    CPU cpu;
    RoundRobinScheduler scheduler;
    ProcessDispatcher dispatcher;

    if (
        dispatcher.dispatchNext(
            cpu,
            table,
            scheduler
        ) != first
        || !cpu.hasBinaryProgram()
        || cpu.binaryCodeSize()
            != firstImage.metadata.code_size
        || cpu.state().memory().readBytes(
            firstImage.metadata.code_base,
            firstImage.metadata.code_size
        )
            != firstImage.executable.code
    ) {
        detail =
            "first executable activation mismatch";
        return false;
    }

    cpu.step();

    cpu.state().memory().writeI64(
        memory_map::kUserDataBase,
        111
    );

    if (
        cpu.state().registers().get(
            RegisterName::R0
        ) != 1
        || cpu.state().pc()
            != firstImage.metadata.code_base
                + binary::kInstructionSize
    ) {
        detail =
            "first executable did not run";
        return false;
    }

    if (
        dispatcher.dispatchNext(
            cpu,
            table,
            scheduler
        ) != second
        || table.process(first).context()
            .registers[
                static_cast<std::size_t>(
                    RegisterName::R0
                )
            ] != 1
        || table.process(first)
            .addressSpace().memory().readI64(
                memory_map::kUserDataBase
            ) != 111
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 0
        || cpu.state().memory().readI64(
            memory_map::kUserDataBase
        ) != 0
        || cpu.state().memory().readBytes(
            secondImage.metadata.code_base,
            secondImage.metadata.code_size
        )
            != secondImage.executable.code
    ) {
        detail =
            "first-to-second image switch mismatch";
        return false;
    }

    cpu.step();

    cpu.state().memory().writeI64(
        memory_map::kUserDataBase,
        222
    );

    if (
        cpu.state().registers().get(
            RegisterName::R0
        ) != 100
    ) {
        detail =
            "second executable did not run";
        return false;
    }

    if (
        dispatcher.dispatchNext(
            cpu,
            table,
            scheduler
        ) != first
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 1
        || cpu.state().memory().readI64(
            memory_map::kUserDataBase
        ) != 111
        || cpu.state().memory().readBytes(
            firstImage.metadata.code_base,
            firstImage.metadata.code_size
        )
            != firstImage.executable.code
    ) {
        detail =
            "first executable state was not restored";
        return false;
    }

    cpu.step();

    if (
        dispatcher.dispatchNext(
            cpu,
            table,
            scheduler
        ) != second
        || table.process(first).context()
            .registers[
                static_cast<std::size_t>(
                    RegisterName::R1
                )
            ] != 10
        || cpu.state().registers().get(
            RegisterName::R0
        ) != 100
        || cpu.state().memory().readI64(
            memory_map::kUserDataBase
        ) != 222
        || table.process(second)
            .addressSpace().memory().readI64(
                memory_map::kUserDataBase
            ) != 222
    ) {
        detail =
            "second executable state was not restored";
        return false;
    }

    return true;
}

bool addressSpaceRejectsCodeMutation(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessImageLoader loader;

    const ProcessImage image =
        loader.loadProgram(
            makeAddProgram(1),
            "protected.zbin"
        );

    ProcessAddressSpace addressSpace(image);

    Memory corrupted =
        addressSpace.memory();

    const std::uint8_t original =
        corrupted.readU8(
            image.metadata.code_base
        );

    corrupted.writeU8(
        image.metadata.code_base,
        static_cast<std::uint8_t>(
            original ^ 0xFFu
        )
    );

    if (
        !throwsRuntimeError(
            [&] {
                addressSpace.replaceMemory(
                    corrupted
                );
            }
        )
    ) {
        detail =
            "modified code memory was accepted";
        return false;
    }

    if (
        addressSpace.memory().readU8(
            image.metadata.code_base
        ) != original
    ) {
        detail =
            "failed memory update was not atomic";
        return false;
    }

    return true;
}

bool lifecyclePreservesFinalMemory(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;

    ProcessImageLoader loader;

    const ProcessImage image =
        loader.loadProgram(
            makeFaultProgram(),
            "fault.zbin"
        );

    ProcessTable table;

    const ProcessId pid =
        table.createProcess(image);

    CPU cpu;
    RoundRobinScheduler scheduler;

    TimerPreemptiveScheduler preemptive(
        71,
        100
    );

    ProcessLifecycleManager lifecycle;

    if (
        lifecycle.start(
            cpu,
            table,
            scheduler,
            preemptive
        ) != ProcessRuntimeState::Running
    ) {
        detail =
            "image lifecycle did not start";
        return false;
    }

    cpu.state().memory().writeI64(
        memory_map::kUserDataBase,
        12345
    );

    const ProcessLifecycleStepResult result =
        lifecycle.step(
            cpu,
            table,
            scheduler,
            preemptive
        );

    const ProcessControlBlock& process =
        table.process(pid);

    if (
        result.state
            != ProcessRuntimeState::Completed
        || !result.process_terminated
        || !result.process_faulted
        || process.state()
            != ProcessState::Terminated
        || process.terminationKind()
            != ProcessTerminationKind::CpuFault
        || process.addressSpace()
            .memory().readI64(
                memory_map::kUserDataBase
            ) != 12345
    ) {
        detail =
            "final process memory was not preserved";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Process Address Space "
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
            "ProcessImage process creation",
            imageProcessCreation(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Independent executable switching",
            independentExecutableSwitching(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Code memory protection",
            addressSpaceRejectsCodeMutation(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Lifecycle final memory",
            lifecyclePreservesFinalMemory(
                detail
            ),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Process address space test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Process address space test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
