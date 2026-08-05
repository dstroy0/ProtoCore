// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the ESP (RFC 4303) packet transform with AES-256-GCM (RFC 4106), services/esp:
// encapsulate/decapsulate a payload, cross-checked against an independent Python AES-256-GCM reference.

#include "services/system/esp/esp.h"

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// The golden packet is the same key/salt/IV/AAD as the SK-AEAD KAT, so a shared AES-256-GCM impl is
// exercised; the ESP framing (SPI|Seq header, padding, trailer) is what this test pins.
static const uint8_t esp_key[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                                    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
static const uint8_t esp_salt[4] = {0xde, 0xad, 0xbe, 0xef};
static const uint8_t esp_iv[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
static const uint8_t esp_payload[20] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
                                        0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13};
static const uint8_t esp_packet[56] = {
    0x12, 0x34, 0x56, 0x78, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xb4, 0xca, 0x1c,
    0x59, 0x78, 0xce, 0xd0, 0x01, 0xce, 0x53, 0x80, 0x01, 0xe2, 0xf1, 0x48, 0x4b, 0x48, 0xc0, 0x4e, 0x47, 0xa7, 0x4b,
    0x23, 0xef, 0xe5, 0x41, 0x56, 0xed, 0x7e, 0xab, 0x3f, 0xb8, 0xc7, 0x08, 0x8f, 0x3d, 0x02, 0x6b, 0xa7, 0x05};

void test_esp_encapsulate_kat()
{
    uint8_t out[128];
    size_t n = pc_esp_gcm_encapsulate(0x12345678, 1, esp_key, esp_salt, esp_iv, /*next_header=*/4, esp_payload,
                                      sizeof(esp_payload), out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(56, n); // 8 hdr + 8 IV + 24 ct (20 payload + 2 pad + padlen + nexthdr) + 16 ICV
    TEST_ASSERT_EQUAL_MEMORY(esp_packet, out, 56);
}

void test_esp_roundtrip()
{
    uint8_t pkt[128];
    size_t n = pc_esp_gcm_encapsulate(0xAABBCCDD, 42, esp_key, esp_salt, esp_iv, 41 /*IPv6*/, esp_payload,
                                      sizeof(esp_payload), pkt, sizeof(pkt));
    TEST_ASSERT_TRUE(n > 0);

    uint32_t spi = 0, seq = 0;
    uint8_t nh = 0;
    const uint8_t *pl = NULL;
    size_t pl_len = 0;
    TEST_ASSERT_TRUE(pc_esp_gcm_decapsulate(esp_key, esp_salt, pkt, n, &spi, &seq, &nh, &pl, &pl_len));
    TEST_ASSERT_EQUAL_HEX32(0xAABBCCDD, spi);
    TEST_ASSERT_EQUAL_UINT32(42, seq);
    TEST_ASSERT_EQUAL_UINT8(41, nh);
    TEST_ASSERT_EQUAL_size_t(sizeof(esp_payload), pl_len);
    TEST_ASSERT_EQUAL_MEMORY(esp_payload, pl, sizeof(esp_payload));
}

void test_esp_decapsulate_golden()
{
    uint8_t work[64];
    memcpy(work, esp_packet, sizeof(esp_packet));
    uint32_t spi = 0, seq = 0;
    uint8_t nh = 0;
    const uint8_t *pl = NULL;
    size_t pl_len = 0;
    TEST_ASSERT_TRUE(
        pc_esp_gcm_decapsulate(esp_key, esp_salt, work, sizeof(esp_packet), &spi, &seq, &nh, &pl, &pl_len));
    TEST_ASSERT_EQUAL_HEX32(0x12345678, spi);
    TEST_ASSERT_EQUAL_UINT32(1, seq);
    TEST_ASSERT_EQUAL_UINT8(4, nh);
    TEST_ASSERT_EQUAL_size_t(20, pl_len);
    TEST_ASSERT_EQUAL_MEMORY(esp_payload, pl, 20);
}

void test_esp_tamper_and_guards()
{
    uint8_t work[64];
    uint32_t spi, seq;
    uint8_t nh;
    const uint8_t *pl;
    size_t pl_len;
    // A flipped ciphertext byte -> ICV fails.
    memcpy(work, esp_packet, sizeof(esp_packet));
    work[20] ^= 0x01;
    TEST_ASSERT_FALSE(
        pc_esp_gcm_decapsulate(esp_key, esp_salt, work, sizeof(esp_packet), &spi, &seq, &nh, &pl, &pl_len));
    // A flipped AAD byte (an SPI octet) -> ICV fails.
    memcpy(work, esp_packet, sizeof(esp_packet));
    work[0] ^= 0x01;
    TEST_ASSERT_FALSE(
        pc_esp_gcm_decapsulate(esp_key, esp_salt, work, sizeof(esp_packet), &spi, &seq, &nh, &pl, &pl_len));
    // A too-short packet fails closed; a too-small encap buffer returns 0.
    TEST_ASSERT_FALSE(pc_esp_gcm_decapsulate(esp_key, esp_salt, work, 20, &spi, &seq, &nh, &pl, &pl_len));
    uint8_t small[16];
    TEST_ASSERT_EQUAL_size_t(0, pc_esp_gcm_encapsulate(1, 1, esp_key, esp_salt, esp_iv, 4, esp_payload,
                                                       sizeof(esp_payload), small, sizeof(small)));
}

// A zero-length payload still yields a valid ESP packet (just the padded trailer) and round-trips.
void test_esp_empty_payload()
{
    uint8_t pkt[64];
    size_t n =
        pc_esp_gcm_encapsulate(7, 7, esp_key, esp_salt, esp_iv, 59 /*no-next-header*/, NULL, 0, pkt, sizeof(pkt));
    TEST_ASSERT_TRUE(n > 0);
    uint32_t spi, seq;
    uint8_t nh;
    const uint8_t *pl;
    size_t pl_len;
    TEST_ASSERT_TRUE(pc_esp_gcm_decapsulate(esp_key, esp_salt, pkt, n, &spi, &seq, &nh, &pl, &pl_len));
    TEST_ASSERT_EQUAL_size_t(0, pl_len);
    TEST_ASSERT_EQUAL_UINT8(59, nh);
}

// ── ESP anti-replay window (RFC 4303 §3.4.3) ───────────────────────────────────────────────────
void test_esp_replay_window()
{
    EspReplay r;
    pc_esp_replay_init(&r);

    // Sequence 0 is invalid; in-order packets are accepted once each; a duplicate is a replay.
    TEST_ASSERT_FALSE(pc_esp_replay_check(&r, 0));
    TEST_ASSERT_TRUE(pc_esp_replay_check(&r, 1));
    TEST_ASSERT_TRUE(pc_esp_replay_check(&r, 2));
    TEST_ASSERT_TRUE(pc_esp_replay_check(&r, 3));
    TEST_ASSERT_FALSE(pc_esp_replay_check(&r, 2)); // replay
    TEST_ASSERT_FALSE(pc_esp_replay_check(&r, 3)); // replay (the current highest)

    // Out-of-order but inside the window is accepted; re-delivery is rejected.
    TEST_ASSERT_TRUE(pc_esp_replay_check(&r, 5));  // new highest (gap at 4)
    TEST_ASSERT_TRUE(pc_esp_replay_check(&r, 4));  // fills the gap, inside the window
    TEST_ASSERT_FALSE(pc_esp_replay_check(&r, 4)); // replay
    TEST_ASSERT_FALSE(pc_esp_replay_check(&r, 1)); // replay of an old one still in the window

    // A jump forward slides the window; packets now left of it are too old.
    TEST_ASSERT_TRUE(pc_esp_replay_check(&r, 100));                              // new highest
    TEST_ASSERT_FALSE(pc_esp_replay_check(&r, 100));                             // replay
    TEST_ASSERT_TRUE(pc_esp_replay_check(&r, 99));                               // inside (offset 1)
    TEST_ASSERT_TRUE(pc_esp_replay_check(&r, 100 - (PC_ESP_REPLAY_WINDOW - 1))); // the left edge, inside
    TEST_ASSERT_FALSE(pc_esp_replay_check(&r, 100 - PC_ESP_REPLAY_WINDOW));      // one past the edge -> too old
    TEST_ASSERT_FALSE(pc_esp_replay_check(&r, 5));                               // far left -> too old
}

void test_esp_replay_large_jump()
{
    // A jump larger than the window clears the bitmap: previously-seen numbers in the old window are now
    // "too old" (outside the new window), and the new highest is the only accepted entry.
    EspReplay r;
    pc_esp_replay_init(&r);
    TEST_ASSERT_TRUE(pc_esp_replay_check(&r, 10));
    TEST_ASSERT_TRUE(pc_esp_replay_check(&r, 11));
    TEST_ASSERT_TRUE(pc_esp_replay_check(&r, 1000));  // jump >> window
    TEST_ASSERT_FALSE(pc_esp_replay_check(&r, 1000)); // replay
    TEST_ASSERT_TRUE(pc_esp_replay_check(&r, 999));   // inside the new window
    TEST_ASSERT_FALSE(pc_esp_replay_check(&r, 11));   // now far too old
    // Null guard.
    TEST_ASSERT_FALSE(pc_esp_replay_check(NULL, 1));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_esp_encapsulate_kat);
    RUN_TEST(test_esp_roundtrip);
    RUN_TEST(test_esp_decapsulate_golden);
    RUN_TEST(test_esp_tamper_and_guards);
    RUN_TEST(test_esp_empty_payload);
    RUN_TEST(test_esp_replay_window);
    RUN_TEST(test_esp_replay_large_jump);
    return UNITY_END();
}
