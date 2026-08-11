@echo off
setlocal EnableExtensions

cd /d "%~dp0\.."

if "%~1"=="" goto usage

set "PORT=%~1"
set "BAUD=115200"

if not "%~2"=="" set "BAUD=%~2"

echo.
echo ========================================
echo Zero-CPU Protected ESP32 GPIO Demo
echo ========================================
echo.
echo Port: %PORT%
echo Baud: %BAUD%
echo.

echo [1/3] Building Zero-CPU...
cmake --build build
if errorlevel 1 goto fail

set "ZERO_CLI=build\Debug\zero_cli.exe"

if not exist "%ZERO_CLI%" (
    set "ZERO_CLI=build\Release\zero_cli.exe"
)

if not exist "%ZERO_CLI%" (
    echo ERROR: zero_cli.exe not found.
    echo Tried:
    echo   build\Debug\zero_cli.exe
    echo   build\Release\zero_cli.exe
    goto fail
)

echo.
echo [2/3] Assembling protected GPIO User process...
"%ZERO_CLI%" assemble "examples\protected_hardware_gpio_live.zasm" "build\protected_hardware_gpio_live.zbin"
if errorlevel 1 goto fail

echo.
echo [3/3] Running User process through protected serial hardware...
"%ZERO_CLI%" run-processes --quantum 100 --max-steps 200 --protected-syscalls --hardware-serial "%PORT%" --baud %BAUD% --expect-exit 1=0 "build\protected_hardware_gpio_live.zbin"
if errorlevel 1 goto fail

echo.
echo [PASS] Protected ESP32 GPIO demo completed.
echo GPIO output offset 0 was written and read back through INT 80.
echo ESP32 GPIO 2 should now be HIGH.
echo.
echo To return GPIO 2 to LOW, run:
echo   "%ZERO_CLI%" hardware-live-test %PORT% %BAUD%
echo.
exit /b 0

:usage
echo Usage:
echo   scripts\run_esp32_protected_gpio_demo.bat COM3 [baud]
echo.
echo Example:
echo   scripts\run_esp32_protected_gpio_demo.bat COM3 115200
exit /b 2

:fail
echo.
echo [FAIL] Protected ESP32 GPIO demo failed.
echo Check the COM port, ESP32 firmware, Serial Monitor ownership, and wiring.
exit /b 1

rem Patch: v1.7-protected-esp32-gpio-demo-r1
