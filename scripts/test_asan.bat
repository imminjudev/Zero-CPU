@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0\.."

echo.
echo ========================================
echo Zero-CPU Sanitizer Test Suite
echo ========================================
echo.

set "ASAN_BUILD=build-asan"
set "ASAN_DLL=clang_rt.asan_dynamic-x86_64.dll"

cmake -S . -B "%ASAN_BUILD%" ^
    -DZERO_CPU_ENABLE_SANITIZERS=ON
if errorlevel 1 goto fail

cmake --build "%ASAN_BUILD%" --config Debug
if errorlevel 1 goto fail

rem Locate the MSVC AddressSanitizer runtime.
set "ASAN_RUNTIME_PATH="

for /f "delims=" %%I in ('where "%ASAN_DLL%" 2^>nul') do (
    if not defined ASAN_RUNTIME_PATH (
        set "ASAN_RUNTIME_PATH=%%I"
    )
)

if not defined ASAN_RUNTIME_PATH (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

    if exist "!VSWHERE!" (
        set "VS_INSTALL_PATH="

        for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            set "VS_INSTALL_PATH=%%I"
        )

        if defined VS_INSTALL_PATH (
            for /f "delims=" %%I in ('where /r "!VS_INSTALL_PATH!\VC\Tools\MSVC" "%ASAN_DLL%" 2^>nul') do (
                if not defined ASAN_RUNTIME_PATH (
                    set "ASAN_RUNTIME_PATH=%%I"
                )
            )
        )
    )
)

if not defined ASAN_RUNTIME_PATH (
    echo.
    echo ERROR: %ASAN_DLL% was not found.
    echo.
    echo Open Visual Studio Installer and modify the installed
    echo Visual Studio workload. Make sure C++ AddressSanitizer
    echo support is installed.
    echo.
    echo You can also run this script from:
    echo   x64 Native Tools Command Prompt for VS 2022
    goto fail
)

echo AddressSanitizer runtime:
echo   !ASAN_RUNTIME_PATH!
echo.

copy /y "!ASAN_RUNTIME_PATH!" ^
    "%ASAN_BUILD%\Debug\%ASAN_DLL%" >nul
if errorlevel 1 goto fail

ctest --test-dir "%ASAN_BUILD%" ^
    -C Debug ^
    --output-on-failure
if errorlevel 1 goto fail

echo.
echo ========================================
echo All sanitizer tests passed.
echo ========================================
echo.
exit /b 0

:fail
echo.
echo ========================================
echo Zero-CPU sanitizer tests failed.
echo ========================================
echo.
exit /b 1
