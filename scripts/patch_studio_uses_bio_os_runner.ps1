param(
    [string]$RepoRoot = (Get-Location).Path
)

$ErrorActionPreference = "Stop"

Write-Host "START patch_studio_uses_bio_os_runner"

function Normalize-Newlines {
    param([string]$Text)
    return $Text -replace "`r`n", "`n"
}

function Save-Utf8NoBom {
    param(
        [string]$Path,
        [string]$Text
    )

    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText(
        $Path,
        ($Text -replace "`n", "`r`n"),
        $utf8NoBom
    )
}

function Backup-Once {
    param(
        [string]$Path,
        [string]$Suffix
    )

    $backupPath = "$Path.$Suffix"

    if ((Test-Path $Path) -and -not (Test-Path $backupPath)) {
        Copy-Item $Path $backupPath
    }
}

function Find-MatchingBraceIndex {
    param(
        [string]$Text,
        [int]$OpenBraceIndex
    )

    $depth = 0

    for ($i = $OpenBraceIndex; $i -lt $Text.Length; ++$i) {
        $ch = $Text[$i]

        if ($ch -eq '{') {
            ++$depth
        } elseif ($ch -eq '}') {
            --$depth

            if ($depth -eq 0) {
                return $i
            }
        }
    }

    throw "Matching brace not found."
}

$headerPath = Join-Path $RepoRoot "include\zero_cpu\system\BioOSRunner.hpp"
$sourcePath = Join-Path $RepoRoot "src\system\BioOSRunner.cpp"
$studioPath = Join-Path $RepoRoot "studio\zero_studio.cpp"
$docPath = Join-Path $RepoRoot "docs\studio-bio-os-runner-refactor.md"

if (-not (Test-Path $headerPath)) {
    throw "BioOSRunner.hpp not found. Apply BioOSRunner module patch first."
}

if (-not (Test-Path $sourcePath)) {
    throw "BioOSRunner.cpp not found. Apply BioOSRunner module patch first."
}

if (-not (Test-Path $studioPath)) {
    throw "studio\zero_studio.cpp not found."
}

Backup-Once -Path $headerPath -Suffix "bak_studio_bio_os_runner_refactor"
Backup-Once -Path $sourcePath -Suffix "bak_studio_bio_os_runner_refactor"
Backup-Once -Path $studioPath -Suffix "bak_studio_bio_os_runner_refactor"

$header = @'
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zero_cpu {

class CPU;
class DebugOutputDevice;
class InterruptController;
class MMIOBus;
class TimerDevice;

} // namespace zero_cpu

namespace zero_cpu::system {

struct BioOSRunOptions {
    std::string directory = "examples\\bio_os";
    std::string combined_source_path;
    std::string combined_binary_path;
    std::size_t max_steps = 5000;
    bool write_generated_files = true;
};

struct BioOSRunResult {
    std::string directory;
    std::string combined_source_path;
    std::string combined_binary_path;
    std::string combined_source;

    std::size_t instruction_count = 0;
    std::size_t code_size = 0;
    std::size_t syscall_handler_pc = 0;
    std::size_t timer_handler_pc = 0;
    std::size_t stack_base = 0;
    std::size_t step_count = 0;

    bool halted = false;
    bool hit_step_limit = false;
    bool has_error = false;
    std::string error_message;

    std::size_t final_pc = 0;
    std::size_t final_sp = 0;
    std::int64_t exit_code = 0;

    std::vector<std::int64_t> debug_writes;
    std::string debug_ascii;

    std::int64_t timer_tick_count = 0;
    std::int64_t timer_interval = 0;
    int timer_vector = 0;
    std::int64_t timer_payload = 0;
    std::int64_t timer_interrupt_count = 0;
    bool timer_enabled = false;

    bool success() const {
        return halted && !has_error && !hit_step_limit;
    }
};

std::string bioOSDebugOutputAsAscii(
    const std::vector<std::int64_t>& writes
);

class BioOSRunner {
public:
    BioOSRunResult run(const BioOSRunOptions& options = {}) const;

