/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#ifndef FREERTOS_STUB_SEMPHR_H
#define FREERTOS_STUB_SEMPHR_H

#include "queue.h"

typedef QueueHandle_t SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
void vSemaphoreDelete(SemaphoreHandle_t sem);

#endif
