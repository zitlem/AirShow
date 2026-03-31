@echo off
REM AirShow — Build and Run (Windows / MSYS2 MinGW-64)
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "PRESET=windows-msys2-debug"
set "BUILD_DIR=build\windows-debug"

echo === AirShow Build ^& Run ===
echo Platform: Windows (MSYS2 MinGW-64)
echo Preset:   %PRESET%
echo Build:    %BUILD_DIR%
echo.

REM --- Check MSYS2 ---
if not exist "C:\msys64\mingw64\bin\gcc.exe" (
    echo ERROR: MSYS2 MinGW-64 not found at C:\msys64
    echo.
    echo Install MSYS2 from https://www.msys2.org/ then run:
    echo   pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
    echo   pacman -S mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qt6-declarative
    echo   pacman -S mingw-w64-x86_64-gstreamer mingw-w64-x86_64-gst-plugins-base
    echo   pacman -S mingw-w64-x86_64-gst-plugins-good mingw-w64-x86_64-gst-plugins-bad
    echo   pacman -S mingw-w64-x86_64-gst-libav
    echo   pacman -S mingw-w64-x86_64-openssl mingw-w64-x86_64-libupnp
    echo   pacman -S mingw-w64-x86_64-libplist
    exit /b 1
)

REM Add MSYS2 to PATH for this session
set "PATH=C:\msys64\mingw64\bin;%PATH%"

REM --- Check tools ---
where cmake >nul 2>&1 || (
    echo ERROR: cmake not found. Install: pacman -S mingw-w64-x86_64-cmake
    exit /b 1
)
where ninja >nul 2>&1 || (
    echo ERROR: ninja not found. Install: pacman -S mingw-w64-x86_64-ninja
    exit /b 1
)

REM --- Configure ---
if not exist "%BUILD_DIR%\build.ninja" (
    echo === Configuring ^(%PRESET%^) ===
    cmake --preset %PRESET%
    if errorlevel 1 (
        echo ERROR: CMake configure failed.
        exit /b 1
    )
    echo.
)

REM --- Build ---
echo === Building ===
cmake --build %BUILD_DIR% --target airshow
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)
echo.

REM --- Run ---
set "BINARY=%BUILD_DIR%\airshow.exe"
if not exist "%BINARY%" (
    echo ERROR: Binary not found at %BINARY%
    exit /b 1
)

echo === Running AirShow ===
echo Press Ctrl+C to stop.
echo.
"%BINARY%" %*
