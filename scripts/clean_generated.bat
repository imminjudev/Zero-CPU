@echo off
setlocal EnableExtensions

cd /d "%~dp0\.."

echo.
echo ========================================
echo Cleaning Zero-CPU generated artifacts
echo ========================================
echo.

echo Removing generated virtual binaries...
del /q "examples\*.zbin" 2>nul
del /q "examples\bio_os\*.zbin" 2>nul

echo Removing generated combined BIO-OS source...
del /q "examples\bio_os\combined_boot.zasm" 2>nul

echo Removing downloaded/generated ZIP files in repository root...
del /q "*.zip" 2>nul

echo Removing generated extracted artifact folders...
for /d %%D in (*_files) do (
    echo Removing %%D
    rmdir /s /q "%%D"
)

for /d %%D in (zero_cpu_*_files) do (
    echo Removing %%D
    rmdir /s /q "%%D"
)

if exist "register_indirect_full_files" (
    echo Removing register_indirect_full_files
    rmdir /s /q "register_indirect_full_files"
)

if exist "zero_cpu_register_indirect_full_files" (
    echo Removing zero_cpu_register_indirect_full_files
    rmdir /s /q "zero_cpu_register_indirect_full_files"
)

echo.
echo Git status:
git status --short --ignored

echo.
echo Done.
