// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// End-to-end SSH server dispatcher test: drives a full handshake
// (KEXINIT → KEXDH → NEWKEYS → userauth → channel) through pc_ssh_server_dispatch
// and checks the emitted messages and resulting state at each step.

#include "crypto/mac/hmac_sha256.h" // hand-built ETM / E&M packets
#include "mmgr/plaintext.h"         // arena-exhaustion (fail-closed) packet paths
#include "network_drivers/presentation/ssh/auth/ssh_auth.h"
#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_server.h"
#include "network_drivers/presentation/ssh/transport/ssh_dh.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include "network_drivers/tls/ssh_rsa.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

extern uint8_t _test_rsa_n[256];
extern uint8_t _test_rsa_d[256];
extern uint8_t _test_rsa_e[4];

// ---- emit recorder --------------------------------------------------------

static uint8_t emt_type[32];
static int emt_n;
static uint8_t emt_last[64]; // bytes of the most recent emit (for payload checks)
static size_t emt_last_len;
static void rec_emit(uint8_t slot, const uint8_t *p, size_t n)
{
    (void)slot;
    if (emt_n < 32 && n > 0)
    {
        emt_type[emt_n++] = p[0];
    }
    emt_last_len = n < sizeof(emt_last) ? n : sizeof(emt_last);
    memcpy(emt_last, p, emt_last_len);
}
static void emt_reset()
{
    emt_n = 0;
}

// ---- channel data recorder ------------------------------------------------

static int chan_data_count;
static uint8_t chan_data[64];
static size_t chan_data_len;
static void on_chan_data(uint8_t slot, uint32_t channel, const uint8_t *d, size_t n)
{
    (void)slot;
    (void)channel;
    chan_data_count++;
    chan_data_len = n < sizeof(chan_data) ? n : sizeof(chan_data);
    memcpy(chan_data, d, chan_data_len);
}

static proto_bool pw_cb(const char *u, const char *p)
{
    return strcmp(u, "alice") == 0 && strcmp(p, "s3cret") == 0;
}

void setUp()
{
    ssh_transport_init(0);
    pc_ssh_channel_init(0);
    emt_reset();
    chan_data_count = 0;
    chan_data_len = 0;

    memset(_test_rsa_n, 0, 256);
    _test_rsa_n[0] = 0xFF;
    _test_rsa_n[255] = 0x01;
    memset(_test_rsa_d, 0, 256);
    _test_rsa_d[255] = 0x01;
    _test_rsa_e[0] = 0;
    _test_rsa_e[1] = 1;
    _test_rsa_e[2] = 0;
    _test_rsa_e[3] = 1;
    pc_ssh_rsa_load_pubkey();

    pc_ssh_auth_set_password_cb(pw_cb);
    pc_ssh_channel_set_data_cb(on_chan_data);
    pc_ssh_server_set_emit_cb(rec_emit);
}
void tearDown()
{
}

// ---- wire builders --------------------------------------------------------

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
static uint32_t rd_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static size_t put_mpint(uint8_t *p, const uint8_t *be, size_t len)
{
    size_t off = 0;
    while (off < len && be[off] == 0)
    {
        off++;
    }
    proto_bool pad = (off < len) && (be[off] & 0x80u);
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

static size_t put_namelist(uint8_t *p, const char *s)
{
    return put_string(p, s);
}

static size_t build_client_kexinit(uint8_t *out, proto_bool ext_info_c = PROTO_TRUE)
{
    size_t o = 0;
    out[o++] = SSH_MSG_KEXINIT;
    for (int j = 0; j < 16; j++)
    {
        out[o++] = (uint8_t)j;
    }
    o += put_namelist(out + o,
                      ext_info_c ? "diffie-hellman-group14-sha256,ext-info-c" // RFC 8308
                                 : "diffie-hellman-group14-sha256");
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

// ---- the full handshake ---------------------------------------------------

void test_full_handshake_to_channel_data()
{
    SshSession *s = &ssh_sess[0];
    // Banner exchange already done out-of-band; seed V_C and enter KEXINIT.
    strcpy(s->v_c, "SSH-2.0-TestClient");
    s->v_c_len = (uint16_t)strlen(s->v_c);
    s->phase = SSH_PHASE_KEXINIT;

    // 1. Client KEXINIT → server replies KEXINIT, generates ephemeral.
    uint8_t pkt[2048];
    size_t n = build_client_kexinit(pkt);
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_EQUAL_INT(1, emt_n);
    TEST_ASSERT_EQUAL(SSH_MSG_KEXINIT, emt_type[0]);
    TEST_ASSERT_EQUAL(SSH_PHASE_DH_INIT, s->phase);

    // 2. KEXDH_INIT (e = 2) → KEXDH_REPLY + NEWKEYS.
    uint8_t e_be[256];
    memset(e_be, 0, sizeof(e_be));
    e_be[255] = 0x02;
    n = 0;
    pkt[n++] = SSH_MSG_KEXDH_INIT;
    n += put_mpint(pkt + n, e_be, 256);
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_EQUAL_INT(2, emt_n);
    TEST_ASSERT_EQUAL(SSH_MSG_KEXDH_REPLY, emt_type[0]);
    TEST_ASSERT_EQUAL(SSH_MSG_NEWKEYS, emt_type[1]);
    TEST_ASSERT_TRUE(ssh_keys[0].active);
    // Sending our NEWKEYS activated the outbound direction; inbound waits for the client's NEWKEYS.
    TEST_ASSERT_TRUE(ssh_pkt[0].enc_out);
    TEST_ASSERT_FALSE(ssh_pkt[0].enc_in);

    // 3. Client NEWKEYS → encryption active, service phase. Because the client
    //    KEXINIT advertised ext-info-c, the server now sends EXT_INFO (RFC 8308).
    uint8_t nk = SSH_MSG_NEWKEYS;
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, nk, &nk, 1));
    TEST_ASSERT_TRUE(ssh_pkt[0].enc_in && ssh_pkt[0].enc_out); // both directions now encrypted
    TEST_ASSERT_EQUAL(SSH_PHASE_SERVICE, s->phase);
    TEST_ASSERT_EQUAL_INT(1, emt_n);
    TEST_ASSERT_EQUAL(SSH_MSG_EXT_INFO, emt_type[0]);

    // 4. SERVICE_REQUEST → SERVICE_ACCEPT, auth phase.
    n = 0;
    pkt[n++] = SSH_MSG_SERVICE_REQUEST;
    n += put_string(pkt + n, "ssh-userauth");
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_EQUAL(SSH_MSG_SERVICE_ACCEPT, emt_type[0]);
    TEST_ASSERT_EQUAL(SSH_PHASE_AUTH, s->phase);

    // 5. USERAUTH_REQUEST (password) → SUCCESS, open phase.
    n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(pkt + n, "alice");
    n += put_string(pkt + n, "ssh-connection");
    n += put_string(pkt + n, "password");
    pkt[n++] = 0;
    n += put_string(pkt + n, "s3cret");
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_SUCCESS, emt_type[0]);
    TEST_ASSERT_TRUE(s->authed);
    TEST_ASSERT_EQUAL(SSH_PHASE_OPEN, s->phase);

    // 6. CHANNEL_OPEN (session) → CONFIRMATION.
    n = 0;
    pkt[n++] = SSH_MSG_CHANNEL_OPEN;
    n += put_string(pkt + n, "session");
    wr_u32(pkt + n, 11);
    wr_u32(pkt + n + 4, 4096);
    wr_u32(pkt + n + 8, 32768);
    n += 12;
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_EQUAL(SSH_MSG_CHANNEL_OPEN_CONFIRM, emt_type[0]);
    TEST_ASSERT_TRUE(ssh_chan[0][0].open);

    // 7. CHANNEL_REQUEST (shell, want_reply) → SUCCESS.
    n = 0;
    pkt[n++] = SSH_MSG_CHANNEL_REQUEST;
    wr_u32(pkt + n, 0);
    n += 4;
    n += put_string(pkt + n, "shell");
    pkt[n++] = 1;
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_EQUAL(SSH_MSG_CHANNEL_SUCCESS, emt_type[0]);

    // 8. CHANNEL_DATA → application callback receives the bytes.
    n = 0;
    pkt[n++] = SSH_MSG_CHANNEL_DATA;
    wr_u32(pkt + n, 0);
    n += 4;
    n += put_string(pkt + n, "hi");
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_EQUAL_INT(1, chan_data_count);
    TEST_ASSERT_EQUAL_INT(2, (int)chan_data_len);
    TEST_ASSERT_EQUAL_MEMORY("hi", chan_data, 2);
}

