// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the ESP packet transform (services/system/esp/esp.h).
//
// RFC 4303 sec 2 fixes the packet: SPI, Sequence Number, the explicit IV RFC 4106 puts in the
// Payload Data, then Padding, Pad Length and Next Header, then the ICV. sec 2.4's second bullet
// requires Pad Length and Next Header to be right-aligned in a 4-byte word, and names the default
// padding contents as the monotonic sequence 1, 2, 3, .... test_rfc4303_packet_layout is the
// load-bearing case: it reads every cleartext field off the wire at the offset the RFC assigns it
// and checks the padding the RFC prescribes, so no expectation here comes from running the codec.
// The ciphertext itself is checked by the property an AEAD must have - any single flipped bit
// anywhere in the packet must fail the ICV.

#include "services/system/esp/esp.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t KEY[PROTOCORE_ESP_KEY_LEN] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                                   0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                                   0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
static const uint8_t SALT[PROTOCORE_ESP_SALT_LEN] = {0xca, 0xfe, 0xba, 0xbe};
static const uint8_t IV[PROTOCORE_ESP_IV_LEN] = {0xfa, 0xce, 0xdb, 0xad, 0xde, 0xca, 0xf8, 0x88};

// RFC 4303 sec 2.4: the Pad Length and Next Header fields are right-aligned in a 4-byte word, and
// the padding computation covers the Payload Data inclusive of the IV. The IV is 8 octets, so the
// pad count is whatever brings payload + 2 up to a multiple of four.
static size_t pad_for(size_t payload_len)
{
    return (4u - ((payload_len + 2u) & 3u)) & 3u;
}

// RFC 4303 sec 2: SPI(4) | Seq(4) | IV(8) | ciphertext | ICV(16).
static size_t packet_len_for(size_t payload_len)
{
    return PROTOCORE_ESP_HDR_LEN + PROTOCORE_ESP_IV_LEN + payload_len + pad_for(payload_len) + 2 +
           PROTOCORE_ESP_ICV_LEN;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// The load-bearing case: the cleartext fields sit where RFC 4303 sec 2 puts them, the packet is the
// length sec 2.4's alignment rule implies, and the trailer carries the padding the RFC prescribes.
void test_rfc4303_packet_layout(void)
{
    static const size_t LENGTHS[8] = {0, 1, 2, 3, 4, 5, 16, 64};
    for (unsigned i = 0; i < 8; i++)
    {
        const size_t plen = LENGTHS[i];
        uint8_t payload[64];
        for (size_t k = 0; k < plen; k++)
        {
            payload[k] = (uint8_t)(0xA0 + k);
        }

        uint8_t packet[192];
        size_t n = protocore_esp_gcm_encapsulate(0x11223344u, 0x00000001u, KEY, SALT, IV, 4, plen ? payload : NULL,
                                                 plen, packet, sizeof(packet));
        TEST_ASSERT_EQUAL_UINT32((uint32_t)packet_len_for(plen), (uint32_t)n);

        // sec 2.1 SPI and sec 2.2 Sequence Number are on the wire in network byte order.
        TEST_ASSERT_EQUAL_HEX32(0x11223344u, be32(packet));
        TEST_ASSERT_EQUAL_HEX32(0x00000001u, be32(packet + 4));
        // RFC 4106 sec 3: the explicit IV follows the ESP header, in the clear.
        TEST_ASSERT_EQUAL_HEX8_ARRAY(IV, packet + PROTOCORE_ESP_HDR_LEN, PROTOCORE_ESP_IV_LEN);
        // The ciphertext ends on a 4-byte boundary so the ICV does too.
        TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)((n - PROTOCORE_ESP_ICV_LEN) % 4u));

        uint32_t spi = 0;
        uint32_t seq = 0;
        uint8_t next_header = 0;
        const uint8_t *out = NULL;
        size_t out_len = 0;
        TEST_ASSERT_TRUE(protocore_esp_gcm_decapsulate(KEY, SALT, packet, n, &spi, &seq, &next_header, &out, &out_len));
        TEST_ASSERT_EQUAL_HEX32(0x11223344u, spi);
        TEST_ASSERT_EQUAL_HEX32(0x00000001u, seq);
        TEST_ASSERT_EQUAL_UINT8(4, next_header);
        TEST_ASSERT_EQUAL_UINT32((uint32_t)plen, (uint32_t)out_len);
        if (plen)
        {
            TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, out, plen);
        }

        // sec 2.4: "The first padding byte appended to the plaintext is numbered 1, with subsequent
        // padding bytes making up a monotonically increasing sequence: 1, 2, 3, ...", then the Pad
        // Length and the Next Header.
        const uint8_t *trailer = out + plen;
        const size_t pad = pad_for(plen);
        for (size_t k = 0; k < pad; k++)
        {
            TEST_ASSERT_EQUAL_UINT8((uint8_t)(k + 1), trailer[k]);
        }
        TEST_ASSERT_EQUAL_UINT8((uint8_t)pad, trailer[pad]);
        TEST_ASSERT_EQUAL_UINT8(4, trailer[pad + 1]);
    }
}

