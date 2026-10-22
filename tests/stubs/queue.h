/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#ifndef FREERTOS_STUB_QUEUE_H
#define FREERTOS_STUB_QUEUE_H

#include "FreeRTOS.h"

typedef struct stub_obj *QueueHandle_t;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
void vQueueDelete(QueueHandle_t queue);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks);

#endif
