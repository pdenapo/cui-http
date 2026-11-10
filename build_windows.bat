@echo off
REM Build the Windows version (native Win32 backend).
REM Requires: CMake, Ninja, LDC, SQLite and libcurl dev libraries in PATH.
cmake -S . -B build_windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=win32
if errorlevel 1 exit /b 1
cmake --build build_windows --parallel
if errorlevel 1 exit /b 1
echo Build OK: build_windows\cui_http.exe