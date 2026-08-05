// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Telnet server test: drives a PROTO_TELNET connection through the real
// conn_pool ring buffer and checks the IAC negotiation, line echo/editing, and
// command dispatch via the tcp_write capture mock.

#include "network_drivers/presentation/telnet/telnet.h"
#include "network_drivers/session/proto_handler.h" // ProtoHandler: full type needed to check pc_telnet_proto_handler()'s fields
#include "network_drivers/transport/tcp.h"
#include <stdint.h>

#include <unity.h>

#define IAC 255
#define WILL 251
#define WONT 252
#define DO 253
#define DONT 254
#define OPT_ECHO 1
#define OPT_SGA 3

static char g_last_cmd[TELNET_BUF_SIZE]; // matches TelnetConn::line[] so a full-length line round-trips uncut
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
        conn_pool[i].pcb = pc_net_host_pcb();
        conn_pool[i].proto = PROTO_TELNET;
    }
    g_last_cmd[0] = '\0';
    g_cmd_count = 0;
    pc_telnet_on_command(cmd_cb);
    tcp_capture_reset();
}

void tearDown()
{
    pc_telnet_close(0);
    tcp_capture_disable();
}

static void push_bytes(uint8_t slot, const uint8_t *d, size_t n)
{
    TcpConn *c = &conn_pool[slot];
    for (size_t i = 0; i < n; i++)
    {
        c->rx_buffer[c->rx_head] = d[i];
        c->rx_head = (c->rx_head + 1) % RX_BUF_SIZE;
    }
}
static void push_str(uint8_t slot, const char *s)
{
    push_bytes(slot, (const uint8_t *)s, strlen(s));
}

// ---------------------------------------------------------------------------

void test_accept_negotiates_echo_and_sga()
{
    pc_telnet_accept(0);
    const uint8_t *out = (const uint8_t *)tcp_captured();
    TEST_ASSERT_TRUE(tcp_captured_len() >= 6);
    // IAC WILL ECHO, IAC WILL SUPPRESS-GO-AHEAD
    const uint8_t expect[6] = {IAC, WILL, OPT_ECHO, IAC, WILL, OPT_SGA};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 6);
    TEST_ASSERT_EQUAL_UINT8(1, pc_telnet_client_count());
}

void test_line_echoed_and_dispatched()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    push_str(0, "hello\n");
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_STRING("hello", g_last_cmd);
    TEST_ASSERT_EQUAL_INT(1, g_cmd_count);
    // Each typed char was echoed back.
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "hello"));
}

void test_backspace_first_line()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    push_str(0, "ab\x08\n"); // 0x08 backspace removes 'b' -> "a"
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_STRING("a", g_last_cmd);
}

void test_iac_will_gets_dont()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    const uint8_t seq[3] = {IAC, WILL, 24}; // client WILL TERMINAL-TYPE
    push_bytes(0, seq, 3);
    pc_telnet_rx(0);
    const uint8_t *out = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT(3, tcp_captured_len());
    const uint8_t expect[3] = {IAC, DONT, 24};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 3);
}

void test_iac_do_unsupported_gets_wont()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    const uint8_t seq[3] = {IAC, DO, 31}; // client DO NAWS (window size) - unsupported
    push_bytes(0, seq, 3);
    pc_telnet_rx(0);
    const uint8_t *out = (const uint8_t *)tcp_captured();
    TEST_ASSERT_EQUAL_UINT(3, tcp_captured_len());
    const uint8_t expect[3] = {IAC, WONT, 31};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 3);
}

void test_iac_do_echo_is_silent()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    const uint8_t seq[3] = {IAC, DO, OPT_ECHO}; // already offered WILL ECHO -> no reply
    push_bytes(0, seq, 3);
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());
}

void test_iac_stripped_from_data()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    // "a" IAC-NOP "b" \n  -> line should be "ab" (IAC sequence consumed)
    const uint8_t seq[] = {'a', IAC, 241 /*NOP*/, 'b', '\n'};
    push_bytes(0, seq, sizeof(seq));
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_STRING("ab", g_last_cmd);
}

