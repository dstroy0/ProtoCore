// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SSH keyboard-interactive authentication tests (RFC 4256): the server sends one INFO_REQUEST with a
// single non-echoed "Password:" prompt and verifies the INFO_RESPONSE through the password callback.

#include "network_drivers/presentation/ssh/auth/ssh_auth.h"
#include "network_drivers/presentation/ssh/connection/ssh_channel.h" // full-switch dispatch coverage
#include "network_drivers/presentation/ssh/connection/ssh_server.h"  // the dispatcher's INFO_RESPONSE case
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include "network_drivers/tls/ssh_rsa.h" // host key for the KEXDH reply
#include <stdint.h>
#include <string.h>

#include "core_setup/hal/nvs.h"
#include "test/fixtures/ssh_test_host_key/ssh_test_keys.h"
#include <unity.h>

void setUp()
{
    ssh_transport_init(0);
    pc_ssh_auth_set_password_cb(NULL);
}
void tearDown()
{
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

static proto_bool check_uv(const char *u, const char *p)
{
    return strcmp(u, "alice") == 0 && strcmp(p, "s3cret") == 0;
}

// USERAUTH_REQUEST(50) || user || "ssh-connection" || "keyboard-interactive" || lang || submethods.
static size_t build_kbdint_request(uint8_t *pkt, const char *user)
{
    size_t n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(pkt + n, user);
    n += put_string(pkt + n, "ssh-connection");
    n += put_string(pkt + n, "keyboard-interactive");
    n += put_string(pkt + n, ""); // language tag (deprecated)
    n += put_string(pkt + n, ""); // submethods
    return n;
}

// INFO_RESPONSE(61) || uint32 num-responses || response strings.
static size_t build_info_response(uint8_t *pkt, uint32_t nr, const char *resp)
{
    size_t n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_INFO_RESPONSE;
    pkt[n++] = (uint8_t)(nr >> 24);
    pkt[n++] = (uint8_t)(nr >> 16);
    pkt[n++] = (uint8_t)(nr >> 8);
    pkt[n++] = (uint8_t)nr;
    if (resp)
    {
        n += put_string(pkt + n, resp);
    }
    return n;
}

// A keyboard-interactive request produces exactly one non-echoed "Password: " prompt.
void test_kbdint_request_prompts()
{
    pc_ssh_auth_set_password_cb(check_uv);
    uint8_t pkt[128];
    size_t n = build_kbdint_request(pkt, "alice");
    uint8_t out[128];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_INFO_REQUEST, out[0]); // 60
    // name(4)=0 || instruction(4)=0 || lang(4)=0 || num-prompts(4)=1 || string("Password: ") || echo=0
    size_t o = 1;
    for (int f = 0; f < 3; f++) // name, instruction, language are all empty
    {
        TEST_ASSERT_EQUAL_UINT32(0, ((uint32_t)out[o] << 24) | ((uint32_t)out[o + 1] << 16) |
                                        ((uint32_t)out[o + 2] << 8) | out[o + 3]);
        o += 4;
    }
    uint32_t np = ((uint32_t)out[o] << 24) | ((uint32_t)out[o + 1] << 16) | ((uint32_t)out[o + 2] << 8) | out[o + 3];
    o += 4;
    TEST_ASSERT_EQUAL_UINT32(1, np); // one prompt
    uint32_t pl = ((uint32_t)out[o] << 24) | ((uint32_t)out[o + 1] << 16) | ((uint32_t)out[o + 2] << 8) | out[o + 3];
    o += 4;
    TEST_ASSERT_EQUAL_UINT32(10, pl); // strlen("Password: ")
    TEST_ASSERT_EQUAL_MEMORY("Password: ", out + o, 10);
    o += pl;
    TEST_ASSERT_EQUAL_UINT8(0, out[o]); // echo = FALSE
}

// The full exchange with the right password authenticates the session.
void test_kbdint_correct_password_succeeds()
{
    pc_ssh_auth_set_password_cb(check_uv);
    uint8_t pkt[128], out[128];
    size_t n, olen = 0;
    n = build_kbdint_request(pkt, "alice");
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_INFO_REQUEST, out[0]);

    n = build_info_response(pkt, 1, "s3cret");
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_info_response(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_SUCCESS, out[0]); // 52
    TEST_ASSERT_TRUE(ssh_sess[0].authed);
}

