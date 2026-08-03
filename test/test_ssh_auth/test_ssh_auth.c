// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SSH user-authentication tests (RFC 4252): service request/accept, request
// parsing, and the password method.

#include "baseline_keys.h"
#include "crypto/aead/aesgcm.h"
#include "crypto/asymmetric/ecdsa.h"
#include "crypto/asymmetric/ed25519.h"
#include "network_drivers/presentation/ssh/auth/ssh_auth.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include "network_drivers/tls/ssh_rsa.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

// The keyed api needs a context, not a key. These are one-shot vectors, so one scratch context is
// enough - rebuilt per call, and released first so a backend that attaches vendor resources to a
// context (ESP's mbedtls does) does not leak one per vector.
static uint8_t g_gcm_ws[PC_WORK_AESGCM] __attribute__((aligned(8)));
static proto_bool g_gcm_live = PROTO_FALSE;
// One hex digit to its value.
static int nib(char c)
{
    return c >= 'a' ? c - 'a' + 10 : c - '0';
}

static struct pc_aesgcm_key *gcm_key(const uint8_t *key)
{
    if (g_gcm_live)
    {
        pc_aesgcm_key_wipe((struct pc_aesgcm_key *)(g_gcm_ws));
    }
    g_gcm_live = PROTO_TRUE;
    return pc_aesgcm_key_init(g_gcm_ws, key);
}

// Native RSA sign fixture (defined in ssh_rsa.cpp native path) - lets a test forge a
// genuine client signature with a private key we control.
extern uint8_t _test_rsa_n[256];
extern uint8_t _test_rsa_d[256];
extern uint8_t _test_rsa_e[4];

void setUp()
{
    ssh_transport_init(0);
    pc_ssh_auth_set_password_cb(NULL);
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

static proto_bool check_uv(const char *u, const char *p)
{
    return strcmp(u, "alice") == 0 && strcmp(p, "s3cret") == 0;
}

// ---- service request ------------------------------------------------------

void test_service_request_accept()
{
    uint8_t pkt[64];
    size_t n = 0;
    pkt[n++] = SSH_MSG_SERVICE_REQUEST;
    n += put_string(pkt + n, "ssh-userauth");

    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_service_request(pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_SERVICE_ACCEPT, out[0]);
    uint32_t sl = ((uint32_t)out[1] << 24) | ((uint32_t)out[2] << 16) | ((uint32_t)out[3] << 8) | out[4];
    TEST_ASSERT_EQUAL_UINT32(12, sl);
    TEST_ASSERT_EQUAL_MEMORY("ssh-userauth", out + 5, 12);
}

void test_service_request_rejects_unknown()
{
    uint8_t pkt[64];
    size_t n = 0;
    pkt[n++] = SSH_MSG_SERVICE_REQUEST;
    n += put_string(pkt + n, "ssh-connection");
    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_service_request(pkt, n, out, &olen, sizeof(out)));
}

// ---- request parsing ------------------------------------------------------

void test_parse_password_request()
{
    uint8_t pkt[128];
    size_t n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(pkt + n, "alice");
    n += put_string(pkt + n, "ssh-connection");
    n += put_string(pkt + n, "password");
    pkt[n++] = 0; // change-password = FALSE
    n += put_string(pkt + n, "s3cret");

    SshAuthReq req;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_parse_request(pkt, n, &req));
    TEST_ASSERT_TRUE(req.is_password);
    TEST_ASSERT_EQUAL_STRING("alice", req.user);
    TEST_ASSERT_EQUAL_STRING("ssh-connection", req.service);
    TEST_ASSERT_EQUAL_STRING("password", req.method);
    TEST_ASSERT_EQUAL_STRING("s3cret", req.password);
}

void test_parse_none_request()
{
    uint8_t pkt[128];
    size_t n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(pkt + n, "bob");
    n += put_string(pkt + n, "ssh-connection");
    n += put_string(pkt + n, "none");

    SshAuthReq req;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_parse_request(pkt, n, &req));
    TEST_ASSERT_FALSE(req.is_password);
    TEST_ASSERT_EQUAL_STRING("bob", req.user);
}

// ---- orchestration --------------------------------------------------------

static size_t build_pw_request(uint8_t *pkt, const char *u, const char *p)
{
    size_t n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(pkt + n, u);
    n += put_string(pkt + n, "ssh-connection");
    n += put_string(pkt + n, "password");
    pkt[n++] = 0;
    n += put_string(pkt + n, p);
    return n;
}

void test_handle_request_success()
{
    pc_ssh_auth_set_password_cb(check_uv);
    uint8_t pkt[128];
    size_t n = build_pw_request(pkt, "alice", "s3cret");

    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_SUCCESS, out[0]);
    TEST_ASSERT_TRUE(ssh_sess[0].authed);
    TEST_ASSERT_EQUAL(SSH_PHASE_OPEN, ssh_sess[0].phase);
}

void test_handle_request_wrong_password_fails()
{
    pc_ssh_auth_set_password_cb(check_uv);
    uint8_t pkt[128];
    size_t n = build_pw_request(pkt, "alice", "wrong");

    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
    // Failure advertises the "password" method.
    proto_bool adv = PROTO_FALSE;
    for (size_t k = 0; k + 8 <= olen; k++)
    {
        if (memcmp(out + k, "password", 8) == 0)
        {
            adv = PROTO_TRUE;
            break;
        }
    }
    TEST_ASSERT_TRUE(adv);
}

void test_handle_none_request_fails_without_auth()
{
    pc_ssh_auth_set_password_cb(check_uv);
    uint8_t pkt[64];
    size_t n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(pkt + n, "alice");
    n += put_string(pkt + n, "ssh-connection");
    n += put_string(pkt + n, "none");

    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
}

void test_handle_request_no_callback_fails()
{
    // No callback installed → all credentials rejected.
    uint8_t pkt[128];
    size_t n = build_pw_request(pkt, "alice", "s3cret");
    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
}

