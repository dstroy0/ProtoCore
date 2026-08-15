// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// transport/inflate.c (RFC 1950, RFC 1951, RFC 4253 sec 6.2): the decompressor half of the
// zlib@openssh.com stream - what it gives back, the window it resolves back references out
// of, and what it refuses.

#include "network_drivers/presentation/ssh/transport/inflate.h"
#include "network_drivers/presentation/ssh/transport/zlib.h"
#include <stdint.h>
#include <unity.h>

#if PROTOCORE_ENABLE_SSH_ZLIB

static uint8_t s_work[SSH_ZLIB_WORK_SIZE];
static uint16_t s_head[SSH_ZLIB_HASH_SIZE];
static uint16_t s_prev[SSH_ZLIB_WORK_SIZE];
static uint16_t s_ll_code[288];
static uint8_t s_ll_len[288];
static uint16_t s_d_code[30];
static uint8_t s_d_len[30];
static uint8_t s_window[SSH_INFLATE_WINDOW];
static SshDeflate s_def;
static SshInflate s_inf;

// One packet through the compressor.
static size_t compress_one(const uint8_t *src, size_t len, uint8_t *dst, size_t cap)
{
    size_t out = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_deflate_packet(&s_def, src, len, dst, cap, &out));
    return out;
}

// And back through the decompressor.
static size_t expand_one(const uint8_t *src, size_t len, uint8_t *dst, size_t cap)
{
    size_t out = 0;
    TEST_ASSERT_EQUAL_INT(0, ssh_inflate_packet(&s_inf, src, len, dst, cap, &out));
    return out;
}

void setUp(void)
{

    ssh_deflate_init(&s_def, s_work, s_head, s_prev, s_ll_code, s_ll_len, s_d_code, s_d_len);
    ssh_inflate_init(&s_inf, s_window);
}
void tearDown(void)
{
}

static void test_sec6_2_one_packet_round_trips(void)
{
    const uint8_t msg[] = "SSH_MSG_CHANNEL_DATA and a little payload behind it";
    uint8_t comp[1024], back[1024];
    const size_t cn = compress_one(msg, sizeof(msg) - 1, comp, sizeof(comp));
    const size_t bn = expand_one(comp, cn, back, sizeof(back));

    TEST_ASSERT_EQUAL_size_t(sizeof(msg) - 1, bn);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, back, sizeof(msg) - 1);
}

// Each packet is decompressed on its own, in order, against the running stream.
static void test_sec6_2_successive_packets_round_trip_in_order(void)
{
    static const char *msgs[] = {"first packet", "second packet, a bit longer", "third",
                                 "fourth packet repeating first packet"};
    for (int k = 0; k < 4; k++)
    {
        const uint8_t *src = (const uint8_t *)msgs[k];
        size_t len = 0;
        while (src[len] != '\0')
        {
            len++;
        }
        uint8_t comp[1024], back[1024];
        const size_t cn = compress_one(src, len, comp, sizeof(comp));
        const size_t bn = expand_one(comp, cn, back, sizeof(back));
        TEST_ASSERT_EQUAL_size_t(len, bn);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(src, back, len);
    }
}

// The decompressor's window has to carry the same history, or the second packet's back-references
// resolve to nothing.
static void test_sec6_2_decompressor_history_resolves_back_references(void)
{
    static const uint8_t msg[] = "a string long enough to be worth a back reference, twice over now";
    const size_t len = sizeof(msg) - 1;

    uint8_t c1[1024], c2[1024], b1[1024], b2[1024];
    const size_t n1 = compress_one(msg, len, c1, sizeof(c1));
    const size_t n2 = compress_one(msg, len, c2, sizeof(c2));
    const size_t r1 = expand_one(c1, n1, b1, sizeof(b1));
    const size_t r2 = expand_one(c2, n2, b2, sizeof(b2));

    TEST_ASSERT_EQUAL_size_t(len, r1);
    TEST_ASSERT_EQUAL_size_t(len, r2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, b1, len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, b2, len); // the repeat came out of the window
}

