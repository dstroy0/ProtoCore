// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-file coverage for ssh/connection/ssh_conn.c - the TCP transport to SSH protocol glue.
//
// The sections covered here are the ones this file implements, read off the source rather than
// inferred from any test:
//
//   RFC 4253 sec 4.2  pc_ssh_conn_accept writes the server identification string raw, before any
//                     binary packet; pc_ssh_conn_rx consumes the peer's while the phase is BANNER.
//   RFC 4253 sec 6    ssh_emit frames outbound through ssh_pkt_emit on .out, pc_ssh_conn_rx
//                     deframes inbound through ssh_pkt_recv on .in, ssh_tx_drain moves the framed
//                     bytes out in send-window sized pieces.
//   RFC 4253 sec 9    pc_ssh_conn_poll emits a fresh KEXINIT once the volume or time budget is
//                     spent and no exchange is already running.
//   RFC 4252 sec 8    pc_ssh_conn_poll drains a completed password change into its deferred reply.
//   RFC 4254 sec 5.2  pc_ssh_conn_send builds CHANNEL_DATA bounded by the peer window.
//   RFC 4254 sec 5.3  pc_ssh_conn_close_channel builds EOF then CLOSE, as two binary packets.
//   RFC 4254 sec 7.2  pc_ssh_conn_open_forwarded opens a server-initiated forwarded-tcpip channel.
//
// What this file does that no section governs - the SSH-slot to TCP-slot mapping, the secure and
// plaintext pool borrows, dropping a reply for a socket that died between the read and the write,
// and wiping the connection's whole span on close - is the library's own ownership law, so it
// carries no section number and is not asserted here under one.
//
// Every quotation below is from the RFC named beside it.

#include "mmgr/plaintext.h"
#include "mmgr/secure.h"
#include "network_drivers/presentation/ssh/auth/ssh_auth.h"
#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "network_drivers/presentation/ssh/ssh_server.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include "network_drivers/tls/ssh_rsa.h"
#include "network_drivers/transport/tcp.h"
#include <stdint.h>
#include <string.h>

#include "core_setup/hal/nvs.h"
#include "rx_feed.h"
#include <Arduino.h> // set_millis(): moving the virtual clock past the password-change cooldown
#include "test/fixtures/ssh_test_host_key/ssh_test_keys.h"
#include <unity.h>

void setUp()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].pcb = pc_net_host_pcb();
        conn_pool[i].proto = PROTO_SSH;
        conn_pool[i].proto_slot = PC_PROTO_SLOT_NONE;
    }
    pc_ssh_conn_setup();
    TEST_ASSERT_TRUE(pc_nvs_put_blob(PC_SSH_HOST_KEY_NS, PC_SSH_HOST_KEY_ITEM, PC_SSH_BASELINE_KEY_DER,
                                     PC_SSH_BASELINE_KEY_DER_LEN));
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_rsa_load_pubkey());
    tcp_capture_reset();
}

void tearDown()
{
    pc_ssh_conn_close(0);
    tcp_capture_disable();
}

// ---------------------------------------------------------------------------
// Wire builders
// ---------------------------------------------------------------------------

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t rd_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static size_t put_string(uint8_t *p, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    wr_u32(p, n);
    memcpy(p + 4, s, n);
    return 4 + n;
}

// RFC 4251 sec 5 mpint: a leading zero octet when the high bit of the first byte is set.
static size_t put_mpint(uint8_t *p, const uint8_t *be, size_t n)
{
    size_t at = 0;
    while (at < n && be[at] == 0)
    {
        at++;
    }
    size_t len = n - at;
    size_t pad = (len > 0 && (be[at] & 0x80u)) ? 1 : 0;
    wr_u32(p, (uint32_t)(len + pad));
    if (pad)
    {
        p[4] = 0;
    }
    memcpy(p + 4 + pad, be + at, len);
    return 4 + pad + len;
}

