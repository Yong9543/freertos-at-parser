/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#include "test_harness.h"

#include <stdio.h>

int g_failed = 0;
int g_passed = 0;

void test_fail(const char *file, int line, const char *expr)
{
    g_failed++;
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
}

int main(void)
{
    test_line();
    test_ring();
    test_client();
    test_server();

    if (g_failed != 0) {
        fprintf(stderr, "%d failed, %d passed\n", g_failed, g_passed);
        return 1;
    }
    printf("%d checks passed\n", g_passed);
    return 0;
}
