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

#include "network_drivers/presentation/security/dtls/dtls_record.h"
#include <string.h>

#include <unity.h>

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
    DtlsRecord.keys_derive(&g_keys, DTLS_CIPHER_AES_128_GCM_SHA256, 3, SECRET);
}
void tearDown(void)
{
}

// RFC 9147 sec 4, Figure 2: "struct { ContentType type; ProtocolVersion legacy_record_version;
// uint16 epoch = 0; uint48 sequence_number; uint16 length; opaque fragment[...]; } DTLSPlaintext",
// with legacy_record_version "{254, 253} for all records other than the initial ClientHello".
void test_rfc9147_4_plaintext_record_layout(void)
{
    size_t n = DtlsRecord.plaintext_build(PROTOCORE_DTLS_CT_HANDSHAKE, 0x0102, 0x0000AABBCCDDull, MSG, sizeof(MSG),
                                          g_out, sizeof(g_out));
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
    TEST_ASSERT_EQUAL_UINT(n, DtlsRecord.plaintext_parse(g_out, n, &view));
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
    size_t n = DtlsRecord.plaintext_build(PROTOCORE_DTLS_CT_HANDSHAKE, 0, 0, MSG, sizeof(MSG), g_out, sizeof(g_out));
    g_out[1] = 0xFE;
    g_out[2] = 0xFF; // {254, 255}, the compatibility spelling

    DtlsPlaintext view;
    TEST_ASSERT_EQUAL_UINT(n, DtlsRecord.plaintext_parse(g_out, n, &view));
    TEST_ASSERT_EQUAL_UINT(sizeof(MSG), view.frag_len);

    g_out[1] = 0x00;
    g_out[2] = 0x00; // nonsense, still ignored
    TEST_ASSERT_EQUAL_UINT(n, DtlsRecord.plaintext_parse(g_out, n, &view));
}

// A datagram shorter than the header, or shorter than the length field claims, is not a record.
// Bytes after the record belong to the next one in the same datagram (sec 4: "multiple ... records
// can be included in the same underlying transport datagram").
void test_plaintext_parse_bounds(void)
{
    size_t n = DtlsRecord.plaintext_build(PROTOCORE_DTLS_CT_ACK, 0, 1, MSG, sizeof(MSG), g_out, sizeof(g_out));
    DtlsPlaintext view;

    for (size_t take = 0; take < n; take++)
    {
        TEST_ASSERT_EQUAL_UINT(0u, DtlsRecord.plaintext_parse(g_out, take, &view));
    }
    TEST_ASSERT_EQUAL_UINT(n, DtlsRecord.plaintext_parse(g_out, n, &view));
    TEST_ASSERT_EQUAL_UINT(n, DtlsRecord.plaintext_parse(g_out, n + 40, &view));

    // A destination too small writes nothing.
    TEST_ASSERT_EQUAL_UINT(0u, DtlsRecord.plaintext_build(PROTOCORE_DTLS_CT_ACK, 0, 1, MSG, sizeof(MSG), g_out, n - 1));
    TEST_ASSERT_EQUAL_UINT(n, DtlsRecord.plaintext_build(PROTOCORE_DTLS_CT_ACK, 0, 1, MSG, sizeof(MSG), g_out, n));
}

// RFC 9147 sec 4 Figure 3: "|0|0|1|C|S|L|E E|". This build sets S (16-bit sequence number) and L
// (length present), leaves C clear when no connection id is given, and puts the low two bits of the
// epoch in E E. The header is then byte0 || seq16 || length16.
void test_rfc9147_4_unified_header(void)
{
    size_t n = DtlsRecord.protect(&g_keys, 0x1234, PROTOCORE_DTLS_CT_APPLICATION_DATA, MSG, sizeof(MSG), g_out,
                                  sizeof(g_out), NULL, 0);
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
    DtlsRecord.keys_derive(&e4, DTLS_CIPHER_AES_128_GCM_SHA256, 4, SECRET);
    TEST_ASSERT_TRUE(DtlsRecord.protect(&e4, 1, PROTOCORE_DTLS_CT_APPLICATION_DATA, MSG, sizeof(MSG), g_out,
                                        sizeof(g_out), NULL, 0) > 0);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[0] & 0x03);
}