// ---- publickey method (RFC 4252 §7) ---------------------------------------

// Real "ssh-rsa" blob and a genuine signature over the exact publickey
// signed-data (session_id = bytes 0..31, user "alice", service
// "ssh-connection"), produced with openssl against a real RSA-2048 key.
static const char *PK_BLOB_HEX =
    "000000077373682d727361000000030100010000010100932e817a74435fdd1d7c7c5e8ec9b0f2a8b06b6b3db9b03e907"
    "d010d2c003985deaeb56d1ac5734116772da333131f8eb0b5bec0eff6f4dfd612068d2857acefa7dc75e84b0f2ffca57e"
    "82297f4f085b8584caebaa5a4be51c6f5887529f84fbee5f6940a31307d1224714705ca5cf47ca9e04d2a9faafe7b022b"
    "e41f426a1868f61410141a0a8b39110107f59d9c5a5920e8e2921ccaf85672ba0f860644f793f7c38425018c17e915a5f"
    "18ba5cfcf002a6a8fc50eeb08bc2f1e81f1df69704aacfce062bcd9ef678197e9778d411f3f364959637d09d56c6adc59"
    "8147a6f924d9075a3df5dd2c606a9e963afd49fe56b4633e5e905871d0b31093954ec57ff";
static const char *PK_SIG_HEX =
    "25044aeea46af01d38e0b09b626f43be788a30a156ff8b16bf5f6a637ef47f0066f6dda53fd2fe60ea8f4fe82f94988d3"
    "dfc0da02aea3d578a884069904124385e7bd16297135b86ee4d44071493edc1b9f18528f0c03c16069ece8a552da8ccab"
    "f72a846d684c43d97fc55003553f8978ded240b63b2b50bc37f1f7aa3e9148018a5e3a3a1823288b8ae003dc45498ab8f"
    "ced85bf14899c47f590b22dfdf78758be99d281f790d86c9d84bc629e26ddfb6b95cb0cb5b8b1ef6daec18480091eb364"
    "f594edcde4f02cd9a1e8a08c6267ff7221e279159d59a0502d261499ed7b9b96c51da7db03b421d316e14f994ecba90d3"
    "7f0e1841747182863d0433f964a";

static size_t hexdec(const char *h, uint8_t *out)
{
    size_t n = 0;
    for (; h[0] && h[1]; h += 2)
    {

        out[n++] = (uint8_t)((nib(h[0]) << 4) | nib(h[1]));
    }
    return n;
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static proto_bool pk_cb_alice(const char *u, const uint8_t *blob, size_t n)
{
    (void)blob;
    (void)n;
    return strcmp(u, "alice") == 0;
}

static size_t build_pubkey_req(uint8_t *pkt, const uint8_t *blob, size_t blob_len, const uint8_t *sig, size_t sig_len,
                               proto_bool with_sig)
{
    size_t n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(pkt + n, "alice");
    n += put_string(pkt + n, "ssh-connection");
    n += put_string(pkt + n, "publickey");
    pkt[n++] = with_sig ? 1 : 0;
    n += put_string(pkt + n, "rsa-sha2-256");
    wr_u32(pkt + n, (uint32_t)blob_len);
    n += 4;
    memcpy(pkt + n, blob, blob_len);
    n += blob_len;
    if (with_sig)
    {
        uint32_t inner = 4 + 12 + 4 + (uint32_t)sig_len;
        wr_u32(pkt + n, inner);
        n += 4;
        n += put_string(pkt + n, "rsa-sha2-256");
        wr_u32(pkt + n, (uint32_t)sig_len);
        n += 4;
        memcpy(pkt + n, sig, sig_len);
        n += sig_len;
    }
    return n;
}

static void set_session_id_0_to_31()
{
    for (int j = 0; j < 32; j++)
    {
        ssh_sess[0].session_id[j] = (uint8_t)j;
    }
    ssh_sess[0].session_id_len = 32; // a completed SHA-256 KEX
    ssh_sess[0].have_session_id = PROTO_TRUE;
}

void test_pubkey_probe_returns_pk_ok()
{
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
    set_session_id_0_to_31();
    uint8_t blob[512];
    size_t blob_len = hexdec(PK_BLOB_HEX, blob);

    uint8_t pkt[1024];
    size_t n = build_pubkey_req(pkt, blob, blob_len, NULL, 0, PROTO_FALSE);
    uint8_t out[1024];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_PK_OK, out[0]);
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
}

void test_pubkey_valid_signature_succeeds()
{
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
    set_session_id_0_to_31();
    uint8_t blob[512], sig[256];
    size_t blob_len = hexdec(PK_BLOB_HEX, blob);
    size_t sig_len = hexdec(PK_SIG_HEX, sig);

    uint8_t pkt[1024];
    size_t n = build_pubkey_req(pkt, blob, blob_len, sig, sig_len, PROTO_TRUE);
    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_SUCCESS, out[0]);
    TEST_ASSERT_TRUE(ssh_sess[0].authed);
    TEST_ASSERT_EQUAL(SSH_PHASE_OPEN, ssh_sess[0].phase);
}

// Encode an SSH mpint from a big-endian value (strip leading zeros, prepend 0x00 if the
// high bit is set). Returns bytes written.
static size_t put_mpint(uint8_t *p, const uint8_t *v, size_t vlen)
{
    size_t off = 0;
    while (off < vlen && v[off] == 0)
    {
        off++;
    }
    proto_bool pad = (off < vlen) && (v[off] & 0x80);
    uint32_t mlen = (uint32_t)(vlen - off) + (pad ? 1u : 0u);
    wr_u32(p, mlen);
    size_t n = 4;
    if (pad)
    {
        p[n++] = 0x00;
    }
    memcpy(p + n, v + off, vlen - off);
    return n + (vlen - off);
}