void test_channel_open_before_auth_rejected()
{
    SshSession *s = &ssh_sess[0];
    s->authed = PROTO_FALSE;
    s->phase = SSH_PHASE_SERVICE;
    uint8_t pkt[64];
    size_t n = 0;
    pkt[n++] = SSH_MSG_CHANNEL_OPEN;
    n += put_string(pkt + n, "session");
    wr_u32(pkt + n, 1);
    wr_u32(pkt + n + 4, 4096);
    wr_u32(pkt + n + 8, 32768);
    n += 12;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
}

// Regression (pentest ssh_msgtype_abuse, HW-found 2026-07-11): a SERVICE_REQUEST before the key
// exchange completes must be rejected. Without the phase guard a client could jump from DH_INIT straight
// to userauth in cleartext, skipping KEX + host-key verification entirely (RFC 4253 §10).
void test_service_request_before_newkeys_rejected()
{
    SshSession *s = &ssh_sess[0];
    s->phase = SSH_PHASE_DH_INIT; // mid key-exchange; NEWKEYS not received, encryption not active
    uint8_t pkt[64];
    size_t n = 0;
    pkt[n++] = SSH_MSG_SERVICE_REQUEST;
    n += put_string(pkt + n, "ssh-userauth");
    emt_reset();
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_NOT_EQUAL(SSH_PHASE_AUTH, s->phase); // must NOT have advanced to userauth
}

void test_disconnect_closes()
{
    uint8_t pkt[1] = {SSH_MSG_DISCONNECT};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, pkt[0], pkt, 1));
}

void test_ignore_is_noop()
{
    uint8_t pkt[1] = {SSH_MSG_IGNORE};
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, 1));
    TEST_ASSERT_EQUAL_INT(0, emt_n);
}

// Build a password USERAUTH_REQUEST with the given (wrong/right) password.
static size_t build_password_auth(uint8_t *p, const char *user, const char *pass)
{
    size_t n = 0;
    p[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(p + n, user);
    n += put_string(p + n, "ssh-connection");
    n += put_string(p + n, "password");
    p[n++] = 0; // change-password boolean = FALSE
    n += put_string(p + n, pass);
    return n;
}

// Repeated auth failures must be bounded: after SSH_MAX_AUTH_ATTEMPTS failed
// USERAUTH_REQUESTs the server sends SSH_MSG_DISCONNECT and signals close (-1).
void test_auth_bruteforce_disconnect()
{
    SshSession *s = &ssh_sess[0];
    s->phase = SSH_PHASE_AUTH;
    s->authed = PROTO_FALSE;
    s->auth_failures = 0;

    uint8_t pkt[128];
    size_t n = build_password_auth(pkt, "alice", "wrong");

    // The first SSH_MAX_AUTH_ATTEMPTS-1 failures keep the connection open.
    for (int k = 0; k < SSH_MAX_AUTH_ATTEMPTS - 1; k++)
    {
        emt_reset();
        TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
        TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, emt_type[0]);
        TEST_ASSERT_FALSE(s->authed);
    }
    TEST_ASSERT_EQUAL_INT(SSH_MAX_AUTH_ATTEMPTS - 1, s->auth_failures);

    // The final failure trips the limit: FAILURE then DISCONNECT, return -1.
    emt_reset();
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_EQUAL_INT(2, emt_n);
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, emt_type[0]);
    TEST_ASSERT_EQUAL(SSH_MSG_DISCONNECT, emt_type[1]);
}

// A successful auth before the limit does not count toward the failure budget.
void test_auth_success_after_failures()
{
    SshSession *s = &ssh_sess[0];
    s->phase = SSH_PHASE_AUTH;
    s->authed = PROTO_FALSE;
    s->auth_failures = 0;

    uint8_t pkt[128];
    size_t n = build_password_auth(pkt, "alice", "wrong");
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, emt_type[0]);

    n = build_password_auth(pkt, "alice", "s3cret");
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_SUCCESS, emt_type[0]);
    TEST_ASSERT_TRUE(s->authed);
    TEST_ASSERT_EQUAL_INT(1, s->auth_failures); // unchanged by the success
}

// An unrecognized message gets SSH_MSG_UNIMPLEMENTED with the rejected packet's
// sequence number (RFC 4253 §11.4). seq_no_recv has already advanced past it.
void test_unimplemented_reply_for_unknown_message()
{
    ssh_pkt_init(0);
    ssh_pkt[0].seq_no_recv = 7; // pretend recv() just processed packet #6
    emt_reset();
    uint8_t pkt[1] = {200}; // 200 is not a handled message type
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, 1));
    TEST_ASSERT_EQUAL(SSH_MSG_UNIMPLEMENTED, emt_type[0]);
    TEST_ASSERT_EQUAL_INT(5, (int)emt_last_len);
    uint32_t seq = ((uint32_t)emt_last[1] << 24) | ((uint32_t)emt_last[2] << 16) | ((uint32_t)emt_last[3] << 8) |
                   (uint32_t)emt_last[4];
    TEST_ASSERT_EQUAL_UINT32(6, seq); // rejected packet = seq_no_recv - 1
}

// An inbound CHANNEL_CLOSE must be answered with CHANNEL_EOF and CHANNEL_CLOSE as
// two separate binary packets (RFC 4253 6) - not both bytes in one packet, which a
// strict peer (openssh packet_check_eom()) rejects.
void test_inbound_close_emits_eof_then_close_separately()
{
    // Open a channel so the close path has something to close (peer id 21).
    uint8_t op[64];
    size_t on = 0;
    op[on++] = SSH_MSG_CHANNEL_OPEN;
    on += put_string(op + on, "session");
    wr_u32(op + on, 21);
    wr_u32(op + on + 4, 4096);
    wr_u32(op + on + 8, 32768);
    on += 12;
    uint8_t obuf[64];
    size_t ol = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_channel_handle_open(0, op, on, obuf, &ol, sizeof(obuf)));

    uint8_t pkt[8];
    pkt[0] = SSH_MSG_CHANNEL_CLOSE;
    wr_u32(pkt + 1, 0); // recipient = local channel 0
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_CHANNEL_CLOSE, pkt, 5));
    TEST_ASSERT_EQUAL_INT(2, emt_n); // two distinct binary packets, not one
    TEST_ASSERT_EQUAL(SSH_MSG_CHANNEL_EOF, emt_type[0]);
    TEST_ASSERT_EQUAL(SSH_MSG_CHANNEL_CLOSE, emt_type[1]);
    TEST_ASSERT_FALSE(ssh_chan[0][0].open);
}

