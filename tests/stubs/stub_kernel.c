/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 *
 * Just enough of the FreeRTOS API for the adapter to link on a host.
 * Queues copy items. Semaphores do not block; a take on an empty semaphore
 * returns immediately so a missing peer task shows up as a timeout.
 */
#include "semphr.h"
#include "task.h"

#include <stdlib.h>
#include <string.h>

struct stub_obj {
    int kind;
    int count;
    size_t item_size;
    size_t cap;
    size_t head;
    size_t nitems;
    uint8_t *storage;
    int notify;
};

enum {
    STUB_QUEUE = 1,
    STUB_MUTEX = 2,
    STUB_BINARY = 3,
    STUB_TASK = 4
};

static struct stub_obj *obj_new(int kind)
{
    struct stub_obj *obj = (struct stub_obj *)calloc(1, sizeof(*obj));
    if (obj != NULL) {
        obj->kind = kind;
    }
    return obj;
}

BaseType_t xTaskCreate(TaskFunction_t task_code, const char *name,
                       uint16_t stack_words, void *param, UBaseType_t priority,
                       TaskHandle_t *created)
{
    struct stub_obj *task;

    (void)name;
    (void)stack_words;
    (void)priority;
    task = obj_new(STUB_TASK);
    if (task == NULL) {
        return pdFAIL;
    }
    (void)task_code;
    (void)param;
    if (created != NULL) {
        *created = task;
    }
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task)
{
    free(task);
}

uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks)
{
    (void)clear_on_exit;
    (void)ticks;
    return 0;
}

void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken)
{
    struct stub_obj *obj = (struct stub_obj *)task;
    if (obj != NULL) {
        obj->notify++;
    }
    if (woken != NULL) {
        *woken = pdFALSE;
    }
}

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    struct stub_obj *q;

    if (length == 0 || item_size == 0) {
        return NULL;
    }
    q = obj_new(STUB_QUEUE);
    if (q == NULL) {
        return NULL;
    }
    q->storage = (uint8_t *)calloc(length, item_size);
    if (q->storage == NULL) {
        free(q);
        return NULL;
    }
    q->item_size = item_size;
    q->cap = length;
    return q;
}

void vQueueDelete(QueueHandle_t queue)
{
    if (queue == NULL) {
        return;
    }
    free(queue->storage);
    free(queue);
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks)
{
    uint8_t *slot;

    (void)ticks;
    if (queue == NULL || item == NULL || queue->nitems >= queue->cap) {
        return pdFALSE;
    }
    slot = queue->storage + (queue->head * queue->item_size);
    memcpy(slot, item, queue->item_size);
    queue->head++;
    if (queue->head == queue->cap) {
        queue->head = 0;
    }
    queue->nitems++;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks)
{
    size_t index;

    (void)ticks;
    if (queue == NULL || item == NULL || queue->nitems == 0) {
        return pdFALSE;
    }
    index = (queue->head + queue->cap - queue->nitems) % queue->cap;
    memcpy(item, queue->storage + (index * queue->item_size), queue->item_size);
    queue->nitems--;
    return pdTRUE;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    struct stub_obj *sem = obj_new(STUB_MUTEX);
    if (sem != NULL) {
        sem->count = 1;
    }
    return sem;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return obj_new(STUB_BINARY);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{
    (void)ticks;
    if (sem == NULL || sem->count <= 0) {
        return pdFALSE;
    }
    sem->count--;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem)
{
    if (sem == NULL) {
        return pdFALSE;
    }
    if (sem->count < 1) {
        sem->count++;
    }
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t sem)
{
    free(sem);
}
