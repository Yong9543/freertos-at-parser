/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#include "at_freertos.h"

#include <string.h>

static TickType_t ms_to_ticks(uint32_t ms)
{
    TickType_t ticks;

    if (ms == 0) {
        return 0;
    }
    if (ms == portMAX_DELAY) {
        return portMAX_DELAY;
    }
    ticks = pdMS_TO_TICKS(ms);
    if (ticks == 0) {
        ticks = 1;
    }
    return ticks;
}

static void rx_drain(at_rtos_t *rt)
{
    uint8_t buf[64];
    size_t n;

    if (rt->on_rx == NULL) {
        return;
    }
    for (;;) {
        n = at_ring_read(&rt->rx_ring, buf, sizeof(buf));
        if (n == 0) {
            return;
        }
        rt->on_rx(rt->on_rx_ctx, buf, n);
    }
}

static void rx_task(void *arg)
{
    at_rtos_t *rt = (at_rtos_t *)arg;

    for (;;) {
        rx_drain(rt);
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

/* Copied into the queue so the caller's buffer can be reused after a timeout. */
typedef struct at_tx_item {
    uint16_t seq;
    uint16_t len;
    uint8_t data[AT_TX_LINE_MAX];
} at_tx_item_t;

static void tx_task(void *arg)
{
    at_rtos_t *rt = (at_rtos_t *)arg;

    for (;;) {
        at_tx_item_t item;
        int rc = -1;

        if (xQueueReceive(rt->tx_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (rt->uart_write != NULL && item.len > 0) {
            rc = rt->uart_write(rt->uart_ctx, item.data, item.len);
        }
        if (rc < 0 || (size_t)rc != item.len) {
            rt->last_tx_status = -1;
        } else {
            rt->last_tx_status = 0;
        }
        rt->tx_done_seq = item.seq;
        (void)xSemaphoreGive(rt->tx_done);
    }
}

static int queue_chunk(at_rtos_t *rt, const uint8_t *data, size_t len)
{
    at_tx_item_t item;
    uint16_t seq;

    if (len > sizeof(item.data)) {
        return -1;
    }
    seq = (uint16_t)(rt->tx_seq + 1u);
    rt->tx_seq = seq;
    item.seq = seq;
    item.len = (uint16_t)len;
    memcpy(item.data, data, len);

    if (xQueueSend(rt->tx_queue, &item, ms_to_ticks(rt->tx_timeout_ms)) != pdTRUE) {
        return -1;
    }

    for (;;) {
        if (xSemaphoreTake(rt->tx_done, ms_to_ticks(rt->tx_timeout_ms)) != pdTRUE) {
            /* The copy remains queued and may still be transmitted. */
            return -1;
        }
        if (rt->tx_done_seq == seq) {
            break;
        }
    }
    if (rt->last_tx_status != 0) {
        return -1;
    }
    return 0;
}

static int queued_write(void *ctx, const uint8_t *data, size_t len)
{
    at_rtos_t *rt = (at_rtos_t *)ctx;
    size_t off = 0;

    if (data == NULL && len > 0) {
        return -1;
    }
    while (off < len) {
        size_t n = len - off;
        if (n > AT_TX_LINE_MAX) {
            n = AT_TX_LINE_MAX;
        }
        if (queue_chunk(rt, data + off, n) != 0) {
            return -1;
        }
        off += n;
    }
    return (int)len;
}

static void rtos_lock(void *ctx)
{
    at_rtos_t *rt = (at_rtos_t *)ctx;
    (void)xSemaphoreTake(rt->cmd_lock, portMAX_DELAY);
}

static void rtos_unlock(void *ctx)
{
    at_rtos_t *rt = (at_rtos_t *)ctx;
    (void)xSemaphoreGive(rt->cmd_lock);
}

static int rtos_wait(void *ctx, uint32_t timeout_ms)
{
    at_rtos_t *rt = (at_rtos_t *)ctx;
    if (xSemaphoreTake(rt->resp_sem, ms_to_ticks(timeout_ms)) == pdTRUE) {
        return 0;
    }
    return AT_ERR_TIMEOUT;
}

static void rtos_signal(void *ctx)
{
    at_rtos_t *rt = (at_rtos_t *)ctx;
    (void)xSemaphoreGive(rt->resp_sem);
}

static void feed_client(void *ctx, const uint8_t *data, size_t len)
{
    (void)at_client_feed((at_client_t *)ctx, data, len);
}

static void rtos_cleanup(at_rtos_t *rt)
{
    if (rt->rx_task != NULL) {
        vTaskDelete(rt->rx_task);
        rt->rx_task = NULL;
    }
    if (rt->tx_task != NULL) {
        vTaskDelete(rt->tx_task);
        rt->tx_task = NULL;
    }
    if (rt->tx_queue != NULL) {
        vQueueDelete(rt->tx_queue);
        rt->tx_queue = NULL;
    }
    if (rt->cmd_lock != NULL) {
        vSemaphoreDelete(rt->cmd_lock);
        rt->cmd_lock = NULL;
    }
    if (rt->resp_sem != NULL) {
        vSemaphoreDelete(rt->resp_sem);
        rt->resp_sem = NULL;
    }
    if (rt->tx_done != NULL) {
        vSemaphoreDelete(rt->tx_done);
        rt->tx_done = NULL;
    }
}

int at_rtos_start(at_rtos_t *rt, const at_rtos_config_t *cfg)
{
    uint16_t rx_stack;
    uint16_t tx_stack;
    uint16_t qlen;
    const char *rx_name;
    const char *tx_name;

    if (rt == NULL || cfg == NULL || cfg->rx_storage == NULL || cfg->rx_storage_size < 2) {
        return AT_ERR_PARAM;
    }
    if (cfg->on_rx == NULL && cfg->client == NULL) {
        return AT_ERR_PARAM;
    }
    if (cfg->client != NULL && cfg->uart_write == NULL) {
        return AT_ERR_PARAM;
    }
    /* rt must be zeroed before the first start. A live adapter is not restarted. */
    if (rt->started) {
        return AT_ERR_STATE;
    }

    memset(rt, 0, sizeof(*rt));
    if (at_ring_init(&rt->rx_ring, cfg->rx_storage, cfg->rx_storage_size) != AT_OK) {
        return AT_ERR_PARAM;
    }

    rt->uart_write = cfg->uart_write;
    rt->uart_ctx = cfg->uart_ctx;
    rt->client = cfg->client;
    rt->tx_timeout_ms = cfg->tx_timeout_ms == 0 ? 1000u : cfg->tx_timeout_ms;
    if (cfg->on_rx != NULL) {
        rt->on_rx = cfg->on_rx;
        rt->on_rx_ctx = cfg->on_rx_ctx;
    } else {
        rt->on_rx = feed_client;
        rt->on_rx_ctx = cfg->client;
    }

    rx_stack = cfg->rx_stack_words == 0 ? 512u : cfg->rx_stack_words;
    tx_stack = cfg->tx_stack_words == 0 ? 512u : cfg->tx_stack_words;
    qlen = cfg->tx_queue_length == 0 ? 4u : cfg->tx_queue_length;
    rx_name = cfg->rx_task_name != NULL ? cfg->rx_task_name : "at-rx";
    tx_name = cfg->tx_task_name != NULL ? cfg->tx_task_name : "at-tx";

    if (xTaskCreate(rx_task, rx_name, rx_stack, rt, cfg->rx_priority, &rt->rx_task) != pdPASS) {
        rtos_cleanup(rt);
        return AT_ERR_NOMEM;
    }

    if (cfg->client != NULL) {
        rt->cmd_lock = xSemaphoreCreateMutex();
        rt->resp_sem = xSemaphoreCreateBinary();
        rt->tx_done = xSemaphoreCreateBinary();
        rt->tx_queue = xQueueCreate(qlen, (UBaseType_t)sizeof(at_tx_item_t));
        if (rt->cmd_lock == NULL || rt->resp_sem == NULL || rt->tx_done == NULL ||
            rt->tx_queue == NULL) {
            rtos_cleanup(rt);
            return AT_ERR_NOMEM;
        }
        if (xTaskCreate(tx_task, tx_name, tx_stack, rt, cfg->tx_priority, &rt->tx_task) != pdPASS) {
            rtos_cleanup(rt);
            return AT_ERR_NOMEM;
        }

        rt->io_ops.write = queued_write;
        rt->io_ops.ctx = rt;
        rt->sync_ops.lock = rtos_lock;
        rt->sync_ops.unlock = rtos_unlock;
        rt->sync_ops.wait = rtos_wait;
        rt->sync_ops.signal = rtos_signal;
        rt->sync_ops.ctx = rt;
        (void)at_client_set_io(cfg->client, &rt->io_ops);
        (void)at_client_set_sync(cfg->client, &rt->sync_ops);
    }

    rt->started = 1;
    return AT_OK;
}

void at_rtos_stop(at_rtos_t *rt)
{
    at_client_t *client;

    if (rt == NULL || !rt->started) {
        return;
    }
    client = rt->client;
    if (client != NULL) {
        if (client->io == &rt->io_ops) {
            (void)at_client_set_io(client, NULL);
        }
        if (client->sync == &rt->sync_ops) {
            (void)at_client_set_sync(client, NULL);
        }
    }
    rtos_cleanup(rt);
    rt->started = 0;
}

int at_rtos_rx_from_isr(at_rtos_t *rt, const uint8_t *data, size_t len, BaseType_t *woken)
{
    size_t stored;
    BaseType_t local_woken = pdFALSE;

    if (rt == NULL || !rt->started || (data == NULL && len > 0)) {
        return AT_ERR_PARAM;
    }
    if (len == 0) {
        return AT_OK;
    }

    stored = at_ring_write(&rt->rx_ring, data, len);
    if (rt->rx_task != NULL) {
        vTaskNotifyGiveFromISR(rt->rx_task, &local_woken);
    }
    if (woken != NULL) {
        *woken = local_woken;
    }
    if (stored != len) {
        return AT_ERR_OVERFLOW;
    }
    return AT_OK;
}
