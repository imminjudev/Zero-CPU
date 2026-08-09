#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/debug/DebugCondition.hpp"
#include "zero_cpu/debug/DebugConsole.hpp"
#include "zero_cpu/debug/DebugSession.hpp"
#include "zero_cpu/debug/DebugSnapshotJson.hpp"
#include "zero_cpu/debug/DebugSymbols.hpp"
#include "zero_cpu/debug/MultiProcessDebugConsole.hpp"
#include "zero_cpu/debug/MultiProcessDebugSession.hpp"
#include "zero_cpu/kernel/ProcessImageLoader.hpp"

#include <cstdio>
#include <fstream>
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

std::string readFile(
    const std::string& path
) {
    std::ifstream input(
        path,
        std::ios::binary
    );

    if (!input) {
        throw std::runtime_error(
            "Cannot read test JSON file"
        );
    }

    std::ostringstream output;
    output << input.rdbuf();

    return output.str();
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

struct TemporarySourceExecutable {
    std::string binary_path;
    std::string symbols_path;

    ~TemporarySourceExecutable() {
        std::remove(binary_path.c_str());
        std::remove(symbols_path.c_str());
    }
};

TemporarySourceExecutable
writeSourceExecutable(
    const std::string& baseName,
    const std::string& sourcePath
) {
    using namespace zero_cpu;

    Assembler assembler;

    const AssembledProgram assembled =
        assembler.assembleString(
            kFirstSource
        );

    const std::string binaryPath =
        baseName + ".zbin";

    const std::string symbolsPath =
        debug::debugSymbolsPathForExecutable(
            binaryPath
        );

    binary::BinaryWriter writer;

    writer.writeFile(
        binaryPath,
        assembled.toBinaryProgram()
    );

    debug::DebugSymbols::
        fromAssembledProgram(
            assembled,
            memory_map::kBinaryCodeBase,
            sourcePath
        ).writeFile(
            symbolsPath
        );

    return {
        binaryPath,
        symbolsPath
    };
}

zero_cpu::debug::DebugSnapshotOptions
snapshotOptions() {
    zero_cpu::debug::DebugSnapshotOptions
        options;

    options.memory_address =
        zero_cpu::memory_map::kUserDataBase;

    options.memory_size = 8;

    return options;
}

zero_cpu::debug::MultiProcessDebugOptions
multiOptions() {
    zero_cpu::debug::MultiProcessDebugOptions
        options;

    options.quantum = 1;
    options.default_continue_steps = 100;

    return options;
}

bool singleSnapshot(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    DebugSession session(
        makeImage(
            kFirstSource,
            "single-snapshot.zbin"
        )
    );

    (void)session.addBreakpoint(
        memory_map::kBinaryCodeBase
            + binary::kInstructionSize
    );

    (void)session.addConditionalBreakpoint(
        memory_map::kBinaryCodeBase
            + binary::kInstructionSize,
        parseDebugCondition(
            "R0",
            "==",
            "1"
        )
    );

    (void)session.addWatchpoint(
        memory_map::kUserDataBase,
        8,
        MemoryWatchMode::Write
    );

    (void)session.step();

    const std::string json =
        DebugSnapshotJsonWriter::toJson(
            session,
            snapshotOptions()
        );

    if (
        json.find(
            "\"schema\": "
            "\"zero_cpu_debug_snapshot\""
        ) == std::string::npos
        || json.find(
            "\"mode\": \"single_process\""
        ) == std::string::npos
        || json.find(
            "\"source_name\": "
            "\"single-snapshot.zbin\""
        ) == std::string::npos
        || json.find(
            "\"breakpoints\": [536]"
        ) == std::string::npos
        || json.find(
            "\"expression\": \"R0 == 1\""
        ) == std::string::npos
        || json.find(
            "\"mode\": \"Write\""
        ) == std::string::npos
        || json.find(
            "\"bytes\": [1, 0, 0, 0, "
            "0, 0, 0, 0]"
        ) == std::string::npos
        || json.find(
            "\"last_trace\": {"
        ) == std::string::npos
    ) {
        detail =
            "single-process snapshot JSON mismatch";
        return false;
    }

    return true;
}

bool multiSnapshot(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    MultiProcessDebugSession session(
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
        multiOptions()
    );

    (void)session.addBreakpoint(
        2,
        memory_map::kBinaryCodeBase
    );

    (void)session.addConditionalBreakpoint(
        1,
        memory_map::kBinaryCodeBase
            + binary::kInstructionSize,
        parseDebugCondition(
            "R0",
            "==",
            "1"
        )
    );

    (void)session.addWatchpoint(
        1,
        memory_map::kUserDataBase,
        8,
        ProcessMemoryWatchMode::Write
    );

    (void)session.step();
    (void)session.step();

    const std::string json =
        DebugSnapshotJsonWriter::toJson(
            session,
            snapshotOptions()
        );

    if (
        json.find(
            "\"mode\": \"multi_process\""
        ) == std::string::npos
        || json.find(
            "\"runtime_state\": \"Running\""
        ) == std::string::npos
        || json.find(
            "\"pid\": 1"
        ) == std::string::npos
        || json.find(
            "\"pid\": 2"
        ) == std::string::npos
        || json.find(
            "\"running_pid\":"
        ) == std::string::npos
        || json.find(
            "\"preemption_count\":"
        ) == std::string::npos
        || json.find(
            "\"context_switches\": ["
        ) == std::string::npos
        || json.find(
            "\"conditional_breakpoints\": ["
        ) == std::string::npos
        || json.find(
            "\"watchpoints\": ["
        ) == std::string::npos
        || json.find(
            "\"last_trace\": {"
        ) == std::string::npos
    ) {
        detail =
            "multi-process snapshot JSON mismatch";
        return false;
    }

    return true;
}

bool sourceLocations(std::string& detail) {
    using namespace zero_cpu;
    using namespace zero_cpu::debug;

    const TemporarySourceExecutable temporary =
        writeSourceExecutable(
            "debug_snapshot_source_location",
            "examples\\snapshot_source.zasm"
        );

    {
        DebugSession session(
            temporary.binary_path
        );

        (void)session.step();

        const std::string json =
            DebugSnapshotJsonWriter::toJson(
                session,
                snapshotOptions()
            );

        if (
            json.find(
                "\"schema_version\": 2"
            ) == std::string::npos
            || json.find(
                "\"source_location\": {"
            ) == std::string::npos
            || json.find(
                "\"available\": true"
            ) == std::string::npos
            || json.find(
                "\"path\": "
                "\"examples\\\\snapshot_source.zasm\""
            ) == std::string::npos
            || json.find(
                "\"line\": 10"
            ) == std::string::npos
            || json.find(
                "\"address\": 536"
            ) == std::string::npos
        ) {
            detail =
                "single-process source location mismatch";
            return false;
        }
    }

    {
        MultiProcessDebugSession session(
            {
                temporary.binary_path,
                temporary.binary_path
            },
            multiOptions()
        );

        (void)session.step();
        (void)session.step();

        const std::string json =
            DebugSnapshotJsonWriter::toJson(
                session,
                snapshotOptions()
            );

        const std::string sourcePath =
            "\"path\": "
            "\"examples\\\\snapshot_source.zasm\"";

        const std::size_t first =
            json.find(sourcePath);

        const std::size_t second =
            first == std::string::npos
                ? std::string::npos
                : json.find(
                    sourcePath,
                    first + sourcePath.size()
                );

        if (
            json.find(
                "\"mode\": \"multi_process\""
            ) == std::string::npos
            || first == std::string::npos
            || second == std::string::npos
            || json.find(
                "\"line\": 10"
            ) == std::string::npos
            || json.find(
                "\"address\": 536"
            ) == std::string::npos
        ) {
            detail =
                "multi-process source location mismatch";
            return false;
        }
    }

    {
        DebugSession session(
            makeImage(
                kFirstSource,
                "no-source-map.zbin"
            )
        );

        const std::string json =
            DebugSnapshotJsonWriter::toJson(
                session,
                snapshotOptions()
            );

        if (
            json.find(
                "\"source_location\": {"
            ) == std::string::npos
            || json.find(
                "\"available\": false"
            ) == std::string::npos
            || json.find(
                "\"path\": \"\""
            ) == std::string::npos
            || json.find(
                "\"line\": 0"
            ) == std::string::npos
            || json.find(
                "\"address\": 512"
            ) == std::string::npos
        ) {
            detail =
                "source-map fallback mismatch";
            return false;
        }
    }

    return true;
}

bool fileWriting(std::string& detail) {
    using namespace zero_cpu::debug;

    const std::string path =
        "debug_snapshot_writer_test.json";

    struct Cleanup {
        std::string path;

        ~Cleanup() {
            std::remove(
                path.c_str()
            );
        }
    } cleanup{path};

    DebugSession session(
        makeImage(
            kFirstSource,
            "file-snapshot.zbin"
        )
    );

    (void)session.step();

    DebugSnapshotJsonWriter::writeFile(
        path,
        session,
        snapshotOptions()
    );

    const std::string json =
        readFile(path);

    if (
        json.empty()
        || json.find(
            "\"schema_version\": 2"
        ) == std::string::npos
        || json.back() != '\n'
    ) {
        detail =
            "snapshot JSON file write mismatch";
        return false;
    }

    return true;
}

bool consoleCommands(std::string& detail) {
    using namespace zero_cpu::debug;

    const std::string singlePath =
        "debug_snapshot_console_single.json";

    const std::string multiPath =
        "debug_snapshot_console_multi.json";

    struct Cleanup {
        std::string first;
        std::string second;

        ~Cleanup() {
            std::remove(first.c_str());
            std::remove(second.c_str());
        }
    } cleanup{
        singlePath,
        multiPath
    };

    {
        DebugSession session(
            makeImage(
                kFirstSource,
                "console-single.zbin"
            )
        );

        std::istringstream input(
            "step 1\n"
            "snapshot-json "
            + singlePath
            + " 0 8\n"
            "quit\n"
        );

        std::ostringstream output;
        std::ostringstream error;

        DebugConsoleOptions options;
        options.show_prompt = false;
        options.print_banner = false;

        DebugConsole console(
            session,
            input,
            output,
            error,
            options
        );

        const DebugConsoleRunResult result =
            console.run();

        if (
            !result.success()
            || !result.quit_requested
            || result.command_count != 3
            || !error.str().empty()
            || output.str().find(
                "Debug snapshot JSON written: "
                + singlePath
            ) == std::string::npos
        ) {
            detail =
                "single console snapshot command mismatch";
            return false;
        }
    }

    {
        MultiProcessDebugSession session(
            {
                makeImage(
                    kFirstSource,
                    "console-first.zbin"
                ),
                makeImage(
                    kSecondSource,
                    "console-second.zbin"
                )
            },
            multiOptions()
        );

        std::istringstream input(
            "step 2\n"
            "snapshot-json "
            + multiPath
            + " 0 8\n"
            "quit\n"
        );

        std::ostringstream output;
        std::ostringstream error;

        MultiProcessDebugConsoleOptions options;
        options.show_prompt = false;
        options.print_banner = false;

        MultiProcessDebugConsole console(
            session,
            input,
            output,
            error,
            options
        );

        const MultiProcessDebugConsoleResult result =
            console.run();

        if (
            !result.success()
            || !result.quit_requested
            || result.command_count != 3
            || !error.str().empty()
            || output.str().find(
                "Multi-process debug snapshot JSON "
                "written: "
                + multiPath
            ) == std::string::npos
        ) {
            detail =
                "multi console snapshot command mismatch";
            return false;
        }
    }

    if (
        readFile(singlePath).find(
            "\"mode\": \"single_process\""
        ) == std::string::npos
        || readFile(multiPath).find(
            "\"mode\": \"multi_process\""
        ) == std::string::npos
    ) {
        detail =
            "console snapshot files mismatch";
        return false;
    }

    return true;
}

bool validation(std::string& detail) {
    using namespace zero_cpu::debug;

    DebugSession session(
        makeImage(
            kFirstSource,
            "validation-snapshot.zbin"
        )
    );

    DebugSnapshotOptions zeroSize =
        snapshotOptions();

    zeroSize.memory_size = 0;

    DebugSnapshotOptions outside =
        snapshotOptions();

    outside.memory_address =
        session.cpu().state().memory().size()
        - 4;

    outside.memory_size = 8;

    if (
        !throwsRuntimeError(
            [&] {
                (void)DebugSnapshotJsonWriter::
                    toJson(
                        session,
                        zeroSize
                    );
            }
        )
        || !throwsRuntimeError(
            [&] {
                (void)DebugSnapshotJsonWriter::
                    toJson(
                        session,
                        outside
                    );
            }
        )
        || !throwsRuntimeError(
            [&] {
                DebugSnapshotJsonWriter::writeFile(
                    "",
                    session,
                    snapshotOptions()
                );
            }
        )
    ) {
        detail =
            "invalid snapshot request was accepted";
        return false;
    }

    DebugSnapshotOptions noMemory;
    noMemory.include_memory = false;
    noMemory.memory_size = 0;

    if (
        DebugSnapshotJsonWriter::toJson(
            session,
            noMemory
        ).find(
            "\"included\": false"
        ) == std::string::npos
    ) {
        detail =
            "memory-disabled snapshot mismatch";
        return false;
    }

    return true;
}

} // namespace

int main() {
    std::cout
        << "=== Zero-CPU Debug Snapshot JSON "
           "Test ===\n\n";

    int failures = 0;

    auto report = [&](
        const std::string& name,
        bool passed,
        const std::string& detail
    ) {
        std::cout
            << (
                passed
                    ? "[PASS] "
                    : "[FAIL] "
            )
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
            "Single-process snapshot",
            singleSnapshot(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Multi-process snapshot",
            multiSnapshot(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Snapshot source locations",
            sourceLocations(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Snapshot file writing",
            fileWriting(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Console snapshot commands",
            consoleCommands(detail),
            detail
        );
    }

    {
        std::string detail;
        report(
            "Snapshot validation",
            validation(detail),
            detail
        );
    }

    std::cout << "\n";

    if (failures == 0) {
        std::cout
            << "Debug snapshot JSON test "
               "finished successfully.\n";

        return 0;
    }

    std::cout
        << "Debug snapshot JSON test failed. "
        << "Failure count: "
        << failures
        << "\n";

    return 1;
}
