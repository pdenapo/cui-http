#!/usr/bin/env bash
# Build the Linux version with the GLFW backend.
# Recommended on Linux: GLFW talks to X11 and Wayland natively, so the same
# binary works on both without XWayland.
cmake -S . -B build_linux -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=glfw
cmake --build build_linux -j 2
# ./build_linux/cui_http