void test_print_broadcast()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    pc_telnet_println("hi there");
    const char *out = tcp_captured();
    TEST_ASSERT_NOT_NULL(strstr(out, "hi there"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\r\n"));
}

// pc_telnet_rx / pc_telnet_close on a slot with no Telnet connection are safe no-ops.
void test_unknown_slot_is_noop()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    pc_telnet_rx(1); // no TelnetConn for slot 1
    pc_telnet_close(1);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());
    TEST_ASSERT_EQUAL_UINT8(1, pc_telnet_client_count());
}

// A bare CR waits for its LF, and control characters are ignored in a line.
void test_cr_and_control_ignored()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    const uint8_t seq[] = {'a', '\r', 0x01, 'b', '\n'}; // CR held, 0x01 dropped
    push_bytes(0, seq, sizeof(seq));
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_STRING("ab", g_last_cmd);
}

// IAC IAC in the data stream is an escaped literal 0xFF added to the line.
void test_iac_escaped_literal()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    const uint8_t seq[] = {'x', IAC, IAC, '\n'};
    push_bytes(0, seq, sizeof(seq));
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_UINT8('x', (uint8_t)g_last_cmd[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, (uint8_t)g_last_cmd[1]);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)g_last_cmd[2]);
}

// A subnegotiation (IAC SB ... SE) is consumed; following data resumes normally.
void test_subnegotiation_consumed()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    const uint8_t seq[] = {IAC, 250 /*SB*/, 24, 'a', 'b', 240 /*SE*/, 'h', 'i', '\n'};
    push_bytes(0, seq, sizeof(seq));
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_STRING("hi", g_last_cmd);
}

// Past MAX_TELNET_CONNS the extra connection is dropped, not admitted.
void test_accept_no_capacity()
{
    for (uint8_t s = 0; s < MAX_TELNET_CONNS; s++)
    {
        pc_telnet_accept(s);
    }
    TEST_ASSERT_EQUAL_UINT8(MAX_TELNET_CONNS, pc_telnet_client_count());
    pc_telnet_accept(MAX_TELNET_CONNS); // one past capacity -> dropped
    TEST_ASSERT_EQUAL_UINT8(MAX_TELNET_CONNS, pc_telnet_client_count());
    for (uint8_t s = 0; s < MAX_TELNET_CONNS; s++)
    {
        pc_telnet_close(s);
    }
}

// Application output doubles a literal IAC (RFC 854); printf formats and broadcasts.
void test_output_escaping_and_printf()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    pc_telnet_print("a\xff"
                    "b");
    const uint8_t *out = (const uint8_t *)tcp_captured();
    const uint8_t expect[] = {'a', 0xFF, 0xFF, 'b'};
    TEST_ASSERT_EQUAL_UINT(4, tcp_captured_len());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, 4);

    tcp_capture_reset();
    static const pc_field NEQ[] = {{PC_FK_LIT, 0, 2, "n="}, PC_U32, PC_END};
    pc_telnet_frame(NEQ, (uint32_t)7);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "n=7"));
}

// An inactive connection (no pcb) swallows both raw and escaped sends.
void test_inactive_conn_sends_nothing()
{
    pc_telnet_accept(0);
    conn_pool[0].pcb = NULL; // connection went away under us
    tcp_capture_reset();
    pc_telnet_print("\xff"); // send_escaped bails on the inactive conn
    push_str(0, "x\n");      // handle_data -> raw_send bails too
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());
    TEST_ASSERT_EQUAL_STRING("x", g_last_cmd); // line still dispatched
}

// Client-sent WONT/DONT get no reply (only WILL/DO are answered by this server); both option
// bytes are consumed and the parser returns to TN_NORMAL either way.
void test_iac_wont_and_dont_are_silent()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    const uint8_t wont[3] = {IAC, WONT, 24};
    push_bytes(0, wont, 3);
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());

    const uint8_t dont[3] = {IAC, DONT, 24};
    push_bytes(0, dont, 3);
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());
}

