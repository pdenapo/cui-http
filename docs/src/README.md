# cui-http

`cui-http` is a blazing-fast, lightweight, native API client (Postman/Insomnia
alternative) for Linux, Windows and macOS. The frontend is C99 on top of the
[CUI](https://github.com/yappologistic/CUI) framework; the backend is D,
compiled into a static library, handling HTTP via libcurl and persistence via
SQLite. No web views, instant startup, tiny memory footprint.

## Highlights

- Request view: method + URL + Send bar, query-parameter editor synced with the
  URL, request body editor, request/response headers editors and viewers,
  response status/latency/size, JSON tree preview or raw text, copy button.
- Async engine: requests never block the UI; the Send button doubles as Cancel.
- History view: every request persisted in SQLite; click a row to reload it.
- Settings: multiple languages and light/dark theme, both persisted and
  restored on startup.
- Command palette (Ctrl+K) and Ctrl+Enter to send.
- Command line client (`cui_http_cli`) sharing the same backend and history.

## Links

- [Source code](https://github.com/yappologistic/cui-http)
- [Releases](https://github.com/yappologistic/cui-http/releases)
- [Build guide](build.md)