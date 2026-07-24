param(
    [string]$RepoRoot = (Get-Location).Path
)

$ErrorActionPreference = "Stop"

Write-Host "START patch_v0_1_release_smoke_test"

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

$smokePath = Join-Path $RepoRoot "scripts\v0_1_smoke_test.bat"
$releaseNotesPath = Join-Path $RepoRoot "docs\v0.1-release-notes.md"
$demoGuidePath = Join-Path $RepoRoot "docs\v0.1-demo-guide.md"

$smoke = @'
@echo off
setlocal

echo ========================================
echo Zero-CPU v0.1 Smoke Test
echo ========================================
echo.

cd /d "%~dp0\.."

if not exist build (
    echo [FAIL] build directory not found.
    echo Run CMake configure first, for example:
    echo   cmake -S . -B build
    exit /b 1
)

echo [1/6] Building all targets...
cmake --build build
if errorlevel 1 goto fail

set ZERO_CLI=.\build\Debug\zero_cli.exe
set ZERO_STUDIO=.\build\Debug\zero_studio.exe

if not exist "%ZERO_CLI%" (
    echo [FAIL] zero_cli.exe not found: %ZERO_CLI%
    exit /b 1
)

echo.
echo [2/6] Running full test suite...
call scripts\test_all.bat
if errorlevel 1 goto fail

echo.
echo [3/6] Running BIO-OS through shared BioOSRunner test command...
"%ZERO_CLI%" bio-os-runner-test examples\bio_os
if errorlevel 1 goto fail

echo.
echo [4/6] Running public CLI OS command...
"%ZERO_CLI%" run-os examples\bio_os
if errorlevel 1 goto fail

echo.
echo [5/6] Checking generated BIO-OS artifacts...
if not exist examples\bio_os\combined_boot.zasm (
    echo [FAIL] examples\bio_os\combined_boot.zasm not found.
    exit /b 1
)

if not exist examples\bio_os\combined_boot.zbin (
    echo [FAIL] examples\bio_os\combined_boot.zbin not found.
    exit /b 1
)

echo [PASS] combined_boot.zasm exists.
echo [PASS] combined_boot.zbin exists.

echo.
echo [6/6] Checking Studio executable...
if exist "%ZERO_STUDIO%" (
    echo [PASS] zero_studio.exe exists: %ZERO_STUDIO%
) else (
    echo [WARN] zero_studio.exe not found. This is expected on non-Windows generators or if Studio target was not built.
)

echo.
echo ========================================
echo Zero-CPU v0.1 Smoke Test PASSED
echo ========================================
echo.
echo Manual Studio check:
echo   1. Run .\build\Debug\zero_studio.exe
echo   2. Click Run BIO-OS
echo   3. Confirm:
echo      [Studio BIO-OS Run via BioOSRunner]
echo      Debug ASCII: BU
echo      Timer Interrupt Count: 1
echo      Timer Enabled: false
echo.
exit /b 0

:fail
echo.
echo ========================================
echo Zero-CPU v0.1 Smoke Test FAILED
echo ========================================
exit /b 1
'@

$releaseNotes = @'
# Zero-CPU v0.1 Release Notes

Zero-CPU v0.1 is the first portfolio-ready milestone of the project.

The project has evolved from a simple CPU simulator into a small virtual computer platform with a custom ISA, assembler, binary format, virtual memory, interrupts, MMIO devices, syscalls, a mini-kernel flow, a BIO-OS demo, and a Win32 Studio debugger/demo runner.

## Highlights

```text
- Custom Zero-CPU ISA
- .zasm assembly language
- Assembler
- Instruction encoder / decoder
- .zbin binary format
- Binary writer / reader / loader
- Virtual memory
- Fetch / decode / execute CPU loop
- Register file
- ALU
- FLAGS
- Stack
- CALL / RET
- MMIO bus
- DebugOutputDevice
- TimerDevice
- InterruptController
- EI / DI
- INT / IRET
- INT 80 syscall convention
- Mini-kernel syscall handler
- BIO-OS combined boot demo
- CLI BIO-OS runner
- Studio BIO-OS runner
- Studio system panel
- Shared BioOSRunner module
```

## BIO-OS demo

BIO-OS combines:

```text
examples\bio_os\boot.zasm
examples\bio_os\kernel.zasm
examples\bio_os\user_program.zasm
```

into:

```text
examples\bio_os\combined_boot.zasm
examples\bio_os\combined_boot.zbin
```

The demo exercises:

```text
- boot code
- mini-kernel syscall dispatch
- user program execution
- INT 80 software interrupt
- timer MMIO device
- hardware-style timer interrupt
- debug output MMIO
- IRET return flow
```