// RFC 4253 sec 6: uint32 packet_length | byte padding_length | payload | padding. packet_length
// excludes itself and the mac; the padding is at least 4 bytes and brings the whole run to a
// multiple of 8 with no cipher negotiated.
static size_t frame_packet(uint8_t *out, const uint8_t *payload, size_t plen)
{
    size_t total = 5 + plen;
    size_t pad = 8 - (total % 8);
    if (pad < 4)
    {
        pad += 8;
    }
    size_t pkt_len = 1 + plen + pad;
    wr_u32(out, (uint32_t)pkt_len);
    out[4] = (uint8_t)pad;
    memcpy(out + 5, payload, plen);
    memset(out + 5 + plen, 0, pad);
    return 4 + pkt_len;
}

static size_t put_namelist(uint8_t *p, const char *s)
{
    return put_string(p, s);
}

// RFC 4253 sec 7.1: byte SSH_MSG_KEXINIT | 16 bytes cookie | ten name-lists | boolean
// first_kex_packet_follows | uint32 reserved.
static size_t build_client_kexinit(uint8_t *out)
{
    size_t o = 0;
    out[o++] = SSH_MSG_KEXINIT;
    for (int j = 0; j < 16; j++)
    {
        out[o++] = (uint8_t)j;
    }
    o += put_namelist(out + o, "diffie-hellman-group14-sha256");
    o += put_namelist(out + o, "rsa-sha2-256");
    o += put_namelist(out + o, "aes256-ctr");
    o += put_namelist(out + o, "aes256-ctr");
    o += put_namelist(out + o, "hmac-sha2-256");
    o += put_namelist(out + o, "hmac-sha2-256");
    o += put_namelist(out + o, "none");
    o += put_namelist(out + o, "none");
    o += put_namelist(out + o, "");
    o += put_namelist(out + o, "");
    out[o++] = 0; // first_kex_packet_follows
    wr_u32(out + o, 0);
    o += 4;
    return o;
}

// The message byte of the Nth captured binary packet, or 0 past the end.
static uint8_t captured_msg(int want)
{
    const uint8_t *w = (const uint8_t *)tcp_captured();
    size_t n = tcp_captured_len();
    size_t at = 0;
    for (int k = 0; at + 6 <= n; k++)
    {
        uint32_t pkt_len = rd_u32(w + at);
        if (pkt_len < 2 || at + 4u + pkt_len > n)
        {
            return 0;
        }
        if (k == want)
        {
            return w[at + 5];
        }
        at += 4u + pkt_len;
    }
    return 0;
}

// Drive the connection to the point a KEXINIT has been answered.
static void accept_and_identify(void)
{
    pc_ssh_conn_accept(0);
    push_str(0, "SSH-2.0-ConnSuite\r\n");
    pc_ssh_conn_rx(0);
}

// ---------------------------------------------------------------------------
// RFC 4253 sec 4.2 - Protocol Version Exchange
// ---------------------------------------------------------------------------

// "the server MUST send ... SSH-protoversion-softwareversion SP comments CR LF". It goes out raw,
// before any binary packet, so the very first bytes on the socket are the identification string.
static void test_s4_2_accept_sends_the_identification_string(void)
{
    pc_ssh_conn_accept(0);
    size_t n = tcp_captured_len();
    TEST_ASSERT_TRUE(n >= 10);
    TEST_ASSERT_EQUAL_MEMORY("SSH-2.0-", tcp_captured(), 8);
    TEST_ASSERT_EQUAL_UINT8('\r', tcp_captured()[n - 2]);
    TEST_ASSERT_EQUAL_UINT8('\n', tcp_captured()[n - 1]);
}

// The peer's identification is consumed and draws no reply of its own; the session moves on to
// await its KEXINIT.
static void test_s4_2_peer_identification_is_consumed(void)
{
    pc_ssh_conn_accept(0);
    tcp_capture_reset();
    push_str(0, "SSH-2.0-ConnSuite\r\n");
    pc_ssh_conn_rx(0);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    TEST_ASSERT_EQUAL(SSH_PHASE_KEXINIT, ssh_sess[conn_pool[0].proto_slot].phase);
}

// The identification string ends at CR LF, so a line without one is not yet complete and the
// session stays where it is rather than parsing a partial version.
static void test_s4_2_a_partial_identification_waits(void)
{
    pc_ssh_conn_accept(0);
    tcp_capture_reset();
    push_str(0, "SSH-2.0-NoTerminatorYet");
    pc_ssh_conn_rx(0);
    TEST_ASSERT_EQUAL(SSH_PHASE_BANNER, ssh_sess[conn_pool[0].proto_slot].phase);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
}

