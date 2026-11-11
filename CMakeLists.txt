cmake_minimum_required(VERSION 3.14)
project(cui_http C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# 1. Locate system libraries (portable)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)

message(STATUS "SQLITE3: ${SQLite3_LIBRARIES}")
message(STATUS "CURL:    ${CURL_LIBRARIES}")

# 2. Backend C sources
set(BACKEND_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/src/backend.c
)

# Help CMake find dependencies installed by Homebrew (macOS) or other
# non-standard prefixes.
# Help CMake find dependencies installed by Homebrew/MacPorts (macOS) or other
# non-standard prefixes.
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH
        /opt/homebrew /opt/homebrew/opt/glfw
        /usr/local /usr/local/opt/glfw
        /opt/local)
endif()
if(DEFINED ENV{CMAKE_PREFIX_PATH})
    list(APPEND CMAKE_PREFIX_PATH $ENV{CMAKE_PREFIX_PATH})
endif()

# Default CUI platform per OS (so users don't have to pass -DCUI_PLATFORM=...)
if(NOT DEFINED CUI_PLATFORM OR CUI_PLATFORM STREQUAL "" OR CUI_PLATFORM STREQUAL "auto")
    if(APPLE)
        set(CUI_PLATFORM "cocoa" CACHE STRING "Window backend" FORCE)
    elseif(WIN32)
        set(CUI_PLATFORM "win32" CACHE STRING "Window backend" FORCE)
    else()
        set(CUI_PLATFORM "glfw" CACHE STRING "Window backend" FORCE)
    endif()
endif()
message(STATUS "CUI_PLATFORM: ${CUI_PLATFORM}")

# Ensure the requested platform's dependencies are available, with a clear
# message (otherwise CMake fails deep inside vendor/CUI).
if(CUI_PLATFORM STREQUAL "glfw")
    find_package(glfw3 QUIET)
    if(NOT glfw3_FOUND)
        message(FATAL_ERROR
            "glfw3 not found. Install it:\n"
            "  macOS:  brew install glfw\n"
            "  Linux:  sudo apt-get install libglfw3-dev   (or pacman -S glfw)\n"
            "Or use a different CUI_PLATFORM (x11, sdl2, cocoa, win32).")
    endif()
endif()

# 3. CUI framework
add_subdirectory(vendor/CUI)

# 4. GUI executable
add_executable(cui_http
    source-frontend/main.c
    ${BACKEND_SOURCES}
)
target_include_directories(cui_http PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/include
)
target_link_libraries(cui_http PRIVATE
    cui::cui
    Threads::Threads
    SQLite3::SQLite3
    CURL::libcurl
)

# 5. CLI
add_executable(cui_http_cli
    api-tester-backend/src/cli.c
    ${BACKEND_SOURCES}
)
target_include_directories(cui_http_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/include
)
target_link_libraries(cui_http_cli PRIVATE
    Threads::Threads
    SQLite3::SQLite3
    CURL::libcurl
    ${CMAKE_DL_LIBS}
    m
)

# 6. Test program
add_executable(cui_http_test
    api-tester-backend/src/test.c
    ${BACKEND_SOURCES}
)
target_include_directories(cui_http_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/include
)
target_link_libraries(cui_http_test PRIVATE
    Threads::Threads
    SQLite3::SQLite3
    CURL::libcurl
    ${CMAKE_DL_LIBS}
    m
)

# 7. Install
install(TARGETS cui_http cui_http_cli RUNTIME DESTINATION bin)

# 8. Tests (ctest)
enable_testing()
add_test(NAME backend_unit COMMAND cui_http_test)
if(UNIX)
    add_test(NAME e2e_cli COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.sh)
endif()