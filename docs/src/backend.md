# Backend (D)

Located in `api-tester-backend/source_lib/backend.d`, compiled by CMake into
`libapi_tester_backend.a`.

## Key exports

- `initDRuntime` / `terminateDRuntime` / `start_worker` / `backend_shutdown`
- `init_db(path)`, `default_db_path()`
- `start_request` / `start_request_ex` / `request_done` / `last_response` /
  `cancel_request` / `free_response`
- `history_refresh` / `history_count` / `history_row` / `clear_history` /
  `delete_history_row`
- `get_setting` / `set_setting` / `free_string`

## Persistence

SQLite tables:

- `requests(id, created_at, method, url, req_body, status, latency_ms,
  res_size, req_headers, res_headers)`
- `settings(key, value)`

The default DB location follows OS conventions (XDG / APPDATA / Application
Support) and can be overridden with `--db PATH` or `CUI_HTTP_DB`.

## Networking

libcurl via `std.net.curl`. Always use `auto client = HTTP();` (value
constructor) — `new HTTP()` skips `initialize()` and leaves the curl handle
null. Request/response headers are collected into text blocks.