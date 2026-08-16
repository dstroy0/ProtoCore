// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the TLS 1.3 stream handshake driver (network_drivers/tls/handshake/handshake.h).
//
// The load-bearing case is test_full_handshake_on_rfc8448_key_material. Every key this server runs
// on is a published octet string: the X25519 pairs, the ServerHello Random and the ECDHE shared
// secret are printed in RFC 8448 sec 3 (Simple 1-RTT Handshake), and the Ed25519 credential is
// RFC 8032 sec 7.1 TEST 1. So the server's key_share and the secret the test client recovers are
// compared with numbers the documents publish, not with a second run of the same code. The message
// field offsets come from RFC 8446 sec 4.1.3 / 4.2.8 / 4.4.2 / 4.4.3 / 4.4.4 and are cross-checked
// against the 90-octet ServerHello dump in RFC 8448 sec 3; the 44-octet RawPublicKey credential is
// DER derived from RFC 8410 sec 3 and sec 4 in the comment that carries it.
//
// The alert cases are anchored on RFC 8446 sec 6 and sec 6.2, which name the alert for each fault
// class outright: sec 6 for a length that runs past the message boundary (decode_error), sec 4 for
// a handshake message received out of order (unexpected_message), sec 4.4.4 for a Finished whose
// contents do not verify (decrypt_error), sec 4.1.1 for parameters that do not overlap
// (handshake_failure or insufficient_security - the RFC permits either, so the case accepts either).
//
// test_a_missing_key_share_owes_a_hello_retry_request FAILS by design. RFC 8446 sec 4.1.1 makes a
// HelloRetryRequest a MUST when the server selects a group the client offered without a matching
// key_share; this driver refuses the connection instead. The case asserts the RFC's requirement.

#include "crypto/asymmetric/curve25519.h"
#include "crypto/asymmetric/ed25519.h"
#include "crypto/hash/sha256.h"
#include "crypto/hash/sha512.h"
#include "network_drivers/tls/handshake/handshake.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"
#include "network_drivers/tls/record/record.h"
#include <string.h>

#include <unity.h>

// RFC 8448 sec 3, "{client} create an ephemeral x25519 key pair": the private key and the public key
// the ClientHello of that trace carries in its key_share.
static const uint8_t CLIENT_X25519_PRIV[32] = {0x49, 0xaf, 0x42, 0xba, 0x7f, 0x79, 0x94, 0x85, 0x2d, 0x71, 0x3e,
                                               0xf2, 0x78, 0x4b, 0xcb, 0xca, 0xa7, 0x91, 0x1d, 0xe2, 0x6a, 0xdc,
                                               0x56, 0x42, 0xcb, 0x63, 0x45, 0x40, 0xe7, 0xea, 0x50, 0x05};
static const uint8_t CLIENT_X25519_PUB[32] = {0x99, 0x38, 0x1d, 0xe5, 0x60, 0xe4, 0xbd, 0x43, 0xd2, 0x3d, 0x8e,
                                              0x43, 0x5a, 0x7d, 0xba, 0xfe, 0xb3, 0xc0, 0x6e, 0x51, 0xc1, 0x3c,
                                              0xae, 0x4d, 0x54, 0x13, 0x69, 0x1e, 0x52, 0x9a, 0xaf, 0x2c};

// RFC 8448 sec 3, "{server} create an ephemeral x25519 key pair": the pair whose public half the
// ServerHello of that trace carries.
static const uint8_t SERVER_X25519_PRIV[32] = {0xb1, 0x58, 0x0e, 0xea, 0xdf, 0x6d, 0xd5, 0x89, 0xb8, 0xef, 0x4f,
                                               0x2d, 0x56, 0x52, 0x57, 0x8c, 0xc8, 0x10, 0xe9, 0x98, 0x01, 0x91,
                                               0xec, 0x8d, 0x05, 0x83, 0x08, 0xce, 0xa2, 0x16, 0xa2, 0x1e};
static const uint8_t SERVER_X25519_PUB[32] = {0xc9, 0x82, 0x88, 0x76, 0x11, 0x20, 0x95, 0xfe, 0x66, 0x76, 0x2b,
                                              0xdb, 0xf7, 0xc6, 0x72, 0xe1, 0x56, 0xd6, 0xcc, 0x25, 0x3b, 0x83,
                                              0x3d, 0xf1, 0xdd, 0x69, 0xb1, 0xb0, 0x4e, 0x75, 0x1f, 0x0f};

// RFC 8448 sec 3, "{server} extract secret \"handshake\"", IKM: X25519(client priv, server pub).
static const uint8_t RFC8448_ECDHE[32] = {0x8b, 0xd4, 0x05, 0x4f, 0xb5, 0x5b, 0x9d, 0x63, 0xfd, 0xfb, 0xac,
                                          0xf9, 0xf0, 0x4b, 0x9f, 0x0d, 0x35, 0xe6, 0xd6, 0x3f, 0x53, 0x75,
                                          0x63, 0xef, 0xd4, 0x62, 0x72, 0x90, 0x0f, 0x89, 0x49, 0x2d};

// RFC 8032 sec 7.1 TEST 1: SECRET KEY and the PUBLIC KEY it produces.
static const uint8_t SERVER_ED_SEED[32] = {0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a,
                                           0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32,
                                           0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};
static const uint8_t SERVER_ED_PUB[32] = {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
                                          0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
                                          0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};

// RFC 8448 sec 3, the ServerHello Random of that trace.
static const uint8_t SERVER_RANDOM[32] = {0xa6, 0xaf, 0x06, 0xa4, 0x12, 0x18, 0x60, 0xdc, 0x5e, 0x6e, 0x60,
                                          0x24, 0x9c, 0xd3, 0x4c, 0x95, 0x93, 0x0c, 0x8a, 0xc5, 0xcb, 0x14,
                                          0x34, 0xda, 0xc1, 0x55, 0x77, 0x2e, 0xd3, 0xe2, 0x69, 0x28};

