// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Built with PC_SSH_ALLOW_PASSWORD=0: verifies password authentication is
// refused and not advertised, while publickey auth remains available.

#include "crypto/asymmetric/ecdsa.h"
#include "network_drivers/presentation/ssh/auth/ssh_auth.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include "protocore_config.h"
#include <stdint.h>
#include <string.h>

#include <unity.h>

#if PC_SSH_ALLOW_PASSWORD != 0
#error "test_ssh_hardening must be built with PC_SSH_ALLOW_PASSWORD=0"
#endif

void setUp()
{
    SshTransport.init(0);
    SshAuth.set_password_cb(NULL);
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

static proto_bool always_ok(const char *u, const char *p)
{
    (void)u;
    (void)p;
    return PROTO_TRUE;
}

// Same length-prefix wire format as put_string(), but for a raw byte buffer (not a NUL-terminated
// C string) - needed to place a public-key blob / signature into a hand-built USERAUTH_REQUEST.
static size_t put_bytes_string(uint8_t *p, const uint8_t *data, uint32_t n)
{
    p[0] = (uint8_t)(n >> 24);
    p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8);
    p[3] = (uint8_t)n;
    memcpy(p + 4, data, n);
    return 4 + n;
}

static proto_bool accept_any_pubkey(const char *u, const uint8_t *blob, size_t blob_len)
{
    (void)u;
    (void)blob;
    (void)blob_len;
    return PROTO_TRUE;
}

void test_password_refused_even_with_correct_callback()
{
    // Even a callback that accepts everything must not authenticate, because
    // the password method is compiled out.
    SshAuth.set_password_cb(always_ok);
    uint8_t pkt[128];
    size_t n = 0;
    pkt[n++] = SSH_MSG_USERAUTH_REQUEST;
    n += put_string(pkt + n, "alice");
    n += put_string(pkt + n, "ssh-connection");
    n += put_string(pkt + n, "password");
    pkt[n++] = 0;
    n += put_string(pkt + n, "whatever");

    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, SshAuth.handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_FAILURE, out[0]);
    TEST_ASSERT_FALSE(ssh_sess[0].authed);
}

void test_failure_advertises_publickey_only()
{
    uint8_t out[64];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, SshAuth.build_failure(out, &olen, sizeof(out), PROTO_FALSE));
    // name-list at out[1..]: must contain "publickey" and not "password".
    proto_bool has_pk = PROTO_FALSE, has_pw = PROTO_FALSE;
    for (size_t k = 0; k + 9 <= olen; k++)
    {
        if (memcmp(out + k, "publickey", 9) == 0)
        {
            has_pk = PROTO_TRUE;
        }
    }
    for (size_t k = 0; k + 8 <= olen; k++)
    {
        if (memcmp(out + k, "password", 8) == 0)
        {
            has_pw = PROTO_TRUE;
        }
    }
    TEST_ASSERT_TRUE(has_pk);
    TEST_ASSERT_FALSE(has_pw);
}

// ---------------------------------------------------------------------------
// ECDSA P-256 (pc_ecdsa.cpp) - this env links pc_ecdsa.cpp as part of the
// SSH auth/transport stack under test (pc_ssh_auth_handle_pubkey verifies
// "ecdsa-sha2-nistp256" client keys), so its coverage is measured here too.
// Full RFC 6979 known-answer-vector coverage lives in native_ecdsa /
// test_ecdsa.cpp; these two tests just need to actually run the native
// software P-256 path (sign, verify, ecdh) at least once under this build.
// ---------------------------------------------------------------------------

// Direct-API round trip: pubkey -> sign -> verify, plus ecdh between two keys. Exercises the
// software field/scalar arithmetic (fp_mul -> reduce_mod -> reduce_low8_ge), the RFC 6979
// deterministic nonce path (ecdsa_try_sign / ecdsa_sign_core), and pc_ecdsa_p256_ecdh's
// on-curve + non-identity checks - none of which any other test in this env's binary reaches.
void test_ecdsa_direct_sign_verify_ecdh_roundtrip(void)
{
    uint8_t priv_a[32];
    memset(priv_a, 0, 32);
    priv_a[31] = 0x2A;
    uint8_t pub_a[65];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(pub_a, priv_a));

    const uint8_t msg[] = "ecdsa still works with password auth compiled out";
    uint8_t sig[64];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_sign(sig, msg, sizeof(msg) - 1, priv_a));
    TEST_ASSERT_TRUE(pc_ecdsa_p256_verify(pub_a, msg, sizeof(msg) - 1, sig));
    sig[0] ^= 0x01;
    TEST_ASSERT_FALSE(pc_ecdsa_p256_verify(pub_a, msg, sizeof(msg) - 1, sig));

    uint8_t priv_b[32];
    memset(priv_b, 0, 32);
    priv_b[31] = 0x2B;
    uint8_t pub_b[65];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(pub_b, priv_b));

    uint8_t shared_ab[32];
    uint8_t shared_ba[32];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_ecdh(shared_ab, pub_b, priv_a));
    TEST_ASSERT_TRUE(pc_ecdsa_p256_ecdh(shared_ba, pub_a, priv_b));
    TEST_ASSERT_EQUAL_MEMORY(shared_ab, shared_ba, 32);
}