// A wrong response is rejected and does not authenticate.
void test_kbdint_wrong_password_fails()
{
    pc_ssh_auth_set_password_cb(check_uv);
    uint8_t pkt[128], out[128];
    size_t n, olen = 0;
    n = build_kbdint_request(pkt, "alice");
    pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out));
    n = build_info_response(pkt, 1, "wrong");
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_info_response(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_FAILURE, out[0]); // 51
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
}

// An INFO_RESPONSE with no exchange armed is rejected (cannot skip the request).
void test_kbdint_response_without_request_fails()
{
    pc_ssh_auth_set_password_cb(check_uv);
    uint8_t pkt[128], out[128];
    size_t olen = 0;
    size_t n = build_info_response(pkt, 1, "s3cret");
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_info_response(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
}

// num-responses must match the single prompt; 0 responses fails cleanly.
void test_kbdint_zero_responses_fails()
{
    pc_ssh_auth_set_password_cb(check_uv);
    uint8_t pkt[128], out[128];
    size_t n, olen = 0;
    n = build_kbdint_request(pkt, "alice");
    pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out));
    n = build_info_response(pkt, 0, NULL);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_info_response(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
}

// Replay: a second INFO_RESPONSE after the exchange was consumed is rejected.
void test_kbdint_response_replay_fails()
{
    pc_ssh_auth_set_password_cb(check_uv);
    uint8_t pkt[128], out[128];
    size_t n, olen = 0;
    n = build_kbdint_request(pkt, "alice");
    pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out));
    n = build_info_response(pkt, 1, "s3cret");
    pc_ssh_auth_handle_info_response(0, pkt, n, out, &olen, sizeof(out)); // consumes the exchange
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_info_response(0, pkt, n, out, &olen, sizeof(out)));
}

// The USERAUTH_FAILURE method list advertises keyboard-interactive.
void test_methods_list_advertises_kbdint()
{
    uint8_t out[128];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_build_failure(out, &olen, sizeof(out), PROTO_FALSE));
    uint32_t ml = ((uint32_t)out[1] << 24) | ((uint32_t)out[2] << 16) | ((uint32_t)out[3] << 8) | out[4];
    char list[128];
    memcpy(list, out + 5, ml);
    list[ml] = '\0';
    TEST_ASSERT_NOT_NULL(strstr(list, "keyboard-interactive"));
}

// A keyboard-interactive request needs a verifier to challenge against, and room for the prompt.
void test_kbdint_request_without_verifier_or_room()
{
    uint8_t pkt[128], out[128];
    size_t n = build_kbdint_request(pkt, "alice");
    size_t olen = 0;

    pc_ssh_auth_set_password_cb(NULL); // nothing could ever verify an answer -> do not prompt
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_FAILURE, out[0]);

    pc_ssh_auth_set_password_cb(check_uv); // verifier present, but the prompt does not fit
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, 8));
}

// The INFO_RESPONSE reader rejects an out-of-range slot, an empty or mistyped payload, a payload
// with no room for the num-responses field, a truncated response string, and an exchange whose
// verifier was removed while it was in flight.
void test_kbdint_info_response_wire_guards()
{
    uint8_t pkt[128], req[128], out[128];
    size_t olen = 0;
    pc_ssh_auth_set_password_cb(check_uv);
    size_t rq = build_kbdint_request(req, "alice");

    size_t n = build_info_response(pkt, 1, "s3cret");
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_info_response(MAX_SSH_CONNS, pkt, n, out, &olen, sizeof(out)));

    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, req, rq, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_info_response(0, pkt, 0, out, &olen, sizeof(out))); // empty

    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, req, rq, out, &olen, sizeof(out)));
    n = build_info_response(pkt, 1, "s3cret");
    pkt[0] = SSH_MSG_USERAUTH_REQUEST; // some other message number
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_info_response(0, pkt, n, out, &olen, sizeof(out)));

    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, req, rq, out, &olen, sizeof(out)));
    uint8_t stub[3] = {SSH_MSG_USERAUTH_INFO_RESPONSE, 0, 0}; // no room for num-responses
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_info_response(0, stub, sizeof(stub), out, &olen, sizeof(out)));

    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, req, rq, out, &olen, sizeof(out)));
    uint8_t trunc[9] = {SSH_MSG_USERAUTH_INFO_RESPONSE, 0, 0, 0, 1, 0, 0, 0, 40}; // declares 40 bytes
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_info_response(0, trunc, sizeof(trunc), out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_FAILURE, out[0]);

    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, req, rq, out, &olen, sizeof(out)));
    pc_ssh_auth_set_password_cb(NULL); // verifier removed mid-exchange
    n = build_info_response(pkt, 1, "s3cret");
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_info_response(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
}

