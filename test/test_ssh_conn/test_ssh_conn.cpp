// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SSH transport-glue test: drives a ConnProto::PROTO_SSH connection through the real
// conn_pool ring buffer + packet layer (banner exchange → KEXINIT) and checks
// the bytes written back to the socket via the tcp_write capture mock.

#include "crypto/asymmetric/bignum.h" // bn_expmod_group14 direct coverage (Montgomery guard sliver)
#include "lwip/tcp.h"
#include "network_drivers/presentation/ssh/auth/ssh_auth.h" // password cb for the direct-dispatch handshake
#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "network_drivers/presentation/ssh/connection/ssh_server.h" // pc_ssh_server_set_emit_cb (emit-wiring regression)
#include "network_drivers/presentation/ssh/crypto/ssh_rsa.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include "network_drivers/transport/tcp.h"
#include "server/mmgr/scratch.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

// Native RSA test fixture (defined in ssh_rsa.cpp native path).
extern uint8_t _test_rsa_n[256];
extern uint8_t _test_rsa_d[256];
extern uint8_t _test_rsa_e[4];

void setUp()
{
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = {};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = ConnState::CONN_ACTIVE;
        conn_pool[i].pcb = &_mock_pcb;
        conn_pool[i].proto = ConnProto::PROTO_SSH;
        conn_pool[i].proto_slot = PC_PROTO_SLOT_NONE;
    }
    pc_ssh_conn_setup();
    // A host key must be available for host-key negotiation to succeed (RSA here).
    memset(_test_rsa_n, 0, 256);
    _test_rsa_n[0] = 0xFF;
    _test_rsa_n[255] = 0x01;
    memset(_test_rsa_d, 0, 256);
    _test_rsa_d[255] = 0x01;
    _test_rsa_e[0] = 0x00;
    _test_rsa_e[1] = 0x01;
    _test_rsa_e[2] = 0x00;
    _test_rsa_e[3] = 0x01;
    pc_ssh_rsa_load_pubkey();
    tcp_capture_reset();
}
void tearDown()
{
    // Release the SSH slot (mirrors a real disconnect) so it is free next test.
    pc_ssh_conn_close(0);
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

static void hex2bytes(uint8_t *out, const char *hex, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        char b[3] = {hex[2 * i], hex[2 * i + 1], 0};
        out[i] = (uint8_t)strtol(b, nullptr, 16);
    }
}

static size_t put_namelist(uint8_t *p, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
    memcpy(p + 4, s, n);
    return 4 + n;
}

static size_t build_kexinit_payload(uint8_t *out)
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
    out[o++] = 0;
    for (int j = 0; j < 4; j++)
    {
        out[o++] = 0;
    }
    return o;
}

// Frame an unencrypted SSH binary packet around a payload (RFC 4253 §6).
static size_t frame_packet(uint8_t *out, const uint8_t *payload, size_t plen)
{
    size_t total = 5 + plen;
    size_t pad = 8 - (total % 8);
    if (pad < 4)
    {
        pad += 8;
    }
    size_t pkt_len = 1 + plen + pad;
    out[0] = (uint8_t)(pkt_len >> 24);
    out[1] = (uint8_t)(pkt_len >> 16);
    out[2] = (uint8_t)(pkt_len >> 8);
    out[3] = (uint8_t)pkt_len;
    out[4] = (uint8_t)pad;
    memcpy(out + 5, payload, plen);
    memset(out + 5 + plen, 0, pad);
    return 4 + pkt_len;
}

// ---------------------------------------------------------------------------
// Direct pc_ssh_server_dispatch() coverage: every switch arm of the message
// dispatcher (ssh_server.cpp) driven once, so the connection env exercises the
// full msg_type switch (not just the KEXINIT/DISCONNECT arms the byte-pump hits).
// ---------------------------------------------------------------------------

static uint8_t dsp_type[32];
static int dsp_n;
static void dsp_emit(uint8_t slot, const uint8_t *p, size_t n)
{
    (void)slot;
    if (dsp_n < 32 && n > 0)
    {
        dsp_type[dsp_n++] = p[0];
    }
}
static void dsp_reset()
{
    dsp_n = 0;
}

static size_t put_string(uint8_t *p, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
    memcpy(p + 4, s, n);
    return 4 + n;
}
static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static size_t put_mpint(uint8_t *p, const uint8_t *be, size_t len)
{
    size_t off = 0;
    while (off < len && be[off] == 0)
    {
        off++;
    }
    bool pad = (off < len) && (be[off] & 0x80u);
    size_t mlen = (len - off) + (pad ? 1 : 0);
    wr_u32(p, (uint32_t)mlen);
    size_t o = 4;
    if (pad)
    {
        p[o++] = 0;
    }
    memcpy(p + o, be + off, len - off);
    return o + (len - off);
}
static size_t build_kexinit_ext(uint8_t *out)
{
    size_t o = 0;
    out[o++] = SSH_MSG_KEXINIT;
    for (int j = 0; j < 16; j++)
    {
        out[o++] = (uint8_t)j;
    }
    o += put_namelist(out + o, "diffie-hellman-group14-sha256,ext-info-c");
    o += put_namelist(out + o, "rsa-sha2-256");
    o += put_namelist(out + o, "aes256-ctr");
    o += put_namelist(out + o, "aes256-ctr");
    o += put_namelist(out + o, "hmac-sha2-256");
    o += put_namelist(out + o, "hmac-sha2-256");
    o += put_namelist(out + o, "none");
    o += put_namelist(out + o, "none");
    o += put_namelist(out + o, "");
    o += put_namelist(out + o, "");
    out[o++] = 0;
    for (int j = 0; j < 4; j++)
    {
        out[o++] = 0;
    }
    return o;
}
static bool dsp_pw_cb(const char *u, const char *p)
{
    return strcmp(u, "alice") == 0 && strcmp(p, "s3cret") == 0;
}
static void dsp_on_chan_data(uint8_t slot, uint32_t ch, const uint8_t *d, size_t n)
{
    (void)slot;
    (void)ch;
    (void)d;
    (void)n;
}

