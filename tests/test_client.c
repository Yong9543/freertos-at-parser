/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#include "test_harness.h"

#include "at_parser.h"

#include <string.h>

typedef struct fixture {
    at_client_t *client;
    const char *script;
    char tx[AT_TX_LINE_MAX];
    size_t tx_len;
    int sem;
    int urc_hits;
    int idle_hits;
    char idle[64];
    int cli_bytes;
} fixture_t;

static int fix_write(void *ctx, const uint8_t *data, size_t len)
{
    fixture_t *fx = (fixture_t *)ctx;

    if (fx->tx_len + len < sizeof(fx->tx)) {
        memcpy(fx->tx + fx->tx_len, data, len);
        fx->tx_len += len;
        fx->tx[fx->tx_len] = '\0';
    }
    return (int)len;
}

static int fix_wait(void *ctx, uint32_t timeout_ms)
{
    fixture_t *fx = (fixture_t *)ctx;

    if (timeout_ms == 0) {
        if (fx->sem <= 0) {
            return AT_ERR_TIMEOUT;
        }
        fx->sem--;
        return 0;
    }
    if (fx->sem == 0 && fx->script != NULL) {
        (void)at_client_feed(fx->client, (const uint8_t *)fx->script, strlen(fx->script));
    }
    if (fx->sem > 0) {
        fx->sem--;
        return 0;
    }
    return AT_ERR_TIMEOUT;
}

static void fix_signal(void *ctx)
{
    ((fixture_t *)ctx)->sem++;
}

static void on_boot(void *user, const char *line, size_t len)
{
    fixture_t *fx = (fixture_t *)user;
    (void)line;
    (void)len;
    fx->urc_hits++;
}

static void on_idle(void *user, const char *line, size_t len)
{
    fixture_t *fx = (fixture_t *)user;
    if (len >= sizeof(fx->idle)) {
        len = sizeof(fx->idle) - 1;
    }
    memcpy(fx->idle, line, len);
    fx->idle[len] = '\0';
    fx->idle_hits++;
}

static void on_cli(void *user, const uint8_t *data, size_t len)
{
    fixture_t *fx = (fixture_t *)user;
    (void)data;
    fx->cli_bytes += (int)len;
}

static void reset_tx(fixture_t *fx)
{
    fx->tx_len = 0;
    fx->tx[0] = '\0';
    fx->sem = 0;
    fx->script = NULL;
}

