// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// DTLS 1.3 record layer tests (RFC 9147 §4). The record + key derivation is pinned byte-for-byte
// to an INDEPENDENT reconstruction (test/scratch pc_dtls_kat.py): HKDF-Expand-Label via stdlib
// hmac/hashlib, AEAD_AES_128_GCM + AES-ECB via the `cryptography` library. A byte-exact match of
// the whole DTLSCiphertext record proves the unified header, the AEAD nonce (§4.2.2), the AAD
// selection, and the sequence-number encryption (§4.2.3) are all assembled correctly.

#include "network_drivers/presentation/security/dtls/dtls_record.h"
#include <stdint.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// ---- KAT: secret 0x00..0x1f, epoch 3, seq 5, content_type 23, "hello dtls 1.3". Keys use the DTLS
// 1.3 "dtls13" HKDF-Expand-Label prefix (RFC 9147 sec 5.9); pinned to an independent reconstruction
// (stdlib HKDF + cryptography AES-GCM/AES-ECB), and cross-checked against wolfSSL DTLS 1.3 interop. ----
static const uint8_t KAT_SECRET[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                       0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                       0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
static const uint8_t KAT_KEY[16] = {0xcc, 0x95, 0xab, 0xc2, 0x58, 0xd3, 0x09, 0x42,
                                    0x4d, 0xdb, 0xf7, 0xcb, 0xa6, 0x8b, 0xd7, 0x7e};
static const uint8_t KAT_IV[12] = {0x6d, 0x32, 0x99, 0x30, 0x5d, 0xd2, 0x09, 0xfc, 0x86, 0x5c, 0xf8, 0xf1};
static const uint8_t KAT_SN[16] = {0xc5, 0xb1, 0xa0, 0x64, 0x9e, 0xa4, 0xfd, 0xaf,
                                   0xbe, 0x7e, 0x25, 0x66, 0x65, 0x06, 0x82, 0x22};
static const uint8_t KAT_WIRE[36] = {0x2f, 0xce, 0x85, 0x00, 0x1f, 0x48, 0x98, 0x88, 0x04, 0x32, 0x05, 0x52,
                                     0x8c, 0xf2, 0xfd, 0x7c, 0x2f, 0xa0, 0xb0, 0xb1, 0xba, 0x50, 0xf4, 0x84,
                                     0x3a, 0xe1, 0x84, 0xcd, 0x94, 0x45, 0xf7, 0xb0, 0x7b, 0xca, 0xe9, 0xb0};
static const char *KAT_PLAINTEXT = "hello dtls 1.3";
static const uint16_t KAT_EPOCH = 3;
static const uint64_t KAT_SEQ = 5;
static const uint8_t KAT_CT = PC_DTLS_CT_APPLICATION_DATA; // 23

// The record keys are derived from the traffic secret exactly like the independent tool.
static void test_dtls_record_keys_derive_kat(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, KAT_EPOCH, KAT_SECRET);
    // The raw key is no longer stored - it becomes a keyed context - so prove it a step removed: a
    // context built from the expected key must seal identically to the derived one. That is the same
    // assertion plus proof the context was actually built, rather than that two byte arrays matched.
    static uint8_t ref_ws[PC_WORK_AES128GCM] __attribute__((aligned(8)));
    struct pc_aes128gcm_key *ref = pc_aes128gcm_key_init(ref_ws, KAT_KEY);
    uint8_t n12[12] = {0}, zpt[16] = {0}, c1[16], t1[16], c2[16], t2[16];
    pc_aes128gcm_seal(ref, n12, NULL, 0, zpt, sizeof(zpt), c1, t1);
    pc_aes128gcm_seal((struct pc_aes128gcm_key *)(k.gcm), n12, NULL, 0, zpt, sizeof(zpt), c2, t2);
    TEST_ASSERT_EQUAL_MEMORY(c1, c2, sizeof(c1));
    TEST_ASSERT_EQUAL_MEMORY(t1, t2, sizeof(t1));
    pc_aes128gcm_key_wipe(ref);
    TEST_ASSERT_EQUAL_MEMORY(KAT_IV, k.iv, 12);
    // sn_key is a keyed AES-128 context now, not the raw key: compare the block it produces against a
    // context built from the expected key.
    static uint8_t sn_ref[PC_WORK_AES128] __attribute__((aligned(8)));
    uint8_t blk[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}, s1[16], s2[16];
    pc_aes128_init((struct pc_aes128 *)(sn_ref), KAT_SN);
    pc_aes128_encrypt_block((struct pc_aes128 *)(sn_ref), blk, s1);
    pc_aes128_encrypt_block((struct pc_aes128 *)(k.sn_key), blk, s2);
    TEST_ASSERT_EQUAL_MEMORY(s1, s2, 16);
    pc_aes128_wipe((struct pc_aes128 *)(sn_ref));
    TEST_ASSERT_EQUAL_UINT16(KAT_EPOCH, k.epoch);
}

