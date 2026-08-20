// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for QUIC packet protection
// (network_drivers/presentation/http/http3/quic_crypto.h, RFC 9001 sec 5).
//
// RFC 9001 Appendix A publishes a complete, real handshake: the Initial salt and every derived
// secret (A.1), the client Initial packet with its header-protection sample and mask (A.2), the
// whole server Initial packet in the clear and protected (A.3), and a Retry integrity tag (A.4).
// test_rfc9001_a3_server_initial is the load-bearing case: it protects the RFC's own unprotected
// header and frames and requires all 135 octets of the RFC's protected packet, so the AEAD nonce
// construction, the associated data, the header-protection sample offset and the mask application
// all have to be right at once. The AES and GCM primitives underneath are pinned to FIPS 197
// Appendix C.1 and to Test Case 4 of the GCM specification.

#include "crypto/aead/aes128gcm/aes128gcm.h"
#include "crypto/kdf/hkdf/hkdf.h"
#include "mmgr/secure/secure.h"
#include "network_drivers/presentation/http/http3/quic_crypto/quic_crypto.h"
#include <string.h>

#include <unity.h>

static uint8_t quic_crypto_work[16]; // the borrow an entry takes; QuicCrypto never reads it

static uint8_t tw[4096]; // the borrow every namespace call in this suite runs out of

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_work[PROTOCORE_QUIC_KEYS_BORROW];
static uint8_t g_pkt[2048];
static uint8_t g_plain[2048];

// Decode a hex string into @p buf, ignoring anything that is not a hex digit; returns the octet count.
static size_t hx(const char *s, uint8_t *buf, size_t cap)
{
    size_t n = 0;
    int hi = -1;
    for (; *s; s++)
    {
        int v;
        if (*s >= '0' && *s <= '9')
        {
            v = *s - '0';
        }
        else if (*s >= 'a' && *s <= 'f')
        {
            v = *s - 'a' + 10;
        }
        else if (*s >= 'A' && *s <= 'F')
        {
            v = *s - 'A' + 10;
        }
        else
        {
            continue;
        }
        if (hi < 0)
        {
            hi = v;
        }
        else
        {
            TEST_ASSERT_TRUE(n < cap);
            buf[n++] = (uint8_t)((hi << 4) | v);
            hi = -1;
        }
    }
    TEST_ASSERT_EQUAL_INT(-1, hi);
    return n;
}

// One scratch AEAD context, rekeyed per use so a backend that attaches vendor state to a context
// does not accumulate one per vector.
static uint8_t g_gcm_ws[PROTOCORE_WORK_AES128GCM] __attribute__((aligned(8)));
static proto_bool g_gcm_live = PROTO_FALSE;

static uint8_t *gcm(const uint8_t *key)
{
    if (g_gcm_live)
    {
        Aes128Gcm.key_wipe(g_gcm_ws);
    }
    g_gcm_live = PROTO_TRUE;
    Aes128GcmV.key_args.key = key;
    Aes128Gcm.key_init(g_gcm_ws);
    return g_gcm_ws;
}

// The derived AEAD context is opaque, so it is compared by what it produces against a context built
// from the key the RFC publishes.
static void same_aead_key(const uint8_t expect_key[16], uint8_t *derived_ctx)
{
    static uint8_t ref_ws[PROTOCORE_WORK_AES128GCM] __attribute__((aligned(8)));
    uint8_t nonce[12] = {0};
    uint8_t pt[16] = {0};
    uint8_t c1[16], t1[16], c2[16], t2[16];
    Aes128GcmV.key_args.key = expect_key;
    Aes128Gcm.key_init(ref_ws);
    uint8_t *ref = ref_ws;
    Aes128GcmV.seal_args.nonce = nonce;
    Aes128GcmV.seal_args.aad = NULL;
    Aes128GcmV.seal_args.aad_len = 0;
    Aes128GcmV.seal_args.pt = pt;
    Aes128GcmV.seal_args.pt_len = sizeof(pt);
    Aes128GcmV.seal_args.ct_out = c1;
    Aes128GcmV.seal_args.tag_out = t1;
    Aes128Gcm.seal(ref);
    Aes128GcmV.seal_args.nonce = nonce;
    Aes128GcmV.seal_args.aad = NULL;
    Aes128GcmV.seal_args.aad_len = 0;
    Aes128GcmV.seal_args.pt = pt;
    Aes128GcmV.seal_args.pt_len = sizeof(pt);
    Aes128GcmV.seal_args.ct_out = c2;
    Aes128GcmV.seal_args.tag_out = t2;
    Aes128Gcm.seal(derived_ctx);
    TEST_ASSERT_EQUAL_MEMORY(c1, c2, 16);
    TEST_ASSERT_EQUAL_MEMORY(t1, t2, 16);
    Aes128Gcm.key_wipe(ref);
}

