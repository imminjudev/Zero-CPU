@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0\.."

echo.
echo ========================================
echo Zero-CPU Test Suite
echo ========================================
echo.

echo [1/23] Building project...
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

echo [2/23] Running ALU unit test...
"%ZERO_CLI%" alu-test
if errorlevel 1 goto fail

echo.
echo [3/23] Running MMIO bus test...
"%ZERO_CLI%" mmio-test
if errorlevel 1 goto fail

echo.
echo [4/23] Running interrupt controller test...
"%ZERO_CLI%" interrupt-test
if errorlevel 1 goto fail

echo.
echo [5/23] Running CPU interrupt delivery test...
"%ZERO_CLI%" cpu-interrupt-test
if errorlevel 1 goto fail

echo.
echo [6/23] Running timer device test...
"%ZERO_CLI%" timer-test
if errorlevel 1 goto fail

echo.
echo [7/23] Running CPU timer interrupt test...
"%ZERO_CLI%" cpu-timer-test
if errorlevel 1 goto fail

echo.
echo [8/23] Running CPU EI/DI interrupt control test...
"%ZERO_CLI%" cpu-ei-di-test
if errorlevel 1 goto fail

echo.
echo [9/23] Running software interrupt test...
"%ZERO_CLI%" software-interrupt-test
if errorlevel 1 goto fail

echo.
echo [10/23] Running register-indirect memory test...
"%ZERO_CLI%" register-indirect-test
if errorlevel 1 goto fail

echo.
echo [11/23] Running mini kernel syscall test...
"%ZERO_CLI%" mini-kernel-syscall-test
if errorlevel 1 goto fail

echo.
echo [12/23] Running mini kernel syscall 2 test...
"%ZERO_CLI%" mini-kernel-syscall2-test
if errorlevel 1 goto fail

echo.
echo [13/23] Running mini kernel syscall 3 exit test...
"%ZERO_CLI%" mini-kernel-syscall3-test
if errorlevel 1 goto fail

echo.
echo [14/23] Running mini kernel syscall 4 timer read test...
"%ZERO_CLI%" mini-kernel-syscall4-timer-read-test
if errorlevel 1 goto fail

echo.
echo [15/23] Running mini kernel syscall 5 timer enable test...
"%ZERO_CLI%" mini-kernel-syscall5-timer-enable-test
if errorlevel 1 goto fail

echo.
echo [16/23] Running mini kernel syscall 6 timer disable test...
"%ZERO_CLI%" mini-kernel-syscall6-timer-disable-test
if errorlevel 1 goto fail

echo.
echo [17/23] Running mini kernel syscall 7 timer configure test...
"%ZERO_CLI%" mini-kernel-syscall7-timer-configure-test
if errorlevel 1 goto fail

echo.
echo [18/23] Running mini kernel timer lifecycle test...
"%ZERO_CLI%" mini-kernel-timer-lifecycle-test
if errorlevel 1 goto fail

echo.
echo [19/23] Running BIO-OS combined boot test...
"%ZERO_CLI%" bio-os-combined-boot-test
if errorlevel 1 goto fail

echo.
echo [20/23] Running binary format round-trip test...
"%ZERO_CLI%" binary-test
if errorlevel 1 goto fail

echo.
echo [21/23] Assembling and running function_call.zasm...
"%ZERO_CLI%" assemble "examples\function_call.zasm" "examples\function_call.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" run-binary "examples\function_call.zbin" --expect-memory 100=20 2048=20
if errorlevel 1 goto fail

echo.
echo [22/23] Assembling and running alu_flags.zasm...
"%ZERO_CLI%" assemble "examples\alu_flags.zasm" "examples\alu_flags.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" run-binary "examples\alu_flags.zbin" --expect-memory 120=30 128=20 136=1 144=2 152=3 160=4 168=5 200=777
if errorlevel 1 goto fail

echo.
echo [23/23] Assembling and running mmio_output.zasm...
"%ZERO_CLI%" assemble "examples\mmio_output.zasm" "examples\mmio_output.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" run-binary "examples\mmio_output.zbin" --debug-mmio --expect-memory 220=66 228=2
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