// RFC 8308: EXT_INFO advertises server-sig-algs listing every client public-key signature
// algorithm the server can verify (rsa-sha2-512, rsa-sha2-256, ecdsa-sha2-nistp256, ssh-ed25519,
// ordered by the negotiation preference) so a modern OpenSSH client picks a key type it can offer.
void test_extinfo_build_advertises_server_sig_algs()
{
    ssh_kex_set_prefer_rsa(PROTO_TRUE); // deterministic ordering: rsa first
    uint8_t out[128];
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_extinfo_build(out, &n, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_EXT_INFO, out[0]);
    TEST_ASSERT_EQUAL_UINT32(1u, rd_u32(out + 1));  // one extension
    TEST_ASSERT_EQUAL_UINT32(15u, rd_u32(out + 5)); // strlen("server-sig-algs")
    TEST_ASSERT_EQUAL_MEMORY("server-sig-algs", out + 9, 15);
    size_t off = 9 + 15;
    const char *want = "rsa-sha2-512,rsa-sha2-256,ecdsa-sha2-nistp256,ssh-ed25519";
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(want), rd_u32(out + off));
    TEST_ASSERT_EQUAL_MEMORY(want, out + off + 4, strlen(want));
}

// A real OpenSSH client KEXINIT is ~1.5 KB; the parser must accept one well past
// the old 512-byte bound (a smaller bound reset the connection at key exchange).
void test_large_client_kexinit_accepted()
{
    uint8_t pkt[2048];
    size_t o = 0;
    pkt[o++] = SSH_MSG_KEXINIT;
    for (int j = 0; j < 16; j++)
    {
        pkt[o++] = 0; // cookie
    }

    char kex[1200];
    size_t k = 0;
    for (int j = 0; j < 40; j++)
    {
        k += (size_t)sprintf(kex + k, "filler-alg-%02d@example.com,", j);
    }
    k += (size_t)sprintf(kex + k, "diffie-hellman-group14-sha256,ext-info-c");
    o += put_namelist(pkt + o, kex);
    o += put_namelist(pkt + o, "rsa-sha2-256");
    o += put_namelist(pkt + o, "aes256-ctr");
    o += put_namelist(pkt + o, "aes256-ctr");
    o += put_namelist(pkt + o, "hmac-sha2-256");
    o += put_namelist(pkt + o, "hmac-sha2-256");
    o += put_namelist(pkt + o, "none");
    o += put_namelist(pkt + o, "none");
    o += put_namelist(pkt + o, "");
    o += put_namelist(pkt + o, "");
    pkt[o++] = 0; // first_kex_packet_follows
    for (int j = 0; j < 4; j++)
    {
        pkt[o++] = 0; // reserved
    }

    TEST_ASSERT_TRUE(o > 512); // larger than the old SSH_KEXINIT_MAX
    SshSession *s = &ssh_sess[0];
    s->phase = SSH_PHASE_KEXINIT;
    TEST_ASSERT_EQUAL_INT(0, ssh_kexinit_parse(0, pkt, o));
    TEST_ASSERT_TRUE(s->ext_info_c); // ext-info-c detected in the big list
}

// A client that did NOT advertise ext-info-c must not be sent EXT_INFO.
void test_extinfo_not_sent_without_ext_info_c()
{
    SshSession *s = &ssh_sess[0];
    strcpy(s->v_c, "SSH-2.0-NoExt");
    s->v_c_len = (uint16_t)strlen(s->v_c);
    s->phase = SSH_PHASE_KEXINIT;

    uint8_t pkt[2048];
    size_t n = build_client_kexinit(pkt, /*ext_info_c=*/PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));

    uint8_t e_be[256];
    memset(e_be, 0, sizeof(e_be));
    e_be[255] = 0x02;
    n = 0;
    pkt[n++] = SSH_MSG_KEXDH_INIT;
    n += put_mpint(pkt + n, e_be, 256);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));

    uint8_t nk = SSH_MSG_NEWKEYS;
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, nk, &nk, 1));
    TEST_ASSERT_FALSE(ssh_sess[0].ext_info_c);
    TEST_ASSERT_EQUAL_INT(0, emt_n); // no EXT_INFO emitted
}

// An inbound EXT_INFO from the client is accepted and ignored (no UNIMPLEMENTED).
void test_inbound_ext_info_ignored()
{
    uint8_t pkt[1] = {SSH_MSG_EXT_INFO};
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_EXT_INFO, pkt, 1));
    TEST_ASSERT_EQUAL_INT(0, emt_n);
}

// ---------------------------------------------------------------------------
// Packet-layer (ssh_packet.cpp) framing edge cases
// ---------------------------------------------------------------------------

#include "crypto/cipher/aes256ctr.h"
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"

// The keyed api needs a context, not a key. These are one-shot vectors, so one scratch context is
// enough - rebuilt per call, and released first so a backend that attaches vendor resources to a
// context (ESP's mbedtls does) does not leak one per vector.
static uint8_t g_gcm_ws[PC_WORK_AESGCM] __attribute__((aligned(8)));
static proto_bool g_gcm_live = PROTO_FALSE;
static pc_aesgcm_key *gcm_key(const uint8_t *key)
{
    if (g_gcm_live)
    {
        pc_aesgcm_key_wipe(reinterpret_cast<pc_aesgcm_key *>(g_gcm_ws));
    }
    g_gcm_live = PROTO_TRUE;
    return pc_aesgcm_key_init(g_gcm_ws, key);
}

static int g_pkt_calls = 0;
static void pkt_rec_handler(uint8_t slot, uint8_t msg_type, const uint8_t *payload, size_t len)
{
    (void)slot;
    (void)msg_type;
    (void)payload;
    (void)len;
    g_pkt_calls++;
}

// Every packet entry point rejects an out-of-range slot, and a send whose wire form
// does not fit the output buffer.
void test_ssh_pkt_index_and_cap_guards()
{
    uint8_t out[64];
    size_t out_len = 0;
    ssh_pkt_init(MAX_SSH_CONNS); // out-of-range slot: no-op, no crash
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_send(MAX_SSH_CONNS, (const uint8_t *)"x", 1, out, &out_len, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(MAX_SSH_CONNS, (const uint8_t *)"x", 1, pkt_rec_handler));
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_disconnect(MAX_SSH_CONNS, 11, out, &out_len, sizeof(out)));

    ssh_pkt_init(0);
    uint8_t big_payload[64];
    memset(big_payload, 'a', sizeof(big_payload));
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_send(0, big_payload, sizeof(big_payload), out, &out_len, 8)); // cap too small
}

// Unencrypted receive rejects a buffer overflow, an invalid packet length and an
// over-large padding length, and stalls (returns 0, consumes nothing) on a partial packet.
void test_ssh_pkt_recv_unencrypted_errors()
{
    static uint8_t overflow[SSH_PKT_BUF_SIZE + 1];
    memset(overflow, 0, sizeof(overflow));
    ssh_pkt_init(0);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, overflow, sizeof(overflow), pkt_rec_handler)); // buffer overflow

    ssh_pkt_init(0);
    uint8_t zero_len[4] = {0, 0, 0, 0}; // packet_length == 0
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, zero_len, 4, pkt_rec_handler));

    ssh_pkt_init(0);
    uint8_t bad_pad[10] = {0, 0, 0, 6, 6, 1, 0, 0, 0, 0}; // padding_length 6 >= packet_length 6
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, bad_pad, sizeof(bad_pad), pkt_rec_handler));

    ssh_pkt_init(0);
    uint8_t partial[5] = {0, 0, 0, 6, 4}; // announces 6, only 5 present -> buffered, not consumed
    g_pkt_calls = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, partial, sizeof(partial), pkt_rec_handler));
    TEST_ASSERT_EQUAL_INT(0, g_pkt_calls);
}

