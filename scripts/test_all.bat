@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0\.."

echo.
echo ========================================
echo Zero-CPU Test Suite
echo ========================================
echo.

echo [1/32] Building project...
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

echo.
echo [2/32] Running syscall table command...
"%ZERO_CLI%" syscall-table
if errorlevel 1 goto fail

echo [3/32] Running ALU unit test...
"%ZERO_CLI%" alu-test
if errorlevel 1 goto fail

echo.
echo [4/32] Running trace JSON writer test...
"%ZERO_CLI%" trace-json-test
if errorlevel 1 goto fail

echo.
echo [5/32] Running trace JSON diff test...
"%ZERO_CLI%" trace-diff-test
if errorlevel 1 goto fail

echo.
echo [6/32] Running golden trace regression test...
"%ZERO_CLI%" trace-golden-test
if errorlevel 1 goto fail

echo.
echo [7/32] Running MMIO bus test...
"%ZERO_CLI%" mmio-test
if errorlevel 1 goto fail

echo.
echo [8/32] Running hardware bus integration test...
"%ZERO_CLI%" hardware-bus-test
if errorlevel 1 goto fail

echo.
echo [9/32] Running serial hardware protocol test...
"%ZERO_CLI%" serial-hardware-test
if errorlevel 1 goto fail

echo.
echo [10/32] Running interrupt controller test...
"%ZERO_CLI%" interrupt-test
if errorlevel 1 goto fail

echo.
echo [11/32] Running CPU interrupt delivery test...
"%ZERO_CLI%" cpu-interrupt-test
if errorlevel 1 goto fail

echo.
echo [12/32] Running timer device test...
"%ZERO_CLI%" timer-test
if errorlevel 1 goto fail

echo.
echo [13/32] Running CPU timer interrupt test...
"%ZERO_CLI%" cpu-timer-test
if errorlevel 1 goto fail

echo.
echo [14/32] Running CPU EI/DI interrupt control test...
"%ZERO_CLI%" cpu-ei-di-test
if errorlevel 1 goto fail

echo.
echo [15/32] Running software interrupt test...
"%ZERO_CLI%" software-interrupt-test
if errorlevel 1 goto fail


echo.
echo [16/32] Running interrupt FLAGS restore test...
"%ZERO_CLI%" interrupt-flags-restore-test
if errorlevel 1 goto fail

echo.
echo [17/32] Running register-indirect memory test...
"%ZERO_CLI%" register-indirect-test
if errorlevel 1 goto fail

echo.
echo [18/32] Running mini kernel syscall test...
"%ZERO_CLI%" mini-kernel-syscall-test
if errorlevel 1 goto fail

echo.
echo [19/32] Running mini kernel syscall 2 test...
"%ZERO_CLI%" mini-kernel-syscall2-test
if errorlevel 1 goto fail

echo.
echo [20/32] Running mini kernel syscall 3 exit test...
"%ZERO_CLI%" mini-kernel-syscall3-test
if errorlevel 1 goto fail

echo.
echo [21/32] Running mini kernel syscall 4 timer read test...
"%ZERO_CLI%" mini-kernel-syscall4-timer-read-test
if errorlevel 1 goto fail

echo.
echo [22/32] Running mini kernel syscall 5 timer enable test...
"%ZERO_CLI%" mini-kernel-syscall5-timer-enable-test
if errorlevel 1 goto fail

echo.
echo [23/32] Running mini kernel syscall 6 timer disable test...
"%ZERO_CLI%" mini-kernel-syscall6-timer-disable-test
if errorlevel 1 goto fail

echo.
echo [24/32] Running mini kernel syscall 7 timer configure test...
"%ZERO_CLI%" mini-kernel-syscall7-timer-configure-test
if errorlevel 1 goto fail

echo.
echo [25/32] Running mini kernel timer lifecycle test...
"%ZERO_CLI%" mini-kernel-timer-lifecycle-test
if errorlevel 1 goto fail

echo.
echo [26/32] Running BIO-OS combined boot test...
"%ZERO_CLI%" bio-os-combined-boot-test
if errorlevel 1 goto fail

echo.
echo [27/32] Running binary format round-trip test...
"%ZERO_CLI%" binary-test
if errorlevel 1 goto fail

echo.
echo [28/32] Assembling and running function_call.zasm...
"%ZERO_CLI%" assemble "examples\function_call.zasm" "examples\function_call.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" run-binary "examples\function_call.zbin" --expect-memory 100=20 2048=20
if errorlevel 1 goto fail

echo.
echo [29/32] Assembling and running alu_flags.zasm...
"%ZERO_CLI%" assemble "examples\alu_flags.zasm" "examples\alu_flags.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" run-binary "examples\alu_flags.zbin" --expect-memory 120=30 128=20 136=1 144=2 152=3 160=4 168=5 200=777
if errorlevel 1 goto fail

echo.
echo [30/32] Assembling and running mmio_output.zasm...
"%ZERO_CLI%" assemble "examples\mmio_output.zasm" "examples\mmio_output.zbin"
if errorlevel 1 goto fail

"%ZERO_CLI%" run-binary "examples\mmio_output.zbin" --debug-mmio --expect-memory 220=66 228=2
if errorlevel 1 goto fail

echo.
echo [31/32] Running signed branch correctness test...
"%ZERO_CLI%" signed-branch-test
if errorlevel 1 goto fail

echo.
echo [32/32] Running vector/binary differential test...
"%ZERO_CLI%" differential-test
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
