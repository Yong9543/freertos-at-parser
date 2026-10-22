/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#include "at_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void client_lock(at_client_t *client)
{
    if (client->sync != NULL && client->sync->lock != NULL) {
        client->sync->lock(client->sync->ctx);
    }
}

static void client_unlock(at_client_t *client)
{
    if (client->sync != NULL && client->sync->unlock != NULL) {
        client->sync->unlock(client->sync->ctx);
    }
}

static void client_signal(at_client_t *client)
{
    if (client->sync != NULL && client->sync->signal != NULL) {
        client->sync->signal(client->sync->ctx);
    }
}

static void complete_resp(at_client_t *client, int status)
{
    client->resp_status = status;
    if (client->resp != NULL) {
        client->resp->status = status;
    }
    client->waiting = 0;
    AT_BARRIER();
    client_signal(client);
}

void at_resp_init(at_response_t *resp, char *buf, size_t buf_size, size_t line_num)
{
    if (resp == NULL) {
        return;
    }
    resp->buf = buf;
    resp->buf_size = buf_size;
    resp->buf_len = 0;
    resp->line_num = line_num;
    resp->line_count = 0;
    resp->status = AT_RESP_IDLE;
    if (buf != NULL && buf_size > 0) {
        buf[0] = '\0';
    }
}

void at_resp_reset(at_response_t *resp)
{
    if (resp == NULL) {
        return;
    }
    resp->buf_len = 0;
    resp->line_count = 0;
    resp->status = AT_RESP_IDLE;
    if (resp->buf != NULL && resp->buf_size > 0) {
        resp->buf[0] = '\0';
    }
}

const char *at_resp_line(const at_response_t *resp, size_t index)
{
    const char *p;
    size_t i;

    if (resp == NULL || resp->buf == NULL || index >= resp->line_count) {
        return NULL;
    }
    p = resp->buf;
    for (i = 0; i < index; i++) {
        p += strlen(p) + 1;
    }
    return p;
}

const char *at_resp_find(const at_response_t *resp, const char *keyword)
{
    size_t i;

    if (resp == NULL || keyword == NULL) {
        return NULL;
    }
    for (i = 0; i < resp->line_count; i++) {
        const char *line = at_resp_line(resp, i);
        if (line != NULL && strstr(line, keyword) != NULL) {
            return line;
        }
    }
    return NULL;
}

int at_resp_scanf(const at_response_t *resp, size_t index, const char *fmt, ...)
{
    const char *line;
    va_list ap;
    int n;

    line = at_resp_line(resp, index);
    if (line == NULL || fmt == NULL) {
        return -1;
    }
    va_start(ap, fmt);
    n = vsscanf(line, fmt, ap);
    va_end(ap);
    return n;
}

int at_client_init(at_client_t *client, const at_client_config_t *cfg)
{
    if (client == NULL || cfg == NULL || cfg->line_buf == NULL || cfg->line_buf_size < 2) {
        return AT_ERR_PARAM;
    }
    memset(client, 0, sizeof(*client));
    at_line_parser_init(&client->line, cfg->line_buf, cfg->line_buf_size, cfg->end_sign);
    client->io = cfg->io;
    client->sync = cfg->sync;
    client->urcs = cfg->urcs;
    client->urc_count = cfg->urc_count;
    client->on_line = cfg->on_line;
    client->on_line_user = cfg->on_line_user;
    client->resp_status = AT_RESP_IDLE;
    client->ready = 1;
    return AT_OK;
}

int at_client_set_io(at_client_t *client, const at_io_ops_t *io)
{
    if (client == NULL || !client->ready) {
        return AT_ERR_PARAM;
    }
    client->io = io;
    return AT_OK;
}

int at_client_set_sync(at_client_t *client, const at_sync_ops_t *sync)
{
    if (client == NULL || !client->ready) {
        return AT_ERR_PARAM;
    }
    client->sync = sync;
    return AT_OK;
}

int at_client_set_urcs(at_client_t *client, const at_urc_t *urcs, size_t count)
{
    if (client == NULL || !client->ready || (urcs == NULL && count > 0)) {
        return AT_ERR_PARAM;
    }
    client->urcs = urcs;
    client->urc_count = count;
    return AT_OK;
}

int at_client_set_end_sign(at_client_t *client, char end_sign)
{
    if (client == NULL || !client->ready) {
        return AT_ERR_PARAM;
    }
    client->line.end_sign = end_sign;
    return AT_OK;
}