// RFC 8446 sec 4.1.3: "the HelloRetryRequest message uses the same structure as the ServerHello, but
// with Random set to the special value of the SHA-256 of \"HelloRetryRequest\"", printed there as
// CF 21 AD 74 E5 9A 61 11 BE 1D 8C 02 1E 65 B8 91 C2 A2 11 16 7A BB 8C 5E 07 9E 09 E2 C8 A8 33 9C.
static const uint8_t HRR_RANDOM[32] = {0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11, 0xBE, 0x1D, 0x8C,
                                       0x02, 0x1E, 0x65, 0xB8, 0x91, 0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB,
                                       0x8C, 0x5E, 0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C};

// RFC 8446 sec 6.2 AlertDescription code points.
#define ALERT_UNEXPECTED_MESSAGE 10
#define ALERT_HANDSHAKE_FAILURE 40
#define ALERT_DECODE_ERROR 50
#define ALERT_DECRYPT_ERROR 51
#define ALERT_INSUFFICIENT_SECURITY 71
#define ALERT_INTERNAL_ERROR 80

static TlsConn g_conn;
static TlsConnConfig g_cfg;
static uint8_t g_srv_out[2048];

static uint8_t g_cli_ks_bytes[PROTOCORE_TLS13_KS_BORROW] __attribute__((aligned(8)));
static uint8_t g_cli_hash_work[PROTOCORE_SHA256_BORROW] __attribute__((aligned(4)));
static uint8_t g_sign_work[PROTOCORE_SHA512_BORROW] __attribute__((aligned(8)));
static Tls13KeySchedule g_cli_ks;
static uint8_t *g_cli_transcript;
static TlsRecordKeys g_cli_hs_rx;
static TlsRecordKeys g_cli_hs_tx;
static TlsRecordKeys g_cli_ap_rx;
static TlsRecordKeys g_cli_ap_tx;