// Drive the whole message switch through the dispatcher: a full handshake advances the phase
// so the post-auth arms are reachable, and the remaining stateless arms are poked once each.
void test_dispatch_all_switch_arms()
{
    ssh_transport_init(0);
    pc_ssh_channel_init(0);
    pc_ssh_auth_set_password_cb(dsp_pw_cb);
    pc_ssh_channel_set_data_cb(dsp_on_chan_data);
    pc_ssh_server_set_emit_cb(dsp_emit);

    SshSession *s = &ssh_sess[0];
    strcpy(s->v_c, "SSH-2.0-DispatchClient");
    s->v_c_len = (uint16_t)strlen(s->v_c);
    s->phase = SshPhase::SSH_PHASE_KEXINIT;

    uint8_t pkt[2048];
    size_t n = 0;

    // KEXINIT -> server KEXINIT, phase DH_INIT.
    n = build_kexinit_ext(pkt);
    dsp_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_KEXINIT, pkt, n));
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_DH_INIT, s->phase);

    // KEXDH_INIT (e = 2) -> KEXDH_REPLY + NEWKEYS.
    uint8_t e_be[256];
    memset(e_be, 0, sizeof(e_be));
    e_be[255] = 0x02;
    n = 0;
    pkt[n++] = SSH_MSG_KEXDH_INIT;
    n += put_mpint(pkt + n, e_be, 256);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_KEXDH_INIT, pkt, n));

    // NEWKEYS -> encryption active, EXT_INFO emitted (ext-info-c advertised), SERVICE phase.
    uint8_t nk = SSH_MSG_NEWKEYS;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_NEWKEYS, &nk, 1));
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_SERVICE, s->phase);

    // SERVICE_REQUEST -> SERVICE_ACCEPT, AUTH phase.
    n = 0;
    pkt[n++] = SSH_MSG_SERVICE_REQUEST;
    n += put_string(pkt + n, "ssh-userauth");
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_SERVICE_REQUEST, pkt, n));
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_AUTH, s->phase);

    // USERAUTH_REQUEST (password) -> SUCCESS, OPEN phase, authed.
    n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(pkt + n, "alice");
    n += put_string(pkt + n, "ssh-connection");
    n += put_string(pkt + n, "password");
    pkt[n++] = 0;
    n += put_string(pkt + n, "s3cret");
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_REQUEST, pkt, n));
    TEST_ASSERT_TRUE(s->authed);

    // CHANNEL_OPEN (session) -> CONFIRMATION.
    n = 0;
    pkt[n++] = SSH_MSG_CHANNEL_OPEN;
    n += put_string(pkt + n, "session");
    wr_u32(pkt + n, 11);
    wr_u32(pkt + n + 4, 4096);
    wr_u32(pkt + n + 8, 32768);
    n += 12;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_CHANNEL_OPEN, pkt, n));
    TEST_ASSERT_TRUE(ssh_chan[0][0].open);

    // CHANNEL_REQUEST (shell, want_reply) -> SUCCESS.
    n = 0;
    pkt[n++] = SSH_MSG_CHANNEL_REQUEST;
    wr_u32(pkt + n, 0);
    n += 4;
    n += put_string(pkt + n, "shell");
    pkt[n++] = 1;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_CHANNEL_REQUEST, pkt, n));

    // CHANNEL_DATA -> delivered to the app callback.
    n = 0;
    pkt[n++] = SSH_MSG_CHANNEL_DATA;
    wr_u32(pkt + n, 0);
    n += 4;
    n += put_string(pkt + n, "hi");
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_CHANNEL_DATA, pkt, n));

    // CHANNEL_WINDOW_ADJUST -> accepted, no reply.
    uint8_t w[9] = {SSH_MSG_CHANNEL_WINDOW_ADJUST, 0, 0, 0, 0, 0, 0, 0, 10};
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_CHANNEL_WINDOW_ADJUST, w, sizeof(w)));

    // CHANNEL_EOF -> accepted, no reply.
    uint8_t eofm[5] = {SSH_MSG_CHANNEL_EOF, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_CHANNEL_EOF, eofm, sizeof(eofm)));

    // GLOBAL_REQUEST (tcpip-forward, want_reply) -> REQUEST_SUCCESS/FAILURE.
    n = 0;
    pkt[n++] = SSH_MSG_GLOBAL_REQUEST;
    n += put_string(pkt + n, "tcpip-forward");
    pkt[n++] = 1;
    n += put_string(pkt + n, "0.0.0.0");
    wr_u32(pkt + n, 8080);
    n += 4;
    dsp_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_GLOBAL_REQUEST, pkt, n));
    TEST_ASSERT_EQUAL_INT(1, dsp_n);

    // CHANNEL_OPEN_CONFIRM / CHANNEL_OPEN_FAILURE -> stray, accepted-and-ignored (authed).
    uint8_t oc[9] = {SSH_MSG_CHANNEL_OPEN_CONFIRM, 0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_CHANNEL_OPEN_CONFIRM, oc, sizeof(oc)));
    uint8_t of[5] = {SSH_MSG_CHANNEL_OPEN_FAILURE, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_CHANNEL_OPEN_FAILURE, of, sizeof(of)));

    // CHANNEL_CLOSE -> EOF + CLOSE emitted, channel closed.
    uint8_t cl[5];
    cl[0] = SSH_MSG_CHANNEL_CLOSE;
    wr_u32(cl + 1, 0);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_CHANNEL_CLOSE, cl, sizeof(cl)));
    TEST_ASSERT_FALSE(ssh_chan[0][0].open);

    // IGNORE -> no-op.
    uint8_t ign = SSH_MSG_IGNORE;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_IGNORE, &ign, 1));

    // EXT_INFO (inbound) -> ignored.
    uint8_t ext = SSH_MSG_EXT_INFO;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_EXT_INFO, &ext, 1));

    // default (unrecognized) -> UNIMPLEMENTED.
    ssh_pkt[0].seq_no_recv = 3;
    uint8_t unk = 200;
    dsp_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, 200, &unk, 1));
    TEST_ASSERT_EQUAL(SSH_MSG_UNIMPLEMENTED, dsp_type[0]);

    // DISCONNECT -> peer closing.
    uint8_t disc = SSH_MSG_DISCONNECT;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_DISCONNECT, &disc, 1));

    // Out-of-range slot -> rejected before the switch.
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(MAX_SSH_CONNS, SSH_MSG_IGNORE, &ign, 1));

    // Restore the transport-wired emit callback for the remaining byte-pump tests.
    (void)ssh_proto_handler();
}

