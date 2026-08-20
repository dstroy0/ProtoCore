// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the DTLS 1.3 record layer
// (network_drivers/presentation/security/dtls/dtls_record.h).
//
// RFC 9147 sec 4 Figure 3 lays the unified header out bit by bit - "0 0 1 C S L E E" - and sec 4.2.3
// then encrypts the sequence number in place with a mask taken from the ciphertext. Those two
// together are the load-bearing pair: test_rfc9147_4_unified_header reads every one of those bits
// off a sealed record, and test_rfc9147_4_2_3_sequence_number_encryption shows the on-wire sequence
// number is not the real one and that the receiver still reconstructs it. Without both, records
// would either be rejected by a conforming peer or leak the counter a datagram protocol hides.

#include "network_drivers/presentation/security/dtls/dtls_record/dtls_record.h"
#include <string.h>

#include <unity.h>

static uint8_t dtls_record_work[16]; // the borrow an entry takes; DtlsRecord never reads it

static DtlsRecordKeys g_keys;
static uint8_t g_out[512];

// A 32-byte traffic secret. Arbitrary: every expectation below is a property of the record layout
// or a round trip, never a value copied out of some other implementation.
static const uint8_t SECRET[32] = {0xb3, 0xed, 0xdb, 0x12, 0x6e, 0x06, 0x7f, 0x35, 0xa7, 0x80, 0xb3,
                                   0xab, 0xf4, 0x5e, 0x2d, 0x8f, 0x3b, 0x1a, 0x95, 0x07, 0x38, 0xf5,
                                   0x2e, 0x96, 0x00, 0x74, 0x6a, 0x0e, 0x27, 0xa5, 0x5a, 0x21};
static const uint8_t MSG[24] = "the quick brown fox jump";

void setUp(void)
{
    memset(&g_keys, 0, sizeof(g_keys));
    memset(g_out, 0xAA, sizeof(g_out));
    DtlsRecord.keys_derive_args.out = &g_keys;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 3;
    DtlsRecord.keys_derive_args.secret = SECRET;
    DtlsRecord.keys_derive(dtls_record_work);
}
void tearDown(void)
{
}

// RFC 9147 sec 4, Figure 2: "struct { ContentType type; ProtocolVersion legacy_record_version;
// uint16 epoch = 0; uint48 sequence_number; uint16 length; opaque fragment[...]; } DTLSPlaintext",
// with legacy_record_version "{254, 253} for all records other than the initial ClientHello".
void test_rfc9147_4_plaintext_record_layout(void)
{
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0x0102;
    DtlsRecord.plaintext_build_args.seq = 0x0000AABBCCDDull;
    DtlsRecord.plaintext_build_args.fragment = MSG;
    DtlsRecord.plaintext_build_args.frag_len = sizeof(MSG);
    DtlsRecord.plaintext_build_args.out = g_out;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(g_out);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t n = DtlsRecord.n;
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_DTLS_PLAINTEXT_HDR_LEN + sizeof(MSG), n);

    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DTLS_CT_HANDSHAKE, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, g_out[1]); // {254, 253} = DTLS 1.2
    TEST_ASSERT_EQUAL_HEX8(0xFD, g_out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, g_out[3]); // epoch, big-endian
    TEST_ASSERT_EQUAL_HEX8(0x02, g_out[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[5]); // uint48 sequence_number, big-endian
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[6]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, g_out[7]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, g_out[8]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, g_out[9]);
    TEST_ASSERT_EQUAL_HEX8(0xDD, g_out[10]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[11]); // uint16 length
    TEST_ASSERT_EQUAL_HEX8(sizeof(MSG), g_out[12]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MSG, g_out + PROTOCORE_DTLS_PLAINTEXT_HDR_LEN, sizeof(MSG));

    DtlsPlaintext view;
    DtlsRecord.plaintext_parse_args.rec = g_out;
    DtlsRecord.plaintext_parse_args.rec_len = n;
    DtlsRecord.plaintext_parse_args.out = &view;
    DtlsRecord.plaintext_parse(dtls_record_work);
    TEST_ASSERT_EQUAL_UINT(n, DtlsRecord.n);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DTLS_CT_HANDSHAKE, view.content_type);
    TEST_ASSERT_EQUAL_HEX16(0x0102, view.epoch);
    TEST_ASSERT_EQUAL_UINT64(0x0000AABBCCDDull, view.seq);
    TEST_ASSERT_EQUAL_UINT(sizeof(MSG), view.frag_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MSG, view.fragment, sizeof(MSG));
}