// The header-protection context likewise: compare the mask block it produces.
static void same_hp_key(const uint8_t expect_key[16], uint8_t *derived_ctx)
{
    static uint8_t ref[PROTOCORE_AES128GCM_BORROW] __attribute__((aligned(8)));
    uint8_t block[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    uint8_t m1[16], m2[16];
    Aes128GcmV.block_key_args.key = expect_key;
    Aes128Gcm.block_init(ref);
    Aes128GcmV.block_args.in = block;
    Aes128GcmV.block_args.out = m1;
    Aes128Gcm.block_encrypt(ref);
    Aes128GcmV.block_args.in = block;
    Aes128GcmV.block_args.out = m2;
    Aes128Gcm.block_encrypt(derived_ctx);
    TEST_ASSERT_EQUAL_MEMORY(m1, m2, 16);
    Aes128Gcm.block_wipe(ref);
}

// FIPS 197 Appendix C.1 (AES-128) publishes one worked example: PLAINTEXT
// 00112233445566778899aabbccddeeff under KEY 000102030405060708090a0b0c0d0e0f gives CIPHERTEXT
// 69c4e0d86a7b0430d8cdb78070b4c55a. Header protection is exactly this block operation.
void test_fips197_aes128_block(void)
{
    uint8_t key[16], in[16], want[16], out[16];
    hx("000102030405060708090a0b0c0d0e0f", key, sizeof(key));
    hx("00112233445566778899aabbccddeeff", in, sizeof(in));
    hx("69c4e0d86a7b0430d8cdb78070b4c55a", want, sizeof(want));

    size_t mark = protocore_secure_mark();
    protocore_span w = protocore_secure_span(PROTOCORE_AES128GCM_BORROW, 8);
    TEST_ASSERT_TRUE(span.ok(w));
    Aes128GcmV.block_key_args.key = key;
    Aes128Gcm.block_init(w.buf);
    Aes128GcmV.block_args.in = in;
    Aes128GcmV.block_args.out = out;
    Aes128Gcm.block_encrypt(w.buf);
    Aes128Gcm.block_wipe(w.buf);
    protocore_secure_release(mark);
    TEST_ASSERT_EQUAL_MEMORY(want, out, 16);
}

// Test Case 4 of the GCM specification (McGrew and Viega, "The Galois/Counter Mode of Operation
// (GCM)"), the case with a plaintext that is not block aligned and 20 octets of additional
// authenticated data - the shape every QUIC packet has.
void test_gcm_test_case_4(void)
{
    uint8_t key[16], iv[12], pt[60], aad[20], want_ct[60], want_tag[16];
    hx("feffe9928665731c6d6a8f9467308308", key, sizeof(key));
    hx("cafebabefacedbaddecaf888", iv, sizeof(iv));
    hx("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
       "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
       pt, sizeof(pt));
    hx("feedfacedeadbeeffeedfacedeadbeefabaddad2", aad, sizeof(aad));
    hx("42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
       "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091",
       want_ct, sizeof(want_ct));
    hx("5bc94fbc3221a5db94fae95ae7121a47", want_tag, sizeof(want_tag));

    uint8_t sealed[60 + 16];
    Aes128GcmV.seal_args.nonce = iv;
    Aes128GcmV.seal_args.aad = aad;
    Aes128GcmV.seal_args.aad_len = sizeof(aad);
    Aes128GcmV.seal_args.pt = pt;
    Aes128GcmV.seal_args.pt_len = sizeof(pt);
    Aes128GcmV.seal_args.ct_out = sealed;
    Aes128GcmV.seal_args.tag_out = sealed + 60;
    Aes128Gcm.seal(gcm(key));
    TEST_ASSERT_EQUAL_MEMORY(want_ct, sealed, 60);
    TEST_ASSERT_EQUAL_MEMORY(want_tag, sealed + 60, 16);

    uint8_t opened[60];
    Aes128GcmV.open_args.nonce = iv;
    Aes128GcmV.open_args.aad = aad;
    Aes128GcmV.open_args.aad_len = sizeof(aad);
    Aes128GcmV.open_args.ct = sealed;
    Aes128GcmV.open_args.ct_len = 60;
    Aes128GcmV.open_args.tag = sealed + 60;
    Aes128GcmV.open_args.out = opened;
    Aes128Gcm.open(gcm(key));
    TEST_ASSERT_TRUE(Aes128GcmV.ok);
    TEST_ASSERT_EQUAL_MEMORY(pt, opened, 60);

    // one flipped ciphertext bit must fail the tag check, and nothing may be written on failure
    sealed[0] ^= 0x01;
    Aes128GcmV.open_args.nonce = iv;
    Aes128GcmV.open_args.aad = aad;
    Aes128GcmV.open_args.aad_len = sizeof(aad);
    Aes128GcmV.open_args.ct = sealed;
    Aes128GcmV.open_args.ct_len = 60;
    Aes128GcmV.open_args.tag = sealed + 60;
    Aes128GcmV.open_args.out = opened;
    Aes128Gcm.open(gcm(key));
    TEST_ASSERT_FALSE(Aes128GcmV.ok);
    // and one flipped associated-data bit likewise, since the header is authenticated too
    sealed[0] ^= 0x01;
    aad[0] ^= 0x01;
    Aes128GcmV.open_args.nonce = iv;
    Aes128GcmV.open_args.aad = aad;
    Aes128GcmV.open_args.aad_len = sizeof(aad);
    Aes128GcmV.open_args.ct = sealed;
    Aes128GcmV.open_args.ct_len = 60;
    Aes128GcmV.open_args.tag = sealed + 60;
    Aes128GcmV.open_args.out = opened;
    Aes128Gcm.open(gcm(key));
    TEST_ASSERT_FALSE(Aes128GcmV.ok);
}

// RFC 9001 sec 5.2 gives the version-1 Initial salt, and Appendix A.1 the value it extracts to for
// the connection ID 0x8394c8f03e515708, plus each traffic secret expanded from it.
void test_rfc9001_a1_initial_secret_chain(void)
{
    uint8_t salt[20], dcid[8], want_initial[32], want_client[32], want_server[32];
    hx("38762cf7f55934b34d179ae6a4c80cadccbb7f0a", salt, sizeof(salt));
    hx("8394c8f03e515708", dcid, sizeof(dcid));
    hx("7db5df06e7a69e432496adedb00851923595221596ae2ae9fb8115c1e9ed0a44", want_initial, 32);
    hx("c00cf151ca5be075ed0ebfb5c80323c42d6b7db67881289af4008f1f6c357aea", want_client, 32);
    hx("3c199828fd139efd216c155ad844cc81fb82fa8d7446fa7d78be803acdda951b", want_server, 32);

    uint8_t initial[32], client[32], server[32];
    Hkdf.extract_args.salt = salt;
    Hkdf.extract_args.salt_len = sizeof(salt);
    Hkdf.extract_args.ikm = dcid;
    Hkdf.extract_args.ikm_len = sizeof(dcid);
    Hkdf.extract_args.prk = initial;
    Hkdf.extract(g_work);
    TEST_ASSERT_EQUAL_MEMORY(want_initial, initial, 32);

    Hkdf.expand_label_args.secret = initial;
    Hkdf.expand_label_args.label = "client in";
    Hkdf.expand_label_args.out = client;
    Hkdf.expand_label_args.out_len = 32;
    Hkdf.expand_label_args.label_prefix = PROTOCORE_HKDF_LABEL_PREFIX;
    Hkdf.expand_label(g_work);
    Hkdf.expand_label_args.secret = initial;
    Hkdf.expand_label_args.label = "server in";
    Hkdf.expand_label_args.out = server;
    Hkdf.expand_label_args.out_len = 32;
    Hkdf.expand_label_args.label_prefix = PROTOCORE_HKDF_LABEL_PREFIX;
    Hkdf.expand_label(g_work);
    TEST_ASSERT_EQUAL_MEMORY(want_client, client, 32);
    TEST_ASSERT_EQUAL_MEMORY(want_server, server, 32);
}

// RFC 9001 A.1 also prints the three packet-protection values each direction expands to, under the
// sec 5.1 labels "quic key", "quic iv" and "quic hp".
void test_rfc9001_a1_packet_keys(void)
{
    uint8_t dcid[8], ck[16], civ[12], chp[16], sk[16], siv[12], shp[16];
    hx("8394c8f03e515708", dcid, sizeof(dcid));
    hx("1f369613dd76d5467730efcbe3b1a22d", ck, sizeof(ck));
    hx("fa044b2f42a3fd3b46fb255c", civ, sizeof(civ));
    hx("9f50449e04a0e810283a1e9933adedd2", chp, sizeof(chp));
    hx("cf3a5331653c364c88f0f379b6067e37", sk, sizeof(sk));
    hx("0ac1493ca1905853b0bba03e", siv, sizeof(siv));
    hx("c206b8d9b9f0f37644430b490eeaa314", shp, sizeof(shp));

    QuicInitialSecrets s;
    QuicCrypto.derive_initial_secrets_args.keys_work = g_work;
    QuicCrypto.derive_initial_secrets_args.dcid = dcid;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(dcid);
    QuicCrypto.derive_initial_secrets_args.out = &s;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);
    TEST_ASSERT_EQUAL_MEMORY(civ, s.client.iv, 12);
    TEST_ASSERT_EQUAL_MEMORY(siv, s.server.iv, 12);
    same_aead_key(ck, s.client.gcm);
    same_aead_key(sk, s.server.gcm);
    same_hp_key(chp, s.client.gcm);
    same_hp_key(shp, s.server.gcm);
}

