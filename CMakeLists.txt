cmake_minimum_required(VERSION 3.22)
project(Mango)

set(CMAKE_C_STANDARD 11)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/W4)
endif()

add_executable(server)
target_sources(
    server PUBLIC
    "src/server.c"
    "src/setup.h"
    "src/setup.c"
)

add_executable(client)
target_sources(
    client PUBLIC
    "src/client.c"
    "src/setup.h"
    "src/setup.c"
)
