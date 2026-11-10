# Build

There are several native frontends (one per platform / display server). The
backend is the same in every case.

| Platform | Recommended frontend | Script | CMake flag |
|---|---|---|---|
| Linux (X11 or Wayland) | GLFW (native on both) | `./build_linux.sh` | `-DCUI_PLATFORM=glfw` |
| Linux (X11 only) | X11 | `./build_x11.sh` | `-DCUI_PLATFORM=x11` |
| Linux (SDL2) | SDL2 | `./build_sdl2.sh` | `-DCUI_PLATFORM=sdl2` |
| Windows | Win32 (or GLFW/SDL2) | `build_windows.bat` | `-DCUI_PLATFORM=win32` |
| macOS | Cocoa (or GLFW/SDL2) | `./build_macos.sh` | `-DCUI_PLATFORM=cocoa` |

On Linux we recommend **GLFW for everyone**: it speaks to X11 and Wayland
natively, so a single binary works on both without XWayland.

Manual build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=glfw
cmake --build build -j 2
```

CMake never uses `dub` for the backend; the whole project is pure C99. CMake
compiles `backend.c` directly into `cui_http` and `cui_http_cli`.