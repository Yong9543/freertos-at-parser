/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Yong9543
 */
#include "test_harness.h"

#include "at_parser.h"

#include <string.h>

static void feed_str(at_line_parser_t *p, const char *s, at_line_event_t *last)
{
    while (*s != '\0') {
        *last = at_line_parser_byte(p, (unsigned char)*s);
        s++;
    }
}

void test_line(void)
{
    char storage[32];
    at_line_parser_t parser;
    at_line_event_t ev = AT_LINE_CONTINUE;
    at_cmd_t cmd;
    char line[64];
    int rc;

    at_line_parser_init(&parser, storage, sizeof(storage), 0);
    feed_str(&parser, "AT+CMD\r", &ev);
    EXPECT(ev == AT_LINE_READY);
    EXPECT(strcmp(parser.buf, "AT+CMD") == 0);

    ev = at_line_parser_byte(&parser, '\n');
    EXPECT(ev == AT_LINE_BLANK);

    feed_str(&parser, "OK\n", &ev);
    EXPECT(ev == AT_LINE_READY);
    EXPECT(strcmp(parser.buf, "OK") == 0);

    feed_str(&parser, "O", &ev);
    EXPECT(ev == AT_LINE_CONTINUE);
    feed_str(&parser, "K\r", &ev);
    EXPECT(ev == AT_LINE_READY);
    EXPECT(strcmp(parser.buf, "OK") == 0);

    at_line_parser_init(&parser, storage, 8, 0);
    feed_str(&parser, "01234567\r", &ev);
    EXPECT(ev == AT_LINE_OVERFLOW);

    at_line_parser_init(&parser, storage, sizeof(storage), '>');
    feed_str(&parser, ">", &ev);
    EXPECT(ev == AT_LINE_READY);
    EXPECT(parser.ended_by_sign == 1);
    EXPECT(strcmp(parser.buf, ">") == 0);

    strcpy(line, "  at  ");
    rc = at_parse_command(line, &cmd);
    EXPECT(rc == AT_OK);
    EXPECT(cmd.type == AT_CMD_BASIC);
    EXPECT(cmd.name[0] == '\0');
    EXPECT(cmd.argc == 0);

    strcpy(line, "ATE0");
    rc = at_parse_command(line, &cmd);
    EXPECT(rc == AT_OK);
    EXPECT(cmd.type == AT_CMD_BASIC);
    EXPECT(strcmp(cmd.name, "E0") == 0);

    strcpy(line, "AT&F");
    rc = at_parse_command(line, &cmd);
    EXPECT(rc == AT_OK);
    EXPECT(strcmp(cmd.name, "&F") == 0);

    strcpy(line, "at+add");
    rc = at_parse_command(line, &cmd);
    EXPECT(rc == AT_OK);
    EXPECT(cmd.type == AT_CMD_EXEC);
    EXPECT(strcmp(cmd.name, "ADD") == 0);

    strcpy(line, "AT+GMR?");
    rc = at_parse_command(line, &cmd);
    EXPECT(rc == AT_OK);
    EXPECT(cmd.type == AT_CMD_READ);

    strcpy(line, "AT+GMR=?");
    rc = at_parse_command(line, &cmd);
    EXPECT(rc == AT_OK);
    EXPECT(cmd.type == AT_CMD_TEST);
    EXPECT(cmd.argc == 0);

    strcpy(line, "AT+ADD=2, 40");
    rc = at_parse_command(line, &cmd);
    EXPECT(rc == AT_OK);
    EXPECT(cmd.type == AT_CMD_SET);
    EXPECT(cmd.argc == 2);
    EXPECT(strcmp(cmd.argv[0], "2") == 0);
    EXPECT(strcmp(cmd.argv[1], "40") == 0);

    strcpy(line, "AT+E=\"\"");
    rc = at_parse_command(line, &cmd);
    EXPECT(rc == AT_OK);
    EXPECT(cmd.argc == 1);
    EXPECT(cmd.argv[0][0] == '\0');

    strcpy(line, "AT+ECHO=\"hi,there\",3");
    rc = at_parse_command(line, &cmd);
    EXPECT(rc == AT_OK);
    EXPECT(cmd.argc == 2);
    EXPECT(strcmp(cmd.argv[0], "hi,there") == 0);
    EXPECT(strcmp(cmd.argv[1], "3") == 0);

    strcpy(line, "AT+GAP=1,,x");
    rc = at_parse_command(line, &cmd);
    EXPECT(rc == AT_OK);
    EXPECT(cmd.argc == 3);
    EXPECT(cmd.argv[1][0] == '\0');
    EXPECT(strcmp(cmd.argv[2], "x") == 0);

    strcpy(line, "AT+Z=");
    rc = at_parse_command(line, &cmd);
    EXPECT(rc == AT_OK);
    EXPECT(cmd.type == AT_CMD_SET);
    EXPECT(cmd.argc == 0);

    strcpy(line, "AT+Z=1,");
    rc = at_parse_command(line, &cmd);
    EXPECT(rc == AT_OK);
    EXPECT(cmd.argc == 2);
    EXPECT(strcmp(cmd.argv[0], "1") == 0);
    EXPECT(cmd.argv[1][0] == '\0');

    strcpy(line, "AT+");
    EXPECT(at_parse_command(line, &cmd) == AT_ERR_SYNTAX);
    strcpy(line, "HELLO");
    EXPECT(at_parse_command(line, &cmd) == AT_ERR_SYNTAX);
    strcpy(line, "AT+1BAD");
    EXPECT(at_parse_command(line, &cmd) == AT_ERR_SYNTAX);
    strcpy(line, "AT+Q=\"unterminated");
    EXPECT(at_parse_command(line, &cmd) == AT_ERR_SYNTAX);

    strcpy(line, "AT+ABCDEFGHIJKLMNOPQRSTUVWXYZ012345");
    EXPECT(at_parse_command(line, &cmd) == AT_ERR_OVERFLOW);

    strcpy(line, "AT+M=0,1,2,3,4,5,6,7,8");
    EXPECT(at_parse_command(line, &cmd) == AT_ERR_OVERFLOW);
}