// The transform carries the SPI, the sequence number and the Next Header value it was given, over
// their whole ranges.
void test_header_fields_round_trip(void)
{
    static const uint32_t SPIS[4] = {0x00000001u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
    static const uint32_t SEQS[4] = {1u, 2u, 0x7FFFFFFFu, 0xFFFFFFFFu};
    static const uint8_t NEXT[4] = {4, 6, 41, 59}; // IPv4, TCP, IPv6, no next header

    static const uint8_t PAYLOAD[5] = {1, 2, 3, 4, 5};
    for (unsigned i = 0; i < 4; i++)
    {
        uint8_t packet[128];
        size_t n = protocore_esp_gcm_encapsulate(SPIS[i], SEQS[i], KEY, SALT, IV, NEXT[i], PAYLOAD, sizeof(PAYLOAD),
                                                 packet, sizeof(packet));
        TEST_ASSERT_EQUAL_UINT32((uint32_t)packet_len_for(sizeof(PAYLOAD)), (uint32_t)n);
        TEST_ASSERT_EQUAL_HEX32(SPIS[i], be32(packet));
        TEST_ASSERT_EQUAL_HEX32(SEQS[i], be32(packet + 4));

        uint32_t spi = 0;
        uint32_t seq = 0;
        uint8_t nh = 0;
        const uint8_t *out = NULL;
        size_t out_len = 0;
        TEST_ASSERT_TRUE(protocore_esp_gcm_decapsulate(KEY, SALT, packet, n, &spi, &seq, &nh, &out, &out_len));
        TEST_ASSERT_EQUAL_HEX32(SPIS[i], spi);
        TEST_ASSERT_EQUAL_HEX32(SEQS[i], seq);
        TEST_ASSERT_EQUAL_UINT8(NEXT[i], nh);
        TEST_ASSERT_EQUAL_UINT32(5, (uint32_t)out_len);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(PAYLOAD, out, 5);
    }
}

// The AEAD property RFC 4303 sec 3.4.4 depends on: every octet of the packet is authenticated, so
// flipping any single bit anywhere - header, IV, ciphertext or ICV - must fail the check.
void test_every_bit_is_authenticated(void)
{
    static const uint8_t PAYLOAD[8] = {0xde, 0xad, 0xbe, 0xef, 0xfe, 0xed, 0xfa, 0xce};
    uint8_t good[128];
    size_t n =
        protocore_esp_gcm_encapsulate(0x0a0b0c0du, 7u, KEY, SALT, IV, 4, PAYLOAD, sizeof(PAYLOAD), good, sizeof(good));
    TEST_ASSERT_GREATER_THAN_UINT32(0, (uint32_t)n);

    for (size_t byte = 0; byte < n; byte++)
    {
        for (unsigned bit = 0; bit < 8; bit += 3) // bits 0, 3, 6 of every octet
        {
            uint8_t packet[128];
            memcpy(packet, good, n);
            packet[byte] ^= (uint8_t)(1u << bit);

            uint32_t spi = 0;
            uint32_t seq = 0;
            uint8_t nh = 0;
            const uint8_t *out = (const uint8_t *)1;
            size_t out_len = 99;
            TEST_ASSERT_FALSE(protocore_esp_gcm_decapsulate(KEY, SALT, packet, n, &spi, &seq, &nh, &out, &out_len));
        }
    }
}

// The wrong key or the wrong salt is the wrong SA: neither can open the packet.
void test_a_different_key_or_salt_cannot_open_the_packet(void)
{
    static const uint8_t PAYLOAD[4] = {1, 2, 3, 4};
    uint8_t packet[128];
    uint8_t scratch[128];
    size_t n =
        protocore_esp_gcm_encapsulate(1u, 1u, KEY, SALT, IV, 4, PAYLOAD, sizeof(PAYLOAD), packet, sizeof(packet));

    uint8_t other_key[PROTOCORE_ESP_KEY_LEN];
    memcpy(other_key, KEY, sizeof(other_key));
    other_key[31] ^= 0x01;
    uint8_t other_salt[PROTOCORE_ESP_SALT_LEN];
    memcpy(other_salt, SALT, sizeof(other_salt));
    other_salt[0] ^= 0x01;

    uint32_t spi = 0;
    uint32_t seq = 0;
    uint8_t nh = 0;
    const uint8_t *out = NULL;
    size_t out_len = 0;

    memcpy(scratch, packet, n);
    TEST_ASSERT_FALSE(protocore_esp_gcm_decapsulate(other_key, SALT, scratch, n, &spi, &seq, &nh, &out, &out_len));
    memcpy(scratch, packet, n);
    TEST_ASSERT_FALSE(protocore_esp_gcm_decapsulate(KEY, other_salt, scratch, n, &spi, &seq, &nh, &out, &out_len));
    memcpy(scratch, packet, n);
    TEST_ASSERT_TRUE(protocore_esp_gcm_decapsulate(KEY, SALT, scratch, n, &spi, &seq, &nh, &out, &out_len));
}

// The nonce is the salt concatenated with the explicit IV (RFC 4106 sec 4), so two packets built
// with different IVs under one key differ in their ciphertext even for identical plaintext.
void test_the_iv_selects_the_nonce(void)
{
    static const uint8_t PAYLOAD[16] = {0};
    uint8_t iv2[PROTOCORE_ESP_IV_LEN];
    memcpy(iv2, IV, sizeof(iv2));
    iv2[7] ^= 0x01;

    uint8_t a[128];
    uint8_t b[128];
    size_t na = protocore_esp_gcm_encapsulate(1u, 1u, KEY, SALT, IV, 4, PAYLOAD, sizeof(PAYLOAD), a, sizeof(a));
    size_t nb = protocore_esp_gcm_encapsulate(1u, 1u, KEY, SALT, iv2, 4, PAYLOAD, sizeof(PAYLOAD), b, sizeof(b));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)na, (uint32_t)nb);
    const size_t ct = PROTOCORE_ESP_HDR_LEN + PROTOCORE_ESP_IV_LEN;
    TEST_ASSERT_TRUE(memcmp(a + ct, b + ct, na - ct) != 0);

    // The same IV under the same key and sequence number is deterministic.
    uint8_t again[128];
    TEST_ASSERT_EQUAL_UINT32((uint32_t)na,
                             (uint32_t)protocore_esp_gcm_encapsulate(1u, 1u, KEY, SALT, IV, 4, PAYLOAD, sizeof(PAYLOAD),
                                                                     again, sizeof(again)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, again, na);
}