// The send and receive sequence-number overflow guards fire at the rekey threshold.
void test_ssh_pkt_seq_overflow_guards()
{
    uint8_t out[64];
    size_t out_len = 0;
    ssh_pkt_init(0);
    ssh_pkt[0].seq_no_send = SSH_SEQ_CLOSE_THRESHOLD;
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_send(0, (const uint8_t *)"x", 1, out, &out_len, sizeof(out)));

    ssh_pkt_init(0);
    ssh_pkt[0].seq_no_recv = SSH_SEQ_CLOSE_THRESHOLD;
    uint8_t pkt[10] = {0, 0, 0, 6, 4, 42, 0, 0, 0, 0}; // a well-formed unencrypted packet
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, pkt, sizeof(pkt), pkt_rec_handler));
}

// An encrypted send round-trips through an encrypted receive (loopback keys), and a
// corrupted MAC is rejected.
void test_ssh_pkt_encrypted_roundtrip_and_mac_fail()
{
    uint8_t key[32], iv[16];
    memset(key, 0x11, sizeof(key));
    memset(iv, 0x22, sizeof(iv));

    ssh_pkt_init(0);
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;
    memcpy(ssh_keys[0].aes_key_s2c, key, PC_AES256CTR_KEY_LEN);
    memcpy(ssh_keys[0].aes_iv_s2c, iv, PC_AES256CTR_CTR_LEN);
    memcpy(ssh_keys[0].aes_key_c2s, key, PC_AES256CTR_KEY_LEN);
    memcpy(ssh_keys[0].aes_iv_c2s, iv, PC_AES256CTR_CTR_LEN); // identical -> loopback
    memset(ssh_keys[0].mac_key_s2c, 0x33, 32);
    memset(ssh_keys[0].mac_key_c2s, 0x33, 32);

    uint8_t payload[8];
    memset(payload, 0xAB, sizeof(payload));
    payload[0] = 50; // msg_type
    uint8_t out[256];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, payload, sizeof(payload), out, &out_len, sizeof(out)));
    TEST_ASSERT_TRUE(out_len > 0);

    // Good packet: decrypt + MAC verify + deliver.
    memcpy(ssh_keys[0].aes_key_c2s, key, PC_AES256CTR_KEY_LEN);
    memcpy(ssh_keys[0].aes_iv_c2s, iv, PC_AES256CTR_CTR_LEN); // reset decrypt cipher to the send's start
    ssh_pkt[0].seq_no_recv = 0;
    ssh_pkt[0].rx_len = 0;
    uint8_t rx[256];
    memcpy(rx, out, out_len);
    g_pkt_calls = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, rx, out_len, pkt_rec_handler));
    TEST_ASSERT_EQUAL_INT(1, g_pkt_calls);

    // Corrupted MAC (last byte) is rejected.
    memcpy(ssh_keys[0].aes_key_c2s, key, PC_AES256CTR_KEY_LEN);
    memcpy(ssh_keys[0].aes_iv_c2s, iv, PC_AES256CTR_CTR_LEN);
    ssh_pkt[0].seq_no_recv = 0;
    ssh_pkt[0].rx_len = 0;
    memcpy(rx, out, out_len);
    rx[out_len - 1] ^= 0xFF;
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, rx, out_len, pkt_rec_handler));

    ssh_pkt_init(0); // leave the slot clean for later tests
}

// ---------------------------------------------------------------------------
// Dispatcher guard / error branches
// ---------------------------------------------------------------------------

// An out-of-range connection slot is rejected outright.
void test_ssh_dispatch_bad_slot()
{
    uint8_t p[1] = {SSH_MSG_IGNORE};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(MAX_SSH_CONNS, p[0], p, 1));
}

// A KEXINIT whose payload is far too short fails negotiation.
void test_ssh_kexinit_parse_fail()
{
    ssh_sess[0].phase = SSH_PHASE_KEXINIT;
    uint8_t p[4] = {SSH_MSG_KEXINIT, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, p[0], p, sizeof(p)));
}

// KEXDH_INIT outside the DH_INIT phase is rejected; a malformed one in-phase fails the handler.
void test_ssh_kexdh_guards()
{
    ssh_sess[0].phase = SSH_PHASE_KEXINIT; // not DH_INIT
    uint8_t bad[4] = {SSH_MSG_KEXDH_INIT, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, bad[0], bad, sizeof(bad))); // wrong phase

    SshSession *s = &ssh_sess[0];
    strcpy(s->v_c, "SSH-2.0-T");
    s->v_c_len = (uint16_t)strlen(s->v_c);
    s->phase = SSH_PHASE_KEXINIT;
    uint8_t pkt[2048];
    size_t n = build_client_kexinit(pkt);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));            // -> DH_INIT
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, bad[0], bad, sizeof(bad))); // handler fails
}

// SERVICE_REQUEST with a truncated service name fails.
void test_ssh_service_request_fail()
{
    ssh_sess[0].phase = SSH_PHASE_SERVICE;
    uint8_t bad[2] = {SSH_MSG_SERVICE_REQUEST, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, bad[0], bad, sizeof(bad)));
}

// USERAUTH_REQUEST outside the AUTH phase is rejected; a truncated one in-phase fails the handler.
void test_ssh_userauth_guards()
{
    ssh_sess[0].phase = SSH_PHASE_SERVICE; // not AUTH
    uint8_t p[2] = {SSH_MSG_USERAUTH_REQUEST, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, p[0], p, sizeof(p)));
    ssh_sess[0].phase = SSH_PHASE_AUTH;
    uint8_t bad[3] = {SSH_MSG_USERAUTH_REQUEST, 0, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, bad[0], bad, sizeof(bad)));
}

// Every post-auth connection message is rejected while unauthenticated.
void test_ssh_postauth_authed_guard()
{
    ssh_sess[0].authed = PROTO_FALSE;
    const uint8_t mts[] = {SSH_MSG_GLOBAL_REQUEST, SSH_MSG_CHANNEL_OPEN_CONFIRM, SSH_MSG_CHANNEL_OPEN_FAILURE,
                           SSH_MSG_CHANNEL_REQUEST, SSH_MSG_CHANNEL_DATA};
    for (size_t j = 0; j < sizeof(mts) / sizeof(mts[0]); j++)
    {
        uint8_t p[8] = {mts[j], 0, 0, 0, 0};
        TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, mts[j], p, sizeof(p)));
    }
}

// Authenticated, a malformed post-auth message fails its handler.
void test_ssh_postauth_handler_fails()
{
    ssh_sess[0].authed = PROTO_TRUE;
    const uint8_t mts[] = {SSH_MSG_GLOBAL_REQUEST, SSH_MSG_CHANNEL_OPEN, SSH_MSG_CHANNEL_REQUEST, SSH_MSG_CHANNEL_DATA};
    for (size_t j = 0; j < sizeof(mts) / sizeof(mts[0]); j++)
    {
        uint8_t p[2] = {mts[j], 0}; // too short for the handler to parse
        TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, mts[j], p, sizeof(p)));
    }
}

// Authenticated, a stray OPEN_CONFIRM / OPEN_FAILURE is accepted-and-ignored (returns 0).
void test_ssh_open_confirm_failure_authed()
{
    ssh_sess[0].authed = PROTO_TRUE;
    uint8_t c[9] = {SSH_MSG_CHANNEL_OPEN_CONFIRM, 0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, c[0], c, sizeof(c)));
    uint8_t f[5] = {SSH_MSG_CHANNEL_OPEN_FAILURE, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, f[0], f, sizeof(f)));
}

// A well-formed GLOBAL_REQUEST with want_reply is handled and answered (REQUEST_SUCCESS/FAILURE).
void test_ssh_global_request_reply()
{
    ssh_sess[0].authed = PROTO_TRUE;
    uint8_t p[64];
    size_t n = 0;
    p[n++] = SSH_MSG_GLOBAL_REQUEST;
    n += put_string(p + n, "tcpip-forward");
    p[n++] = 1; // want_reply
    n += put_string(p + n, "0.0.0.0");
    wr_u32(p + n, 8080);
    n += 4;
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, p[0], p, n));
    TEST_ASSERT_EQUAL_INT(1, emt_n); // a REQUEST_SUCCESS or REQUEST_FAILURE was emitted
}

