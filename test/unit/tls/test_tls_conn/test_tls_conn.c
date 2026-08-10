// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TLS 1.3 handshake driver over the stream record layer (network_drivers/tls/tls_conn, RFC 8446
// sec 4). The driver is the server half only: conn_start refuses and conn_process rejects
// TLS_ROLE_CLIENT, so the peer here is a hand-assembled ClientHello rather than another TlsConn.
//
// The message bytes are pinned against RFC 8448 in test_tls13_msg and the key schedule in
// test_tls13_kdf. What this adds is the driver: that init takes its one secure-pool borrow and
// wires the regions, that a ClientHello inside the one supported profile draws a server flight and
// leaves the connection awaiting the client Finished, and that a ClientHello outside the profile
// fails with an alert instead of negotiating something else.

#include "baseline_keys.h"
#include "crypto/asymmetric/ed25519.h"
#include "mmgr/secure.h"
#include "network_drivers/tls/tls_conn.h"
#include <string.h>

#include <unity.h>

static uint8_t tw[8192]; // test-side working bytes for the crypto entry points

#define SERVER_SEED BASELINE_ED25519_SEEDS[0]
static const uint8_t SERVER_EPH[32] = {0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                                       0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                                       0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22};
static const uint8_t SERVER_RANDOM[32] = {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
                                          0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
                                          0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33};
static const uint8_t CLIENT_SHARE[32] = {0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
                                         0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
                                         0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44};

static uint8_t g_server_pub[32];

void setUp()
{
    pc_secure_reset();
    pc_ed25519_pubkey(tw, g_server_pub, SERVER_SEED);
}
void tearDown()
{
}

static TlsConnConfig server_cfg(void)
{
    TlsConnConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.ed25519_seed = SERVER_SEED;
    cfg.ed25519_pub = g_server_pub;
    cfg.ephemeral_priv = SERVER_EPH;
    cfg.random = SERVER_RANDOM;
    return cfg;
}

// Which extension to leave out, so each arm of the profile check gets its own ClientHello.
typedef enum
{
    CH_COMPLETE = 0,
    CH_NO_TLS13,     // supported_versions offers only TLS 1.2
    CH_NO_X25519,    // supported_groups offers only secp256r1
    CH_NO_KEY_SHARE, // supported_groups is right but no share is sent
    CH_NO_ED25519,   // signature_algorithms offers only rsa_pss_rsae_sha256
    CH_NO_SUITE      // cipher_suites offers only TLS_AES_256_GCM_SHA384
} ChFlaw;

static void put16(uint8_t *b, size_t *p, uint16_t v)
{
    b[(*p)++] = (uint8_t)(v >> 8);
    b[(*p)++] = (uint8_t)v;
}

