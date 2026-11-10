# Release Guide — cui-http

This document describes how to build `cui-http` for the supported platforms and how
to publish a source release on GitHub (including the AUR and Debian packaging).

## 0. Prerequisites

- Linux build machine (the project is developed and tested on Linux).
- Toolchain:
  - `cmake` (>= 3.14), `ninja` (optional but recommended)
  - `ldc2` (LDC D compiler)
  - `libsqlite3-dev`, `libcurl4-openssl-dev`
  - `git`
- For releases: `gh` (GitHub CLI), authenticated (`gh auth login`).
- For AUR: an AUR account + SSH key, `makepkg`, `namcap`.
- For Debian: `debhelper`, `dpkg-dev`.

Submodules must be initialized:

```sh
git submodule update --init --recursive
```

## 1. Local build

Never use `dub` to build or link the backend — CMake drives `ldc2 -lib` directly.

```sh
# X11 (default on Linux)
./build_x11.sh
./build_x11/cui_http            # GUI
./build_x11/cui_http_cli help   # CLI

# Other CUI backends
./build_glfw.sh
./build_sdl2.sh
```

Manual equivalent:

```sh
cmake -S . -B build_x11 -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=x11
cmake --build build_x11 -j 2
```

CMake produces three binaries:

- `cui_http` — GUI
- `cui_http_cli` — command line client
- `api_tester_d_test` — standalone D test program

## 2. Tests

```sh
./tests/run_tests.sh   # 25 checks, all must pass
```

The suite spins up a local vibe.d REST API (`tests/server/`) and drives the CLI
through every HTTP verb, headers/body round trips, status codes, latency,
history persistence (cross-process), `--db`, single-row delete and clear.

## 3. Multi-platform builds

The project supports Linux (x11/glfw/sdl2), Windows (win32/glfw) and macOS
(cocoa/glfw). Cross-compilation is driven by GitHub Actions (see
`.github/workflows/build.yml`); locally you can build for any platform
supported by your host:

```sh
for p in x11 glfw sdl2; do
    cmake -S . -B build_$p -DCMAKE_BUILD_TYPE=Release -DCUI_PLATFORM=$p
    cmake --build build_$p -j 2
done
```

On Windows use `-DCUI_PLATFORM=win32` (or glfw/sdl2) with the MSVC/MinGW
toolchain; on macOS use `cocoa` (or glfw/sdl2).

## 4. Build a complete source tarball

GitHub's auto-generated archive (`/archive/vX.Y.Z.tar.gz`) does **not** include
submodule contents, so use the provided script:

```sh
./scripts/make_tarball.sh 0.1.0
# -> /tmp/cui-http-0.1.0.tar.gz  (submodules included)
# prints the SHA256 to paste into packaging/arch/PKGBUILD
```

## 5. Publish a GitHub release

```sh
# 1. authenticate once
gh auth login

# 2. tag + push + tarball + release (one command)
./scripts/release.sh 0.1.0

# or manually:
git tag -a v0.1.0 -m "cui-http 0.1.0"
git push origin v0.1.0
./scripts/make_tarball.sh 0.1.0
gh release create v0.1.0 \
    --title "cui-http 0.1.0" \
    --notes "Native API client + CLI." \
    /tmp/cui-http-0.1.0.tar.gz
```

The release page will also offer GitHub's own (submodule-less) archive; the
attached tarball is the one PKGBUILDs should use.

## 6. Publish to the AUR (Arch)

```sh
# 1. create an AUR account and upload your SSH key
# 2. clone the (empty) AUR repo for your package
git clone ssh://aur@aur.archlinux.org/cui-http.git aur-cui-http
cd aur-cui-http

# 3. copy the PKGBUILD and update sha256sums with the value from make_tarball.sh
cp /home/luis/proyectos/api-tester/packaging/arch/PKGBUILD .
# edit sha256sums=('...')

# 4. generate .SRCINFO (mandatory)
makepkg --printsrcinfo > .SRCINFO

# 5. verify locally
makepkg -si
namcap PKGBUILD
namcap *.pkg.tar.zst

# 6. publish
git add PKGBUILD .SRCINFO
git commit -m "Initial import: cui-http 0.1.0"
git push
```

## 7. Build a Debian package

```sh
cd /home/luis/proyectos/api-tester
cp -r packaging/debian .
dpkg-buildpackage -us -uc -b
# produces ../cui-http_0.1.0-1_amd64.deb
```

## 8. (Optional) Automated releases via GitHub Actions

The workflow in `.github/workflows/build.yml` builds all platform combos on every
push/PR and on `workflow_dispatch`. To make it publish a release automatically
on tags, add a job:

```yaml
release:
  if: startsWith(github.ref, 'refs/tags/v')
  needs: build
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
    - name: Download all artifacts
      uses: actions/download-artifact@v4
    - name: Create release
      run: gh release create ${{ github.ref_name }} --generate-notes cui-http-*/cui_http cui-http-*/cui_http_cli
      env:
        GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

## 9. Troubleshooting

- **"A libcurl function was given a bad argument on handle null"**: the D
  `HTTP` type is a struct; always write `auto client = HTTP();` (never
  `new HTTP()`), otherwise `initialize()` is skipped and the curl handle is null.
- **`extern (C)` argument errors**: D `string` does not decay to `const char*`;
  pass `cast(const(char)*) toStringz(s)`.
- **History not loading on startup**: ensure `hist_stale` is initialized to
  `true` so the History view triggers `reload_history()`.
- **Theme not persisting**: ensure `first_frame` is initialized to `true` so
  `cui_set_theme()` runs on the first frame with the saved value.
- **Submodules empty in a tarball**: regenerate with `scripts/make_tarball.sh`,
  which materializes `vendor/CUI` and `vendor/d2sqlite3`.

## Conventions

- Default DB location: `$XDG_DATA_HOME/cui-http/requests.db` (Linux),
  `%APPDATA%/cui-http/requests.db` (Windows), `~/Library/Application
  Support/cui-http/requests.db` (macOS), overridable with `--db PATH` or
  `CUI_HTTP_DB`.
- All user-facing strings and identifiers are in English; the UI ships with
  English, Spanish, Italian, French, German and Portuguese.