// "The maximum length of the string is 255 characters, including the Carriage Return and Line
// Feed." A line that runs past it with no terminator is refused and the connection is dropped.
static void test_s4_2_an_overlong_identification_closes(void)
{
    pc_ssh_conn_accept(0);
    tcp_capture_reset();

    char over[600];
    memcpy(over, "SSH-2.0-", 8);
    memset(over + 8, 'X', sizeof(over) - 8);
    push_bytes(0, (const uint8_t *)over, sizeof(over));
    pc_ssh_conn_rx(0);

    TEST_ASSERT_EQUAL(PC_PROTO_SLOT_NONE, conn_pool[0].proto_slot); // the slot was released
}

// ---------------------------------------------------------------------------
// RFC 4253 sec 6 - Binary Packet Protocol, through the ring
// ---------------------------------------------------------------------------

// A framed packet arriving after the identification exchange is deframed and dispatched, and the
// reply leaves framed on the same pass.
static void test_s6_a_framed_packet_is_deframed_and_answered(void)
{
    accept_and_identify();
    tcp_capture_reset();

    uint8_t payload[2048];
    uint8_t wire[2048];
    size_t plen = build_client_kexinit(payload);
    size_t wlen = frame_packet(wire, payload, plen);
    push_bytes(0, wire, wlen);
    pc_ssh_conn_rx(0);

    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_KEXINIT, captured_msg(0));
    TEST_ASSERT_EQUAL(SSH_PHASE_DH_INIT, ssh_sess[conn_pool[0].proto_slot].phase);
}

// The identification string and the first packet can arrive in one read: the banner is consumed
// exactly, and the bytes after it are deframed rather than being taken for more banner.
static void test_s6_identification_and_packet_in_one_read(void)
{
    pc_ssh_conn_accept(0);
    tcp_capture_reset();

    uint8_t payload[2048];
    uint8_t wire[2048];
    size_t plen = build_client_kexinit(payload);
    size_t wlen = frame_packet(wire, payload, plen);

    push_str(0, "SSH-2.0-ConnSuite\r\n");
    push_bytes(0, wire, wlen);
    pc_ssh_conn_rx(0);

    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_KEXINIT, captured_msg(0));
    TEST_ASSERT_EQUAL(SSH_PHASE_DH_INIT, ssh_sess[conn_pool[0].proto_slot].phase);
}

// ---------------------------------------------------------------------------
// RFC 4253 sec 9 - Key Re-Exchange
// ---------------------------------------------------------------------------

// "It is RECOMMENDED that the keys be changed after each gigabyte of transmitted data or after each
// hour of connection time, whichever comes sooner." The poll starts that exchange by sending a
// KEXINIT once the budget is spent.
static void test_s9_poll_starts_a_rekey_once_the_budget_is_spent(void)
{
    accept_and_identify();
    uint8_t j = conn_pool[0].proto_slot;
    ssh_sess[j].phase = SSH_PHASE_OPEN;
    ssh_sess[j].kex_active = PROTO_FALSE;
    ssh_pkt[j].seq_no_send = SSH_REKEY_PACKET_THRESHOLD;
    tcp_capture_reset();

    pc_ssh_conn_poll(0);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_KEXINIT, captured_msg(0));
}

// "an exchange starts only when one is not already running": with one in flight the poll sends
// nothing, so a long session cannot stack re-exchanges on top of each other.
static void test_s9_no_rekey_while_one_is_running(void)
{
    accept_and_identify();
    uint8_t j = conn_pool[0].proto_slot;
    ssh_sess[j].phase = SSH_PHASE_OPEN;
    ssh_sess[j].kex_active = PROTO_TRUE; // an exchange is already under way
    ssh_pkt[j].seq_no_send = SSH_REKEY_PACKET_THRESHOLD;
    tcp_capture_reset();

    pc_ssh_conn_poll(0);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
}

