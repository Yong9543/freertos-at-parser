/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#include "at_internal.h"

int at_ring_init(at_ring_t *ring, uint8_t *storage, size_t cap)
{
    if (ring == NULL || storage == NULL || cap < 2) {
        return AT_ERR_PARAM;
    }
    ring->buf = storage;
    ring->cap = cap;
    ring->head = 0;
    ring->tail = 0;
    return AT_OK;
}

size_t at_ring_write(at_ring_t *ring, const uint8_t *data, size_t len)
{
    size_t head;
    size_t tail;
    size_t written = 0;

    if (ring == NULL || ring->buf == NULL || (data == NULL && len > 0)) {
        return 0;
    }

    head = ring->head;
    tail = ring->tail;
    while (written < len) {
        size_t next = head + 1;
        if (next == ring->cap) {
            next = 0;
        }
        if (next == tail) {
            break;
        }
        ring->buf[head] = data[written];
        head = next;
        written++;
    }
    AT_BARRIER();
    ring->head = head;
    return written;
}

size_t at_ring_read(at_ring_t *ring, uint8_t *data, size_t len)
{
    size_t head;
    size_t tail;
    size_t got = 0;

    if (ring == NULL || ring->buf == NULL || (data == NULL && len > 0)) {
        return 0;
    }

    head = ring->head;
    tail = ring->tail;
    while (got < len && tail != head) {
        data[got] = ring->buf[tail];
        tail++;
        if (tail == ring->cap) {
            tail = 0;
        }
        got++;
    }
    AT_BARRIER();
    ring->tail = tail;
    return got;
}

size_t at_ring_used(const at_ring_t *ring)
{
    size_t head;
    size_t tail;

    if (ring == NULL || ring->cap == 0) {
        return 0;
    }
    head = ring->head;
    tail = ring->tail;
    if (head >= tail) {
        return head - tail;
    }
    return ring->cap - tail + head;
}

size_t at_ring_free(const at_ring_t *ring)
{
    size_t used;

    if (ring == NULL || ring->cap < 2) {
        return 0;
    }
    used = at_ring_used(ring);
    /* cap-1 is the maximum occupancy. */
    return (ring->cap - 1) - used;
}