// sec 4: legacy_record_version "MUST be ignored for all purposes", and the initial ClientHello may
// carry {254, 255}. A parser that checks it turns a legal ClientHello away.
void test_legacy_record_version_is_ignored(void)
{
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 0;
    DtlsRecord.plaintext_build_args.fragment = MSG;
    DtlsRecord.plaintext_build_args.frag_len = sizeof(MSG);
    DtlsRecord.plaintext_build_args.out = g_out;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(g_out);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t n = DtlsRecord.n;
    g_out[1] = 0xFE;
    g_out[2] = 0xFF; // {254, 255}, the compatibility spelling

    DtlsPlaintext view;
    DtlsRecord.plaintext_parse_args.rec = g_out;
    DtlsRecord.plaintext_parse_args.rec_len = n;
    DtlsRecord.plaintext_parse_args.out = &view;
    DtlsRecord.plaintext_parse(dtls_record_work);
    TEST_ASSERT_EQUAL_UINT(n, DtlsRecord.n);
    TEST_ASSERT_EQUAL_UINT(sizeof(MSG), view.frag_len);

    g_out[1] = 0x00;
    g_out[2] = 0x00; // nonsense, still ignored
    DtlsRecord.plaintext_parse_args.rec = g_out;
    DtlsRecord.plaintext_parse_args.rec_len = n;
    DtlsRecord.plaintext_parse_args.out = &view;
    DtlsRecord.plaintext_parse(dtls_record_work);
    TEST_ASSERT_EQUAL_UINT(n, DtlsRecord.n);
}

// A datagram shorter than the header, or shorter than the length field claims, is not a record.
// Bytes after the record belong to the next one in the same datagram (sec 4: "multiple ... records
// can be included in the same underlying transport datagram").
void test_plaintext_parse_bounds(void)
{
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_ACK;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 1;
    DtlsRecord.plaintext_build_args.fragment = MSG;
    DtlsRecord.plaintext_build_args.frag_len = sizeof(MSG);
    DtlsRecord.plaintext_build_args.out = g_out;
    DtlsRecord.plaintext_build_args.out_cap = sizeof(g_out);
    DtlsRecord.plaintext_build(dtls_record_work);
    size_t n = DtlsRecord.n;
    DtlsPlaintext view;

    for (size_t take = 0; take < n; take++)
    {
        DtlsRecord.plaintext_parse_args.rec = g_out;
        DtlsRecord.plaintext_parse_args.rec_len = take;
        DtlsRecord.plaintext_parse_args.out = &view;
        DtlsRecord.plaintext_parse(dtls_record_work);
        TEST_ASSERT_EQUAL_UINT(0u, DtlsRecord.n);
    }
    DtlsRecord.plaintext_parse_args.rec = g_out;
    DtlsRecord.plaintext_parse_args.rec_len = n;
    DtlsRecord.plaintext_parse_args.out = &view;
    DtlsRecord.plaintext_parse(dtls_record_work);
    TEST_ASSERT_EQUAL_UINT(n, DtlsRecord.n);
    DtlsRecord.plaintext_parse_args.rec = g_out;
    DtlsRecord.plaintext_parse_args.rec_len = n + 40;
    DtlsRecord.plaintext_parse_args.out = &view;
    DtlsRecord.plaintext_parse(dtls_record_work);
    TEST_ASSERT_EQUAL_UINT(n, DtlsRecord.n);

    // A destination too small writes nothing.
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_ACK;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 1;
    DtlsRecord.plaintext_build_args.fragment = MSG;
    DtlsRecord.plaintext_build_args.frag_len = sizeof(MSG);
    DtlsRecord.plaintext_build_args.out = g_out;
    DtlsRecord.plaintext_build_args.out_cap = n - 1;
    DtlsRecord.plaintext_build(dtls_record_work);
    TEST_ASSERT_EQUAL_UINT(0u, DtlsRecord.n);
    DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_ACK;
    DtlsRecord.plaintext_build_args.epoch = 0;
    DtlsRecord.plaintext_build_args.seq = 1;
    DtlsRecord.plaintext_build_args.fragment = MSG;
    DtlsRecord.plaintext_build_args.frag_len = sizeof(MSG);
    DtlsRecord.plaintext_build_args.out = g_out;
    DtlsRecord.plaintext_build_args.out_cap = n;
    DtlsRecord.plaintext_build(dtls_record_work);
    TEST_ASSERT_EQUAL_UINT(n, DtlsRecord.n);
}

