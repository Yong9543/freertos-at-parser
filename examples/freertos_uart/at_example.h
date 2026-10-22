/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 *
 * Firmware sketch API. Call at_example_start() after the UART driver is up.
 */
#ifndef AT_EXAMPLE_H
#define AT_EXAMPLE_H

#include <stddef.h>
#include <stdint.h>

int at_example_start(void);
void at_example_stop(void);

/* Call from the UART receive ISR for the local command port. */
void at_example_server_isr(const uint8_t *data, size_t len);

/* Call from the UART receive ISR for the modem port. */
void at_example_modem_isr(const uint8_t *data, size_t len);

#endif
