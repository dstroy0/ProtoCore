// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "mmgr/protoframe.h"
#include "network_drivers/transport/tcp/common.h"
#include "network_drivers/presentation/telnet/telnet.h"
#include "server/core/proto_handler.h"
#include "network_drivers/transport/tcp/tcp.h"
#include <stdint.h>
#include <string.h>

#include "rx_feed.h"
#include <unity.h>

#define IAC 255
#define WILL 251
#define WONT 252
#define DO 253
#define DONT 254
#define OPT_ECHO 1
#define OPT_SGA 3

static char g_last_cmd[TELNET_BUF_SIZE];
static int g_cmd_count;

static void cmd_cb(const char *line, uint8_t id)
{
    (void)id;
    strncpy(g_last_cmd, line, sizeof(g_last_cmd) - 1);
    g_last_cmd[sizeof(g_last_cmd) - 1] = '\0';
    g_cmd_count++;
}

void setUp()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].pcb = protocore_net_host_pcb();
        conn_pool[i].proto = PROTO_TELNET;
    }
    g_last_cmd[0] = '\0';
    g_cmd_count = 0;
    Telnet.on_command(cmd_cb);
    tcp_capture_reset();
}

void tearDown()
{
    Telnet.close(0);
    tcp_capture_disable();
}

void test_accept_negotiates_echo_and_sga()
{
    Telnet.accept(0);
    const uint8_t *out = (const uint8_t *)tcp_captured();

    const uint8_t neg[6] = {IAC, WILL, OPT_ECHO, IAC, WILL, OPT_SGA};
    const char greet[] = "PC Telnet ready\r\n> ";
    TEST_ASSERT_EQUAL_UINT(sizeof(neg) + sizeof(greet) - 1, tcp_captured_len());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(neg, out, sizeof(neg));
    TEST_ASSERT_EQUAL_MEMORY(greet, out + sizeof(neg), sizeof(greet) - 1);
    TEST_ASSERT_EQUAL_UINT8(1, Telnet.client_count());
}

void test_line_echoed_and_dispatched()
{
    Telnet.accept(0);
    tcp_capture_reset();
    push_str(0, "hello\n");
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_STRING("hello", g_last_cmd);
    TEST_ASSERT_EQUAL_INT(1, g_cmd_count);

    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "hello"));
}

void test_backspace_first_line()
{
    Telnet.accept(0);
    tcp_capture_reset();
    push_str(0, "ab\x08\n");
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_STRING("a", g_last_cmd);
}

void test_iac_will_gets_dont()
{
    Telnet.accept(0);
    tcp_capture_reset();
    const uint8_t seq[3] = {IAC, WILL, 24};
    push_bytes(0, seq, 3);
    Telnet.rx(0);
    const uint8_t *out = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT(3, tcp_captured_len());
    const uint8_t expect[3] = {IAC, DONT, 24};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 3);
}

void test_iac_do_unsupported_gets_wont()
{
    Telnet.accept(0);
    tcp_capture_reset();
    const uint8_t seq[3] = {IAC, DO, 31};
    push_bytes(0, seq, 3);
    Telnet.rx(0);
    const uint8_t *out = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT(3, tcp_captured_len());
    const uint8_t expect[3] = {IAC, WONT, 31};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 3);
}

void test_iac_do_echo_is_silent()
{
    Telnet.accept(0);
    tcp_capture_reset();
    const uint8_t seq[3] = {IAC, DO, OPT_ECHO};
    push_bytes(0, seq, 3);
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());
}

void test_iac_stripped_from_data()
{
    Telnet.accept(0);
    tcp_capture_reset();

    const uint8_t seq[] = {'a', IAC, 241 , 'b', '\n'};
    push_bytes(0, seq, sizeof(seq));
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_STRING("ab", g_last_cmd);
}

void test_print_broadcast()
{
    Telnet.accept(0);
    tcp_capture_reset();
    Telnet.println("hi there");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "hi there"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\r\n"));
}

