# Backend (C)

Located in `api-tester-backend/src/backend.c`, compiled directly into
`cui_http` and `cui_http_cli`.

## Key functions

- `backend_init(path)`, `backend_shutdown()`, `default_db_path()`
- `start_request(method, url, body, headers, timeout_ms)`
- `request_done()`, `last_response()`, `cancel_request()`, `free_response()`
- `history_refresh()`, `history_count()`, `history_row()`, `clear_history()`,
  `delete_history_row()`, `free_history_row()`
- `get_setting(key)`, `set_setting(key, value)`

## Persistence

SQLite tables:

- `requests(id, created_at, method, url, req_body, status, latency_ms,
  res_size, req_headers, res_headers)`
- `settings(key, value)`

The default DB location follows OS conventions (XDG / APPDATA / Application
Support) and can be overridden with `--db PATH` or `CUI_HTTP_DB`.

## Networking

libcurl. The worker thread runs one request at a time and never blocks the UI.
Request/response headers are collected into text blocks.