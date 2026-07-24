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