// RFC 9147 sec 4.2.3: "record sequence numbers are also encrypted ... Mask = AES-ECB(sn_key,
// Ciphertext[0..15])", XORed with the on-the-wire sequence number. So the two header octets are not
// the counter, and the receiver recovers it by running the same mask.
void test_rfc9147_4_2_3_sequence_number_encryption(void)
{
    const uint64_t SEQ = 0x1234;
    size_t n = DtlsRecord.protect(&g_keys, SEQ, PROTOCORE_DTLS_CT_APPLICATION_DATA, MSG, sizeof(MSG), g_out,
                                  sizeof(g_out), NULL, 0);
    TEST_ASSERT_TRUE(n > 0);

    // The header must NOT carry 0x12 0x34: that is the whole point of sec 4.2.3.
    TEST_ASSERT_FALSE(g_out[1] == 0x12 && g_out[2] == 0x34);

    uint8_t pt[64];
    DtlsCiphertext info;
    TEST_ASSERT_TRUE(DtlsRecord.unprotect(&g_keys, SEQ, g_out, n, pt, sizeof(pt), &info, NULL, 0));
    TEST_ASSERT_EQUAL_UINT64(SEQ, info.seq);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DTLS_CT_APPLICATION_DATA, info.content_type);
    TEST_ASSERT_EQUAL_UINT(sizeof(MSG), info.pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MSG, pt, sizeof(MSG));
    TEST_ASSERT_EQUAL_HEX16(3, info.epoch);

    // The mask depends on the ciphertext, so two records with the same sequence number but different
    // content do not share the encrypted sequence-number octets.
    uint8_t other[512];
    static const uint8_t MSG2[24] = "THE QUICK BROWN FOX JUMP";
    size_t m = DtlsRecord.protect(&g_keys, SEQ, PROTOCORE_DTLS_CT_APPLICATION_DATA, MSG2, sizeof(MSG2), other,
                                  sizeof(other), NULL, 0);
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
        size_t n = DtlsRecord.protect(&g_keys, SEQS[i], PROTOCORE_DTLS_CT_APPLICATION_DATA, MSG, sizeof(MSG), g_out,
                                      sizeof(g_out), NULL, 0);
        TEST_ASSERT_TRUE(n > 0);
        // The receiver expects the next record after the previous one.
        TEST_ASSERT_TRUE(DtlsRecord.unprotect(&g_keys, SEQS[i], g_out, n, pt, sizeof(pt), &info, NULL, 0));
        TEST_ASSERT_EQUAL_UINT64(SEQS[i], info.seq);
    }

    // Reordering inside the 16-bit window still resolves: a record 100 behind the expectation.
    size_t n = DtlsRecord.protect(&g_keys, 0x10000 - 100, PROTOCORE_DTLS_CT_APPLICATION_DATA, MSG, sizeof(MSG), g_out,
                                  sizeof(g_out), NULL, 0);
    TEST_ASSERT_TRUE(DtlsRecord.unprotect(&g_keys, 0x10000, g_out, n, pt, sizeof(pt), &info, NULL, 0));
    TEST_ASSERT_EQUAL_UINT64(0x10000ull - 100, info.seq);
}

// The nonce is built over the full sequence number (sec 4.2.2), so a receiver that reconstructs a
// different one cannot open the record: the counter is authenticated, not merely carried.
void test_a_wrong_sequence_number_fails_deprotection(void)
{
    uint8_t pt[64];
    DtlsCiphertext info;
    size_t n = DtlsRecord.protect(&g_keys, 0x10000, PROTOCORE_DTLS_CT_APPLICATION_DATA, MSG, sizeof(MSG), g_out,
                                  sizeof(g_out), NULL, 0);
    TEST_ASSERT_TRUE(n > 0);

    // An expectation a whole 16-bit window away resolves to a different full number and fails.
    TEST_ASSERT_FALSE(DtlsRecord.unprotect(&g_keys, 0x30000, g_out, n, pt, sizeof(pt), &info, NULL, 0));
    // The right expectation still opens it.
    TEST_ASSERT_TRUE(DtlsRecord.unprotect(&g_keys, 0x10000, g_out, n, pt, sizeof(pt), &info, NULL, 0));
}