    BioOSRunResult runOn(
        CPU& cpu,
        const std::shared_ptr<InterruptController>& interruptController,
        const std::shared_ptr<MMIOBus>& mmioBus,
        const std::shared_ptr<DebugOutputDevice>& debugOutputDevice,
        const std::shared_ptr<TimerDevice>& timerDevice,
        const BioOSRunOptions& options = {}
    ) const;
};

} // namespace zero_cpu::system
'@

$source = @'
#include "zero_cpu/system/BioOSRunner.hpp"

#include "zero_cpu/assembler/Assembler.hpp"
#include "zero_cpu/binary/BinaryFormat.hpp"
#include "zero_cpu/binary/BinaryProgram.hpp"
#include "zero_cpu/binary/BinaryWriter.hpp"
#include "zero_cpu/core/CPU.hpp"
#include "zero_cpu/core/DebugOutputDevice.hpp"
#include "zero_cpu/core/InterruptController.hpp"
#include "zero_cpu/core/MMIOBus.hpp"
#include "zero_cpu/core/MemoryMap.hpp"
#include "zero_cpu/core/RegisterFile.hpp"
#include "zero_cpu/core/TimerDevice.hpp"
#include "zero_cpu/isa/InstructionEncoder.hpp"

#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace zero_cpu::system {
namespace {

std::string joinPath(
    const std::string& directory,
    const std::string& fileName
) {
    if (directory.empty()) {
        return fileName;
    }

    const char last = directory.back();

    if (last == '\\' || last == '/') {
        return directory + fileName;
    }

    return directory + "\\" + fileName;
}

std::string readTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error("Failed to open file: " + path);
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

void writeTextFile(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error("Failed to write file: " + path);
    }

    file << text;
}

std::string makeCombinedSource(const std::string& directory) {
    std::ostringstream source;

    source << "; Generated by Zero-CPU BioOSRunner\n";
    source << "; Source directory: " << directory << "\n\n";

    source << "; --- boot.zasm ---\n";
    source << readTextFile(joinPath(directory, "boot.zasm"));
    source << "\n\n";

    source << "; --- kernel.zasm ---\n";
    source << readTextFile(joinPath(directory, "kernel.zasm"));
    source << "\n\n";

    source << "; --- user_program.zasm ---\n";
    source << readTextFile(joinPath(directory, "user_program.zasm"));
    source << "\n";

    return source.str();
}

std::string resolveCombinedSourcePath(const BioOSRunOptions& options) {
    if (!options.combined_source_path.empty()) {
        return options.combined_source_path;
    }

    return joinPath(options.directory, "combined_boot.zasm");
}

std::string resolveCombinedBinaryPath(const BioOSRunOptions& options) {
    if (!options.combined_binary_path.empty()) {
        return options.combined_binary_path;
    }

    return joinPath(options.directory, "combined_boot.zbin");
}

std::size_t labelAddress(
    const AssembledProgram& assembled,
    const std::string& labelName,
    std::size_t codeBase
) {
    const auto it = assembled.labels.find(labelName);

    if (it == assembled.labels.end()) {
        throw std::runtime_error("BIO-OS label not found: " + labelName);
    }

    return codeBase + it->second * binary::kInstructionSize;
}