void setUp(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    memset(g_cli_ks_bytes, 0, sizeof(g_cli_ks_bytes));
    memset(&g_cli_ks, 0, sizeof(g_cli_ks));
    memset(&g_cli_hs_rx, 0, sizeof(g_cli_hs_rx));
    memset(&g_cli_hs_tx, 0, sizeof(g_cli_hs_tx));
    memset(&g_cli_ap_rx, 0, sizeof(g_cli_ap_rx));
    memset(&g_cli_ap_tx, 0, sizeof(g_cli_ap_tx));

    g_cfg.ed25519_seed = SERVER_ED_SEED;
    g_cfg.ed25519_pub = SERVER_ED_PUB;
    g_cfg.ephemeral_priv = SERVER_X25519_PRIV;
    g_cfg.random = SERVER_RANDOM;
}
void tearDown(void)
{
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static size_t be24(const uint8_t *p)
{
    return ((size_t)p[0] << 16) | ((size_t)p[1] << 8) | (size_t)p[2];
}

// Walk an extension block of RFC 8446 sec 4.2 Extension records - extension_type(2),
// extension_data<0..2^16-1> - and report whether @p type appears carrying exactly @p body.
static proto_bool ext_carries(const uint8_t *ext, size_t ext_len, uint16_t type, const uint8_t *body, size_t body_len)
{
    size_t i = 0;
    while (i + 4 <= ext_len)
    {
        const uint16_t t = be16(ext + i);
        const size_t n = be16(ext + i + 2);
        if (i + 4 + n > ext_len)
        {
            return PROTO_FALSE;
        }
        if (t == type)
        {
            return (proto_bool)(n == body_len && memcmp(ext + i + 4, body, body_len) == 0);
        }
        i += 4 + n;
    }
    return PROTO_FALSE;
}

// Build a ClientHello per RFC 8446 sec 4.1.2: legacy_version 0x0303, Random(32),
// legacy_session_id<0..32>, cipher_suites<2..2^16-2>, legacy_compression_methods<1..2^8-1>,
// extensions<8..2^16-1>. The extension code points are the sec 4.2 table:
// supported_groups(10), signature_algorithms(13), supported_versions(43), key_share(51).
static size_t build_client_hello(uint8_t *out, proto_bool with_x25519_group, proto_bool with_share,
                                 proto_bool with_ed25519, uint16_t suite)
{
    size_t i = 0;
    out[i++] = TLS_HS_CLIENT_HELLO;
    size_t body_at = i;
    i += 3;

    out[i++] = 0x03;
    out[i++] = 0x03;
    for (size_t k = 0; k < 32; k++)
    {
        out[i++] = (uint8_t)(0xcb + k);
    }
    out[i++] = 0;

    out[i++] = 0x00;
    out[i++] = 0x02;
    out[i++] = (uint8_t)(suite >> 8);
    out[i++] = (uint8_t)suite;

    out[i++] = 0x01;
    out[i++] = 0x00;

    size_t ext_at = i;
    i += 2;

    // supported_versions, ClientHello form (sec 4.2.1): ProtocolVersion versions<2..254>.
    out[i++] = 0x00;
    out[i++] = 0x2b;
    out[i++] = 0x00;
    out[i++] = 0x03;
    out[i++] = 0x02;
    out[i++] = 0x03;
    out[i++] = 0x04;

    if (with_x25519_group)
    {
        // supported_groups (sec 4.2.7): NamedGroup named_group_list<2..2^16-2>, x25519 = 0x001D.
        out[i++] = 0x00;
        out[i++] = 0x0a;
        out[i++] = 0x00;
        out[i++] = 0x04;
        out[i++] = 0x00;
        out[i++] = 0x02;
        out[i++] = 0x00;
        out[i++] = 0x1d;
    }
    if (with_ed25519)
    {
        // signature_algorithms (sec 4.2.3): SignatureScheme list, ed25519 = 0x0807.
        out[i++] = 0x00;
        out[i++] = 0x0d;
        out[i++] = 0x00;
        out[i++] = 0x04;
        out[i++] = 0x00;
        out[i++] = 0x02;
        out[i++] = 0x08;
        out[i++] = 0x07;
    }
    if (with_share)
    {
        // key_share (sec 4.2.8): KeyShareEntry client_shares<0..2^16-1>, entry = group(2) +
        // key_exchange<1..2^16-1>. 2 + 2 + 32 = 36 = 0x24 of list, 38 = 0x26 of extension_data.
        out[i++] = 0x00;
        out[i++] = 0x33;
        out[i++] = 0x00;
        out[i++] = 0x26;
        out[i++] = 0x00;
        out[i++] = 0x24;
        out[i++] = 0x00;
        out[i++] = 0x1d;
        out[i++] = 0x00;
        out[i++] = 0x20;
        memcpy(out + i, CLIENT_X25519_PUB, 32);
        i += 32;
    }

    const size_t ext_len = i - ext_at - 2;
    out[ext_at] = (uint8_t)(ext_len >> 8);
    out[ext_at + 1] = (uint8_t)ext_len;
    const size_t body_len = i - body_at - 3;
    out[body_at] = (uint8_t)(body_len >> 16);
    out[body_at + 1] = (uint8_t)(body_len >> 8);
    out[body_at + 2] = (uint8_t)body_len;
    return i;
}

static void init_server(void)
{
    TlsConnection.conn = &g_conn;
    TlsConnection.init_args.role = TLS_ROLE_SERVER;
    TlsConnection.init_args.cfg = &g_cfg;
    TlsConnection.init(NULL);
    TEST_ASSERT_TRUE(TlsConnection.ok);
}

static int feed(const uint8_t *rec, size_t rec_len)
{
    memcpy(g_conn.rx, rec, rec_len);
    TlsConnection.conn = &g_conn;
    TlsConnection.io.rx_len = rec_len;
    TlsConnection.out_args.out = g_srv_out;
    TlsConnection.out_args.out_cap = sizeof(g_srv_out);
    TlsConnection.process(NULL);
    return TlsConnection.i32;
}

// Wrap @p frag in a TLSPlaintext record (sec 5.1) and hand it to the server.
static int feed_plaintext(uint8_t content_type, const uint8_t *frag, size_t frag_len, uint8_t *rec, size_t rec_cap)
{
    TlsRecord.content_type = content_type;
    TlsRecord.plain.fragment = frag;
    TlsRecord.plain.frag_len = frag_len;
    TlsRecord.out_args.out = rec;
    TlsRecord.out_args.out_cap = rec_cap;
    TlsRecord.plaintext_build(NULL);
    TEST_ASSERT_TRUE(TlsRecord.n > 0);
    return feed(rec, TlsRecord.n);
}

static uint8_t server_alert(void)
{
    TlsConnection.conn = &g_conn;
    TlsConnection.alert(NULL);
    return TlsConnection.u8;
}

static proto_bool server_established(void)
{
    TlsConnection.conn = &g_conn;
    TlsConnection.established(NULL);
    return TlsConnection.ok;
}

static void cli_keys(TlsRecordKeys *keys, const uint8_t *secret)
{
    TlsRecord.key.keys = keys;
    TlsRecord.key.cipher = TLS_CIPHER_AES_128_GCM_SHA256;
    TlsRecord.key.secret = secret;
    TlsRecord.keys_derive(NULL);
}

// RFC 8446 sec 4.4.3 prints the content covered by a server CertificateVerify signature for a
// transcript hash of 32 octets of 0x01:
//   64 octets of 0x20, then
//   544c5320312e332c20736572766572204365727469666963617465566572696679  ("TLS 1.3, server
//                                                                        CertificateVerify", 33)
//   00                                                                 (the separator)
//   32 octets of 0x01                                                  (the hash)
// 64 + 33 + 1 + 32 = 130 octets.
void test_rfc8446_cert_verify_content_worked_example(void)
{
    static const char SERVER_CTX[] = "TLS 1.3, server CertificateVerify";
    static const char CLIENT_CTX[] = "TLS 1.3, client CertificateVerify";
    uint8_t want[130];
    uint8_t hash[32];
    uint8_t out[160];

    memset(hash, 0x01, sizeof(hash));
    memset(want, 0x20, 64);
    memcpy(want + 64, SERVER_CTX, 33);
    want[97] = 0x00;
    memset(want + 98, 0x01, 32);

    TEST_ASSERT_EQUAL_UINT(130u, protocore_tls13_cert_verify_content(out, sizeof(out), hash, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 130);

    // Same section: the client's context string differs by one word, so the two contents must not
    // be equal - that separation is the whole point of the string.
    memcpy(want + 64, CLIENT_CTX, 33);
    TEST_ASSERT_EQUAL_UINT(130u, protocore_tls13_cert_verify_content(out, sizeof(out), hash, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 130);
}

// One whole handshake driven by a test client that holds the RFC 8448 sec 3 client key pair against
// a server holding that trace's server key pair and the RFC 8032 TEST 1 Ed25519 credential.
void test_full_handshake_on_rfc8448_key_material(void)
{
    uint8_t ch[512];
    uint8_t rec[1024];
    uint8_t pt[2048];
    uint8_t scratch[32];

    // The two published pairs: base(priv) is the public half each document prints.
    Curve25519.x25519_base_args.out = scratch;
    Curve25519.x25519_base_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_base(g_sign_work);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CLIENT_X25519_PUB, scratch, 32);
    Curve25519.x25519_base_args.out = scratch;
    Curve25519.x25519_base_args.scalar = SERVER_X25519_PRIV;
    Curve25519.x25519_base(g_sign_work);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SERVER_X25519_PUB, scratch, 32);
    Ed25519.pubkey_args.pub = scratch;
    Ed25519.pubkey_args.seed = SERVER_ED_SEED;
    Ed25519.pubkey(g_sign_work);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SERVER_ED_PUB, scratch, 32);

    init_server();
    g_cli_transcript = g_cli_hash_work;
    Sha256.init(g_cli_transcript);

    size_t ch_len = build_client_hello(ch, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = ch_len;
    Sha256.update(g_cli_transcript);

    int wrote = feed_plaintext(PROTOCORE_TLS_CT_HANDSHAKE, ch, ch_len, rec, sizeof(rec));
    TEST_ASSERT_TRUE_MESSAGE(wrote > 0, "the server owed a flight");

    // sec 5.1: TLSPlaintext is type(1) + legacy_record_version(2) + length(2), and
    // legacy_record_version "MUST be set to 0x0303 for all records generated by a TLS 1.3
    // implementation other than an initial ClientHello". The trace's own record reads 16 03 03 00 5a.
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_TLS_CT_HANDSHAKE, g_srv_out[0]);
    TEST_ASSERT_EQUAL_HEX16(0x0303, be16(g_srv_out + 1));

    TlsPlaintext view;
    TlsRecord.sealed.rec = g_srv_out;
    TlsRecord.sealed.rec_len = (size_t)wrote;
    TlsRecord.plain.view = &view;
    TlsRecord.plaintext_parse(NULL);
    size_t off = TlsRecord.n;
    TEST_ASSERT_TRUE(off > 0);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_TLS_CT_HANDSHAKE, view.content_type);
    Sha256.update_args.data = view.fragment;
    Sha256.update_args.len = view.frag_len;
    Sha256.update(g_cli_transcript);

    // ServerHello, sec 4.1.3: msg_type(1) legacy_version(2) random(32) legacy_session_id_echo<0..32>
    // cipher_suite(2) legacy_compression_method(1) extensions<6..2^16-1>. With an empty session id
    // the offsets are those of the 90-octet ServerHello printed in RFC 8448 sec 3:
    //   02 00 00 56 | 03 03 | a6 af .. 69 28 | 00 | 13 01 | 00 | 00 2e | 00 33 00 24 00 1d 00 20 ..
    const uint8_t *sh = view.fragment;
    TEST_ASSERT_EQUAL_HEX8(TLS_HS_SERVER_HELLO, sh[0]);
    TEST_ASSERT_EQUAL_UINT(view.frag_len, 4u + be24(sh + 1));
    TEST_ASSERT_EQUAL_HEX16(0x0303, be16(sh + 4));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SERVER_RANDOM, sh + 6, 32);
    TEST_ASSERT_EQUAL_UINT8(0u, sh[38]);
    TEST_ASSERT_EQUAL_HEX16(PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, be16(sh + 39));
    TEST_ASSERT_EQUAL_UINT8(0u, sh[41]);

    // sec 4.2.1: "A server which negotiates TLS 1.3 MUST respond by sending a 'supported_versions'
    // extension containing the selected version value (0x0304)". Extension order is not fixed, so
    // the block is walked rather than indexed.
    static const uint8_t TLS13[2] = {0x03, 0x04};
    const uint8_t *sh_ext = sh + 44;
    const size_t sh_ext_len = be16(sh + 42);
    TEST_ASSERT_EQUAL_UINT(view.frag_len, 44u + sh_ext_len);
    TEST_ASSERT_TRUE(ext_carries(sh_ext, sh_ext_len, 0x002b, TLS13, sizeof(TLS13)));

    // sec 4.2.8 KeyShareServerHello: a single KeyShareEntry - group(2) + key_exchange<1..2^16-1>.
    TEST_ASSERT_EQUAL_HEX16(0x0033, be16(sh_ext));
    TEST_ASSERT_EQUAL_UINT(36u, (size_t)be16(sh_ext + 2));
    TEST_ASSERT_EQUAL_HEX16(TLS_GROUP_X25519, be16(sh_ext + 4));
    TEST_ASSERT_EQUAL_UINT(32u, (size_t)be16(sh_ext + 6));
    const uint8_t *server_share = sh_ext + 8;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SERVER_X25519_PUB, server_share, 32);

    // The shared secret RFC 8448 sec 3 feeds into HKDF-Extract as the "handshake" IKM.
    uint8_t ecdhe[32];
    Curve25519.x25519_args.out = ecdhe;
    Curve25519.x25519_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_args.point = server_share;
    Curve25519.x25519(g_sign_work);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RFC8448_ECDHE, ecdhe, 32);

    uint8_t ch_sh_hash[32];
    Sha256.final_args.out = ch_sh_hash;
    Sha256.final(g_cli_transcript);

    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &g_cli_ks;
    Tls13Ks.bind.s = g_cli_ks_bytes;
    Tls13Ks.early(NULL);
    TEST_ASSERT_TRUE(Tls13Ks.ok);
    Tls13Ks.bind.ks = &g_cli_ks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = sizeof(ecdhe);
    Tls13Ks.step.ch_sh_hash = ch_sh_hash;
    Tls13Ks.handshake(NULL);

    cli_keys(&g_cli_hs_rx, g_cli_ks.s + TLS13_KS_SERVER_HS);
    cli_keys(&g_cli_hs_tx, g_cli_ks.s + TLS13_KS_CLIENT_HS);

    // sec 2 figure 1 and sec 4.4.1: the server's encrypted flight is EncryptedExtensions,
    // Certificate, CertificateVerify, Finished, in that order, and sec 4 makes the order a MUST.
    static const uint8_t FLIGHT[4] = {TLS_HS_ENCRYPTED_EXTENSIONS, TLS_HS_CERTIFICATE, TLS_HS_CERTIFICATE_VERIFY,
                                      TLS_HS_FINISHED};
    uint8_t cert_verify_hash[32];
    uint8_t peer_pub[32];
    uint8_t cv_sig[PROTOCORE_ED25519_SIG_LEN];
    size_t seen = 0;

    while (off < (size_t)wrote)
    {
        TlsCiphertext info;
        size_t body = (size_t)be16(g_srv_out + off + 3);
        size_t whole = PROTOCORE_TLS_PLAINTEXT_HDR_LEN + body;
        TEST_ASSERT_TRUE(off + whole <= (size_t)wrote);
        // sec 5.2: "TLSCiphertext.opaque_type ... is set to the value 23 (application_data)".
        TEST_ASSERT_EQUAL_HEX8(PROTOCORE_TLS_CT_APPLICATION_DATA, g_srv_out[off]);
        TEST_ASSERT_EQUAL_HEX16(0x0303, be16(g_srv_out + off + 1));

        TlsRecord.key.keys = &g_cli_hs_rx;
        TlsRecord.sealed.rec = g_srv_out + off;
        TlsRecord.sealed.rec_len = whole;
        TlsRecord.sealed.info = &info;
        TlsRecord.out_args.out = pt;
        TlsRecord.out_args.out_cap = sizeof(pt);
        TlsRecord.unprotect(NULL);
        TEST_ASSERT_TRUE_MESSAGE(TlsRecord.ok, "the client could not open a server handshake record");
        TEST_ASSERT_EQUAL_UINT8(PROTOCORE_TLS_CT_HANDSHAKE, info.content_type);
        off += whole;

        const uint8_t *msg = pt;
        size_t body_len = be24(msg + 1);
        TEST_ASSERT_EQUAL_UINT(info.pt_len, 4u + body_len);
        TEST_ASSERT_TRUE(seen < sizeof(FLIGHT));
        TEST_ASSERT_EQUAL_HEX8(FLIGHT[seen], msg[0]);

        if (msg[0] == TLS_HS_CERTIFICATE)
        {
            // sec 4.4.2: certificate_request_context<0..2^8-1> then certificate_list<0..2^24-1> of
            // CertificateEntry - cert_data<1..2^24-1> then extensions<0..2^16-1>. One entry here,
            // so msg[4]=0, msg[5..7]=list length, msg[8..10]=cert_data length, msg[11..]=cert_data.
            TEST_ASSERT_EQUAL_UINT8(0u, msg[4]);
            TEST_ASSERT_EQUAL_UINT((size_t)PROTOCORE_TLS13_ED25519_SPKI_LEN, be24(msg + 8));
            // RFC 7250 sec 3 RawPublicKey: cert_data is a DER SubjectPublicKeyInfo. RFC 8410 sec 4
            // gives SPKI = SEQUENCE { AlgorithmIdentifier, BIT STRING }; sec 3 gives id-Ed25519 =
            // { 1 3 101 112 }, whose DER value octets are 40*1+3 = 0x2B, 101 = 0x65, 112 = 0x70:
            //   06 03 2B 65 70              OID, 3 value octets
            //   30 05 <that>                AlgorithmIdentifier, PARAMS absent, 5 content octets
            //   03 21 00 <32 key octets>    BIT STRING, 33 content octets, 0 unused bits
            //   30 2A <the two above>       SPKI, 7 + 35 = 42 = 0x2A content octets -> 44 total
            static const uint8_t SPKI_PREFIX[12] = {0x30, 0x2a, 0x30, 0x05, 0x06, 0x03,
                                                    0x2b, 0x65, 0x70, 0x03, 0x21, 0x00};
            TEST_ASSERT_EQUAL_UINT8_ARRAY(SPKI_PREFIX, msg + 11, sizeof(SPKI_PREFIX));
            memcpy(peer_pub, msg + 11 + sizeof(SPKI_PREFIX), 32);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(SERVER_ED_PUB, peer_pub, 32);
        }
        if (msg[0] == TLS_HS_CERTIFICATE_VERIFY)
        {
            // sec 4.4.3: algorithm(2) then signature<0..2^16-1>, over
            // Transcript-Hash(Handshake Context, Certificate). ed25519 = 0x0807 (sec 4.2.3) and
            // RFC 8032 sec 5.1.6 makes an Ed25519 signature 64 octets.
            Sha256.final_args.out = cert_verify_hash;
            Sha256.final(g_cli_transcript);
            TEST_ASSERT_EQUAL_HEX16(TLS_SIG_ED25519, be16(msg + 4));
            TEST_ASSERT_EQUAL_UINT(64u, (size_t)be16(msg + 6));
            TEST_ASSERT_EQUAL_UINT((size_t)PROTOCORE_ED25519_SIG_LEN, (size_t)be16(msg + 6));
            memcpy(cv_sig, msg + 8, PROTOCORE_ED25519_SIG_LEN);
        }
        if (msg[0] == TLS_HS_FINISHED)
        {
            // sec 4.4.4: "struct { opaque verify_data[Hash.length]; } Finished", Hash = SHA-256.
            uint8_t hash[32];
            uint8_t expect[32];
            Sha256.final_args.out = hash;
            Sha256.final(g_cli_transcript);
            Tls13Ks.bind.ks = &g_cli_ks;
            Tls13Ks.finished_args.base_secret = g_cli_ks.s + TLS13_KS_SERVER_HS;
            Tls13Ks.finished_args.transcript_hash = hash;
            Tls13Ks.finished_args.out = expect;
            Tls13Ks.finished_mac(NULL);
            TEST_ASSERT_EQUAL_UINT(32u, body_len);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, msg + 4, 32);
        }
        Sha256.update_args.data = msg;
        Sha256.update_args.len = info.pt_len;
        Sha256.update(g_cli_transcript);
        seen++;
    }
    TEST_ASSERT_EQUAL_UINT(sizeof(FLIGHT), seen);

    uint8_t content[160];
    size_t clen = protocore_tls13_cert_verify_content(content, sizeof(content), cert_verify_hash, PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT(130u, clen);
    Ed25519.verify_args.pub = peer_pub;
    Ed25519.verify_args.msg = content;
    Ed25519.verify_args.msg_len = clen;
    Ed25519.verify_args.sig = cv_sig;
    Ed25519.verify(g_sign_work);
    TEST_ASSERT_TRUE_MESSAGE(Ed25519.ok, "the server's CertificateVerify did not verify under the key it presented");

    uint8_t ch_sfin_hash[32];
    Sha256.final_args.out = ch_sfin_hash;
    Sha256.final(g_cli_transcript);
    Tls13Ks.bind.ks = &g_cli_ks;
    Tls13Ks.step.ch_sfin_hash = ch_sfin_hash;
    Tls13Ks.master(NULL);
    cli_keys(&g_cli_ap_rx, g_cli_ks.s + TLS13_KS_SERVER_AP);
    cli_keys(&g_cli_ap_tx, g_cli_ks.s + TLS13_KS_CLIENT_AP);

    uint8_t verify[32];
    Tls13Ks.bind.ks = &g_cli_ks;
    Tls13Ks.finished_args.base_secret = g_cli_ks.s + TLS13_KS_CLIENT_HS;
    Tls13Ks.finished_args.transcript_hash = ch_sfin_hash;
    Tls13Ks.finished_args.out = verify;
    Tls13Ks.finished_mac(NULL);

    uint8_t fin_msg[4 + 32];
    fin_msg[0] = TLS_HS_FINISHED;
    fin_msg[1] = 0;
    fin_msg[2] = 0;
    fin_msg[3] = 32;
    memcpy(fin_msg + 4, verify, 32);

    TlsRecord.key.keys = &g_cli_hs_tx;
    TlsRecord.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
    TlsRecord.sealed.pt = fin_msg;
    TlsRecord.sealed.pt_len = sizeof(fin_msg);
    TlsRecord.out_args.out = rec;
    TlsRecord.out_args.out_cap = sizeof(rec);
    TlsRecord.protect(NULL);
    size_t rec_len = TlsRecord.n;
    TEST_ASSERT_TRUE(rec_len > 0);

    TEST_ASSERT_EQUAL_INT(0, feed(rec, rec_len));
    TEST_ASSERT_TRUE_MESSAGE(server_established(), "the handshake did not complete");
    TEST_ASSERT_EQUAL_UINT8(0u, server_alert());

    // Application data both ways under the keys each end derived independently: what the two ends
    // installed must open what the other sealed.
    static const uint8_t SERVER_MSG[13] = "hello, client";
    TlsConnection.conn = &g_conn;
    TlsConnection.io.data = SERVER_MSG;
    TlsConnection.io.len = sizeof(SERVER_MSG);
    TlsConnection.out_args.out = g_srv_out;
    TlsConnection.out_args.out_cap = sizeof(g_srv_out);
    TlsConnection.seal_app(NULL);
    size_t app_len = TlsConnection.n;
    TEST_ASSERT_TRUE(app_len > 0);

    TlsCiphertext info;
    TlsRecord.key.keys = &g_cli_ap_rx;
    TlsRecord.sealed.rec = g_srv_out;
    TlsRecord.sealed.rec_len = app_len;
    TlsRecord.sealed.info = &info;
    TlsRecord.out_args.out = pt;
    TlsRecord.out_args.out_cap = sizeof(pt);
    TlsRecord.unprotect(NULL);
    TEST_ASSERT_TRUE_MESSAGE(TlsRecord.ok, "the client could not open the server's application record");
    TEST_ASSERT_EQUAL_UINT(sizeof(SERVER_MSG), info.pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SERVER_MSG, pt, sizeof(SERVER_MSG));

    static const uint8_t CLIENT_MSG[13] = "hello, server";
    TlsRecord.key.keys = &g_cli_ap_tx;
    TlsRecord.content_type = PROTOCORE_TLS_CT_APPLICATION_DATA;
    TlsRecord.sealed.pt = CLIENT_MSG;
    TlsRecord.sealed.pt_len = sizeof(CLIENT_MSG);
    TlsRecord.out_args.out = rec;
    TlsRecord.out_args.out_cap = sizeof(rec);
    TlsRecord.protect(NULL);
    rec_len = TlsRecord.n;
    TEST_ASSERT_TRUE(rec_len > 0);

    size_t got = 0;
    TlsConnection.conn = &g_conn;
    TlsConnection.io.rec = rec;
    TlsConnection.io.rec_len = rec_len;
    TlsConnection.out_args.out = pt;
    TlsConnection.out_args.out_cap = sizeof(pt);
    TlsConnection.out_args.out_len = &got;
    TlsConnection.open_app(NULL);
    TEST_ASSERT_TRUE_MESSAGE(TlsConnection.ok, "the server could not open the client's application record");
    TEST_ASSERT_EQUAL_UINT(sizeof(CLIENT_MSG), got);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CLIENT_MSG, pt, sizeof(CLIENT_MSG));
}

