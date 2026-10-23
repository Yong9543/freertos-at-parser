# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Yong9543

cmake_minimum_required(VERSION 3.16)
project(freertos_at_parser VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(at_parser
    src/at_util.c
    src/at_line.c
    src/at_ring.c
    src/at_client.c
    src/at_server.c)
target_include_directories(at_parser PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_options(at_parser PRIVATE -Wall -Wextra -Wpedantic)

add_executable(at_tests
    tests/test_main.c
    tests/test_line.c
    tests/test_client.c
    tests/test_server.c
    tests/test_ring.c
    tests/test_util.c)
target_link_libraries(at_tests PRIVATE at_parser)
target_compile_options(at_tests PRIVATE -Wall -Wextra -Wpedantic)

add_executable(host_loopback examples/host_loopback/main.c)
target_link_libraries(host_loopback PRIVATE at_parser)
target_compile_options(host_loopback PRIVATE -Wall -Wextra -Wpedantic)

# The adapter includes FreeRTOS headers. Point FREERTOS_DIR at a kernel tree
# (the directory that contains FreeRTOS.h) and configure with -DAT_PARSER_FREERTOS=ON.
option(AT_PARSER_FREERTOS "Build the FreeRTOS adapter" OFF)
if(AT_PARSER_FREERTOS)
    add_library(at_parser_freertos src/at_freertos.c)
    target_link_libraries(at_parser_freertos PUBLIC at_parser)
    if(FREERTOS_DIR)
        target_include_directories(at_parser_freertos PUBLIC "${FREERTOS_DIR}")
    endif()
    target_compile_options(at_parser_freertos PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME at_tests COMMAND at_tests)
add_test(NAME host_loopback COMMAND host_loopback)

install(TARGETS at_parser ARCHIVE DESTINATION lib)
install(FILES include/at_parser.h DESTINATION include)
if(AT_PARSER_FREERTOS)
    install(TARGETS at_parser_freertos ARCHIVE DESTINATION lib)
    install(FILES include/at_freertos.h DESTINATION include)
endif()
