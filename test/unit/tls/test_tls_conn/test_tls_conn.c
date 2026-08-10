// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TLS 1.3 handshake driver over the stream record layer (network_drivers/tls/tls_conn, RFC 8446
// sec 4). The module drives both ends, so a client and a server are stood up against each other and
// run the whole handshake; what is checked is the state machine, not the wire format, which is
// pinned against RFC 8448 in test_tls13_msg and test_tls13_kdf:
//   - the flight sequence and the state each end lands in,
//   - that both ends install the same application-traffic keys, by sealing on one and opening
//     on the other in both directions,
//   - the one-profile refusals and the alert each ends on.

#include "crypto/asymmetric/curve25519.h"
#include "crypto/asymmetric/ed25519.h"
#include "mmgr/secure.h"
#include "network_drivers/tls/tls_conn.h"
#include <string.h>

#include <unity.h>

static uint8_t tw[8192]; // test-side working bytes for the crypto entry points

// Fixed inputs: a known-answer test is meaningless if its inputs move. Freshness per handshake is
// the caller's contract (TlsConnConfig), not this module's, so the values are pinned here.
static const uint8_t SERVER_SEED[32] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                                        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                                        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11};
static const uint8_t SERVER_EPH[32] = {0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                                       0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
                                       0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22};
static const uint8_t SERVER_RANDOM[32] = {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
                                          0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
                                          0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33};
static const uint8_t CLIENT_EPH[32] = {0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
                                       0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
                                       0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44};
static const uint8_t CLIENT_RANDOM[32] = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                                          0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                                          0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};

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

static TlsConnConfig client_cfg(void)
{
    TlsConnConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.peer_pub = g_server_pub; // raw public key authentication (RFC 7250)
    cfg.ephemeral_priv = CLIENT_EPH;
    cfg.random = CLIENT_RANDOM;
    cfg.hostname = "example.org";
    return cfg;
}

// Feed one record into @p dst and hand back whatever it owes. The record goes through dst->rx,
// which is where the worker puts it.
static int feed(TlsConn *dst, const uint8_t *rec, size_t rec_len, uint8_t *out, size_t out_cap)
{
    memcpy(dst->rx, rec, rec_len);
    return TlsConnection.process(dst, rec_len, out, out_cap);
}

// Walk a byte stream one TLS record at a time: a flight can carry several, and process() consumes
// exactly one per call. Returns the total the far end wrote back.
static size_t feed_all(TlsConn *dst, const uint8_t *stream, size_t len, uint8_t *out, size_t out_cap)
{
    size_t off = 0;
    size_t wrote = 0;
    while (off + 5 <= len)
    {
        const size_t body = ((size_t)stream[off + 3] << 8) | stream[off + 4];
        const size_t rec_len = 5 + body;
        TEST_ASSERT_TRUE(off + rec_len <= len);
        int n = feed(dst, stream + off, rec_len, out + wrote, out_cap - wrote);
        TEST_ASSERT_TRUE(n >= 0);
        wrote += (size_t)n;
        off += rec_len;
    }
    TEST_ASSERT_EQUAL_UINT(len, off); // every byte belonged to a record
    return wrote;
}

// RFC 8446 sec 4: ClientHello -> server flight -> client Finished. Both ends end up established
// with the same application-traffic keys.
void test_full_handshake()
{
    TlsConnConfig scfg = server_cfg();
    TlsConnConfig ccfg = client_cfg();
    TlsConn server;
    TlsConn client;
    TEST_ASSERT_TRUE(TlsConnection.init(&server, TLS_ROLE_SERVER, &scfg));
    TEST_ASSERT_TRUE(TlsConnection.init(&client, TLS_ROLE_CLIENT, &ccfg));

    static uint8_t ch[2048];
    size_t ch_len = TlsConnection.start(&client, ch, sizeof ch);
    TEST_ASSERT_TRUE(ch_len > 0);
    TEST_ASSERT_FALSE(TlsConnection.established(&client));

    // The server answers the ClientHello with its whole flight.
    static uint8_t flight[4096];
    size_t flight_len = feed_all(&server, ch, ch_len, flight, sizeof flight);
    TEST_ASSERT_TRUE(flight_len > 0);
    TEST_ASSERT_EQUAL_UINT8(0, TlsConnection.alert(&server));

    // The client consumes it and owes its Finished.
    static uint8_t cfin[1024];
    size_t cfin_len = feed_all(&client, flight, flight_len, cfin, sizeof cfin);
    TEST_ASSERT_EQUAL_UINT8(0, TlsConnection.alert(&client));
    TEST_ASSERT_TRUE(TlsConnection.established(&client));
    TEST_ASSERT_TRUE(cfin_len > 0);

    // The server verifies it and is established too.
    static uint8_t nothing[256];
    size_t back = feed_all(&server, cfin, cfin_len, nothing, sizeof nothing);
    TEST_ASSERT_EQUAL_UINT8(0, TlsConnection.alert(&server));
    TEST_ASSERT_TRUE(TlsConnection.established(&server));
    TEST_ASSERT_EQUAL_UINT(0, back); // the handshake is over; the server owes nothing

    // The keys agree in both directions: what one seals, the other opens.
    static uint8_t rec[512];
    static uint8_t got[512];
    size_t got_len = 0;
    const uint8_t s2c[] = "server to client";
    size_t n = TlsConnection.seal_app(&server, s2c, sizeof s2c - 1, rec, sizeof rec);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(TlsConnection.open_app(&client, rec, n, got, sizeof got, &got_len));
    TEST_ASSERT_EQUAL_UINT(sizeof s2c - 1, got_len);
    TEST_ASSERT_EQUAL_MEMORY(s2c, got, got_len);

    const uint8_t c2s[] = "client to server";
    n = TlsConnection.seal_app(&client, c2s, sizeof c2s - 1, rec, sizeof rec);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(TlsConnection.open_app(&server, rec, n, got, sizeof got, &got_len));
    TEST_ASSERT_EQUAL_UINT(sizeof c2s - 1, got_len);
    TEST_ASSERT_EQUAL_MEMORY(c2s, got, got_len);
}