// ---- dispatcher integration ----------------------------------------------

static uint8_t emt_type[16];
static int emt_n;
static void rec_emit(uint8_t slot, const uint8_t *p, size_t n)
{
    (void)slot;
    if (emt_n < 16 && n > 0)
    {
        emt_type[emt_n++] = p[0];
    }
}

// Arm a keyboard-interactive exchange through the message dispatcher and answer it.
static int dispatch_kbdint_round(const char *answer)
{
    uint8_t pkt[128];
    size_t n = build_kbdint_request(pkt, "alice");
    int rc = pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_REQUEST, pkt, n);
    if (rc != 0)
    {
        return rc;
    }
    n = build_info_response(pkt, 1, answer);
    return pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_INFO_RESPONSE, pkt, n);
}

// The dispatcher only accepts an INFO_RESPONSE mid-userauth and only for an armed exchange; an
// armed round with the right answer emits the prompt then USERAUTH_SUCCESS and authenticates.
void test_kbdint_dispatch_guards_and_success()
{
    pc_ssh_auth_set_password_cb(check_uv);
    pc_ssh_server_set_emit_cb(rec_emit);
    uint8_t pkt[128];
    size_t n = build_info_response(pkt, 1, "s3cret");

    ssh_sess[0].phase = SSH_PHASE_SERVICE; // userauth has not started
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_INFO_RESPONSE, pkt, n));

    ssh_sess[0].phase = SSH_PHASE_AUTH; // right phase, nothing armed
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_INFO_RESPONSE, pkt, n));

    emt_n = 0;
    ssh_sess[0].phase = SSH_PHASE_AUTH;
    TEST_ASSERT_EQUAL_INT(0, dispatch_kbdint_round("s3cret"));
    TEST_ASSERT_EQUAL_INT(2, emt_n);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_INFO_REQUEST, emt_type[0]);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_SUCCESS, emt_type[1]);
    TEST_ASSERT_TRUE(ssh_sess[0].authed);
    TEST_ASSERT_EQUAL(SSH_PHASE_OPEN, ssh_sess[0].phase);
}

// A failed INFO_RESPONSE counts toward the per-connection brute-force limit exactly as a failed
// USERAUTH_REQUEST does: the last attempt emits SSH_MSG_DISCONNECT and closes (RFC 4252 §4).
void test_kbdint_dispatch_failures_hit_the_limit()
{
    pc_ssh_auth_set_password_cb(check_uv);
    pc_ssh_server_set_emit_cb(rec_emit);
    for (int k = 1; k < SSH_MAX_AUTH_ATTEMPTS; k++)
    {
        ssh_sess[0].phase = SSH_PHASE_AUTH;
        emt_n = 0;
        TEST_ASSERT_EQUAL_INT(0, dispatch_kbdint_round("wrong"));
        TEST_ASSERT_EQUAL_INT(2, emt_n);
        TEST_ASSERT_EQUAL_UINT8(SSH_MSG_USERAUTH_FAILURE, emt_type[1]);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)k, ssh_sess[0].auth_failures);
    }
    ssh_sess[0].phase = SSH_PHASE_AUTH;
    emt_n = 0;
    TEST_ASSERT_EQUAL_INT(-1, dispatch_kbdint_round("wrong"));
    TEST_ASSERT_EQUAL_INT(3, emt_n);
    TEST_ASSERT_EQUAL_UINT8(SSH_MSG_DISCONNECT, emt_type[2]);
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
}