void test_unknown_slot_is_noop()
{
    Telnet.accept(0);
    tcp_capture_reset();
    Telnet.rx(1);
    Telnet.close(1);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());
    TEST_ASSERT_EQUAL_UINT8(1, Telnet.client_count());
}

void test_cr_and_control_ignored()
{
    Telnet.accept(0);
    tcp_capture_reset();
    const uint8_t seq[] = {'a', '\r', 0x01, 'b', '\n'};
    push_bytes(0, seq, sizeof(seq));
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_STRING("ab", g_last_cmd);
}

void test_cr_nul_dispatches_line()
{
    Telnet.accept(0);
    tcp_capture_reset();
    g_last_cmd[0] = '\0';
    const uint8_t seq[] = {'h', 'i', '\r', 0x00};
    push_bytes(0, seq, sizeof(seq));
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_STRING("hi", g_last_cmd);
}

void test_iac_escaped_literal()
{
    Telnet.accept(0);
    tcp_capture_reset();
    const uint8_t seq[] = {'x', IAC, IAC, '\n'};
    push_bytes(0, seq, sizeof(seq));
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)g_last_cmd[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, (uint8_t)g_last_cmd[1]);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)g_last_cmd[2]);
}

void test_subnegotiation_consumed()
{
    Telnet.accept(0);
    tcp_capture_reset();
    const uint8_t seq[] = {IAC, 250 , 24, 'a', 'b', IAC, 240 , 'h', 'i', '\n'};
    push_bytes(0, seq, sizeof(seq));
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_STRING("hi", g_last_cmd);
}

void test_subnegotiation_bare_se_does_not_inject()
{
    Telnet.accept(0);
    tcp_capture_reset();
    const uint8_t seq[] = {IAC, 250 , 24, 'a', 240 , 'X', IAC, 240 , 'h', 'i', '\n'};
    push_bytes(0, seq, sizeof(seq));
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_STRING("hi", g_last_cmd);
}

void test_accept_no_capacity()
{
    for (uint8_t s = 0; s < MAX_TELNET_CONNS; s++)
    {
        Telnet.accept(s);
    }
    TEST_ASSERT_EQUAL_UINT8(MAX_TELNET_CONNS, Telnet.client_count());
    Telnet.accept(MAX_TELNET_CONNS);
    TEST_ASSERT_EQUAL_UINT8(MAX_TELNET_CONNS, Telnet.client_count());
    for (uint8_t s = 0; s < MAX_TELNET_CONNS; s++)
    {
        Telnet.close(s);
    }
}

void test_output_escaping_and_printf()
{
    Telnet.accept(0);
    tcp_capture_reset();
    Telnet.print("a\xff"
                 "b");
    const uint8_t *out = (const uint8_t *)tcp_captured();
    const uint8_t expect[] = {'a', 0xFF, 0xFF, 'b'};
    TEST_ASSERT_EQUAL_UINT(4, tcp_captured_len());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 4);

    tcp_capture_reset();
    static const protocore_field NEQ[] = {{PROTOCORE_FK_LIT, 0, 2, "n="}, PROTOCORE_U32, PROTOCORE_END};
    Telnet.frame(NEQ, (const protocore_fval[]){PROTOCORE_VU32(7u)}, 1);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "n=7"));
}

void test_inactive_conn_sends_nothing()
{
    Telnet.accept(0);
    conn_pool[0].pcb = NULL;
    tcp_capture_reset();
    Telnet.print("\xff");
    push_str(0, "x\n");
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());
    TEST_ASSERT_EQUAL_STRING("x", g_last_cmd);
}

void test_iac_wont_and_dont_are_silent()
{
    Telnet.accept(0);
    tcp_capture_reset();
    const uint8_t wont[3] = {IAC, WONT, 24};
    push_bytes(0, wont, 3);
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());

    const uint8_t dont[3] = {IAC, DONT, 24};
    push_bytes(0, dont, 3);
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());
}

