@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0\.."

echo.
echo ========================================
echo Zero-CPU Test Suite
echo ========================================
echo.

echo [1/6] Building project...
cmake --build build
if errorlevel 1 goto fail

set "ZERO_CLI=build\Debug\zero_cli.exe"

if not exist "%ZERO_CLI%" (
    set "ZERO_CLI=build\Release\zero_cli.exe"
)

if not exist "%ZERO_CLI%" (
    echo.
    echo ERROR: zero_cli.exe not found.
    echo Tried:
    echo   build\Debug\zero_cli.exe
    echo   build\Release\zero_cli.exe
    goto fail
)

echo.
echo Using CLI:
echo   %ZERO_CLI%
echo.

echo [2/6] Running ALU unit test...
"%ZERO_CLI%" alu-test
if errorlevel 1 goto fail

echo.
echo Running MMIO bus test...
"%ZERO_CLI%" mmio-test
if errorlevel 1 goto fail

echo.
echo Running interrupt controller test...
"%ZERO_CLI%" interrupt-test
if errorlevel 1 goto fail

echo.
echo Running CPU interrupt delivery test...
"%ZERO_CLI%" cpu-interrupt-test
if errorlevel 1 goto fail

echo.
echo [3/6] Running binary format round-trip test...
"%ZERO_CLI%" binary-test
if errorlevel 1 goto fail

echo.
echo [4/6] Assembling function_call.zasm...
"%ZERO_CLI%" assemble examples\function_call.zasm examples\function_call.zbin
if errorlevel 1 goto fail

echo.
echo [5/6] Running function_call.zbin...
"%ZERO_CLI%" run-binary examples\function_call.zbin --expect-memory 100=20 2048=20
if errorlevel 1 goto fail

echo.
echo [6/6] Assembling and running alu_flags.zasm...
"%ZERO_CLI%" assemble examples\alu_flags.zasm examples\alu_flags.zbin
if errorlevel 1 goto fail

"%ZERO_CLI%" run-binary examples\alu_flags.zbin --expect-memory 120=30 128=20 136=1 144=2 152=3 160=4 168=5 200=777
if errorlevel 1 goto fail

echo.
echo ========================================
echo All Zero-CPU tests passed.
echo ========================================
echo.

exit /b 0

:fail
echo.
echo ========================================
echo Zero-CPU tests failed.
echo ========================================
echo.

exit /b 1

echo.
echo Running MMIO output example...
"%ZERO_CLI%" assemble examples\mmio_output.zasm examples\mmio_output.zbin
if errorlevel 1 goto fail

"%ZERO_CLI%" run-binary examples\mmio_output.zbin --debug-mmio --expect-memory 220=66 228=2
if errorlevel 1 goto fail