// RSA-2048 keypair (identical to the rsa-sha2-512 KAT key in test_ssh_crypto) so this test can
// forge a genuine client signature. NEVER use for actual SSH.
static const char *AUTH_RSA_N = "beeda21e84ecc2e3335ce4f4f247ba4847d0bf23cc335effe99cbf54bf7e7428"
                                "8a9a06d130f34b760071146b4689ac0f04abe7cad4c883a163ef98446b28b7ad"
                                "5177c509fd5810b08e1acac05128496bfec0966ad69921366949d7b8b1d7e17f"
                                "35b33b0681fc64afe7d3056b90293f757996648680ec195b1f45fb517f34529b"
                                "ab86a3669afa957e4156820b2405ef560f1da6cd77b6f8a6a4298a03698ac1de"
                                "4bc4884bcc2325eb6b59e3476fa03abd539ebffeadf52da5ecbf8a28ef056aaa"
                                "b157efd5fb2a59d9394a007978a3cdb1e2e8018060537518b6ab0854da88ed25"
                                "4cc63a52b1332a4631522a9a84577acead26bbefab695e5502a9f9e14421b73d";
static const char *AUTH_RSA_D = "03a9d89e004bf0b35e556e793abae09aa9721a70cbe6c27063a1a3d432f670b1"
                                "2473af24cd6d25aa067924fca7f6554c56791bf1fae23c1059340c3667ddf8a4"
                                "4537689af7f6fc1eff230977e636c12de6cdf834e5983b98692dc70b5eb2373b"
                                "f32254c41bb36595307c0e9311499153a6391a05b0ac9711f6082839d8987eeb"
                                "4042247dc8f321efc730abf53170b02b55aba49d7e2323c782ebfebb34b3c634"
                                "f34d0fd1cc81088c9c7db441169b1e26a3ad39d5d2e43b0ebe9b6fc6e71931f8"
                                "a255d837862f830a3c82f2fb31ae5b47138bfed232aeeb74ddf766483edea5e1"
                                "60f4dbe3cb313587a642e63caf60dcedddc4b229f072ef1f4cc8e2c5cd5e7401";

// Server-sig-algs advertises rsa-sha2-512; prove the auth layer actually verifies a client's
// rsa-sha2-512 signature. The verify hash is chosen from pk_algo: if the code ignored it and used
// SHA-256, this genuine SHA-512 signature would fail and auth would return FAILURE.
void test_pubkey_rsa_sha512_signature_succeeds()
{
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
    set_session_id_0_to_31();

    // Install the private key into the native RSA sign fixture, e = 65537.
    hexdec(AUTH_RSA_N, _test_rsa_n);
    hexdec(AUTH_RSA_D, _test_rsa_d);
    _test_rsa_e[0] = 0x00;
    _test_rsa_e[1] = 0x01;
    _test_rsa_e[2] = 0x00;
    _test_rsa_e[3] = 0x01;

    // Matching ssh-rsa public blob = string("ssh-rsa") || mpint(e) || mpint(n).
    uint8_t nbytes[256];
    hexdec(AUTH_RSA_N, nbytes);
    const uint8_t ebytes[3] = {0x01, 0x00, 0x01};
    uint8_t blob[300];
    size_t bl = put_string(blob, "ssh-rsa");
    bl += put_mpint(blob + bl, ebytes, 3);
    bl += put_mpint(blob + bl, nbytes, 256);

    // Signed prefix P (RFC 4252 §7): msg .. blob, has_signature = 1, algo = rsa-sha2-512.
    uint8_t P[512];
    size_t pn = 0;
    P[pn++] = SSH_MSG_USERAUTH_REQUEST;
    pn += put_string(P + pn, "alice");
    pn += put_string(P + pn, "ssh-connection");
    pn += put_string(P + pn, "publickey");
    P[pn++] = 1; // has_signature (part of the signed data)
    pn += put_string(P + pn, "rsa-sha2-512");
    wr_u32(P + pn, (uint32_t)bl);
    pn += 4;
    memcpy(P + pn, blob, bl);
    pn += bl;

    // signed_data = string(session_id) || P; sign it with SHA-512.
    uint8_t sd[700];
    size_t sn = 0;
    wr_u32(sd + sn, 32);
    sn += 4;
    memcpy(sd + sn, ssh_sess[0].session_id, 32);
    sn += 32;
    memcpy(sd + sn, P, pn);
    sn += pn;
    uint8_t sig[256];
    TEST_ASSERT_EQUAL_INT(0, ssh_rsa_sign(sd, sn, PC_RSA_HASH_SHA512, sig));

    // Full request = P || string( string("rsa-sha2-512") || string(sig) ).
    uint8_t pkt[1024];
    memcpy(pkt, P, pn);
    size_t n = pn;
    wr_u32(pkt + n, 4 + 12 + 4 + 256); // inner sig-blob length
    n += 4;
    n += put_string(pkt + n, "rsa-sha2-512");
    wr_u32(pkt + n, 256);
    n += 4;
    memcpy(pkt + n, sig, 256);
    n += 256;

    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_SUCCESS, out[0]);
    TEST_ASSERT_TRUE(ssh_sess[0].authed);
}