Expected output:

```text
Debug ASCII: BU
Timer Interrupt Count: 1
Timer Enabled: false
Exit Code: 0
```

## CLI demo

```bat
.\build\Debug\zero_cli.exe run-os examples\bio_os
```

Expected signs:

```text
BIO-OS run finished successfully.
ASCII view:
BU
Timer interrupt count = 1
Timer enabled = false
```

## Studio demo

```bat
.\build\Debug\zero_studio.exe
```

Click:

```text
Run BIO-OS
```

Expected trace:

```text
[Studio BIO-OS Run via BioOSRunner]
BIO-OS execution finished successfully.
Debug ASCII: BU
Timer Interrupt Count: 1
Timer Enabled: false
```

Expected system panel:

```text
Debug ASCII = BU
Timer Interrupt Count = 1
Timer Enabled = false
```

## Shared runner

The BIO-OS execution flow is now centralized in:

```text
include/zero_cpu/system/BioOSRunner.hpp
src/system/BioOSRunner.cpp
```

CLI and Studio both use the shared runner.

```text
zero_cli run-os
    ↓
BioOSRunner

Studio Run BIO-OS
    ↓
BioOSRunner::runOn(...)
```

## Smoke test

```bat
scripts\v0_1_smoke_test.bat
```

This verifies:

```text
- build
- full test suite
- BioOSRunner test command
- public run-os command
- generated BIO-OS artifacts
- Studio executable presence
```

## Suggested tag

After the smoke test passes:

```bat
git tag v0.1
git push origin v0.1
```
'@

$demoGuide = @'
# Zero-CPU v0.1 Demo Guide

This is the recommended demo flow for showing Zero-CPU v0.1 in a portfolio or interview.

## 1. Explain the project in one sentence

```text
Zero-CPU is a small virtual computer platform with a custom ISA, assembler, binary format, interrupts, MMIO, syscalls, a mini-kernel style BIO-OS demo, and a Win32 Studio debugger/demo runner.
```

## 2. Run the smoke test

```bat
scripts\v0_1_smoke_test.bat
```

Expected:

```text
Zero-CPU v0.1 Smoke Test PASSED
```

## 3. Show the CLI OS demo

```bat
.\build\Debug\zero_cli.exe run-os examples\bio_os
```

Point out:

```text
BIO-OS run finished successfully.
ASCII view:
BU
Timer interrupt count = 1
Timer enabled = false
```

Explain:

```text
B = boot code wrote to DebugOutputDevice
U = user program wrote to DebugOutputDevice
Timer interrupt count = timer hardware-style interrupt fired
Timer enabled = false = timer handler disabled the timer after handling it
```

## 4. Show Studio

```bat
.\build\Debug\zero_studio.exe
```

Click:

```text
Run BIO-OS
```

Point out:

```text
[Studio BIO-OS Run via BioOSRunner]
Debug ASCII: BU
Timer Interrupt Count: 1
Timer Enabled: false
```

Then point out the System Panel:

```text
Debug MMIO = 0xf000..0xf00f
Timer MMIO = 0xf100..0xf12f
Syscall Vector = 80
Default Timer Vector = 44
Debug ASCII = BU
Timer Interrupt Count = 1
```

## 5. Show the source files

```text
examples\bio_os\boot.zasm
examples\bio_os\kernel.zasm
examples\bio_os\user_program.zasm
```

Explain:

```text
boot.zasm starts the system
kernel.zasm handles INT 80 syscalls and timer interrupts
user_program.zasm behaves like a tiny user process
```

## 6. Show the architecture files

```text
include\zero_cpu\system\BioOSRunner.hpp
src\system\BioOSRunner.cpp
```

Explain:

```text
BioOSRunner is the shared execution module used by both CLI and Studio.
```

## 7. Closing pitch

```text
This project demonstrates low-level systems design: instruction encoding, binary loading, CPU state management, interrupts, MMIO, syscall convention design, a minimal kernel flow, and debugging tooling.
```
'@

Save-Utf8NoBom -Path $smokePath -Text $smoke
Save-Utf8NoBom -Path $releaseNotesPath -Text $releaseNotes
Save-Utf8NoBom -Path $demoGuidePath -Text $demoGuide

Write-Host "PATCH OK"
Write-Host "Created:"
Write-Host "  scripts\v0_1_smoke_test.bat"
Write-Host "  docs\v0.1-release-notes.md"
Write-Host "  docs\v0.1-demo-guide.md"
Write-Host ""
Write-Host "Next:"
Write-Host "  scripts\v0_1_smoke_test.bat"
