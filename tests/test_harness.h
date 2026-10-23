/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>

extern int g_failed;
extern int g_passed;

void test_fail(const char *file, int line, const char *expr);

#define EXPECT(cond)                                                         \
    do {                                                                     \
        if (cond) {                                                          \
            g_passed++;                                                      \
        } else {                                                             \
            test_fail(__FILE__, __LINE__, #cond);                            \
        }                                                                    \
    } while (0)

void test_line(void);
void test_client(void);
void test_server(void);
void test_ring(void);
void test_util(void);

#endif