// One TLS 1.3 ClientHello wrapped in a plaintext handshake record, carrying the four extensions the
// server's profile reads. @p flaw removes exactly one requirement.
static size_t build_client_hello(uint8_t *rec, size_t cap, ChFlaw flaw)
{
    uint8_t body[512];
    size_t p = 0;
    put16(body, &p, 0x0303); // legacy_version
    for (size_t i = 0; i < 32; i++)
    {
        body[p++] = (uint8_t)(0x60 + i); // random
    }
    body[p++] = 0; // legacy_session_id: empty

    put16(body, &p, 2); // cipher_suites
    if (flaw == CH_NO_SUITE)
    {
        put16(body, &p, 0x1302); // TLS_AES_256_GCM_SHA384, which this stack does not implement
    }
    else
    {
        put16(body, &p, 0x1301); // TLS_AES_128_GCM_SHA256
    }

    body[p++] = 1; // legacy_compression_methods: exactly one null byte
    body[p++] = 0;

    const size_t ext_total_at = p;
    p += 2;
    const size_t ext_start = p;

    // supported_versions
    put16(body, &p, 0x002b);
    put16(body, &p, 3);
    body[p++] = 2;
    if (flaw == CH_NO_TLS13)
    {
        put16(body, &p, 0x0303);
    }
    else
    {
        put16(body, &p, 0x0304);
    }

    // supported_groups
    put16(body, &p, 0x000a);
    put16(body, &p, 4);
    put16(body, &p, 2);
    if (flaw == CH_NO_X25519)
    {
        put16(body, &p, 0x0017); // secp256r1
    }
    else
    {
        put16(body, &p, 0x001d); // x25519
    }

    // key_share
    if (flaw != CH_NO_KEY_SHARE)
    {
        put16(body, &p, 0x0033);
        put16(body, &p, (uint16_t)(2 + 4 + 32));
        put16(body, &p, (uint16_t)(4 + 32));
        put16(body, &p, 0x001d);
        put16(body, &p, 32);
        memcpy(body + p, CLIENT_SHARE, 32);
        p += 32;
    }

    // signature_algorithms
    put16(body, &p, 0x000d);
    put16(body, &p, 4);
    put16(body, &p, 2);
    if (flaw == CH_NO_ED25519)
    {
        put16(body, &p, 0x0804); // rsa_pss_rsae_sha256
    }
    else
    {
        put16(body, &p, 0x0807); // ed25519
    }

    const uint16_t ext_len = (uint16_t)(p - ext_start);
    body[ext_total_at] = (uint8_t)(ext_len >> 8);
    body[ext_total_at + 1] = (uint8_t)ext_len;

    // Handshake header, then the plaintext record around it.
    uint8_t msg[600];
    msg[0] = 0x01; // client_hello
    msg[1] = (uint8_t)(p >> 16);
    msg[2] = (uint8_t)(p >> 8);
    msg[3] = (uint8_t)p;
    memcpy(msg + 4, body, p);
    return TlsRecord.plaintext_build(PC_TLS_CT_HANDSHAKE, msg, p + 4, rec, cap);
}

// init takes the one borrow and wires every region of it (tls_conn.c TLS_OFF_*). Nothing is
// declared in TlsConn itself, so a null region means the split never happened.
void test_init_takes_its_borrow()
{
    TlsConnConfig cfg = server_cfg();
    static TlsConn c; // a pool slot: zeroed before first use, like every real connection
    TEST_ASSERT_TRUE(TlsConnection.init(&c, TLS_ROLE_SERVER, &cfg));
    TEST_ASSERT_NOT_NULL(c.tx);
    TEST_ASSERT_NOT_NULL(c.rx);
    TEST_ASSERT_NOT_NULL(c.terms);
    TEST_ASSERT_NOT_NULL(c.hash_work);
    TEST_ASSERT_NOT_NULL(c.sign_work);
    TEST_ASSERT_NOT_NULL(c.hello);
    TEST_ASSERT_EQUAL_INT(TLS_CONN_START, c.state);
    TEST_ASSERT_EQUAL_UINT8(0, TlsConnection.alert(&c));
    TEST_ASSERT_FALSE(TlsConnection.established(&c));
}

// A ClientHello inside the profile draws the server's flight and leaves it awaiting the client
// Finished, with the handshake keys installed.
void test_client_hello_draws_the_server_flight()
{
    TlsConnConfig cfg = server_cfg();
    static TlsConn c; // a pool slot: zeroed before first use, like every real connection
    TEST_ASSERT_TRUE(TlsConnection.init(&c, TLS_ROLE_SERVER, &cfg));

    static uint8_t rec[600];
    size_t rec_len = build_client_hello(rec, sizeof rec, CH_COMPLETE);
    TEST_ASSERT_TRUE(rec_len > 0);
    TEST_ASSERT_TRUE(rec_len <= PC_TLS_CONN_REC_CAP);

    static uint8_t out[4096];
    memcpy(c.rx, rec, rec_len);
    int n = TlsConnection.process(&c, rec_len, out, sizeof out);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT8(0, TlsConnection.alert(&c));
    TEST_ASSERT_EQUAL_INT(TLS_CONN_WAIT_FINISHED, c.state);
    TEST_ASSERT_TRUE(c.hs_keys_ready);
    TEST_ASSERT_FALSE(TlsConnection.established(&c));

    // The flight opens with a plaintext ServerHello; everything after it is protected, so the
    // record after the first carries application_data on the wire (RFC 8446 sec 5.2).
    TEST_ASSERT_EQUAL_HEX8(PC_TLS_CT_HANDSHAKE, out[0]);
    const size_t sh_len = 5 + (((size_t)out[3] << 8) | out[4]);
    TEST_ASSERT_TRUE(sh_len < (size_t)n);
    TEST_ASSERT_EQUAL_HEX8(PC_TLS_CT_APPLICATION_DATA, out[sh_len]);
}

