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
    size_t n;

    EXPECT(at_ring_init(&ring, storage, 1) == AT_ERR_PARAM);
    EXPECT(at_ring_init(&ring, storage, 4) == AT_OK);
    EXPECT(at_ring_free(&ring) == 3);
    EXPECT(at_ring_used(&ring) == 0);

    n = at_ring_write(&ring, (const uint8_t *)"abcd", 4);
    EXPECT(n == 3);
    EXPECT(at_ring_free(&ring) == 0);
    EXPECT(at_ring_write(&ring, (const uint8_t *)"z", 1) == 0);

    n = at_ring_read(&ring, out, 2);
    EXPECT(n == 2);
    EXPECT(out[0] == 'a' && out[1] == 'b');
    EXPECT(at_ring_write(&ring, (const uint8_t *)"XY", 2) == 2);

    n = at_ring_read(&ring, out, sizeof(out));
    EXPECT(n == 3);
    EXPECT(memcmp(out, "cXY", 3) == 0);
    EXPECT(at_ring_used(&ring) == 0);
    EXPECT(at_ring_read(&ring, out, 1) == 0);
}