// A session that has not spent its budget is not re-keyed either.
static void test_s9_no_rekey_before_the_budget_is_spent(void)
{
    accept_and_identify();
    uint8_t j = conn_pool[0].proto_slot;
    ssh_sess[j].phase = SSH_PHASE_OPEN;
    ssh_sess[j].kex_active = PROTO_FALSE;
    ssh_pkt[j].seq_no_send = 1;
    ssh_pkt[j].seq_no_recv = 1;
    ssh_sess[j].last_kex_ms = pc_millis();
    tcp_capture_reset();

    pc_ssh_conn_poll(0);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
}

// ---------------------------------------------------------------------------
// RFC 4252 sec 8 - Password Authentication, the change reply
// ---------------------------------------------------------------------------

static void pw_change_started(uint8_t slot, const char *user, const char *old_pw, const char *new_pw)
{
    (void)slot;
    (void)user;
    (void)old_pw;
    (void)new_pw; // the application answers later, through pc_ssh_auth_pw_change_report
}

// Put a change in flight the way a client does: sec 8's change form carries the TRUE flag and both
// passwords, and the server defers rather than answering it.
//
//   byte SSH_MSG_USERAUTH_REQUEST | string user name | string service name | string "password"
//   | boolean TRUE | string plaintext old password | string plaintext new password
static uint8_t begin_password_change(void)
{
    uint8_t j = conn_pool[0].proto_slot;
    ssh_sess[j].phase = SSH_PHASE_AUTH;
    pc_ssh_auth_set_password_change_cb(pw_change_started);
    // The change cooldown is measured server-wide and survives a connection, so the clock is moved
    // past it here rather than letting the previous case's change decide this one's outcome.
    set_millis(millis() + PC_SSH_PW_CHANGE_COOLDOWN_MS + 1);

    uint8_t req[192];
    size_t n = 0;
    req[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(req + n, "alice");
    n += put_string(req + n, "ssh-connection");
    n += put_string(req + n, "password");
    req[n++] = 1; // TRUE: a change, not an authentication
    n += put_string(req + n, "oldpw");
    n += put_string(req + n, "newpw");

    uint8_t out[192];
    size_t olen = 99;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(j, req, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, olen); // deferred: the reply follows once the application reports
    return j;
}

// "The server must reply to each request message with SSH_MSG_USERAUTH_SUCCESS,
// SSH_MSG_USERAUTH_FAILURE, or another SSH_MSG_USERAUTH_PASSWD_CHANGEREQ." The change runs in the
// application, so the reply its USERAUTH_REQUEST deferred leaves from the poll:
// "SSH_MSG_USERAUTH_SUCCESS - The password has been changed, and authentication has been
// successfully completed."
static void test_rfc4252_s8_a_completed_password_change_answers_success(void)
{
    accept_and_identify();
    uint8_t j = begin_password_change();
    tcp_capture_reset();

    pc_ssh_auth_pw_change_report(j, PROTO_TRUE);
    pc_ssh_conn_poll(0);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_SUCCESS, captured_msg(0));
}

// "SSH_MSG_USERAUTH_FAILURE without partial success - The password has not been changed."
static void test_rfc4252_s8_a_refused_password_change_answers_failure(void)
{
    accept_and_identify();
    uint8_t j = begin_password_change();
    tcp_capture_reset();

    pc_ssh_auth_pw_change_report(j, PROTO_FALSE);
    pc_ssh_conn_poll(0);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_FAILURE, captured_msg(0));
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 5.2 / 5.3 - Data Transfer and Closing a Channel
// ---------------------------------------------------------------------------

// Put an open session channel on the slot so the channel entry points have one to address.
static uint8_t open_channel(uint32_t peer_id, uint32_t peer_window)
{
    uint8_t j = conn_pool[0].proto_slot;
    pc_ssh_channel_init(j);
    ssh_chan[j][0].open = PROTO_TRUE;
    ssh_chan[j][0].type = SSH_CHAN_SESSION;
    ssh_chan[j][0].local_id = 0;
    ssh_chan[j][0].peer_id = peer_id;
    pc_ssh_flow_init(&ssh_chan[j][0].flow, SSH_CHAN_WINDOW, peer_window, SSH_CHAN_MAX_PACKET);
    return j;
}

