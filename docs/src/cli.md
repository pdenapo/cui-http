# Command Line Client

`cui_http_cli` reuses the same D backend, so requests land in the same history
the GUI reads.

```sh
cui_http_cli GET https://api.example.com/v1/users
cui_http_cli POST https://api.example.com/v1/users -d '{"name":"Ada"}'
cui_http_cli -X PUT https://api.example.com/v1/users/1 -d '{"name":"Ada"}' -H "X-Api: 1"
cui_http_cli history 20
cui_http_cli clear
```

Options: `-X/--method`, `-d/--data`, `-H/--header` (repeatable), `--db PATH`,
`--timeout SECS`. History DB: `$CUI_HTTP_DB`, `--db PATH`, or the
OS-conventional location.