// The rejection half of every guarded switch arm: wrong-phase, unauthenticated, and
// malformed-payload inputs each return -1 (or drop silently), covering the guard branches
// the happy-path handshake in test_dispatch_all_switch_arms does not.
void test_dispatch_guard_and_error_arms()
{
    ssh_transport_init(0);
    pc_ssh_channel_init(0);
    pc_ssh_auth_set_password_cb(dsp_pw_cb);
    pc_ssh_channel_set_data_cb(dsp_on_chan_data);
    pc_ssh_server_set_emit_cb(dsp_emit);

    SshSession *s = &ssh_sess[0];

    // KEXINIT with a payload too short to negotiate -> parse fails.
    s->phase = SshPhase::SSH_PHASE_KEXINIT;
    uint8_t badkex[4] = {SSH_MSG_KEXINIT, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_KEXINIT, badkex, sizeof(badkex)));

    // KEXDH_INIT outside DH_INIT phase -> rejected.
    s->phase = SshPhase::SSH_PHASE_KEXINIT;
    uint8_t badkexdh[4] = {SSH_MSG_KEXDH_INIT, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_KEXDH_INIT, badkexdh, sizeof(badkexdh)));

    // KEXDH_INIT in DH_INIT phase but malformed -> handler fails.
    s->phase = SshPhase::SSH_PHASE_DH_INIT;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_KEXDH_INIT, badkexdh, sizeof(badkexdh)));

    // SERVICE_REQUEST outside SERVICE phase -> rejected; in phase but truncated -> handler fails.
    s->phase = SshPhase::SSH_PHASE_AUTH;
    uint8_t badsvc[2] = {SSH_MSG_SERVICE_REQUEST, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_SERVICE_REQUEST, badsvc, sizeof(badsvc)));
    s->phase = SshPhase::SSH_PHASE_SERVICE;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_SERVICE_REQUEST, badsvc, sizeof(badsvc)));

    // USERAUTH_REQUEST outside AUTH phase -> rejected; in phase but truncated -> handler fails.
    s->phase = SshPhase::SSH_PHASE_SERVICE;
    uint8_t badua[2] = {SSH_MSG_USERAUTH_REQUEST, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_REQUEST, badua, sizeof(badua)));
    s->phase = SshPhase::SSH_PHASE_AUTH;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_REQUEST, badua, sizeof(badua)));

    // A wrong password below the limit -> FAILURE, connection stays open (limit not tripped).
    s->phase = SshPhase::SSH_PHASE_AUTH;
    s->authed = false;
    s->auth_failures = 0;
    uint8_t pw[128];
    size_t pn = 0;
    pw[pn++] = SSH_MSG_USERAUTH_REQUEST;
    pn += put_string(pw + pn, "alice");
    pn += put_string(pw + pn, "ssh-connection");
    pn += put_string(pw + pn, "password");
    pw[pn++] = 0;
    pn += put_string(pw + pn, "wrong");
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_REQUEST, pw, pn));
    TEST_ASSERT_EQUAL_INT(1, s->auth_failures);

    // Repeated failures trip the brute-force limit: FAILURE then DISCONNECT, return -1.
    s->auth_failures = SSH_MAX_AUTH_ATTEMPTS - 1;
    dsp_reset();
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_REQUEST, pw, pn));
    TEST_ASSERT_EQUAL(SSH_MSG_DISCONNECT, dsp_type[dsp_n - 1]);

    // Every post-auth connection message is rejected while unauthenticated.
    s->authed = false;
    const uint8_t authed_arms[] = {SSH_MSG_GLOBAL_REQUEST,       SSH_MSG_CHANNEL_OPEN,    SSH_MSG_CHANNEL_OPEN_CONFIRM,
                                   SSH_MSG_CHANNEL_OPEN_FAILURE, SSH_MSG_CHANNEL_REQUEST, SSH_MSG_CHANNEL_DATA};
    for (size_t j = 0; j < sizeof(authed_arms) / sizeof(authed_arms[0]); j++)
    {
        uint8_t p[8] = {authed_arms[j], 0, 0, 0, 0, 0, 0, 0};
        TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, authed_arms[j], p, sizeof(p)));
    }

    // Authenticated but malformed -> the arm's handler fails.
    s->authed = true;
    const uint8_t handler_arms[] = {SSH_MSG_GLOBAL_REQUEST, SSH_MSG_CHANNEL_OPEN, SSH_MSG_CHANNEL_REQUEST,
                                    SSH_MSG_CHANNEL_DATA};
    for (size_t j = 0; j < sizeof(handler_arms) / sizeof(handler_arms[0]); j++)
    {
        uint8_t p[2] = {handler_arms[j], 0};
        TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, handler_arms[j], p, sizeof(p)));
    }

    // GLOBAL_REQUEST with want_reply = FALSE for an unknown request -> handled, nothing emitted.
    s->authed = true;
    uint8_t gr[64];
    size_t gn = 0;
    gr[gn++] = SSH_MSG_GLOBAL_REQUEST;
    gn += put_string(gr + gn, "no-such-request@example.com");
    gr[gn++] = 0; // want_reply = FALSE
    dsp_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_GLOBAL_REQUEST, gr, gn));
    TEST_ASSERT_EQUAL_INT(0, dsp_n);

    // With no emit callback wired, a reply-producing dispatch (UNIMPLEMENTED) drops the frame.
    pc_ssh_server_set_emit_cb(nullptr);
    ssh_pkt[0].seq_no_recv = 1;
    uint8_t unk = 201;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, 201, &unk, 1));

    (void)ssh_proto_handler(); // restore the transport emit wiring
}