// sec 5.2: byte SSH_MSG_CHANNEL_DATA | uint32 recipient channel | string data. The recipient is the
// peer's channel number, not ours.
static void test_rfc4254_s5_2_send_frames_channel_data_to_the_peer_id(void)
{
    accept_and_identify();
    open_channel(77, 32768);
    tcp_capture_reset();

    TEST_ASSERT_EQUAL_INT(5, pc_ssh_conn_send(conn_pool[0].proto_slot, 0, (const uint8_t *)"hello", 5));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_DATA, captured_msg(0));

    const uint8_t *p = (const uint8_t *)tcp_captured() + 5; // past packet_length + padding_length
    TEST_ASSERT_EQUAL_UINT32(77, rd_u32(p + 1));
    TEST_ASSERT_EQUAL_UINT32(5, rd_u32(p + 5));
    TEST_ASSERT_EQUAL_MEMORY("hello", p + 9, 5);
}

// sec 5.2: "The maximum amount of data allowed is determined by the maximum packet size for the
// channel, and the current window size, whichever is smaller." More than the window is refused.
static void test_rfc4254_s5_2_send_past_the_peer_window_is_refused(void)
{
    accept_and_identify();
    open_channel(77, 2); // a window of two bytes
    tcp_capture_reset();

    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_send(conn_pool[0].proto_slot, 0, (const uint8_t *)"hello", 5));
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
}

// sec 5.3: EOF is sent "when a party will no longer send more data", CLOSE "when either party
// wishes to terminate the channel". Each is its own message, so the two leave as two binary
// packets rather than one carrying both.
static void test_rfc4254_s5_3_close_channel_sends_eof_then_close_as_two_packets(void)
{
    accept_and_identify();
    open_channel(77, 32768);
    tcp_capture_reset();

    TEST_ASSERT_EQUAL_INT(0, pc_ssh_conn_close_channel(conn_pool[0].proto_slot, 0));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_EOF, captured_msg(0));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_CLOSE, captured_msg(1));
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 7.2 - TCP/IP Forwarding Channels
// ---------------------------------------------------------------------------

// "When a connection comes to a port for which remote forwarding has been requested, a channel is
// opened to forward the port to the other side": byte SSH_MSG_CHANNEL_OPEN | string
// "forwarded-tcpip" | ... This entry point puts that open on the wire.
static void test_rfc4254_s7_2_open_forwarded_emits_a_forwarded_tcpip_open(void)
{
    accept_and_identify();
    uint8_t j = conn_pool[0].proto_slot;
    pc_ssh_channel_init(j);
    tcp_capture_reset();

    TEST_ASSERT_TRUE(pc_ssh_conn_open_forwarded(j, "10.0.0.1", 8080, "192.168.1.9", 51000) >= 0);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_CHANNEL_OPEN, captured_msg(0));

    const uint8_t *p = (const uint8_t *)tcp_captured() + 5;
    TEST_ASSERT_EQUAL_UINT32(15, rd_u32(p + 1));
    TEST_ASSERT_EQUAL_MEMORY("forwarded-tcpip", p + 5, 15);
}

// ---------------------------------------------------------------------------
// End to end: the handshake in the order RFC 4253 gives it
//
// The cases above pin this file's own seams. This one proves the translation units compose: raw
// bytes go into the receive ring and every reply is read back off the socket, so the path driven is
// tcp_conn -> ssh_conn -> ssh_packet -> ssh_transport -> ssh_server and back, with no dispatcher or
// codec called directly.
//
// It stops at NEWKEYS, which is where sec 7.3 starts requiring the client to encrypt.
// test/servers/cyclone_ssh drives the encrypted session with a real Oryx client.
// ---------------------------------------------------------------------------

