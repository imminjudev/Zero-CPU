@echo off
setlocal EnableExtensions

cd /d "%~dp0\.."

echo.
echo ========================================
echo Zero-CPU CTest Suite
echo ========================================
echo.

cmake -S . -B build
if errorlevel 1 goto fail

cmake --build build --config Debug
if errorlevel 1 goto fail

ctest --test-dir build -C Debug --output-on-failure
if errorlevel 1 goto fail

echo.
echo ========================================
echo All registered CTest tests passed.
echo ========================================
echo.
exit /b 0

:fail
echo.
echo ========================================
echo Zero-CPU CTest suite failed.
echo ========================================
echo.
exit /b 1