// ---------------------------------------------------------------------------
// Full pc_ssh_server_dispatch() switch coverage: every SSH_MSG_* arm of the
// message dispatcher (ssh_server.cpp) driven once. Built with keyboard-interactive
// on, this env compiles the SSH_MSG_USERAUTH_INFO_RESPONSE arm, so the whole switch
// (all 19 arms incl. default) is reachable here.
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
static void dsp_load_rsa_hostkey()
{
    TEST_ASSERT_TRUE(pc_nvs_put_blob(PC_SSH_HOST_KEY_NS, PC_SSH_HOST_KEY_ITEM, PC_SSH_BASELINE_KEY_DER,
                                     PC_SSH_BASELINE_KEY_DER_LEN));
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_rsa_load_pubkey());
}
static size_t put_namelist(uint8_t *p, const char *s)
{
    return put_string(p, s); // a name-list is wire-identical to a string
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
static proto_bool dsp_pw_cb(const char *u, const char *p)
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

// Drive the whole message switch through the dispatcher: a full handshake advances the phase so the
// post-auth arms are reachable, then the remaining stateless arms are poked once each.
void test_dispatch_all_switch_arms()
{
    dsp_load_rsa_hostkey();
    ssh_transport_init(0);
    pc_ssh_channel_init(0);
    pc_ssh_auth_set_password_cb(dsp_pw_cb);
    pc_ssh_channel_set_data_cb(dsp_on_chan_data);
    pc_ssh_server_set_emit_cb(dsp_emit);

    SshSession *s = &ssh_sess[0];
    strcpy(s->v_c, "SSH-2.0-DispatchClient");
    s->v_c_len = (uint16_t)strlen(s->v_c);
    s->phase = SSH_PHASE_KEXINIT;

    uint8_t pkt[2048];
    size_t n = 0;

    // KEXINIT -> server KEXINIT, phase DH_INIT.
    n = build_kexinit_ext(pkt);
    dsp_reset();
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_KEXINIT, pkt, n));
    TEST_ASSERT_EQUAL(SSH_PHASE_DH_INIT, s->phase);

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
    TEST_ASSERT_EQUAL(SSH_PHASE_SERVICE, s->phase);

    // SERVICE_REQUEST -> SERVICE_ACCEPT, AUTH phase.
    n = 0;
    pkt[n++] = SSH_MSG_SERVICE_REQUEST;
    n += put_string(pkt + n, "ssh-userauth");
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, SSH_MSG_SERVICE_REQUEST, pkt, n));
    TEST_ASSERT_EQUAL(SSH_PHASE_AUTH, s->phase);

    // USERAUTH_INFO_RESPONSE arm (keyboard-interactive): valid phase but nothing armed -> handler fails.
    uint8_t ir[5] = {SSH_MSG_USERAUTH_INFO_RESPONSE, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_INFO_RESPONSE, ir, sizeof(ir)));

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
}