// Server-sig-algs advertises ecdsa-sha2-nistp256; prove the auth layer verifies a genuine
// client P-256 signature (RFC 5656). The signature is forged at test time with a key we control.
void test_pubkey_ecdsa_signature_succeeds()
{
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
    set_session_id_0_to_31();

    uint8_t d[32];
    memset(d, 0, 32);
    d[31] = 0x0A; // client private scalar
    uint8_t q[PC_ECDSA_P256_PUB_LEN];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(q, d));

    // pubkey blob = string("ecdsa-sha2-nistp256") || string("nistp256") || string(Q).
    uint8_t blob[200];
    size_t bl = 0;
    bl += put_string(blob + bl, "ecdsa-sha2-nistp256");
    bl += put_string(blob + bl, "nistp256");
    wr_u32(blob + bl, PC_ECDSA_P256_PUB_LEN);
    bl += 4;
    memcpy(blob + bl, q, PC_ECDSA_P256_PUB_LEN);
    bl += PC_ECDSA_P256_PUB_LEN;

    // Signed prefix P: msg .. blob, has_signature = 1, algo = ecdsa-sha2-nistp256.
    uint8_t P[512];
    size_t pn = 0;
    P[pn++] = SSH_MSG_USERAUTH_REQUEST;
    pn += put_string(P + pn, "alice");
    pn += put_string(P + pn, "ssh-connection");
    pn += put_string(P + pn, "publickey");
    P[pn++] = 1;
    pn += put_string(P + pn, "ecdsa-sha2-nistp256");
    wr_u32(P + pn, (uint32_t)bl);
    pn += 4;
    memcpy(P + pn, blob, bl);
    pn += bl;

    // signed_data = string(session_id) || P; sign it.
    uint8_t sd[700];
    size_t sn = 0;
    wr_u32(sd + sn, 32);
    sn += 4;
    memcpy(sd + sn, ssh_sess[0].session_id, 32);
    sn += 32;
    memcpy(sd + sn, P, pn);
    sn += pn;
    uint8_t raw[PC_ECDSA_P256_SIG_LEN];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_sign(raw, sd, sn, d));

    // ECDSA signature blob = mpint(r) || mpint(s).
    uint8_t ecblob[80];
    size_t el = 0;
    el += put_mpint(ecblob + el, raw, 32);
    el += put_mpint(ecblob + el, raw + 32, 32);

    // Full request = P || string( string("ecdsa-sha2-nistp256") || string(ecblob) ).
    uint8_t pkt[1024];
    memcpy(pkt, P, pn);
    size_t n = pn;
    wr_u32(pkt + n, 4 + 19 + 4 + (uint32_t)el);
    n += 4;
    n += put_string(pkt + n, "ecdsa-sha2-nistp256");
    wr_u32(pkt + n, (uint32_t)el);
    n += 4;
    memcpy(pkt + n, ecblob, el);
    n += el;

    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_SUCCESS, out[0]);
    TEST_ASSERT_TRUE(ssh_sess[0].authed);
}

void test_pubkey_tampered_signature_fails()
{
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
    set_session_id_0_to_31();
    uint8_t blob[512], sig[256];
    size_t blob_len = hexdec(PK_BLOB_HEX, blob);
    size_t sig_len = hexdec(PK_SIG_HEX, sig);
    sig[100] ^= 0x01;

    uint8_t pkt[1024];
    size_t n = build_pubkey_req(pkt, blob, blob_len, sig, sig_len, PROTO_TRUE);
    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
}

void test_pubkey_unauthorized_key_fails()
{
    pc_ssh_auth_set_pubkey_cb(NULL); // no key authorized
    set_session_id_0_to_31();
    uint8_t blob[512], sig[256];
    size_t blob_len = hexdec(PK_BLOB_HEX, blob);
    size_t sig_len = hexdec(PK_SIG_HEX, sig);

    uint8_t pkt[1024];
    size_t n = build_pubkey_req(pkt, blob, blob_len, sig, sig_len, PROTO_TRUE);
    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
}

// ---- ssh-ed25519 client key (RFC 8709) ------------------------------------

// Assemble a publickey USERAUTH_REQUEST for an ssh-ed25519 key. Without with_sig this is
// exactly the signed prefix; the signature blob is string("ssh-ed25519") || string(sig).
static size_t build_pubkey_req_ed(uint8_t *pkt, const uint8_t *pub, const uint8_t *sig, proto_bool with_sig,
                                  size_t *prefix_len)
{
    size_t n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(pkt + n, "alice");
    n += put_string(pkt + n, "ssh-connection");
    n += put_string(pkt + n, "publickey");
    pkt[n++] = 1;                            // has_signature TRUE (part of the signed data)
    n += put_string(pkt + n, "ssh-ed25519"); // signature algorithm
    // pubkey blob = string("ssh-ed25519") || string(pub32)
    uint32_t blen = 4 + 11 + 4 + 32;
    wr_u32(pkt + n, blen);
    n += 4;
    n += put_string(pkt + n, "ssh-ed25519");
    wr_u32(pkt + n, 32);
    n += 4;
    memcpy(pkt + n, pub, 32);
    n += 32;
    if (prefix_len)
    {
        *prefix_len = n; // signed prefix ends after the blob
    }
    if (with_sig)
    {
        uint32_t inner = 4 + 11 + 4 + 64;
        wr_u32(pkt + n, inner);
        n += 4;
        n += put_string(pkt + n, "ssh-ed25519");
        wr_u32(pkt + n, 64);
        n += 4;
        memcpy(pkt + n, sig, 64);
        n += 64;
    }
    return n;
}

// A genuine ed25519 client signature (computed here over the RFC 4252 signed data)
// authenticates; flipping any signature byte is rejected.
void test_pubkey_ed25519_valid_signature_succeeds()
{
    const uint8_t *seed = BASELINE_ED25519_SEEDS[0]; // deterministic baseline client key
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
    set_session_id_0_to_31();
    uint8_t pub[32];
    pc_ed25519_pubkey(pub, seed);

    // Build the signed prefix, prepend string(session_id), sign the whole thing.
    uint8_t pkt[512];
    size_t prefix_len = 0;
    build_pubkey_req_ed(pkt, pub, NULL, PROTO_FALSE, &prefix_len);
    uint8_t signed_data[4 + 32 + 512];
    size_t sd = 0;
    wr_u32(signed_data + sd, 32);
    sd += 4;
    memcpy(signed_data + sd, ssh_sess[0].session_id, 32);
    sd += 32;
    memcpy(signed_data + sd, pkt, prefix_len);
    sd += prefix_len;
    uint8_t sig[64];
    pc_ed25519_sign(sig, signed_data, sd, seed);

    size_t n = build_pubkey_req_ed(pkt, pub, sig, PROTO_TRUE, NULL);
    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_SUCCESS, out[0]);
    TEST_ASSERT_TRUE(ssh_sess[0].authed);

    // Tamper: flip a signature byte -> rejected.
    ssh_transport_init(0);
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
    set_session_id_0_to_31();
    sig[10] ^= 0x01;
    n = build_pubkey_req_ed(pkt, pub, sig, PROTO_TRUE, NULL);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
}