int at_client_enter_cli(at_client_t *client, at_cli_fn fn, void *user)
{
    if (client == NULL || !client->ready) {
        return AT_ERR_PARAM;
    }
    if (client->waiting) {
        return AT_ERR_BUSY;
    }
    at_line_parser_reset(&client->line);
    client->cli_fn = fn;
    client->cli_user = user;
    client->cli = 1;
    return AT_OK;
}

int at_client_exit_cli(at_client_t *client)
{
    if (client == NULL || !client->ready) {
        return AT_ERR_PARAM;
    }
    client->cli = 0;
    client->cli_fn = NULL;
    client->cli_user = NULL;
    at_line_parser_reset(&client->line);
    return AT_OK;
}

int at_client_is_waiting(const at_client_t *client)
{
    if (client == NULL) {
        return 0;
    }
    return client->waiting ? 1 : 0;
}

at_resp_status_t at_client_last_status(const at_client_t *client)
{
    if (client == NULL) {
        return AT_RESP_PARAM;
    }
    return (at_resp_status_t)client->resp_status;
}

static const at_urc_t *match_urc(const at_client_t *client, const char *line, size_t len)
{
    size_t i;

    for (i = 0; i < client->urc_count; i++) {
        const at_urc_t *urc = &client->urcs[i];
        size_t plen = (urc->prefix != NULL) ? strlen(urc->prefix) : 0;
        size_t slen = (urc->suffix != NULL) ? strlen(urc->suffix) : 0;

        if (plen == 0 && slen == 0) {
            continue;
        }
        if (plen + slen > len) {
            continue;
        }
        if (plen > 0 && memcmp(line, urc->prefix, plen) != 0) {
            continue;
        }
        if (slen > 0 && memcmp(line + len - slen, urc->suffix, slen) != 0) {
            continue;
        }
        return urc;
    }
    return NULL;
}

static int final_of(const char *line)
{
    if (at_str_eq_ci(line, "OK")) {
        return AT_RESP_OK;
    }
    if (at_str_eq_ci(line, "ERROR")) {
        return AT_RESP_ERROR;
    }
    if (at_str_eq_ci(line, "FAIL")) {
        return AT_RESP_FAIL;
    }
    if (at_starts_with_ci(line, "+CME ERROR") || at_starts_with_ci(line, "+CMS ERROR")) {
        return AT_RESP_ERROR;
    }
    return AT_RESP_PENDING;
}

static int append_line(at_response_t *resp, const char *line, size_t len)
{
    if (resp->buf == NULL || resp->buf_len + len + 1 > resp->buf_size) {
        return -1;
    }
    memcpy(resp->buf + resp->buf_len, line, len);
    resp->buf_len += len;
    resp->buf[resp->buf_len++] = '\0';
    resp->line_count++;
    return 0;
}

static void handle_line(at_client_t *client, int by_sign)
{
    const char *line = client->line.buf;
    size_t len = client->line.len;
    const at_urc_t *urc;
    int final_status;

    if (client->waiting && client->resp != NULL) {
        final_status = final_of(line);
        if (final_status == AT_RESP_PENDING) {
            urc = match_urc(client, line, len);
            if (urc != NULL) {
                if (urc->handler != NULL) {
                    urc->handler(urc->user, line, len);
                }
                return;
            }
        }
        if (append_line(client->resp, line, len) != 0) {
            complete_resp(client, AT_RESP_OVERFLOW);
            return;
        }
        if (final_status != AT_RESP_PENDING) {
            complete_resp(client, final_status);
            return;
        }
        if (client->resp->line_num > 0 &&
            client->resp->line_count >= client->resp->line_num) {
            complete_resp(client, AT_RESP_OK);
            return;
        }
        if (by_sign && client->resp->line_num == 0) {
            complete_resp(client, AT_RESP_OK);
        }
        return;
    }

    urc = match_urc(client, line, len);
    if (urc != NULL) {
        if (urc->handler != NULL) {
            urc->handler(urc->user, line, len);
        }
        return;
    }
    if (client->on_line != NULL) {
        client->on_line(client->on_line_user, line, len);
    }
}