BioOSRunResult runPreparedProgram(
    CPU& cpu,
    const std::shared_ptr<InterruptController>& interruptController,
    const std::shared_ptr<MMIOBus>& mmioBus,
    const std::shared_ptr<DebugOutputDevice>& debugOutputDevice,
    const std::shared_ptr<TimerDevice>& timerDevice,
    const BioOSRunOptions& options,
    const AssembledProgram& assembled,
    binary::BinaryProgram program,
    const std::string& combinedSource
) {
    BioOSRunResult result;
    result.directory = options.directory;
    result.combined_source_path = resolveCombinedSourcePath(options);
    result.combined_binary_path = resolveCombinedBinaryPath(options);
    result.combined_source = combinedSource;
    result.instruction_count = assembled.instructions.size();
    result.code_size = program.code.size();
    result.stack_base = memory_map::kBioOSStackBase;

    if (!interruptController) {
        throw std::runtime_error("BioOSRunner requires InterruptController");
    }

    if (!mmioBus) {
        throw std::runtime_error("BioOSRunner requires MMIOBus");
    }

    if (!debugOutputDevice) {
        throw std::runtime_error("BioOSRunner requires DebugOutputDevice");
    }

    if (!timerDevice) {
        throw std::runtime_error("BioOSRunner requires TimerDevice");
    }

    cpu.loadBinaryProgram(program);
    cpu.setInterruptController(interruptController);
    cpu.setMMIOBus(mmioBus);
    cpu.clearClockedDevices();
    cpu.addClockedDevice(timerDevice);
    cpu.state().setSp(memory_map::kBioOSStackBase);

    result.syscall_handler_pc =
        labelAddress(
            assembled,
            "syscall_handler",
            cpu.binaryCodeBase()
        );

    result.timer_handler_pc =
        labelAddress(
            assembled,
            "timer_handler",
            cpu.binaryCodeBase()
        );

    interruptController->setVectorHandler(
        80,
        result.syscall_handler_pc
    );

    interruptController->setVectorHandler(
        44,
        result.timer_handler_pc
    );

    while (
        !cpu.state().halted() &&
        result.step_count < options.max_steps
    ) {
        cpu.step();

        if (cpu.state().hasError()) {
            result.has_error = true;
            result.error_message = cpu.state().errorMessage();
            break;
        }

        ++result.step_count;
    }

    if (!cpu.state().halted() && !result.has_error) {
        result.hit_step_limit = true;
        result.error_message = "BIO-OS step limit reached";
    }

    result.halted = cpu.state().halted();
    result.final_pc = cpu.state().pc();
    result.final_sp = cpu.state().sp();
    result.exit_code =
        cpu.state().registers().get(RegisterName::R7);

    result.debug_writes = debugOutputDevice->writes();
    result.debug_ascii =
        bioOSDebugOutputAsAscii(result.debug_writes);

    result.timer_tick_count = timerDevice->tickCount();
    result.timer_interval = timerDevice->interval();
    result.timer_vector =
        static_cast<int>(timerDevice->vector());
    result.timer_payload = timerDevice->payload();
    result.timer_interrupt_count =
        timerDevice->interruptCount();
    result.timer_enabled = timerDevice->enabled();

    return result;
}

} // namespace

std::string bioOSDebugOutputAsAscii(
    const std::vector<std::int64_t>& writes
) {
    std::string text;

    for (const std::int64_t value : writes) {
        if (value >= 32 && value <= 126) {
            text.push_back(static_cast<char>(value));
        } else if (value == 10) {
            text.push_back('\n');
        } else {
            text.push_back('.');
        }
    }

    return text;
}

BioOSRunResult BioOSRunner::run(const BioOSRunOptions& options) const {
    CPU cpu;

    auto interruptController =
        std::make_shared<InterruptController>();
    auto mmioBus = std::make_shared<MMIOBus>();
    auto debugOutputDevice =
        std::make_shared<DebugOutputDevice>();
    auto timerDevice =
        std::make_shared<TimerDevice>(
            interruptController,
            44,
            1000,
            0
        );

    timerDevice->setEnabled(false);

    mmioBus->mapDevice(
        memory_map::kDebugOutputBase,
        memory_map::kDebugOutputSize,
        debugOutputDevice
    );

    mmioBus->mapDevice(
        memory_map::kTimerBase,
        memory_map::kTimerSize,
        timerDevice
    );

    return runOn(
        cpu,
        interruptController,
        mmioBus,
        debugOutputDevice,
        timerDevice,
        options
    );
}

BioOSRunResult BioOSRunner::runOn(
    CPU& cpu,
    const std::shared_ptr<InterruptController>& interruptController,
    const std::shared_ptr<MMIOBus>& mmioBus,
    const std::shared_ptr<DebugOutputDevice>& debugOutputDevice,
    const std::shared_ptr<TimerDevice>& timerDevice,
    const BioOSRunOptions& options
) const {
    using namespace zero_cpu::binary;

    BioOSRunResult result;
    result.directory = options.directory;
    result.combined_source_path = resolveCombinedSourcePath(options);
    result.combined_binary_path = resolveCombinedBinaryPath(options);
    result.stack_base = memory_map::kBioOSStackBase;

    try {
        const std::string combinedSource =
            makeCombinedSource(options.directory);

        if (options.write_generated_files) {
            writeTextFile(
                result.combined_source_path,
                combinedSource
            );
        }

        Assembler assembler;
        AssembledProgram assembled =
            assembler.assembleString(combinedSource);

        InstructionEncoder encoder;
        std::vector<std::uint8_t> code =
            encoder.encodeProgram(
                assembled.instructions,
                assembled.labels
            );

        BinaryProgram program;
        program.header.major_version = kMajorVersion;
        program.header.minor_version = kMinorVersion;
        program.header.endianness = BinaryEndianness::Little;
        program.header.entry_point = 0;
        program.header.code_size =
            static_cast<std::uint32_t>(code.size());
        program.code = std::move(code);

        if (options.write_generated_files) {
            BinaryWriter writer;
            writer.writeFile(result.combined_binary_path, program);
        }

        return runPreparedProgram(
            cpu,
            interruptController,
            mmioBus,
            debugOutputDevice,
            timerDevice,
            options,
            assembled,
            std::move(program),
            combinedSource
        );
    } catch (const std::exception& ex) {
        result.has_error = true;
        result.error_message = ex.what();
        return result;
    }
}

} // namespace zero_cpu::system
'@

