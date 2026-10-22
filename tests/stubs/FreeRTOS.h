/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 *
 * Host stand-in so the FreeRTOS adapter can be type-checked without the kernel.
 * This is not a scheduler.
 */
#ifndef FREERTOS_STUB_H
#define FREERTOS_STUB_H

#include <stddef.h>
#include <stdint.h>

typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY ((TickType_t)0xffffffffu)

#define configTICK_RATE_HZ 1000
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#define portYIELD_FROM_ISR(x)                                                  \
    do {                                                                       \
        (void)(x);                                                             \
    } while (0)

#endif