// WINDOW_ADJUST and CHANNEL_EOF are accepted (no reply, returns 0).
void test_ssh_window_adjust_and_eof()
{
    uint8_t w[9] = {SSH_MSG_CHANNEL_WINDOW_ADJUST, 0, 0, 0, 0, 0, 0, 0, 10};
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, w[0], w, sizeof(w)));
    uint8_t e[5] = {SSH_MSG_CHANNEL_EOF, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, e[0], e, sizeof(e)));
}

// Open one "session" channel through the dispatcher so the channel-scoped tests below have a live
// channel 0. Leaves the session authenticated and in the OPEN phase.
static void open_session_channel(uint32_t peer_id)
{
    ssh_sess[0].authed = PROTO_TRUE;
    ssh_sess[0].phase = SSH_PHASE_OPEN;
    uint8_t p[64];
    size_t n = 0;
    p[n++] = SSH_MSG_CHANNEL_OPEN;
    n += put_string(p + n, "session");
    wr_u32(p + n, peer_id);
    wr_u32(p + n + 4, 4096);
    wr_u32(p + n + 8, 32768);
    n += 12;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, p[0], p, n));
    TEST_ASSERT_TRUE(ssh_chan[0][0].open);
}

// emit() drops the frame when no packet-emit callback is registered (the transport owner has not
// wired one up yet, or is tearing the connection down). Every dispatch path must still run to
// completion and return the same status - the callback is an output sink, not a control path.
void test_ssh_dispatch_without_emit_cb()
{
    emt_reset();
    pc_ssh_server_set_emit_cb(NULL);

    SshSession *s = &ssh_sess[0];
    strcpy(s->v_c, "SSH-2.0-NoEmit");
    s->v_c_len = (uint16_t)strlen(s->v_c);
    s->phase = SSH_PHASE_KEXINIT;

    uint8_t pkt[2048];
    size_t n = build_client_kexinit(pkt); // KEXINIT reply dropped
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_EQUAL(SSH_PHASE_DH_INIT, s->phase);

    uint8_t e_be[256];
    memset(e_be, 0, sizeof(e_be));
    e_be[255] = 0x02;
    n = 0;
    pkt[n++] = SSH_MSG_KEXDH_INIT;
    n += put_mpint(pkt + n, e_be, 256); // KEXDH_REPLY + NEWKEYS dropped
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_TRUE(ssh_pkt[0].enc_out); // the keys were still installed

    uint8_t nk = SSH_MSG_NEWKEYS; // EXT_INFO dropped
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, nk, &nk, 1));
    TEST_ASSERT_EQUAL(SSH_PHASE_SERVICE, s->phase);

    n = 0;
    pkt[n++] = SSH_MSG_SERVICE_REQUEST;
    n += put_string(pkt + n, "ssh-userauth"); // SERVICE_ACCEPT dropped
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_EQUAL(SSH_PHASE_AUTH, s->phase);

    n = build_password_auth(pkt, "alice", "s3cret"); // USERAUTH_SUCCESS dropped
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_TRUE(s->authed);

    open_session_channel(41); // CHANNEL_OPEN_CONFIRMATION dropped

    n = 0;
    pkt[n++] = SSH_MSG_CHANNEL_REQUEST;
    wr_u32(pkt + n, 0);
    n += 4;
    n += put_string(pkt + n, "shell");
    pkt[n++] = 1; // want_reply: CHANNEL_SUCCESS dropped
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));

    uint8_t cl[5];
    cl[0] = SSH_MSG_CHANNEL_CLOSE;
    wr_u32(cl + 1, 0); // CHANNEL_EOF + CHANNEL_CLOSE dropped
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_CHANNEL_CLOSE, cl, sizeof(cl)));
    TEST_ASSERT_FALSE(ssh_chan[0][0].open); // the close still took effect

    uint8_t unk[1] = {201}; // UNIMPLEMENTED dropped
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, unk[0], unk, 1));

    const int recorded = emt_n;
    pc_ssh_server_set_emit_cb(rec_emit); // restore for the remaining tests
    TEST_ASSERT_EQUAL_INT(0, recorded);  // nothing reached the recorder while it was unhooked
}

// RFC 4254 §4: a global request with want_reply = FALSE is answered with silence. The handler
// leaves the reply empty and the dispatcher must not emit a zero-length packet.
void test_ssh_global_request_silent_without_want_reply()
{
    ssh_sess[0].authed = PROTO_TRUE;
    uint8_t p[64];
    size_t n = 0;
    p[n++] = SSH_MSG_GLOBAL_REQUEST;
    n += put_string(p + n, "no-such-request@example.com"); // unrecognized request name
    p[n++] = 0;                                            // want_reply = FALSE
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, p[0], p, n));
    TEST_ASSERT_EQUAL_INT(0, emt_n);
}

// RFC 4254 §5.4: same rule for a channel request - executed, but not answered.
void test_ssh_channel_request_silent_without_want_reply()
{
    open_session_channel(51);

    uint8_t p[64];
    size_t n = 0;
    p[n++] = SSH_MSG_CHANNEL_REQUEST;
    wr_u32(p + n, 0); // recipient = local channel 0
    n += 4;
    n += put_string(p + n, "shell");
    p[n++] = 0; // want_reply = FALSE
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, p[0], p, n));
    TEST_ASSERT_EQUAL_INT(0, emt_n);
}

// A CHANNEL_CLOSE that the channel layer cannot act on (unknown recipient, or a payload too short
// to carry one) emits nothing and is NOT fatal: a stray close must not drop the connection.
void test_ssh_channel_close_unhandled_emits_nothing()
{
    // No channel has been opened in this test, so recipient 0 does not resolve.
    uint8_t pkt[5];
    pkt[0] = SSH_MSG_CHANNEL_CLOSE;
    wr_u32(pkt + 1, 0);
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_CHANNEL_CLOSE, pkt, sizeof(pkt)));
    TEST_ASSERT_EQUAL_INT(0, emt_n);

    // Recipient id past the channel table.
    wr_u32(pkt + 1, PC_SSH_MAX_CHANNELS + 7u);
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_CHANNEL_CLOSE, pkt, sizeof(pkt)));
    TEST_ASSERT_EQUAL_INT(0, emt_n);

    // Truncated: no room for the recipient field at all.
    uint8_t shortpkt[3] = {SSH_MSG_CHANNEL_CLOSE, 0, 0};
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_CHANNEL_CLOSE, shortpkt, sizeof(shortpkt)));
    TEST_ASSERT_EQUAL_INT(0, emt_n);
}