// RFC 9147 sec 4 Figure 3: "|0|0|1|C|S|L|E E|". This build sets S (16-bit sequence number) and L
// (length present), leaves C clear when no connection id is given, and puts the low two bits of the
// epoch in E E. The header is then byte0 || seq16 || length16.
void test_rfc9147_4_unified_header(void)
{
    DtlsRecord.protect_args.keys = &g_keys;
    DtlsRecord.protect_args.seq = 0x1234;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = MSG;
    DtlsRecord.protect_args.pt_len = sizeof(MSG);
    DtlsRecord.protect_args.out = g_out;
    DtlsRecord.protect_args.out_cap = sizeof(g_out);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t n = DtlsRecord.n;
    const size_t hdr_len = 1 + 2 + 2;
    const size_t enc_len = sizeof(MSG) + 1 + PROTOCORE_DTLS_TAG_LEN;
    TEST_ASSERT_EQUAL_UINT(hdr_len + enc_len, n);

    TEST_ASSERT_EQUAL_HEX8(0x20, g_out[0] & 0xE0); // fixed bits 001
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[0] & 0x10); // C clear: no connection id
    TEST_ASSERT_EQUAL_HEX8(0x08, g_out[0] & 0x08); // S set: 16-bit sequence number
    TEST_ASSERT_EQUAL_HEX8(0x04, g_out[0] & 0x04); // L set: length present
    TEST_ASSERT_EQUAL_HEX8(0x03, g_out[0] & 0x03); // E E = epoch 3 & 3

    // "Length: Identical to the length field in a TLS 1.3 record" - the encrypted_record length.
    TEST_ASSERT_EQUAL_UINT(enc_len, ((size_t)g_out[3] << 8) | g_out[4]);

    // An epoch whose low two bits are 0 spells E E = 00.
    DtlsRecordKeys e4;
    memset(&e4, 0, sizeof(e4));
    DtlsRecord.keys_derive_args.out = &e4;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 4;
    DtlsRecord.keys_derive_args.secret = SECRET;
    DtlsRecord.keys_derive(dtls_record_work);
    DtlsRecord.protect_args.keys = &e4;
    DtlsRecord.protect_args.seq = 1;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = MSG;
    DtlsRecord.protect_args.pt_len = sizeof(MSG);
    DtlsRecord.protect_args.out = g_out;
    DtlsRecord.protect_args.out_cap = sizeof(g_out);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.n > 0);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[0] & 0x03);
}

// RFC 9147 sec 4.2.3: "record sequence numbers are also encrypted ... Mask = AES-ECB(sn_key,
// Ciphertext[0..15])", XORed with the on-the-wire sequence number. So the two header octets are not
// the counter, and the receiver recovers it by running the same mask.
void test_rfc9147_4_2_3_sequence_number_encryption(void)
{
    const uint64_t SEQ = 0x1234;
    DtlsRecord.protect_args.keys = &g_keys;
    DtlsRecord.protect_args.seq = SEQ;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = MSG;
    DtlsRecord.protect_args.pt_len = sizeof(MSG);
    DtlsRecord.protect_args.out = g_out;
    DtlsRecord.protect_args.out_cap = sizeof(g_out);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t n = DtlsRecord.n;
    TEST_ASSERT_TRUE(n > 0);

    // The header must NOT carry 0x12 0x34: that is the whole point of sec 4.2.3.
    TEST_ASSERT_FALSE(g_out[1] == 0x12 && g_out[2] == 0x34);

    uint8_t pt[64];
    DtlsCiphertext info;
    DtlsRecord.unprotect_args.keys = &g_keys;
    DtlsRecord.unprotect_args.next_seq = SEQ;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = NULL;
    DtlsRecord.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    TEST_ASSERT_EQUAL_UINT64(SEQ, info.seq);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DTLS_CT_APPLICATION_DATA, info.content_type);
    TEST_ASSERT_EQUAL_UINT(sizeof(MSG), info.pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MSG, pt, sizeof(MSG));
    TEST_ASSERT_EQUAL_HEX16(3, info.epoch);

    // The mask depends on the ciphertext, so two records with the same sequence number but different
    // content do not share the encrypted sequence-number octets.
    uint8_t other[512];
    static const uint8_t MSG2[24] = "THE QUICK BROWN FOX JUMP";
    DtlsRecord.protect_args.keys = &g_keys;
    DtlsRecord.protect_args.seq = SEQ;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = MSG2;
    DtlsRecord.protect_args.pt_len = sizeof(MSG2);
    DtlsRecord.protect_args.out = other;
    DtlsRecord.protect_args.out_cap = sizeof(other);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t m = DtlsRecord.n;
    TEST_ASSERT_EQUAL_UINT(n, m);
    TEST_ASSERT_FALSE(other[1] == g_out[1] && other[2] == g_out[2]);
}

