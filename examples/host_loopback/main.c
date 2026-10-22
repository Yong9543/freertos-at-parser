/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 *
 * Host loopback: a command server and a client share two write hooks.
 * No FreeRTOS required. Build with `make` and run `build/host_loopback`.
 */
#include "at_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct demo {
    at_server_t server;
    at_client_t client;
    int failures;
    int boots;
} demo_t;

static demo_t g;

static int server_to_client(void *ctx, const uint8_t *data, size_t len)
{
    demo_t *demo = (demo_t *)ctx;
    if (at_client_feed(&demo->client, data, len) != AT_OK) {
        return -1;
    }
    return (int)len;
}

static int client_to_server(void *ctx, const uint8_t *data, size_t len)
{
    demo_t *demo = (demo_t *)ctx;
    if (at_server_feed(&demo->server, data, len) != AT_OK) {
        return -1;
    }
    return (int)len;
}

static int on_add(at_server_t *server, const at_cmd_t *cmd, void *user)
{
    char *end = NULL;
    long a;
    long b;

    (void)user;
    if (cmd->type == AT_CMD_TEST) {
        return at_server_reply_line(server, "+ADD: (int),(int)");
    }
    if (cmd->type != AT_CMD_SET || cmd->argc != 2) {
        return AT_ERR_PARAM;
    }
    a = strtol(cmd->argv[0], &end, 10);
    if (end == cmd->argv[0]) {
        return AT_ERR_PARAM;
    }
    b = strtol(cmd->argv[1], &end, 10);
    if (end == cmd->argv[1]) {
        return AT_ERR_PARAM;
    }
    return at_server_reply_line(server, "+ADD: %ld", a + b);
}

static int on_echo(at_server_t *server, const at_cmd_t *cmd, void *user)
{
    (void)user;
    if (cmd->type != AT_CMD_SET || cmd->argc < 1) {
        return AT_ERR_PARAM;
    }
    return at_server_reply_line(server, "+ECHO: %s", cmd->argv[0]);
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

static void on_boot(void *user, const char *line, size_t len)
{
    demo_t *demo = (demo_t *)user;
    (void)len;
    demo->boots++;
    printf("urc %s\n", line);
}

static void print_resp(const at_response_t *resp)
{
    size_t i;

    for (i = 0; i < resp->line_count; i++) {
        printf("<< %s\n", at_resp_line(resp, i));
    }
}

static void exchange(const char *cmd, at_resp_status_t want, const char *needle)
{
    char storage[256];
    at_response_t resp;
    at_resp_status_t st;
    const char *found;

    at_resp_init(&resp, storage, sizeof(storage), 0);
    printf(">> %s\n", cmd);
    st = at_client_exec(&g.client, &resp, 1000, "%s", cmd);
    print_resp(&resp);
    if (st != want) {
        fprintf(stderr, "expected %s, got %s\n", at_resp_status_str(want),
                at_resp_status_str(st));
        g.failures++;
        return;
    }
    if (needle != NULL) {
        found = at_resp_find(&resp, needle);
        if (found == NULL) {
            fprintf(stderr, "missing '%s' in the response\n", needle);
            g.failures++;
        }
    }
}

int main(void)
{
    char server_line[256];
    char client_line[256];
    at_cmd_entry_t table[8];
    at_server_config_t server_cfg;
    at_client_config_t client_cfg;
    at_io_ops_t io;
    at_urc_t urcs[1];
    const char boot[] = "\r\n+BOOT: ready\r\n";

    memset(&g, 0, sizeof(g));
    memset(&server_cfg, 0, sizeof(server_cfg));
    memset(&client_cfg, 0, sizeof(client_cfg));
    memset(&table, 0, sizeof(table));

    server_cfg.line_buf = server_line;
    server_cfg.line_buf_size = sizeof(server_line);
    server_cfg.write = server_to_client;
    server_cfg.write_ctx = &g;
    server_cfg.table = table;
    server_cfg.table_cap = 8;
    if (at_server_init(&g.server, &server_cfg) != AT_OK) {
        fprintf(stderr, "server init failed\n");
        return 1;
    }
    if (at_server_register(&g.server, "ADD", on_add, NULL) != AT_OK ||
        at_server_register(&g.server, "ECHO", on_echo, NULL) != AT_OK ||
        at_server_register(&g.server, "GMR", on_gmr, NULL) != AT_OK) {
        fprintf(stderr, "register failed\n");
        return 1;
    }

    urcs[0].prefix = "+BOOT";
    urcs[0].suffix = NULL;
    urcs[0].handler = on_boot;
    urcs[0].user = &g;

    io.write = client_to_server;
    io.ctx = &g;
    client_cfg.line_buf = client_line;
    client_cfg.line_buf_size = sizeof(client_line);
    client_cfg.io = &io;
    client_cfg.urcs = urcs;
    client_cfg.urc_count = 1;
    if (at_client_init(&g.client, &client_cfg) != AT_OK) {
        fprintf(stderr, "client init failed\n");
        return 1;
    }

    if (at_client_feed(&g.client, (const uint8_t *)boot, sizeof(boot) - 1) != AT_OK ||
        g.boots != 1) {
        fprintf(stderr, "URC was not delivered\n");
        return 1;
    }

    exchange("AT", AT_RESP_OK, "OK");
    exchange("AT+ADD=2,40", AT_RESP_OK, "+ADD: 42");
    exchange("AT+ADD=?", AT_RESP_OK, "+ADD: (int),(int)");
    exchange("AT+ECHO=\"hello\"", AT_RESP_OK, "+ECHO: hello");
    exchange("AT+GMR?", AT_RESP_OK, "freertos-at-parser " AT_PARSER_VERSION_STRING);
    exchange("AT+NOPE", AT_RESP_ERROR, "ERROR");

    if (g.failures != 0) {
        fprintf(stderr, "%d exchange(s) failed\n", g.failures);
        return 1;
    }
    printf("loopback demo passed\n");
    return 0;
}