// RFC 9001 A.3, whole. The unprotected header and the frames the server sends are printed there, as
// is the final protected packet; protecting the first must produce the second octet for octet, and
// unprotecting that must give the frames back with packet number 1.
void test_rfc9001_a3_server_initial(void)
{
    static const char *const HDR = "c1000000010008f067a5502a4262b50040750001";
    static const char *const FRAMES = "02000000000600405a020000560303eefce7f7b37ba1d1632e96677825ddf739"
                                      "88cfc79825df566dc5430b9a045a1200130100002e00330024001d00209d3c94"
                                      "0d89690b84d08a60993c144eca684d1081287c834d5311bcf32bb9da1a002b00"
                                      "020304";
    static const char *const PROTECTED = "cf000000010008f067a5502a4262b5004075c0d95a482cd0991cd25b0aac406a"
                                         "5816b6394100f37a1c69797554780bb38cc5a99f5ede4cf73c3ec2493a1839b3"
                                         "dbcba3f6ea46c5b7684df3548e7ddeb9c3bf9c73cc3f3bded74b562bfb19fb84"
                                         "022f8ef4cdd93795d77d06edbb7aaf2f58891850abbdca3d20398c276456cbc4"
                                         "2158407dd074ee";
    uint8_t dcid[8], want[256];
    hx("8394c8f03e515708", dcid, sizeof(dcid));
    size_t want_len = hx(PROTECTED, want, sizeof(want));
    TEST_ASSERT_EQUAL_UINT(135u, want_len);

    QuicInitialSecrets s;
    QuicCrypto.derive_initial_secrets_args.keys_work = g_work;
    QuicCrypto.derive_initial_secrets_args.dcid = dcid;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(dcid);
    QuicCrypto.derive_initial_secrets_args.out = &s;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    size_t hdr_len = hx(HDR, g_pkt, sizeof(g_pkt));
    TEST_ASSERT_EQUAL_UINT(20u, hdr_len);
    size_t frames_len = hx(FRAMES, g_pkt + hdr_len, sizeof(g_pkt) - hdr_len);
    TEST_ASSERT_EQUAL_UINT(99u, frames_len);

    // the header's Length field is 0x4075, a 2-octet varint holding 117 = 2 packet-number octets +
    // 99 frame octets + the 16-octet tag, so the packet number starts at offset 18
    QuicCrypto.packet_protect_args.pkt = g_pkt;
    QuicCrypto.packet_protect_args.cap = sizeof(g_pkt);
    QuicCrypto.packet_protect_args.pn_offset = 18;
    QuicCrypto.packet_protect_args.pn_len = 2;
    QuicCrypto.packet_protect_args.full_pn = 1;
    QuicCrypto.packet_protect_args.payload_len = frames_len;
    QuicCrypto.packet_protect_args.keys = &s.server;
    QuicCrypto.packet_protect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_protect(quic_crypto_work);
    size_t n = QuicCrypto.n;
    TEST_ASSERT_EQUAL_UINT(135u, n);
    TEST_ASSERT_EQUAL_MEMORY(want, g_pkt, 135);

    // and back: the protected packet opens to the same frames under packet number 1
    uint64_t pn = 0;
    memcpy(g_pkt, want, want_len);
    QuicCrypto.packet_unprotect_args.pkt = g_pkt;
    QuicCrypto.packet_unprotect_args.pn_offset = 18;
    QuicCrypto.packet_unprotect_args.length = 0x75;
    QuicCrypto.packet_unprotect_args.largest_pn = 0;
    QuicCrypto.packet_unprotect_args.keys = &s.server;
    QuicCrypto.packet_unprotect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_unprotect_args.out = g_plain;
    QuicCrypto.packet_unprotect_args.out_pn = &pn;
    QuicCrypto.packet_unprotect(quic_crypto_work);
    size_t pt = QuicCrypto.n;
    TEST_ASSERT_EQUAL_UINT(99u, pt);
    TEST_ASSERT_EQUAL_HEX64(1u, pn);
    uint8_t frames[128];
    (void)hx(FRAMES, frames, sizeof(frames));
    TEST_ASSERT_EQUAL_MEMORY(frames, g_plain, 99);
}