void test_iac_do_sga_is_silent()
{
    Telnet.accept(0);
    tcp_capture_reset();
    const uint8_t seq[3] = {IAC, DO, OPT_SGA};
    push_bytes(0, seq, 3);
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());
}

void test_line_no_cmd_cb_is_noop()
{
    Telnet.accept(0);
    Telnet.on_command(NULL);
    tcp_capture_reset();
    push_str(0, "hello\n");
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_INT(0, g_cmd_count);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "> "));
}

void test_backspace_del_and_empty_noop()
{
    Telnet.accept(0);
    tcp_capture_reset();
    const uint8_t seq[] = {0x08, 'a', 0x7F, '\n'};
    push_bytes(0, seq, sizeof(seq));
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_STRING("", g_last_cmd);
}

void test_line_buffer_overflow_truncates()
{
    Telnet.accept(0);
    tcp_capture_reset();
    uint8_t seq[TELNET_BUF_SIZE + 10];
    for (int i = 0; i < TELNET_BUF_SIZE + 9; i++)
    {
        seq[i] = 'a';
    }
    seq[TELNET_BUF_SIZE + 9] = '\n';
    push_bytes(0, seq, sizeof(seq));
    Telnet.rx(0);
    TEST_ASSERT_EQUAL_UINT(TELNET_BUF_SIZE - 1, strlen(g_last_cmd));
}

void test_print_println_null_and_printf_empty()
{
    Telnet.accept(0);

    tcp_capture_reset();
    Telnet.print(NULL);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());

    tcp_capture_reset();
    Telnet.print("");
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());

    tcp_capture_reset();
    Telnet.println(NULL);
    TEST_ASSERT_EQUAL_STRING("\r\n", tcp_captured());

    tcp_capture_reset();
    static const protocore_field EMPTY[] = {PROTOCORE_END};
    Telnet.frame(EMPTY, NULL, 0);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());
}

void test_protocore_handler_accessor()
{
    const ProtoHandler *h = Telnet.proto_handler();
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_TRUE((void *)h->on_accept == (void *)Telnet.accept);
    TEST_ASSERT_TRUE((void *)h->on_data == (void *)Telnet.rx);
    TEST_ASSERT_TRUE((void *)h->on_close == (void *)Telnet.close);
    TEST_ASSERT_NULL(h->on_poll);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_accept_negotiates_echo_and_sga);
    RUN_TEST(test_line_echoed_and_dispatched);
    RUN_TEST(test_backspace_first_line);
    RUN_TEST(test_iac_will_gets_dont);
    RUN_TEST(test_iac_do_unsupported_gets_wont);
    RUN_TEST(test_iac_do_echo_is_silent);
    RUN_TEST(test_iac_stripped_from_data);
    RUN_TEST(test_print_broadcast);
    RUN_TEST(test_unknown_slot_is_noop);
    RUN_TEST(test_cr_and_control_ignored);
    RUN_TEST(test_cr_nul_dispatches_line);
    RUN_TEST(test_iac_escaped_literal);
    RUN_TEST(test_subnegotiation_consumed);
    RUN_TEST(test_subnegotiation_bare_se_does_not_inject);
    RUN_TEST(test_accept_no_capacity);
    RUN_TEST(test_output_escaping_and_printf);
    RUN_TEST(test_inactive_conn_sends_nothing);
    RUN_TEST(test_iac_wont_and_dont_are_silent);
    RUN_TEST(test_iac_do_sga_is_silent);
    RUN_TEST(test_line_no_cmd_cb_is_noop);
    RUN_TEST(test_backspace_del_and_empty_noop);
    RUN_TEST(test_line_buffer_overflow_truncates);
    RUN_TEST(test_print_println_null_and_printf_empty);
    RUN_TEST(test_protocore_handler_accessor);
    return UNITY_END();
}
