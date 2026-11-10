# Project Specification: Native CUI HTTP Client (`cui-http`)

## 1. Overview & Goals
Build a blazing-fast, lightweight, native API client (Postman/Insomnia alternative) for Linux.
- **Frontend:** C99 using the [CUI](https://github.com/yappologistic/CUI) framework, specifically leveraging native components like `cui_request_bar`, `cui_api_explorer`, tables, tabs and the JSON viewer.
- **Backend & Logic:** C99, handling networking via `libcurl` and persistence via `SQLite` (through the `sqlite3` C API). Compiled directly into the same binary — no separate runtime, no FFI bridge.
- **Core Philosophy:** Zero bloatware, instant startup (< 10ms), extremely low memory footprint (~5MB RAM), and zero web-view dependencies (pure native compilation).
- **UI features:** English + Spanish (in-app language switch persisted in the database) and light/dark theme (persisted as well). All documentation, code comments and identifier names are written in English.

---

## 2. Directory Layout (this repository)

```text
api-tester/
├── api-tester-backend/
│   ├── include/
│   │   └── backend.h            # shared structs + backend function declarations
│   └── src/
│       ├── backend.c            # SQLite persistence, HTTP engine (libcurl), worker thread
│       ├── cli.c                # command line client
│       └── test.c              # standalone test program
├── source-frontend/
│   └── main.c                   # C frontend: CUI app + calls into the backend
├── vendor/
│   ├── CUI/                     # CUI framework (submodule)
│   └── d2sqlite3/               # (legacy, unused in the C version)
├── tests/
│   ├── server/                  # vibe.d REST API for end-to-end tests (kept in D)
│   └── run_tests.sh
├── CMakeLists.txt               # pure C build
├── build_linux.sh / build_x11.sh / build_sdl2.sh / build_macos.sh / build_windows.bat
└── README.md
```

### Important notes about the build

### Important notes about the build

- The whole project is now pure C99: no D runtime, no FFI bridge. The backend
  lives in `api-tester-backend/src/backend.c` and is compiled directly into
  `cui_http` and `cui_http_cli`.
- `api-tester-backend/include/backend.h` is the single source of truth for the
  shared structs (`HttpResponse`, `HistoryRow`) and backend functions.
- All strings crossing internal boundaries are `malloc`'ed and freed with
  `free()` / `free_response()` / `free_history_row()`.
- The test server in `tests/server/` stays in D (vibe.d) as a test harness.

## 3. Build instructions

### Backend + frontend (normalized path — via CMake)

```sh
./build_linux.sh        # recommended on Linux (GLFW: X11 + Wayland)
./build_x11.sh          # X11 only
./build_sdl2.sh         # SDL2
./build_macos.sh        # macOS (Cocoa)
build_windows.bat       # Windows (Win32)
./build_linux/cui_http  # run (adjust path to the chosen build)
```

The standard workflow:
```sh
cmake -S . -B build_x11 -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=x11
cmake --build build_x11 -j 2
```

CMake steps performed automatically:
1. The CUI framework is built as a subdirectory (`add_subdirectory(vendor/CUI)`).
2. `cui_http` (C frontend + backend) is built and linked against CUI, sqlite3, curl, pthreads.
3. `cui_http_cli` (CLI) and `cui_http_test` (test program) are built.

## 4. Application features (current state)

- **Request view:** method + URL + Send bar (`cui_request_bar`), query param
  editor synced with the URL, request body editor, response status/latency/size
  line, JSON tree preview or raw code view, copy button.
- **Async engine:** requests never block the UI. The frontend calls
  `start_request()` and the D backend executes it on a worker thread owned by
  the D runtime; the frontend polls with `request_done()` /
  `last_response()`. The Send button doubles as Cancel.
- **History view:** every request is persisted to SQLite (`requests` table:
  created_at, method, url, req_body, status, latency_ms, res_size) and listed
  in a filterable table; a click loads it back into the request bar. History
  can be refreshed or cleared.
- **Settings:** `language` and `theme` are stored in the `settings` table and
  restored on startup; the language switch is in the top bar (English /
  Español) next to the light/dark toggle.
- **Command palette** (Ctrl+K) for Send / navigate to History / refresh /
  clear history, and Ctrl+Enter to send the current request.

## 5. Command line client (`cui_http_cli`)

A curl-like front-end that reuses the very same D backend, so requests made
from the shell land in the same SQLite history the GUI reads:

```sh
./build_x11/cui_http_cli GET https://api.example.com/v1/users
./build_x11/cui_http_cli POST https://api.example.com/v1/users -d '{"name":"Ada"}'
./build_x11/cui_http_cli -X PUT https://api.example.com/v1/users/1 -d '{"name":"Ada"}' -H "X-Api: 1"
./build_x11/cui_http_cli history 20
./build_x11/cui_http_cli clear
```

History database: `$CUI_HTTP_DB`, `--db PATH`, or the OS-conventional location
(`$XDG_DATA_HOME/cui-http/requests.db` / `~/.local/share/cui-http/requests.db`).

## 6. End to end tests

`tests/server/` contains a small vibe.d REST API (users CRUD, echo, status
and slow endpoints) and `tests/run_tests.sh` drives the CLI through every
verb, body/header round trips, status codes, latency and the history
database. All 23 checks must pass:

```sh
./tests/run_tests.sh
```

## 7. Notes for contributors

- Pure C99: no D runtime like in the previous versions, no FFI. The backend is compiled directly into the
  frontend and CLI.
- `backend.h` is the single source of truth for structs and functions.
- `extern` C functions take `const char *`; pass heap-allocated, NUL-terminated
  strings. Free results with `free()` / `free_response()` /
  `free_history_row()`.
- The test server in `tests/server/` stays in D (vibe.d) as a test harness.
