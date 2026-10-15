@echo off
setlocal

:: CTR Native MSVC Build Script (Windows x86)
:: Requires Visual Studio 2022 or Build Tools with the Desktop C++ workload.
:: --package also writes dist\PowerSlide-<version>-windows-x86.zip (package-windows.ps1).

set "CTR_NATIVE_PACKAGE=0"
if /I "%~1"=="--package" set "CTR_NATIVE_PACKAGE=1"

where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: cmake not found in PATH
    echo Install CMake 3.20 or newer, or the Visual Studio C++ CMake tools component.
    exit /b 1
)

cmake --preset windows-msvc-x86
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configure failed
    exit /b 1
)

cmake --build --preset windows-msvc-x86-release --parallel
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed
    exit /b 1
)

ctest --preset windows-msvc-x86-release
if %ERRORLEVEL% neq 0 (
    echo ERROR: Smoke test failed
    exit /b 1
)

if "%CTR_NATIVE_PACKAGE%"=="1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-windows.ps1"
    if errorlevel 1 (
        echo ERROR: Packaging failed
        exit /b 1
    )
)

echo.
echo Build succeeded: build-msvc-x86\Release\ctr_native.exe
