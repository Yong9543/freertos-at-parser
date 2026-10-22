/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 *
 * FreeRTOS adapter: RX ring drained by a task, optional TX queue, and the
 * semaphore pair used by at_client_exec. Include this header only from
 * firmware that already builds against FreeRTOS.
 */
#ifndef AT_FREERTOS_H
#define AT_FREERTOS_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "at_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*at_rx_fn)(void *ctx, const uint8_t *data, size_t len);

typedef struct at_rtos_config {
    at_write_fn uart_write; /* Called on the TX task. Must copy or finish TX before return. */
    void *uart_ctx;
    at_rx_fn on_rx;         /* Called on the RX task. NULL feeds cfg->client. */
    void *on_rx_ctx;
    at_client_t *client;    /* Optional. Receives the command lock, wake-up, and queued TX. */
    uint8_t *rx_storage;
    size_t rx_storage_size; /* Ring capacity, including the one unused slot. */
    uint16_t tx_queue_length;
    uint16_t rx_stack_words;
    uint16_t tx_stack_words;
    UBaseType_t rx_priority;
    UBaseType_t tx_priority;
    uint32_t tx_timeout_ms;
    const char *rx_task_name;
    const char *tx_task_name;
} at_rtos_config_t;

typedef struct at_rtos {
    at_ring_t rx_ring;
    at_write_fn uart_write;
    void *uart_ctx;
    at_rx_fn on_rx;
    void *on_rx_ctx;
    at_client_t *client;
    QueueHandle_t tx_queue;
    SemaphoreHandle_t cmd_lock;
    SemaphoreHandle_t resp_sem;
    SemaphoreHandle_t tx_done;
    TaskHandle_t rx_task;
    TaskHandle_t tx_task;
    at_io_ops_t io_ops;
    at_sync_ops_t sync_ops;
    uint32_t tx_timeout_ms;
    volatile uint16_t tx_seq;
    volatile uint16_t tx_done_seq;
    volatile int last_tx_status;
    int started;
} at_rtos_t;

int at_rtos_start(at_rtos_t *rt, const at_rtos_config_t *cfg);
void at_rtos_stop(at_rtos_t *rt);

/*
 * ISR-safe producer. Copies what fits into the ring and wakes the RX task.
 * woken is optional. When non-NULL it receives the FreeRTOS yield flag;
 * pass that flag to portYIELD_FROM_ISR in the ISR.
 * Returns 0, AT_ERR_PARAM, or AT_ERR_OVERFLOW when the ring could not take
 * every byte.
 */
int at_rtos_rx_from_isr(at_rtos_t *rt, const uint8_t *data, size_t len,
                        BaseType_t *woken);

#ifdef __cplusplus
}
#endif

#endif /* AT_FREERTOS_H */
