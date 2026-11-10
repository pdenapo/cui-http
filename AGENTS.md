# Project Specification: Native CUI HTTP Client (`cui-http`)

## 1. Overview & Goals
Build a blazing-fast, lightweight, native API client (Postman/Insomnia alternative) for Linux. 
- **Frontend:** C99 using the [CUI](https://github.com/yappologistic/CUI) framework, specifically leveraging native components like `http-request-bar` and `api-explorer`.
- **Backend & Logic:** D programming language compiled as a static library, handling networking via `std.net.curl` / `libcurl` and persistence via `d2sqlite3`.
- **Core Philosophy:** Zero bloatware, instant startup (< 10ms), extremely low memory footprint (~5MB RAM), and zero web-view dependencies (pure native compilation).

---

## 2. Directory Structure
```text
api-tester-backed/
|__ api-tester-backend     # D backend
    |__ include/
    │           └── bridge.h   # C header defining shared structs and exported D functions
    ├── source_lib/
│               |__ backend.d      # D backend: SQLite persistence, HTTP requests, C ABI exports
├── CMakeLists.txt         # Main CMake build integrating C UI and D static library
│   └── main.c             # C frontend entrypoint (CUI initialization + runtime linkage)
└── README.md


# Build instructions 

## Backend 

Run 

dub build

from the directory api-tester-backend

./build_x11.sh

Build X11 version.