// pc_dtls_ciphertext_protect must produce the exact on-wire record byte-for-byte.
static void test_dtls_ciphertext_protect_kat(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, KAT_EPOCH, KAT_SECRET);
    uint8_t out[64];
    size_t n = pc_dtls_ciphertext_protect(&k, KAT_SEQ, KAT_CT, (const uint8_t *)KAT_PLAINTEXT, strlen(KAT_PLAINTEXT),
                                          out, sizeof(out), NULL, 0);
    TEST_ASSERT_EQUAL_size_t(sizeof(KAT_WIRE), n);
    TEST_ASSERT_EQUAL_MEMORY(KAT_WIRE, out, sizeof(KAT_WIRE));
}

// Unprotecting the KAT record recovers the plaintext, content type, and full sequence number.
static void test_dtls_ciphertext_unprotect_kat(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, KAT_EPOCH, KAT_SECRET);
    uint8_t out[64];
    DtlsCiphertext info;
    TEST_ASSERT_TRUE(
        pc_dtls_ciphertext_unprotect(&k, KAT_SEQ, KAT_WIRE, sizeof(KAT_WIRE), out, sizeof(out), &info, NULL, 0));
    TEST_ASSERT_EQUAL_UINT8(KAT_CT, info.content_type);
    TEST_ASSERT_EQUAL_UINT64(KAT_SEQ, info.seq);
    TEST_ASSERT_EQUAL_size_t(strlen(KAT_PLAINTEXT), info.pt_len);
    TEST_ASSERT_EQUAL_MEMORY(KAT_PLAINTEXT, out, info.pt_len);
}

// Round-trip a range of payload sizes, content types, and sequence numbers.
static void test_dtls_ciphertext_roundtrip(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, 2, KAT_SECRET);

    const size_t sizes[] = {0, 1, 15, 16, 40, 250};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        uint8_t pt[256];
        for (size_t j = 0; j < sizes[i]; j++)
        {
            pt[j] = (uint8_t)(j * 7 + i);
        }
        uint64_t seq = 1000 + i;
        uint8_t ct = (i & 1) ? PC_DTLS_CT_HANDSHAKE : PC_DTLS_CT_APPLICATION_DATA;

        uint8_t wire[320];
        size_t n = pc_dtls_ciphertext_protect(&k, seq, ct, pt, sizes[i], wire, sizeof(wire), NULL, 0);
        TEST_ASSERT_TRUE(n > 0);

        uint8_t out[320];
        DtlsCiphertext info;
        TEST_ASSERT_TRUE(pc_dtls_ciphertext_unprotect(&k, seq, wire, n, out, sizeof(out), &info, NULL, 0));
        TEST_ASSERT_EQUAL_UINT8(ct, info.content_type);
        TEST_ASSERT_EQUAL_UINT64(seq, info.seq);
        TEST_ASSERT_EQUAL_size_t(sizes[i], info.pt_len);
        if (sizes[i])
        {
            TEST_ASSERT_EQUAL_MEMORY(pt, out, sizes[i]);
        }
    }
}

