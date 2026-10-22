/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#include "at_internal.h"

#include <string.h>

void at_line_parser_init(at_line_parser_t *parser, char *buf, size_t cap, char end_sign)
{
    parser->buf = buf;
    parser->cap = cap;
    parser->len = 0;
    parser->ended_by_sign = 0;
    parser->end_sign = end_sign;
    parser->overflowing = 0;
    parser->finished = 0;
    if (buf != NULL && cap > 0) {
        buf[0] = '\0';
    }
}

void at_line_parser_reset(at_line_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }
    parser->len = 0;
    parser->ended_by_sign = 0;
    parser->overflowing = 0;
    parser->finished = 0;
    if (parser->buf != NULL && parser->cap > 0) {
        parser->buf[0] = '\0';
    }
}

at_line_event_t at_line_parser_byte(at_line_parser_t *parser, unsigned char byte)
{
    int by_sign = 0;

    if (parser == NULL || parser->buf == NULL || parser->cap < 2) {
        return AT_LINE_OVERFLOW;
    }

    if (parser->finished) {
        parser->len = 0;
        parser->finished = 0;
        parser->ended_by_sign = 0;
        parser->overflowing = 0;
    }

    if (byte == '\0') {
        return AT_LINE_CONTINUE;
    }

    if (parser->end_sign != '\0' && byte == (unsigned char)parser->end_sign &&
        parser->end_sign != '\r' && parser->end_sign != '\n') {
        by_sign = 1;
        if (!parser->overflowing) {
            if (parser->len + 1 >= parser->cap) {
                parser->overflowing = 1;
            } else {
                parser->buf[parser->len++] = (char)byte;
            }
        }
    } else if (byte != '\r' && byte != '\n') {
        if (parser->overflowing) {
            return AT_LINE_CONTINUE;
        }
        if (parser->len + 1 >= parser->cap) {
            parser->overflowing = 1;
            return AT_LINE_CONTINUE;
        }
        parser->buf[parser->len++] = (char)byte;
        return AT_LINE_CONTINUE;
    }

    if (byte != '\r' && byte != '\n' && !by_sign) {
        return AT_LINE_CONTINUE;
    }

    parser->buf[parser->len] = '\0';
    parser->ended_by_sign = by_sign;
    parser->finished = 1;
    if (parser->overflowing) {
        parser->len = 0;
        parser->buf[0] = '\0';
        return AT_LINE_OVERFLOW;
    }
    if (parser->len == 0) {
        return AT_LINE_BLANK;
    }
    return AT_LINE_READY;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *trim_inplace(char *s)
{
    char *end;

    while (*s != '\0' && is_space(*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    end = s + strlen(s);
    while (end > s && is_space(end[-1])) {
        end--;
    }
    *end = '\0';
    return s;
}

static int ext_name_char(unsigned char c, int first)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        return 1;
    }
    if (!first && ((c >= '0' && c <= '9') || c == '_')) {
        return 1;
    }
    return 0;
}

static int basic_name_char(unsigned char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        return 1;
    }
    if ((c >= '0' && c <= '9') || c == '_' || c == '&' || c == '#' || c == '%') {
        return 1;
    }
    return 0;
}

static int copy_name(char *dst, size_t dst_sz, const char *src, size_t n)
{
    size_t i;

    if (n + 1 > dst_sz) {
        return AT_ERR_OVERFLOW;
    }
    for (i = 0; i < n; i++) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        dst[i] = c;
    }
    dst[n] = '\0';
    return AT_OK;
}

static int split_args(char *s, const char **argv, int max, int *argc_out)
{
    int argc = 0;

    *argc_out = 0;
    if (*s == '\0') {
        return AT_OK;
    }

    for (;;) {
        char *start;
        char *stop;
        char saved;

        if (argc >= max) {
            return AT_ERR_OVERFLOW;
        }
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s == '"') {
            s++;
            start = s;
            while (*s != '\0' && *s != '"') {
                s++;
            }
            if (*s != '"') {
                return AT_ERR_SYNTAX;
            }
            *s++ = '\0';
            argv[argc++] = start;
            while (*s == ' ' || *s == '\t') {
                s++;
            }
        } else {
            start = s;
            while (*s != '\0' && *s != ',') {
                s++;
            }
            stop = s;
            while (stop > start && (stop[-1] == ' ' || stop[-1] == '\t')) {
                stop--;
            }
            saved = *s;
            *stop = '\0';
            argv[argc++] = start;
            if (saved == '\0') {
                break;
            }
            s++;
            if (*s == '\0') {
                if (argc >= max) {
                    return AT_ERR_OVERFLOW;
                }
                argv[argc++] = s;
                break;
            }
            continue;
        }

        if (*s == '\0') {
            break;
        }
        if (*s != ',') {
            return AT_ERR_SYNTAX;
        }
        s++;
        if (*s == '\0') {
            if (argc >= max) {
                return AT_ERR_OVERFLOW;
            }
            argv[argc++] = s;
            break;
        }
    }

    *argc_out = argc;
    return AT_OK;
}

int at_parse_command(char *line, at_cmd_t *cmd)
{
    char *p;
    char *name;
    size_t name_len;
    int rc;

    if (line == NULL || cmd == NULL) {
        return AT_ERR_PARAM;
    }

    memset(cmd, 0, sizeof(*cmd));
    p = trim_inplace(line);
    if (p[0] == '\0' || p[1] == '\0') {
        return AT_ERR_SYNTAX;
    }
    if (!((p[0] == 'A' || p[0] == 'a') && (p[1] == 'T' || p[1] == 't'))) {
        return AT_ERR_SYNTAX;
    }
    p += 2;

    if (*p != '+') {
        name = p;
        name_len = 0;
        while (basic_name_char((unsigned char)p[name_len])) {
            name_len++;
        }
        if (p[name_len] != '\0') {
            return AT_ERR_SYNTAX;
        }
        cmd->type = AT_CMD_BASIC;
        return copy_name(cmd->name, sizeof(cmd->name), name, name_len);
    }

    p++;
    name = p;
    name_len = 0;
    if (!ext_name_char((unsigned char)*p, 1)) {
        return AT_ERR_SYNTAX;
    }
    while (ext_name_char((unsigned char)p[name_len], name_len == 0)) {
        name_len++;
    }
    rc = copy_name(cmd->name, sizeof(cmd->name), name, name_len);
    if (rc != AT_OK) {
        return rc;
    }
    p += name_len;

    if (*p == '\0') {
        cmd->type = AT_CMD_EXEC;
        return AT_OK;
    }
    if (*p == '?' && p[1] == '\0') {
        cmd->type = AT_CMD_READ;
        return AT_OK;
    }
    if (*p == '=' && p[1] == '?' && p[2] == '\0') {
        cmd->type = AT_CMD_TEST;
        return AT_OK;
    }
    if (*p != '=') {
        return AT_ERR_SYNTAX;
    }

    cmd->type = AT_CMD_SET;
    return split_args(p + 1, cmd->argv, AT_CMD_ARG_MAX, &cmd->argc);
}
