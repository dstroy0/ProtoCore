// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-file coverage for application/sftp/ssh_sftp.c - the SFTP v3 server subsystem.
//
// No environment in the matrix compiled this file, so nothing exercised the binding between the SSH
// channel layer and the SFTP codec. Here the whole path is driven: a session channel is opened, a
// subsystem "sftp" CHANNEL_REQUEST tags it, SSH_FXP_* requests arrive as CHANNEL_DATA, and the
// responses are read back out of the captured SSH packets.
//
// The oracle is draft-ietf-secsh-filexfer-02 (ietf.org/archive/id), the authoritative text for
// SFTP v3, quoted verbatim at each check.
//
// sec 3, the framing every packet uses:
//
//   uint32             length
//   byte               type
//   byte[length - 1]   data payload
//
//   "The `length' is the length of the data area, and does not include the `length' field itself."
//
// sec 4, initialization:
//
//   "When the file transfer protocol starts, it first sends a SSH_FXP_INIT (including its version
//    number) packet to the server.  The server responds with a SSH_FXP_VERSION packet, supplying
//    the lowest of its own and the client's version number."
//
//   SSH_FXP_INIT:     uint32 version, <extension data>
//   SSH_FXP_VERSION:  uint32 version, <extension data>
//
//   "The version number of the protocol specified in this document is 3."

#include "network_drivers/application/sftp/sftp.h"
#include "network_drivers/application/sftp/ssh_sftp.h"
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

// The client's channel id, and the local id the server confirms back.
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
    pc_fs_begin("/");

    pc_ssh_conn_setup();
    pc_ssh_channel_init(SSH_SLOT);
    pc_ssh_conn_accept(SSH_CONN_SLOT);
    pc_ssh_sftp_begin();
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

// Open a "session" channel and turn it into an SFTP one with a subsystem request (RFC 4254 sec 6.5).
static void open_sftp_channel(void)
{
    uint8_t pkt[128], out[128];
    size_t n = 0, ol = 0;
    pkt[n++] = SSH_MSG_CHANNEL_OPEN;
    n += put_string(pkt + n, "session");
    put_u32(pkt + n, PEER_CHAN);
    put_u32(pkt + n + 4, 32768); // initial window size
    put_u32(pkt + n + 8, 32768); // maximum packet size
    n += 12;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_open(SSH_SLOT, pkt, n, out, &ol, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN_CONFIRM, out[0]);
    g_chan = get_u32(out + 5);

    n = 0;
    pkt[n++] = SSH_MSG_CHANNEL_REQUEST;
    put_u32(pkt + n, g_chan);
    n += 4;
    n += put_string(pkt + n, "subsystem");
    pkt[n++] = 1; // want_reply
    n += put_string(pkt + n, "sftp");
    ol = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_request(SSH_SLOT, pkt, n, out, &ol, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_SUCCESS, out[0]);
}

// Hand @p len bytes of SFTP stream to the server as one CHANNEL_DATA message.
static void feed(const uint8_t *sftp, size_t len)
{
    uint8_t pkt[1024], out[64];
    size_t n = 0, ol = 0;
    pkt[n++] = SSH_MSG_CHANNEL_DATA;
    put_u32(pkt + n, g_chan);
    n += 4;
    put_u32(pkt + n, (uint32_t)len);
    n += 4;
    memcpy(pkt + n, sftp, len);
    n += len;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_data(SSH_SLOT, pkt, n, out, &ol, sizeof(out)));
}

// The SFTP payload the server sent back: the first captured SSH packet, its CHANNEL_DATA string,
// then sec 3's length-prefixed body. Returns the type byte, or 0 when nothing was sent.
static uint8_t response(const uint8_t **body, uint32_t *body_len)
{
    size_t n = 0;
    const uint8_t *w = pc_net_host_sent(&n);
    if (n < 6)
    {
        return 0;
    }
    uint32_t pkt_len = get_u32(w);
    uint8_t pad = w[4];
    if (pkt_len < (uint32_t)pad + 1u || 4u + pkt_len > n)
    {
        return 0;
    }
    const uint8_t *p = w + 5; // SSH payload
    if (p[0] != SSH_MSG_CHANNEL_DATA)
    {
        return 0;
    }
    uint32_t dlen = get_u32(p + 5); // the CHANNEL_DATA string
    const uint8_t *d = p + 9;
    if (dlen < 5)
    {
        return 0;
    }
    uint32_t sftp_len = get_u32(d); // sec 3 length, excluding itself
    if (sftp_len + 4u > dlen)
    {
        return 0;
    }
    if (body)
    {
        *body = d + 5;
        *body_len = sftp_len - 1u;
    }
    return d[4];
}

// ---------------------------------------------------------------------------
// sec 4 - Protocol Initialization
// ---------------------------------------------------------------------------

// A version-3 client gets version 3 back: the lowest of the two is 3 either way.
static void test_s4_init_is_answered_with_version(void)
{
    open_sftp_channel();
    tcp_capture_reset();

    uint8_t init[9];
    put_u32(init, 5); // sec 3 length: type + uint32 version
    init[4] = PC_SSH_FXP_INIT;
    put_u32(init + 5, 3);
    feed(init, sizeof(init));

    const uint8_t *body = NULL;
    uint32_t blen = 0;
    TEST_ASSERT_EQUAL_UINT8(PC_SSH_FXP_VERSION, response(&body, &blen));
    TEST_ASSERT_EQUAL_UINT32(4, blen);
    TEST_ASSERT_EQUAL_UINT32(3, get_u32(body));
}