// sec 4 Figure 3: "E: The two low bits (0x03) include the low-order two bits of the epoch." A
// record must be opened with the epoch keys whose low bits match, and only those.
void test_epoch_bits_select_the_keys(void)
{
    uint8_t pt[64];
    DtlsCiphertext info;
    size_t n = DtlsRecord.protect(&g_keys, 7, PROTOCORE_DTLS_CT_APPLICATION_DATA, MSG, sizeof(MSG), g_out,
                                  sizeof(g_out), NULL, 0);

    DtlsRecordKeys other;
    memset(&other, 0, sizeof(other));
    DtlsRecord.keys_derive(&other, DTLS_CIPHER_AES_128_GCM_SHA256, 2, SECRET); // low bits 10, not 11
    TEST_ASSERT_FALSE(DtlsRecord.unprotect(&other, 7, g_out, n, pt, sizeof(pt), &info, NULL, 0));

    // Epoch 7 shares the low two bits with epoch 3, so the header accepts it - and then the AEAD
    // rejects it, because the keys were expanded from a different epoch's secret.
    DtlsRecordKeys e7;
    memset(&e7, 0, sizeof(e7));
    static const uint8_t OTHER_SECRET[32] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
                                             0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                             0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00};
    DtlsRecord.keys_derive(&e7, DTLS_CIPHER_AES_128_GCM_SHA256, 7, OTHER_SECRET);
    TEST_ASSERT_FALSE(DtlsRecord.unprotect(&e7, 7, g_out, n, pt, sizeof(pt), &info, NULL, 0));

    TEST_ASSERT_TRUE(DtlsRecord.unprotect(&g_keys, 7, g_out, n, pt, sizeof(pt), &info, NULL, 0));
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

    size_t n = DtlsRecord.protect(&g_keys, 5, PROTOCORE_DTLS_CT_APPLICATION_DATA, MSG, sizeof(MSG), g_out,
                                  sizeof(g_out), CID, sizeof(CID));
    TEST_ASSERT_EQUAL_UINT(1 + sizeof(CID) + 2 + 2 + sizeof(MSG) + 1 + PROTOCORE_DTLS_TAG_LEN, n);
    TEST_ASSERT_EQUAL_HEX8(0x10, g_out[0] & 0x10); // C set
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CID, g_out + 1, sizeof(CID));

    TEST_ASSERT_TRUE(DtlsRecord.unprotect(&g_keys, 5, g_out, n, pt, sizeof(pt), &info, CID, sizeof(CID)));
    TEST_ASSERT_EQUAL_UINT(sizeof(MSG), info.pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MSG, pt, sizeof(MSG));

    // Another endpoint's CID is not ours.
    TEST_ASSERT_FALSE(DtlsRecord.unprotect(&g_keys, 5, g_out, n, pt, sizeof(pt), &info, WRONG_CID, sizeof(WRONG_CID)));
    // A receiver expecting no CID must refuse a record that carries one, and vice versa.
    TEST_ASSERT_FALSE(DtlsRecord.unprotect(&g_keys, 5, g_out, n, pt, sizeof(pt), &info, NULL, 0));

    size_t m = DtlsRecord.protect(&g_keys, 5, PROTOCORE_DTLS_CT_APPLICATION_DATA, MSG, sizeof(MSG), g_out,
                                  sizeof(g_out), NULL, 0);
    TEST_ASSERT_FALSE(DtlsRecord.unprotect(&g_keys, 5, g_out, m, pt, sizeof(pt), &info, CID, sizeof(CID)));

    // A connection id wider than the header scratch is refused rather than truncated.
    uint8_t big[PROTOCORE_DTLS_CID_MAX + 1];
    memset(big, 0x11, sizeof(big));
    TEST_ASSERT_EQUAL_UINT(0u, DtlsRecord.protect(&g_keys, 5, PROTOCORE_DTLS_CT_APPLICATION_DATA, MSG, sizeof(MSG),
                                                  g_out, sizeof(g_out), big, sizeof(big)));
    TEST_ASSERT_FALSE(DtlsRecord.unprotect(&g_keys, 5, g_out, m, pt, sizeof(pt), &info, big, sizeof(big)));
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
        size_t n = DtlsRecord.protect(&g_keys, i, TYPES[i], MSG, sizeof(MSG), g_out, sizeof(g_out), NULL, 0);
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_TRUE(DtlsRecord.unprotect(&g_keys, i, g_out, n, pt, sizeof(pt), &info, NULL, 0));
        TEST_ASSERT_EQUAL_HEX8(TYPES[i], info.content_type);
        TEST_ASSERT_EQUAL_UINT(sizeof(MSG), info.pt_len);
    }

    // Sealing "abc" || 0x17 under an outer type of 0x00 lays down the same inner plaintext a peer
    // that padded by one octet would send: content, content type, then a zero.
    static const uint8_t INNER[4] = {'a', 'b', 'c', PROTOCORE_DTLS_CT_APPLICATION_DATA};
    size_t n = DtlsRecord.protect(&g_keys, 9, 0x00, INNER, sizeof(INNER), g_out, sizeof(g_out), NULL, 0);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(DtlsRecord.unprotect(&g_keys, 9, g_out, n, pt, sizeof(pt), &info, NULL, 0));
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DTLS_CT_APPLICATION_DATA, info.content_type);
    TEST_ASSERT_EQUAL_UINT(3u, info.pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("abc", pt, 3);

    // An inner plaintext that is all zeros names no content type: an invalid record (sec 4.5.2).
    static const uint8_t ZERO = 0x00;
    n = DtlsRecord.protect(&g_keys, 10, 0x00, &ZERO, 1, g_out, sizeof(g_out), NULL, 0);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_FALSE(DtlsRecord.unprotect(&g_keys, 10, g_out, n, pt, sizeof(pt), &info, NULL, 0));
}

