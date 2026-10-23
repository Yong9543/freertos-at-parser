/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#include "test_harness.h"

#include "at_parser.h"

#include <string.h>

/* True when got is the non-empty label the public helper should return. */
static int label_is(const char *got, const char *want)
{
    return got != NULL && got[0] != '\0' && strcmp(got, want) == 0;
}

void test_util(void)
{
    EXPECT(label_is(at_status_str(AT_OK), "ok"));
    EXPECT(label_is(at_status_str(AT_REPLIED), "replied"));
    EXPECT(label_is(at_status_str(AT_ERR_PARAM), "bad parameter"));
    EXPECT(label_is(at_status_str(AT_ERR_SYNTAX), "syntax error"));
    EXPECT(label_is(at_status_str(AT_ERR_OVERFLOW), "overflow"));
    EXPECT(label_is(at_status_str(AT_ERR_NOMEM), "no memory"));
    EXPECT(label_is(at_status_str(AT_ERR_BUSY), "busy"));
    EXPECT(label_is(at_status_str(AT_ERR_IO), "io error"));
    EXPECT(label_is(at_status_str(AT_ERR_TIMEOUT), "timeout"));
    EXPECT(label_is(at_status_str(AT_ERR_STATE), "bad state"));
    EXPECT(label_is(at_status_str(2), "unknown"));
    EXPECT(label_is(at_status_str(-9), "unknown"));

    EXPECT(label_is(at_resp_status_str(AT_RESP_OK), "OK"));
    EXPECT(label_is(at_resp_status_str(AT_RESP_ERROR), "ERROR"));
    EXPECT(label_is(at_resp_status_str(AT_RESP_TIMEOUT), "TIMEOUT"));
    EXPECT(label_is(at_resp_status_str(AT_RESP_OVERFLOW), "OVERFLOW"));
    EXPECT(label_is(at_resp_status_str(AT_RESP_FAIL), "FAIL"));
    EXPECT(label_is(at_resp_status_str(AT_RESP_BUSY), "BUSY"));
    EXPECT(label_is(at_resp_status_str(AT_RESP_PARAM), "PARAM"));
    EXPECT(label_is(at_resp_status_str(AT_RESP_IO), "IO"));
    EXPECT(label_is(at_resp_status_str(AT_RESP_PENDING), "PENDING"));
    EXPECT(label_is(at_resp_status_str(AT_RESP_IDLE), "IDLE"));
    EXPECT(label_is(at_resp_status_str(3), "UNKNOWN"));
    EXPECT(label_is(at_resp_status_str(-8), "UNKNOWN"));
}
