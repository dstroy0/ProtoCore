// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// transport/zlib.c (RFC 1950, RFC 1951, RFC 4253 sec 6.2): the compressor half of the
// zlib@openssh.com stream - the header that opens it, the history it keeps, and the bound a
// caller sizes its destination by.

#include "network_drivers/presentation/ssh/common.h"
#include "network_drivers/presentation/ssh/transport/inflate/inflate.h"
#include "network_drivers/presentation/ssh/transport/zlib/zlib.h"
#include <stdint.h>
#include <unity.h>

static uint8_t inflate_work[16]; // the borrow an entry takes; Inflate never reads it

static uint8_t zlib_work[16]; // the borrow an entry takes; Zlib never reads it

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
    ZlibV.packet_args.z = &s_def;
    ZlibV.packet_args.src = src;
    ZlibV.packet_args.src_len = len;
    ZlibV.packet_args.dst = dst;
    ZlibV.packet_args.dst_cap = cap;
    ZlibV.packet_args.out_len = &out;
    Zlib.packet(zlib_work);
    TEST_ASSERT_EQUAL_INT(0, ZlibV.n);
    return out;
}

// And back through the decompressor.
static size_t expand_one(const uint8_t *src, size_t len, uint8_t *dst, size_t cap)
{
    size_t out = 0;
    InflateV.packet_args.z = &s_inf;
    InflateV.packet_args.src = src;
    InflateV.packet_args.src_len = len;
    InflateV.packet_args.dst = dst;
    InflateV.packet_args.dst_cap = cap;
    InflateV.packet_args.out_len = &out;
    Inflate.packet(inflate_work);
    TEST_ASSERT_EQUAL_INT(0, InflateV.n);
    return out;
}

void setUp(void)
{

    ZlibV.init_args.z = &s_def;
    ZlibV.init_args.win = s_work;
    ZlibV.init_args.head = s_head;
    ZlibV.init_args.prev = s_prev;
    ZlibV.init_args.ll_code = s_ll_code;
    ZlibV.init_args.ll_len = s_ll_len;
    ZlibV.init_args.d_code = s_d_code;
    ZlibV.init_args.d_len = s_d_len;
    Zlib.init(zlib_work);
    InflateV.init_args.z = &s_inf;
    InflateV.init_args.window = s_window;
    Inflate.init(inflate_work);
}
void tearDown(void)
{
}

static void test_rfc1950_header_is_emitted_once_at_stream_start(void)
{
    const uint8_t msg[] = "the quick brown fox";
    uint8_t out[512];
    const size_t n = compress_one(msg, sizeof(msg) - 1, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_size_t(2u, n);

    // "CM ... identifies the compression method used in the file. CM = 8 denotes the "deflate"
    // compression method", in CMF bits 0 to 3.
    TEST_ASSERT_EQUAL_UINT8(8u, out[0] & 0x0Fu);

    // "CINFO is the base-2 logarithm of the LZ77 window size, minus eight (CINFO=7 indicates a 32K
    // window size). Values of CINFO above 7 are not allowed."
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(7u, (uint8_t)(out[0] >> 4));

    // "The FCHECK value must be such that CMF and FLG, when viewed as a 16-bit unsigned integer
    // stored in MSB order (CMF*256 + FLG), is a multiple of 31."
    const uint16_t hdr = (uint16_t)(((uint16_t)out[0] << 8) | out[1]);
    TEST_ASSERT_EQUAL_UINT16(0u, (uint16_t)(hdr % 31u));

    // FDICT is FLG bit 5; no preset dictionary is used, so no DICTID follows the two bytes.
    TEST_ASSERT_EQUAL_UINT8(0u, (uint8_t)((out[1] >> 5) & 1u));
}

// The header opens the stream, not each packet: a second packet continues it.
static void test_rfc1950_header_is_not_repeated_per_packet(void)
{
    const uint8_t msg[] = "abcabcabcabc";
    uint8_t first[512], second[512];
    (void)compress_one(msg, sizeof(msg) - 1, first, sizeof(first));
    const size_t n2 = compress_one(msg, sizeof(msg) - 1, second, sizeof(second));

    // A repeated header would put CM = 8 at the front of the second packet with a multiple-of-31
    // check word; the second packet instead opens mid-stream.
    const uint16_t hdr2 = (uint16_t)(((uint16_t)second[0] << 8) | second[1]);
    TEST_ASSERT_FALSE((second[0] & 0x0Fu) == 8u && (hdr2 % 31u) == 0u && n2 > 2u && second[0] == first[0] &&
                      second[1] == first[1]);
}

// "passed from one packet to the next" - the history is what makes a repeat of an earlier packet
// cheaper than the first time it was seen. That is the whole reason the context carries.
static void test_sec6_2_history_carries_across_packets(void)
{
    static const uint8_t msg[] = "a string long enough to be worth a back reference, twice over now";
    const size_t len = sizeof(msg) - 1;

    uint8_t c1[1024], c2[1024];
    const size_t n1 = compress_one(msg, len, c1, sizeof(c1));
    const size_t n2 = compress_one(msg, len, c2, sizeof(c2));

    // The second copy is matched against the first out of the window, so it costs less.
    TEST_ASSERT_LESS_THAN_size_t(n1, n2);
}

// A highly repetitive payload is where the back references pay, and it must still come back exact.
static void test_repetitive_payload_round_trips_and_shrinks(void)
{
    uint8_t src[1024];
    for (size_t k = 0; k < sizeof(src); k++)
    {
        src[k] = (uint8_t)('A' + (k % 4u));
    }
    uint8_t comp[4096], back[4096];
    const size_t cn = compress_one(src, sizeof(src), comp, sizeof(comp));
    const size_t bn = expand_one(comp, cn, back, sizeof(back));
    TEST_ASSERT_EQUAL_size_t(sizeof(src), bn);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(src, back, sizeof(src));
    TEST_ASSERT_LESS_THAN_size_t(sizeof(src), cn);
}

// ssh_deflate_bound is what a caller sizes its destination by, so it has to cover the worst case.
static void test_deflate_bound_covers_incompressible_input(void)
{
    uint8_t src[512];
    for (size_t k = 0; k < sizeof(src); k++)
    {
        src[k] = (uint8_t)((k * 61u + 7u) & 0xFFu);
    }
    uint8_t comp[4096];
    const size_t cn = compress_one(src, sizeof(src), comp, sizeof(comp));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(ssh_deflate_bound(sizeof(src)), cn);
}

// A destination too small is refused rather than half-written.
static void test_undersized_destination_is_refused(void)
{
    const uint8_t msg[] = "something that will not fit in four bytes";
    uint8_t comp[4];
    size_t out = 0;
    ZlibV.packet_args.z = &s_def;
    ZlibV.packet_args.src = msg;
    ZlibV.packet_args.src_len = sizeof(msg) - 1;
    ZlibV.packet_args.dst = comp;
    ZlibV.packet_args.dst_cap = sizeof(comp);
    ZlibV.packet_args.out_len = &out;
    Zlib.packet(zlib_work);
    TEST_ASSERT_NOT_EQUAL(0, ZlibV.n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rfc1950_header_is_emitted_once_at_stream_start);
    RUN_TEST(test_rfc1950_header_is_not_repeated_per_packet);
    RUN_TEST(test_sec6_2_history_carries_across_packets);
    RUN_TEST(test_repetitive_payload_round_trips_and_shrinks);
    RUN_TEST(test_deflate_bound_covers_incompressible_input);
    RUN_TEST(test_undersized_destination_is_refused);
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