// A record that fails to open must not be handed to the application, and must not desynchronize
// the connection's record counter into accepting the next forgery.
void test_forged_application_record_is_refused()
{
    TlsConnConfig scfg = server_cfg();
    TlsConnConfig ccfg = client_cfg();
    TlsConn server;
    TlsConn client;
    TEST_ASSERT_TRUE(TlsConnection.init(&server, TLS_ROLE_SERVER, &scfg));
    TEST_ASSERT_TRUE(TlsConnection.init(&client, TLS_ROLE_CLIENT, &ccfg));

    static uint8_t ch[2048];
    static uint8_t flight[4096];
    static uint8_t cfin[1024];
    static uint8_t sink[256];
    size_t ch_len = TlsConnection.start(&client, ch, sizeof ch);
    size_t flight_len = feed_all(&server, ch, ch_len, flight, sizeof flight);
    size_t cfin_len = feed_all(&client, flight, flight_len, cfin, sizeof cfin);
    feed_all(&server, cfin, cfin_len, sink, sizeof sink);
    TEST_ASSERT_TRUE(TlsConnection.established(&server));

    static uint8_t rec[512];
    static uint8_t got[512];
    size_t got_len = 0;
    const uint8_t msg[] = "payload";
    size_t n = TlsConnection.seal_app(&server, msg, sizeof msg - 1, rec, sizeof rec);
    TEST_ASSERT_TRUE(n > 0);

    rec[n - 1] ^= 0x01; // flip a tag bit
    TEST_ASSERT_FALSE(TlsConnection.open_app(&client, rec, n, got, sizeof got, &got_len));

    rec[n - 1] ^= 0x01; // the genuine record still opens, at the same sequence number
    TEST_ASSERT_TRUE(TlsConnection.open_app(&client, rec, n, got, sizeof got, &got_len));
    TEST_ASSERT_EQUAL_UINT(sizeof msg - 1, got_len);
    TEST_ASSERT_EQUAL_MEMORY(msg, got, got_len);
}

// The server runs one profile (RFC 8446 sec 4.1.3 / sec 9.1): TLS 1.3, X25519 with the share up
// front, Ed25519, and TLS_AES_128_GCM_SHA256. A ClientHello missing any of them is a handshake
// failure rather than a silently different negotiation.
static void refuse_mutated_hello(size_t patch_off, uint8_t patch_val, const char *what)
{
    TlsConnConfig scfg = server_cfg();
    TlsConnConfig ccfg = client_cfg();
    TlsConn server;
    TlsConn client;
    TEST_ASSERT_TRUE(TlsConnection.init(&server, TLS_ROLE_SERVER, &scfg));
    TEST_ASSERT_TRUE(TlsConnection.init(&client, TLS_ROLE_CLIENT, &ccfg));

    static uint8_t ch[2048];
    static uint8_t out[2048];
    size_t ch_len = TlsConnection.start(&client, ch, sizeof ch);
    TEST_ASSERT_TRUE(patch_off < ch_len);
    ch[patch_off] = patch_val;

    (void)feed_all(&server, ch, ch_len, out, sizeof out);
    TEST_ASSERT_FALSE_MESSAGE(TlsConnection.established(&server), what);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, TlsConnection.alert(&server), what);
}

void test_client_hello_outside_the_profile_is_refused()
{
    // The record header is 5 bytes; the handshake header 4; then legacy_version and the random.
    // The cipher_suites list starts after the session id, which our own ClientHello leaves empty.
    static uint8_t ch[2048];
    TlsConnConfig ccfg = client_cfg();
    TlsConn probe;
    TEST_ASSERT_TRUE(TlsConnection.init(&probe, TLS_ROLE_CLIENT, &ccfg));
    size_t ch_len = TlsConnection.start(&probe, ch, sizeof ch);
    TEST_ASSERT_TRUE(ch_len > 0);

    const size_t hs = 5;                       // the ClientHello record is plaintext
    const size_t sid_len_off = hs + 4 + 2 + 32;
    const size_t cs_len_off = sid_len_off + 1 + ch[sid_len_off];
    const size_t cs_off = cs_len_off + 2;

    // Break the offered suite: 0x1301 becomes 0x1302, which this stack does not implement.
    refuse_mutated_hello(cs_off + 1, 0x02, "no offered cipher suite");
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_full_handshake);
    RUN_TEST(test_forged_application_record_is_refused);
    RUN_TEST(test_client_hello_outside_the_profile_is_refused);
    return UNITY_END();
}
