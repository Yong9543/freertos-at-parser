/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 *
 * Public API for freertos-at-parser.
 * The core does not include FreeRTOS headers. See at_freertos.h for the
 * task, queue, and ISR adapter.
 */
#ifndef AT_PARSER_H
#define AT_PARSER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AT_PARSER_VERSION_MAJOR 0
#define AT_PARSER_VERSION_MINOR 1
#define AT_PARSER_VERSION_PATCH 0
#define AT_PARSER_VERSION_STRING "0.1.0"

#ifndef AT_CMD_NAME_MAX
#define AT_CMD_NAME_MAX 32
#endif

#ifndef AT_CMD_ARG_MAX
#define AT_CMD_ARG_MAX 8
#endif

#ifndef AT_TX_LINE_MAX
#define AT_TX_LINE_MAX 192
#endif

/* Library status. Zero is success. Handler callbacks use the same codes. */
typedef enum at_status {
    AT_OK = 0,
    AT_REPLIED = 1,          /* Handler already sent the final result line. */
    AT_ERR_PARAM = -1,
    AT_ERR_SYNTAX = -2,
    AT_ERR_OVERFLOW = -3,
    AT_ERR_NOMEM = -4,
    AT_ERR_BUSY = -5,
    AT_ERR_IO = -6,
    AT_ERR_TIMEOUT = -7,
    AT_ERR_STATE = -8
} at_status_t;

/* Result of one command/response exchange. */
typedef enum at_resp_status {
    AT_RESP_OK = 0,
    AT_RESP_ERROR = -1,      /* Final line was ERROR, +CME ERROR, or +CMS ERROR. */
    AT_RESP_TIMEOUT = -2,
    AT_RESP_OVERFLOW = -3,   /* Response buffer could not hold the next line. */
    AT_RESP_FAIL = -4,       /* Final line was FAIL. */
    AT_RESP_BUSY = -5,
    AT_RESP_PARAM = -6,
    AT_RESP_IO = -7,
    AT_RESP_PENDING = 1,
    AT_RESP_IDLE = 2
} at_resp_status_t;

typedef enum at_cmd_type {
    AT_CMD_BASIC = 0,        /* AT, ATE0, ATI, AT&F */
    AT_CMD_EXEC,             /* AT+NAME */
    AT_CMD_READ,             /* AT+NAME? */
    AT_CMD_TEST,             /* AT+NAME=? */
    AT_CMD_SET               /* AT+NAME=... */
} at_cmd_type_t;

typedef enum at_line_event {
    AT_LINE_CONTINUE = 0,
    AT_LINE_READY = 1,
    AT_LINE_BLANK = 2,
    AT_LINE_OVERFLOW = 3
} at_line_event_t;

typedef struct at_cmd {
    at_cmd_type_t type;
    char name[AT_CMD_NAME_MAX]; /* Upper case. Empty for a bare "AT". */
    int argc;
    const char *argv[AT_CMD_ARG_MAX]; /* Valid only until the line buffer is reused. */
} at_cmd_t;

/*
 * Byte-at-a-time line assembler.
 * After AT_LINE_READY, read buf, len, and ended_by_sign before the next byte.
 * CR, LF, and CR+LF all terminate a line. A blank line (extra CR/LF) is reported
 * as AT_LINE_BLANK and is not a command.
 */
typedef struct at_line_parser {
    char *buf;
    size_t cap;
    size_t len;
    int ended_by_sign;
    char end_sign; /* Optional extra terminator such as '>'. 0 disables it. */
    int overflowing;
    int finished;
} at_line_parser_t;

typedef struct at_response {
    char *buf;
    size_t buf_size;
    size_t buf_len;
    size_t line_num;   /* 0: collect until OK / ERROR / FAIL. N: stop after N lines. */
    size_t line_count;
    int status;        /* at_resp_status_t */
} at_response_t;

typedef struct at_client at_client_t;

typedef void (*at_urc_fn)(void *user, const char *line, size_t len);
typedef void (*at_line_fn)(void *user, const char *line, size_t len);
typedef void (*at_cli_fn)(void *user, const uint8_t *data, size_t len);

typedef struct at_urc {
    const char *prefix; /* Matched at the start of a line. NULL skips the prefix test. */
    const char *suffix; /* Matched at the end of a line. NULL skips the suffix test. */
    at_urc_fn handler;
    void *user;
} at_urc_t;

/* UART write hook. Return the number of bytes accepted, or a negative error. */
typedef int (*at_write_fn)(void *ctx, const uint8_t *data, size_t len);

typedef struct at_io_ops {
    at_write_fn write;
    void *ctx;
} at_io_ops_t;

/*
 * Optional synchronization used when a command is sent from one thread and
 * received on another. lock/unlock guard a single in-flight command.
 * wait() returns 0 when signal() has been called, or AT_ERR_TIMEOUT.
 * A timeout of 0 polls. The lock must not be recursive: response parsing
 * never takes it. Do not call at_client_exec from a URC or line callback.
 */
typedef struct at_sync_ops {
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
    int (*wait)(void *ctx, uint32_t timeout_ms);
    void (*signal)(void *ctx);
    void *ctx;
} at_sync_ops_t;