// "The server responds with a SSH_FXP_VERSION packet, supplying the lowest of its own and the
// client's version number." The version the client offered is never read: the reply is the fixed
// PC_SFTP_VERSION, so a version-2 client is answered 3. Pinned as it behaves; docs/BUGS.md carries
// the deviation.
static void test_s4_an_older_client_is_not_answered_the_lower_version(void)
{
    open_sftp_channel();
    tcp_capture_reset();

    uint8_t init[9];
    put_u32(init, 5);
    init[4] = PC_SSH_FXP_INIT;
    put_u32(init + 5, 2); // the client offers version 2
    feed(init, sizeof(init));

    const uint8_t *body = NULL;
    uint32_t blen = 0;
    TEST_ASSERT_EQUAL_UINT8(PC_SSH_FXP_VERSION, response(&body, &blen));
    TEST_ASSERT_EQUAL_UINT32(3, get_u32(body)); // 3, not the lower 2 the draft asks for
}

// ---------------------------------------------------------------------------
// sec 3 - General Packet Format
// ---------------------------------------------------------------------------

// A request whose sec 3 length runs past the bytes delivered is not yet a packet: the server waits
// for the rest rather than parsing a short buffer.
static void test_s3_a_short_packet_produces_no_response(void)
{
    open_sftp_channel();
    tcp_capture_reset();

    uint8_t part[7];
    put_u32(part, 5); // claims 5 bytes of body
    part[4] = PC_SSH_FXP_INIT;
    part[5] = 0; // only two of the four version bytes arrive
    part[6] = 0;
    feed(part, sizeof(part));

    size_t n = 0;
    (void)pc_net_host_sent(&n);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

// The stream is a stream: a packet split across two CHANNEL_DATA messages is reassembled and served
// once its last byte arrives.
static void test_s3_a_packet_split_across_messages_is_reassembled(void)
{
    open_sftp_channel();
    tcp_capture_reset();

    uint8_t init[9];
    put_u32(init, 5);
    init[4] = PC_SSH_FXP_INIT;
    put_u32(init + 5, 3);

    feed(init, 6); // length, type, and one version byte
    size_t n = 0;
    (void)pc_net_host_sent(&n);
    TEST_ASSERT_EQUAL_size_t(0, n); // nothing yet

    feed(init + 6, 3); // the rest
    TEST_ASSERT_EQUAL_UINT8(PC_SSH_FXP_VERSION, response(NULL, NULL));
}

// Two requests in one CHANNEL_DATA message are both served: the server drains the buffer rather
// than handling the first and dropping the remainder.
static void test_s3_two_packets_in_one_message_are_both_served(void)
{
    open_sftp_channel();
    tcp_capture_reset();

    uint8_t two[18];
    put_u32(two, 5);
    two[4] = PC_SSH_FXP_INIT;
    put_u32(two + 5, 3);
    put_u32(two + 9, 5);
    two[13] = PC_SSH_FXP_INIT;
    put_u32(two + 14, 3);
    feed(two, sizeof(two));

    // Two VERSION replies, so the capture holds two SSH packets.
    size_t n = 0;
    const uint8_t *w = pc_net_host_sent(&n);
    uint32_t first = get_u32(w);
    TEST_ASSERT_TRUE(4u + first < n); // a second packet follows the first
}

// ---------------------------------------------------------------------------
// The subsystem binding (RFC 4254 sec 6.5)
// ---------------------------------------------------------------------------

// Until the channel is an SFTP channel, SSH_FXP_* bytes are not SFTP: a session channel that never
// asked for the subsystem gets no response.
static void test_s6_5_a_plain_session_channel_serves_no_sftp(void)
{
    uint8_t pkt[128], out[128];
    size_t n = 0, ol = 0;
    pkt[n++] = SSH_MSG_CHANNEL_OPEN;
    n += put_string(pkt + n, "session");
    put_u32(pkt + n, PEER_CHAN);
    put_u32(pkt + n + 4, 32768);
    put_u32(pkt + n + 8, 32768);
    n += 12;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_open(SSH_SLOT, pkt, n, out, &ol, sizeof(out)));
    g_chan = get_u32(out + 5);
    tcp_capture_reset();

    uint8_t init[9];
    put_u32(init, 5);
    init[4] = PC_SSH_FXP_INIT;
    put_u32(init + 5, 3);
    feed(init, sizeof(init));

    size_t got = 0;
    (void)pc_net_host_sent(&got);
    TEST_ASSERT_EQUAL_size_t(0, got);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_s4_init_is_answered_with_version);
    RUN_TEST(test_s4_an_older_client_is_not_answered_the_lower_version);
    RUN_TEST(test_s3_a_short_packet_produces_no_response);
    RUN_TEST(test_s3_a_packet_split_across_messages_is_reassembled);
    RUN_TEST(test_s3_two_packets_in_one_message_are_both_served);
    RUN_TEST(test_s6_5_a_plain_session_channel_serves_no_sftp);
    return UNITY_END();
}