// SERVICE_REQUEST parsing rejects a wrong/empty message, a truncated service string,
// and an output buffer too small for SERVICE_ACCEPT.
void test_service_request_errors()
{
    uint8_t out[64], p[64];
    size_t olen = 0;
    p[0] = 99;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_service_request(p, 1, out, &olen, sizeof(out))); // wrong type
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_service_request(p, 0, out, &olen, sizeof(out))); // len 0
    p[0] = SSH_MSG_SERVICE_REQUEST;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_service_request(p, 1, out, &olen, sizeof(out))); // truncated service
    size_t n = 0;
    p[n++] = SSH_MSG_SERVICE_REQUEST;
    n += put_string(p + n, "ssh-userauth");
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_service_request(p, n, out, &olen, 3)); // cap too small
}

// The response builders reject an output buffer too small for their fixed messages.
void test_build_response_guards()
{
    uint8_t out[8];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_build_failure(out, &olen, 2, PROTO_FALSE)); // < 1+4+methods+1
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_build_success(out, &olen, 0));              // cap < 1

    // build_pk_ok via a pubkey probe with a tiny output buffer.
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
    set_session_id_0_to_31();
    uint8_t blob[512];
    size_t blob_len = hexdec(PK_BLOB_HEX, blob);
    uint8_t pkt[1024];
    size_t pn = build_pubkey_req(pkt, blob, blob_len, NULL, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_request(0, pkt, pn, out, &olen, 4));
}

// USERAUTH_REQUEST parsing rejects a truncation at every field, for both the password
// and publickey methods (including the signature blob).
void test_parse_request_truncations()
{
    SshAuthReq req;
    uint8_t p[512];
    size_t n;

    p[0] = 99;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, 1, &req)); // wrong type
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, 0, &req)); // len 0

    p[0] = SSH_MSG_USERAUTH_REQUEST;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, 1, &req)); // user length header truncated

    n = 0;
    p[n++] = SSH_MSG_USERAUTH_REQUEST;
    wr_u32(p + n, 20);
    n += 4; // user claims 20 bytes, none present
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, n, &req));

    n = 0;
    p[n++] = SSH_MSG_USERAUTH_REQUEST;
    char big[300];
    memset(big, 'u', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    n += put_string(p + n, big); // user longer than the fixed buffer
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, n, &req));

    n = 0;
    p[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(p + n, "alice");
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, n, &req)); // service missing

    n = 0;
    p[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(p + n, "alice");
    n += put_string(p + n, "ssh-connection");
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, n, &req)); // method missing

    // password: boolean missing, then password string missing.
    n = 0;
    p[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(p + n, "alice");
    n += put_string(p + n, "ssh-connection");
    n += put_string(p + n, "password");
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, n, &req)); // no boolean
    p[n++] = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, n, &req)); // password missing

    // publickey: has-signature byte missing, then algo, then blob (header and body).
    n = 0;
    p[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(p + n, "alice");
    n += put_string(p + n, "ssh-connection");
    n += put_string(p + n, "publickey");
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, n, &req)); // no has-sig byte
    p[n++] = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, n, &req)); // algo missing
    n += put_string(p + n, "rsa-sha2-256");
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, n, &req)); // blob header missing
    wr_u32(p + n, 100);
    n += 4; // blob claims 100 bytes, none present
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, n, &req));

    // publickey with signature: outer sig blob missing, then inner algo, then raw sig.
    n = 0;
    p[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(p + n, "alice");
    n += put_string(p + n, "ssh-connection");
    n += put_string(p + n, "publickey");
    p[n++] = 1; // has signature
    n += put_string(p + n, "rsa-sha2-256");
    n += put_string(p + n, "blob");
    size_t base = n;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, n, &req)); // outer sig blob missing

    n = base;
    wr_u32(p + n, 2);
    n += 4;
    p[n++] = 0;
    p[n++] = 0; // sig blob len 2: cannot hold the inner sig-algo string
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, n, &req));

    n = base;
    uint8_t sb[32];
    size_t sblen = put_string(sb, "rsa-sha2-256"); // sig blob = algo only, no raw signature
    wr_u32(p + n, (uint32_t)sblen);
    n += 4;
    memcpy(p + n, sb, sblen);
    n += sblen;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_parse_request(p, n, &req));
}

