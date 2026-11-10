# cui-http — native API client

A blazing-fast, lightweight, native API client (Postman/Insomnia alternative) for
Linux. The frontend is C99 on top of the [CUI](https://github.com/yappologistic/CUI)
framework; the backend is D, compiled into a static library, handling HTTP via
libcurl and persistence via SQLite. No web views, instant startup, tiny memory
footprint.

## Features

- Request view: method + URL + Send bar (`cui_request_bar`), query-parameter
  editor synced with the URL, request body editor, **request and response
  headers editors/viewers**, response status/latency/size, JSON tree preview or
  raw text, copy button.
- Async engine: requests never block the UI; the Send button doubles as Cancel.
- History view: every request is persisted (method, url, body, request/response
  headers, status, latency, size) in SQLite; click a row to reload it.
- Settings: English / Español language switch and light/dark theme, both
  persisted in the database and restored on startup.
- Command palette (Ctrl+K) and Ctrl+Enter to send.
- Command line client (`cui_http_cli`) sharing the same backend and history.

For now, this project only works on Linux...

## Layout

```
api-tester/
├── api-tester-backend/
│   ├── include/bridge.h          # C↔D ABI contract
│   ├── source_lib/backend.d      # D backend: SQLite + libcurl + C ABI exports
│   ├── source/app.d              # standalone D test program (built by CMake)
│   └── source/cli.d              # command line client (built by CMake)
├── source-frontend/main.c        # C frontend
├── CUI/                          # CUI framework (local copy / submodule)
├── vendor/d2sqlite3/             # d2sqlite3 submodule
├── tests/server/                 # small vibe.d REST API for end-to-end tests
├── tests/run_tests.sh            # 23 end-to-end checks
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
sudo pacman -S base-devel cmake ninja ldc sqlite curl libx11 libglvnd
```
### Linux (Debian / Ubuntu)
```sh
sudo apt-get install -y build-essential cmake ninja-build ldc libsqlite3-dev libcurl4-openssl-dev libx11-dev libgl-dev
```
### Windows
- [LDC](https://github.com/ldc-developers/ldc/releases) (add to PATH)
- CMake + Ninja (`choco install cmake ninja`)
- SQLite and libcurl dev libraries (vcpkg or prebuilt)
- Visual Studio Build Tools (MSVC) or MinGW
### macOS
```sh
brew install cmake ninja ldc sqlite curl
```
Xcode Command Line Tools are required for the compiler.

## Which frontend for Wayland?

On Wayland, prefer a backend with native Wayland support:

- **Recommended: `glfw`** — GLFW 3.4+ talks to Wayland directly (no XWayland).
- **`sdl2`** — also native Wayland (SDL2 ≥ 2.24).
- **`x11`** — works through XWayland (fine, but not native; default of `build_x11.sh`).

Build for Wayland with:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=glfw
cmake --build build -j 2
```

## Build

Do **not** use dub to build or link the backend — CMake invokes `ldc2 -lib`
directly over `backend.d` plus the d2sqlite3 sources and links the result into
the C frontend.

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
./tests/run_tests.sh   # 23 checks, all must pass
```

## Notes

- The D `HTTP` type is a struct whose value constructor runs `initialize()`.
  Always write `auto client = HTTP();` (never `new HTTP()`), otherwise the curl
  handle stays null.
- `extern (C)` parameters take `const char*`; D `string` does not decay to a
  pointer, so pass `cast(const(char)*) toStringz(s)`.
- Response bodies/headers handed to C are fresh `malloc`'ed copies owned by the
  caller (`free_response()` / `free_string()`).

`requests.db` is the SQLite database created at runtime (git-ignored).