void test_accept_sends_server_banner()
{
    pc_ssh_conn_accept(0);
    const char *resp = tcp_captured();
    TEST_ASSERT_TRUE(tcp_captured_len() >= 8);
    TEST_ASSERT_EQUAL_MEMORY("SSH-2.0-", resp, 8);
    TEST_ASSERT_NOT_EQUAL(PC_PROTO_SLOT_NONE, conn_pool[0].proto_slot);
}

void test_banner_then_kexinit_advances_and_replies()
{
    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;
    tcp_capture_reset();

    // Client banner followed by a framed KEXINIT, delivered together.
    const char *banner = "SSH-2.0-TestClient\r\n";
    push_bytes(0, (const uint8_t *)banner, strlen(banner));

    uint8_t payload[700];
    size_t plen = build_kexinit_payload(payload);
    uint8_t pkt[768];
    size_t pktlen = frame_packet(pkt, payload, plen);
    push_bytes(0, pkt, pktlen);

    pc_ssh_conn_rx(0);

    // The client identification string was captured.
    TEST_ASSERT_EQUAL_STRING("SSH-2.0-TestClient", ssh_sess[j].v_c);
    // Negotiation succeeded and the server replied + generated its ephemeral.
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_DH_INIT, ssh_sess[j].phase);
    // A server packet (KEXINIT) was written back.
    TEST_ASSERT_TRUE(tcp_captured_len() > 0);
}

void test_poll_triggers_server_rekey()
{
    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;
    // An authenticated, open session that has spent its volume budget (packet-count proxy).
    ssh_sess[j].authed = true;
    ssh_sess[j].phase = SshPhase::SSH_PHASE_OPEN;
    ssh_sess[j].last_kex_ms = 0;
    ssh_pkt[j].kex_active = false;
    ssh_pkt[j].enc_out = true;
    ssh_pkt[j].enc_in = true;
    ssh_pkt[j].seq_no_send = SSH_REKEY_PACKET_THRESHOLD;
    tcp_capture_reset();

    pc_ssh_conn_poll(0);
    // The server emitted a fresh KEXINIT and entered the re-key handshake (RFC 4253 §9).
    TEST_ASSERT_TRUE(tcp_captured_len() > 0);
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_KEXINIT, ssh_sess[j].phase);

    // A poll on an already-re-keying / under-budget session does not fire again.
    tcp_capture_reset();
    pc_ssh_conn_poll(0);
    TEST_ASSERT_EQUAL(0, tcp_captured_len());
}

// The L5 ProtoHandler accessor exposes the installed dispatch table.
void test_proto_handler_accessor()
{
    TEST_ASSERT_NOT_NULL(ssh_proto_handler());
}