Save-Utf8NoBom -Path $headerPath -Text $header
Save-Utf8NoBom -Path $sourcePath -Text $source

$studio = Normalize-Newlines (Get-Content -Raw -Path $studioPath)

if (-not $studio.Contains('zero_cpu/system/BioOSRunner.hpp')) {
    $includeAnchor = '#include "zero_cpu/core/CPU.hpp"'

    if (-not $studio.Contains($includeAnchor)) {
        throw "Cannot find CPU include anchor in zero_studio.cpp."
    }

    $studio = $studio.Replace(
        $includeAnchor,
        $includeAnchor + "`n" + '#include "zero_cpu/system/BioOSRunner.hpp"'
    )
}

$match = [regex]::Match($studio, 'void\s+onRunBioOSClicked\s*\(\s*\)\s*\{')

if (-not $match.Success) {
    throw "onRunBioOSClicked function not found."
}

$openBrace = $studio.IndexOf('{', $match.Index)
$closeBrace = Find-MatchingBraceIndex -Text $studio -OpenBraceIndex $openBrace

$replacement = @'
void onRunBioOSClicked() {
    using namespace zero_cpu::system;

    const std::string bioOSDirectory = "examples\\bio_os";

    try {
        configureSystemDevices();

        BioOSRunOptions options;
        options.directory = bioOSDirectory;

        BioOSRunner runner;
        const BioOSRunResult result =
            runner.runOn(
                g_cpu,
                g_interruptController,
                g_mmioBus,
                g_debugOutputDevice,
                g_timerDevice,
                options
            );

        g_mode = StudioMode::Binary;
        g_programLoaded = true;
        g_loadedPath = result.combined_binary_path;

        SetWindowTextA(g_inputEdit, result.combined_binary_path.c_str());
        SetWindowTextA(g_outputEdit, result.combined_binary_path.c_str());
        setEditText(g_sourceEdit, result.combined_source);

        std::ostringstream log;
        log << "[Studio BIO-OS Run via BioOSRunner]\n";
        log << "Directory: " << result.directory << "\n";
        log << "Generated Source: "
            << result.combined_source_path
            << "\n";
        log << "Generated Binary: "
            << result.combined_binary_path
            << "\n";
        log << "Instruction Count: "
            << result.instruction_count
            << "\n";
        log << "Code Size: "
            << result.code_size
            << " bytes\n";
        log << "Syscall Handler PC: "
            << result.syscall_handler_pc
            << "\n";
        log << "Timer Handler PC: "
            << result.timer_handler_pc
            << "\n";
        log << "BIO-OS Stack Base: "
            << result.stack_base
            << "\n";
        log << "Step Count: "
            << result.step_count
            << "\n";
        log << "Final PC: "
            << result.final_pc
            << "\n";
        log << "Final SP: "
            << result.final_sp
            << "\n";
        log << "Exit Code: "
            << result.exit_code
            << "\n\n";

        if (!result.success()) {
            log << "BIO-OS execution failed: "
                << result.error_message
                << "\n";
        } else {
            log << "BIO-OS execution finished successfully.\n";
        }

        log << "\n";
        log << "Debug Writes: "
            << result.debug_writes.size()
            << "\n";
        log << "Debug ASCII: "
            << (result.debug_ascii.empty()
                ? std::string("<empty>")
                : result.debug_ascii)
            << "\n";
        log << "Timer Tick Count: "
            << result.timer_tick_count
            << "\n";
        log << "Timer Interval: "
            << result.timer_interval
            << "\n";
        log << "Timer Vector: "
            << result.timer_vector
            << "\n";
        log << "Timer Payload: "
            << result.timer_payload
            << "\n";
        log << "Timer Interrupt Count: "
            << result.timer_interrupt_count
            << "\n";
        log << "Timer Enabled: "
            << studioBoolText(result.timer_enabled)
            << "\n";

        setEditText(g_traceEdit, log.str());
        refreshStateView();
    } catch (const std::exception& ex) {
        std::ostringstream log;
        log << "[Studio BIO-OS Run Failed]\n";
        log << "Error: " << ex.what() << "\n";

        setEditText(g_traceEdit, log.str());
        refreshStateView();
    }
}
'@