// sec 4.2.2: "implementations SHOULD reconstruct the sequence number by computing the full sequence
// number which is numerically closest to one plus the sequence number of the highest successfully
// deprotected record". Only 16 bits travel, so a counter past 65535 has to be rebuilt from the
// receiver's expectation.
void test_rfc9147_4_2_2_sequence_reconstruction(void)
{
    static const uint64_t SEQS[] = {0, 1, 0xFFFF, 0x10000, 0x10001, 0x1FFFF, 0x123456, 0xFFFFFFFFull};
    uint8_t pt[64];
    DtlsCiphertext info;

    for (size_t i = 0; i < sizeof(SEQS) / sizeof(SEQS[0]); i++)
    {
        DtlsRecord.protect_args.keys = &g_keys;
        DtlsRecord.protect_args.seq = SEQS[i];
        DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
        DtlsRecord.protect_args.plaintext = MSG;
        DtlsRecord.protect_args.pt_len = sizeof(MSG);
        DtlsRecord.protect_args.out = g_out;
        DtlsRecord.protect_args.out_cap = sizeof(g_out);
        DtlsRecord.protect_args.cid = NULL;
        DtlsRecord.protect_args.cid_len = 0;
        DtlsRecord.protect(dtls_record_work);
        size_t n = DtlsRecord.n;
        TEST_ASSERT_TRUE(n > 0);
        // The receiver expects the next record after the previous one.
        DtlsRecord.unprotect_args.keys = &g_keys;
        DtlsRecord.unprotect_args.next_seq = SEQS[i];
        DtlsRecord.unprotect_args.rec = g_out;
        DtlsRecord.unprotect_args.rec_len = n;
        DtlsRecord.unprotect_args.out = pt;
        DtlsRecord.unprotect_args.out_cap = sizeof(pt);
        DtlsRecord.unprotect_args.info = &info;
        DtlsRecord.unprotect_args.expected_cid = NULL;
        DtlsRecord.unprotect_args.expected_cid_len = 0;
        DtlsRecord.unprotect(dtls_record_work);
        TEST_ASSERT_TRUE(DtlsRecord.ok);
        TEST_ASSERT_EQUAL_UINT64(SEQS[i], info.seq);
    }

    // Reordering inside the 16-bit window still resolves: a record 100 behind the expectation.
    DtlsRecord.protect_args.keys = &g_keys;
    DtlsRecord.protect_args.seq = 0x10000 - 100;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = MSG;
    DtlsRecord.protect_args.pt_len = sizeof(MSG);
    DtlsRecord.protect_args.out = g_out;
    DtlsRecord.protect_args.out_cap = sizeof(g_out);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t n = DtlsRecord.n;
    DtlsRecord.unprotect_args.keys = &g_keys;
    DtlsRecord.unprotect_args.next_seq = 0x10000;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = NULL;
    DtlsRecord.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    TEST_ASSERT_EQUAL_UINT64(0x10000ull - 100, info.seq);
}

// The nonce is built over the full sequence number (sec 4.2.2), so a receiver that reconstructs a
// different one cannot open the record: the counter is authenticated, not merely carried.
void test_a_wrong_sequence_number_fails_deprotection(void)
{
    uint8_t pt[64];
    DtlsCiphertext info;
    DtlsRecord.protect_args.keys = &g_keys;
    DtlsRecord.protect_args.seq = 0x10000;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = MSG;
    DtlsRecord.protect_args.pt_len = sizeof(MSG);
    DtlsRecord.protect_args.out = g_out;
    DtlsRecord.protect_args.out_cap = sizeof(g_out);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t n = DtlsRecord.n;
    TEST_ASSERT_TRUE(n > 0);

    // An expectation a whole 16-bit window away resolves to a different full number and fails.
    DtlsRecord.unprotect_args.keys = &g_keys;
    DtlsRecord.unprotect_args.next_seq = 0x30000;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = NULL;
    DtlsRecord.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);
    // The right expectation still opens it.
    DtlsRecord.unprotect_args.keys = &g_keys;
    DtlsRecord.unprotect_args.next_seq = 0x10000;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = NULL;
    DtlsRecord.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
}

// sec 4 Figure 3: "E: The two low bits (0x03) include the low-order two bits of the epoch." A
// record must be opened with the epoch keys whose low bits match, and only those.
void test_epoch_bits_select_the_keys(void)
{
    uint8_t pt[64];
    DtlsCiphertext info;
    DtlsRecord.protect_args.keys = &g_keys;
    DtlsRecord.protect_args.seq = 7;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = MSG;
    DtlsRecord.protect_args.pt_len = sizeof(MSG);
    DtlsRecord.protect_args.out = g_out;
    DtlsRecord.protect_args.out_cap = sizeof(g_out);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t n = DtlsRecord.n;

    DtlsRecordKeys other;
    memset(&other, 0, sizeof(other));
    DtlsRecord.keys_derive_args.out = &other;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 2;
    DtlsRecord.keys_derive_args.secret = SECRET;
    DtlsRecord.keys_derive(dtls_record_work); // low bits 10, not 11
    DtlsRecord.unprotect_args.keys = &other;
    DtlsRecord.unprotect_args.next_seq = 7;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = NULL;
    DtlsRecord.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);

    // Epoch 7 shares the low two bits with epoch 3, so the header accepts it - and then the AEAD
    // rejects it, because the keys were expanded from a different epoch's secret.
    DtlsRecordKeys e7;
    memset(&e7, 0, sizeof(e7));
    static const uint8_t OTHER_SECRET[32] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                             0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                             0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};
    DtlsRecord.keys_derive_args.out = &e7;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 7;
    DtlsRecord.keys_derive_args.secret = OTHER_SECRET;
    DtlsRecord.keys_derive(dtls_record_work);
    DtlsRecord.unprotect_args.keys = &e7;
    DtlsRecord.unprotect_args.next_seq = 7;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = NULL;
    DtlsRecord.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);

    DtlsRecord.unprotect_args.keys = &g_keys;
    DtlsRecord.unprotect_args.next_seq = 7;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = NULL;
    DtlsRecord.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
}