// Regression (SSH KEX stall, HW-found 2026-07-11): installing the SSH handler must also wire the
// dispatcher's binary-packet emit callback. Before the fix, pc_ssh_conn_setup() had no production caller,
// so the server parsed the client KEXINIT but silently dropped its reply (emit callback null) and the
// handshake died on the idle timeout. ssh_proto_handler() is the single install seam, so requesting it
// must leave the emit path live even if the callback had been cleared.
void test_proto_handler_wires_emit()
{
    pc_ssh_server_set_emit_cb(nullptr); // simulate a dispatcher with no emit callback wired
    (void)ssh_proto_handler();          // the one install seam must (re)wire it

    pc_ssh_conn_accept(0);
    const char *banner = "SSH-2.0-TestClient\r\n";
    push_bytes(0, (const uint8_t *)banner, strlen(banner));
    uint8_t payload[700];
    size_t plen = build_kexinit_payload(payload);
    uint8_t pkt[768];
    size_t pktlen = frame_packet(pkt, payload, plen);
    push_bytes(0, pkt, pktlen);
    tcp_capture_reset();
    pc_ssh_conn_rx(0);
    // The server's KEXINIT reply reached the socket: proves ssh_proto_handler() wired ssh_emit.
    TEST_ASSERT_TRUE(tcp_captured_len() > 0);
}

// The channel-send entry points fail closed on an out-of-range slot and on a mapped
// slot whose TCP conn has lost its pcb.
void test_send_entrypoints_reject()
{
    uint8_t data[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_send(250, 0, data, sizeof(data)));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_close_channel(250, 0));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_open_forwarded(250, "h", 22, "o", 1));

    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;
    conn_pool[0].pcb = nullptr; // live SSH slot but the socket is gone
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_send(j, 0, data, sizeof(data)));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_close_channel(j, 0));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_open_forwarded(j, "h", 22, "o", 1));
}

// poll and rx ignore a live conn that is not a mapped SSH session, and rx on an empty
// ring reads nothing; a partial client banner leaves the session in the banner phase.
void test_poll_rx_banner_guards()
{
    pc_ssh_conn_poll(1); // conn 1 is ACTIVE but never accepted (proto_slot == PC_NONE)
    pc_ssh_conn_rx(1);

    pc_ssh_conn_accept(0);
    pc_ssh_conn_rx(0); // empty rx ring -> reads nothing

    const char *partial = "SSH-2.0-Incomplete"; // no CRLF yet
    push_bytes(0, (const uint8_t *)partial, strlen(partial));
    pc_ssh_conn_rx(0);
    uint8_t j = conn_pool[0].proto_slot;
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_BANNER, ssh_sess[j].phase);
}

// With an open channel (white-box), pc_ssh_conn_send frames CHANNEL_DATA to the socket;
// pc_ssh_conn_close_channel frames EOF+CLOSE; and pc_ssh_conn_open_forwarded opens a
// server-initiated forwarded-tcpip channel.
void test_conn_send_close_open_channel()
{
    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;
    ssh_chan[j][0].open = true;
    ssh_chan[j][0].local_id = 0;
    ssh_chan[j][0].peer_id = 1;
    ssh_chan[j][0].flow.peer_window = 100000;
    ssh_chan[j][0].flow.peer_max_pkt = 100000;

    const uint8_t data[5] = {'h', 'e', 'l', 'l', 'o'};
    tcp_capture_reset();
    TEST_ASSERT_EQUAL_INT(5, pc_ssh_conn_send(j, 0, data, sizeof(data)));
    TEST_ASSERT_TRUE(tcp_captured_len() > 0);

    ssh_chan[j][0].open = true; // pc_ssh_conn_send left it open
    tcp_capture_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_conn_close_channel(j, 0));
    TEST_ASSERT_TRUE(tcp_captured_len() > 0);

    // The channel slot is free again -> a server-initiated forwarded-tcpip open succeeds.
    tcp_capture_reset();
    TEST_ASSERT_TRUE(pc_ssh_conn_open_forwarded(j, "10.0.0.1", 80, "192.168.0.9", 5000) >= 0);
    TEST_ASSERT_TRUE(tcp_captured_len() > 0);
}

// Channel-layer rejections propagate as -1: closing a channel that is not open,
// sending more than the peer window allows, and opening a forwarded channel when the
// (sole) channel slot is already occupied.
void test_send_channel_reject_paths()
{
    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;

    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_close_channel(j, 0)); // channel 0 is not open -> build_close fails

    ssh_chan[j][0].open = true;
    ssh_chan[j][0].flow.peer_window = 2;
    ssh_chan[j][0].flow.peer_max_pkt = 100000;
    const uint8_t data[5] = {1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_send(j, 0, data, sizeof(data))); // 5 > peer window 2

    // The sole channel slot is occupied -> no room for a server-initiated forward.
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_open_forwarded(j, "h", 22, "o", 1));
}

// The only SSH slot (MAX_SSH_CONNS == 1) is consumed by the first accept; a second
// connection has no capacity and is dropped without a slot assignment.
void test_accept_no_ssh_capacity()
{
    pc_ssh_conn_accept(0);
    TEST_ASSERT_NOT_EQUAL(PC_PROTO_SLOT_NONE, conn_pool[0].proto_slot);
    pc_ssh_conn_accept(1);
    TEST_ASSERT_EQUAL(PC_PROTO_SLOT_NONE, conn_pool[1].proto_slot);
}

// poll on a non-ACTIVE connection returns at the state guard.
void test_poll_ignores_inactive_conn()
{
    conn_pool[2].state = ConnState::CONN_CLOSING;
    pc_ssh_conn_poll(2);
    TEST_ASSERT_EQUAL(ConnState::CONN_CLOSING, (ConnState)conn_pool[2].state); // untouched
}