// "The compression context is initialized after each key exchange" - a re-initialized pair starts a
// fresh stream, header and all, and the old history is gone.
static void test_sec6_2_re_initialization_starts_a_fresh_stream(void)
{
    const uint8_t msg[] = "payload that will be sent either side of a key exchange";
    const size_t len = sizeof(msg) - 1;
    uint8_t comp[1024], back[1024];

    (void)compress_one(msg, len, comp, sizeof(comp));

    // Both ends re-initialize, as a key exchange makes them.
    ssh_deflate_init(&s_def, s_work, s_head, s_prev, s_ll_code, s_ll_len, s_d_code, s_d_len);
    ssh_inflate_init(&s_inf, s_window);

    const size_t cn = compress_one(msg, len, comp, sizeof(comp));
    TEST_ASSERT_EQUAL_UINT8(8u, comp[0] & 0x0Fu); // the RFC 1950 header opens the new stream
    const uint16_t hdr = (uint16_t)(((uint16_t)comp[0] << 8) | comp[1]);
    TEST_ASSERT_EQUAL_UINT16(0u, (uint16_t)(hdr % 31u));

    const size_t bn = expand_one(comp, cn, back, sizeof(back));
    TEST_ASSERT_EQUAL_size_t(len, bn);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, back, len);
}

// An empty payload is a whole packet: the flush block still has to come out and go back in.
static void test_empty_payload_round_trips(void)
{
    uint8_t comp[64], back[64];
    const size_t cn = compress_one((const uint8_t *)"", 0, comp, sizeof(comp));
    const size_t bn = expand_one(comp, cn, back, sizeof(back));
    TEST_ASSERT_EQUAL_size_t(0u, bn);
}

// Incompressible bytes must still survive the trip, whatever they cost.
static void test_incompressible_payload_round_trips(void)
{
    uint8_t src[256];
    for (size_t k = 0; k < sizeof(src); k++)
    {
        src[k] = (uint8_t)((k * 37u + 11u) & 0xFFu); // no repeats to match
    }
    uint8_t comp[2048], back[2048];
    const size_t cn = compress_one(src, sizeof(src), comp, sizeof(comp));
    const size_t bn = expand_one(comp, cn, back, sizeof(back));
    TEST_ASSERT_EQUAL_size_t(sizeof(src), bn);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(src, back, sizeof(src));
}

// Every byte value has to survive, including the NUL that a string-shaped test would stop at.
static void test_all_byte_values_round_trip(void)
{
    uint8_t src[256];
    for (size_t k = 0; k < sizeof(src); k++)
    {
        src[k] = (uint8_t)k;
    }
    uint8_t comp[2048], back[2048];
    const size_t cn = compress_one(src, sizeof(src), comp, sizeof(comp));
    const size_t bn = expand_one(comp, cn, back, sizeof(back));
    TEST_ASSERT_EQUAL_size_t(sizeof(src), bn);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(src, back, sizeof(src));
}

// Bytes that are not a zlib stream are refused rather than decoded into the caller's buffer.
static void test_garbage_input_is_refused(void)
{
    uint8_t junk[32];
    for (size_t k = 0; k < sizeof(junk); k++)
    {
        junk[k] = (uint8_t)(0xA5u ^ k);
    }
    uint8_t back[256];
    size_t out = 0;
    TEST_ASSERT_NOT_EQUAL(0, ssh_inflate_packet(&s_inf, junk, sizeof(junk), back, sizeof(back), &out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sec6_2_one_packet_round_trips);
    RUN_TEST(test_sec6_2_successive_packets_round_trip_in_order);
    RUN_TEST(test_sec6_2_decompressor_history_resolves_back_references);
    RUN_TEST(test_sec6_2_re_initialization_starts_a_fresh_stream);
    RUN_TEST(test_empty_payload_round_trips);
    RUN_TEST(test_incompressible_payload_round_trips);
    RUN_TEST(test_all_byte_values_round_trip);
    RUN_TEST(test_garbage_input_is_refused);
    return UNITY_END();
}

#else // PROTOCORE_ENABLE_SSH_ZLIB

void setUp(void)
{
}
void tearDown(void)
{
}

static void test_this_configuration_does_not_build_it(void)
{
    TEST_IGNORE_MESSAGE("PROTOCORE_ENABLE_SSH_ZLIB is off");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_this_configuration_does_not_build_it);
    return UNITY_END();
}

#endif
