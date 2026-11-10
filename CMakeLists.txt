cmake_minimum_required(VERSION 3.14)
project(cui_http C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 1. Encontrar los archivos fuente del backend
file(GLOB_RECURSE D_BACKEND_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/source_lib/*.d
)

file(GLOB_RECURSE D2SQLITE3_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/d2sqlite3/source/*.d
)

set(ALL_D_SOURCES ${D_BACKEND_SOURCES} ${D2SQLITE3_SOURCES})

# 2. Compilar la librería estática limpia
set(D_STATIC_LIB ${CMAKE_CURRENT_BINARY_DIR}/libapi_tester_backend.a)

add_custom_command(
    OUTPUT ${D_STATIC_LIB}
    COMMAND ldc2 -lib -of=${D_STATIC_LIB}
            -I${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/source_lib
            -I${CMAKE_CURRENT_SOURCE_DIR}/vendor/d2sqlite3/source
            ${ALL_D_SOURCES}
    DEPENDS ${ALL_D_SOURCES}
    COMMENT "Compilando backend en D de forma estática..."
)

add_custom_target(build_d_backend ALL DEPENDS ${D_STATIC_LIB})

# 4. Integrar CUI y el ejecutable en C
add_subdirectory(CUI)
add_executable(cui_http ./source-frontend/main.c)

add_dependencies(cui_http build_d_backend)

target_include_directories(cui_http PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/include
)

# 5. Enlazar la librería estática, Phobos, Druntime y dependencias del sistema
target_link_libraries(cui_http PRIVATE
    cui::cui
    ${D_STATIC_LIB}
    /usr/lib/libphobos2-ldc.a
    /usr/lib/libdruntime-ldc.a
    pthread
    sqlite3
    curl
)

# 6. D test program (app.d)
add_custom_target(run_d_test ALL
    COMMAND ldc2 -I${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/source_lib
                -I${CMAKE_CURRENT_SOURCE_DIR}/vendor/d2sqlite3/source
                ${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/source/app.d
                ${D_STATIC_LIB}
                /usr/lib/libphobos2-ldc.a
                /usr/lib/libdruntime-ldc.a
                -L-lsqlite3 -L-lcurl -L-lpthread -L-ldl -L-lm
                -of=${CMAKE_CURRENT_BINARY_DIR}/api_tester_d_test
    DEPENDS build_d_backend
    COMMENT "Compiling the D test program..."
)

# 7. Command line client (cli.d) sharing the same backend and history
add_custom_target(cui_http_cli ALL
    COMMAND ldc2 -I${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/source_lib
                -I${CMAKE_CURRENT_SOURCE_DIR}/vendor/d2sqlite3/source
                ${CMAKE_CURRENT_SOURCE_DIR}/api-tester-backend/source/cli.d
                ${D_STATIC_LIB}
                /usr/lib/libphobos2-ldc.a
                /usr/lib/libdruntime-ldc.a
                -L-lsqlite3 -L-lcurl -L-lpthread -L-ldl -L-lm
                -of=${CMAKE_CURRENT_BINARY_DIR}/cui_http_cli
    DEPENDS build_d_backend
    COMMENT "Compiling the command line client..."
)