// An in-session KEXINIT is a re-key request (RFC 4253 §9), not a protocol error: the dispatcher
// re-negotiates, replies with a fresh server KEXINIT and rewinds the phase to DH_INIT while the
// session id and authentication survive.
void test_ssh_kexinit_midsession_rekey()
{
    SshSession *s = &ssh_sess[0];
    s->authed = PROTO_TRUE;
    s->have_session_id = PROTO_TRUE;
    s->session_id_len = 32;
    for (int j = 0; j < 32; j++)
    {
        s->session_id[j] = (uint8_t)(0x10 + j);
    }
    s->phase = SSH_PHASE_OPEN;

    uint8_t pkt[2048];
    size_t n = build_client_kexinit(pkt);
    emt_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    TEST_ASSERT_EQUAL_INT(1, emt_n);
    TEST_ASSERT_EQUAL(SSH_MSG_KEXINIT, emt_type[0]);
    TEST_ASSERT_EQUAL(SSH_PHASE_DH_INIT, s->phase);
    TEST_ASSERT_TRUE(s->authed);
    TEST_ASSERT_TRUE(s->have_session_id);
    for (int j = 0; j < 32; j++)
    {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(0x10 + j), s->session_id[j]); // fixed at the first KEX
    }

    // The client's NEWKEYS after a re-key resumes the channel phase, not the service phase.
    uint8_t e_be[256];
    memset(e_be, 0, sizeof(e_be));
    e_be[255] = 0x02;
    n = 0;
    pkt[n++] = SSH_MSG_KEXDH_INIT;
    n += put_mpint(pkt + n, e_be, 256);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, pkt[0], pkt, n));
    uint8_t nk = SSH_MSG_NEWKEYS;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, nk, &nk, 1));
    TEST_ASSERT_EQUAL(SSH_PHASE_OPEN, s->phase);
}

// ---------------------------------------------------------------------------
// Packet layer: client role, cipher modes, and the malformed-frame guards
// ---------------------------------------------------------------------------

static const uint8_t PKT_KEY_BYTE = 0x5A;
static const uint8_t PKT_IV_BYTE = 0x3C;
static const uint8_t PKT_MAC_BYTE = 0x77;

// Install a loopback key set on slot 0: both directions carry identical key material, so a packet
// this process sends can be fed straight back into the receive path whichever role is selected.
static void pkt_loopback_keys(uint8_t cipher_mode, uint8_t mac_mode, proto_bool client)
{
    uint8_t key[PC_CHACHAPOLY_KEY_LEN];
    uint8_t iv[16];
    memset(key, PKT_KEY_BYTE, sizeof(key));
    memset(iv, PKT_IV_BYTE, sizeof(iv));
    ssh_pkt_init(0);
    if (client)
    {
        ssh_pkt_set_client(0);
    }
    ssh_pkt[0].enc_out = PROTO_TRUE;
    ssh_pkt[0].enc_in = PROTO_TRUE;
    memset(&ssh_keys[0], 0, sizeof(ssh_keys[0]));
    ssh_keys[0].cipher_mode = cipher_mode;
    ssh_keys[0].mac_mode = mac_mode;
    memcpy(ssh_keys[0].chacha_key_c2s, key, PC_CHACHAPOLY_KEY_LEN);
    memcpy(ssh_keys[0].chacha_key_s2c, key, PC_CHACHAPOLY_KEY_LEN);
    // aes256-gcm reuses aes_key_*/aes_iv_* installed by the aes256-ctr memcpy below (mode-exclusive).
    memcpy(ssh_keys[0].aes_key_c2s, key, PC_AES256CTR_KEY_LEN);
    memcpy(ssh_keys[0].aes_iv_c2s, iv, PC_AES256CTR_CTR_LEN);
    memcpy(ssh_keys[0].aes_key_s2c, key, PC_AES256CTR_KEY_LEN);
    memcpy(ssh_keys[0].aes_iv_s2c, iv, PC_AES256CTR_CTR_LEN);
    memset(ssh_keys[0].mac_key_c2s, PKT_MAC_BYTE, sizeof(ssh_keys[0].mac_key_c2s));
    memset(ssh_keys[0].mac_key_s2c, PKT_MAC_BYTE, sizeof(ssh_keys[0].mac_key_s2c));
}

// Frame one payload with the given cipher/MAC/role and feed the wire bytes straight back in.
static void pkt_roundtrip(uint8_t cipher_mode, uint8_t mac_mode, proto_bool client, const uint8_t *payload, size_t n)
{
    static uint8_t wire[SSH_WIRE_CAP];
    size_t wlen = 0;
    pkt_loopback_keys(cipher_mode, mac_mode, client);
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, payload, n, wire, &wlen, sizeof(wire)));
    ssh_pkt[0].seq_no_recv = 0;
    ssh_pkt[0].rx_len = 0;
    g_pkt_calls = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire, wlen, pkt_rec_handler));
    TEST_ASSERT_EQUAL_INT(1, g_pkt_calls);
}

// ssh_pkt_set_client flips the slot onto the client key mapping and ignores an out-of-range slot;
// a payload whose framed size is already a multiple of 16 takes the zero-remainder padding path
// (which then still has to grow to the RFC 4253 §6 four-byte minimum).
void test_ssh_pkt_client_role_and_zero_remainder_padding()
{
    ssh_pkt_init(0);
    ssh_pkt_set_client(MAX_SSH_CONNS); // out-of-range: no-op
    TEST_ASSERT_FALSE(ssh_pkt[0].is_client);
    ssh_pkt_set_client(0);
    TEST_ASSERT_TRUE(ssh_pkt[0].is_client);

    ssh_pkt_init(0);
    uint8_t payload[11]; // 4 + 1 + 11 == 16 -> remainder 0
    memset(payload, 0x21, sizeof(payload));
    payload[0] = SSH_MSG_IGNORE;
    uint8_t out[64];
    size_t out_len = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, payload, sizeof(payload), out, &out_len, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(16, out[4]); // padding 0 is below the minimum -> one whole extra block
    TEST_ASSERT_EQUAL_size_t(4 + 1 + 11 + 16, out_len);
}

// Every cipher mode round-trips in the CLIENT role, which is the mirror of the server key mapping
// (send with c2s, receive with s2c). Both SHA-512 MAC variants are exercised too, so the 64-byte
// HMAC selector in compute_mac_mode is driven from both its encrypt-and-MAC and its ETM caller.
void test_ssh_pkt_client_role_all_cipher_modes()
{
    const uint8_t payload[9] = {SSH_MSG_IGNORE, 1, 2, 3, 4, 5, 6, 7, 8};
    pkt_roundtrip(SSH_CIPHER_CHACHA20POLY1305, SSH_MAC_HMAC_SHA256, PROTO_TRUE, payload, sizeof(payload));
    pkt_roundtrip(SSH_CIPHER_AES256GCM, SSH_MAC_HMAC_SHA256, PROTO_TRUE, payload, sizeof(payload));
    pkt_roundtrip(SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256_ETM, PROTO_TRUE, payload, sizeof(payload));
    pkt_roundtrip(SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256, PROTO_TRUE, payload, sizeof(payload));
    pkt_roundtrip(SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA512, PROTO_TRUE, payload, sizeof(payload));
    pkt_roundtrip(SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA512_ETM, PROTO_TRUE, payload, sizeof(payload));
    ssh_pkt_init(0);
}

// aes256-gcm@openssh.com aligns (padding_length || payload) to a 16-byte multiple: a payload whose
// natural padding falls below the RFC 4253 §6 four-byte minimum grows by one whole extra block.
void test_ssh_pkt_aesgcm_minimum_padding()
{
    static uint8_t wire[SSH_WIRE_CAP];
    uint8_t payload[13]; // base 14 -> natural padding 2 -> below the minimum -> 18
    memset(payload, 0x3E, sizeof(payload));
    payload[0] = SSH_MSG_IGNORE;
    pkt_loopback_keys(SSH_CIPHER_AES256GCM, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    size_t wlen = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, payload, sizeof(payload), wire, &wlen, sizeof(wire)));
    const uint32_t pkt_len = rd_u32(wire);
    TEST_ASSERT_EQUAL_UINT32(1u + 13u + 18u, pkt_len);
    TEST_ASSERT_EQUAL_size_t(4 + pkt_len + PC_AESGCM_TAG_LEN, wlen);

    ssh_pkt[0].seq_no_recv = 0;
    ssh_pkt[0].rx_len = 0;
    g_pkt_calls = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, wire, wlen, pkt_rec_handler));
    TEST_ASSERT_EQUAL_INT(1, g_pkt_calls);
    ssh_pkt_init(0);
}