// sec 4 Figure 3 and RFC 9146: the C bit says a connection id follows the first byte. The CID is not
// length-prefixed on the wire, so both ends must agree on its length from the handshake, and it is
// covered by the AEAD's associated data.
void test_rfc9146_connection_id(void)
{
    static const uint8_t CID[4] = {0xde, 0xad, 0xbe, 0xef};
    static const uint8_t WRONG_CID[4] = {0xde, 0xad, 0xbe, 0xee};
    uint8_t pt[64];
    DtlsCiphertext info;

    DtlsRecord.protect_args.keys = &g_keys;
    DtlsRecord.protect_args.seq = 5;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = MSG;
    DtlsRecord.protect_args.pt_len = sizeof(MSG);
    DtlsRecord.protect_args.out = g_out;
    DtlsRecord.protect_args.out_cap = sizeof(g_out);
    DtlsRecord.protect_args.cid = CID;
    DtlsRecord.protect_args.cid_len = sizeof(CID);
    DtlsRecord.protect(dtls_record_work);
    size_t n = DtlsRecord.n;
    TEST_ASSERT_EQUAL_UINT(1 + sizeof(CID) + 2 + 2 + sizeof(MSG) + 1 + PROTOCORE_DTLS_TAG_LEN, n);
    TEST_ASSERT_EQUAL_HEX8(0x10, g_out[0] & 0x10); // C set
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CID, g_out + 1, sizeof(CID));

    DtlsRecord.unprotect_args.keys = &g_keys;
    DtlsRecord.unprotect_args.next_seq = 5;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = CID;
    DtlsRecord.unprotect_args.expected_cid_len = sizeof(CID);
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(MSG), info.pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MSG, pt, sizeof(MSG));

    // Another endpoint's CID is not ours.
    DtlsRecord.unprotect_args.keys = &g_keys;
    DtlsRecord.unprotect_args.next_seq = 5;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = WRONG_CID;
    DtlsRecord.unprotect_args.expected_cid_len = sizeof(WRONG_CID);
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);
    // A receiver expecting no CID must refuse a record that carries one, and vice versa.
    DtlsRecord.unprotect_args.keys = &g_keys;
    DtlsRecord.unprotect_args.next_seq = 5;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = NULL;
    DtlsRecord.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);

    DtlsRecord.protect_args.keys = &g_keys;
    DtlsRecord.protect_args.seq = 5;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = MSG;
    DtlsRecord.protect_args.pt_len = sizeof(MSG);
    DtlsRecord.protect_args.out = g_out;
    DtlsRecord.protect_args.out_cap = sizeof(g_out);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t m = DtlsRecord.n;
    DtlsRecord.unprotect_args.keys = &g_keys;
    DtlsRecord.unprotect_args.next_seq = 5;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = m;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = CID;
    DtlsRecord.unprotect_args.expected_cid_len = sizeof(CID);
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);

    // A connection id wider than the header scratch is refused rather than truncated.
    uint8_t big[PROTOCORE_DTLS_CID_MAX + 1];
    memset(big, 0x11, sizeof(big));
    DtlsRecord.protect_args.keys = &g_keys;
    DtlsRecord.protect_args.seq = 5;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = MSG;
    DtlsRecord.protect_args.pt_len = sizeof(MSG);
    DtlsRecord.protect_args.out = g_out;
    DtlsRecord.protect_args.out_cap = sizeof(g_out);
    DtlsRecord.protect_args.cid = big;
    DtlsRecord.protect_args.cid_len = sizeof(big);
    DtlsRecord.protect(dtls_record_work);
    TEST_ASSERT_EQUAL_UINT(0u, DtlsRecord.n);
    DtlsRecord.unprotect_args.keys = &g_keys;
    DtlsRecord.unprotect_args.next_seq = 5;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = m;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = big;
    DtlsRecord.unprotect_args.expected_cid_len = sizeof(big);
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);
}