// sec 4.5.2 with sec 4.2.3: a record whose fixed bits are wrong, that is too short to carry the
// 16-byte sequence-number sample, or whose body has been altered, is discarded as if deprotection
// had failed.
void test_rfc9147_4_5_2_invalid_records(void)
{
    uint8_t pt[64];
    DtlsCiphertext info;
    size_t n = DtlsRecord.protect(&g_keys, 11, PROTOCORE_DTLS_CT_APPLICATION_DATA, MSG, sizeof(MSG), g_out,
                                  sizeof(g_out), NULL, 0);
    uint8_t rec[128];
    memcpy(rec, g_out, n);

    // "The three high bits of the first byte of the unified header are set to 001."
    for (uint8_t top = 0; top < 8; top++)
    {
        memcpy(rec, g_out, n);
        rec[0] = (uint8_t)((rec[0] & 0x1F) | (uint8_t)(top << 5));
        proto_bool ok = DtlsRecord.unprotect(&g_keys, 11, rec, n, pt, sizeof(pt), &info, NULL, 0);
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
        TEST_ASSERT_FALSE_MESSAGE(DtlsRecord.unprotect(&g_keys, 11, rec, n, pt, sizeof(pt), &info, NULL, 0),
                                  "a flipped bit must not deprotect");
    }

    // "This procedure requires the ciphertext length to be at least 16 bytes. Receivers MUST reject
    // shorter records": every truncation of this record is short of its own declared length.
    for (size_t take = 0; take < n; take++)
    {
        TEST_ASSERT_FALSE(DtlsRecord.unprotect(&g_keys, 11, g_out, take, pt, sizeof(pt), &info, NULL, 0));
    }

    // A destination too small for the recovered plaintext, and one too small for the record.
    TEST_ASSERT_FALSE(DtlsRecord.unprotect(&g_keys, 11, g_out, n, pt, 4, &info, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0u, DtlsRecord.protect(&g_keys, 11, PROTOCORE_DTLS_CT_APPLICATION_DATA, MSG, sizeof(MSG),
                                                  g_out, n - 1, NULL, 0));
}

// RFC 9147 sec 4.5.1: "the receiver MUST verify that the record contains a sequence number that does
// not duplicate the sequence number of any other record received in that epoch", using a sliding
// window whose right edge is the highest accepted number and whose left edge rejects everything
// older. "The window MUST NOT be updated ... until that record has been deprotected successfully."
void test_rfc9147_4_5_1_replay_window(void)
{
    DtlsReplayWindow w;
    DtlsRecord.replay_init(&w);

    // "The received record counter for an epoch MUST be initialized to zero when that epoch is first
    // used": before any record, every sequence number is new, including zero.
    TEST_ASSERT_TRUE(DtlsRecord.replay_check(&w, 0));
    TEST_ASSERT_TRUE(DtlsRecord.replay_check(&w, 1000));
    DtlsRecord.replay_mark(&w, 0);
    TEST_ASSERT_FALSE(DtlsRecord.replay_check(&w, 0)); // now a duplicate

    // Ahead of the right edge is new; a gap is allowed and the skipped numbers stay acceptable.
    TEST_ASSERT_TRUE(DtlsRecord.replay_check(&w, 5));
    DtlsRecord.replay_mark(&w, 5);
    TEST_ASSERT_FALSE(DtlsRecord.replay_check(&w, 5));
    TEST_ASSERT_TRUE(DtlsRecord.replay_check(&w, 3)); // reordered, inside the window, not yet seen
    DtlsRecord.replay_mark(&w, 3);
    TEST_ASSERT_FALSE(DtlsRecord.replay_check(&w, 3));
    TEST_ASSERT_FALSE(DtlsRecord.replay_check(&w, 0));
    TEST_ASSERT_TRUE(DtlsRecord.replay_check(&w, 4));

    // "Records that contain sequence numbers lower than the left edge of the window are rejected."
    // The window is 64 wide, so with the right edge at 5 + 64 = 69, 5 falls out and 6 is the oldest
    // still inside.
    DtlsRecord.replay_mark(&w, 69);
    TEST_ASSERT_FALSE(DtlsRecord.replay_check(&w, 5)); // 69 - 5 = 64: outside
    TEST_ASSERT_TRUE(DtlsRecord.replay_check(&w, 6));  // 69 - 6 = 63: the oldest bit
    TEST_ASSERT_FALSE(DtlsRecord.replay_check(&w, 69));

    // A jump of more than the window width clears everything behind it.
    DtlsRecord.replay_mark(&w, 1000);
    TEST_ASSERT_FALSE(DtlsRecord.replay_check(&w, 1000));
    TEST_ASSERT_FALSE(DtlsRecord.replay_check(&w, 6));
    TEST_ASSERT_FALSE(DtlsRecord.replay_check(&w, 936)); // 1000 - 936 = 64: outside
    TEST_ASSERT_TRUE(DtlsRecord.replay_check(&w, 937));
    TEST_ASSERT_TRUE(DtlsRecord.replay_check(&w, 1001));

    // "Because each epoch resets the sequence number space, a separate sliding window is needed for
    // each epoch": a fresh window shares nothing with the old one.
    DtlsRecord.replay_init(&w);
    TEST_ASSERT_TRUE(DtlsRecord.replay_check(&w, 0));
    TEST_ASSERT_TRUE(DtlsRecord.replay_check(&w, 1000));
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
        size_t n = DtlsRecord.protect(&g_keys, len, PROTOCORE_DTLS_CT_APPLICATION_DATA, src, len, g_out, sizeof(g_out),
                                      NULL, 0);
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_TRUE(DtlsRecord.unprotect(&g_keys, len, g_out, n, pt, sizeof(pt), &info, NULL, 0));
        TEST_ASSERT_EQUAL_UINT(len, info.pt_len);
        TEST_ASSERT_EQUAL_HEX8(PROTOCORE_DTLS_CT_APPLICATION_DATA, info.content_type);
        if (len)
        {
            TEST_ASSERT_EQUAL_UINT8_ARRAY(src, pt, len);
        }
    }
}
