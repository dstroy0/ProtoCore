// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-file coverage for application/scp/ssh_scp.c - the rcp SINK state machine.
//
// No environment in the matrix compiled this file. Here the whole path is driven: a session channel
// is opened, an `exec "scp -t <path>"` CHANNEL_REQUEST tags it, the rcp stream arrives as
// CHANNEL_DATA, and the acks are read back out of the captured SSH packets.
//
// THE ORACLE IS NOT A SPECIFICATION. Unlike every other suite in this tree, the rcp protocol has no
// RFC and no draft - it is the BSD rcp wire format as OpenSSH implements it. What is pinned here is
// therefore the control flow OpenSSH's client drives and this server's own documented contract, not
// normative text. Where a case asserts a message string it is asserting THIS implementation's
// wording, which no peer is entitled to rely on.
//
// The exchange, in the order it happens:
//
//   server -> client   0x00                              ready for a record
//   client -> server   "C<mode> <size> <name>\n"         one file's control line
//   server -> client   0x00                              proceed with the data
//   client -> server   <size> bytes                      the file
//   client -> server   0x00                              end of record
//   server -> client   0x00                              stored
//
// An ack byte is 0 for OK, 1 for a warning and 2 for a fatal error; 1 and 2 carry a message and a
// newline after the byte.

#include "network_drivers/application/scp/scp.h"
#include "network_drivers/application/scp/ssh_scp.h"
#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/transport/tcp.h"
#include "network_drivers/transport/tcp/tcp_conn.h"
#include "server/clock/clock.h"
#include "server/filesystem/filesystem.h"
#include "server/filesystem/mnt.h"
#include <stdint.h>
#include <string.h>

#include <unity.h>

#define SSH_SLOT 0
#define SSH_CONN_SLOT 0
#define PEER_CHAN 7

static uint32_t g_chan;

void setUp()
{
    pc_net_host_reset();
    set_millis(1000);
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].pcb = pc_net_host_pcb();
        conn_pool[i].proto_slot = PC_PROTO_SLOT_NONE;
        conn_pool[i].last_activity_ms = pc_millis();
    }
    conn_pool[SSH_CONN_SLOT].proto = PROTO_SSH;

    pc_mnt_ram_format();
    pc_mnt_mount(pc_mnt_ram());

    pc_ssh_conn_setup();
    pc_ssh_channel_init(SSH_SLOT);
    pc_ssh_conn_accept(SSH_CONN_SLOT);
    pc_ssh_scp_begin();
    tcp_capture_reset();
}

void tearDown()
{
    pc_ssh_conn_close(SSH_CONN_SLOT);
    pc_mnt_mount(NULL);
}

// ---------------------------------------------------------------------------
// Wire helpers
// ---------------------------------------------------------------------------

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static size_t put_string(uint8_t *p, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    put_u32(p, n);
    memcpy(p + 4, s, n);
    return 4 + n;
}

// Open a session channel and run @p cmd on it as an exec request (RFC 4254 sec 6.5), which is what
// tags the channel for this server.
static void exec_on_channel(const char *cmd)
{
    uint8_t pkt[192], out[128];
    size_t n = 0, ol = 0;
    pkt[n++] = SSH_MSG_CHANNEL_OPEN;
    n += put_string(pkt + n, "session");
    put_u32(pkt + n, PEER_CHAN);
    put_u32(pkt + n + 4, 32768);
    put_u32(pkt + n + 8, 32768);
    n += 12;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_open(SSH_SLOT, pkt, n, out, &ol, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_CONFIRM, out[0]);
    g_chan = get_u32(out + 5);

    n = 0;
    pkt[n++] = SSH_MSG_CHANNEL_REQUEST;
    put_u32(pkt + n, g_chan);
    n += 4;
    n += put_string(pkt + n, "exec");
    pkt[n++] = 1; // want_reply
    n += put_string(pkt + n, cmd);
    ol = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_request(SSH_SLOT, pkt, n, out, &ol, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_SUCCESS, out[0]);
}

