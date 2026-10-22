/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 *
 * Two UART ports on one MCU:
 *   - a local AT server (register handlers, reply OK / ERROR / +CMD)
 *   - a modem AT client (send a command and wait for the response)
 *
 * Replace board_uart_write() with the driver call that copies or finishes
 * the transfer before returning. From the receive ISR, pass the new bytes
 * to at_example_server_isr() or at_example_modem_isr(), then yield if the
 * FreeRTOS port asks you to:
 *
 *   BaseType_t woken = pdFALSE;
 *   at_rtos_rx_from_isr(&rt, bytes, n, &woken);
 *   portYIELD_FROM_ISR(woken);
 *
 * The helpers above already do that. Do not call at_client_feed(),
 * at_server_feed(), or at_client_exec() from interrupt context.
 * URC callbacks run on the RX task and must not call at_client_exec()
 * on that same client (the RX task would wait for itself).
 */
#include "at_example.h"

#include "at_freertos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static at_server_t g_server;
static at_client_t g_modem;
static at_rtos_t g_server_rt;
static at_rtos_t g_modem_rt;
static TaskHandle_t g_demo_task;

static char g_server_line[256];
static char g_modem_line[256];
static uint8_t g_server_rx[128];
static uint8_t g_modem_rx[128];
static at_cmd_entry_t g_table[8];

static int board_uart_write(void *ctx, const uint8_t *data, size_t len)
{
    /* Replace this with the UART / DMA transmit for the selected port.
     * Return only after the driver has copied the bytes or finished sending.
     */
    (void)ctx;
    (void)data;
    return (int)len;
}

static int on_vers(at_server_t *server, const at_cmd_t *cmd, void *user)
{
    (void)user;
    if (cmd->type != AT_CMD_READ && cmd->type != AT_CMD_EXEC) {
        return AT_ERR_PARAM;
    }
    return at_server_reply_line(server, "+VERS: freertos-at-parser %s",
                                AT_PARSER_VERSION_STRING);
}

static int on_echo(at_server_t *server, const at_cmd_t *cmd, void *user)
{
    (void)user;
    if (cmd->type != AT_CMD_SET || cmd->argc != 1) {
        return AT_ERR_PARAM;
    }
    return at_server_reply_line(server, "+ECHO: %s", cmd->argv[0]);
}

static int on_add(at_server_t *server, const at_cmd_t *cmd, void *user)
{
    char *end = NULL;
    long a;
    long b;

    (void)user;
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

static void on_boot(void *user, const char *line, size_t len)
{
    (void)user;
    (void)len;
    printf("modem urc: %s\n", line);
}

static void server_on_rx(void *ctx, const uint8_t *data, size_t len)
{
    (void)at_server_feed((at_server_t *)ctx, data, len);
}

static void modem_demo_task(void *arg)
{
    char storage[160];
    at_response_t resp;
    at_resp_status_t st;
    const char *line;

    (void)arg;
    at_resp_init(&resp, storage, sizeof(storage), 0);
    st = at_client_exec(&g_modem, &resp, 1000, "AT+GMR");
    line = at_resp_find(&resp, "+GMR:");
    printf("modem AT+GMR -> %s (%s)\n", at_resp_status_str(st),
           line != NULL ? line : "");
    g_demo_task = NULL;
    vTaskDelete(NULL);
}

int at_example_start(void)
{
    at_server_config_t server_cfg;
    at_client_config_t client_cfg;
    at_rtos_config_t server_rt;
    at_rtos_config_t modem_rt;
    static const at_urc_t urcs[] = {
        {"+BOOT", NULL, on_boot, NULL},
    };

    memset(&server_cfg, 0, sizeof(server_cfg));
    memset(&client_cfg, 0, sizeof(client_cfg));
    memset(&server_rt, 0, sizeof(server_rt));
    memset(&modem_rt, 0, sizeof(modem_rt));

    server_cfg.line_buf = g_server_line;
    server_cfg.line_buf_size = sizeof(g_server_line);
    server_cfg.write = board_uart_write;
    server_cfg.write_ctx = &g_server_rt;
    server_cfg.table = g_table;
    server_cfg.table_cap = 8;
    if (at_server_init(&g_server, &server_cfg) != AT_OK) {
        return AT_ERR_PARAM;
    }
    if (at_server_register(&g_server, "VERS", on_vers, NULL) != AT_OK ||
        at_server_register(&g_server, "ECHO", on_echo, NULL) != AT_OK ||
        at_server_register(&g_server, "ADD", on_add, NULL) != AT_OK) {
        return AT_ERR_NOMEM;
    }

    server_rt.uart_write = board_uart_write;
    server_rt.uart_ctx = &g_server_rt;
    server_rt.on_rx = server_on_rx;
    server_rt.on_rx_ctx = &g_server;
    server_rt.rx_storage = g_server_rx;
    server_rt.rx_storage_size = sizeof(g_server_rx);
    server_rt.rx_stack_words = 512;
    server_rt.rx_priority = 3;
    server_rt.rx_task_name = "at-server";
    if (at_rtos_start(&g_server_rt, &server_rt) != AT_OK) {
        return AT_ERR_NOMEM;
    }

    client_cfg.line_buf = g_modem_line;
    client_cfg.line_buf_size = sizeof(g_modem_line);
    client_cfg.urcs = urcs;
    client_cfg.urc_count = 1;
    if (at_client_init(&g_modem, &client_cfg) != AT_OK) {
        at_rtos_stop(&g_server_rt);
        return AT_ERR_PARAM;
    }

    modem_rt.uart_write = board_uart_write;
    modem_rt.uart_ctx = &g_modem_rt;
    modem_rt.client = &g_modem;
    modem_rt.rx_storage = g_modem_rx;
    modem_rt.rx_storage_size = sizeof(g_modem_rx);
    modem_rt.tx_queue_length = 4;
    modem_rt.rx_stack_words = 512;
    modem_rt.tx_stack_words = 512;
    modem_rt.rx_priority = 4;
    modem_rt.tx_priority = 3;
    modem_rt.tx_timeout_ms = 1000;
    modem_rt.rx_task_name = "at-modem-rx";
    modem_rt.tx_task_name = "at-modem-tx";
    if (at_rtos_start(&g_modem_rt, &modem_rt) != AT_OK) {
        at_rtos_stop(&g_server_rt);
        return AT_ERR_NOMEM;
    }

    if (xTaskCreate(modem_demo_task, "at-demo", 512, NULL, 2, &g_demo_task) != pdPASS) {
        at_example_stop();
        return AT_ERR_NOMEM;
    }
    return AT_OK;
}

void at_example_stop(void)
{
    if (g_demo_task != NULL) {
        vTaskDelete(g_demo_task);
        g_demo_task = NULL;
    }
    at_rtos_stop(&g_modem_rt);
    at_rtos_stop(&g_server_rt);
}

void at_example_server_isr(const uint8_t *data, size_t len)
{
    BaseType_t woken = pdFALSE;
    (void)at_rtos_rx_from_isr(&g_server_rt, data, len, &woken);
    portYIELD_FROM_ISR(woken);
}

void at_example_modem_isr(const uint8_t *data, size_t len)
{
    BaseType_t woken = pdFALSE;
    (void)at_rtos_rx_from_isr(&g_modem_rt, data, len, &woken);
    portYIELD_FROM_ISR(woken);
}