// RFC 9001 A.2, the client Initial. Its payload is the printed CRYPTO frame followed by PADDING
// frames to 1162 octets, and the RFC states the protected header and the 16-octet sample taken from
// the start of the protected payload, so both are checked without embedding all 1200 octets.
void test_rfc9001_a2_client_initial(void)
{
    static const char *const HDR = "c300000001088394c8f03e5157080000449e00000002";
    static const char *const CRYPTO_FRAME = "060040f1010000ed0303ebf8fa56f129 39b9584a3896472ec40bb863cfd3e868"
                                            "04fe3a47f06a2b69484c000004130113 02010000c000000010000e00000b6578"
                                            "616d706c652e636f6dff01000100000a 00080006001d00170018001000070005"
                                            "04616c706e0005000501000000000033 00260024001d00209370b2c9caa47fba"
                                            "baf4559fedba753de171fa71f50f1ce1 5d43e994ec74d748002b000302030400"
                                            "0d0010000e0403050306030203080408 050806002d00020101001c0002400100"
                                            "3900320408ffffffffffffffff050480 00ffff07048000ffff08011001048000"
                                            "75300901100f088394c8f03e51570806 048000ffff";
    uint8_t dcid[8];
    hx("8394c8f03e515708", dcid, sizeof(dcid));

    QuicInitialSecrets s;
    QuicCrypto.derive_initial_secrets_args.keys_work = g_work;
    QuicCrypto.derive_initial_secrets_args.dcid = dcid;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(dcid);
    QuicCrypto.derive_initial_secrets_args.out = &s;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    size_t hdr_len = hx(HDR, g_pkt, sizeof(g_pkt));
    TEST_ASSERT_EQUAL_UINT(22u, hdr_len);
    size_t crypto_len = hx(CRYPTO_FRAME, g_pkt + hdr_len, sizeof(g_pkt) - hdr_len);
    TEST_ASSERT_EQUAL_UINT(245u, crypto_len);
    memset(g_pkt + hdr_len + crypto_len, 0, 1162 - crypto_len); // PADDING frames are zero octets

    // Length 0x449e = 1182 = 4 packet-number octets + 1162 payload + 16 tag, so pn_offset is 18
    QuicCrypto.packet_protect_args.pkt = g_pkt;
    QuicCrypto.packet_protect_args.cap = sizeof(g_pkt);
    QuicCrypto.packet_protect_args.pn_offset = 18;
    QuicCrypto.packet_protect_args.pn_len = 4;
    QuicCrypto.packet_protect_args.full_pn = 2;
    QuicCrypto.packet_protect_args.payload_len = 1162;
    QuicCrypto.packet_protect_args.keys = &s.client;
    QuicCrypto.packet_protect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_protect(quic_crypto_work);
    size_t n = QuicCrypto.n;
    TEST_ASSERT_EQUAL_UINT(1200u, n);

    // "header = c000000001088394c8f03e5157080000449e7b9aec34"
    uint8_t want_hdr[22];
    hx("c000000001088394c8f03e5157080000449e7b9aec34", want_hdr, sizeof(want_hdr));
    TEST_ASSERT_EQUAL_MEMORY(want_hdr, g_pkt, 22);

    // "sample = d1b1c98dd7689fb8ec11d242b123dc9b" - the first 16 octets of the protected payload
    uint8_t want_sample[16];
    hx("d1b1c98dd7689fb8ec11d242b123dc9b", want_sample, sizeof(want_sample));
    TEST_ASSERT_EQUAL_MEMORY(want_sample, g_pkt + 22, 16);

    uint64_t pn = 0;
    QuicCrypto.packet_unprotect_args.pkt = g_pkt;
    QuicCrypto.packet_unprotect_args.pn_offset = 18;
    QuicCrypto.packet_unprotect_args.length = 0x49e;
    QuicCrypto.packet_unprotect_args.largest_pn = 0;
    QuicCrypto.packet_unprotect_args.keys = &s.client;
    QuicCrypto.packet_unprotect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_unprotect_args.out = g_plain;
    QuicCrypto.packet_unprotect_args.out_pn = &pn;
    QuicCrypto.packet_unprotect(quic_crypto_work);
    size_t pt = QuicCrypto.n;
    TEST_ASSERT_EQUAL_UINT(1162u, pt);
    TEST_ASSERT_EQUAL_HEX64(2u, pn);
    uint8_t frame[256];
    (void)hx(CRYPTO_FRAME, frame, sizeof(frame));
    TEST_ASSERT_EQUAL_MEMORY(frame, g_plain, 245);
    for (size_t i = 245; i < 1162; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, g_plain[i]);
    }
}

