# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Yong9543

# Make's built-in CC is "cc". Prefer GCC when the user did not choose one,
# so the default sanitizer flags link against libasan.
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude
SAN ?= -fsanitize=address,undefined -fno-omit-frame-pointer

LIB_SRC := src/at_util.c src/at_line.c src/at_ring.c src/at_client.c src/at_server.c
TEST_SRC := tests/test_main.c tests/test_line.c tests/test_client.c tests/test_server.c tests/test_ring.c tests/test_util.c
FREERTOS_SRC := src/at_freertos.c tests/stubs/stub_kernel.c examples/freertos_uart/main.c tests/test_freertos_build.c

.PHONY: all check clean

all: build/at_tests build/host_loopback build/freertos_stub

build:
	mkdir -p build

build/at_tests: $(LIB_SRC) $(TEST_SRC) include/at_parser.h | build
	$(CC) $(CFLAGS) $(SAN) -o $@ $(LIB_SRC) $(TEST_SRC)

build/host_loopback: $(LIB_SRC) examples/host_loopback/main.c include/at_parser.h | build
	$(CC) $(CFLAGS) $(SAN) -o $@ $(LIB_SRC) examples/host_loopback/main.c

build/freertos_stub: $(LIB_SRC) $(FREERTOS_SRC) include/at_freertos.h include/at_parser.h | build
	$(CC) $(CFLAGS) $(SAN) -Itests/stubs -Iexamples/freertos_uart -o $@ $(LIB_SRC) $(FREERTOS_SRC)

check: all
	./build/at_tests
	./build/host_loopback
	./build/freertos_stub

clean:
	rm -rf build
