/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 *
 * Links the FreeRTOS adapter against the host stub and exercises the public
 * start / ISR / stop path. It does not run a scheduler.
 */
#include "at_example.h"
#include "at_parser.h"

#include <stdio.h>

int main(void)
{
    const uint8_t bytes[] = "AT+VERS?\r";

    if (at_example_start() != AT_OK) {
        fprintf(stderr, "example start failed\n");
        return 1;
    }
    at_example_server_isr(bytes, sizeof(bytes) - 1);
    at_example_modem_isr((const uint8_t *)"OK\r\n", 4);
    at_example_stop();
    printf("freertos adapter linked\n");
    return 0;
}