// RFC 9001 A.4 prints a Retry packet whose last 16 octets are the integrity tag computed over the
// Retry Pseudo-Packet (sec 5.8), whose associated data begins with the original Destination
// Connection ID that the Retry itself does not carry.
void test_rfc9001_a4_retry_integrity_tag(void)
{
    uint8_t retry[64], odcid[8], tag[16];
    size_t retry_len =
        hx("ff000000010008f067a5502a4262b5746f6b656e04a265ba2eff4d829058fb3f0f2496ba", retry, sizeof(retry));
    TEST_ASSERT_EQUAL_UINT(36u, retry_len);
    hx("8394c8f03e515708", odcid, sizeof(odcid));

    QuicCrypto.retry_integrity_tag_args.odcid = odcid;
    QuicCrypto.retry_integrity_tag_args.odcid_len = sizeof(odcid);
    QuicCrypto.retry_integrity_tag_args.retry = retry;
    QuicCrypto.retry_integrity_tag_args.retry_len = retry_len - 16;
    QuicCrypto.retry_integrity_tag_args.tag = tag;
    QuicCrypto.retry_integrity_tag(quic_crypto_work);
    TEST_ASSERT_EQUAL_MEMORY(retry + retry_len - 16, tag, 16);

    // the ODCID is authenticated, so a Retry rebound to a different original connection fails
    uint8_t other[8];
    memcpy(other, odcid, sizeof(other));
    other[0] ^= 0x01;
    uint8_t tag2[16];
    QuicCrypto.retry_integrity_tag_args.odcid = other;
    QuicCrypto.retry_integrity_tag_args.odcid_len = sizeof(other);
    QuicCrypto.retry_integrity_tag_args.retry = retry;
    QuicCrypto.retry_integrity_tag_args.retry_len = retry_len - 16;
    QuicCrypto.retry_integrity_tag_args.tag = tag2;
    QuicCrypto.retry_integrity_tag(quic_crypto_work);
    TEST_ASSERT_TRUE(memcmp(tag, tag2, 16) != 0);
}