// The 16-bit on-wire sequence number is reconstructed to the full value nearest the expected one,
// including across a 16-bit rollover (RFC 9147 §4.2.2 / RFC 9000 App. A.3).
static void test_dtls_seq_reconstruction(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, 2, KAT_SECRET);
    const uint8_t pt[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    // Full sequence numbers whose low 16 bits are all that travel on the wire; unprotect with the
    // correct "expected next" recovers the full value.
    const uint64_t seqs[] = {0x0001u, 0xFFFFu, 0x10000u, 0x1FFFFu, 0x20003u, 0xABCDEu};
    for (unsigned i = 0; i < sizeof(seqs) / sizeof(seqs[0]); i++)
    {
        uint8_t wire[64];
        size_t n = pc_dtls_ciphertext_protect(&k, seqs[i], PC_DTLS_CT_APPLICATION_DATA, pt, sizeof(pt), wire,
                                              sizeof(wire), NULL, 0);
        TEST_ASSERT_TRUE(n > 0);
        uint8_t out[64];
        DtlsCiphertext info;
        // next_seq = seqs[i] (the receiver expects this record next); reconstruction returns it exactly.
        TEST_ASSERT_TRUE(pc_dtls_ciphertext_unprotect(&k, seqs[i], wire, n, out, sizeof(out), &info, NULL, 0));
        TEST_ASSERT_EQUAL_UINT64(seqs[i], info.seq);
        TEST_ASSERT_EQUAL_MEMORY(pt, out, sizeof(pt));
    }
}

// A tampered ciphertext, a wrong-epoch key, and a connection-id record are all rejected.
static void test_dtls_ciphertext_unprotect_rejects(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, KAT_EPOCH, KAT_SECRET);
    uint8_t out[64];
    DtlsCiphertext info;

    // Flip one ciphertext byte -> AEAD tag fails.
    uint8_t bad[36];
    memcpy(bad, KAT_WIRE, sizeof(bad));
    bad[20] ^= 0x01;
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, KAT_SEQ, bad, sizeof(bad), out, sizeof(out), &info, NULL, 0));

    // Keys for a different epoch (low 2 bits mismatch the header) -> rejected before AEAD.
    DtlsRecordKeys k2;
    pc_dtls_record_keys_derive(&k2, DTLS_CIPHER_AES_128_GCM_SHA256, 2, KAT_SECRET); // epoch 2 != header's 3
    TEST_ASSERT_FALSE(
        pc_dtls_ciphertext_unprotect(&k2, KAT_SEQ, KAT_WIRE, sizeof(KAT_WIRE), out, sizeof(out), &info, NULL, 0));

    // A connection-id record (C bit set) when no CID was negotiated (the default) is rejected.
    memcpy(bad, KAT_WIRE, sizeof(bad));
    bad[0] |= 0x10; // set C
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, KAT_SEQ, bad, sizeof(bad), out, sizeof(out), &info, NULL, 0));

    // Truncated record (no room for the 16-byte sequence-number sample) is rejected.
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, KAT_SEQ, KAT_WIRE, 10, out, sizeof(out), &info, NULL, 0));
}

// DTLSPlaintext builds and parses back to the same fields (initial-flight / alert records).
static void test_dtls_plaintext_roundtrip(void)
{
    const uint8_t frag[19] = "handshake fragment";
    uint8_t rec[64];
    size_t n = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0x0102030405ull, frag, sizeof(frag), rec, sizeof(rec));
    TEST_ASSERT_EQUAL_size_t(PC_DTLS_PLAINTEXT_HDR_LEN + sizeof(frag), n);
    // legacy_version is DTLS 1.2 on the wire.
    TEST_ASSERT_EQUAL_UINT8(0xFE, rec[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFD, rec[2]);

    DtlsPlaintext pt;
    size_t consumed = pc_dtls_plaintext_parse(rec, n, &pt);
    TEST_ASSERT_EQUAL_size_t(n, consumed);
    TEST_ASSERT_EQUAL_UINT8(PC_DTLS_CT_HANDSHAKE, pt.content_type);
    TEST_ASSERT_EQUAL_UINT16(0, pt.epoch);
    TEST_ASSERT_EQUAL_UINT64(0x0102030405ull, pt.seq);
    TEST_ASSERT_EQUAL_size_t(sizeof(frag), pt.frag_len);
    TEST_ASSERT_EQUAL_MEMORY(frag, pt.fragment, sizeof(frag));

    // A wrong legacy_version is rejected.
    rec[1] = 0x03;
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_plaintext_parse(rec, n, &pt));
}