// The rejection half of every guarded switch arm: wrong-phase, unauthenticated, and malformed-payload
// inputs each return -1 (or drop silently), covering the guard branches the happy path does not.
void test_dispatch_guard_and_error_arms()
{
    dsp_load_rsa_hostkey();
    ssh_transport_init(0);
    pc_ssh_channel_init(0);
    pc_ssh_auth_set_password_cb(dsp_pw_cb);
    pc_ssh_channel_set_data_cb(dsp_on_chan_data);
    pc_ssh_server_set_emit_cb(dsp_emit);

    SshSession *s = &ssh_sess[0];

    // KEXINIT with a payload too short to negotiate -> parse fails.
    s->phase = SSH_PHASE_KEXINIT;
    uint8_t badkex[4] = {SSH_MSG_KEXINIT, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_KEXINIT, badkex, sizeof(badkex)));

    // KEXDH_INIT outside DH_INIT phase -> rejected; in phase but malformed -> handler fails.
    s->phase = SSH_PHASE_KEXINIT;
    uint8_t badkexdh[4] = {SSH_MSG_KEXDH_INIT, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_KEXDH_INIT, badkexdh, sizeof(badkexdh)));
    s->phase = SSH_PHASE_DH_INIT;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_KEXDH_INIT, badkexdh, sizeof(badkexdh)));

    // SERVICE_REQUEST outside SERVICE phase -> rejected; in phase but truncated -> handler fails.
    s->phase = SSH_PHASE_AUTH;
    uint8_t badsvc[2] = {SSH_MSG_SERVICE_REQUEST, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_SERVICE_REQUEST, badsvc, sizeof(badsvc)));
    s->phase = SSH_PHASE_SERVICE;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_SERVICE_REQUEST, badsvc, sizeof(badsvc)));

    // USERAUTH_REQUEST outside AUTH phase -> rejected; in phase but truncated -> handler fails.
    s->phase = SSH_PHASE_SERVICE;
    uint8_t badua[2] = {SSH_MSG_USERAUTH_REQUEST, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_REQUEST, badua, sizeof(badua)));
    s->phase = SSH_PHASE_AUTH;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_REQUEST, badua, sizeof(badua)));

    // USERAUTH_INFO_RESPONSE outside AUTH phase -> rejected; in phase but nothing armed -> handler fails.
    uint8_t badir[5] = {SSH_MSG_USERAUTH_INFO_RESPONSE, 0, 0, 0, 0};
    s->phase = SSH_PHASE_SERVICE;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_INFO_RESPONSE, badir, sizeof(badir)));
    s->phase = SSH_PHASE_AUTH;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, SSH_MSG_USERAUTH_INFO_RESPONSE, badir, sizeof(badir)));

    // A wrong password below the limit -> FAILURE, connection stays open (limit not tripped).
    s->phase = SSH_PHASE_AUTH;
    s->authed = PROTO_FALSE;
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
    s->authed = PROTO_FALSE;
    const uint8_t authed_arms[] = {SSH_MSG_GLOBAL_REQUEST,       SSH_MSG_CHANNEL_OPEN,    SSH_MSG_CHANNEL_OPEN_CONFIRM,
                                   SSH_MSG_CHANNEL_OPEN_FAILURE, SSH_MSG_CHANNEL_REQUEST, SSH_MSG_CHANNEL_DATA};
    for (size_t j = 0; j < sizeof(authed_arms) / sizeof(authed_arms[0]); j++)
    {
        uint8_t p[8] = {authed_arms[j], 0, 0, 0, 0, 0, 0, 0};
        TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, authed_arms[j], p, sizeof(p)));
    }

    // Authenticated but malformed -> the arm's handler fails.
    s->authed = PROTO_TRUE;
    const uint8_t handler_arms[] = {SSH_MSG_GLOBAL_REQUEST, SSH_MSG_CHANNEL_OPEN, SSH_MSG_CHANNEL_REQUEST,
                                    SSH_MSG_CHANNEL_DATA};
    for (size_t j = 0; j < sizeof(handler_arms) / sizeof(handler_arms[0]); j++)
    {
        uint8_t p[2] = {handler_arms[j], 0};
        TEST_ASSERT_EQUAL_INT(-1, pc_ssh_server_dispatch(0, handler_arms[j], p, sizeof(p)));
    }

    // With no emit callback wired, a reply-producing dispatch (UNIMPLEMENTED) drops the frame.
    pc_ssh_server_set_emit_cb(NULL);
    ssh_pkt[0].seq_no_recv = 1;
    uint8_t unk = 201;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_server_dispatch(0, 201, &unk, 1));
    pc_ssh_server_set_emit_cb(dsp_emit);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_dispatch_all_switch_arms);
    RUN_TEST(test_dispatch_guard_and_error_arms);
    RUN_TEST(test_kbdint_request_prompts);
    RUN_TEST(test_kbdint_correct_password_succeeds);
    RUN_TEST(test_kbdint_wrong_password_fails);
    RUN_TEST(test_kbdint_response_without_request_fails);
    RUN_TEST(test_kbdint_zero_responses_fails);
    RUN_TEST(test_kbdint_response_replay_fails);
    RUN_TEST(test_methods_list_advertises_kbdint);
    RUN_TEST(test_kbdint_request_without_verifier_or_room);
    RUN_TEST(test_kbdint_info_response_wire_guards);
    RUN_TEST(test_kbdint_dispatch_guards_and_success);
    RUN_TEST(test_kbdint_dispatch_failures_hit_the_limit);
    return UNITY_END();
}