// DO SGA (already offered, like DO ECHO) is silently ignored - the other half of the
// "don't answer an option we already offered" guard.
void test_iac_do_sga_is_silent()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    const uint8_t seq[3] = {IAC, DO, OPT_SGA};
    push_bytes(0, seq, 3);
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());
}

// With no command callback installed, a completed line still echoes and re-prompts but
// dispatches nothing (the null-callback half of the guard in handle_data).
void test_line_no_cmd_cb_is_noop()
{
    pc_telnet_accept(0);
    pc_telnet_on_command(NULL);
    tcp_capture_reset();
    push_str(0, "hello\n");
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_INT(0, g_cmd_count);
    TEST_ASSERT_NOT_NULL(strstr(tcp_captured(), "> "));
}

// Backspace/DEL on an empty line is a no-op (len stays 0, no underflow); DEL (0x7F) removes a
// character exactly like backspace (0x08) does.
void test_backspace_del_and_empty_noop()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    const uint8_t seq[] = {0x08, 'a', 0x7F, '\n'}; // BS on empty line (no-op), then 'a', then DEL removes it
    push_bytes(0, seq, sizeof(seq));
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_STRING("", g_last_cmd);
}

// Once the line buffer is full (TELNET_BUF_SIZE-1 bytes), further characters before the
// newline are dropped rather than overflowing t->line[].
void test_line_buffer_overflow_truncates()
{
    pc_telnet_accept(0);
    tcp_capture_reset();
    uint8_t seq[TELNET_BUF_SIZE + 10];
    for (int i = 0; i < TELNET_BUF_SIZE + 9; i++)
    {
        seq[i] = 'a';
    }
    seq[TELNET_BUF_SIZE + 9] = '\n';
    push_bytes(0, seq, sizeof(seq));
    pc_telnet_rx(0);
    TEST_ASSERT_EQUAL_UINT(TELNET_BUF_SIZE - 1, strlen(g_last_cmd));
}

// pc_telnet_print/println swallow a null pointer instead of dereferencing it, and a printf
// whose format yields zero output broadcasts nothing. Also exercises send_escaped's n==0
// guard via an empty (non-null) string on an active connection.
void test_print_println_null_and_printf_empty()
{
    pc_telnet_accept(0);

    tcp_capture_reset();
    pc_telnet_print(NULL);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());

    tcp_capture_reset();
    pc_telnet_print(""); // strnlen("") == 0 -> send_escaped's n==0 guard while the conn is active
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());

    tcp_capture_reset();
    pc_telnet_println(NULL);
    TEST_ASSERT_EQUAL_STRING("\r\n", tcp_captured()); // the unconditional CRLF still goes out

    tcp_capture_reset();
    static const pc_field EMPTY[] = {PC_END};
    pc_telnet_frame(EMPTY);
    TEST_ASSERT_EQUAL_UINT(0, tcp_captured_len());
}

// The Layer 5 ProtoHandler accessor exposes the installed dispatch table.
void test_proto_handler_accessor()
{
    const ProtoHandler *h = pc_telnet_proto_handler();
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_TRUE((void *)h->on_accept == (void *)pc_telnet_accept);
    TEST_ASSERT_TRUE((void *)h->on_data == (void *)pc_telnet_rx);
    TEST_ASSERT_TRUE((void *)h->on_close == (void *)pc_telnet_close);
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
    RUN_TEST(test_iac_escaped_literal);
    RUN_TEST(test_subnegotiation_consumed);
    RUN_TEST(test_accept_no_capacity);
    RUN_TEST(test_output_escaping_and_printf);
    RUN_TEST(test_inactive_conn_sends_nothing);
    RUN_TEST(test_iac_wont_and_dont_are_silent);
    RUN_TEST(test_iac_do_sga_is_silent);
    RUN_TEST(test_line_no_cmd_cb_is_noop);
    RUN_TEST(test_backspace_del_and_empty_noop);
    RUN_TEST(test_line_buffer_overflow_truncates);
    RUN_TEST(test_print_println_null_and_printf_empty);
    RUN_TEST(test_proto_handler_accessor);
    return UNITY_END();
}