// The anti-replay sliding window accepts new records, rejects replays, and ages out old ones.
static void test_dtls_replay_window(void)
{
    DtlsReplayWindow w;
    pc_dtls_replay_init(&w);

    TEST_ASSERT_TRUE(pc_dtls_replay_check(&w, 5)); // first record, any seq
    pc_dtls_replay_mark(&w, 5);
    TEST_ASSERT_FALSE(pc_dtls_replay_check(&w, 5)); // replay of the highest

    TEST_ASSERT_TRUE(pc_dtls_replay_check(&w, 6));
    pc_dtls_replay_mark(&w, 6);
    TEST_ASSERT_TRUE(pc_dtls_replay_check(&w, 3)); // older but in-window and unseen
    pc_dtls_replay_mark(&w, 3);
    TEST_ASSERT_FALSE(pc_dtls_replay_check(&w, 3)); // now a replay

    // Jump far ahead; records older than the 64-wide window are rejected.
    pc_dtls_replay_mark(&w, 200);
    TEST_ASSERT_FALSE(pc_dtls_replay_check(&w, 6));   // 200 - 6 = 194 >= 64
    TEST_ASSERT_FALSE(pc_dtls_replay_check(&w, 200)); // replay of the new highest
    TEST_ASSERT_TRUE(pc_dtls_replay_check(&w, 180));  // in-window, unseen
    TEST_ASSERT_TRUE(pc_dtls_replay_check(&w, 201));  // ahead
}

// Connection ids (RFC 9146 / RFC 9147 §9): a record protected with a CID carries the C bit and the CID
// immediately after the first byte, round-trips when the receiver expects that exact CID, and the CID is
// covered by the AEAD AAD (so a mismatch fails to open).
static void test_dtls_cid_roundtrip(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, 3, KAT_SECRET);
    const uint8_t cid[4] = {0xCA, 0xFE, 0xBA, 0xBE};
    const uint8_t pt[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    uint8_t wire[64];
    size_t n = pc_dtls_ciphertext_protect(&k, 7, PC_DTLS_CT_APPLICATION_DATA, pt, sizeof(pt), wire, sizeof(wire), cid,
                                          sizeof(cid));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE((wire[0] & 0x10) != 0);              // C bit set
    TEST_ASSERT_EQUAL_MEMORY(cid, wire + 1, sizeof(cid)); // CID immediately after the first byte

    uint8_t out[64];
    DtlsCiphertext info;
    TEST_ASSERT_TRUE(pc_dtls_ciphertext_unprotect(&k, 7, wire, n, out, sizeof(out), &info, cid, sizeof(cid)));
    TEST_ASSERT_EQUAL_UINT64(7, info.seq);
    TEST_ASSERT_EQUAL_size_t(sizeof(pt), info.pt_len);
    TEST_ASSERT_EQUAL_MEMORY(pt, out, sizeof(pt));
}

// Every CID mismatch is rejected: an unexpected CID, a wrong CID, a wrong CID length, a missing CID when
// one was negotiated, and an over-long CID at protect time.
static void test_dtls_cid_rejects(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, 3, KAT_SECRET);
    const uint8_t cid[4] = {0xCA, 0xFE, 0xBA, 0xBE};
    const uint8_t pt[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    uint8_t out[64];
    DtlsCiphertext info;

    uint8_t wire[64];
    size_t n = pc_dtls_ciphertext_protect(&k, 3, PC_DTLS_CT_APPLICATION_DATA, pt, sizeof(pt), wire, sizeof(wire), cid,
                                          sizeof(cid));
    TEST_ASSERT_TRUE(n > 0);

    // A CID record but the receiver expects none -> rejected (unexpected CID).
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, 3, wire, n, out, sizeof(out), &info, NULL, 0));
    // Wrong CID of the right length -> rejected.
    const uint8_t other[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, 3, wire, n, out, sizeof(out), &info, other, sizeof(other)));
    // Right CID prefix but a shorter expected length -> rejected (offsets + AAD differ).
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, 3, wire, n, out, sizeof(out), &info, cid, 3));

    // A non-CID record but the receiver expects a CID -> rejected.
    uint8_t plain[64];
    size_t pn =
        pc_dtls_ciphertext_protect(&k, 3, PC_DTLS_CT_APPLICATION_DATA, pt, sizeof(pt), plain, sizeof(plain), NULL, 0);
    TEST_ASSERT_TRUE(pn > 0);
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, 3, plain, pn, out, sizeof(out), &info, cid, sizeof(cid)));

    // A CID longer than PC_DTLS_CID_MAX is refused by protect.
    uint8_t longcid[PC_DTLS_CID_MAX + 1] = {0};
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_ciphertext_protect(&k, 3, PC_DTLS_CT_APPLICATION_DATA, pt, sizeof(pt), wire,
                                                           sizeof(wire), longcid, sizeof(longcid)));
}