// RFC 8446 sec 4.1.1: "If the server is unable to negotiate a supported set of parameters (i.e.,
// there is no overlap between the client and server parameters), it MUST abort the handshake with
// either a 'handshake_failure' or 'insufficient_security' fatal alert". The RFC permits either, so
// the case accepts either; 40 and 71 are the sec 6.2 code points.
void test_no_overlapping_parameters_is_a_handshake_failure(void)
{
    uint8_t ch[512];
    uint8_t rec[1024];

    struct
    {
        proto_bool group;
        proto_bool ed;
        uint16_t suite;
        const char *why;
    } static const CASES[] = {
        {PROTO_FALSE, PROTO_TRUE, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, "no x25519 in supported_groups"},
        {PROTO_TRUE, PROTO_FALSE, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256, "no ed25519 in signature_algorithms"},
        {PROTO_TRUE, PROTO_TRUE, 0x1302, "a cipher suite this stack does not implement"},
    };

    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        setUp();
        init_server();
        size_t ch_len = build_client_hello(ch, CASES[i].group, PROTO_TRUE, CASES[i].ed, CASES[i].suite);
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, feed_plaintext(PROTOCORE_TLS_CT_HANDSHAKE, ch, ch_len, rec, sizeof(rec)),
                                      CASES[i].why);
        const uint8_t a = server_alert();
        TEST_ASSERT_TRUE_MESSAGE(a == ALERT_HANDSHAKE_FAILURE || a == ALERT_INSUFFICIENT_SECURITY, CASES[i].why);
        TEST_ASSERT_FALSE(server_established());
    }
}

