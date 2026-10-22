/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#include "at_internal.h"

#include <string.h>

static char at_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

int at_str_eq_ci(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    while (*a != '\0' && *b != '\0') {
        if (at_lower(*a) != at_lower(*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int at_starts_with_ci(const char *text, const char *prefix)
{
    size_t i;

    if (text == NULL || prefix == NULL) {
        return 0;
    }
    for (i = 0; prefix[i] != '\0'; i++) {
        if (text[i] == '\0' || at_lower(text[i]) != at_lower(prefix[i])) {
            return 0;
        }
    }
    return 1;
}

int at_write_all(at_write_fn write, void *ctx, const uint8_t *data, size_t len)
{
    size_t off = 0;

    if (write == NULL) {
        return AT_ERR_IO;
    }
    while (off < len) {
        int n = write(ctx, data + off, len - off);
        if (n < 0) {
            return AT_ERR_IO;
        }
        if (n == 0) {
            return AT_ERR_IO;
        }
        off += (size_t)n;
    }
    return AT_OK;
}

const char *at_status_str(int status)
{
    switch (status) {
    case AT_OK:
        return "ok";
    case AT_REPLIED:
        return "replied";
    case AT_ERR_PARAM:
        return "bad parameter";
    case AT_ERR_SYNTAX:
        return "syntax error";
    case AT_ERR_OVERFLOW:
        return "overflow";
    case AT_ERR_NOMEM:
        return "no memory";
    case AT_ERR_BUSY:
        return "busy";
    case AT_ERR_IO:
        return "io error";
    case AT_ERR_TIMEOUT:
        return "timeout";
    case AT_ERR_STATE:
        return "bad state";
    default:
        return "unknown";
    }
}

const char *at_resp_status_str(int status)
{
    switch (status) {
    case AT_RESP_OK:
        return "OK";
    case AT_RESP_ERROR:
        return "ERROR";
    case AT_RESP_TIMEOUT:
        return "TIMEOUT";
    case AT_RESP_OVERFLOW:
        return "OVERFLOW";
    case AT_RESP_FAIL:
        return "FAIL";
    case AT_RESP_BUSY:
        return "BUSY";
    case AT_RESP_PARAM:
        return "PARAM";
    case AT_RESP_IO:
        return "IO";
    case AT_RESP_PENDING:
        return "PENDING";
    case AT_RESP_IDLE:
        return "IDLE";
    default:
        return "UNKNOWN";
    }
}