// Sequence-number reconstruction across a 16-bit rollover in BOTH directions (RFC 9147 §4.2.2 /
// RFC 9000 App. A.3): when the receiver's "expected next" sits in the neighbouring window the decode
// must wrap forward (candidate + win) or backward (candidate - win) to the value the sender actually
// used, so the AEAD nonce matches and the record opens.
static void test_dtls_seq_rollover_both_directions(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, 2, KAT_SECRET);
    const uint8_t pt[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t wire[64];
    uint8_t out[64];
    DtlsCiphertext info;

    // Forward wrap: sender used 0x10000 (low 16 bits 0x0000); receiver expected 0xF000 (previous window).
    size_t n = pc_dtls_ciphertext_protect(&k, 0x10000ull, PC_DTLS_CT_APPLICATION_DATA, pt, sizeof(pt), wire,
                                          sizeof(wire), NULL, 0);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(pc_dtls_ciphertext_unprotect(&k, 0xF000ull, wire, n, out, sizeof(out), &info, NULL, 0));
    TEST_ASSERT_EQUAL_UINT64(0x10000ull, info.seq);
    TEST_ASSERT_EQUAL_MEMORY(pt, out, sizeof(pt));

    // Backward wrap: sender used 0xFFFF (low 16 bits 0xFFFF); receiver expected 0x10000 (next window).
    n = pc_dtls_ciphertext_protect(&k, 0xFFFFull, PC_DTLS_CT_APPLICATION_DATA, pt, sizeof(pt), wire, sizeof(wire), NULL,
                                   0);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(pc_dtls_ciphertext_unprotect(&k, 0x10000ull, wire, n, out, sizeof(out), &info, NULL, 0));
    TEST_ASSERT_EQUAL_UINT64(0xFFFFull, info.seq);
    TEST_ASSERT_EQUAL_MEMORY(pt, out, sizeof(pt));
}

// DTLSPlaintext build refuses an overflowing buffer and an over-long (> 0xFFFF) fragment, accepts a
// zero-length fragment (header only), and parse refuses a short header and a length past the record.
static void test_dtls_plaintext_bounds(void)
{
    const uint8_t frag[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t rec[64];
    DtlsPlaintext pt;

    // total > out_cap.
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, frag, sizeof(frag), rec, 10));

    // A zero-length fragment is a valid header-only record.
    size_t n0 = pc_dtls_plaintext_build(PC_DTLS_CT_ALERT, 0, 0, NULL, 0, rec, sizeof(rec));
    TEST_ASSERT_EQUAL_size_t(PC_DTLS_PLAINTEXT_HDR_LEN, n0);
    TEST_ASSERT_EQUAL_size_t(PC_DTLS_PLAINTEXT_HDR_LEN, pc_dtls_plaintext_parse(rec, n0, &pt));
    TEST_ASSERT_EQUAL_size_t(0, pt.frag_len);

    // A fragment larger than 0xFFFF is refused even with ample capacity (static: too large for the stack).
    static uint8_t bigfrag[0x10000];
    static uint8_t bigout[0x10000 + PC_DTLS_PLAINTEXT_HDR_LEN + 8];
    memset(bigfrag, 0, sizeof(bigfrag));
    TEST_ASSERT_EQUAL_size_t(
        0, pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0, bigfrag, sizeof(bigfrag), bigout, sizeof(bigout)));

    // parse: header shorter than 13 bytes.
    size_t n = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 0x0102030405ull, frag, sizeof(frag), rec, sizeof(rec));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_plaintext_parse(rec, PC_DTLS_PLAINTEXT_HDR_LEN - 1, &pt));
    // parse: the length field claims more than the record carries (truncated by one byte).
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_plaintext_parse(rec, n - 1, &pt));
}

