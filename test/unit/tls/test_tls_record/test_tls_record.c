// Copyright (C) 2026 Douglas Quigg (dstroy0)
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TLS 1.3 stream record layer (RFC 8446 sec 5). Properties and wire structure; the AEAD itself is
// pinned elsewhere (test_crypto_kat) and the key schedule against RFC 8448 (test_tls13_kdf), so what
// is checked here is what this layer adds: the header, the inner content type, the padding strip,
// and the record counter that never travels.

#include "mmgr/secure.h"
#include "network_drivers/tls/tls_record.h"
#include <stdint.h>
#include <string.h>

#include <unity.h>

// Any 32 bytes work as a traffic secret: the schedule that turns it into {key, iv} is tested in
// test_tls13_kdf. What matters here is that both directions derive from the same one and agree.
static const uint8_t SECRET[32] = {0x9e, 0x40, 0x64, 0x6c, 0xe7, 0x9a, 0x7f, 0x9d, 0xc0, 0x5a, 0xf8,
                                   0x88, 0x9b, 0xce, 0x65, 0x52, 0x87, 0x5a, 0xfa, 0x0b, 0x06, 0xdf,
                                   0x00, 0x87, 0xf7, 0x92, 0xeb, 0xb7, 0xc1, 0x75, 0x04, 0xa5};