void test_client(void)
{
    char line_buf[128];
    char resp_buf[128];
    at_client_t client;
    at_response_t resp;
    at_client_config_t cfg;
    at_io_ops_t io;
    at_sync_ops_t sync;
    fixture_t fx;
    at_urc_t urcs[1];
    at_resp_status_t st;
    int value = 0;

    memset(&fx, 0, sizeof(fx));
    fx.client = &client;
    urcs[0].prefix = "+BOOT";
    urcs[0].suffix = NULL;
    urcs[0].handler = on_boot;
    urcs[0].user = &fx;

    memset(&cfg, 0, sizeof(cfg));
    io.write = fix_write;
    io.ctx = &fx;
    sync.lock = NULL;
    sync.unlock = NULL;
    sync.wait = fix_wait;
    sync.signal = fix_signal;
    sync.ctx = &fx;
    cfg.line_buf = line_buf;
    cfg.line_buf_size = sizeof(line_buf);
    cfg.io = &io;
    cfg.sync = &sync;
    cfg.urcs = urcs;
    cfg.urc_count = 1;
    cfg.on_line = on_idle;
    cfg.on_line_user = &fx;

    EXPECT(at_client_init(&client, &cfg) == AT_OK);
    at_resp_init(&resp, resp_buf, sizeof(resp_buf), 0);

    fx.script = "\r\n+GMR: demo\r\n\r\nOK\r\n";
    st = at_client_exec(&client, &resp, 1000, "AT+GMR");
    EXPECT(st == AT_RESP_OK);
    EXPECT(strcmp(fx.tx, "AT+GMR\r\n") == 0);
    EXPECT(resp.line_count == 2);
    EXPECT(strcmp(at_resp_line(&resp, 0), "+GMR: demo") == 0);
    EXPECT(strcmp(at_resp_line(&resp, 1), "OK") == 0);
    EXPECT(at_resp_find(&resp, "+GMR:") != NULL);
    EXPECT(at_resp_find(&resp, "demo") != NULL);

    reset_tx(&fx);
    fx.script = "\r\nERROR\r\n";
    st = at_client_exec(&client, &resp, 500, "AT+NOPE");
    EXPECT(st == AT_RESP_ERROR);
    EXPECT(resp.line_count == 1);

    reset_tx(&fx);
    fx.script = "\r\nFAIL\r\n";
    st = at_client_exec(&client, &resp, 500, "AT+X");
    EXPECT(st == AT_RESP_FAIL);

    reset_tx(&fx);
    fx.script = "\r\n+CME ERROR: 10\r\n";
    st = at_client_exec(&client, &resp, 500, "AT+C");
    EXPECT(st == AT_RESP_ERROR);
    EXPECT(at_resp_scanf(&resp, 0, "+CME ERROR: %d", &value) == 1);
    EXPECT(value == 10);

    reset_tx(&fx);
    value = 0;
    fx.script = "\r\n+CMS ERROR: 310\r\n";
    st = at_client_exec(&client, &resp, 500, "AT+CMGS");
    EXPECT(st == AT_RESP_ERROR);
    EXPECT(resp.line_count == 1);
    EXPECT(at_resp_scanf(&resp, 0, "+CMS ERROR: %d", &value) == 1);
    EXPECT(value == 310);

    reset_tx(&fx);
    fx.script = NULL;
    st = at_client_exec(&client, &resp, 20, "AT");
    EXPECT(st == AT_RESP_TIMEOUT);
    EXPECT(at_client_is_waiting(&client) == 0);

    reset_tx(&fx);
    fx.urc_hits = 0;
    fx.script = "\r\n+BOOT: 1\r\n+VAL: 5\r\nOK\r\n";
    st = at_client_exec(&client, &resp, 500, "AT+VAL");
    EXPECT(st == AT_RESP_OK);
    EXPECT(fx.urc_hits == 1);
    EXPECT(resp.line_count == 2);
    EXPECT(strcmp(at_resp_line(&resp, 0), "+VAL: 5") == 0);
    EXPECT(at_resp_find(&resp, "+BOOT") == NULL);

    /* Prefix and suffix must both match. A prefix-only lookalike stays in the response. */
    reset_tx(&fx);
    fx.urc_hits = 0;
    {
        at_urc_t suffix_urc;

        suffix_urc.prefix = "+EVT";
        suffix_urc.suffix = ": done";
        suffix_urc.handler = on_boot;
        suffix_urc.user = &fx;
        EXPECT(at_client_set_urcs(&client, &suffix_urc, 1) == AT_OK);
        fx.script = "\r\n+EVT: 1: done\r\n+EVT: pending\r\nOK\r\n";
        st = at_client_exec(&client, &resp, 500, "AT+EVT");
        EXPECT(st == AT_RESP_OK);
        EXPECT(fx.urc_hits == 1);
        EXPECT(resp.line_count == 2);
        EXPECT(strcmp(at_resp_line(&resp, 0), "+EVT: pending") == 0);
        EXPECT(strcmp(at_resp_line(&resp, 1), "OK") == 0);
        EXPECT(at_resp_find(&resp, ": done") == NULL);
        EXPECT(at_client_set_urcs(&client, urcs, 1) == AT_OK);
    }

    reset_tx(&fx);
    fx.idle_hits = 0;
    resp.line_num = 1;
    fx.script = "+ONLY: 1\r\nOK\r\n";
    st = at_client_exec(&client, &resp, 500, "AT+ONLY");
    EXPECT(st == AT_RESP_OK);
    EXPECT(resp.line_count == 1);
    EXPECT(strcmp(at_resp_line(&resp, 0), "+ONLY: 1") == 0);
    EXPECT(fx.idle_hits == 1);
    EXPECT(strcmp(fx.idle, "OK") == 0);
    resp.line_num = 0;

    reset_tx(&fx);
    fx.script = ">";
    (void)at_client_set_end_sign(&client, '>');
    st = at_client_exec(&client, &resp, 500, "AT+PROMPT");
    EXPECT(st == AT_RESP_OK);
    EXPECT(strcmp(at_resp_line(&resp, 0), ">") == 0);
    (void)at_client_set_end_sign(&client, 0);

    reset_tx(&fx);
    fx.script = "OK\r\n";
    st = at_client_exec(&client, &resp, 500, "AT+GMR\r\n");
    EXPECT(st == AT_RESP_OK);
    EXPECT(strcmp(fx.tx, "AT+GMR\r\n") == 0);

    /* No wait callback: arm, then complete from feed. */
    sync.wait = NULL;
    sync.signal = NULL;
    reset_tx(&fx);
    st = at_client_exec(&client, &resp, 1000, "AT+PING");
    EXPECT(st == AT_RESP_PENDING);
    EXPECT(at_client_is_waiting(&client) == 1);
    (void)at_client_feed(&client, (const uint8_t *)"OK\r\n", 4);
    EXPECT(at_client_is_waiting(&client) == 0);
    EXPECT(at_client_last_status(&client) == AT_RESP_OK);

    reset_tx(&fx);
    st = at_client_exec(&client, &resp, 1000, "AT+LATE");
    EXPECT(st == AT_RESP_PENDING);
    at_client_abort(&client);
    EXPECT(at_client_last_status(&client) == AT_RESP_TIMEOUT);
    fx.idle_hits = 0;
    (void)at_client_feed(&client, (const uint8_t *)"OK\r\n", 4);
    EXPECT(fx.idle_hits == 1);
    EXPECT(at_client_last_status(&client) == AT_RESP_TIMEOUT);

    /* A command still armed rejects another exec and CLI entry. */
    reset_tx(&fx);
    fx.cli_bytes = 0;
    st = at_client_exec(&client, &resp, 1000, "AT+HOLD");
    EXPECT(st == AT_RESP_PENDING);
    EXPECT(at_client_is_waiting(&client) == 1);
    st = at_client_exec(&client, &resp, 1000, "AT+NEXT");
    EXPECT(st == AT_RESP_BUSY);
    EXPECT(at_client_enter_cli(&client, on_cli, &fx) == AT_ERR_BUSY);
    EXPECT(at_client_is_waiting(&client) == 1);
    EXPECT(strcmp(fx.tx, "AT+HOLD\r\n") == 0);
    (void)at_client_feed(&client, (const uint8_t *)"OK\r\n", 4);
    EXPECT(at_client_is_waiting(&client) == 0);
    EXPECT(at_client_last_status(&client) == AT_RESP_OK);
    EXPECT(fx.cli_bytes == 0);

    EXPECT(at_client_enter_cli(&client, on_cli, &fx) == AT_OK);
    st = at_client_exec(&client, &resp, 10, "AT");
    EXPECT(st == AT_RESP_BUSY);
    fx.cli_bytes = 0;
    (void)at_client_feed(&client, (const uint8_t *)"raw", 3);
    EXPECT(fx.cli_bytes == 3);
    EXPECT(at_client_exit_cli(&client) == AT_OK);

    sync.wait = fix_wait;
    sync.signal = fix_signal;

    {
        char tiny_line[8];
        char tiny_resp[8];
        at_client_t tiny;
        at_response_t tiny_r;
        at_client_config_t tiny_cfg;

        memset(&tiny_cfg, 0, sizeof(tiny_cfg));
        tiny_cfg.line_buf = tiny_line;
        tiny_cfg.line_buf_size = sizeof(tiny_line);
        tiny_cfg.io = &io;
        tiny_cfg.sync = &sync;
        EXPECT(at_client_init(&tiny, &tiny_cfg) == AT_OK);
        fx.client = &tiny;
        at_resp_init(&tiny_r, tiny_resp, sizeof(tiny_resp), 0);
        reset_tx(&fx);
        fx.script = "01234567\r\n";
        st = at_client_exec(&tiny, &tiny_r, 100, "AT+LONG");
        EXPECT(st == AT_RESP_OVERFLOW);
        fx.client = &client;
    }

    {
        char small[8];
        at_resp_init(&resp, small, sizeof(small), 0);
        reset_tx(&fx);
        fx.script = "+LONG: value\r\n";
        st = at_client_exec(&client, &resp, 100, "AT+LONG");
        EXPECT(st == AT_RESP_OVERFLOW);
    }

    at_resp_init(&resp, resp_buf, sizeof(resp_buf), 0);
    reset_tx(&fx);
    fx.script = "OK\r\n";
    st = at_client_exec(&client, NULL, 100, "AT");
    EXPECT(st == AT_RESP_OK);
    EXPECT(strcmp(fx.tx, "AT\r\n") == 0);

    EXPECT(at_resp_status_str(AT_RESP_OK) != NULL);
    EXPECT(at_status_str(AT_OK) != NULL);
}