// pc_dtls_ciphertext_protect rejects an unsupported cipher, a non-null-required CID (cid_len>0, cid null),
// and an output buffer too small for the record.
static void test_dtls_protect_bounds(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, KAT_EPOCH, KAT_SECRET);
    const uint8_t pt[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t out[64];

    DtlsRecordKeys kbad = k;
    kbad.cipher = (DtlsCipher)0x7F; // unsupported cipher
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_ciphertext_protect(&kbad, 1, PC_DTLS_CT_APPLICATION_DATA, pt, sizeof(pt), out,
                                                           sizeof(out), NULL, 0));
    // cid_len > 0 with a null cid pointer.
    TEST_ASSERT_EQUAL_size_t(
        0, pc_dtls_ciphertext_protect(&k, 1, PC_DTLS_CT_APPLICATION_DATA, pt, sizeof(pt), out, sizeof(out), NULL, 4));
    // Output buffer too small for header + body.
    TEST_ASSERT_EQUAL_size_t(
        0, pc_dtls_ciphertext_protect(&k, 1, PC_DTLS_CT_APPLICATION_DATA, pt, sizeof(pt), out, 5, NULL, 0));
}

// pc_dtls_ciphertext_unprotect rejects every malformed shape: unsupported cipher, zero-length record,
// over-long expected CID, wrong fixed-pattern first byte, too short for the sequence number, too short
// for the length field, a length below the AEAD minimum (< 16 and exactly 16), an output buffer too small
// for the plaintext, and an L=0 (implicit length) record that then fails the AEAD open.
static void test_dtls_unprotect_bounds(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, KAT_EPOCH, KAT_SECRET);
    uint8_t out[64];
    DtlsCiphertext info;

    DtlsRecordKeys kbad = k;
    kbad.cipher = (DtlsCipher)0x7F;
    TEST_ASSERT_FALSE(
        pc_dtls_ciphertext_unprotect(&kbad, KAT_SEQ, KAT_WIRE, sizeof(KAT_WIRE), out, sizeof(out), &info, NULL, 0));
    // Zero-length record.
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, KAT_SEQ, KAT_WIRE, 0, out, sizeof(out), &info, NULL, 0));
    // expected_cid_len beyond PC_DTLS_CID_MAX.
    uint8_t bigcid[PC_DTLS_CID_MAX + 1] = {0};
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, KAT_SEQ, KAT_WIRE, sizeof(KAT_WIRE), out, sizeof(out), &info,
                                                   bigcid, sizeof(bigcid)));

    // Wrong fixed pattern in the first byte (top three bits must be 001).
    uint8_t bad[sizeof(KAT_WIRE)];
    memcpy(bad, KAT_WIRE, sizeof(bad));
    bad[0] = 0x00;
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, KAT_SEQ, bad, sizeof(bad), out, sizeof(out), &info, NULL, 0));

    // Too short for the 16-bit sequence number, and too short for the 2-byte length field.
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, KAT_SEQ, KAT_WIRE, 2, out, sizeof(out), &info, NULL, 0));
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, KAT_SEQ, KAT_WIRE, 4, out, sizeof(out), &info, NULL, 0));

    // Well-formed header (byte0 0x2f: 001, S=1, L=1, epoch 3) but a length below the AEAD minimum: 10 (< 16)
    // and exactly 16 (== tag, no inner byte). Both rejected before any AEAD work.
    const uint8_t rec_enc10[15] = {0x2f, 0x00, 0x05, 0x00, 0x0A, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(
        pc_dtls_ciphertext_unprotect(&k, 0, rec_enc10, sizeof(rec_enc10), out, sizeof(out), &info, NULL, 0));
    uint8_t rec_enc16[21];
    memset(rec_enc16, 0, sizeof(rec_enc16));
    rec_enc16[0] = 0x2f;
    rec_enc16[2] = 0x05;
    rec_enc16[4] = 0x10; // enc_len = 16
    TEST_ASSERT_FALSE(
        pc_dtls_ciphertext_unprotect(&k, 0, rec_enc16, sizeof(rec_enc16), out, sizeof(out), &info, NULL, 0));

    // Output buffer too small for the recovered plaintext (KAT inner_len = 15).
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, KAT_SEQ, KAT_WIRE, sizeof(KAT_WIRE), out, 10, &info, NULL, 0));

    // An L=0 record: parsed to end-of-datagram, then fails the AEAD open (its AAD/offsets no longer match).
    memcpy(bad, KAT_WIRE, sizeof(bad));
    bad[0] = (uint8_t)(KAT_WIRE[0] & ~0x04); // clear the L bit
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, KAT_SEQ, bad, sizeof(bad), out, sizeof(out), &info, NULL, 0));
}