// A DISCONNECT after the banner is fatal: pc_ssh_server_dispatch returns <0, so
// ssh_msg_handler flags the slot and pc_ssh_conn_rx tears the connection down.
void test_rx_disconnect_tears_down()
{
    pc_ssh_conn_accept(0);
    const char *banner = "SSH-2.0-TestClient\r\n";
    push_bytes(0, (const uint8_t *)banner, strlen(banner));
    uint8_t disc[13] = {SSH_MSG_DISCONNECT, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0}; // reason 11, empty desc+lang
    uint8_t pkt[64];
    size_t pktlen = frame_packet(pkt, disc, sizeof(disc));
    push_bytes(0, pkt, pktlen);
    pc_ssh_conn_rx(0);
    TEST_ASSERT_EQUAL(PC_PROTO_SLOT_NONE, conn_pool[0].proto_slot); // slot released
}

// A client identification line that runs past SSH_VERSION_MAX with no CRLF is rejected
// by recv_banner, and pc_ssh_conn_rx closes the connection.
void test_rx_overlong_banner_closes()
{
    pc_ssh_conn_accept(0);
    uint8_t longline[300];
    memset(longline, 'A', sizeof(longline)); // >= SSH_VERSION_MAX (256), no CRLF
    push_bytes(0, longline, sizeof(longline));
    pc_ssh_conn_rx(0);
    TEST_ASSERT_EQUAL(PC_PROTO_SLOT_NONE, conn_pool[0].proto_slot); // slot released
}

static void drain_arena()
{
    scratch_reset();
    while (scratch_alloc(256, 1))
    {
    }
}

// Every outbound SSH function fails closed when the shared scratch arena is exhausted:
// the wire/payload borrow returns null and the message is dropped.
void test_conn_outbound_arena_exhausted()
{
    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;
    ssh_chan[j][0].open = true;
    ssh_chan[j][0].local_id = 0;
    ssh_chan[j][0].peer_id = 1;
    ssh_chan[j][0].flow.peer_window = 100000;
    ssh_chan[j][0].flow.peer_max_pkt = 100000;
    const uint8_t data[3] = {1, 2, 3};

    drain_arena();
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_send(j, 0, data, sizeof(data)));     // payload/wire alloc fails
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_close_channel(j, 0));                // wire alloc fails
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_open_forwarded(j, "h", 22, "o", 1)); // payload/wire alloc fails
    scratch_reset();
}

// Every outbound SSH function fails closed when the packet layer refuses to send (the send
// sequence number has reached the close threshold, RFC 4253 rekey/overflow guard).
void test_conn_outbound_pkt_send_fails()
{
    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;
    ssh_chan[j][0].open = true;
    ssh_chan[j][0].local_id = 0;
    ssh_chan[j][0].peer_id = 1;
    ssh_chan[j][0].flow.peer_window = 100000;
    ssh_chan[j][0].flow.peer_max_pkt = 100000;
    ssh_pkt[j].seq_no_send = SSH_SEQ_CLOSE_THRESHOLD;
    const uint8_t data[3] = {1, 2, 3};

    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_send(j, 0, data, sizeof(data))); // build_data ok, pkt_send fails
    ssh_chan[j][0].open = true;                                            // still open for the close
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_close_channel(j, 0));            // build_close ok, pkt_send fails
    ssh_chan[j][0].open = false;                                           // free the slot for a forwarded open
    TEST_ASSERT_EQUAL_INT(
        -1, pc_ssh_conn_open_forwarded(j, "10.0.0.1", 80, "192.168.0.9", 5000)); // open ok, pkt_send fails
}

// The poll-driven server re-key emits via ssh_emit, which fails closed when the packet
// layer refuses (sequence threshold) and when the arena is exhausted.
void test_poll_rekey_emit_fails()
{
    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;
    ssh_sess[j].phase = SshPhase::SSH_PHASE_OPEN;
    ssh_pkt[j].kex_active = false;
    ssh_sess[j].last_kex_ms = 0;
    ssh_pkt[j].seq_no_send = SSH_SEQ_CLOSE_THRESHOLD; // rekey due; ssh_emit's pkt_send then fails
    pc_ssh_conn_poll(0);

    ssh_sess[j].phase = SshPhase::SSH_PHASE_OPEN;
    ssh_pkt[j].kex_active = false;
    ssh_pkt[j].seq_no_send = SSH_REKEY_PACKET_THRESHOLD; // rekey due; ssh_emit's arena borrow then fails
    drain_arena();
    pc_ssh_conn_poll(0);
    scratch_reset();
    TEST_PASS();
}

// A slot that is in range but mapped to no TCP connection (never accepted, or already closed) is
// refused by every outbound entry point.
void test_conn_entrypoints_reject_unmapped_slot()
{
    const uint8_t data[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_send(0, 0, data, sizeof(data)));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_close_channel(0, 0));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_open_forwarded(0, "h", 22, "o", 1));
}

