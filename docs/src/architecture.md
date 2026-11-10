# Architecture

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
├── tests/                        # vibe.d test server + run_tests.sh
└── CMakeLists.txt
```

## Backend (D)

- D runtime lifecycle (`initDRuntime` / `terminateDRuntime`).
- SQLite persistence: `requests` (history) and `settings` tables.
- HTTP engine on a worker thread: `start_request` / `request_done` /
  `last_response` / `cancel_request`. The Send button doubles as Cancel.
- C ABI bridge via `bridge.h`. All strings crossing the boundary are
  `malloc`'ed by D and freed from C.

## Frontend (C)

- CUI components: `cui_request_bar`, params/headers editors, JSON viewer,
  tables, tabs, command palette.
- Async polling: `start_request()` then poll `request_done()` /
  `last_response()`.
- Settings (language, theme) persisted via the backend.

## Data flow

1. User fills the request bar and presses Send.
2. Frontend serializes headers and calls `start_request_ex(...)`.
3. Backend worker runs libcurl, stores the result, writes a history row.
4. Frontend polls `request_done()` and fetches `last_response()` (a fresh
   malloc'ed copy).