// A tampered packet is rejected, not decrypted: every octet of the protected packet is inside the
// AEAD's associated data or its ciphertext.
void test_tampered_packet_fails_authentication(void)
{
    uint8_t dcid[8], pkt[256];
    hx("8394c8f03e515708", dcid, sizeof(dcid));
    QuicInitialSecrets s;
    QuicCrypto.derive_initial_secrets_args.keys_work = g_work;
    QuicCrypto.derive_initial_secrets_args.dcid = dcid;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(dcid);
    QuicCrypto.derive_initial_secrets_args.out = &s;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    size_t want_len = hx("cf000000010008f067a5502a4262b5004075c0d95a482cd0991cd25b0aac406a"
                         "5816b6394100f37a1c69797554780bb38cc5a99f5ede4cf73c3ec2493a1839b3"
                         "dbcba3f6ea46c5b7684df3548e7ddeb9c3bf9c73cc3f3bded74b562bfb19fb84"
                         "022f8ef4cdd93795d77d06edbb7aaf2f58891850abbdca3d20398c276456cbc4"
                         "2158407dd074ee",
                         pkt, sizeof(pkt));
    TEST_ASSERT_EQUAL_UINT(135u, want_len);

    // a flipped octet in the connection ID, which is associated data
    memcpy(g_pkt, pkt, want_len);
    g_pkt[10] ^= 0x01;
    QuicCrypto.packet_unprotect_args.pkt = g_pkt;
    QuicCrypto.packet_unprotect_args.pn_offset = 18;
    QuicCrypto.packet_unprotect_args.length = 0x75;
    QuicCrypto.packet_unprotect_args.largest_pn = 0;
    QuicCrypto.packet_unprotect_args.keys = &s.server;
    QuicCrypto.packet_unprotect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_unprotect_args.out = g_plain;
    QuicCrypto.packet_unprotect_args.out_pn = NULL;
    QuicCrypto.packet_unprotect(quic_crypto_work);
    TEST_ASSERT_EQUAL_UINT((size_t)-1, QuicCrypto.n);

    // a flipped octet in the ciphertext
    memcpy(g_pkt, pkt, want_len);
    g_pkt[40] ^= 0x01;
    QuicCrypto.packet_unprotect_args.pkt = g_pkt;
    QuicCrypto.packet_unprotect_args.pn_offset = 18;
    QuicCrypto.packet_unprotect_args.length = 0x75;
    QuicCrypto.packet_unprotect_args.largest_pn = 0;
    QuicCrypto.packet_unprotect_args.keys = &s.server;
    QuicCrypto.packet_unprotect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_unprotect_args.out = g_plain;
    QuicCrypto.packet_unprotect_args.out_pn = NULL;
    QuicCrypto.packet_unprotect(quic_crypto_work);
    TEST_ASSERT_EQUAL_UINT((size_t)-1, QuicCrypto.n);

    // a flipped octet in the authentication tag
    memcpy(g_pkt, pkt, want_len);
    g_pkt[want_len - 1] ^= 0x01;
    QuicCrypto.packet_unprotect_args.pkt = g_pkt;
    QuicCrypto.packet_unprotect_args.pn_offset = 18;
    QuicCrypto.packet_unprotect_args.length = 0x75;
    QuicCrypto.packet_unprotect_args.largest_pn = 0;
    QuicCrypto.packet_unprotect_args.keys = &s.server;
    QuicCrypto.packet_unprotect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_unprotect_args.out = g_plain;
    QuicCrypto.packet_unprotect_args.out_pn = NULL;
    QuicCrypto.packet_unprotect(quic_crypto_work);
    TEST_ASSERT_EQUAL_UINT((size_t)-1, QuicCrypto.n);
}