// An all-zero inner plaintext carries no content type (RFC 8446 §5.2/§5.4): unprotect strips every zero
// byte and rejects the record. Exercises the zero-padding strip loop and the "no content type" guard.
static void test_dtls_unprotect_all_zero_inner(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, KAT_EPOCH, KAT_SECRET);
    const uint8_t zeros[4] = {0, 0, 0, 0};
    uint8_t wire[64];
    // content_type 0 with an all-zero payload -> the sealed inner plaintext is all zeros.
    size_t n = pc_dtls_ciphertext_protect(&k, 9, 0x00, zeros, sizeof(zeros), wire, sizeof(wire), NULL, 0);
    TEST_ASSERT_TRUE(n > 0);
    uint8_t out[64];
    DtlsCiphertext info;
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, 9, wire, n, out, sizeof(out), &info, NULL, 0));
}

// Marking a sequence number far below the window's high-water mark is a no-op (RFC 9147 §4.5.1): it stays
// outside the window and the bitmap is unchanged.
static void test_dtls_replay_mark_below_window(void)
{
    DtlsReplayWindow w;
    pc_dtls_replay_init(&w);
    pc_dtls_replay_mark(&w, 200);                     // seed the high-water mark
    pc_dtls_replay_mark(&w, 6);                       // 200 - 6 = 194 >= 64: below the window, ignored
    TEST_ASSERT_FALSE(pc_dtls_replay_check(&w, 6));   // still rejected as outside the window
    TEST_ASSERT_FALSE(pc_dtls_replay_check(&w, 200)); // the high-water record is a replay
    TEST_ASSERT_TRUE(pc_dtls_replay_check(&w, 201));  // ahead of the window is accepted
}

// pc_dtls_plaintext_parse rejects a record whose legacy_version high byte is correct but whose low
// byte is wrong (the second half of the `||` version check, not exercised by the "high byte wrong"
// case in test_dtls_plaintext_roundtrip).
static void test_dtls_plaintext_parse_wrong_version_low_byte(void)
{
    const uint8_t frag[4] = {1, 2, 3, 4};
    uint8_t rec[32];
    size_t n = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, 1, frag, sizeof(frag), rec, sizeof(rec));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT8(0xFE, rec[1]); // high byte correct (left side of the || is false)
    rec[2] = 0x00;                         // low byte wrong -> right side of the || is true
    DtlsPlaintext pt;
    TEST_ASSERT_EQUAL_size_t(0, pc_dtls_plaintext_parse(rec, n, &pt));
}

// A CID record whose caller-supplied length is right but the record is too short to actually hold
// that many CID bytes is rejected (the "off + expected_cid_len > rec_len" arm, distinct from the
// "wrong CID bytes" and "no CID negotiated" rejections already covered).
static void test_dtls_cid_record_too_short_for_expected_cid(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, 3, KAT_SECRET);
    const uint8_t cid[4] = {0xCA, 0xFE, 0xBA, 0xBE};
    const uint8_t pt[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t wire[64];
    size_t n = pc_dtls_ciphertext_protect(&k, 11, PC_DTLS_CT_APPLICATION_DATA, pt, sizeof(pt), wire, sizeof(wire), cid,
                                          sizeof(cid));
    TEST_ASSERT_TRUE(n > 0);

    uint8_t out[64];
    DtlsCiphertext info;
    // Truncate the record to fewer bytes than 1 (unified-header first byte) + sizeof(cid): the C bit is
    // still set (byte 0 is untouched) but there isn't room for the full expected CID.
    TEST_ASSERT_FALSE(
        pc_dtls_ciphertext_unprotect(&k, 11, wire, 1 + sizeof(cid) - 1, out, sizeof(out), &info, cid, sizeof(cid)));
}

