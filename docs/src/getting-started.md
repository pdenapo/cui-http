# Getting Started

## Prerequisites

### Linux (Arch)
```sh
sudo pacman -S base-devel cmake ninja ldc sqlite curl libx11 libglvnd
```
### Linux (Debian / Ubuntu)
```sh
sudo apt-get install -y build-essential cmake ninja-build ldc libsqlite3-dev libcurl4-openssl-dev libx11-dev libgl-dev
```
### Windows
- LDC, CMake + Ninja, SQLite and libcurl dev libraries, Visual Studio Build Tools.
### macOS
```sh
brew install cmake ninja ldc sqlite curl
```

## Build

```sh
# Linux (recommended: GLFW works on X11 and Wayland)
./build_linux.sh
./build_linux/cui_http

# macOS
./build_macos.sh

# Windows (cmd)
build_windows.bat
```

See [Build](build.md) for all frontends and manual CMake invocations.