// RFC 8446 sec 4.1.3 / sec 9.1: one profile - TLS 1.3, X25519 with the share up front, Ed25519,
// TLS_AES_128_GCM_SHA256. Each missing requirement is a handshake failure, not a quiet fallback.
void test_client_hello_outside_the_profile_is_refused()
{
    static const ChFlaw flaws[] = {CH_NO_TLS13, CH_NO_X25519, CH_NO_KEY_SHARE, CH_NO_ED25519, CH_NO_SUITE};
    for (size_t i = 0; i < sizeof flaws / sizeof flaws[0]; i++)
    {
        pc_secure_reset();
        TlsConnConfig cfg = server_cfg();
        static TlsConn c; // a pool slot: zeroed before first use, like every real connection
        TEST_ASSERT_TRUE(TlsConnection.init(&c, TLS_ROLE_SERVER, &cfg));

        static uint8_t rec[600];
        size_t rec_len = build_client_hello(rec, sizeof rec, flaws[i]);
        TEST_ASSERT_TRUE(rec_len > 0);

        static uint8_t out[4096];
        memcpy(c.rx, rec, rec_len);
        (void)TlsConnection.process(&c, rec_len, out, sizeof out);
        TEST_ASSERT_FALSE(TlsConnection.established(&c));
        TEST_ASSERT_EQUAL_INT(TLS_CONN_FAILED, c.state);
        TEST_ASSERT_NOT_EQUAL(0, TlsConnection.alert(&c));
    }
}

// A record that is not a well-formed TLSPlaintext ends the connection on decode_error rather than
// being read past.
void test_malformed_record_is_a_decode_error()
{
    TlsConnConfig cfg = server_cfg();
    static TlsConn c; // a pool slot: zeroed before first use, like every real connection
    TEST_ASSERT_TRUE(TlsConnection.init(&c, TLS_ROLE_SERVER, &cfg));

    static uint8_t out[256];
    const uint8_t truncated[4] = {PC_TLS_CT_HANDSHAKE, 0x03, 0x03, 0x00}; // short of the 5-byte header
    memcpy(c.rx, truncated, sizeof truncated);
    TEST_ASSERT_TRUE(TlsConnection.process(&c, sizeof truncated, out, sizeof out) < 0);
    TEST_ASSERT_EQUAL_INT(TLS_CONN_FAILED, c.state);
    TEST_ASSERT_NOT_EQUAL(0, TlsConnection.alert(&c));
}

// The client half is declared in tls_conn.h - TLS_ROLE_CLIENT, the WAIT_SH / WAIT_FLIGHT states,
// TlsConnConfig::peer_pub and ::hostname - but conn_start refuses and conn_process turns the role
// away. This pins the surface as it stands so the gap is visible rather than assumed absent.
void test_client_role_is_not_implemented()
{
    TlsConnConfig cfg = server_cfg();
    static TlsConn c; // a pool slot: zeroed before first use, like every real connection
    TEST_ASSERT_TRUE(TlsConnection.init(&c, TLS_ROLE_CLIENT, &cfg));

    static uint8_t out[256];
    TEST_ASSERT_EQUAL_UINT(0, TlsConnection.start(&c, out, sizeof out));
    TEST_ASSERT_EQUAL_INT(TLS_CONN_FAILED, c.state);
    TEST_ASSERT_NOT_EQUAL(0, TlsConnection.alert(&c));

    // process turns the role away on its own guard, not on the failed state start left behind.
    TEST_ASSERT_TRUE(TlsConnection.init(&c, TLS_ROLE_CLIENT, &cfg));
    TEST_ASSERT_TRUE(TlsConnection.process(&c, 0, out, sizeof out) < 0);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_init_takes_its_borrow);
    RUN_TEST(test_client_hello_draws_the_server_flight);
    RUN_TEST(test_client_hello_outside_the_profile_is_refused);
    RUN_TEST(test_malformed_record_is_a_decode_error);
    RUN_TEST(test_client_role_is_not_implemented);
    return UNITY_END();
}
