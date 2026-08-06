#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/kernel/ProcessImage.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"
#include "zero_cpu/kernel/ProcessState.hpp"
#include "zero_cpu/kernel/ProcessTermination.hpp"
#include "zero_cpu/system/MultiProcessRunner.hpp"

#include <cstdio>
#include <cstddef>
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

zero_cpu::kernel::ProcessImage makeImage(
    const std::string& source,
    const std::string& name
) {
    zero_cpu::Assembler assembler;

    const zero_cpu::AssembledProgram assembled =
        assembler.assembleString(source);

    zero_cpu::kernel::ProcessImageLoader loader;

    return loader.loadProgram(
        assembled.toBinaryProgram(),
        name
    );
}

const char* kFirstSource = R"ASM(
.entry start

.data
value: .qword 1

.text
start:
    LOAD R0, [value]
    ADD R0, 1
    STORE [value], R0
)ASM";

const char* kSecondSource = R"ASM(
.entry start

.data
value: .qword 10

.text
start:
    LOAD R0, [value]
    ADD R0, 5
    STORE [value], R0
)ASM";

bool preemptiveExecutionAndIsolation(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;
    using namespace zero_cpu::system;

    MultiProcessRunOptions options;
    options.quantum = 1;
    options.max_lifecycle_steps = 100;

    MultiProcessRunner runner;

    const MultiProcessRunResult result =
        runner.runImages(
            {
                makeImage(
                    kFirstSource,
                    "first.zbin"
                ),
                makeImage(
                    kSecondSource,
                    "second.zbin"
                )
            },
            options
        );

    const ProcessRunSummary& first =
        result.process(1);

    const ProcessRunSummary& second =
        result.process(2);

    if (
        !result.success()
        || result.process_count != 2
        || result.termination_count != 2
        || result.fault_count != 0
        || result.context_switch_count == 0
        || !first.terminated()
        || !second.terminated()
        || first.faulted()
        || second.faulted()
        || first.final_context.registers[
            static_cast<std::size_t>(
                RegisterName::R0
            )
        ] != 2
        || second.final_context.registers[
            static_cast<std::size_t>(
                RegisterName::R0
            )
        ] != 15
        || first.final_memory.readI64(
            memory_map::kUserDataBase
        ) != 2
        || second.final_memory.readI64(
            memory_map::kUserDataBase
        ) != 15
        || first.final_context.pc
            != first.code_end_exclusive
        || second.final_context.pc
            != second.code_end_exclusive
    ) {
        detail =
            "preemptive process execution or "
            "memory isolation mismatch";
        return false;
    }

    return true;
}

bool faultRecovery(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::kernel;
    using namespace zero_cpu::system;

    const char* faultSource = R"ASM(
.entry start
.text
start:
    DIV R0, R1
)ASM";

    const char* healthySource = R"ASM(
.entry start
.text
start:
    MOV R0, 77
)ASM";

    MultiProcessRunOptions options;
    options.quantum = 1;
    options.max_lifecycle_steps = 50;
    options.fault_exit_code = -77;

    MultiProcessRunner runner;

    const MultiProcessRunResult result =
        runner.runImages(
            {
                makeImage(
                    faultSource,
                    "fault.zbin"
                ),
                makeImage(
                    healthySource,
                    "healthy.zbin"
                )
            },
            options
        );

    const ProcessRunSummary& faulted =
        result.process(1);

    const ProcessRunSummary& healthy =
        result.process(2);

    if (
        !result.completed()
        || result.success()
        || result.termination_count != 2
        || result.fault_count != 1
        || !faulted.faulted()
        || faulted.exit_code != -77
        || faulted.termination_message.empty()
        || !healthy.terminated()
        || healthy.faulted()
        || healthy.final_context.registers[
            static_cast<std::size_t>(
                RegisterName::R0
            )
        ] != 77
    ) {
        detail =
            "faulted process did not recover "
            "to the healthy process";
        return false;
    }

    return true;
}

bool fileLoading(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::system;

    const std::string firstPath =
        "multi_process_first_test.zbin";

    const std::string secondPath =
        "multi_process_second_test.zbin";

    struct Cleanup {
        std::vector<std::string> paths;

        ~Cleanup() {
            for (const std::string& path : paths) {
                std::remove(path.c_str());
            }
        }
    } cleanup{{firstPath, secondPath}};

    Assembler assembler;
    binary::BinaryWriter writer;

    writer.writeFile(
        firstPath,
        assembler.assembleString(
            kFirstSource
        ).toBinaryProgram()
    );

    writer.writeFile(
        secondPath,
        assembler.assembleString(
            kSecondSource
        ).toBinaryProgram()
    );

    MultiProcessRunner runner;

    MultiProcessRunOptions options;
    options.quantum = 2;
    options.max_lifecycle_steps = 100;

    const MultiProcessRunResult result =
        runner.runFiles(
            {firstPath, secondPath},
            options
        );

    if (
        !result.success()
        || result.process(1).source_name
            != firstPath
        || result.process(2).source_name
            != secondPath
    ) {
        detail =
            "file-based process loading mismatch";
        return false;
    }

    return true;
}

bool stepLimit(
    std::string& detail
) {
    using namespace zero_cpu::system;

    const char* loopSource = R"ASM(
.entry loop
.text
loop:
    JMP loop
)ASM";

    MultiProcessRunOptions options;
    options.quantum = 1;
    options.max_lifecycle_steps = 5;

    MultiProcessRunner runner;

    const MultiProcessRunResult result =
        runner.runImages(
            {
                makeImage(
                    loopSource,
                    "loop.zbin"
                )
            },
            options
        );

    if (
        !result.step_limit_reached
        || result.completed()
        || result.success()
        || result.lifecycle_steps != 5
        || result.termination_count != 0
        || result.process(1).state
            != zero_cpu::kernel::ProcessState::Running
    ) {
        detail =
            "step-limit result mismatch";
        return false;
    }

    return true;
}

bool invalidInput(
    std::string& detail
) {
    using namespace zero_cpu::system;

    MultiProcessRunner runner;

    if (
        !throwsRuntimeError(
            [&] {
                (void)runner.runImages({});
            }
        )
    ) {
        detail =
            "empty process image list was accepted";
        return false;
    }

    MultiProcessRunOptions invalidQuantum;
    invalidQuantum.quantum = 0;

    if (
        !throwsRuntimeError(
            [&] {
                (void)runner.runImages(
                    {
                        makeImage(
                            kFirstSource,
                            "first.zbin"
                        )
                    },
                    invalidQuantum
                );
            }
        )
    ) {
        detail =
            "zero quantum was accepted";
        return false;
    }

    MultiProcessRunOptions invalidLimit;
    invalidLimit.max_lifecycle_steps = 0;

    if (
        !throwsRuntimeError(
            [&] {
                (void)runner.runImages(
                    {
                        makeImage(
                            kFirstSource,
                            "first.zbin"
                        )
                    },
                    invalidLimit
                );
            }
        )
    ) {
        detail =
            "zero step limit was accepted";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Multi-Process Runner "
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
            "Preemptive execution and isolation",
            preemptiveExecutionAndIsolation(
                detail
            ),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Fault recovery",
            faultRecovery(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "File loading",
            fileLoading(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Step limit",
            stepLimit(detail),
            detail
        );
    }

    {
        std::string detail;

        report(
            "Invalid input",
            invalidInput(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Multi-process runner test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Multi-process runner test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
