# API overview

Include `at_parser.h` from portable code. Include `at_freertos.h` only in firmware that builds against FreeRTOS.

Version: `AT_PARSER_VERSION_STRING` (`0.1.0`).

## Status codes

`at_status_t` is the library result (`AT_OK`, `AT_REPLIED`, and `AT_ERR_*`).

`at_resp_status_t` is the result of one modem exchange:

| Value | Meaning |
| --- | --- |
| `AT_RESP_OK` | Final line was `OK`, a fixed line count was reached, or the end sign arrived |
| `AT_RESP_ERROR` | Final line was `ERROR`, `+CME ERROR`, or `+CMS ERROR` |
| `AT_RESP_FAIL` | Final line was `FAIL` |
| `AT_RESP_TIMEOUT` | The wait elapsed, or `at_client_abort` ran |
| `AT_RESP_OVERFLOW` | The line or the response buffer ran out of room |
| `AT_RESP_PENDING` | Command is armed and the caller must keep feeding bytes |
| `AT_RESP_IDLE` | No exchange has completed yet (after init or reset) |
| `AT_RESP_BUSY` | A command is already in flight, or the client is in CLI mode |
| `AT_RESP_IO` | The write hook failed |
| `AT_RESP_PARAM` | A required pointer or buffer was missing |

`at_status_str()` and `at_resp_status_str()` return short English labels.

## Line parser

```c
void at_line_parser_init(at_line_parser_t *parser, char *buf, size_t cap, char end_sign);
at_line_event_t at_line_parser_byte(at_line_parser_t *parser, unsigned char byte);
```

`CR`, `LF`, and `CR+LF` all end a line. A bare extra `CR` or `LF` is `AT_LINE_BLANK`. After `AT_LINE_READY`, `parser->buf` and `parser->len` are valid until the next byte. `end_sign` (for example `'>'`) is an extra terminator and is kept in the line. It must not be `CR` or `LF`.

```c
int at_parse_command(char *line, at_cmd_t *cmd);
```

Accepts `AT`, `ATE0`, `AT&F`, `AT+NAME`, `AT+NAME?`, `AT+NAME=?`, and `AT+NAME=args`. Names are stored in upper case. Set arguments are split on commas. Double quotes group a field and are removed; backslash escapes are not processed. The function writes NULs into `line`. `cmd->argv` points into that buffer.

## Response object

Caller-owned. No heap use.

```c
void at_resp_init(at_response_t *resp, char *buf, size_t buf_size, size_t line_num);
const char *at_resp_line(const at_response_t *resp, size_t index); /* 0-based */
const char *at_resp_find(const at_response_t *resp, const char *keyword);
int at_resp_scanf(const at_response_t *resp, size_t index, const char *fmt, ...);
```

`line_num == 0` collects until a final result. Any other value stops after that many stored lines and reports `AT_RESP_OK` even if `OK` has not arrived yet. Final result lines are stored too. Lines are packed as consecutive NUL-terminated strings.

## Client

```c
int at_client_init(at_client_t *client, const at_client_config_t *cfg);
at_resp_status_t at_client_exec(at_client_t *client, at_response_t *resp,
                                uint32_t timeout_ms, const char *fmt, ...);
int at_client_feed(at_client_t *client, const uint8_t *data, size_t len);
void at_client_abort(at_client_t *client);
int at_client_enter_cli(at_client_t *client, at_cli_fn fn, void *user);
int at_client_exit_cli(at_client_t *client);
```

`at_client_exec` formats the command, appends `CR+LF` when the text does not already end with `CR` or `LF`, and writes it through `at_io_ops.write`. The formatted command must fit in `AT_TX_LINE_MAX` bytes including the line ending.

While a response is armed, a line is classified in this order:

1. `OK`, `ERROR`, `FAIL`, `+CME ERROR`, `+CMS ERROR` (case-insensitive) complete the exchange.
2. A URC prefix/suffix match is delivered and is not stored.
3. Anything else is stored. The end sign or `line_num` can complete the exchange.

Unsolicited lines that arrive with no command armed go to the URC table, then to `on_line`.

CLI mode forwards raw bytes and rejects `at_client_exec` with `AT_RESP_BUSY`.

## Server

```c
int at_server_init(at_server_t *server, const at_server_config_t *cfg);
int at_server_register(at_server_t *server, const char *name, at_cmd_fn handler, void *user);
int at_server_feed(at_server_t *server, const uint8_t *data, size_t len);
int at_server_reply_line(at_server_t *server, const char *fmt, ...);
int at_server_reply_ok(at_server_t *server);
int at_server_reply_error(at_server_t *server);
int at_server_reply_cme(at_server_t *server, int code);
int at_server_reply_cms(at_server_t *server, int code);
int at_server_set_echo(at_server_t *server, int enable);
```

The command table is caller-owned. `name` is the command without `AT+` (`"ADD"` matches `AT+ADD`). Comparison ignores case. Names must outlive the server.

A handler runs with `cmd->argv` valid only for that call. Return `AT_OK` to let the server append `OK` after any `at_server_reply_line` text. Return `AT_REPLIED` after `at_server_reply_ok`, `at_server_reply_error`, `at_server_reply_cme`, or `at_server_reply_cms`. `at_server_reply_cme` writes `+CME ERROR: <code>` and `at_server_reply_cms` writes `+CMS ERROR: <code>`; both mark the command finished, so the server does not append `OK`. Any other negative code produces `ERROR`, unless the handler already sent a final line or the write hook failed.

Built in, when not overridden:

- bare `AT` replies `OK`
- `ATE0` / `ATE1` toggle echo, then reply `OK`

Unknown commands and syntax errors reply `ERROR`.

Replies are framed as `\r\n` + text + `\r\n`.

## Ring buffer

```c
int at_ring_init(at_ring_t *ring, uint8_t *storage, size_t cap);
size_t at_ring_write(at_ring_t *ring, const uint8_t *data, size_t len);
size_t at_ring_read(at_ring_t *ring, uint8_t *data, size_t len);
size_t at_ring_used(const at_ring_t *ring);
size_t at_ring_free(const at_ring_t *ring);
```

`at_ring_used` and `at_ring_free` report stored bytes and remaining free slots. One slot stays empty, so the usable capacity is `cap - 1`. One producer and one consumer. See `docs/thread-safety.md`.

## FreeRTOS adapter

```c
int at_rtos_start(at_rtos_t *rt, const at_rtos_config_t *cfg);
void at_rtos_stop(at_rtos_t *rt);
int at_rtos_rx_from_isr(at_rtos_t *rt, const uint8_t *data, size_t len, BaseType_t *woken);
```

`rt` must be zeroed before the first start. `rx_storage` is the ring. `on_rx` runs on the RX task; leave it NULL to feed `cfg->client`.

When `cfg->client` is set, the adapter installs a mutex, a response semaphore, and a TX queue. Each queued chunk is a copy, so the caller's buffer does not have to live until the UART finishes. `uart_write` still runs on the TX task and should accept the whole chunk before returning. `tx_timeout_ms` bounds both the queue wait and the wait for that copy to be handed to `uart_write`. A timeout means the exchange failed; a copy already accepted by the queue may still go out the UART.

Stack sizes are in words, matching `xTaskCreate`.