// The framed wire form is larger than the payload it carries, so an arena with room for the
// payload but not for the wire buffer must still fail closed rather than emit a partial packet.
void test_conn_outbound_arena_fits_payload_not_wire()
{
    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;
    ssh_chan[j][0].open = true;
    ssh_chan[j][0].local_id = 0;
    ssh_chan[j][0].peer_id = 1;
    ssh_chan[j][0].flow.peer_window = 100000;
    ssh_chan[j][0].flow.peer_max_pkt = 100000;

    scratch_reset();
    while (scratch_capacity() - scratch_used() >= SSH_WIRE_CAP)
    {
        TEST_ASSERT_NOT_NULL(scratch_alloc(16, 1));
    }
    TEST_ASSERT_TRUE(scratch_capacity() - scratch_used() >= SSH_PKT_BUF_SIZE);

    const uint8_t data[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_send(j, 0, data, sizeof(data)));
    ssh_chan[j][0].open = false; // free the sole channel slot for a server-initiated open
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_conn_open_forwarded(j, "10.0.0.1", 80, "1.2.3.4", 90));
    scratch_reset();
}

// The receive path drains the ring without asking whether the socket is still sendable, so the
// dispatcher's emit callback has to: a reply produced for a connection whose pcb died between the
// read and the write is dropped instead of being handed to the transport.
void test_conn_emit_drops_reply_on_dead_socket()
{
    pc_ssh_conn_accept(0);
    const char *banner = "SSH-2.0-TestClient\r\n";
    push_bytes(0, (const uint8_t *)banner, strlen(banner));
    uint8_t payload[700];
    size_t plen = build_kexinit_payload(payload);
    uint8_t pkt[768];
    size_t pktlen = frame_packet(pkt, payload, plen);
    push_bytes(0, pkt, pktlen);

    conn_pool[0].pcb = nullptr; // socket gone; the client's bytes are still in the ring
    tcp_capture_reset();
    pc_ssh_conn_rx(0);
    TEST_ASSERT_EQUAL(0, tcp_captured_len()); // the KEXINIT reply never reached the socket
    uint8_t j = conn_pool[0].proto_slot;
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_DH_INIT, ssh_sess[j].phase); // ...but it was negotiated
}

// poll and rx ignore a connection whose proto_slot names an SSH session that belongs to a
// different TCP slot (a stale mapping left behind by a reused slot).
void test_conn_poll_rx_foreign_slot_mapping()
{
    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;
    conn_pool[1].proto_slot = j; // conn 1 points at conn 0's session
    const char *banner = "SSH-2.0-Other\r\n";
    push_bytes(1, (const uint8_t *)banner, strlen(banner));
    tcp_capture_reset();
    pc_ssh_conn_poll(1);
    pc_ssh_conn_rx(1);
    TEST_ASSERT_EQUAL(0, tcp_captured_len());
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_BANNER, ssh_sess[j].phase); // conn 0's session untouched
    conn_pool[1].proto_slot = PC_PROTO_SLOT_NONE;
}

// The poll-driven re-key fires only for an open session that is not already re-keying AND has
// actually spent its volume / time budget.
void test_conn_poll_rekey_preconditions()
{
    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;
    ssh_sess[j].phase = SshPhase::SSH_PHASE_OPEN;
    ssh_sess[j].last_kex_ms = 0;
    ssh_pkt[j].seq_no_send = 0;
    ssh_pkt[j].seq_no_recv = 0;

    ssh_pkt[j].kex_active = true; // a key exchange is already in flight
    tcp_capture_reset();
    pc_ssh_conn_poll(0);
    TEST_ASSERT_EQUAL(0, tcp_captured_len());

    ssh_pkt[j].kex_active = false; // open and idle, but nothing has been spent yet
    pc_ssh_conn_poll(0);
    TEST_ASSERT_EQUAL(0, tcp_captured_len());
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_OPEN, ssh_sess[j].phase);
}

// The identification banner is only written when the socket is genuinely sendable: neither a
// missing pcb nor a non-ACTIVE state produces a write, and the session is set up either way.
void test_conn_accept_skips_banner_on_dead_socket()
{
    conn_pool[0].pcb = nullptr; // ACTIVE, but no pcb
    tcp_capture_reset();
    pc_ssh_conn_accept(0);
    TEST_ASSERT_NOT_EQUAL(PC_PROTO_SLOT_NONE, conn_pool[0].proto_slot);
    TEST_ASSERT_EQUAL(0, tcp_captured_len());
    pc_ssh_conn_close(0);

    conn_pool[1].state = ConnState::CONN_CLOSING; // pcb present, wrong state
    tcp_capture_reset();
    pc_ssh_conn_accept(1);
    TEST_ASSERT_EQUAL(0, tcp_captured_len());
    pc_ssh_conn_close(1);
    conn_pool[1].state = ConnState::CONN_ACTIVE;
}

// The banner is consumed exactly: a read carrying only the identification line leaves nothing for
// the packet layer, and the NEXT read starts straight at the binary-packet path.
void test_conn_rx_banner_then_packet_in_separate_reads()
{
    pc_ssh_conn_accept(0);
    uint8_t j = conn_pool[0].proto_slot;
    const char *banner = "SSH-2.0-TestClient\r\n";
    push_bytes(0, (const uint8_t *)banner, strlen(banner));
    tcp_capture_reset();
    pc_ssh_conn_rx(0);
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_KEXINIT, ssh_sess[j].phase);
    TEST_ASSERT_EQUAL(0, tcp_captured_len()); // nothing followed the banner in that read

    uint8_t payload[700];
    size_t plen = build_kexinit_payload(payload);
    uint8_t pkt[768];
    size_t pktlen = frame_packet(pkt, payload, plen);
    push_bytes(0, pkt, pktlen);
    pc_ssh_conn_rx(0); // already past the banner phase
    TEST_ASSERT_EQUAL(SshPhase::SSH_PHASE_DH_INIT, ssh_sess[j].phase);
    TEST_ASSERT_TRUE(tcp_captured_len() > 0);
}