// Build the 4-byte chacha20-poly1305 length field that decrypts to @p want. The length is a plain
// keystream XOR, so the keystream is recovered by "decrypting" four zero bytes.
static void chacha_len_field(uint8_t out[4], uint32_t want)
{
    const uint8_t zero[4] = {0, 0, 0, 0};
    uint32_t ks = pc_chachapoly_get_length(ssh_keys[0].chacha_key_c2s, ssh_pkt[0].seq_no_recv, zero);
    wr_u32(out, want ^ ks);
}

// chacha20-poly1305@openssh.com receive rejects an out-of-range decrypted packet_length, and a
// tag-valid packet whose padding_length violates RFC 4253 §6 (below four, or past the packet).
void test_ssh_pkt_chachapoly_frame_errors()
{
    uint8_t rx[128];
    memset(rx, 0, sizeof(rx));

    pkt_loopback_keys(SSH_CIPHER_CHACHA20POLY1305, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    chacha_len_field(rx, 0); // packet_length 0
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, rx, 64, pkt_rec_handler));

    pkt_loopback_keys(SSH_CIPHER_CHACHA20POLY1305, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    chacha_len_field(rx, SSH_PKT_BUF_SIZE); // past the receive buffer
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, rx, 64, pkt_rec_handler));

    // A genuinely sealed packet carrying padding_length 2 (< 4).
    const uint32_t pkt_len = 32;
    uint8_t plain[4 + pkt_len];
    uint8_t sealed[4 + pkt_len + PC_CHACHAPOLY_TAG_LEN];
    memset(plain, 0xEE, sizeof(plain));
    wr_u32(plain, pkt_len);
    plain[4] = 2;
    pkt_loopback_keys(SSH_CIPHER_CHACHA20POLY1305, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    pc_chachapoly_encrypt(ssh_keys[0].chacha_key_c2s, 0, sealed, plain, pkt_len);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, sealed, sizeof(sealed), pkt_rec_handler));

    // ...and one whose padding_length runs past the packet itself.
    plain[4] = (uint8_t)pkt_len;
    pkt_loopback_keys(SSH_CIPHER_CHACHA20POLY1305, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    pc_chachapoly_encrypt(ssh_keys[0].chacha_key_c2s, 0, sealed, plain, pkt_len);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, sealed, sizeof(sealed), pkt_rec_handler));
    ssh_pkt_init(0);
}

// Seal a hand-built GCM packet body with a context matching the loopback receive keys.
static size_t gcm_seal_packet(uint8_t *wire, uint32_t pkt_len, const uint8_t *plain)
{
    uint8_t key[PC_AESGCM_KEY_LEN];
    uint8_t iv[PC_AESGCM_IV_LEN];
    memset(key, PKT_KEY_BYTE, sizeof(key));
    memset(iv, PKT_IV_BYTE, sizeof(iv));
    wr_u32(wire, pkt_len);
    pc_aesgcm_seal(gcm_key(key), iv, wire, 4, plain, pkt_len, wire + 4, wire + 4 + pkt_len);
    return 4 + pkt_len + PC_AESGCM_TAG_LEN;
}

// aes256-gcm@openssh.com receive: the cleartext packet_length must be positive, within the receive
// buffer, and a whole number of AES blocks; a short read stalls; an exhausted scratch arena fails
// closed; the sequence-number ceiling closes the connection; and the padding_length is validated.
void test_ssh_pkt_aesgcm_frame_errors()
{
    uint8_t rx[128];
    memset(rx, 0, sizeof(rx));

    pkt_loopback_keys(SSH_CIPHER_AES256GCM, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    wr_u32(rx, 0); // packet_length 0
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, rx, 64, pkt_rec_handler));

    pkt_loopback_keys(SSH_CIPHER_AES256GCM, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    wr_u32(rx, SSH_PKT_BUF_SIZE); // past the receive buffer
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, rx, 64, pkt_rec_handler));

    pkt_loopback_keys(SSH_CIPHER_AES256GCM, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    wr_u32(rx, 17); // not a whole number of AES blocks
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, rx, 64, pkt_rec_handler));

    // Length announced but the body has not arrived: buffered, nothing dispatched.
    pkt_loopback_keys(SSH_CIPHER_AES256GCM, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    wr_u32(rx, 32);
    g_pkt_calls = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, rx, 12, pkt_rec_handler));
    TEST_ASSERT_EQUAL_INT(0, g_pkt_calls);

    // Complete frame but no scratch to decrypt into -> discard + disconnect.
    pkt_loopback_keys(SSH_CIPHER_AES256GCM, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    wr_u32(rx, 16);
    pc_plaintext_reset();
    while (pc_plaintext_alloc(64, 1))
    {
    }
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, rx, 4 + 16 + PC_AESGCM_TAG_LEN, pkt_rec_handler));
    pc_plaintext_reset();

    // A tag-valid packet arriving at the sequence-number ceiling is refused.
    static uint8_t wire[SSH_WIRE_CAP];
    size_t wlen = 0;
    const uint8_t payload[8] = {SSH_MSG_IGNORE, 1, 2, 3, 4, 5, 6, 7};
    pkt_loopback_keys(SSH_CIPHER_AES256GCM, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_send(0, payload, sizeof(payload), wire, &wlen, sizeof(wire)));
    ssh_pkt[0].seq_no_recv = SSH_SEQ_CLOSE_THRESHOLD;
    ssh_pkt[0].rx_len = 0;
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, wlen, pkt_rec_handler));

    // padding_length below the minimum, then past the packet - both after a valid tag.
    const uint32_t pkt_len = 32;
    uint8_t plain[pkt_len];
    memset(plain, 0xC5, sizeof(plain));
    plain[0] = 3;
    pkt_loopback_keys(SSH_CIPHER_AES256GCM, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    size_t n = gcm_seal_packet(wire, pkt_len, plain);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, n, pkt_rec_handler));

    plain[0] = (uint8_t)pkt_len;
    pkt_loopback_keys(SSH_CIPHER_AES256GCM, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    n = gcm_seal_packet(wire, pkt_len, plain);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, n, pkt_rec_handler));
    ssh_pkt_init(0);
}

// Build a hand-made encrypt-then-MAC packet: cleartext length, AES-256-CTR body, HMAC over both.
static size_t ctr_etm_packet(uint8_t *wire, uint32_t pkt_len, const uint8_t *plain)
{
    uint8_t key[32];
    uint8_t iv[16];
    uint8_t mac_key[32];
    memset(key, PKT_KEY_BYTE, sizeof(key));
    memset(iv, PKT_IV_BYTE, sizeof(iv));
    memset(mac_key, PKT_MAC_BYTE, sizeof(mac_key));
    uint8_t ctr[16];
    memcpy(ctr, iv, 16);
    wr_u32(wire, pkt_len);
    pc_aes256ctr_crypt(key, ctr, plain, wire + 4, pkt_len);
    const uint8_t seq_be[4] = {0, 0, 0, 0};
    pc_hmac_sha256_ctx h;
    pc_hmac_sha256_init(&h, mac_key, sizeof(mac_key));
    pc_hmac_sha256_update(&h, seq_be, 4);
    pc_hmac_sha256_update(&h, wire, 4 + pkt_len);
    pc_hmac_sha256_final(&h, wire + 4 + pkt_len);
    return 4 + pkt_len + 32;
}

