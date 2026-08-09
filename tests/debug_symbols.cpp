#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/debug/DebugConsole.hpp"
#include "zero_cpu/debug/DebugSession.hpp"
#include "zero_cpu/debug/DebugSymbols.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include <cstdio>
#include <fstream>
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

const char* kSource = R"ASM(
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
done:
    STORE [value], R0
)ASM";

struct TemporaryExecutable {
    std::string binary_path;
    std::string symbols_path;

    ~TemporaryExecutable() {
        std::remove(
            binary_path.c_str()
        );

        std::remove(
            symbols_path.c_str()
        );
    }
};

TemporaryExecutable writeExecutable(
    const std::string& baseName
) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(
            kSource
        );

    const std::string binaryPath =
        baseName + ".zbin";

    const std::string symbolsPath =
        debugSymbolsPathForExecutable(
            binaryPath
        );

    binary::BinaryWriter writer;

    writer.writeFile(
        binaryPath,
        assembled.toBinaryProgram()
    );

    DebugSymbols::fromAssembledProgram(
        assembled,
        memory_map::kBinaryCodeBase,
        "debug_symbols_source.zasm"
    ).writeFile(
        symbolsPath
    );

    return {
        binaryPath,
        symbolsPath
    };
}

bool roundTrip(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(
            kSource
        );

    const DebugSymbols symbols =
        DebugSymbols::fromAssembledProgram(
            assembled,
            memory_map::kBinaryCodeBase,
            "round trip source.zasm"
        );

    const std::string path =
        "debug_symbols_roundtrip.zsym";

    struct Cleanup {
        std::string path;

        ~Cleanup() {
            std::remove(
                path.c_str()
            );
        }
    } cleanup{path};

    symbols.writeFile(path);

    const DebugSymbols restored =
        DebugSymbols::readFile(path);

    if (
        !(symbols == restored)
        || symbols.size() != 4
        || symbols.resolveCode("start") != 512
        || symbols.resolveCode("loop") != 536
        || symbols.resolveCode("done") != 608
        || symbols.resolveData("value") != 0
        || !symbols.hasSourceLocations()
        || symbols.sourcePath()
            != "round trip source.zasm"
        || symbols.sourceLocations().size() != 5
        || symbols.resolveSourceLine(9) != 512
        || symbols.resolveSourceLine(11) != 536
        || symbols.sourceLineForAddress(608) != 15
    ) {
        detail =
            "debug symbols round-trip mismatch";
        return false;
    }

    return true;
}

bool automaticLoading(std::string& detail) {
    using namespace zero_cpu::debug;

    const TemporaryExecutable temporary =
        writeExecutable(
            "debug_symbols_auto"
        );

    DebugSession session(
        temporary.binary_path
    );

    if (
        !session.hasSymbols()
        || session.symbols().size() != 4
        || session.resolveCodeSymbol(
            "loop"
        ) != 536
        || session.resolveDataSymbol(
            "value"
        ) != 0
        || !session.symbols()
            .hasSourceLocations()
        || session.symbols()
            .sourcePath()
            != "debug_symbols_source.zasm"
        || session.symbols()
            .resolveSourceLine(11)
            != 536
    ) {
        detail =
            "debug symbols were not auto-loaded";
        return false;
    }

    return true;
}

bool consoleCommands(std::string& detail) {
    using namespace zero_cpu::debug;

    const TemporaryExecutable temporary =
        writeExecutable(
            "debug_symbols_console"
        );

    DebugSession session(
        temporary.binary_path
    );

    std::istringstream input(
        "symbols\n"
        "break-label loop\n"
        "continue 50\n"
        "delete 536\n"
        "break-if-label done R0 == 3\n"
        "continue 50\n"
        "status\n"
        "disassemble-label start 5\n"
        "memory-label value 8\n"
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
        || result.command_count != 10
        || !error.str().empty()
        || session.lastStop().reason
            != DebugStopReason::ConditionalBreakpoint
        || text.find(
            "CODE loop = 536"
        ) == std::string::npos
        || text.find(
            "DATA value = 0"
        ) == std::string::npos
        || text.find(
            "Breakpoint at label loop (536) added."
        ) == std::string::npos
        || text.find(
            "Conditional breakpoint 1 added at label done (608): R0 == 3."
        ) == std::string::npos
        || text.find(
            "Stop reason: ConditionalBreakpoint"
        ) == std::string::npos
        || text.find(
            "512: MOV R0, 0"
        ) == std::string::npos
        || text.find(
            "Memory[0..8):"
        ) == std::string::npos
    ) {
        detail =
            "symbol debugger commands mismatch";
        return false;
    }

    return true;
}

bool legacyV1Compatibility(
    std::string& detail
) {
    using namespace zero_cpu::debug;

    const std::string path =
        "debug_symbols_legacy_v1.zsym";

    struct Cleanup {
        std::string path;
        ~Cleanup() {
            std::remove(path.c_str());
        }
    } cleanup{path};

    {
        std::ofstream output(path);
        output
            << "ZCPU-SYMBOLS 1\n"
            << "CODE start 512\n"
            << "DATA value 0\n";
    }

    const DebugSymbols symbols =
        DebugSymbols::readFile(path);

    if (
        symbols.size() != 2
        || symbols.resolveCode("start") != 512
        || symbols.resolveData("value") != 0
        || symbols.hasSourceLocations()
        || !symbols.sourcePath().empty()
    ) {
        detail =
            "legacy v1 symbols compatibility mismatch";
        return false;
    }

    return true;
}

bool validation(std::string& detail) {
    using namespace zero_cpu::debug;

    const std::string path =
        "debug_symbols_invalid.zsym";

    struct Cleanup {
        std::string path;

        ~Cleanup() {
            std::remove(
                path.c_str()
            );
        }
    } cleanup{path};

    {
        std::ofstream output(path);
        output
            << "INVALID HEADER\n"
            << "CODE start 512\n";
    }

    if (
        !throwsRuntimeError(
            [&] {
                (void)DebugSymbols::readFile(
                    path
                );
            }
        )
    ) {
        detail =
            "invalid symbol header was accepted";
        return false;
    }

    const TemporaryExecutable temporary =
        writeExecutable(
            "debug_symbols_validation"
        );

    DebugSession session(
        temporary.binary_path
    );

    if (
        !throwsRuntimeError(
            [&] {
                (void)session.resolveCodeSymbol(
                    "value"
                );
            }
        )
        || !throwsRuntimeError(
            [&] {
                (void)session.resolveDataSymbol(
                    "start"
                );
            }
        )
        || !throwsRuntimeError(
            [&] {
                (void)session.resolveCodeSymbol(
                    "missing"
                );
            }
        )
    ) {
        detail =
            "invalid symbol resolution was accepted";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Debug Symbols Test ===\n\n";

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
            "Symbol sidecar round-trip",
            roundTrip(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Automatic symbol loading",
            automaticLoading(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Symbol debugger commands",
            consoleCommands(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Legacy v1 symbol compatibility",
            legacyV1Compatibility(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Symbol validation",
            validation(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Debug symbols test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Debug symbols test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
