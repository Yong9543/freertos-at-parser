/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#include "test_harness.h"

#include "at_parser.h"

#include <stdlib.h>
#include <string.h>

typedef struct capture {
    char out[512];
    size_t len;
    int one_byte;
} capture_t;

static int cap_write(void *ctx, const uint8_t *data, size_t len)
{
    capture_t *cap = (capture_t *)ctx;
    size_t n = len;

    if (cap->one_byte && n > 1) {
        n = 1;
    }
    if (cap->len + n >= sizeof(cap->out)) {
        return -1;
    }
    memcpy(cap->out + cap->len, data, n);
    cap->len += n;
    cap->out[cap->len] = '\0';
    return (int)n;
}

static int on_add(at_server_t *server, const at_cmd_t *cmd, void *user)
{
    int a;
    int b;
    char *end = NULL;

    (void)user;
    if (cmd->type == AT_CMD_TEST) {
        return at_server_reply_line(server, "+ADD: (int),(int)");
    }
    if (cmd->type != AT_CMD_SET || cmd->argc != 2) {
        return AT_ERR_PARAM;
    }
    a = (int)strtol(cmd->argv[0], &end, 10);
    if (end == cmd->argv[0]) {
        return AT_ERR_PARAM;
    }
    b = (int)strtol(cmd->argv[1], &end, 10);
    if (end == cmd->argv[1]) {
        return AT_ERR_PARAM;
    }
    return at_server_reply_line(server, "+ADD: %d", a + b);
}

static int on_echo(at_server_t *server, const at_cmd_t *cmd, void *user)
{
    (void)user;
    if (cmd->type != AT_CMD_SET || cmd->argc != 1) {
        return AT_ERR_PARAM;
    }
    return at_server_reply_line(server, "+ECHO: %s", cmd->argv[0]);
}

static int on_cme(at_server_t *server, const at_cmd_t *cmd, void *user)
{
    (void)cmd;
    (void)user;
    if (at_server_reply_cme(server, 3) != AT_OK) {
        return AT_ERR_IO;
    }
    return AT_REPLIED;
}

static int on_gmr(at_server_t *server, const at_cmd_t *cmd, void *user)
{
    (void)user;
    if (cmd->type != AT_CMD_READ && cmd->type != AT_CMD_EXEC) {
        return AT_ERR_PARAM;
    }
    return at_server_reply_line(server, "+GMR: freertos-at-parser %s",
                                AT_PARSER_VERSION_STRING);
}

void test_server(void)
{
    char line_buf[128];
    at_cmd_entry_t table[8];
    at_server_t server;
    at_server_config_t cfg;
    capture_t cap;

    memset(&cap, 0, sizeof(cap));
    memset(&cfg, 0, sizeof(cfg));
    cfg.line_buf = line_buf;
    cfg.line_buf_size = sizeof(line_buf);
    cfg.write = cap_write;
    cfg.write_ctx = &cap;
    cfg.table = table;
    cfg.table_cap = 8;

    EXPECT(at_server_init(&server, &cfg) == AT_OK);
    EXPECT(at_server_register(&server, "ADD", on_add, NULL) == AT_OK);
    EXPECT(at_server_register(&server, "add", on_add, NULL) == AT_ERR_PARAM);
    EXPECT(at_server_register(&server, "ECHO", on_echo, NULL) == AT_OK);
    EXPECT(at_server_register(&server, "CME", on_cme, NULL) == AT_OK);
    EXPECT(at_server_register(&server, "GMR", on_gmr, NULL) == AT_OK);

    EXPECT(at_server_feed(&server, (const uint8_t *)"AT\r\n", 4) == AT_OK);
    EXPECT(strcmp(cap.out, "\r\nOK\r\n") == 0);

    cap.len = 0;
    cap.out[0] = '\0';
    cap.one_byte = 1;
    EXPECT(at_server_feed(&server, (const uint8_t *)"AT\r\n", 4) == AT_OK);
    EXPECT(strcmp(cap.out, "\r\nOK\r\n") == 0);
    cap.one_byte = 0;

    cap.len = 0;
    EXPECT(at_server_feed(&server, (const uint8_t *)"AT+ADD=2,40\r\n", 13) == AT_OK);
    EXPECT(strstr(cap.out, "+ADD: 42") != NULL);
    EXPECT(strstr(cap.out, "OK") != NULL);

    cap.len = 0;
    EXPECT(at_server_feed(&server, (const uint8_t *)"AT+ADD=?\r\n", 10) == AT_OK);
    EXPECT(strstr(cap.out, "+ADD: (int),(int)") != NULL);

    cap.len = 0;
    EXPECT(at_server_feed(&server, (const uint8_t *)"AT+ECHO=\"hi,there\"\r\n", 20) == AT_OK);
    EXPECT(strstr(cap.out, "+ECHO: hi,there") != NULL);

    cap.len = 0;
    EXPECT(at_server_feed(&server, (const uint8_t *)"AT+NOPE\r\n", 9) == AT_OK);
    EXPECT(strcmp(cap.out, "\r\nERROR\r\n") == 0);

    cap.len = 0;
    EXPECT(at_server_feed(&server, (const uint8_t *)"AT+CME\r\n", 8) == AT_OK);
    EXPECT(strstr(cap.out, "+CME ERROR: 3") != NULL);
    EXPECT(strstr(cap.out, "\r\nOK\r\n") == NULL);

    cap.len = 0;
    EXPECT(at_server_feed(&server, (const uint8_t *)"AT+GMR?\r\n", 9) == AT_OK);
    EXPECT(strstr(cap.out, "+GMR: freertos-at-parser " AT_PARSER_VERSION_STRING) != NULL);

    cap.len = 0;
    EXPECT(at_server_feed(&server, (const uint8_t *)"ATE1\r\n", 6) == AT_OK);
    EXPECT(strcmp(cap.out, "\r\nOK\r\n") == 0);
    cap.len = 0;
    EXPECT(at_server_feed(&server, (const uint8_t *)"AT+ADD=1,1\r\n", 12) == AT_OK);
    EXPECT(strstr(cap.out, "AT+ADD=1,1\r\n") == cap.out);
    EXPECT(strstr(cap.out, "+ADD: 2") != NULL);

    EXPECT(at_server_set_echo(&server, 0) == AT_OK);
    cap.len = 0;
    EXPECT(at_server_feed(&server, (const uint8_t *)"nope\r\n", 6) == AT_OK);
    EXPECT(strcmp(cap.out, "\r\nERROR\r\n") == 0);
}