// The file's own charter ("publickey auth remains available") had no test actually driving an
// ecdsa-sha2-nistp256 publickey authentication to a real USERAUTH_SUCCESS. Build the wire request
// by hand (RFC 4252 §7 / RFC 5656 §3.1) and sign it with a real key, the way a genuine client would.
void test_ecdsa_publickey_auth_succeeds_when_password_disabled(void)
{
    SshAuth.set_pubkey_cb(accept_any_pubkey);

    // A real client only has a signature to make once the first KEX has produced a session id.
    ssh_sess[0].session_id_len = 32;
    for (int k = 0; k < 32; k++)
    {
        ssh_sess[0].session_id[k] = (uint8_t)(k * 7 + 1);
    }

    uint8_t priv[32];
    memset(priv, 0, 32);
    priv[31] = 0x77;
    uint8_t pub[PC_ECDSA_P256_PUB_LEN];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_pubkey(pub, priv));

    // Public-key blob (RFC 5656 §3.1): string("ecdsa-sha2-nistp256") string("nistp256") string(Q).
    uint8_t blob[4 + 19 + 4 + 8 + 4 + PC_ECDSA_P256_PUB_LEN];
    size_t bo = 0;
    bo += put_string(blob + bo, "ecdsa-sha2-nistp256");
    bo += put_string(blob + bo, "nistp256");
    bo += put_bytes_string(blob + bo, pub, PC_ECDSA_P256_PUB_LEN);

    // Everything the signature covers except the session id: byte(REQUEST) || string(user) ||
    // string(service) || string(method) || boolean(TRUE) || string(algo) || string(blob).
    uint8_t prefix[4 + 32 + 4 + 14 + 4 + 9 + 1 + 4 + 19 + sizeof(blob)];
    size_t po = 0;
    prefix[po++] = SSH_MSG_USERAUTH_REQUEST;
    po += put_string(prefix + po, "alice");
    po += put_string(prefix + po, "ssh-connection");
    po += put_string(prefix + po, "publickey");
    prefix[po++] = 1; // has_signature = TRUE
    po += put_string(prefix + po, "ecdsa-sha2-nistp256");
    po += put_bytes_string(prefix + po, blob, (uint32_t)bo);

    // Signed data (RFC 4252 §7): string(session_id) || prefix.
    uint8_t signed_data[4 + 32 + sizeof(prefix)];
    size_t sd = 0;
    sd += put_bytes_string(signed_data + sd, ssh_sess[0].session_id, ssh_sess[0].session_id_len);
    memcpy(signed_data + sd, prefix, po);
    sd += po;

    uint8_t sig[PC_ECDSA_P256_SIG_LEN];
    TEST_ASSERT_TRUE(pc_ecdsa_p256_sign(sig, signed_data, sd, priv));

    // Signature field: string(sigblob), sigblob = string(sig-algo) || string(mpint(r)||mpint(s)).
    uint8_t rawsig[4 + 32 + 4 + 32];
    size_t rs = 0;
    rs += put_bytes_string(rawsig + rs, sig, 32);
    rs += put_bytes_string(rawsig + rs, sig + 32, 32);
    uint8_t sigblob[4 + 19 + 4 + sizeof(rawsig)];
    size_t sbo = 0;
    sbo += put_string(sigblob + sbo, "ecdsa-sha2-nistp256");
    sbo += put_bytes_string(sigblob + sbo, rawsig, (uint32_t)rs);

    uint8_t pkt[sizeof(prefix) + 4 + sizeof(sigblob)];
    memcpy(pkt, prefix, po);
    size_t n = po;
    n += put_bytes_string(pkt + n, sigblob, (uint32_t)sbo);

    uint8_t out[256];
    size_t olen = 0;
    TEST_ASSERT_EQUAL_INT(0, SshAuth.handle_request(0, pkt, n, out, &olen, sizeof(out)));
    TEST_ASSERT_EQUAL(SSH_MSG_USERAUTH_SUCCESS, out[0]);
    TEST_ASSERT_TRUE(ssh_sess[0].authed);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_password_refused_even_with_correct_callback);
    RUN_TEST(test_failure_advertises_publickey_only);
    RUN_TEST(test_ecdsa_direct_sign_verify_ecdh_roundtrip);
    RUN_TEST(test_ecdsa_publickey_auth_succeeds_when_password_disabled);
    return UNITY_END();
}
