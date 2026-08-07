#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/debug/DebugCondition.hpp"
#include "zero_cpu/debug/DebugConsole.hpp"
#include "zero_cpu/debug/DebugSession.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include <iostream>
#include <sstream>
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
    MOV R0, 0
loop:
    ADD R0, 1
    CMP R0, 3
    JL loop
    STORE [value], R0
)ASM";

    zero_cpu::Assembler assembler;
    const auto assembled =
        assembler.assembleString(source);

    zero_cpu::kernel::ProcessImageLoader loader;

    return loader.loadProgram(
        assembled.toBinaryProgram(),
        "debug-condition-test.zbin"
    );
}

bool registerCondition(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    const std::size_t address =
        memory_map::kBinaryCodeBase + 24;

    const std::size_t id =
        session.addConditionalBreakpoint(
            address,
            parseDebugCondition(
                "R0",
                "==",
                "2"
            )
        );

    const DebugStop stop =
        session.continueExecution(50);

    if (
        id != 1
        || !stop.stoppedAtConditionalBreakpoint()
        || !stop.has_conditional_breakpoint
        || stop.conditional_breakpoint_id != id
        || stop.conditional_expression
            != "R0 == 2"
        || stop.conditional_actual_value != 2
        || stop.pc != address
        || stop.total_steps != 7
        || session.cpu().state()
            .registers().get(
                RegisterName::R0
            ) != 2
    ) {
        detail =
            "register condition stop mismatch";
        return false;
    }

    const DebugStop completed =
        session.continueExecution(50);

    if (
        !completed.reachedProgramEnd()
        || session.cpu().state()
            .memory().readI64(
                memory_map::kUserDataBase
            ) != 3
    ) {
        detail =
            "conditional breakpoint resume mismatch";
        return false;
    }

    return true;
}

bool memoryAndFlagConditions(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    {
        DebugSession session(makeImage());

        const std::size_t branchAddress =
            memory_map::kBinaryCodeBase
            + 3 * 24;

        (void)session.addConditionalBreakpoint(
            branchAddress,
            parseDebugCondition(
                "ZF",
                "==",
                "1"
            )
        );

        const DebugStop stop =
            session.continueExecution(50);

        if (
            !stop.stoppedAtConditionalBreakpoint()
            || stop.pc != branchAddress
            || stop.conditional_actual_value != 1
            || session.cpu().state()
                .registers().get(
                    RegisterName::R0
                ) != 3
        ) {
            detail =
                "flag condition mismatch";
            return false;
        }
    }

    {
        DebugSession session(makeImage());

        const std::size_t storeAddress =
            memory_map::kBinaryCodeBase
            + 4 * 24;

        (void)session.addConditionalBreakpoint(
            storeAddress,
            parseDebugCondition(
                "memory[0]",
                "==",
                "0"
            )
        );

        const DebugStop stop =
            session.continueExecution(50);

        if (
            !stop.stoppedAtConditionalBreakpoint()
            || stop.pc != storeAddress
            || stop.conditional_actual_value != 0
        ) {
            detail =
                "memory condition mismatch";
            return false;
        }
    }

    return true;
}

bool managementAndParsing(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    const std::size_t address =
        memory_map::kBinaryCodeBase;

    const DebugCondition condition =
        parseDebugCondition(
            "pc",
            ">=",
            "0x200"
        );

    const std::size_t first =
        session.addConditionalBreakpoint(
            address,
            condition
        );

    const std::size_t duplicate =
        session.addConditionalBreakpoint(
            address,
            condition
        );

    const std::size_t second =
        session.addConditionalBreakpoint(
            address,
            parseDebugCondition(
                "SP",
                ">",
                "0"
            )
        );

    if (
        first != duplicate
        || first == second
        || session.conditionalBreakpoints()
            .size() != 2
        || !session.removeConditionalBreakpoint(
            first
        )
        || session.removeConditionalBreakpoint(
            first
        )
    ) {
        detail =
            "conditional breakpoint management mismatch";
        return false;
    }

    session.clearConditionalBreakpoints();

    if (
        !session.conditionalBreakpoints().empty()
    ) {
        detail =
            "conditional breakpoint clear mismatch";
        return false;
    }

    if (
        !throwsRuntimeError(
            [] {
                (void)parseDebugCondition(
                    "R8",
                    "==",
                    "0"
                );
            }
        )
        || !throwsRuntimeError(
            [] {
                (void)parseDebugCondition(
                    "R0",
                    "=",
                    "0"
                );
            }
        )
        || !throwsRuntimeError(
            [] {
                (void)parseDebugCondition(
                    "R0",
                    "==",
                    "not-a-number"
                );
            }
        )
        || !throwsRuntimeError(
            [&] {
                session.addConditionalBreakpoint(
                    address,
                    parseDebugCondition(
                        "memory[4092]",
                        "==",
                        "0"
                    )
                );
            }
        )
    ) {
        detail =
            "invalid condition was accepted";
        return false;
    }

    return true;
}

bool regularBreakpointPrecedence(
    std::string& detail
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    const std::size_t address =
        memory_map::kBinaryCodeBase;

    (void)session.addBreakpoint(address);

    (void)session.addConditionalBreakpoint(
        address,
        parseDebugCondition(
            "PC",
            "==",
            "512"
        )
    );

    const DebugStop first =
        session.continueExecution(20);

    const DebugStop resumed =
        session.continueExecution(20);

    if (
        !first.stoppedAtBreakpoint()
        || first.total_steps != 0
        || resumed.stoppedAtConditionalBreakpoint()
        || resumed.total_steps == 0
    ) {
        detail =
            "regular breakpoint precedence mismatch";
        return false;
    }

    return true;
}

bool consoleCommands(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(makeImage());

    std::istringstream input(
        "break-if 536 R0 == 2\n"
        "conditions\n"
        "continue 50\n"
        "status\n"
        "delete-if 1\n"
        "conditions\n"
        "clear-conditions\n"
        "quit\n"
    );

    std::ostringstream output;
    std::ostringstream error;

    DebugConsoleOptions options;
    options.show_prompt = false;
    options.print_banner = false;
    options.default_continue_steps = 50;

    DebugConsole console(
        session,
        input,
        output,
        error,
        options
    );

    const DebugConsoleRunResult result =
        console.run();

    const std::string text =
        output.str();

    if (
        !result.success()
        || !result.quit_requested
        || result.command_count != 8
        || !error.str().empty()
        || session.lastStop().reason
            != DebugStopReason::ConditionalBreakpoint
        || text.find(
            "Conditional breakpoint 1 added at 536: R0 == 2."
        ) == std::string::npos
        || text.find(
            "Stop reason: ConditionalBreakpoint"
        ) == std::string::npos
        || text.find(
            "Condition actual value: 2"
        ) == std::string::npos
        || text.find(
            "Conditional breakpoint 1 deleted."
        ) == std::string::npos
        || text.find(
            "No conditional breakpoints."
        ) == std::string::npos
    ) {
        detail =
            "console conditional commands mismatch";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Conditional Breakpoint Test ===\n\n";

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
            "Register condition",
            registerCondition(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Memory and flag conditions",
            memoryAndFlagConditions(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Management and parsing",
            managementAndParsing(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Regular breakpoint precedence",
            regularBreakpointPrecedence(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Console conditional commands",
            consoleCommands(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Conditional breakpoint test "
               "finished successfully.\n";
        return 0;
    }

    std::cout
        << "Conditional breakpoint test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
