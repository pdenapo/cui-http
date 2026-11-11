cmake_minimum_required(VERSION 3.14)
project(cui_http C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 1. Locate system libraries (portable)
find_package(Threads REQUIRED)
find_library(SQLITE3_LIBRARY NAMES sqlite3 REQUIRED)
find_library(CURL_LIBRARY     NAMES curl   REQUIRED)

message(STATUS "SQLITE3: ${SQLITE3_LIBRARY}")
message(STATUS "CURL:    ${CURL_LIBRARY}")

# 2. Backend C sources
set(BACKEND_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/src/backend.c
)

# Help CMake find dependencies installed by Homebrew (macOS) or other
# non-standard prefixes.
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH /opt/homebrew /usr/local)
endif()
if(DEFINED ENV{CMAKE_PREFIX_PATH})
    list(APPEND CMAKE_PREFIX_PATH $ENV{CMAKE_PREFIX_PATH})
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
    ${SQLITE3_LIBRARY}
    ${CURL_LIBRARY}
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
    ${SQLITE3_LIBRARY}
    ${CURL_LIBRARY}
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
    ${SQLITE3_LIBRARY}
    ${CURL_LIBRARY}
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