#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/debug/MultiProcessDebugConsole.hpp"
#include "zero_cpu/debug/MultiProcessDebugSession.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include <iostream>
#include <sstream>
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

    const auto assembled =
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

std::vector<zero_cpu::kernel::ProcessImage>
normalImages() {
    return {
        makeImage(
            kFirstSource,
            "first.zbin"
        ),
        makeImage(
            kSecondSource,
            "second.zbin"
        )
    };
}

zero_cpu::debug::MultiProcessDebugOptions
debugOptions() {
    zero_cpu::debug::MultiProcessDebugOptions
        options;

    options.quantum = 1;
    options.default_continue_steps = 100;

    return options;
}

bool initialState(std::string& detail) {
    using namespace zero_cpu::debug;
    using namespace zero_cpu::kernel;

    MultiProcessDebugSession session(
        normalImages(),
        debugOptions()
    );

    const auto snapshots =
        session.processSnapshots();

    if (
        !session.started()
        || session.runtimeState()
            != ProcessRuntimeState::Running
        || session.runningPid() != 1
        || session.selectedPid() != 1
        || session.totalSteps() != 0
        || snapshots.size() != 2
        || snapshots[0].pid != 1
        || snapshots[0].state
            != ProcessState::Running
        || !snapshots[0].running
        || snapshots[1].pid != 2
        || snapshots[1].state
            != ProcessState::Ready
        || snapshots[1].running
    ) {
        detail =
            "initial multi-process debug state mismatch";
        return false;
    }

    return true;
}

bool schedulerObservation(
    std::string& detail
) {
    using namespace zero_cpu::debug;

    MultiProcessDebugSession session(
        normalImages(),
        debugOptions()
    );

    for (
        std::size_t index = 0;
        index < 12
        && session.contextSwitches().empty();
        ++index
    ) {
        (void)session.step();
    }

    session.selectProcess(2);

    const ProcessDebugSnapshot selected =
        session.selectedProcessSnapshot();

    if (
        session.preemptionCount() == 0
        || session.schedulerContextSwitchCount()
            == 0
        || session.contextSwitches().empty()
        || selected.pid != 2
        || selected.context.pid != 2
    ) {
        detail =
            "scheduler or PID observation mismatch";
        return false;
    }

    return true;
}

bool completionAndIsolation(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;
    using namespace zero_cpu::kernel;

    MultiProcessDebugSession session(
        normalImages(),
        debugOptions()
    );

    std::size_t stops = 0;

    while (
        session.runtimeState()
            == ProcessRuntimeState::Running
        && stops < 10
    ) {
        const MultiProcessDebugStop stop =
            session.continueExecution(100);

        if (
            stop.reason
                == MultiProcessDebugStopReason::
                    ProcessTerminated
            || stop.reason
                == MultiProcessDebugStopReason::
                    ProcessFaulted
        ) {
            ++stops;
        }
    }

    const ProcessDebugSnapshot first =
        session.processSnapshot(1);

    const ProcessDebugSnapshot second =
        session.processSnapshot(2);

    if (
        session.runtimeState()
            != ProcessRuntimeState::Completed
        || !first.terminated()
        || !second.terminated()
        || first.faulted()
        || second.faulted()
        || first.context.registers[
            static_cast<std::size_t>(
                RegisterName::R0
            )
        ] != 2
        || second.context.registers[
            static_cast<std::size_t>(
                RegisterName::R0
            )
        ] != 15
        || first.memory.readI64(
            memory_map::kUserDataBase
        ) != 2
        || second.memory.readI64(
            memory_map::kUserDataBase
        ) != 15
    ) {
        detail =
            "completion or process isolation mismatch";
        return false;
    }

    return true;
}

bool faultRecovery(std::string& detail) {
    using namespace zero_cpu::debug;
    using namespace zero_cpu::kernel;

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

    MultiProcessDebugOptions options =
        debugOptions();

    options.fault_exit_code = -77;

    MultiProcessDebugSession session(
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

    bool sawFault = false;

    while (
        session.runtimeState()
            == ProcessRuntimeState::Running
    ) {
        const MultiProcessDebugStop stop =
            session.continueExecution(50);

        if (
            stop.reason
            == MultiProcessDebugStopReason::
                ProcessFaulted
        ) {
            sawFault = true;
        }
    }

    const ProcessDebugSnapshot faulted =
        session.processSnapshot(1);

    const ProcessDebugSnapshot healthy =
        session.processSnapshot(2);

    if (
        !sawFault
        || !faulted.faulted()
        || faulted.exit_code != -77
        || !healthy.terminated()
        || healthy.faulted()
        || healthy.context.registers[0] != 77
    ) {
        detail =
            "fault recovery debug state mismatch";
        return false;
    }

    return true;
}

bool consoleWorkflow(std::string& detail) {
    using namespace zero_cpu::debug;

    MultiProcessDebugSession session(
        normalImages(),
        debugOptions()
    );

    std::istringstream input(
        "processes\n"
        "scheduler\n"
        "step 2\n"
        "process 2\n"
        "registers\n"
        "memory 0 8\n"
        "continue 100\n"
        "continue 100\n"
        "processes\n"
        "scheduler\n"
        "quit\n"
    );

    std::ostringstream output;
    std::ostringstream error;

    MultiProcessDebugConsoleOptions options;
    options.show_prompt = false;
    options.print_banner = false;
    options.default_continue_steps = 100;

    MultiProcessDebugConsole console(
        session,
        input,
        output,
        error,
        options
    );

    const MultiProcessDebugConsoleResult result =
        console.run();

    const std::string text =
        output.str();

    if (
        !result.success()
        || !result.quit_requested
        || result.command_count != 11
        || !error.str().empty()
        || text.find("Processes:")
            == std::string::npos
        || text.find("Selected PID 2.")
            == std::string::npos
        || text.find("Registers for PID 2:")
            == std::string::npos
        || text.find("PID 2 Memory[0..8):")
            == std::string::npos
        || text.find("Scheduler:")
            == std::string::npos
        || text.find("Observed switches:")
            == std::string::npos
        || text.find("ProcessTerminated")
            == std::string::npos
    ) {
        detail =
            "multi-process console workflow mismatch";
        return false;
    }

    return true;
}

bool invalidInput(std::string& detail) {
    using namespace zero_cpu::debug;

    MultiProcessDebugOptions invalidQuantum;
    invalidQuantum.quantum = 0;

    if (
        !throwsRuntimeError(
            [&] {
                MultiProcessDebugSession session(
                    normalImages(),
                    invalidQuantum
                );
            }
        )
        || !throwsRuntimeError(
            [] {
                MultiProcessDebugSession session(
                    std::vector<
                        zero_cpu::kernel::ProcessImage
                    >{}
                );
            }
        )
    ) {
        detail =
            "invalid debugger input was accepted";
        return false;
    }

    MultiProcessDebugSession session(
        normalImages(),
        debugOptions()
    );

    if (
        !throwsRuntimeError(
            [&] {
                session.selectProcess(999);
            }
        )
        || !throwsRuntimeError(
            [&] {
                (void)session.continueExecution(0);
            }
        )
    ) {
        detail =
            "invalid debugger operation was accepted";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Multi-Process Debugger Test ===\n\n";

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
            "Initial process state",
            initialState(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Scheduler observation",
            schedulerObservation(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Completion and isolation",
            completionAndIsolation(detail),
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
            "Console workflow",
            consoleWorkflow(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Invalid input rejection",
            invalidInput(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Multi-process debugger test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Multi-process debugger test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