// bn_monpro()'s final correction guard (pc_bignum.cpp) is "if (t[128] || raw>=p)". The
// t[128] half (raw overflowed past 2^2048) is exercised by ordinary DH handshakes elsewhere
// in this suite; the raw>=p half, reached with t[128] still 0, is not - group14_p's top 64
// bits are all 1 (p sits within ~2^1984 of 2^2048), so for operands that land the raw SOS
// value roughly uniformly across [0, 2p) that "still under 2^2048 but over p" window is only
// about 2^-65 of the range and a real handshake essentially never lands in it. It is reachable
// with a deliberately chosen operand though: base = R^-1 mod p (R = 2^2048, computed
// out-of-band with Python's pow(R,-1,p)) makes the very first MonPro call inside
// bn_expmod_group14 - MonPro(base, R^2 mod p), which converts base into Montgomery form -
// compute base*R mod p = 1 exactly, so its pre-correction raw value is exactly p+1: under
// 2^2048 (t[128]==0) but over p (raw>=p true). base is itself a legal DH public value (1 <
// base < p-1, checked below), so this is well within what a real peer could send as e/f. With
// exp=1 the expected output is base itself (base^1 mod p == base), so this checks exact
// correctness, not just that the branch runs.
void test_bn_expmod_group14_hits_correction_sliver_without_overflow_limb(void)
{
    static const char BASE_RINV_MOD_P[] = "f1d8005ff5b8c4112ffb39750e04f8f47b9f13a8b88bc0d138357328129d2c3f"
                                          "4fb076749f1093dafcf49f0dc56104d33e5d407469d9aa3c469dd452656c4b8c"
                                          "561ac43e5c47f020451eaf1b6d6b588b8369d0482fd5e6c8281582ff0f06d4e4"
                                          "3f217fc36c8d15ae7ad34029dccfedd510771b76e4cd91bb6394e97b82ad74c6"
                                          "2d747f72c59261cd43e22ff42b9c2053713e5fb11c276327a72545b06b5b32ce"
                                          "64efadef56a4345a22159875387974238f38ab5dd67c6e91116096a36ddd96a0"
                                          "f2111f35f58b216b80289086b09148f6dc5436edb441bc5ba41f0256a7f399da"
                                          "eae00940b306a2b0ed48a96bbcbc056c804cb001a0de6e7ef5a04400c911dbc3";

    uint8_t base_be[256];
    hex2bytes(base_be, BASE_RINV_MOD_P, 256);
    pc_bignum base;
    bn_from_bytes(&base, base_be, 256);

    // Sanity: base must satisfy the documented bn_expmod_group14() precondition (RFC 4253 §8:
    // a real peer's e/f is validated the same way before it ever reaches this function).
    TEST_ASSERT_EQUAL_INT(0, bn_dh_validate(&base));

    pc_bignum exp;
    memset(exp.d, 0, sizeof(exp.d));
    exp.d[0] = 1; // exponent = 1 -> out must equal base exactly

    pc_bignum out;
    bn_expmod_group14(&out, &base, &exp);

    TEST_ASSERT_EQUAL_INT(0, bn_cmp(&out, &base));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_conn_entrypoints_reject_unmapped_slot);
    RUN_TEST(test_conn_outbound_arena_exhausted);
    RUN_TEST(test_conn_outbound_arena_fits_payload_not_wire);
    RUN_TEST(test_conn_emit_drops_reply_on_dead_socket);
    RUN_TEST(test_conn_poll_rx_foreign_slot_mapping);
    RUN_TEST(test_conn_poll_rekey_preconditions);
    RUN_TEST(test_conn_accept_skips_banner_on_dead_socket);
    RUN_TEST(test_conn_rx_banner_then_packet_in_separate_reads);
    RUN_TEST(test_conn_outbound_pkt_send_fails);
    RUN_TEST(test_poll_rekey_emit_fails);
    RUN_TEST(test_accept_sends_server_banner);
    RUN_TEST(test_banner_then_kexinit_advances_and_replies);
    RUN_TEST(test_poll_triggers_server_rekey);
    RUN_TEST(test_proto_handler_accessor);
    RUN_TEST(test_proto_handler_wires_emit);
    RUN_TEST(test_send_entrypoints_reject);
    RUN_TEST(test_poll_rx_banner_guards);
    RUN_TEST(test_conn_send_close_open_channel);
    RUN_TEST(test_send_channel_reject_paths);
    RUN_TEST(test_accept_no_ssh_capacity);
    RUN_TEST(test_poll_ignores_inactive_conn);
    RUN_TEST(test_rx_disconnect_tears_down);
    RUN_TEST(test_rx_overlong_banner_closes);
    RUN_TEST(test_bn_expmod_group14_hits_correction_sliver_without_overflow_limb);
    RUN_TEST(test_dispatch_all_switch_arms);
    RUN_TEST(test_dispatch_guard_and_error_arms);
    return UNITY_END();
}