static void test_s4_2_to_s7_3_handshake_end_to_end_through_the_byte_pump(void)
{
    // sec 4.2: the identification strings, ours first.
    pc_ssh_conn_accept(0);
    TEST_ASSERT_EQUAL_MEMORY("SSH-2.0-", tcp_captured(), 8);
    tcp_capture_reset();
    push_str(0, "SSH-2.0-EndToEndClient\r\n");
    pc_ssh_conn_rx(0);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    // sec 7.1: "Key exchange begins by each side sending" SSH_MSG_KEXINIT.
    uint8_t payload[2048];
    uint8_t wire[2048];
    size_t plen = build_client_kexinit(payload);
    size_t wlen = frame_packet(wire, payload, plen);
    tcp_capture_reset();
    push_bytes(0, wire, wlen);
    pc_ssh_conn_rx(0);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_KEXINIT, captured_msg(0));

    // sec 8: "First, the client sends ... SSH_MSG_KEXDH_INIT | mpint e", answered by KEXDH_REPLY,
    // and sec 7.3's NEWKEYS follows it.
    uint8_t e_be[256];
    memset(e_be, 0, sizeof(e_be));
    e_be[255] = 0x02; // e = 2, inside [1, p-1]
    plen = 0;
    payload[plen++] = SSH_MSG_KEXDH_INIT;
    plen += put_mpint(payload + plen, e_be, sizeof(e_be));
    wlen = frame_packet(wire, payload, plen);
    tcp_capture_reset();
    push_bytes(0, wire, wlen);
    pc_ssh_conn_rx(0);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_KEXDH_REPLY, captured_msg(0));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_NEWKEYS, captured_msg(1));

    // sec 7.3: "When this message is received, the new keys and algorithms MUST be used for
    // receiving."
    uint8_t j = conn_pool[0].proto_slot;
    TEST_ASSERT_FALSE(ssh_sess[j].in.enc);
    uint8_t nk = SSH_MSG_NEWKEYS;
    wlen = frame_packet(wire, &nk, 1);
    tcp_capture_reset();
    push_bytes(0, wire, wlen);
    pc_ssh_conn_rx(0);
    TEST_ASSERT_TRUE(ssh_sess[j].in.enc);
    TEST_ASSERT_EQUAL(SSH_PHASE_SERVICE, ssh_sess[j].phase);
}

// sec 8: "Values of 'e' or 'f' that are not in the range [1, p-1] MUST NOT be sent or accepted by
// either side.  If this condition is violated, the key exchange fails."
static void test_s8_a_kexdh_init_outside_the_group_fails_the_exchange(void)
{
    accept_and_identify();

    uint8_t payload[2048];
    uint8_t wire[2048];
    size_t plen = build_client_kexinit(payload);
    size_t wlen = frame_packet(wire, payload, plen);
    push_bytes(0, wire, wlen);
    pc_ssh_conn_rx(0);

    uint8_t e_be[256];
    memset(e_be, 0, sizeof(e_be)); // e = 0, below the range
    plen = 0;
    payload[plen++] = SSH_MSG_KEXDH_INIT;
    plen += put_mpint(payload + plen, e_be, sizeof(e_be));
    wlen = frame_packet(wire, payload, plen);
    tcp_capture_reset();
    push_bytes(0, wire, wlen);
    pc_ssh_conn_rx(0);

    // No KEXDH_REPLY anywhere in what was sent, and nothing keyed off the out-of-range value. The
    // exchange is failed by dropping the connection, which releases the slot.
    const uint8_t *out = (const uint8_t *)tcp_captured();
    for (size_t k = 0; k + 5 < tcp_captured_len(); k++)
    {
        TEST_ASSERT_NOT_EQUAL_UINT8(SSH_MSG_KEXDH_REPLY, out[k]);
    }
    TEST_ASSERT_EQUAL(PC_PROTO_SLOT_NONE, conn_pool[0].proto_slot);
}

// ---------------------------------------------------------------------------
// This file's own contract - no RFC governs it, so each case is named for what it asserts
//
// ssh_conn.c owns the SSH-slot to TCP-slot mapping, the pool borrows every outbound call makes,
// whether the socket is still worth writing to, and wiping the connection's span on close. None of
// that is in a specification; all of it decides whether the sections above are reachable at all.
// ---------------------------------------------------------------------------