// RFC 8446 sec 5.2, which sec 4 of RFC 9147 inherits: the true content type is the last non-zero
// octet of the inner plaintext, and the header's own type field is gone entirely.
void test_inner_content_type_and_padding(void)
{
    uint8_t pt[64];
    DtlsCiphertext info;

    static const uint8_t TYPES[] = {PROTOCORE_DTLS_CT_CHANGE_CIPHER_SPEC, PROTOCORE_DTLS_CT_ALERT,
                                    PROTOCORE_DTLS_CT_HANDSHAKE, PROTOCORE_DTLS_CT_APPLICATION_DATA,
                                    PROTOCORE_DTLS_CT_ACK};
    for (size_t i = 0; i < sizeof(TYPES) / sizeof(TYPES[0]); i++)
    {
        DtlsRecord.protect_args.keys = &g_keys;
        DtlsRecord.protect_args.seq = i;
        DtlsRecord.protect_args.content_type = TYPES[i];
        DtlsRecord.protect_args.plaintext = MSG;
        DtlsRecord.protect_args.pt_len = sizeof(MSG);
        DtlsRecord.protect_args.out = g_out;
        DtlsRecord.protect_args.out_cap = sizeof(g_out);
        DtlsRecord.protect_args.cid = NULL;
        DtlsRecord.protect_args.cid_len = 0;
        DtlsRecord.protect(dtls_record_work);
        size_t n = DtlsRecord.n;
        TEST_ASSERT_TRUE(n > 0);
        DtlsRecord.unprotect_args.keys = &g_keys;
        DtlsRecord.unprotect_args.next_seq = i;
        DtlsRecord.unprotect_args.rec = g_out;
        DtlsRecord.unprotect_args.rec_len = n;
        DtlsRecord.unprotect_args.out = pt;
        DtlsRecord.unprotect_args.out_cap = sizeof(pt);
        DtlsRecord.unprotect_args.info = &info;
        DtlsRecord.unprotect_args.expected_cid = NULL;
        DtlsRecord.unprotect_args.expected_cid_len = 0;
        DtlsRecord.unprotect(dtls_record_work);
        TEST_ASSERT_TRUE(DtlsRecord.ok);
        TEST_ASSERT_EQUAL_HEX8(TYPES[i], info.content_type);
        TEST_ASSERT_EQUAL_UINT(sizeof(MSG), info.pt_len);
    }

    // Sealing "abc" || 0x17 under an outer type of 0x00 lays down the same inner plaintext a peer
    // that padded by one octet would send: content, content type, then a zero.
    static const uint8_t INNER[4] = {'a', 'b', 'c', PROTOCORE_DTLS_CT_APPLICATION_DATA};
    DtlsRecord.protect_args.keys = &g_keys;
    DtlsRecord.protect_args.seq = 9;
    DtlsRecord.protect_args.content_type = 0x00;
    DtlsRecord.protect_args.plaintext = INNER;
    DtlsRecord.protect_args.pt_len = sizeof(INNER);
    DtlsRecord.protect_args.out = g_out;
    DtlsRecord.protect_args.out_cap = sizeof(g_out);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t n = DtlsRecord.n;
    TEST_ASSERT_TRUE(n > 0);
    DtlsRecord.unprotect_args.keys = &g_keys;
    DtlsRecord.unprotect_args.next_seq = 9;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = NULL;
    DtlsRecord.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DTLS_CT_APPLICATION_DATA, info.content_type);
    TEST_ASSERT_EQUAL_UINT(3u, info.pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("abc", pt, 3);

    // An inner plaintext that is all zeros names no content type: an invalid record (sec 4.5.2).
    static const uint8_t ZERO = 0x00;
    DtlsRecord.protect_args.keys = &g_keys;
    DtlsRecord.protect_args.seq = 10;
    DtlsRecord.protect_args.content_type = 0x00;
    DtlsRecord.protect_args.plaintext = &ZERO;
    DtlsRecord.protect_args.pt_len = 1;
    DtlsRecord.protect_args.out = g_out;
    DtlsRecord.protect_args.out_cap = sizeof(g_out);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    n = DtlsRecord.n;
    TEST_ASSERT_TRUE(n > 0);
    DtlsRecord.unprotect_args.keys = &g_keys;
    DtlsRecord.unprotect_args.next_seq = 10;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = sizeof(pt);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = NULL;
    DtlsRecord.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);
}

