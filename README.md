# cui-http — native API client

Documentation: https://pdenapo.github.io/cui-http/

A blazing-fast, lightweight, native API client (Postman/Insomnia alternative) for
Linux. The frontend is C99 on top of the [CUI](https://github.com/yappologistic/CUI)
framework; the backend is C99, handling HTTP via libcurl and persistence via
SQLite. No web views, instant startup, tiny memory footprint.

## Features

- Request view: method + URL + Send bar (`cui_request_bar`), query-parameter
  editor synced with the URL, request body editor, **request and response
  headers editors/viewers**, response status/latency/size, JSON tree preview or
  raw text, copy button.
- Async engine: requests never block the UI; the Send button doubles as Cancel.
- History view: every request is persisted (method, url, body, request/response
  headers, status, latency, size) in SQLite; click a row to reload it.
- Settings: language (multiple languages supported) and light/dark theme, both
  persisted in the database and restored on startup; the language switch is in
  the top bar next to the theme toggle.
- Command palette (Ctrl+K) and Ctrl+Enter to send.
- Command line client (`cui_http_cli`) sharing the same backend and history.

For now, this project has been only tested on Linux, but should work in other platforms as well. Your help is wellcome!

## Layout

```
api-tester/
├── api-tester-backend/
│   ├── include/backend.h         # shared structs + backend declarations
│   └── src/
│       ├── backend.c             # SQLite + libcurl + worker thread
│       ├── cli.c                 # command line client
│       └── test.c                # standalone test program
├── source-frontend/main.c        # C frontend
├── vendor/CUI/                   # CUI framework (submodule)
├── vendor/d2sqlite3/             # (legacy, unused)
├── tests/server/                 # small vibe.d REST API for end-to-end tests
├── tests/run_tests.sh            # 25 end-to-end checks
├── CMakeLists.txt
├── build_x11.sh / build_glfw.sh / build_sdl2.sh
└── AGENTS.md
```

## Getting the code 

```sh
git clone --recurse-submodules https://github.com/pdenapo/cui-http.git
```

## Prerequisites

### Linux (Arch)
```sh
sudo pacman -S base-devel cmake ninja sqlite curl libx11 libglvnd
```
### Linux (Debian / Ubuntu)
```sh
sudo apt-get install -y build-essential cmake ninja-build libsqlite3-dev libcurl4-openssl-dev libx11-dev libgl-dev
```
### Windows
- CMake + Ninja (`choco install cmake ninja`)
- SQLite and libcurl dev libraries (vcpkg or prebuilt)
- Visual Studio Build Tools (MSVC) or MinGW
### macOS
```sh
brew install cmake ninja sqlite curl glfw
```
Xcode Command Line Tools are required for the compiler.

## Build

There are several native frontends (one per platform / display server). Pick the
one that matches your system; the backend is the same in every case.

| Platform | Recommended frontend | Build script | CMake flag |
|---|---|---|---|
| Linux (X11 or Wayland) | **GLFW** (native on both) | `./build_linux.sh` | `-DCUI_PLATFORM=glfw` |
| Linux (X11 only) | X11 | `./build_x11.sh` | `-DCUI_PLATFORM=x11` |
| Linux (SDL2) | SDL2 | `./build_sdl2.sh` | `-DCUI_PLATFORM=sdl2` |
| Windows | Win32 (or GLFW/SDL2) | `build_windows.bat` | `-DCUI_PLATFORM=win32` |
| macOS | Cocoa (or GLFW/SDL2) | `./build_macos.sh` | `-DCUI_PLATFORM=cocoa` |

On Linux we recommend **GLFW for everyone**: it speaks to X11 and Wayland
natively, so a single binary works on both without XWayland. Use X11/SDL2 only
if you have a specific reason.

```sh
# Linux (recommended)
./build_linux.sh
./build_linux/cui_http

# macOS
./build_macos.sh
./build_macos/cui_http

# Windows (cmd)
build_windows.bat
build_windows\cui_http.exe
```

Manual equivalent:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=glfw
cmake --build build -j 2
```

Do **not** use dub to build or link the backend — the whole project is now pure
C99. CMake compiles `backend.c` directly into `cui_http` and `cui_http_cli`.

```sh
./build_x11.sh            # X11 (use build_glfw.sh / build_sdl2.sh for the others)
./build_x11/api_tester     # run the GUI
```

Standard workflow:

```sh
cmake -S . -B build_x11 -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=x11
cmake --build build_x11 -j 2
```

CMake also builds `api_tester_d_test` (D test program) and `cui_http_cli`.

## Command line client

```sh
./build_x11/cui_http_cli GET https://api.example.com/v1/users
./build_x11/cui_http_cli POST https://api.example.com/v1/users -d '{"name":"Ada"}'
./build_x11/cui_http_cli -X PUT https://api.example.com/v1/users/1 -d '{"name":"Ada"}' -H "X-Api: 1"
./build_x11/cui_http_cli history 20
./build_x11/cui_http_cli clear
```

History database: `$CUI_HTTP_DB`, `--db PATH`, or the OS-conventional location
(`$XDG_DATA_HOME/cui-http/requests.db` / `~/.local/share/cui-http/requests.db`).

## End-to-end tests

```sh
./tests/run_tests.sh          # 25 end-to-end checks
cd build && ctest --output-on-failure   # via CTest (backend_unit + e2e_cli)
```

## How to release

For exmaple

git tag -a v0.1.3 -m "cui-http 0.1.3"
git push origin v0.1.3
