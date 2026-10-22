/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#ifndef AT_INTERNAL_H
#define AT_INTERNAL_H

#include "at_parser.h"

#if defined(__GNUC__)
#define AT_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define AT_BARRIER() ((void)0)
#endif

int at_str_eq_ci(const char *a, const char *b);
int at_starts_with_ci(const char *text, const char *prefix);
int at_write_all(at_write_fn write, void *ctx, const uint8_t *data, size_t len);

#endif /* AT_INTERNAL_H */
