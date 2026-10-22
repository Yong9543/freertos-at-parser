# freertos-at-parser

A small C11 library for AT-style command exchanges on a UART. It can act as the device that answers commands, as the client that sends a command and waits for `OK` / `ERROR`, or as both. The parser itself does not include FreeRTOS. A thin adapter adds an RX ring, a TX queue, and the semaphores a client needs.

The core does not allocate. Line buffers, response buffers, and the command table belong to the caller.

## Features

- Line assembly for `CR`, `LF`, and `CR+LF`, plus an optional end character such as `>`
- Command forms: `AT`, `ATE0`, `AT&F`, `AT+NAME`, `AT+NAME?`, `AT+NAME=?`, `AT+NAME=arg1,arg2`
- Quoted set-arguments (`AT+ECHO="a,b"`)
- Client: send, collect response lines, wait for `OK` / `ERROR` / `FAIL` / `+CME ERROR` / `+CMS ERROR` or a timeout
- URC table (prefix and optional suffix) so unsolicited lines stay out of the response buffer
- CLI mode that forwards raw bytes
- Server: register handlers and reply with `OK`, `ERROR`, `+CME ERROR`, or a `+CMD:` line
- Single-producer byte ring for a UART ISR
- FreeRTOS RX task, optional TX queue, command mutex, and response semaphore
- Host build and unit tests with no RTOS

## Layout

```
include/at_parser.h          portable API
include/at_freertos.h        FreeRTOS adapter
src/                         library sources
examples/host_loopback/      client and server on one process
examples/freertos_uart/      UART server plus a modem client
tests/                       parser, client, server, ring, adapter link
docs/api.md                  function reference
docs/thread-safety.md       tasks, ISR, and locks
```

## Build and test

Host toolchain: GCC or Clang, C11.

```sh
make check
```

That builds `build/at_tests`, `build/host_loopback`, and a FreeRTOS adapter link test, then runs all three. Sanitizers are on by default. Clear `SAN` if the toolchain has no address sanitizer:

```sh
make check SAN=
```

CMake works for the portable library and the host programs:

```sh
cmake -S . -B build-cmake
cmake --build build-cmake
ctest --test-dir build-cmake
```

The FreeRTOS adapter is a separate translation unit (`src/at_freertos.c`). Point CMake at the kernel headers when you want that static library:

```sh
cmake -S . -B build-cmake -DAT_PARSER_FREERTOS=ON -DFREERTOS_DIR=/path/to/freertos/include
```

## Quick start

Register a few commands and answer them. Returning `AT_OK` makes the server append `OK` after any text you already wrote.

```c
static int on_add(at_server_t *server, const at_cmd_t *cmd, void *user)
{
    (void)user;
    if (cmd->type != AT_CMD_SET || cmd->argc != 2) {
        return AT_ERR_PARAM;
    }
    return at_server_reply_line(server, "+ADD: %d",
                                atoi(cmd->argv[0]) + atoi(cmd->argv[1]));
}

at_cmd_entry_t table[8];
at_server_config_t cfg;
/* cfg.line_buf, cfg.write, cfg.table, cfg.table_cap, ... */
at_server_init(&server, &cfg);
at_server_register(&server, "ADD", on_add, NULL);

/* Bytes from the UART, from a task, not from an ISR. */
at_server_feed(&server, data, len);
```

`AT+ADD=2,40` then produces:

```
+ADD: 42
OK
```

Bare `AT` and `ATE0` / `ATE1` are built in. Unknown names get `ERROR`.

`examples/host_loopback/main.c` wires a server and a client back to back and checks `AT`, `AT+ADD`, `AT+ECHO`, and `AT+GMR`.

## Client: send and wait

```c
char storage[128];
at_response_t resp;
at_resp_init(&resp, storage, sizeof storage, 0); /* 0 = until OK or ERROR */

at_resp_status_t st = at_client_exec(&client, &resp, 1000, "AT+GMR");
if (st == AT_RESP_OK) {
    const char *line = at_resp_find(&resp, "+GMR:");
    (void)line;
}
```

`at_client_exec` adds `CR+LF` when you did not. With a `wait` callback (the FreeRTOS adapter installs one) the call blocks up to `timeout_ms`. Without one, a synchronous write hook can feed the reply before `at_client_exec` returns, which is what the host loopback does. If the reply is still outstanding the call returns `AT_RESP_PENDING`; keep calling `at_client_feed`, or call `at_client_abort` on your own timeout.

URCs are matched before a line is stored:

```c
static void on_boot(void *user, const char *line, size_t len)
{
    (void)user;
    (void)line;
    (void)len;
}

static const at_urc_t urcs[] = {
    { "+BOOT", NULL, on_boot, NULL },
};
```

Do not register `OK` or `ERROR` as URCs. Those tokens complete an armed command.

`at_client_enter_cli()` switches the feed path to a raw byte callback until `at_client_exit_cli()`.

## FreeRTOS

`examples/freertos_uart/main.c` is the sketch:

1. The local UART feeds `at_server_feed` from an RX task. Handlers for `AT+VERS`, `AT+ECHO`, and `AT+ADD` are registered at startup.
2. A second UART talks to a modem. `at_rtos_start` installs the client lock, the response semaphore, and a TX queue, then a task calls `at_client_exec(&modem, &resp, 1000, "AT+GMR")`.

From the receive ISR, only copy bytes and wake the RX task:

```c
void UART_IRQHandler(void)
{
    uint8_t byte = board_uart_read_byte();
    BaseType_t woken = pdFALSE;
    at_rtos_rx_from_isr(&rt, &byte, 1, &woken);
    portYIELD_FROM_ISR(woken);
}
```

Zero `at_rtos_t` before `at_rtos_start`. `uart_write` runs on the TX task and must copy or finish the transfer before it returns. Stack sizes are in words.

Details and the rules for URC callbacks are in [docs/thread-safety.md](docs/thread-safety.md). The function list is in [docs/api.md](docs/api.md).

## Porting

Implement one write hook:

```c
int uart_write(void *ctx, const uint8_t *data, size_t len);
```

Return the number of bytes accepted, or a negative value. Short writes are retried. A zero return is an I/O error.

RX is push-based. A DMA or IRQ handler writes a ring (`at_ring_write` or `at_rtos_rx_from_isr`). One task drains it and calls `at_client_feed` or `at_server_feed`.

On FreeRTOS the adapter's `at_sync_ops` is enough for `at_client_exec`. On another RTOS, fill `at_sync_ops` with your mutex and binary semaphore. `wait` returns `0` when `signal` has run, or `AT_ERR_TIMEOUT`. A timeout of `0` polls.

## Limits

- One in-flight command per client
- No semicolon chaining of several commands on one line
- Quoted arguments do not treat backslash as an escape
- The ring is single-producer, single-consumer, and leaves one slot empty
- The RX task notification belongs to the adapter

## License

Apache License 2.0. See [LICENSE](LICENSE).

Copyright 2026 Yong9543
