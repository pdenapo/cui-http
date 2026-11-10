# Project Specification: Native CUI HTTP Client (`cui-http`)

## 1. Overview & Goals
Build a blazing-fast, lightweight, native API client (Postman/Insomnia alternative) for Linux.
- **Frontend:** C99 using the [CUI](https://github.com/yappologistic/CUI) framework, specifically leveraging native components like `cui_request_bar`, `cui_api_explorer`, tables, tabs and the JSON viewer.
- **Backend & Logic:** D programming language compiled as a **static library**, handling networking via `std.net.curl` / `libcurl` and persistence via `d2sqlite3` (SQLite).
- **Core Philosophy:** Zero bloatware, instant startup (< 10ms), extremely low memory footprint (~5MB RAM), and zero web-view dependencies (pure native compilation).
- **UI features:** English + Spanish (in-app language switch persisted in the database) and light/dark theme (persisted as well). All documentation, code comments and identifier names are written in English.

---

## 2. Directory Layout (this repository)

```text
api-tester/
├── api-tester-backend/          # D backend
│   ├── include/
│   │   └── bridge.h             # C header: shared structs + exported D functions (the ABI contract)
│   ├── source_lib/
│   │   └── backend.d            # D backend: SQLite persistence, HTTP engine (libcurl), C ABI exports
│   ├── source/
│   │   └── app.d                # Standalone D test program (built by CMake, not dub)
│   ├── dub.json                 # dub manifest kept as a dev harness only — NOT used to build/link
│   └── dub.selections.json
├── source-frontend/
│   └── main.c                   # C frontend: CUI app + calls into the backend via bridge.h
├── CUI/                         # CUI framework (fork kept as a submodule / local copy)
├── vendor/
│   └── d2sqlite3/               # d2sqlite3 submodule, compiled together with the backend
├── CMakeLists.txt               # Root build: compiles the D static lib (ldc2 -lib) and links the C frontend
├── build_x11.sh                 # cmake -S . -B build_x11 -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=x11 && cmake --build build_x11 -j 2
├── build_glfw.sh                # same pattern with CUI_PLATFORM=glfw
├── build_sdl2.sh                # same pattern with CUI_PLATFORM=sdl2
├── clone_my_CUI.sh              # helper to (re)clone the CUI fork into ./CUI
├── requests.db                  # SQLite database created at runtime (git-ignored)
└── README.md
```

### Important notes about the build

- **Do NOT use dub to build or link the backend.** The backend static library is
  produced by CMake itself: it invokes `ldc2 -lib` directly over
  `api-tester-backend/source_lib/*.d` **plus** the d2sqlite3 sources from
  `vendor/d2sqlite3/source`, and the resulting `libapi_tester_backend.a` is
  linked into the C executable together with the LDC runtime
  (`libphobos2-ldc.a`, `libdruntime-ldc.a`), `sqlite3`, `curl`, `pthread`.
- `dub.json` exists only as a convenience to fetch/keep D dependencies as
  packages; the authoritative build is CMake.
- `api-tester-backend/include/bridge.h` is the single source of truth for the
  C↔D ABI: structs exported from D (`HttpResponse`, `HistoryRow`) must keep
  field order and types in sync with it.
- All strings crossing the bridge are `malloc`-ed by the backend and must be
  freed from C with `free_response()` / `free_string()`.

## 3. Build instructions

### Backend + frontend (normalized path — via CMake)

```sh
./build_x11.sh          # X11 build (see build_glfw.sh / build_sdl2.sh for the others)
./build_x11/api_tester  # run
```

The standard workflow:
```sh
cmake -S . -B build_x11 -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=x11
cmake --build build_x11 -j 2
```

CMake steps performed automatically:
1. `ldc2 -lib` compiles the D backend (backend.d + d2sqlite3) into
   `build_x11/libapi_tester_backend.a`.
2. The CUI framework is built as a subdirectory (`add_subdirectory(CUI)`).
3. `api_tester` (C frontend) is built and linked against CUI + the backend.
4. A standalone D test program (`api_tester_d_test`) is built from
   `api-tester-backend/source/app.d` linked with the same static library.

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

History database: `$CUI_HTTP_DB` or `./requests.db`.

## 6. End to end tests

`tests/server/` contains a small vibe.d REST API (users CRUD, echo, status
and slow endpoints) and `tests/run_tests.sh` drives the CLI through every
verb, body/header round trips, status codes, latency and the history
database. All 23 checks must pass:

```sh
./tests/run_tests.sh
```

## 7. Notes for contributors

- The D `HTTP` type is a struct whose value constructor runs `initialize()`.
  Always write `auto client = HTTP();` (never `new HTTP()`), otherwise the
  curl handle stays null and every `set`/`perform` fails with
  "A libcurl function was given a bad argument on handle null".
- `extern (C)` functions take `const char*`; D `string` does not decay to a
  pointer, so pass `cast(const(char)*) toStringz(s)`.
- The backend never frees response bodies handed to C: `last_response()`
  returns a fresh `malloc`'ed copy that the caller owns and frees with
  `free_response()`.