// A malformed ssh-rsa public-key blob fails to load at each field, yielding a
// USERAUTH_FAILURE rather than a crash.
void test_pubkey_blob_parse_failures()
{
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
    set_session_id_0_to_31();
    uint8_t out[512], pkt[1024], b[400];
    size_t olen = 0, bl, pn;

    // empty blob: type string cannot be read.
    pn = build_pubkey_req(pkt, b, 0, NULL, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, pn, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);

    // wrong key type.
    bl = put_string(b, "ssh-dss");
    pn = build_pubkey_req(pkt, b, bl, NULL, 0, PROTO_FALSE);
    pc_ssh_auth_handle_request(0, pkt, pn, out, &olen, sizeof(out));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);

    // ssh-rsa with no exponent.
    bl = put_string(b, "ssh-rsa");
    pn = build_pubkey_req(pkt, b, bl, NULL, 0, PROTO_FALSE);
    pc_ssh_auth_handle_request(0, pkt, pn, out, &olen, sizeof(out));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);

    // exponent wider than 4 bytes.
    bl = put_string(b, "ssh-rsa");
    wr_u32(b + bl, 5);
    bl += 4;
    memcpy(b + bl, "\x01\x02\x03\x04\x05", 5);
    bl += 5;
    pn = build_pubkey_req(pkt, b, bl, NULL, 0, PROTO_FALSE);
    pc_ssh_auth_handle_request(0, pkt, pn, out, &olen, sizeof(out));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);

    // valid exponent but no modulus.
    bl = put_string(b, "ssh-rsa");
    wr_u32(b + bl, 1);
    bl += 4;
    b[bl++] = 3;
    pn = build_pubkey_req(pkt, b, bl, NULL, 0, PROTO_FALSE);
    pc_ssh_auth_handle_request(0, pkt, pn, out, &olen, sizeof(out));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);

    // modulus wider than the key buffer.
    bl = put_string(b, "ssh-rsa");
    wr_u32(b + bl, 1);
    bl += 4;
    b[bl++] = 3;
    uint32_t nlen = 300; // > PC_RSA_KEY_BYTES (256)
    wr_u32(b + bl, nlen);
    bl += 4;
    memset(b + bl, 0x11, nlen);
    bl += nlen;
    pn = build_pubkey_req(pkt, b, bl, NULL, 0, PROTO_FALSE);
    pc_ssh_auth_handle_request(0, pkt, pn, out, &olen, sizeof(out));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
}

static proto_bool pk_cb_any(const char *u, const uint8_t *blob, size_t n)
{
    (void)u;
    (void)blob;
    (void)n;
    return PROTO_TRUE;
}

// A publickey request whose signed prefix exceeds SSH_PKT_BUF_SIZE is rejected before
// the signature buffer is assembled (a DoS guard), yielding USERAUTH_FAILURE.
void test_pubkey_oversized_signed_prefix()
{
    pc_ssh_auth_set_pubkey_cb(pk_cb_any); // accept any key so the length guard is reached
    set_session_id_0_to_31();
    static uint8_t blob[2200];
    size_t base = hexdec(PK_BLOB_HEX, blob);     // a valid ssh-rsa blob prefix
    memset(blob + base, 0, sizeof(blob) - base); // trailing padding (parse ignores it)
    static uint8_t pkt[4096], out[4096];
    uint8_t sig[8] = {0};
    size_t n = build_pubkey_req(pkt, blob, sizeof(blob), sig, sizeof(sig), PROTO_TRUE);
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
}

// Assemble a publickey USERAUTH_REQUEST carrying an arbitrary algorithm name, key blob and raw
// signature - enough to drive every blob / signature rejection path from the wire.
static size_t build_pubkey_req_generic(uint8_t *pkt, const char *algo, const uint8_t *blob, size_t blob_len,
                                       const uint8_t *sig, size_t sig_len, proto_bool with_sig)
{
    size_t n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(pkt + n, "alice");
    n += put_string(pkt + n, "ssh-connection");
    n += put_string(pkt + n, "publickey");
    pkt[n++] = with_sig ? 1 : 0;
    n += put_string(pkt + n, algo);
    wr_u32(pkt + n, (uint32_t)blob_len);
    n += 4;
    memcpy(pkt + n, blob, blob_len);
    n += blob_len;
    if (with_sig)
    {
        uint32_t al = (uint32_t)strlen(algo);
        wr_u32(pkt + n, 4 + al + 4 + (uint32_t)sig_len);
        n += 4;
        n += put_string(pkt + n, algo);
        wr_u32(pkt + n, (uint32_t)sig_len);
        n += 4;
        memcpy(pkt + n, sig, sig_len);
        n += sig_len;
    }
    return n;
}