typedef struct at_client_config {
    char *line_buf;
    size_t line_buf_size; /* At least 2. Typical UART lines fit in 256 bytes. */
    const at_io_ops_t *io;
    const at_sync_ops_t *sync;
    const at_urc_t *urcs;
    size_t urc_count;
    at_line_fn on_line; /* Lines that arrive while no command is waiting. */
    void *on_line_user;
    char end_sign;
} at_client_config_t;

struct at_client {
    at_line_parser_t line;
    const at_io_ops_t *io;
    const at_sync_ops_t *sync;
    const at_urc_t *urcs;
    size_t urc_count;
    at_line_fn on_line;
    void *on_line_user;
    at_cli_fn cli_fn;
    void *cli_user;
    at_response_t *resp;
    volatile int resp_status;
    volatile int waiting;
    int cli;
    int in_feed;
    int ready;
};

typedef struct at_server at_server_t;

typedef int (*at_cmd_fn)(at_server_t *server, const at_cmd_t *cmd, void *user);

typedef struct at_cmd_entry {
    const char *name; /* Compared without case. "ADD" matches AT+ADD. */
    at_cmd_fn handler;
    void *user;
} at_cmd_entry_t;

typedef struct at_server_config {
    char *line_buf;
    size_t line_buf_size;
    at_write_fn write;
    void *write_ctx;
    at_cmd_entry_t *table; /* Caller-owned storage. May be NULL when cap is 0. */
    size_t table_count;    /* Entries already filled in. */
    size_t table_cap;
    int echo;
} at_server_config_t;

struct at_server {
    at_line_parser_t line;
    at_write_fn write;
    void *write_ctx;
    at_cmd_entry_t *table;
    size_t table_count;
    size_t table_cap;
    int echo;
    int final_sent;
    int in_feed;
    int ready;
};

typedef struct at_ring {
    uint8_t *buf;
    size_t cap;             /* One slot is left empty, so usable space is cap-1. */
    volatile size_t head;   /* Producer index. */
    volatile size_t tail;   /* Consumer index. */
} at_ring_t;

const char *at_status_str(int status);
const char *at_resp_status_str(int status);

void at_line_parser_init(at_line_parser_t *parser, char *buf, size_t cap, char end_sign);
void at_line_parser_reset(at_line_parser_t *parser);
at_line_event_t at_line_parser_byte(at_line_parser_t *parser, unsigned char byte);

/* Split one AT command line. The line buffer is modified. */
int at_parse_command(char *line, at_cmd_t *cmd);

void at_resp_init(at_response_t *resp, char *buf, size_t buf_size, size_t line_num);
void at_resp_reset(at_response_t *resp);
const char *at_resp_line(const at_response_t *resp, size_t index);
const char *at_resp_find(const at_response_t *resp, const char *keyword);
int at_resp_scanf(const at_response_t *resp, size_t index, const char *fmt, ...);

int at_client_init(at_client_t *client, const at_client_config_t *cfg);
int at_client_set_io(at_client_t *client, const at_io_ops_t *io);
int at_client_set_sync(at_client_t *client, const at_sync_ops_t *sync);
int at_client_set_urcs(at_client_t *client, const at_urc_t *urcs, size_t count);
int at_client_set_end_sign(at_client_t *client, char end_sign);
int at_client_enter_cli(at_client_t *client, at_cli_fn fn, void *user);
int at_client_exit_cli(at_client_t *client);

/*
 * Send one command and, when sync.wait is set, block until the modem finishes
 * or timeout_ms elapses. The formatted command gets a trailing CR+LF if it
 * does not already end with CR or LF. resp may be NULL to send without waiting.
 * With no wait callback the call returns AT_RESP_PENDING and stays armed until
 * at_client_feed completes it or at_client_abort times it out.
 */
at_resp_status_t at_client_exec(at_client_t *client, at_response_t *resp,
                                uint32_t timeout_ms, const char *fmt, ...);
int at_client_feed(at_client_t *client, const uint8_t *data, size_t len);
void at_client_abort(at_client_t *client);
int at_client_is_waiting(const at_client_t *client);
at_resp_status_t at_client_last_status(const at_client_t *client);

int at_server_init(at_server_t *server, const at_server_config_t *cfg);
int at_server_register(at_server_t *server, const char *name, at_cmd_fn handler, void *user);
int at_server_set_echo(at_server_t *server, int enable);
int at_server_feed(at_server_t *server, const uint8_t *data, size_t len);

/* Reply helpers. reply_line does not finish the command; OK/ERROR/CME/CMS do. */
int at_server_reply_line(at_server_t *server, const char *fmt, ...);
int at_server_reply_ok(at_server_t *server);
int at_server_reply_error(at_server_t *server);
int at_server_reply_cme(at_server_t *server, int code);
int at_server_reply_cms(at_server_t *server, int code);

/*
 * Single-producer, single-consumer byte ring.
 * On a single-core MCU the usual pairing is an ISR producer and one consumer
 * task. Indexes are volatile and published with a compiler barrier. A multi-core
 * port must add a real memory barrier around the index updates.
 */
int at_ring_init(at_ring_t *ring, uint8_t *storage, size_t cap);
size_t at_ring_write(at_ring_t *ring, const uint8_t *data, size_t len);
size_t at_ring_read(at_ring_t *ring, uint8_t *data, size_t len);
size_t at_ring_used(const at_ring_t *ring);
size_t at_ring_free(const at_ring_t *ring);

#ifdef __cplusplus
}
#endif

#endif /* AT_PARSER_H */