$studio = $studio.Substring(0, $match.Index) +
    $replacement +
    $studio.Substring($closeBrace + 1)

$studio = $studio.Replace('Zero-CPU Studio v0.8', 'Zero-CPU Studio v0.9')

if (-not $studio.Contains('Studio BIO-OS runner uses BioOSRunner.')) {
    $studio = $studio.Replace(
        '"BIO-OS runner added.\n"',
        '"BIO-OS runner added.\n"
        "Studio BIO-OS runner uses BioOSRunner.\n"'
    )
}

Save-Utf8NoBom -Path $studioPath -Text $studio

$doc = @'
# Studio BioOSRunner Refactor

Studio `Run BIO-OS` now uses the shared `BioOSRunner` module.

## Before

```text
Studio Run BIO-OS
    had its own combine / assemble / zbin / load / run path
```

## After

```text
Studio Run BIO-OS
    ↓
BioOSRunner::runOn(...)
    ↓
g_cpu + g_interruptController + g_mmioBus + g_debugOutputDevice + g_timerDevice
    ↓
Studio trace + System Panel
```

## Why `runOn(...)`

Studio needs the final device and CPU state to remain visible in the UI.

So `BioOSRunner` now has two paths:

```text
BioOSRunner::run(...)
    creates its own CPU/devices internally

BioOSRunner::runOn(...)
    runs on caller-provided CPU/devices
```

CLI can use `run(...)`.

Studio can use `runOn(...)` so that the System Panel still shows:

```text
Debug ASCII = BU
Timer Interrupt Count = 1
Timer Enabled = false
```

## Test

```bat
cmake --build build
.\build\Debug\zero_studio.exe
```

Click:

```text
Run BIO-OS
```

Expected trace title:

```text
[Studio BIO-OS Run via BioOSRunner]
```
'@

Save-Utf8NoBom -Path $docPath -Text $doc

$headerAfter = Normalize-Newlines (Get-Content -Raw -Path $headerPath)
$sourceAfter = Normalize-Newlines (Get-Content -Raw -Path $sourcePath)
$studioAfter = Normalize-Newlines (Get-Content -Raw -Path $studioPath)

if (-not $headerAfter.Contains('BioOSRunResult runOn(')) {
    throw "VERIFY FAILED: BioOSRunner.hpp missing runOn declaration."
}

if (-not $sourceAfter.Contains('BioOSRunResult BioOSRunner::runOn(')) {
    throw "VERIFY FAILED: BioOSRunner.cpp missing runOn implementation."
}

if (-not $studioAfter.Contains('[Studio BIO-OS Run via BioOSRunner]')) {
    throw "VERIFY FAILED: Studio trace title missing."
}

if (-not $studioAfter.Contains('runner.runOn(')) {
    throw "VERIFY FAILED: Studio does not call BioOSRunner::runOn."
}

Write-Host "PATCH OK"
Write-Host "Patched:"
Write-Host "  include\zero_cpu\system\BioOSRunner.hpp"
Write-Host "  src\system\BioOSRunner.cpp"
Write-Host "  studio\zero_studio.cpp"
Write-Host "Created:"
Write-Host "  docs\studio-bio-os-runner-refactor.md"
Write-Host ""
Write-Host "Next:"
Write-Host "  cmake --build build"
Write-Host "  .\build\Debug\zero_studio.exe"