// sec 4.5.2 with sec 4.2.3: a record whose fixed bits are wrong, that is too short to carry the
// 16-byte sequence-number sample, or whose body has been altered, is discarded as if deprotection
// had failed.
void test_rfc9147_4_5_2_invalid_records(void)
{
    uint8_t pt[64];
    DtlsCiphertext info;
    DtlsRecord.protect_args.keys = &g_keys;
    DtlsRecord.protect_args.seq = 11;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = MSG;
    DtlsRecord.protect_args.pt_len = sizeof(MSG);
    DtlsRecord.protect_args.out = g_out;
    DtlsRecord.protect_args.out_cap = sizeof(g_out);
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    size_t n = DtlsRecord.n;
    uint8_t rec[128];
    memcpy(rec, g_out, n);

    // "The three high bits of the first byte of the unified header are set to 001."
    for (uint8_t top = 0; top < 8; top++)
    {
        memcpy(rec, g_out, n);
        rec[0] = (uint8_t)((rec[0] & 0x1F) | (uint8_t)(top << 5));
        DtlsRecord.unprotect_args.keys = &g_keys;
        DtlsRecord.unprotect_args.next_seq = 11;
        DtlsRecord.unprotect_args.rec = rec;
        DtlsRecord.unprotect_args.rec_len = n;
        DtlsRecord.unprotect_args.out = pt;
        DtlsRecord.unprotect_args.out_cap = sizeof(pt);
        DtlsRecord.unprotect_args.info = &info;
        DtlsRecord.unprotect_args.expected_cid = NULL;
        DtlsRecord.unprotect_args.expected_cid_len = 0;
        DtlsRecord.unprotect(dtls_record_work);
        proto_bool ok = DtlsRecord.ok;
        if (top == 1)
        {
            TEST_ASSERT_TRUE(ok); // 001 is the only accepted pattern
        }
        else
        {
            TEST_ASSERT_FALSE(ok);
        }
    }

    // Every octet of the record is covered: the header is the AEAD's associated data and the body
    // carries the tag, so a single flipped bit anywhere is discarded.
    for (size_t i = 0; i < n; i++)
    {
        memcpy(rec, g_out, n);
        rec[i] ^= 0x01;
        DtlsRecord.unprotect_args.keys = &g_keys;
        DtlsRecord.unprotect_args.next_seq = 11;
        DtlsRecord.unprotect_args.rec = rec;
        DtlsRecord.unprotect_args.rec_len = n;
        DtlsRecord.unprotect_args.out = pt;
        DtlsRecord.unprotect_args.out_cap = sizeof(pt);
        DtlsRecord.unprotect_args.info = &info;
        DtlsRecord.unprotect_args.expected_cid = NULL;
        DtlsRecord.unprotect_args.expected_cid_len = 0;
        DtlsRecord.unprotect(dtls_record_work);
        TEST_ASSERT_FALSE_MESSAGE(DtlsRecord.ok, "a flipped bit must not deprotect");
    }

    // "This procedure requires the ciphertext length to be at least 16 bytes. Receivers MUST reject
    // shorter records": every truncation of this record is short of its own declared length.
    for (size_t take = 0; take < n; take++)
    {
        DtlsRecord.unprotect_args.keys = &g_keys;
        DtlsRecord.unprotect_args.next_seq = 11;
        DtlsRecord.unprotect_args.rec = g_out;
        DtlsRecord.unprotect_args.rec_len = take;
        DtlsRecord.unprotect_args.out = pt;
        DtlsRecord.unprotect_args.out_cap = sizeof(pt);
        DtlsRecord.unprotect_args.info = &info;
        DtlsRecord.unprotect_args.expected_cid = NULL;
        DtlsRecord.unprotect_args.expected_cid_len = 0;
        DtlsRecord.unprotect(dtls_record_work);
        TEST_ASSERT_FALSE(DtlsRecord.ok);
    }

    // A destination too small for the recovered plaintext, and one too small for the record.
    DtlsRecord.unprotect_args.keys = &g_keys;
    DtlsRecord.unprotect_args.next_seq = 11;
    DtlsRecord.unprotect_args.rec = g_out;
    DtlsRecord.unprotect_args.rec_len = n;
    DtlsRecord.unprotect_args.out = pt;
    DtlsRecord.unprotect_args.out_cap = 4;
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = NULL;
    DtlsRecord.unprotect_args.expected_cid_len = 0;
    DtlsRecord.unprotect(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);
    DtlsRecord.protect_args.keys = &g_keys;
    DtlsRecord.protect_args.seq = 11;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = MSG;
    DtlsRecord.protect_args.pt_len = sizeof(MSG);
    DtlsRecord.protect_args.out = g_out;
    DtlsRecord.protect_args.out_cap = n - 1;
    DtlsRecord.protect_args.cid = NULL;
    DtlsRecord.protect_args.cid_len = 0;
    DtlsRecord.protect(dtls_record_work);
    TEST_ASSERT_EQUAL_UINT(0u, DtlsRecord.n);
}

