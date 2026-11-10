# Troubleshooting

- **"A libcurl function was given a bad argument on handle null"**: the D
  `HTTP` type is a struct; always write `auto client = HTTP();` (never
  `new HTTP()`), otherwise `initialize()` is skipped.
- **`extern (C)` argument errors**: D `string` does not decay to `const char*`;
  pass `cast(const(char)*) toStringz(s)`.
- **History not loading on startup**: ensure `hist_stale` defaults to `true`.
- **Theme not persisting**: ensure `first_frame` defaults to `true`.
- **Submodules empty in a tarball**: regenerate with `scripts/make_tarball.sh`.
- **Wayland**: prefer the `glfw` frontend (native Wayland); `x11` works via
  XWayland.