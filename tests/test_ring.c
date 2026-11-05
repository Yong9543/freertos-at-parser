/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#include "test_harness.h"

#include "at_parser.h"

#include <string.h>

void test_ring(void)
{
    uint8_t storage[4];
    uint8_t out[8];
    at_ring_t ring;
    at_ring_t blank;
    size_t n;

    memset(&blank, 0, sizeof(blank));

    EXPECT(at_ring_init(NULL, storage, 4) == AT_ERR_PARAM);
    EXPECT(at_ring_init(&blank, NULL, 4) == AT_ERR_PARAM);
    EXPECT(at_ring_init(&blank, storage, 0) == AT_ERR_PARAM);
    EXPECT(at_ring_init(&ring, storage, 1) == AT_ERR_PARAM);

    /* A NULL ring, or one whose buffer was never installed, is a no-op. */
    EXPECT(at_ring_write(NULL, (const uint8_t *)"a", 1) == 0);
    EXPECT(at_ring_read(NULL, out, 1) == 0);
    EXPECT(at_ring_write(NULL, NULL, 0) == 0);
    EXPECT(at_ring_read(NULL, NULL, 0) == 0);
    EXPECT(at_ring_write(&blank, (const uint8_t *)"a", 1) == 0);
    EXPECT(at_ring_read(&blank, out, 1) == 0);
    EXPECT(at_ring_write(&blank, NULL, 0) == 0);
    EXPECT(at_ring_read(&blank, NULL, 0) == 0);
    EXPECT(at_ring_write(&blank, NULL, 1) == 0);
    EXPECT(at_ring_read(&blank, NULL, 1) == 0);
    EXPECT(at_ring_used(NULL) == 0);
    EXPECT(at_ring_free(NULL) == 0);
    EXPECT(at_ring_used(&blank) == 0);
    EXPECT(at_ring_free(&blank) == 0);

    EXPECT(at_ring_init(&ring, storage, 4) == AT_OK);
    EXPECT(at_ring_init(&ring, NULL, 4) == AT_ERR_PARAM);
    EXPECT(at_ring_free(&ring) == 3);
    EXPECT(at_ring_used(&ring) == 0);

    /* NULL data does not fault and does not move the indexes. */
    EXPECT(at_ring_write(&ring, NULL, 0) == 0);
    EXPECT(at_ring_read(&ring, NULL, 0) == 0);
    EXPECT(at_ring_write(&ring, NULL, 2) == 0);
    EXPECT(at_ring_read(&ring, NULL, 2) == 0);
    EXPECT(at_ring_used(&ring) == 0);
    EXPECT(at_ring_free(&ring) == 3);

    n = at_ring_write(&ring, (const uint8_t *)"abcd", 4);
    EXPECT(n == 3);
    EXPECT(at_ring_free(&ring) == 0);
    EXPECT(at_ring_write(&ring, (const uint8_t *)"z", 1) == 0);
    EXPECT(at_ring_write(&ring, NULL, 0) == 0);
    EXPECT(at_ring_read(&ring, NULL, 0) == 0);
    EXPECT(at_ring_write(&ring, NULL, 1) == 0);
    EXPECT(at_ring_read(&ring, NULL, 1) == 0);
    EXPECT(at_ring_used(&ring) == 3);

    n = at_ring_read(&ring, out, 2);
    EXPECT(n == 2);
    EXPECT(out[0] == 'a' && out[1] == 'b');
    EXPECT(at_ring_write(&ring, (const uint8_t *)"XY", 2) == 2);

    n = at_ring_read(&ring, out, sizeof(out));
    EXPECT(n == 3);
    EXPECT(memcmp(out, "cXY", 3) == 0);
    EXPECT(at_ring_used(&ring) == 0);
    EXPECT(at_ring_read(&ring, out, 1) == 0);

    /* Dropping the buffer pointer rejects I/O even while a byte is stored. */
    EXPECT(at_ring_write(&ring, (const uint8_t *)"Q", 1) == 1);
    ring.buf = NULL;
    EXPECT(at_ring_write(&ring, (const uint8_t *)"Z", 1) == 0);
    EXPECT(at_ring_read(&ring, out, 1) == 0);
    EXPECT(at_ring_write(&ring, NULL, 0) == 0);
    EXPECT(at_ring_read(&ring, NULL, 0) == 0);
    EXPECT(at_ring_used(&ring) == 1);
}