// With no public-key verifier installed no key can be authorized, however well-formed it is.
void test_pubkey_without_verifier_fails()
{
    pc_ssh_auth_set_pubkey_cb(NULL);
    set_session_id_0_to_31();
    uint8_t blob[512];
    size_t bl = hexdec(PK_BLOB_HEX, blob);
    uint8_t pkt[1024], out[512];
    size_t olen = 0;
    size_t n = build_pubkey_req(pkt, blob, bl, NULL, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
}

// The ssh-rsa key type is matched element-wise (a name of a different LENGTH is rejected on the
// length test alone), and an mpint that is all zero bytes strips to the empty value and still
// parses - the decision then belongs to the application verifier, not the parser.
void test_pubkey_rsa_blob_type_length_and_zero_mpint()
{
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
    set_session_id_0_to_31();
    uint8_t b[400], pkt[1024], out[1024];
    size_t bl, n, olen = 0;

    bl = put_string(b, "ssh-rsa-x"); // 9 chars, not 7
    n = build_pubkey_req(pkt, b, bl, NULL, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);

    bl = put_string(b, "ssh-rsa");
    wr_u32(b + bl, 4); // exponent: four zero bytes
    bl += 4;
    memset(b + bl, 0, 4);
    bl += 4;
    wr_u32(b + bl, 4); // modulus: four zero bytes
    bl += 4;
    memset(b + bl, 0, 4);
    bl += 4;
    n = build_pubkey_req(pkt, b, bl, NULL, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_PK_OK, out[0]);
}

// An ssh-ed25519 blob whose public-key string is truncated, or is not exactly 32 bytes, is
// rejected; so is a signature of any length other than the 64 bytes Ed25519 produces.
void test_pubkey_ed25519_blob_and_siglen_rejections()
{
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
    set_session_id_0_to_31();
    uint8_t b[128], pkt[512], out[512];
    size_t bl, n, olen = 0;

    bl = put_string(b, "ssh-ed25519");
    wr_u32(b + bl, 32); // declares 32 bytes, none present
    bl += 4;
    n = build_pubkey_req_generic(pkt, "ssh-ed25519", b, bl, NULL, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);

    bl = put_string(b, "ssh-ed25519");
    wr_u32(b + bl, 31); // one byte short of an Ed25519 public key
    bl += 4;
    memset(b + bl, 0xAB, 31);
    bl += 31;
    n = build_pubkey_req_generic(pkt, "ssh-ed25519", b, bl, NULL, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);

    // Well-formed key, signature of the wrong length: refused before any verify work.
    bl = put_string(b, "ssh-ed25519");
    wr_u32(b + bl, 32);
    bl += 4;
    memset(b + bl, 0xAB, 32);
    bl += 32;
    uint8_t sig[63];
    memset(sig, 0x5C, sizeof(sig));
    n = build_pubkey_req_generic(pkt, "ssh-ed25519", b, bl, sig, sizeof(sig), PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
}

// An ecdsa-sha2-nistp256 blob is rejected at every field: a missing or wrong curve identifier, a
// missing or wrongly sized point, and a point that is not in uncompressed (0x04) form.
void test_pubkey_ecdsa_blob_rejections()
{
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
    set_session_id_0_to_31();
    uint8_t b[200], pkt[512], out[512];
    size_t bl, n, olen = 0;
    uint8_t q[PC_ECDSA_P256_PUB_LEN];
    memset(q, 0x11, sizeof(q));
    q[0] = 0x04;

    // curve identifier declared but absent.
    bl = put_string(b, "ecdsa-sha2-nistp256");
    wr_u32(b + bl, 8);
    bl += 4;
    n = build_pubkey_req_generic(pkt, "ecdsa-sha2-nistp256", b, bl, NULL, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);

    // curve identifiers of the wrong length, then the right length but the wrong name.
    const char *curves[2] = {"nistp25", "nistp257"};
    for (int c = 0; c < 2; c++)
    {
        bl = put_string(b, "ecdsa-sha2-nistp256");
        bl += put_string(b + bl, curves[c]);
        wr_u32(b + bl, PC_ECDSA_P256_PUB_LEN);
        bl += 4;
        memcpy(b + bl, q, PC_ECDSA_P256_PUB_LEN);
        bl += PC_ECDSA_P256_PUB_LEN;
        n = build_pubkey_req_generic(pkt, "ecdsa-sha2-nistp256", b, bl, NULL, 0, PROTO_FALSE);
        TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
        TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    }

    // point declared but absent.
    bl = put_string(b, "ecdsa-sha2-nistp256");
    bl += put_string(b + bl, "nistp256");
    wr_u32(b + bl, PC_ECDSA_P256_PUB_LEN);
    bl += 4;
    n = build_pubkey_req_generic(pkt, "ecdsa-sha2-nistp256", b, bl, NULL, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);

    // point of the wrong size, then a compressed point (leading byte != 0x04).
    for (int c = 0; c < 2; c++)
    {
        const uint32_t qlen = c == 0 ? PC_ECDSA_P256_PUB_LEN - 1u : PC_ECDSA_P256_PUB_LEN;
        bl = put_string(b, "ecdsa-sha2-nistp256");
        bl += put_string(b + bl, "nistp256");
        wr_u32(b + bl, qlen);
        bl += 4;
        memcpy(b + bl, q, qlen);
        if (c == 1)
        {
            b[bl] = 0x02; // compressed form is not accepted
        }
        bl += qlen;
        n = build_pubkey_req_generic(pkt, "ecdsa-sha2-nistp256", b, bl, NULL, 0, PROTO_FALSE);
        TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
        TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    }
}

// The ECDSA signature blob (mpint r || mpint s, RFC 5656 §3.1.2) is rejected when either half is
// missing or wider than a P-256 coordinate, and a well-formed signature over the wrong data fails
// verification rather than being accepted.
void test_pubkey_ecdsa_signature_rejections()
{
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
    set_session_id_0_to_31();

    uint8_t d[32];
    memset(d, 0, sizeof(d));
    d[31] = 0x0A;
    uint8_t q[PC_ECDSA_P256_PUB_LEN];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(q, d));
    uint8_t b[200];
    size_t bl = put_string(b, "ecdsa-sha2-nistp256");
    bl += put_string(b + bl, "nistp256");
    wr_u32(b + bl, PC_ECDSA_P256_PUB_LEN);
    bl += 4;
    memcpy(b + bl, q, PC_ECDSA_P256_PUB_LEN);
    bl += PC_ECDSA_P256_PUB_LEN;

    uint8_t wide[33];
    memset(wide, 0x9C, sizeof(wide)); // 33 significant bytes: wider than a P-256 coordinate
    uint8_t small[1] = {0x01};

    uint8_t sigblob[128];
    uint8_t pkt[512], out[512];
    size_t olen = 0;
    struct
    {
        const uint8_t *r;
        size_t rlen;
        const uint8_t *s;
        size_t slen;
        proto_bool with_s;
    } cases[5] = {
        {NULL, 0, NULL, 0, PROTO_FALSE},            // no r at all
        {small, 1, NULL, 0, PROTO_FALSE},           // r only, s missing
        {wide, sizeof(wide), small, 1, PROTO_TRUE}, // r too wide
        {small, 1, wide, sizeof(wide), PROTO_TRUE}, // s too wide
        {small, 1, small, 1, PROTO_TRUE},           // parses, but signs nothing
    };
    for (int c = 0; c < 5; c++)
    {
        size_t sl = 0;
        if (cases[c].r)
        {
            wr_u32(sigblob + sl, (uint32_t)cases[c].rlen);
            sl += 4;
            memcpy(sigblob + sl, cases[c].r, cases[c].rlen);
            sl += cases[c].rlen;
        }
        if (cases[c].with_s)
        {
            wr_u32(sigblob + sl, (uint32_t)cases[c].slen);
            sl += 4;
            memcpy(sigblob + sl, cases[c].s, cases[c].slen);
            sl += cases[c].slen;
        }
        size_t n = build_pubkey_req_generic(pkt, "ecdsa-sha2-nistp256", b, bl, sigblob, sl, PROTO_TRUE);
        TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
        TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
        TEST_ASSERT_FALSE(ssh_sess[0].authed);
    }
}

static proto_bool pk_cb_reject(const char *u, const uint8_t *blob, size_t n)
{
    (void)u;
    (void)blob;
    (void)n;
    return PROTO_FALSE;
}

// A verifier that IS installed but refuses the offered key produces USERAUTH_FAILURE - a distinct
// path from having no verifier at all, and it must not fall through to a signature check.
void test_pubkey_verifier_rejects_key()
{
    pc_ssh_auth_set_pubkey_cb(pk_cb_reject);
    set_session_id_0_to_31();
    uint8_t blob[512];
    size_t bl = hexdec(PK_BLOB_HEX, blob);
    uint8_t pkt[1024], out[512];
    size_t olen = 0;
    size_t n = build_pubkey_req(pkt, blob, bl, NULL, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
    pc_ssh_auth_set_pubkey_cb(pk_cb_alice);
}

// USERAUTH_FAILURE carries the partial-success boolean the caller asks for (RFC 4252 §5.1).
void test_build_failure_partial_success_flag()
{
    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_build_failure(out, &olen, sizeof(out), PROTO_TRUE));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_EQUAL_UINT8(1, out[olen - 1]);
    TEST_ASSERT_EQUAL_INT(0, pc_ssh_auth_build_failure(out, &olen, sizeof(out), PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT8(0, out[olen - 1]);
}

// handle_request rejects an out-of-range slot and a payload that does not parse.
void test_handle_request_index_and_parse_guards()
{
    uint8_t out[64], p[8];
    size_t olen = 0;
    p[0] = 99;
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_request(MAX_SSH_CONNS, p, 1, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, pc_ssh_auth_handle_request(0, p, 1, out, &olen, sizeof(out))); // parse fails
}

// ---- aes256-gcm@openssh.com cipher (keyed by ssh_dh_derive_keys_sid() for this same auth/transport
// flow when AES-GCM is negotiated) ------------------------------------------

// Inside gctr(), the 32-bit GCTR block counter (the low 4 bytes of the 16-byte CTR block - distinct
// from the RFC 5647 8-byte invocation counter in the nonce) always starts at 2 for every seal()/open()
// call and is not caller-controlled, so the only way to force a byte-level carry out of ctr[15] (0xff
// -> 0x00, propagating into ctr[14]) is to actually drive enough 16-byte GCTR blocks through one call:
// byte 15 wraps once 254 blocks (4064 B) have been processed. pc_aesgcm_seal()/open() have no
// buffer-size limit of their own (only the SSH packet layer's SSH_PKT_BUF_SIZE does, and that is a
// packet-layer policy this crypto primitive is not bound by), so a single 4096 B seal()+open() round
// trip reaches the byte-carry arm without anywhere near the ~2^32-block full-counter wrap.
void test_aesgcm_gctr_counter_byte_carry(void)
{
    uint8_t key[32], iv[12];
    for (int i = 0; i < 32; i++)
    {
        key[i] = (uint8_t)(i * 3 + 5);
    }
    for (int i = 0; i < 12; i++)
    {
        iv[i] = (uint8_t)(0x20 + i);
    }

    static uint8_t pt[4096], ct[4096 + PC_AESGCM_TAG_LEN], rt[4096];
    for (size_t i = 0; i < sizeof(pt); i++)
    {
        pt[i] = (uint8_t)(i * 31 + 7);
    }
    const uint8_t aad[4] = {0, 0, 0x10, 0x00};

    pc_aesgcm_seal(gcm_key(key), iv, aad, sizeof(aad), pt, sizeof(pt), ct, ct + sizeof(pt));
    TEST_ASSERT_TRUE(pc_aesgcm_open(gcm_key(key), iv, aad, sizeof(aad), ct, sizeof(pt), ct + sizeof(pt), rt));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, rt, sizeof(pt));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_service_request_errors);
    RUN_TEST(test_build_response_guards);
    RUN_TEST(test_parse_request_truncations);
    RUN_TEST(test_pubkey_blob_parse_failures);
    RUN_TEST(test_pubkey_oversized_signed_prefix);
    RUN_TEST(test_handle_request_index_and_parse_guards);
    RUN_TEST(test_pubkey_without_verifier_fails);
    RUN_TEST(test_pubkey_rsa_blob_type_length_and_zero_mpint);
    RUN_TEST(test_pubkey_ed25519_blob_and_siglen_rejections);
    RUN_TEST(test_pubkey_ecdsa_blob_rejections);
    RUN_TEST(test_pubkey_ecdsa_signature_rejections);
    RUN_TEST(test_pubkey_verifier_rejects_key);
    RUN_TEST(test_build_failure_partial_success_flag);
    RUN_TEST(test_service_request_accept);
    RUN_TEST(test_service_request_rejects_unknown);
    RUN_TEST(test_parse_password_request);
    RUN_TEST(test_parse_none_request);
    RUN_TEST(test_handle_request_success);
    RUN_TEST(test_handle_request_wrong_password_fails);
    RUN_TEST(test_handle_none_request_fails_without_auth);
    RUN_TEST(test_handle_request_no_callback_fails);
    RUN_TEST(test_pubkey_probe_returns_pk_ok);
    RUN_TEST(test_pubkey_valid_signature_succeeds);
    RUN_TEST(test_pubkey_rsa_sha512_signature_succeeds);
    RUN_TEST(test_pubkey_ecdsa_signature_succeeds);
    RUN_TEST(test_pubkey_ed25519_valid_signature_succeeds);
    RUN_TEST(test_pubkey_tampered_signature_fails);
    RUN_TEST(test_pubkey_unauthorized_key_fails);
    RUN_TEST(test_aesgcm_gctr_counter_byte_carry);
    return UNITY_END();
}
