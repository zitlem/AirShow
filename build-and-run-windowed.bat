@echo off
REM AirShow — Build and Run in Windowed Mode (Windows / MSYS2 MinGW-64)
REM Same as build-and-run.bat but passes --windowed to the binary.
call "%~dp0build-and-run.bat" --windowed %*