// SPI and Sequence Number are the additional authenticated data, so a packet whose header is edited
// after the fact no longer verifies even though the ciphertext is untouched.
void test_header_is_additional_authenticated_data(void)
{
    static const uint8_t PAYLOAD[4] = {9, 9, 9, 9};
    uint8_t packet[128];
    size_t n = protocore_esp_gcm_encapsulate(0x01020304u, 5u, KEY, SALT, IV, 4, PAYLOAD, sizeof(PAYLOAD), packet,
                                             sizeof(packet));
    packet[3] ^= 0xFF; // rewrite the low SPI octet only

    uint32_t spi = 0;
    uint32_t seq = 0;
    uint8_t nh = 0;
    const uint8_t *out = NULL;
    size_t out_len = 0;
    TEST_ASSERT_FALSE(protocore_esp_gcm_decapsulate(KEY, SALT, packet, n, &spi, &seq, &nh, &out, &out_len));
}

// A packet shorter than the fields RFC 4303 sec 2 requires cannot be one, and an output buffer
// shorter than the packet is refused rather than partially written.
void test_bounds_are_refused(void)
{
    static const uint8_t PAYLOAD[4] = {1, 2, 3, 4};
    uint8_t packet[128];
    const size_t want = packet_len_for(sizeof(PAYLOAD));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)protocore_esp_gcm_encapsulate(1u, 1u, KEY, SALT, IV, 4, PAYLOAD,
                                                                        sizeof(PAYLOAD), packet, want - 1));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)want, (uint32_t)protocore_esp_gcm_encapsulate(1u, 1u, KEY, SALT, IV, 4, PAYLOAD,
                                                                                     sizeof(PAYLOAD), packet, want));

    uint32_t spi = 0;
    uint32_t seq = 0;
    uint8_t nh = 0;
    const uint8_t *out = NULL;
    size_t out_len = 0;
    // Header + IV + ICV with no room for even the Pad Length and Next Header.
    uint8_t tiny[PROTOCORE_ESP_HDR_LEN + PROTOCORE_ESP_IV_LEN + PROTOCORE_ESP_ICV_LEN] = {0};
    TEST_ASSERT_FALSE(protocore_esp_gcm_decapsulate(KEY, SALT, tiny, sizeof(tiny), &spi, &seq, &nh, &out, &out_len));
    TEST_ASSERT_FALSE(protocore_esp_gcm_decapsulate(KEY, SALT, tiny, 0, &spi, &seq, &nh, &out, &out_len));
    // A truncated but otherwise valid packet fails too.
    TEST_ASSERT_FALSE(protocore_esp_gcm_decapsulate(KEY, SALT, packet, want - 1, &spi, &seq, &nh, &out, &out_len));
}