// aes256-ctr + encrypt-then-MAC receive: the cleartext packet_length is range- and block-checked
// before any MAC work, the padding_length is validated after the MAC verifies, and a packet whose
// wire form cannot fit the receive buffer fills it and is rejected instead of stalling forever.
void test_ssh_pkt_ctr_etm_frame_errors()
{
    uint8_t rx[128];
    memset(rx, 0, sizeof(rx));

    pkt_loopback_keys(SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256_ETM, PROTO_FALSE);
    wr_u32(rx, 0);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, rx, 64, pkt_rec_handler));

    pkt_loopback_keys(SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256_ETM, PROTO_FALSE);
    wr_u32(rx, SSH_PKT_BUF_SIZE); // past the receive buffer
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, rx, 64, pkt_rec_handler));

    pkt_loopback_keys(SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256_ETM, PROTO_FALSE);
    wr_u32(rx, 20); // not a whole number of AES blocks
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, rx, 64, pkt_rec_handler));

    // MAC verifies, padding_length does not.
    static uint8_t wire[SSH_WIRE_CAP];
    const uint32_t pkt_len = 32;
    uint8_t plain[pkt_len];
    memset(plain, 0x91, sizeof(plain));
    plain[0] = 1;
    pkt_loopback_keys(SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256_ETM, PROTO_FALSE);
    size_t n = ctr_etm_packet(wire, pkt_len, plain);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, n, pkt_rec_handler));

    plain[0] = (uint8_t)pkt_len;
    pkt_loopback_keys(SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256_ETM, PROTO_FALSE);
    n = ctr_etm_packet(wire, pkt_len, plain);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, n, pkt_rec_handler));

    // packet_length 2032 is legal but its wire form (4 + 2032 + 32) exceeds the receive buffer, so
    // the buffer fills with no extractable packet: the appender must give up rather than spin.
    static uint8_t flood[SSH_PKT_BUF_SIZE + 64];
    memset(flood, 0, sizeof(flood));
    wr_u32(flood, 2032);
    pkt_loopback_keys(SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256_ETM, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, flood, sizeof(flood), pkt_rec_handler));
    ssh_pkt_init(0);
}

// aes256-ctr + encrypt-and-MAC receive: the first cipher block is needed before the length can be
// peeked; the decrypted length is then range- and block-checked; and the padding_length is
// validated after the MAC verifies. Unencrypted receive rejects an over-large length too.
void test_ssh_pkt_ctr_emac_and_plain_frame_errors()
{
    uint8_t key[32];
    uint8_t iv[16];
    memset(key, PKT_KEY_BYTE, sizeof(key));
    memset(iv, PKT_IV_BYTE, sizeof(iv));

    // Fewer than 16 bytes: not enough to peek the encrypted length, so nothing is consumed.
    uint8_t partial[8] = {0};
    pkt_loopback_keys(SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
    g_pkt_calls = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_pkt_recv(0, partial, sizeof(partial), pkt_rec_handler));
    TEST_ASSERT_EQUAL_INT(0, g_pkt_calls);

    static uint8_t wire[SSH_WIRE_CAP];
    uint8_t hdr[16];
    const uint32_t bad_lens[3] = {0u, 4092u, 13u}; // zero, past the buffer, not block-aligned
    for (int c = 0; c < 3; c++)
    {
        memset(hdr, 0xA7, sizeof(hdr));
        wr_u32(hdr, bad_lens[c]);
        uint8_t ctr[16];
        memcpy(ctr, iv, 16);
        pc_aes256ctr_crypt(key, ctr, hdr, wire, sizeof(hdr));
        pkt_loopback_keys(SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
        TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, sizeof(hdr), pkt_rec_handler));
    }

    // MAC verifies, padding_length does not (below the minimum, then past the packet).
    const uint32_t pkt_len = 28; // 4 + 28 == 32, a whole number of AES blocks
    const uint8_t bad_pads[2] = {2, (uint8_t)pkt_len};
    for (int c = 0; c < 2; c++)
    {
        uint8_t plain[4 + pkt_len];
        memset(plain, 0x6D, sizeof(plain));
        wr_u32(plain, pkt_len);
        plain[4] = bad_pads[c];

        uint8_t mac_key[32];
        memset(mac_key, PKT_MAC_BYTE, sizeof(mac_key));
        const uint8_t seq_be[4] = {0, 0, 0, 0};
        pc_hmac_sha256_ctx h;
        pc_hmac_sha256_init(&h, mac_key, sizeof(mac_key));
        pc_hmac_sha256_update(&h, seq_be, 4);
        pc_hmac_sha256_update(&h, plain, sizeof(plain));
        pc_hmac_sha256_final(&h, wire + sizeof(plain));
        uint8_t ctr[16];
        memcpy(ctr, iv, 16);
        pc_aes256ctr_crypt(key, ctr, plain, wire, sizeof(plain));

        pkt_loopback_keys(SSH_CIPHER_AES256CTR, SSH_MAC_HMAC_SHA256, PROTO_FALSE);
        TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, wire, sizeof(plain) + 32, pkt_rec_handler));
    }

    // Unencrypted: a packet_length past the receive buffer is rejected outright.
    ssh_pkt_init(0);
    uint8_t huge[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(-1, ssh_pkt_recv(0, huge, sizeof(huge), pkt_rec_handler));
    ssh_pkt_init(0);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_ssh_dispatch_bad_slot);
    RUN_TEST(test_ssh_kexinit_parse_fail);
    RUN_TEST(test_ssh_kexdh_guards);
    RUN_TEST(test_ssh_service_request_fail);
    RUN_TEST(test_ssh_userauth_guards);
    RUN_TEST(test_ssh_postauth_authed_guard);
    RUN_TEST(test_ssh_postauth_handler_fails);
    RUN_TEST(test_ssh_open_confirm_failure_authed);
    RUN_TEST(test_ssh_global_request_reply);
    RUN_TEST(test_ssh_window_adjust_and_eof);
    RUN_TEST(test_ssh_pkt_index_and_cap_guards);
    RUN_TEST(test_ssh_pkt_recv_unencrypted_errors);
    RUN_TEST(test_ssh_pkt_seq_overflow_guards);
    RUN_TEST(test_ssh_pkt_encrypted_roundtrip_and_mac_fail);
    RUN_TEST(test_ssh_pkt_client_role_and_zero_remainder_padding);
    RUN_TEST(test_ssh_pkt_client_role_all_cipher_modes);
    RUN_TEST(test_ssh_pkt_aesgcm_minimum_padding);
    RUN_TEST(test_ssh_pkt_chachapoly_frame_errors);
    RUN_TEST(test_ssh_pkt_aesgcm_frame_errors);
    RUN_TEST(test_ssh_pkt_ctr_etm_frame_errors);
    RUN_TEST(test_ssh_pkt_ctr_emac_and_plain_frame_errors);
    RUN_TEST(test_full_handshake_to_channel_data);
    RUN_TEST(test_extinfo_build_advertises_server_sig_algs);
    RUN_TEST(test_extinfo_not_sent_without_ext_info_c);
    RUN_TEST(test_inbound_ext_info_ignored);
    RUN_TEST(test_large_client_kexinit_accepted);
    RUN_TEST(test_channel_open_before_auth_rejected);
    RUN_TEST(test_service_request_before_newkeys_rejected);
    RUN_TEST(test_disconnect_closes);
    RUN_TEST(test_ignore_is_noop);
    RUN_TEST(test_auth_bruteforce_disconnect);
    RUN_TEST(test_auth_success_after_failures);
    RUN_TEST(test_unimplemented_reply_for_unknown_message);
    RUN_TEST(test_inbound_close_emits_eof_then_close_separately);
    RUN_TEST(test_ssh_global_request_silent_without_want_reply);
    RUN_TEST(test_ssh_channel_request_silent_without_want_reply);
    RUN_TEST(test_ssh_channel_close_unhandled_emits_nothing);
    RUN_TEST(test_ssh_kexinit_midsession_rekey);
    RUN_TEST(test_ssh_dispatch_without_emit_cb);
    return UNITY_END();
}
