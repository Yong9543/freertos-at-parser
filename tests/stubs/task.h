/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#ifndef FREERTOS_STUB_TASK_H
#define FREERTOS_STUB_TASK_H

#include "FreeRTOS.h"

typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreate(TaskFunction_t task_code, const char *name,
                       uint16_t stack_words, void *param, UBaseType_t priority,
                       TaskHandle_t *created);
void vTaskDelete(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken);

#endif