// One SSH session per TCP slot, and no more sessions than slots: with the pool spent, the next
// connection is dropped rather than served without a session.
static void test_slot_mapping_accept_without_capacity_drops_the_connection(void)
{
    pc_ssh_conn_accept(0);
    TEST_ASSERT_NOT_EQUAL(PC_PROTO_SLOT_NONE, conn_pool[0].proto_slot);

    for (int k = 1; k < MAX_CONNS && k <= MAX_SSH_CONNS; k++)
    {
        pc_ssh_conn_accept((uint8_t)k);
        TEST_ASSERT_EQUAL(PC_PROTO_SLOT_NONE, conn_pool[k].proto_slot);
    }
}

// A slot whose proto_slot names a session that belongs to a different TCP slot is a stale mapping
// left by a reused slot. Neither the receive path nor the poll acts on it.
static void test_slot_mapping_a_foreign_mapping_is_ignored(void)
{
    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;

    conn_pool[1].proto_slot = j; // conn 1 claims conn 0's session
    tcp_capture_reset();
    push_str(1, "SSH-2.0-Impostor\r\n");
    pc_ssh_conn_rx(1);
    pc_ssh_conn_poll(1);

    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    TEST_ASSERT_EQUAL(SSH_PHASE_BANNER, ssh_sess[j].phase); // conn 0's session untouched
}

// Every outbound entry point fails closed on a slot out of range and on one that maps to no TCP
// connection, so a caller holding a stale session id cannot write to somebody else's socket.
static void test_slot_mapping_outbound_refuses_an_unmapped_slot(void)
{
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_send(MAX_SSH_CONNS, 0, (const uint8_t *)"x", 1));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_close_channel(MAX_SSH_CONNS, 0));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_open_forwarded(MAX_SSH_CONNS, "1.2.3.4", 80, "5.6.7.8", 90));

    // In range, but never accepted: no TCP connection is mapped to it.
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_send(0, 0, (const uint8_t *)"x", 1));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_close_channel(0, 0));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_open_forwarded(0, "1.2.3.4", 80, "5.6.7.8", 90));
}

// The receive path checks the mapping but never liveness, so a socket that died between the inbound
// read and the outbound reply arrives here mapped and dead. The reply is dropped rather than handed
// to a transport that cannot take it.
static void test_liveness_outbound_refuses_a_dead_socket(void)
{
    accept_and_identify();
    open_channel(77, 32768);
    uint8_t j = conn_pool[0].proto_slot;

    conn_pool[0].state = CONN_FREE; // the socket died after the read
    tcp_capture_reset();

    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_send(j, 0, (const uint8_t *)"hello", 5));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_close_channel(j, 0));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_open_forwarded(j, "1.2.3.4", 80, "5.6.7.8", 90));
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
}

// The poll walks every slot the session layer hands it, so it returns at the state guard rather
// than working on a connection that is not active.
static void test_liveness_poll_ignores_an_inactive_connection(void)
{
    accept_and_identify();
    uint8_t j = conn_pool[0].proto_slot;
    ssh_sess[j].phase = SSH_PHASE_OPEN;
    ssh_pkt[j].seq_no_send = SSH_REKEY_PACKET_THRESHOLD; // a re-key would otherwise be due

    conn_pool[0].state = CONN_FREE;
    tcp_capture_reset();
    pc_ssh_conn_poll(0);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
}

// The identification string is only written when the socket can take it: a slot with no pcb is set
// up as a session either way, but nothing is put on the wire.
static void test_liveness_accept_skips_the_identification_on_a_dead_socket(void)
{
    conn_pool[0].pcb = NULL;
    tcp_capture_reset();
    pc_ssh_conn_accept(0);
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());
    TEST_ASSERT_NOT_EQUAL(PC_PROTO_SLOT_NONE, conn_pool[0].proto_slot); // the session still exists
}

// Every outbound call borrows a payload and a wire buffer from the secure pool at once. With the
// pool spent the borrow fails, and the message is dropped rather than half-framed.
static void test_pool_outbound_fails_closed_when_the_secure_pool_is_spent(void)
{
    accept_and_identify();
    open_channel(77, 32768);
    uint8_t j = conn_pool[0].proto_slot;

    size_t mark = pc_secure_mark();
    while (pc_secure_alloc(256, 16) != NULL)
    {
    }
    tcp_capture_reset();

    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_send(j, 0, (const uint8_t *)"hello", 5));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_close_channel(j, 0));
    TEST_ASSERT_EQUAL_size_t(0, tcp_captured_len());

    pc_secure_release(mark);
}

