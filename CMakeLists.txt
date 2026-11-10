cmake_minimum_required(VERSION 3.14)
project(cui_http C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 1. Locate backend (D) and system libraries portably (no hardcoded /usr/lib paths)
find_package(Threads REQUIRED)

find_library(PHOBOS2_LIBRARY  NAMES phobos2-ldc phobos2  REQUIRED)
find_library(DRUNTIME_LIBRARY NAMES druntime-ldc druntime REQUIRED)
find_library(SQLITE3_LIBRARY  NAMES sqlite3            REQUIRED)
find_library(CURL_LIBRARY     NAMES curl               REQUIRED)

message(STATUS "PHOBOS2:   ${PHOBOS2_LIBRARY}")
message(STATUS "DRUNTIME:  ${DRUNTIME_LIBRARY}")
message(STATUS "SQLITE3:   ${SQLITE3_LIBRARY}")
message(STATUS "CURL:      ${CURL_LIBRARY}")

# 2. Backend D sources
file(GLOB_RECURSE D_BACKEND_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/source_lib/*.d
)
file(GLOB_RECURSE D2SQLITE3_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/d2sqlite3/source/*.d
)
set(ALL_D_SOURCES ${D_BACKEND_SOURCES} ${D2SQLITE3_SOURCES})

# 3. Static D library
set(D_STATIC_LIB ${CMAKE_CURRENT_BINARY_DIR}/libapi_tester_backend.a)

add_custom_command(
    OUTPUT ${D_STATIC_LIB}
    COMMAND ldc2 -lib -of=${D_STATIC_LIB}
            -I${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/source_lib
            -I${CMAKE_CURRENT_SOURCE_DIR}/vendor/d2sqlite3/source
            ${ALL_D_SOURCES}
    DEPENDS ${ALL_D_SOURCES}
    COMMENT "Compiling D backend into a static library..."
)
add_custom_target(build_d_backend ALL DEPENDS ${D_STATIC_LIB})

# 4. CUI framework + C frontend
add_subdirectory(vendor/CUI)
add_executable(cui_http ./source-frontend/main.c)
add_dependencies(cui_http build_d_backend)

target_include_directories(cui_http PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/include
)

target_link_libraries(cui_http PRIVATE
    cui::cui
    ${D_STATIC_LIB}
    ${PHOBOS2_LIBRARY}
    ${DRUNTIME_LIBRARY}
    Threads::Threads
    ${SQLITE3_LIBRARY}
    ${CURL_LIBRARY}
)

# 5. D test program
add_custom_target(run_d_test ALL
    COMMAND ldc2 -I${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/source_lib
                -I${CMAKE_CURRENT_SOURCE_DIR}/vendor/d2sqlite3/source
                ${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/source/app.d
                ${D_STATIC_LIB}
                ${PHOBOS2_LIBRARY}
                ${DRUNTIME_LIBRARY}
                -L${SQLITE3_LIBRARY}
                -L${CURL_LIBRARY}
                -L-lpthread -L-ldl -L-lm
                -of=${CMAKE_CURRENT_BINARY_DIR}/api_tester_d_test
    DEPENDS build_d_backend
    COMMENT "Compiling the D test program..."
)

# 6. Command line client
add_custom_target(cui_http_cli ALL
    COMMAND ldc2 -I${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/source_lib
                -I${CMAKE_CURRENT_SOURCE_DIR}/vendor/d2sqlite3/source
                ${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/source/cli.d
                ${D_STATIC_LIB}
                ${PHOBOS2_LIBRARY}
                ${DRUNTIME_LIBRARY}
                -L${SQLITE3_LIBRARY}
                -L${CURL_LIBRARY}
                -L-lpthread -L-ldl -L-lm
                -of=${CMAKE_CURRENT_BINARY_DIR}/cui_http_cli
    DEPENDS build_d_backend
    COMMENT "Compiling the command line client..."
)

# 7. Install rules (used by distro packaging and CPack)
install(TARGETS cui_http RUNTIME DESTINATION bin)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/cui_http_cli DESTINATION bin)