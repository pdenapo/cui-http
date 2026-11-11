#!/usr/bin/env bash
# Build the macOS version (native Cocoa backend).
# No extra dependencies beyond Xcode Command Line Tools.
cmake -S . -B build_macos -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=cocoa
cmake --build build_macos -j 2
# ./build_macos/cui_http