// RFC 9147 sec 4.5.1: "the receiver MUST verify that the record contains a sequence number that does
// not duplicate the sequence number of any other record received in that epoch", using a sliding
// window whose right edge is the highest accepted number and whose left edge rejects everything
// older. "The window MUST NOT be updated ... until that record has been deprotected successfully."
void test_rfc9147_4_5_1_replay_window(void)
{
    DtlsReplayWindow w;
    DtlsRecord.replay_init_args.w = &w;
    DtlsRecord.replay_init(dtls_record_work);

    // "The received record counter for an epoch MUST be initialized to zero when that epoch is first
    // used": before any record, every sequence number is new, including zero.
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 0;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 1000;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    DtlsRecord.replay_mark_args.w = &w;
    DtlsRecord.replay_mark_args.seq = 0;
    DtlsRecord.replay_mark(dtls_record_work);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 0;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok); // now a duplicate

    // Ahead of the right edge is new; a gap is allowed and the skipped numbers stay acceptable.
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 5;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    DtlsRecord.replay_mark_args.w = &w;
    DtlsRecord.replay_mark_args.seq = 5;
    DtlsRecord.replay_mark(dtls_record_work);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 5;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 3;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok); // reordered, inside the window, not yet seen
    DtlsRecord.replay_mark_args.w = &w;
    DtlsRecord.replay_mark_args.seq = 3;
    DtlsRecord.replay_mark(dtls_record_work);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 3;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 0;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 4;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);

    // "Records that contain sequence numbers lower than the left edge of the window are rejected."
    // The window is 64 wide, so with the right edge at 5 + 64 = 69, 5 falls out and 6 is the oldest
    // still inside.
    DtlsRecord.replay_mark_args.w = &w;
    DtlsRecord.replay_mark_args.seq = 69;
    DtlsRecord.replay_mark(dtls_record_work);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 5;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok); // 69 - 5 = 64: outside
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 6;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok); // 69 - 6 = 63: the oldest bit
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 69;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);

    // A jump of more than the window width clears everything behind it.
    DtlsRecord.replay_mark_args.w = &w;
    DtlsRecord.replay_mark_args.seq = 1000;
    DtlsRecord.replay_mark(dtls_record_work);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 1000;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 6;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 936;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_FALSE(DtlsRecord.ok); // 1000 - 936 = 64: outside
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 937;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 1001;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);

    // "Because each epoch resets the sequence number space, a separate sliding window is needed for
    // each epoch": a fresh window shares nothing with the old one.
    DtlsRecord.replay_init_args.w = &w;
    DtlsRecord.replay_init(dtls_record_work);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 0;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
    DtlsRecord.replay_check_args.w = &w;
    DtlsRecord.replay_check_args.seq = 1000;
    DtlsRecord.replay_check(dtls_record_work);
    TEST_ASSERT_TRUE(DtlsRecord.ok);
}

// Protect then unprotect returns the plaintext unchanged for every length from empty to the
// destination's limit, including the one-octet inner plaintext the tag length makes the minimum.
void test_round_trip_over_lengths(void)
{
    uint8_t src[200];
    uint8_t pt[256];
    DtlsCiphertext info;
    for (size_t i = 0; i < sizeof(src); i++)
    {
        src[i] = (uint8_t)(i * 7 + 1);
    }

    for (size_t len = 0; len <= sizeof(src); len += 13)
    {
        DtlsRecord.protect_args.keys = &g_keys;
        DtlsRecord.protect_args.seq = len;
        DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
        DtlsRecord.protect_args.plaintext = src;
        DtlsRecord.protect_args.pt_len = len;
        DtlsRecord.protect_args.out = g_out;
        DtlsRecord.protect_args.out_cap = sizeof(g_out);
        DtlsRecord.protect_args.cid = NULL;
        DtlsRecord.protect_args.cid_len = 0;
        DtlsRecord.protect(dtls_record_work);
        size_t n = DtlsRecord.n;
        TEST_ASSERT_TRUE(n > 0);
        DtlsRecord.unprotect_args.keys = &g_keys;
        DtlsRecord.unprotect_args.next_seq = len;
        DtlsRecord.unprotect_args.rec = g_out;
        DtlsRecord.unprotect_args.rec_len = n;
        DtlsRecord.unprotect_args.out = pt;
        DtlsRecord.unprotect_args.out_cap = sizeof(pt);
        DtlsRecord.unprotect_args.info = &info;
        DtlsRecord.unprotect_args.expected_cid = NULL;
        DtlsRecord.unprotect_args.expected_cid_len = 0;
        DtlsRecord.unprotect(dtls_record_work);
        TEST_ASSERT_TRUE(DtlsRecord.ok);
        TEST_ASSERT_EQUAL_UINT(len, info.pt_len);
        TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DTLS_CT_APPLICATION_DATA, info.content_type);
        if (len)
        {
            TEST_ASSERT_EQUAL_UINT8_ARRAY(src, pt, len);
        }
    }
}