// Closing frees the session slot and returns the TCP slot's mapping, so the next connection gets a
// session rather than inheriting the last one's.
static void test_teardown_close_releases_the_slot_for_the_next_connection(void)
{
    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;
    TEST_ASSERT_NOT_EQUAL(PC_PROTO_SLOT_NONE, j);

    pc_ssh_conn_close(0);
    TEST_ASSERT_EQUAL(PC_PROTO_SLOT_NONE, conn_pool[0].proto_slot);

    // The freed slot is reusable, and the session it hands over starts at the beginning.
    pc_ssh_conn_accept(0);
    TEST_ASSERT_EQUAL(j, conn_pool[0].proto_slot);
    TEST_ASSERT_EQUAL(SSH_PHASE_BANNER, ssh_sess[j].phase);
    TEST_ASSERT_FALSE(ssh_sess[j].authed);
}

// The handler accessor is the one seam a consumer installs SSH through, so it wires the emit
// callback as well as returning the table. Without that a server sends its identification string
// and then silently drops every framed packet after it.
static void test_handler_seam_returns_the_table_and_installs_emit(void)
{
    pc_ssh_server_set_emit_cb(NULL); // as if a consumer had cleared it
    const struct ProtoHandler *h = ssh_proto_handler();
    TEST_ASSERT_NOT_NULL(h);

    // With the callback live again, a framed packet draws a framed reply.
    accept_and_identify();
    uint8_t payload[2048];
    uint8_t wire[2048];
    size_t plen = build_client_kexinit(payload);
    size_t wlen = frame_packet(wire, payload, plen);
    tcp_capture_reset();
    push_bytes(0, wire, wlen);
    pc_ssh_conn_rx(0);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_KEXINIT, captured_msg(0));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_s4_2_accept_sends_the_identification_string);
    RUN_TEST(test_s4_2_peer_identification_is_consumed);
    RUN_TEST(test_s4_2_a_partial_identification_waits);
    RUN_TEST(test_s4_2_an_overlong_identification_closes);
    RUN_TEST(test_s6_a_framed_packet_is_deframed_and_answered);
    RUN_TEST(test_s6_identification_and_packet_in_one_read);
    RUN_TEST(test_s9_poll_starts_a_rekey_once_the_budget_is_spent);
    RUN_TEST(test_s9_no_rekey_while_one_is_running);
    RUN_TEST(test_s9_no_rekey_before_the_budget_is_spent);
    RUN_TEST(test_rfc4252_s8_a_completed_password_change_answers_success);
    RUN_TEST(test_rfc4252_s8_a_refused_password_change_answers_failure);
    RUN_TEST(test_rfc4254_s5_2_send_frames_channel_data_to_the_peer_id);
    RUN_TEST(test_rfc4254_s5_2_send_past_the_peer_window_is_refused);
    RUN_TEST(test_rfc4254_s5_3_close_channel_sends_eof_then_close_as_two_packets);
    RUN_TEST(test_rfc4254_s7_2_open_forwarded_emits_a_forwarded_tcpip_open);
    RUN_TEST(test_s4_2_to_s7_3_handshake_end_to_end_through_the_byte_pump);
    RUN_TEST(test_s8_a_kexdh_init_outside_the_group_fails_the_exchange);
    RUN_TEST(test_slot_mapping_accept_without_capacity_drops_the_connection);
    RUN_TEST(test_slot_mapping_a_foreign_mapping_is_ignored);
    RUN_TEST(test_slot_mapping_outbound_refuses_an_unmapped_slot);
    RUN_TEST(test_liveness_outbound_refuses_a_dead_socket);
    RUN_TEST(test_liveness_poll_ignores_an_inactive_connection);
    RUN_TEST(test_liveness_accept_skips_the_identification_on_a_dead_socket);
    RUN_TEST(test_pool_outbound_fails_closed_when_the_secure_pool_is_spent);
    RUN_TEST(test_teardown_close_releases_the_slot_for_the_next_connection);
    RUN_TEST(test_handler_seam_returns_the_table_and_installs_emit);
    return UNITY_END();
}
