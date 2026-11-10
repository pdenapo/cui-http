# Frontend (C)

The frontend is a single C99 file (`source-frontend/main.c`) built against the
CUI framework.

## Views

- **Request**: method + URL + Send bar, query params (synced with the URL),
  request body, request headers, response status/latency/size, response body
  (JSON tree or raw), response headers, copy button, Reset button.
- **History**: filterable table of past requests; click to reload, Resend,
  Delete single, Clear all.

## i18n

Six languages ship: English, Deutsch, Español, Français, Italiano, Português.
Selection persists in the `settings` table. To add a language, add an enum
value, a row in `dict[][]` and a menu item.

## Theme

Light/dark toggle persists in `settings` and is applied on the first frame
(requires `first_frame == true` on startup).