// ---- KAT: secret 0x00..0x1f, seq 5, content_type 22, "hello tls 1.3" -------
// Byte-exact against an independent reconstruction (tools/crypto/gen_tls_record_kat.py): stdlib
// hmac/hashlib for HKDF-Expand-Label under the "tls13 " prefix, and OpenSSL's AES-128-GCM via the
// cryptography package. Neither shares code with ProtoCore, so this pins the key schedule, the
// iv XOR seq nonce, the header as associated data, and the trailing content type all at once.
static const uint8_t KAT_SECRET[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                       0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                       0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
static const uint8_t KAT_IV[12] = {0x2f, 0x41, 0xc8, 0x46, 0xa4, 0x31, 0xa1, 0x63, 0x81, 0x4b, 0xcd, 0x71};
static const uint8_t KAT_WIRE[35] = {
    0x17, 0x03, 0x03, 0x00, 0x1e, 0x09, 0xf7, 0xc0, 0x34, 0x70, 0xed, 0x10,
    0x8b, 0x2e, 0xc9, 0xbd, 0xb0, 0x19, 0x5f, 0x08, 0x06, 0x5f, 0x86, 0x10,
    0x1b, 0x8a, 0x17, 0x2c, 0xbd, 0xc9, 0x9b, 0xc7, 0x63, 0x3c, 0xff,
};
static const char *KAT_PLAINTEXT = "hello tls 1.3";
static const uint64_t KAT_SEQ = 5;
static const uint8_t KAT_CT = PC_TLS_CT_HANDSHAKE; // 22

// Whether needle occurs in hay. memmem is a GNU extension and these envs ask glibc for POSIX alone.
static proto_bool contains(const uint8_t *hay, size_t hay_len, const uint8_t *needle, size_t needle_len)
{
    if (needle_len == 0 || needle_len > hay_len)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i + needle_len <= hay_len; i++)
    {
        if (memcmp(hay + i, needle, needle_len) == 0)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

static TlsRecordKeys g_send;
static TlsRecordKeys g_recv;

void setUp()
{
    pc_secure_reset();
    memset(&g_send, 0, sizeof(g_send));
    memset(&g_recv, 0, sizeof(g_recv));
    TlsRecord.keys_derive(&g_send, TLS_CIPHER_AES_128_GCM_SHA256, SECRET);
    TlsRecord.keys_derive(&g_recv, TLS_CIPHER_AES_128_GCM_SHA256, SECRET);
}
void tearDown()
{
}

// ---- TLSPlaintext ---------------------------------------------------------

void test_plaintext_round_trips()
{
    const uint8_t frag[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t rec[64];
    size_t n = TlsRecord.plaintext_build(PC_TLS_CT_HANDSHAKE, frag, sizeof(frag), rec, sizeof(rec));
    TEST_ASSERT_EQUAL_INT((int)(PC_TLS_PLAINTEXT_HDR_LEN + sizeof(frag)), n);

    // Header: type, legacy_record_version 0x0303, length.
    TEST_ASSERT_EQUAL_HEX8(PC_TLS_CT_HANDSHAKE, rec[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03, rec[1]);
    TEST_ASSERT_EQUAL_HEX8(0x03, rec[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, rec[3]);
    TEST_ASSERT_EQUAL_HEX8(sizeof(frag), rec[4]);

    TlsPlaintext p;
    TEST_ASSERT_EQUAL_INT((int)n, TlsRecord.plaintext_parse(rec, n, &p));
    TEST_ASSERT_EQUAL_HEX8(PC_TLS_CT_HANDSHAKE, p.content_type);
    TEST_ASSERT_EQUAL_INT((int)sizeof(frag), p.frag_len);
    TEST_ASSERT_EQUAL_MEMORY(frag, p.fragment, sizeof(frag));
}

void test_plaintext_parse_refuses_truncated()
{
    uint8_t rec[16];
    size_t n = TlsRecord.plaintext_build(PC_TLS_CT_ALERT, (const uint8_t *)"\x02\x28", 2, rec, sizeof(rec));
    TlsPlaintext p;
    TEST_ASSERT_EQUAL_INT((int)0, TlsRecord.plaintext_parse(rec, n - 1, &p)); // one byte short of the body
    TEST_ASSERT_EQUAL_INT((int)0, TlsRecord.plaintext_parse(rec, 4, &p));     // short of the header
}

// A ClientHello arrives carrying legacy_record_version 0x0301; RFC 8446 sec 5.1 says ignore it.
void test_plaintext_parse_ignores_legacy_version()
{
    uint8_t rec[8] = {PC_TLS_CT_HANDSHAKE, 0x03, 0x01, 0x00, 0x02, 0xAA, 0xBB, 0x00};
    TlsPlaintext p;
    TEST_ASSERT_EQUAL_INT((int)7, TlsRecord.plaintext_parse(rec, sizeof(rec), &p));
    TEST_ASSERT_EQUAL_INT((int)2, p.frag_len);
}

void test_plaintext_build_refuses_overflow()
{
    uint8_t rec[8];
    const uint8_t frag[8] = {0};
    TEST_ASSERT_EQUAL_INT((int)0, TlsRecord.plaintext_build(PC_TLS_CT_HANDSHAKE, frag, sizeof(frag), rec, sizeof(rec)));
}

// ---- TLSCiphertext --------------------------------------------------------

void test_protect_round_trips_and_hides_the_type()
{
    const uint8_t pt[] = "the quick brown fox";
    const size_t pt_len = sizeof(pt) - 1;
    uint8_t rec[128];
    size_t n = TlsRecord.protect(&g_send, PC_TLS_CT_HANDSHAKE, pt, pt_len, rec, sizeof(rec));
    TEST_ASSERT_EQUAL_INT((int)(PC_TLS_PLAINTEXT_HDR_LEN + pt_len + 1 + PC_TLS_TAG_LEN), n);

    // The outer type is application_data whatever the inner type is (RFC 8446 sec 5.2).
    TEST_ASSERT_EQUAL_HEX8(PC_TLS_CT_APPLICATION_DATA, rec[0]);
    TEST_ASSERT_EQUAL_INT((int)(n - PC_TLS_PLAINTEXT_HDR_LEN), (int)(((size_t)rec[3] << 8) | rec[4]));
    // The plaintext must not be on the wire.
    TEST_ASSERT_FALSE(contains(rec, n, pt, pt_len));

    uint8_t out[128];
    TlsCiphertext info;
    TEST_ASSERT_TRUE(TlsRecord.unprotect(&g_recv, rec, n, out, sizeof(out), &info));
    TEST_ASSERT_EQUAL_HEX8(PC_TLS_CT_HANDSHAKE, info.content_type);
    TEST_ASSERT_EQUAL_INT((int)pt_len, info.pt_len);
    TEST_ASSERT_EQUAL_MEMORY(pt, out, pt_len);
}

// The record number is never sent; it is each side's own count. Two records of the same plaintext
// must differ, and must open in order.
void test_sequence_advances_and_records_differ()
{
    const uint8_t pt[] = {0x42, 0x42, 0x42, 0x42};
    uint8_t a[64];
    uint8_t b[64];
    size_t na = TlsRecord.protect(&g_send, PC_TLS_CT_APPLICATION_DATA, pt, sizeof(pt), a, sizeof(a));
    size_t nb = TlsRecord.protect(&g_send, PC_TLS_CT_APPLICATION_DATA, pt, sizeof(pt), b, sizeof(b));
    TEST_ASSERT_EQUAL_INT((int)na, nb);
    TEST_ASSERT_NOT_EQUAL(0, memcmp(a, b, na)); // different nonce -> different ciphertext

    uint8_t out[64];
    TlsCiphertext info;
    TEST_ASSERT_TRUE(TlsRecord.unprotect(&g_recv, a, na, out, sizeof(out), &info));
    TEST_ASSERT_TRUE(TlsRecord.unprotect(&g_recv, b, nb, out, sizeof(out), &info));
}

// Out of order is not recoverable on a stream: TCP already guarantees order, so record 1 presented
// first simply fails to open. What matters is that it fails rather than opening as something else.
void test_out_of_order_record_fails()
{
    const uint8_t pt[] = {0x01};
    uint8_t a[64];
    uint8_t b[64];
    size_t na = TlsRecord.protect(&g_send, PC_TLS_CT_APPLICATION_DATA, pt, sizeof(pt), a, sizeof(a));
    size_t nb = TlsRecord.protect(&g_send, PC_TLS_CT_APPLICATION_DATA, pt, sizeof(pt), b, sizeof(b));
    (void)na;

    uint8_t out[64];
    TlsCiphertext info;
    TEST_ASSERT_FALSE(TlsRecord.unprotect(&g_recv, b, nb, out, sizeof(out), &info));
}

void test_tampered_record_is_refused()
{
    const uint8_t pt[] = {0x10, 0x20, 0x30};
    uint8_t rec[64];
    size_t n = TlsRecord.protect(&g_send, PC_TLS_CT_APPLICATION_DATA, pt, sizeof(pt), rec, sizeof(rec));

    uint8_t out[64];
    TlsCiphertext info;

    rec[PC_TLS_PLAINTEXT_HDR_LEN] ^= 0x01; // flip a ciphertext bit
    TEST_ASSERT_FALSE(TlsRecord.unprotect(&g_recv, rec, n, out, sizeof(out), &info));
    rec[PC_TLS_PLAINTEXT_HDR_LEN] ^= 0x01;

    rec[n - 1] ^= 0x80; // flip a tag bit
    TEST_ASSERT_FALSE(TlsRecord.unprotect(&g_recv, rec, n, out, sizeof(out), &info));
    rec[n - 1] ^= 0x80;

    rec[0] ^= 0xFF; // the header is the associated data
    TEST_ASSERT_FALSE(TlsRecord.unprotect(&g_recv, rec, n, out, sizeof(out), &info));
    rec[0] ^= 0xFF;

    // The counter did not move on any of those, so the genuine record still opens.
    TEST_ASSERT_TRUE(TlsRecord.unprotect(&g_recv, rec, n, out, sizeof(out), &info));
    TEST_ASSERT_EQUAL_INT((int)sizeof(pt), info.pt_len);
}

void test_short_and_malformed_records_are_refused()
{
    uint8_t out[64];
    TlsCiphertext info;
    uint8_t rec[PC_TLS_PLAINTEXT_HDR_LEN + PC_TLS_TAG_LEN];
    memset(rec, 0, sizeof(rec));

    TEST_ASSERT_FALSE(TlsRecord.unprotect(&g_recv, rec, 4, out, sizeof(out), &info)); // short of a header
    rec[3] = 0;
    rec[4] = PC_TLS_TAG_LEN; // a body that is tag-only carries no inner plaintext
    TEST_ASSERT_FALSE(TlsRecord.unprotect(&g_recv, rec, sizeof(rec), out, sizeof(out), &info));
}

// An unkeyed context must refuse both directions rather than seal with a zero key.
void test_unkeyed_context_fails_closed()
{
    TlsRecordKeys cold;
    memset(&cold, 0, sizeof(cold));
    const uint8_t pt[] = {0x01};
    uint8_t rec[64];
    uint8_t out[64];
    TlsCiphertext info;
    TEST_ASSERT_EQUAL_INT((int)0, TlsRecord.protect(&cold, PC_TLS_CT_APPLICATION_DATA, pt, sizeof(pt), rec, sizeof(rec)));
    TEST_ASSERT_FALSE(TlsRecord.unprotect(&cold, rec, sizeof(rec), out, sizeof(out), &info));
}

void test_protect_refuses_overflow()
{
    const uint8_t pt[] = {1, 2, 3, 4};
    uint8_t rec[8]; // header + inner + tag does not fit
    TEST_ASSERT_EQUAL_INT((int)0, TlsRecord.protect(&g_send, PC_TLS_CT_APPLICATION_DATA, pt, sizeof(pt), rec, sizeof(rec)));
}

void test_empty_plaintext_carries_only_the_type()
{
    uint8_t rec[64];
    size_t n = TlsRecord.protect(&g_send, PC_TLS_CT_ALERT, NULL, 0, rec, sizeof(rec));
    TEST_ASSERT_EQUAL_INT((int)(PC_TLS_PLAINTEXT_HDR_LEN + 1 + PC_TLS_TAG_LEN), n);

    uint8_t out[64];
    TlsCiphertext info;
    TEST_ASSERT_TRUE(TlsRecord.unprotect(&g_recv, rec, n, out, sizeof(out), &info));
    TEST_ASSERT_EQUAL_HEX8(PC_TLS_CT_ALERT, info.content_type);
    TEST_ASSERT_EQUAL_INT((int)0, info.pt_len);
}

// The trailing content type sits after the content, so plaintext that itself ends in zero bytes
// survives the backward scan that finds the type.
void test_content_with_trailing_zeros_round_trips()
{
    uint8_t inner[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0, 0, 0, 0};
    uint8_t rec[64];
    size_t n = TlsRecord.protect(&g_send, PC_TLS_CT_APPLICATION_DATA, inner, sizeof(inner), rec, sizeof(rec));
    TEST_ASSERT_NOT_EQUAL(0, n);

    uint8_t out[64];
    TlsCiphertext info;
    TEST_ASSERT_TRUE(TlsRecord.unprotect(&g_recv, rec, n, out, sizeof(out), &info));
    TEST_ASSERT_EQUAL_HEX8(PC_TLS_CT_APPLICATION_DATA, info.content_type);
    TEST_ASSERT_EQUAL_INT((int)sizeof(inner), info.pt_len);
    TEST_ASSERT_EQUAL_MEMORY(inner, out, sizeof(inner));
}

void test_keys_wipe_disables_the_context()
{
    TlsRecord.keys_wipe(&g_send);
    const uint8_t pt[] = {0x01};
    uint8_t rec[64];
    TEST_ASSERT_EQUAL_INT((int)0, TlsRecord.protect(&g_send, PC_TLS_CT_APPLICATION_DATA, pt, sizeof(pt), rec, sizeof(rec)));
}

// ---- KAT ------------------------------------------------------------------

void test_keys_derive_kat()
{
    TlsRecordKeys k;
    memset(&k, 0, sizeof(k));
    TlsRecord.keys_derive(&k, TLS_CIPHER_AES_128_GCM_SHA256, KAT_SECRET);
    TEST_ASSERT_EQUAL_MEMORY(KAT_IV, k.iv, sizeof(KAT_IV));
    TEST_ASSERT_EQUAL_UINT64(0, k.seq);
}

void test_protect_kat()
{
    TlsRecordKeys k;
    memset(&k, 0, sizeof(k));
    TlsRecord.keys_derive(&k, TLS_CIPHER_AES_128_GCM_SHA256, KAT_SECRET);
    k.seq = KAT_SEQ;

    uint8_t out[64];
    size_t n = TlsRecord.protect(&k, KAT_CT, (const uint8_t *)KAT_PLAINTEXT, strlen(KAT_PLAINTEXT), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(sizeof(KAT_WIRE), n);
    TEST_ASSERT_EQUAL_MEMORY(KAT_WIRE, out, sizeof(KAT_WIRE));
    TEST_ASSERT_EQUAL_UINT64(KAT_SEQ + 1, k.seq);
}

void test_unprotect_kat()
{
    TlsRecordKeys k;
    memset(&k, 0, sizeof(k));
    TlsRecord.keys_derive(&k, TLS_CIPHER_AES_128_GCM_SHA256, KAT_SECRET);
    k.seq = KAT_SEQ;

    uint8_t out[64];
    TlsCiphertext info;
    TEST_ASSERT_TRUE(TlsRecord.unprotect(&k, KAT_WIRE, sizeof(KAT_WIRE), out, sizeof(out), &info));
    TEST_ASSERT_EQUAL_HEX8(KAT_CT, info.content_type);
    TEST_ASSERT_EQUAL_size_t(strlen(KAT_PLAINTEXT), info.pt_len);
    TEST_ASSERT_EQUAL_MEMORY(KAT_PLAINTEXT, out, strlen(KAT_PLAINTEXT));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_keys_derive_kat);
    RUN_TEST(test_protect_kat);
    RUN_TEST(test_unprotect_kat);
    RUN_TEST(test_plaintext_round_trips);
    RUN_TEST(test_plaintext_parse_refuses_truncated);
    RUN_TEST(test_plaintext_parse_ignores_legacy_version);
    RUN_TEST(test_plaintext_build_refuses_overflow);
    RUN_TEST(test_protect_round_trips_and_hides_the_type);
    RUN_TEST(test_sequence_advances_and_records_differ);
    RUN_TEST(test_out_of_order_record_fails);
    RUN_TEST(test_tampered_record_is_refused);
    RUN_TEST(test_short_and_malformed_records_are_refused);
    RUN_TEST(test_unkeyed_context_fails_closed);
    RUN_TEST(test_protect_refuses_overflow);
    RUN_TEST(test_empty_plaintext_carries_only_the_type);
    RUN_TEST(test_content_with_trailing_zeros_round_trips);
    RUN_TEST(test_keys_wipe_disables_the_context);
    return UNITY_END();
}