int at_client_feed(at_client_t *client, const uint8_t *data, size_t len)
{
    size_t i;

    if (client == NULL || !client->ready || (data == NULL && len > 0)) {
        return AT_ERR_PARAM;
    }
    if (client->in_feed) {
        return AT_ERR_BUSY;
    }
    client->in_feed = 1;

    if (client->cli) {
        if (len > 0 && client->cli_fn != NULL) {
            client->cli_fn(client->cli_user, data, len);
        }
        client->in_feed = 0;
        return AT_OK;
    }

    for (i = 0; i < len; i++) {
        at_line_event_t ev = at_line_parser_byte(&client->line, data[i]);
        if (ev == AT_LINE_READY) {
            handle_line(client, client->line.ended_by_sign);
        } else if (ev == AT_LINE_OVERFLOW && client->waiting) {
            complete_resp(client, AT_RESP_OVERFLOW);
        }
    }

    client->in_feed = 0;
    return AT_OK;
}

void at_client_abort(at_client_t *client)
{
    if (client == NULL || !client->ready) {
        return;
    }
    client_lock(client);
    if (client->waiting) {
        client->resp_status = AT_RESP_TIMEOUT;
        if (client->resp != NULL) {
            client->resp->status = AT_RESP_TIMEOUT;
        }
        client->waiting = 0;
        AT_BARRIER();
    }
    client_unlock(client);
}

static int append_crlf(char *cmd, int n, int cap)
{
    if (n > 0 && (cmd[n - 1] == '\n' || cmd[n - 1] == '\r')) {
        return n;
    }
    if (n + 2 >= cap) {
        return -1;
    }
    cmd[n++] = '\r';
    cmd[n++] = '\n';
    cmd[n] = '\0';
    return n;
}

at_resp_status_t at_client_exec(at_client_t *client, at_response_t *resp,
                                uint32_t timeout_ms, const char *fmt, ...)
{
    char cmd[AT_TX_LINE_MAX];
    va_list ap;
    int n;
    int wr;
    at_resp_status_t st;

    if (client == NULL || !client->ready || fmt == NULL) {
        return AT_RESP_PARAM;
    }
    if (client->cli) {
        return AT_RESP_BUSY;
    }

    client_lock(client);
    if (client->waiting) {
        client_unlock(client);
        return AT_RESP_BUSY;
    }

    if (resp != NULL) {
        if (resp->buf == NULL || resp->buf_size < 2) {
            client_unlock(client);
            return AT_RESP_PARAM;
        }
        at_resp_reset(resp);
        resp->status = AT_RESP_PENDING;
    }

    va_start(ap, fmt);
    n = vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        client_unlock(client);
        return AT_RESP_OVERFLOW;
    }
    if (n == 0) {
        client_unlock(client);
        return AT_RESP_PARAM;
    }
    n = append_crlf(cmd, n, (int)sizeof(cmd));
    if (n < 0) {
        client_unlock(client);
        return AT_RESP_OVERFLOW;
    }

    if (resp != NULL) {
        client->resp = resp;
        client->resp_status = AT_RESP_PENDING;
        AT_BARRIER();
        client->waiting = 1;
    } else {
        client->resp = NULL;
        client->waiting = 0;
    }

    if (client->io == NULL || client->io->write == NULL) {
        client->waiting = 0;
        client->resp = NULL;
        client->resp_status = AT_RESP_IO;
        if (resp != NULL) {
            resp->status = AT_RESP_IO;
        }
        client_unlock(client);
        return AT_RESP_IO;
    }

    wr = at_write_all(client->io->write, client->io->ctx, (const uint8_t *)cmd, (size_t)n);
    if (wr != AT_OK) {
        client->waiting = 0;
        client->resp = NULL;
        client->resp_status = AT_RESP_IO;
        if (resp != NULL) {
            resp->status = AT_RESP_IO;
        }
        client_unlock(client);
        return AT_RESP_IO;
    }

    if (resp == NULL) {
        client->resp_status = AT_RESP_OK;
        client_unlock(client);
        return AT_RESP_OK;
    }

    st = (at_resp_status_t)client->resp_status;
    if (client->sync != NULL && client->sync->wait != NULL) {
        if (st == AT_RESP_PENDING) {
            int w = client->sync->wait(client->sync->ctx, timeout_ms);
            st = (at_resp_status_t)client->resp_status;
            if (w != 0 && st == AT_RESP_PENDING) {
                st = AT_RESP_TIMEOUT;
                client->resp_status = st;
                resp->status = st;
            }
        } else {
            (void)client->sync->wait(client->sync->ctx, 0);
        }
        client->waiting = 0;
        client->resp = NULL;
    } else if (st != AT_RESP_PENDING) {
        client->waiting = 0;
        client->resp = NULL;
    }

    client_unlock(client);
    return st;
}