// FAILS BY DESIGN. RFC 8446 sec 4.1.1: "If the server selects an (EC)DHE group and the client did
// not offer a compatible 'key_share' extension in the initial ClientHello, the server MUST respond
// with a HelloRetryRequest (Section 4.1.4) message." This ClientHello lists x25519 in
// supported_groups - so the sec 4.1.1 no-overlap abort does not apply - and carries no key_share.
// The HelloRetryRequest is a ServerHello (sec 4.1.3) whose Random is the published SHA-256 of
// "HelloRetryRequest". handshake.c answers handshake_failure instead and writes nothing.
void test_a_missing_key_share_owes_a_hello_retry_request(void)
{
    uint8_t ch[512];
    uint8_t rec[1024];

    init_server();
    size_t ch_len = build_client_hello(ch, PROTO_TRUE, PROTO_FALSE, PROTO_TRUE, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256);
    int wrote = feed_plaintext(PROTOCORE_TLS_CT_HANDSHAKE, ch, ch_len, rec, sizeof(rec));
    TEST_ASSERT_TRUE_MESSAGE(wrote > 0, "RFC 8446 sec 4.1.1 MUST: a missing key_share owes a HelloRetryRequest");

    TlsPlaintext view;
    TlsRecord.sealed.rec = g_srv_out;
    TlsRecord.sealed.rec_len = (size_t)wrote;
    TlsRecord.plain.view = &view;
    TlsRecord.plaintext_parse(NULL);
    TEST_ASSERT_TRUE(TlsRecord.n > 0);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_TLS_CT_HANDSHAKE, view.content_type);
    TEST_ASSERT_EQUAL_HEX8(TLS_HS_SERVER_HELLO, view.fragment[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(HRR_RANDOM, view.fragment + 6, 32);
}

// RFC 8446 sec 6: "Peers which receive a message which cannot be parsed according to the syntax
// (e.g., have a length extending beyond the message boundary or contain an out-of-range length)
// MUST terminate the connection with a 'decode_error' alert." Both faults here are that: a
// ClientHello whose 24-bit body length names four octets more than the record carries, and a
// record cut short of the five-octet TLSPlaintext header of sec 5.1.
void test_a_length_past_the_message_boundary_is_a_decode_error(void)
{
    uint8_t ch[512];
    uint8_t rec[1024];

    init_server();
    size_t ch_len = build_client_hello(ch, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256);
    ch[3] = (uint8_t)(ch[3] + 4);
    TEST_ASSERT_EQUAL_INT(-1, feed_plaintext(PROTOCORE_TLS_CT_HANDSHAKE, ch, ch_len, rec, sizeof(rec)));
    TEST_ASSERT_EQUAL_UINT8(ALERT_DECODE_ERROR, server_alert());

    setUp();
    init_server();
    static const uint8_t SHORT_RECORD[3] = {0x16, 0x03, 0x03};
    TEST_ASSERT_EQUAL_INT(-1, feed(SHORT_RECORD, sizeof(SHORT_RECORD)));
    TEST_ASSERT_EQUAL_UINT8(ALERT_DECODE_ERROR, server_alert());
}

// RFC 8446 sec 4: "Protocol messages MUST be sent in the order defined in Section 4.4.1 and shown in
// the diagrams in Section 2. A peer which receives a handshake message in an unexpected order MUST
// abort the handshake with an 'unexpected_message' alert." A Finished before any ClientHello is
// that. sec 6.2 names the same alert for "premature Application Data", which is the second half.
void test_a_message_in_the_wrong_order_is_unexpected_message(void)
{
    uint8_t rec[1024];

    init_server();
    uint8_t fin[4 + 32];
    memset(fin, 0, sizeof(fin));
    fin[0] = TLS_HS_FINISHED;
    fin[3] = 32;
    TEST_ASSERT_EQUAL_INT(-1, feed_plaintext(PROTOCORE_TLS_CT_HANDSHAKE, fin, sizeof(fin), rec, sizeof(rec)));
    TEST_ASSERT_EQUAL_UINT8(ALERT_UNEXPECTED_MESSAGE, server_alert());

    setUp();
    init_server();
    static const uint8_t EARLY_APP[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(
        -1, feed_plaintext(PROTOCORE_TLS_CT_APPLICATION_DATA, EARLY_APP, sizeof(EARLY_APP), rec, sizeof(rec)));
    TEST_ASSERT_EQUAL_UINT8(ALERT_UNEXPECTED_MESSAGE, server_alert());
}

// RFC 8446 sec 5: an unencrypted change_cipher_spec record "consisting of the single byte value
// 0x01 at any time after the first ClientHello message has been sent or received and before the
// peer's Finished message has been received ... MUST simply drop it without further processing".
// Dropped means the connection is untouched: no alert, no failure, nothing written back.
void test_a_middlebox_change_cipher_spec_is_dropped(void)
{
    uint8_t ch[512];
    uint8_t rec[1024];
    static const uint8_t CCS[1] = {0x01};

    init_server();
    size_t ch_len = build_client_hello(ch, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256);
    TEST_ASSERT_TRUE(feed_plaintext(PROTOCORE_TLS_CT_HANDSHAKE, ch, ch_len, rec, sizeof(rec)) > 0);

    TEST_ASSERT_EQUAL_INT(0, feed_plaintext(PROTOCORE_TLS_CT_CHANGE_CIPHER_SPEC, CCS, sizeof(CCS), rec, sizeof(rec)));
    TEST_ASSERT_EQUAL_UINT8(0u, server_alert());
    TEST_ASSERT_FALSE(server_established());
}

// RFC 8446 sec 4.4.4: "Recipients of Finished messages MUST verify that the contents are correct and
// if incorrect MUST terminate the connection with a 'decrypt_error' alert." The record here opens
// under the right key, so the record layer is satisfied and only verify_data is wrong.
void test_a_wrong_client_finished_is_decrypt_error(void)
{
    uint8_t ch[512];
    uint8_t rec[1024];

    init_server();
    g_cli_transcript = g_cli_hash_work;
    Sha256.init(g_cli_transcript);
    size_t ch_len = build_client_hello(ch, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE, PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256);
    Sha256.update_args.data = ch;
    Sha256.update_args.len = ch_len;
    Sha256.update(g_cli_transcript);
    int wrote = feed_plaintext(PROTOCORE_TLS_CT_HANDSHAKE, ch, ch_len, rec, sizeof(rec));
    TEST_ASSERT_TRUE(wrote > 0);

    TlsPlaintext view;
    TlsRecord.sealed.rec = g_srv_out;
    TlsRecord.sealed.rec_len = (size_t)wrote;
    TlsRecord.plain.view = &view;
    TlsRecord.plaintext_parse(NULL);
    Sha256.update_args.data = view.fragment;
    Sha256.update_args.len = view.frag_len;
    Sha256.update(g_cli_transcript);
    const uint8_t *server_share = view.fragment + 52;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SERVER_X25519_PUB, server_share, 32);

    uint8_t ecdhe[32];
    uint8_t ch_sh_hash[32];
    Curve25519.x25519_args.out = ecdhe;
    Curve25519.x25519_args.scalar = CLIENT_X25519_PRIV;
    Curve25519.x25519_args.point = server_share;
    Curve25519.x25519(g_sign_work);
    Sha256.final_args.out = ch_sh_hash;
    Sha256.final(g_cli_transcript);
    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.ks = &g_cli_ks;
    Tls13Ks.bind.s = g_cli_ks_bytes;
    Tls13Ks.early(NULL);
    Tls13Ks.bind.ks = &g_cli_ks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ecdhe_len = sizeof(ecdhe);
    Tls13Ks.step.ch_sh_hash = ch_sh_hash;
    Tls13Ks.handshake(NULL);
    cli_keys(&g_cli_hs_tx, g_cli_ks.s + TLS13_KS_CLIENT_HS);

    uint8_t fin_msg[4 + 32];
    memset(fin_msg, 0xA5, sizeof(fin_msg));
    fin_msg[0] = TLS_HS_FINISHED;
    fin_msg[1] = 0;
    fin_msg[2] = 0;
    fin_msg[3] = 32;

    TlsRecord.key.keys = &g_cli_hs_tx;
    TlsRecord.content_type = PROTOCORE_TLS_CT_HANDSHAKE;
    TlsRecord.sealed.pt = fin_msg;
    TlsRecord.sealed.pt_len = sizeof(fin_msg);
    TlsRecord.out_args.out = rec;
    TlsRecord.out_args.out_cap = sizeof(rec);
    TlsRecord.protect(NULL);

    TEST_ASSERT_EQUAL_INT(-1, feed(rec, TlsRecord.n));
    TEST_ASSERT_EQUAL_UINT8(ALERT_DECRYPT_ERROR, server_alert());
    TEST_ASSERT_FALSE(server_established());
}

// Application traffic keys exist only once the handshake completes (RFC 8446 sec 7.1: the
// application traffic secrets are derived from the Master Secret and Transcript-Hash(ClientHello..
// server Finished)). Before that there is nothing to seal or open with, whatever the caller asks.
void test_application_data_needs_the_handshake(void)
{
    uint8_t buf[256];
    size_t got = 0;
    static const uint8_t MSG[4] = {1, 2, 3, 4};

    init_server();
    TlsConnection.conn = &g_conn;
    TlsConnection.io.data = MSG;
    TlsConnection.io.len = sizeof(MSG);
    TlsConnection.out_args.out = buf;
    TlsConnection.out_args.out_cap = sizeof(buf);
    TlsConnection.seal_app(NULL);
    TEST_ASSERT_EQUAL_UINT(0u, TlsConnection.n);

    TlsConnection.conn = &g_conn;
    TlsConnection.io.rec = buf;
    TlsConnection.io.rec_len = sizeof(buf);
    TlsConnection.out_args.out = buf;
    TlsConnection.out_args.out_cap = sizeof(buf);
    TlsConnection.out_args.out_len = &got;
    TlsConnection.open_app(NULL);
    TEST_ASSERT_FALSE(TlsConnection.ok);
    TEST_ASSERT_FALSE(server_established());
}

// RFC 8446 sec 6: "Upon transmission or receipt of a fatal alert message, both parties MUST
// immediately close the connection ... without sending or receiving any additional data." A failed
// connection therefore keeps refusing, and the alert it ended on does not change.
void test_a_failed_connection_stays_failed(void)
{
    static const uint8_t SHORT_RECORD[3] = {0x16, 0x03, 0x03};

    init_server();
    TEST_ASSERT_EQUAL_INT(-1, feed(SHORT_RECORD, sizeof(SHORT_RECORD)));
    TEST_ASSERT_EQUAL_UINT8(ALERT_DECODE_ERROR, server_alert());
    TEST_ASSERT_EQUAL_INT(-1, feed(SHORT_RECORD, sizeof(SHORT_RECORD)));
    TEST_ASSERT_EQUAL_UINT8(ALERT_DECODE_ERROR, server_alert());
    TEST_ASSERT_FALSE(server_established());
}

// The client half of this driver is not built (handshake.h documents the server profile only), so it
// cannot produce the ClientHello sec 4.1.2 requires. RFC 8446 sec 6.2: "internal_error: An internal
// error unrelated to the peer or the correctness of the protocol ... makes it impossible to
// continue" - code point 80 in the sec 6.2 AlertDescription registry.
void test_the_client_role_refuses(void)
{
    uint8_t buf[256];
    TlsConnection.conn = &g_conn;
    TlsConnection.init_args.role = TLS_ROLE_CLIENT;
    TlsConnection.init_args.cfg = &g_cfg;
    TlsConnection.init(NULL);
    TEST_ASSERT_TRUE(TlsConnection.ok);

    TlsConnection.conn = &g_conn;
    TlsConnection.out_args.out = buf;
    TlsConnection.out_args.out_cap = sizeof(buf);
    TlsConnection.start(NULL);
    TEST_ASSERT_EQUAL_UINT(0u, TlsConnection.n);
    TEST_ASSERT_EQUAL_UINT8(ALERT_INTERNAL_ERROR, server_alert());
    TEST_ASSERT_FALSE(server_established());
}