// Bounds: a packet number outside the 1..4 octets RFC 9000 sec 17.2 allows has no encoding, a
// destination too small for header + payload + tag is refused, and a Length field too small to hold
// the header-protection sample plus a tag is refused.
void test_parameter_bounds(void)
{
    uint8_t dcid[8];
    hx("8394c8f03e515708", dcid, sizeof(dcid));
    QuicInitialSecrets s;
    QuicCrypto.derive_initial_secrets_args.keys_work = g_work;
    QuicCrypto.derive_initial_secrets_args.dcid = dcid;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(dcid);
    QuicCrypto.derive_initial_secrets_args.out = &s;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    memset(g_pkt, 0, 64);
    QuicCrypto.packet_protect_args.pkt = g_pkt;
    QuicCrypto.packet_protect_args.cap = sizeof(g_pkt);
    QuicCrypto.packet_protect_args.pn_offset = 18;
    QuicCrypto.packet_protect_args.pn_len = 0;
    QuicCrypto.packet_protect_args.full_pn = 1;
    QuicCrypto.packet_protect_args.payload_len = 8;
    QuicCrypto.packet_protect_args.keys = &s.server;
    QuicCrypto.packet_protect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_protect(quic_crypto_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicCrypto.n);
    QuicCrypto.packet_protect_args.pkt = g_pkt;
    QuicCrypto.packet_protect_args.cap = sizeof(g_pkt);
    QuicCrypto.packet_protect_args.pn_offset = 18;
    QuicCrypto.packet_protect_args.pn_len = 5;
    QuicCrypto.packet_protect_args.full_pn = 1;
    QuicCrypto.packet_protect_args.payload_len = 8;
    QuicCrypto.packet_protect_args.keys = &s.server;
    QuicCrypto.packet_protect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_protect(quic_crypto_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicCrypto.n);
    // 18 + 2 + 8 + 16 = 44 octets needed
    QuicCrypto.packet_protect_args.pkt = g_pkt;
    QuicCrypto.packet_protect_args.cap = 43;
    QuicCrypto.packet_protect_args.pn_offset = 18;
    QuicCrypto.packet_protect_args.pn_len = 2;
    QuicCrypto.packet_protect_args.full_pn = 1;
    QuicCrypto.packet_protect_args.payload_len = 8;
    QuicCrypto.packet_protect_args.keys = &s.server;
    QuicCrypto.packet_protect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_protect(quic_crypto_work);
    TEST_ASSERT_EQUAL_UINT(0u, QuicCrypto.n);
    QuicCrypto.packet_protect_args.pkt = g_pkt;
    QuicCrypto.packet_protect_args.cap = 44;
    QuicCrypto.packet_protect_args.pn_offset = 18;
    QuicCrypto.packet_protect_args.pn_len = 2;
    QuicCrypto.packet_protect_args.full_pn = 1;
    QuicCrypto.packet_protect_args.payload_len = 8;
    QuicCrypto.packet_protect_args.keys = &s.server;
    QuicCrypto.packet_protect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_protect(quic_crypto_work);
    TEST_ASSERT_EQUAL_UINT(44u, QuicCrypto.n);

    QuicCrypto.packet_unprotect_args.pkt = g_pkt;
    QuicCrypto.packet_unprotect_args.pn_offset = 18;
    QuicCrypto.packet_unprotect_args.length = 19;
    QuicCrypto.packet_unprotect_args.largest_pn = 0;
    QuicCrypto.packet_unprotect_args.keys = &s.server;
    QuicCrypto.packet_unprotect_args.is_long = PROTO_TRUE;
    QuicCrypto.packet_unprotect_args.out = g_plain;
    QuicCrypto.packet_unprotect_args.out_pn = NULL;
    QuicCrypto.packet_unprotect(quic_crypto_work);
    TEST_ASSERT_EQUAL_UINT((size_t)-1, QuicCrypto.n);
}