// A record with the S bit clear (8-bit, not 16-bit, sequence number) is rejected once its garbage AEAD
// body fails to open, exercising the seq_len=1 arm that pc_dtls_ciphertext_protect never produces
// itself (it always sets S=1) but the wire format / unprotect() still has to handle.
static void test_dtls_unprotect_seq8_variant(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, 0, KAT_SECRET);
    // byte0: fixed(001) | L=1 | epoch=0, S=0 -> 8-bit sequence number. 1 (hdr) + 1 (seq8) + 2 (length) +
    // 17 (enc_len, >= tag(16) + 1 inner byte) = 21 bytes.
    uint8_t rec[21];
    memset(rec, 0, sizeof(rec));
    rec[0] = 0x24; // 0010 0100: FIXED | LENGTH, no CID, no SEQ16, epoch 0
    rec[1] = 0x00; // 8-bit sequence number byte
    rec[2] = 0x00;
    rec[3] = 0x11; // enc_len = 17
    uint8_t out[64];
    DtlsCiphertext info;
    // Garbage AEAD body -> tag check fails, but the seq_len=1 branch is exercised on the way there.
    TEST_ASSERT_FALSE(pc_dtls_ciphertext_unprotect(&k, 0, rec, sizeof(rec), out, sizeof(out), &info, NULL, 0));
}

// Sequence-number reconstruction where the near-expected candidate would overflow past UINT64_MAX when
// projected forward a full window: `candidate + win` wraps around to (or below) `candidate` itself, so
// the forward-wrap arm's second condition must read false even though the first reads true (RFC 9147
// §4.2.2 / RFC 9000 App. A.3 decode, guarding the "candidate + win > candidate" overflow check). The
// reconstructed sequence number then does not match what was actually protected, so the AEAD tag fails.
static void test_dtls_seq_reconstruction_overflow_guard(void)
{
    DtlsRecordKeys k;
    pc_dtls_record_keys_derive(&k, DTLS_CIPHER_AES_128_GCM_SHA256, 2, KAT_SECRET);
    const uint8_t pt[4] = {1, 2, 3, 4};
    uint8_t wire[64];
    // seq's low 16 bits are 0 and its high bits are all 1s so that "candidate + win" wraps to 0.
    uint64_t seq = 0xFFFFFFFFFFFF0000ull;
    size_t n =
        pc_dtls_ciphertext_protect(&k, seq, PC_DTLS_CT_APPLICATION_DATA, pt, sizeof(pt), wire, sizeof(wire), NULL, 0);
    TEST_ASSERT_TRUE(n > 0);
    uint8_t out[64];
    DtlsCiphertext info;
    // next_seq = UINT64_MAX drives candidate + hwin <= next_seq (true) while candidate + win overflows
    // (so > candidate reads false); the resulting mis-decoded sequence number fails the AEAD tag.
    TEST_ASSERT_FALSE(
        pc_dtls_ciphertext_unprotect(&k, 0xFFFFFFFFFFFFFFFFull, wire, n, out, sizeof(out), &info, NULL, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_dtls_record_keys_derive_kat);
    RUN_TEST(test_dtls_ciphertext_protect_kat);
    RUN_TEST(test_dtls_ciphertext_unprotect_kat);
    RUN_TEST(test_dtls_ciphertext_roundtrip);
    RUN_TEST(test_dtls_seq_reconstruction);
    RUN_TEST(test_dtls_ciphertext_unprotect_rejects);
    RUN_TEST(test_dtls_cid_roundtrip);
    RUN_TEST(test_dtls_cid_rejects);
    RUN_TEST(test_dtls_plaintext_roundtrip);
    RUN_TEST(test_dtls_replay_window);
    RUN_TEST(test_dtls_seq_rollover_both_directions);
    RUN_TEST(test_dtls_plaintext_bounds);
    RUN_TEST(test_dtls_protect_bounds);
    RUN_TEST(test_dtls_unprotect_bounds);
    RUN_TEST(test_dtls_unprotect_all_zero_inner);
    RUN_TEST(test_dtls_replay_mark_below_window);
    RUN_TEST(test_dtls_plaintext_parse_wrong_version_low_byte);
    RUN_TEST(test_dtls_cid_record_too_short_for_expected_cid);
    RUN_TEST(test_dtls_unprotect_seq8_variant);
    RUN_TEST(test_dtls_seq_reconstruction_overflow_guard);
    return UNITY_END();
}
