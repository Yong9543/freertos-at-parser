/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#include "at_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef AT_SERVER_REPLY_MAX
#define AT_SERVER_REPLY_MAX 256
#endif

static int server_write(at_server_t *server, const char *text, size_t len)
{
    return at_write_all(server->write, server->write_ctx, (const uint8_t *)text, len);
}

int at_server_init(at_server_t *server, const at_server_config_t *cfg)
{
    size_t i;

    if (server == NULL || cfg == NULL || cfg->line_buf == NULL || cfg->line_buf_size < 2) {
        return AT_ERR_PARAM;
    }
    if ((cfg->table == NULL && cfg->table_cap > 0) || cfg->table_count > cfg->table_cap) {
        return AT_ERR_PARAM;
    }
    for (i = 0; i < cfg->table_count; i++) {
        if (cfg->table[i].name == NULL || cfg->table[i].handler == NULL) {
            return AT_ERR_PARAM;
        }
    }

    memset(server, 0, sizeof(*server));
    at_line_parser_init(&server->line, cfg->line_buf, cfg->line_buf_size, 0);
    server->write = cfg->write;
    server->write_ctx = cfg->write_ctx;
    server->table = cfg->table;
    server->table_count = cfg->table_count;
    server->table_cap = cfg->table_cap;
    server->echo = cfg->echo ? 1 : 0;
    server->ready = 1;
    return AT_OK;
}

int at_server_register(at_server_t *server, const char *name, at_cmd_fn handler, void *user)
{
    size_t i;

    if (server == NULL || !server->ready || name == NULL || name[0] == '\0' || handler == NULL) {
        return AT_ERR_PARAM;
    }
    if (server->table == NULL || server->table_count >= server->table_cap) {
        return AT_ERR_NOMEM;
    }
    for (i = 0; i < server->table_count; i++) {
        if (at_str_eq_ci(server->table[i].name, name)) {
            return AT_ERR_PARAM;
        }
    }
    server->table[server->table_count].name = name;
    server->table[server->table_count].handler = handler;
    server->table[server->table_count].user = user;
    server->table_count++;
    return AT_OK;
}

int at_server_set_echo(at_server_t *server, int enable)
{
    if (server == NULL || !server->ready) {
        return AT_ERR_PARAM;
    }
    server->echo = enable ? 1 : 0;
    return AT_OK;
}

int at_server_reply_line(at_server_t *server, const char *fmt, ...)
{
    char body[AT_SERVER_REPLY_MAX];
    va_list ap;
    int n;
    int rc;

    if (server == NULL || !server->ready || fmt == NULL) {
        return AT_ERR_PARAM;
    }
    va_start(ap, fmt);
    n = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        return AT_ERR_OVERFLOW;
    }
    rc = server_write(server, "\r\n", 2);
    if (rc != AT_OK) {
        return rc;
    }
    rc = server_write(server, body, (size_t)n);
    if (rc != AT_OK) {
        return rc;
    }
    return server_write(server, "\r\n", 2);
}

int at_server_reply_ok(at_server_t *server)
{
    int rc;

    if (server == NULL || !server->ready) {
        return AT_ERR_PARAM;
    }
    rc = server_write(server, "\r\nOK\r\n", 6);
    if (rc == AT_OK) {
        server->final_sent = 1;
    }
    return rc;
}

int at_server_reply_error(at_server_t *server)
{
    int rc;

    if (server == NULL || !server->ready) {
        return AT_ERR_PARAM;
    }
    rc = server_write(server, "\r\nERROR\r\n", 9);
    if (rc == AT_OK) {
        server->final_sent = 1;
    }
    return rc;
}

int at_server_reply_cme(at_server_t *server, int code)
{
    int rc;

    if (server == NULL || !server->ready) {
        return AT_ERR_PARAM;
    }
    rc = at_server_reply_line(server, "+CME ERROR: %d", code);
    if (rc == AT_OK) {
        server->final_sent = 1;
    }
    return rc;
}

int at_server_reply_cms(at_server_t *server, int code)
{
    int rc;

    if (server == NULL || !server->ready) {
        return AT_ERR_PARAM;
    }
    rc = at_server_reply_line(server, "+CMS ERROR: %d", code);
    if (rc == AT_OK) {
        server->final_sent = 1;
    }
    return rc;
}

static const at_cmd_entry_t *find_cmd(const at_server_t *server, const char *name)
{
    size_t i;

    for (i = 0; i < server->table_count; i++) {
        if (at_str_eq_ci(server->table[i].name, name)) {
            return &server->table[i];
        }
    }
    return NULL;
}

static int dispatch_line(at_server_t *server)
{
    at_cmd_t cmd;
    const at_cmd_entry_t *entry;
    int rc;

    /* The line parser keeps the text in its buffer. Parsing inserts NULs. */
    rc = at_parse_command(server->line.buf, &cmd);
    if (rc != AT_OK) {
        return at_server_reply_error(server);
    }

    entry = find_cmd(server, cmd.name);
    if (entry == NULL) {
        if (cmd.type == AT_CMD_BASIC && cmd.name[0] == '\0') {
            return at_server_reply_ok(server);
        }
        if (cmd.type == AT_CMD_BASIC && at_str_eq_ci(cmd.name, "E0")) {
            server->echo = 0;
            return at_server_reply_ok(server);
        }
        if (cmd.type == AT_CMD_BASIC && at_str_eq_ci(cmd.name, "E1")) {
            server->echo = 1;
            return at_server_reply_ok(server);
        }
        return at_server_reply_error(server);
    }

    server->final_sent = 0;
    rc = entry->handler(server, &cmd, entry->user);
    if (server->final_sent || rc == AT_REPLIED) {
        return AT_OK;
    }
    if (rc == AT_ERR_IO) {
        return rc;
    }
    if (rc == AT_OK) {
        return at_server_reply_ok(server);
    }
    return at_server_reply_error(server);
}

int at_server_feed(at_server_t *server, const uint8_t *data, size_t len)
{
    size_t i;

    if (server == NULL || !server->ready || (data == NULL && len > 0)) {
        return AT_ERR_PARAM;
    }
    if (server->in_feed) {
        return AT_ERR_BUSY;
    }
    server->in_feed = 1;

    for (i = 0; i < len; i++) {
        at_line_event_t ev = at_line_parser_byte(&server->line, data[i]);
        int rc = AT_OK;

        if (ev == AT_LINE_OVERFLOW) {
            rc = at_server_reply_error(server);
        } else if (ev == AT_LINE_READY) {
            if (server->echo) {
                rc = server_write(server, server->line.buf, server->line.len);
                if (rc == AT_OK) {
                    rc = server_write(server, "\r\n", 2);
                }
            }
            if (rc == AT_OK) {
                rc = dispatch_line(server);
            }
        }

        if (rc != AT_OK) {
            server->in_feed = 0;
            return rc;
        }
    }

    server->in_feed = 0;
    return AT_OK;
}