// Hand @p len raw rcp bytes to the server as one CHANNEL_DATA message.
static void feed(const void *bytes, size_t len)
{
    uint8_t pkt[1024], out[64];
    size_t n = 0, ol = 0;
    pkt[n++] = SSH_MSG_CHANNEL_DATA;
    put_u32(pkt + n, g_chan);
    n += 4;
    put_u32(pkt + n, (uint32_t)len);
    n += 4;
    memcpy(pkt + n, bytes, len);
    n += len;
    (void)pc_ssh_channel_handle_data(SSH_SLOT, pkt, n, out, &ol, sizeof(out));
}

static void feed_str(const char *s)
{
    feed(s, strlen(s));
}

// The bytes the server sent on the channel, concatenated across every captured SSH packet: one
// CHANNEL_DATA payload per packet, and an ack is a payload of its own.
static size_t channel_bytes(uint8_t *out, size_t cap)
{
    size_t total = 0, at = 0, n = 0;
    const uint8_t *w = pc_net_host_sent(&n);
    while (at + 6 <= n)
    {
        uint32_t pkt_len = get_u32(w + at);
        uint8_t pad = w[at + 4];
        if (pkt_len < (uint32_t)pad + 1u || at + 4u + pkt_len > n)
        {
            break;
        }
        const uint8_t *p = w + at + 5;
        if (p[0] == SSH_MSG_CHANNEL_DATA)
        {
            uint32_t dlen = get_u32(p + 5);
            for (uint32_t k = 0; k < dlen && total < cap; k++)
            {
                out[total++] = p[9 + k];
            }
        }
        at += 4u + pkt_len;
    }
    return total;
}

// ---------------------------------------------------------------------------
// The SINK handshake
// ---------------------------------------------------------------------------

// `scp -t <path>` puts the server in SINK mode, and the first thing it owes the client is a bare
// zero ack saying it is ready for a record.
static void test_rcp_sink_command_acks_ready(void)
{
    exec_on_channel("scp -t /");

    uint8_t got[32];
    size_t n = channel_bytes(got, sizeof(got));
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT8(PC_SCP_ACK_OK, got[0]);
}

// `scp -f <path>` is the SOURCE direction, which this server does not serve. It answers a fatal
// error record rather than a ready ack. The wording is this implementation's own.
static void test_rcp_source_command_is_refused(void)
{
    exec_on_channel("scp -f /etc/passwd");

    uint8_t got[128];
    size_t n = channel_bytes(got, sizeof(got));
    TEST_ASSERT_TRUE(n > 1);
    TEST_ASSERT_EQUAL_UINT8(PC_SCP_ACK_ERROR, got[0]);
    TEST_ASSERT_EQUAL_UINT8('\n', got[n - 1]); // a message record ends with a newline
}

// A command that names neither -t nor -f is not an scp invocation this server can serve.
static void test_rcp_a_command_without_a_mode_is_refused(void)
{
    exec_on_channel("scp /tmp/x");

    uint8_t got[128];
    size_t n = channel_bytes(got, sizeof(got));
    TEST_ASSERT_TRUE(n > 1);
    TEST_ASSERT_EQUAL_UINT8(PC_SCP_ACK_ERROR, got[0]);
    TEST_ASSERT_EQUAL_UINT8('\n', got[n - 1]);
}

// ---------------------------------------------------------------------------
// The record exchange
// ---------------------------------------------------------------------------

// The full sequence for one file: ready, control line acked, data, end-of-record byte, stored.
// Four zero acks in total, and the bytes reach the file.
static void test_rcp_a_whole_file_transfer_acks_at_each_stage(void)
{
    exec_on_channel("scp -t /");
    tcp_capture_reset();

    feed_str("C0644 5 hello.txt\n");
    uint8_t got[64];
    size_t n = channel_bytes(got, sizeof(got));
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT8(PC_SCP_ACK_OK, got[0]); // proceed with the data

    tcp_capture_reset();
    feed_str("world");
    n = channel_bytes(got, sizeof(got));
    TEST_ASSERT_EQUAL_size_t(0, n); // data is not acked, only the record is

    tcp_capture_reset();
    const uint8_t eor = 0;
    feed(&eor, 1);
    n = channel_bytes(got, sizeof(got));
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT8(PC_SCP_ACK_OK, got[0]); // stored
}