// A 1-RTT short header masks five bits of the first octet rather than four (RFC 9001 sec 5.4.1),
// because the short header's Packet Number Length occupies two bits with the Key Phase above them.
// Protect and unprotect must therefore agree on the form, and a round trip proves they do.
void test_short_header_round_trip(void)
{
    uint8_t dcid[8], payload[16];
    hx("8394c8f03e515708", dcid, sizeof(dcid));
    memset(payload, 0xA5, sizeof(payload));
    QuicInitialSecrets s;
    QuicCrypto.derive_initial_secrets_args.keys_work = g_work;
    QuicCrypto.derive_initial_secrets_args.dcid = dcid;
    QuicCrypto.derive_initial_secrets_args.dcid_len = sizeof(dcid);
    QuicCrypto.derive_initial_secrets_args.out = &s;
    QuicCrypto.derive_initial_secrets(quic_crypto_work);

    // 0x42: short form, Fixed Bit, 3-octet packet number; then a 4-octet connection id
    g_pkt[0] = 0x42;
    memset(g_pkt + 1, 0x11, 4);
    g_pkt[5] = 0x00;
    g_pkt[6] = 0xbf;
    g_pkt[7] = 0xf4;
    memcpy(g_pkt + 8, payload, sizeof(payload));

    QuicCrypto.packet_protect_args.pkt = g_pkt;
    QuicCrypto.packet_protect_args.cap = sizeof(g_pkt);
    QuicCrypto.packet_protect_args.pn_offset = 5;
    QuicCrypto.packet_protect_args.pn_len = 3;
    QuicCrypto.packet_protect_args.full_pn = 0x2700bff4ULL;
    QuicCrypto.packet_protect_args.payload_len = sizeof(payload);
    QuicCrypto.packet_protect_args.keys = &s.server;
    QuicCrypto.packet_protect_args.is_long = PROTO_FALSE;
    QuicCrypto.packet_protect(quic_crypto_work);
    size_t n = QuicCrypto.n;
    TEST_ASSERT_EQUAL_UINT(8u + 16u + 16u, n);
    TEST_ASSERT_TRUE(g_pkt[0] != 0x42); // the first octet is masked

    uint64_t pn = 0;
    QuicCrypto.packet_unprotect_args.pkt = g_pkt;
    QuicCrypto.packet_unprotect_args.pn_offset = 5;
    QuicCrypto.packet_unprotect_args.length = 3 + 16 + 16;
    QuicCrypto.packet_unprotect_args.largest_pn = 0x2700bff3ULL;
    QuicCrypto.packet_unprotect_args.keys = &s.server;
    QuicCrypto.packet_unprotect_args.is_long = PROTO_FALSE;
    QuicCrypto.packet_unprotect_args.out = g_plain;
    QuicCrypto.packet_unprotect_args.out_pn = &pn;
    QuicCrypto.packet_unprotect(quic_crypto_work);
    size_t pt = QuicCrypto.n;
    TEST_ASSERT_EQUAL_UINT(16u, pt);
    TEST_ASSERT_EQUAL_HEX64(0x2700bff4ULL, pn);
    TEST_ASSERT_EQUAL_MEMORY(payload, g_plain, sizeof(payload));
    TEST_ASSERT_EQUAL_HEX8(0x42, g_pkt[0]); // and unmasked again
}