// --- RFC 4303 sec 3.4.3 anti-replay ------------------------------------------------------------

// sec 3.3.3: "the first packet sent using a given SA will contain a sequence number of 1", so 0 is
// never a valid received sequence number.
void test_replay_rejects_sequence_zero(void)
{
    EspReplay r;
    protocore_esp_replay_init(&r);
    TEST_ASSERT_FALSE(protocore_esp_replay_check(&r, 0u));
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, 1u));
    TEST_ASSERT_FALSE(protocore_esp_replay_check(&r, 0u));
}

// sec 3.4.3: the receiver "MUST verify that the packet contains a Sequence Number that does not
// duplicate the Sequence Number of any other packets received during the life of this SA".
void test_replay_rejects_a_duplicate(void)
{
    EspReplay r;
    protocore_esp_replay_init(&r);
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, 1u));
    TEST_ASSERT_FALSE(protocore_esp_replay_check(&r, 1u));
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, 2u));
    TEST_ASSERT_FALSE(protocore_esp_replay_check(&r, 2u));
    TEST_ASSERT_FALSE(protocore_esp_replay_check(&r, 1u));
}

// A packet that arrives out of order but still inside the window is accepted once, and only once.
void test_replay_accepts_reordering_inside_the_window(void)
{
    EspReplay r;
    protocore_esp_replay_init(&r);
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, 10u));
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, 7u));
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, 9u));
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, 8u));
    TEST_ASSERT_FALSE(protocore_esp_replay_check(&r, 9u));
    TEST_ASSERT_FALSE(protocore_esp_replay_check(&r, 10u));
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, 11u));
}

// The window is exactly PROTOCORE_ESP_REPLAY_WINDOW packets wide: with the highest at N, N-63 is
// still inside it and N-64 has fallen off the left edge.
void test_replay_window_width(void)
{
    EspReplay r;
    protocore_esp_replay_init(&r);
    const uint32_t top = 1000u;
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, top));
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, top - (PROTOCORE_ESP_REPLAY_WINDOW - 1)));
    TEST_ASSERT_FALSE(protocore_esp_replay_check(&r, top - PROTOCORE_ESP_REPLAY_WINDOW));
    TEST_ASSERT_FALSE(protocore_esp_replay_check(&r, top - PROTOCORE_ESP_REPLAY_WINDOW - 1));
    TEST_ASSERT_FALSE(protocore_esp_replay_check(&r, 1u));
}

// Advancing the window past its own width discards the old bitmap rather than leaving stale bits: a
// jump forward must not make an unseen sequence number look like a duplicate.
void test_replay_window_advances_cleanly(void)
{
    EspReplay r;
    protocore_esp_replay_init(&r);
    for (uint32_t s = 1; s <= 20; s++)
    {
        TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, s));
    }
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, 5000u)); // a large jump forward
    for (uint32_t s = 5000u - (PROTOCORE_ESP_REPLAY_WINDOW - 1); s < 5000u; s++)
    {
        TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, s)); // none of these was ever seen
    }
    for (uint32_t s = 5000u - (PROTOCORE_ESP_REPLAY_WINDOW - 1); s <= 5000u; s++)
    {
        TEST_ASSERT_FALSE(protocore_esp_replay_check(&r, s)); // now every one is a duplicate
    }
}

// A monotone stream is accepted end to end, which is the ordinary case the window must never break.
void test_replay_accepts_a_monotone_stream(void)
{
    EspReplay r;
    protocore_esp_replay_init(&r);
    for (uint32_t s = 1; s <= 500u; s++)
    {
        TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, s));
    }
    for (uint32_t s = 500u; s > 500u - PROTOCORE_ESP_REPLAY_WINDOW; s--)
    {
        TEST_ASSERT_FALSE(protocore_esp_replay_check(&r, s));
    }
}

// A window that was never initialized to a packet accepts the first one it sees, whatever its
// sequence number, because sec 3.4.3 starts the counter at zero when the SA is established.
void test_replay_first_packet_may_be_any_sequence(void)
{
    EspReplay r;
    protocore_esp_replay_init(&r);
    TEST_ASSERT_FALSE(r.seen_any);
    TEST_ASSERT_TRUE(protocore_esp_replay_check(&r, 0xFFFFFFFFu));
    TEST_ASSERT_TRUE(r.seen_any);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, r.highest);
    TEST_ASSERT_FALSE(protocore_esp_replay_check(&r, 0xFFFFFFFFu));
}