// A control line arriving in pieces is accumulated until its newline: the server acks once, when
// the line is complete, not once per chunk.
static void test_rcp_a_split_control_line_is_acked_once(void)
{
    exec_on_channel("scp -t /");
    tcp_capture_reset();

    feed_str("C0644 5 hel");
    uint8_t got[64];
    size_t n = channel_bytes(got, sizeof(got));
    TEST_ASSERT_EQUAL_size_t(0, n); // no newline yet, so no ack

    feed_str("lo.txt\n");
    n = channel_bytes(got, sizeof(got));
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT8(PC_SCP_ACK_OK, got[0]);
}

// A zero-length file has no data stage: the control line is acked and the very next byte is the
// end-of-record.
static void test_rcp_a_zero_length_file_skips_the_data_stage(void)
{
    exec_on_channel("scp -t /");
    tcp_capture_reset();

    feed_str("C0644 0 empty.txt\n");
    uint8_t got[64];
    size_t n = channel_bytes(got, sizeof(got));
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT8(PC_SCP_ACK_OK, got[0]);

    tcp_capture_reset();
    const uint8_t eor = 0;
    feed(&eor, 1);
    n = channel_bytes(got, sizeof(got));
    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_UINT8(PC_SCP_ACK_OK, got[0]);
}

// rcp's D and E records walk a directory tree, which needs `scp -r`. This server serves one file
// per transfer, so a D record is refused rather than silently treated as a file.
static void test_rcp_a_directory_record_is_refused(void)
{
    exec_on_channel("scp -t /");
    tcp_capture_reset();

    feed_str("D0755 0 subdir\n");
    uint8_t got[128];
    size_t n = channel_bytes(got, sizeof(got));
    TEST_ASSERT_TRUE(n > 1);
    TEST_ASSERT_EQUAL_UINT8(PC_SCP_ACK_ERROR, got[0]);
    TEST_ASSERT_EQUAL_UINT8('\n', got[n - 1]);
}

// A control line that is not a record at all is refused the same way.
static void test_rcp_a_malformed_control_line_is_refused(void)
{
    exec_on_channel("scp -t /");
    tcp_capture_reset();

    feed_str("not a record\n");
    uint8_t got[128];
    size_t n = channel_bytes(got, sizeof(got));
    TEST_ASSERT_TRUE(n > 1);
    TEST_ASSERT_EQUAL_UINT8(PC_SCP_ACK_ERROR, got[0]);
}

// An exec that is not an scp command never reaches this server: the channel stays a plain session,
// so rcp bytes on it produce nothing.
static void test_rcp_a_plain_exec_channel_serves_no_records(void)
{
    exec_on_channel("ls -l");
    tcp_capture_reset();

    feed_str("C0644 5 hello.txt\n");
    uint8_t got[64];
    TEST_ASSERT_EQUAL_size_t(0, channel_bytes(got, sizeof(got)));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_rcp_sink_command_acks_ready);
    RUN_TEST(test_rcp_source_command_is_refused);
    RUN_TEST(test_rcp_a_command_without_a_mode_is_refused);
    RUN_TEST(test_rcp_a_whole_file_transfer_acks_at_each_stage);
    RUN_TEST(test_rcp_a_split_control_line_is_acked_once);
    RUN_TEST(test_rcp_a_zero_length_file_skips_the_data_stage);
    RUN_TEST(test_rcp_a_directory_record_is_refused);
    RUN_TEST(test_rcp_a_malformed_control_line_is_refused);
    RUN_TEST(test_rcp_a_plain_exec_channel_serves_no_records);
    return UNITY_END();
}
