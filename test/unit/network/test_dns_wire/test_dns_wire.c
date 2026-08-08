// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The DNS name codec (network_drivers/network/dns/dns_wire, RFC 1035 sec 3.1 / 4.1.4): labels to a
// dotted string and back, compression pointers followed or refused, the hop limit that makes a
// self-referential message terminate, the label and buffer bounds, and case-insensitive compare.

#include "network_drivers/network/dns/dns_wire.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// --- decode -------------------------------------------------------------------------------------

void test_decode_single_label()
{
    const uint8_t pkt[] = {3, 'f', 'o', 'o', 0};
    char name[64];
    size_t next = 0;
    TEST_ASSERT_TRUE(pc_dns_name_decode(pkt, sizeof(pkt), 0, name, sizeof(name), &next, PROTO_FALSE));
    TEST_ASSERT_EQUAL_STRING("foo", name);
    TEST_ASSERT_EQUAL_UINT(5, next); // past the root byte
}

void test_decode_multi_label()
{
    const uint8_t pkt[] = {5, 'm', 'y', 'h', 's', 't', 5, 'l', 'o', 'c', 'a', 'l', 0};
    char name[64];
    size_t next = 0;
    TEST_ASSERT_TRUE(pc_dns_name_decode(pkt, sizeof(pkt), 0, name, sizeof(name), &next, PROTO_FALSE));
    TEST_ASSERT_EQUAL_STRING("myhst.local", name);
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt), next);
}

// The root alone is the empty name, and it consumes its one byte.
void test_decode_root_is_empty()
{
    const uint8_t pkt[] = {0};
    char name[8];
    size_t next = 0;
    TEST_ASSERT_TRUE(pc_dns_name_decode(pkt, sizeof(pkt), 0, name, sizeof(name), &next, PROTO_FALSE));
    TEST_ASSERT_EQUAL_STRING("", name);
    TEST_ASSERT_EQUAL_UINT(1, next);
}

// A name whose length byte runs past the buffer, and one with no root terminator at all.
void test_decode_truncated()
{
    char name[64];
    const uint8_t runs_past[] = {9, 'f', 'o', 'o'};
    TEST_ASSERT_FALSE(pc_dns_name_decode(runs_past, sizeof(runs_past), 0, name, sizeof(name), NULL, PROTO_FALSE));
    const uint8_t no_root[] = {3, 'f', 'o', 'o'};
    TEST_ASSERT_FALSE(pc_dns_name_decode(no_root, sizeof(no_root), 0, name, sizeof(name), NULL, PROTO_FALSE));
}

// 01 and 10 in the top two bits are not label types RFC 1035 defines.
void test_decode_undefined_label_type()
{
    char name[64];
    const uint8_t forty[] = {0x40, 'x', 0};
    TEST_ASSERT_FALSE(pc_dns_name_decode(forty, sizeof(forty), 0, name, sizeof(name), NULL, PROTO_TRUE));
    const uint8_t eighty[] = {0x80, 'x', 0};
    TEST_ASSERT_FALSE(pc_dns_name_decode(eighty, sizeof(eighty), 0, name, sizeof(name), NULL, PROTO_TRUE));
}

// The dotted name has to fit the caller's buffer, separators included.
void test_decode_out_cap()
{
    const uint8_t pkt[] = {3, 'a', 'b', 'c', 3, 'd', 'e', 'f', 0}; // "abc.def" is 7 + NUL
    char exact[8];
    TEST_ASSERT_TRUE(pc_dns_name_decode(pkt, sizeof(pkt), 0, exact, sizeof(exact), NULL, PROTO_FALSE));
    TEST_ASSERT_EQUAL_STRING("abc.def", exact);
    char tight[7];
    TEST_ASSERT_FALSE(pc_dns_name_decode(pkt, sizeof(pkt), 0, tight, sizeof(tight), NULL, PROTO_FALSE));
}

// A pointer is a decode failure for a question and a hop for an answer. Same bytes, both ways.
void test_decode_pointer_refused_and_followed()
{
    // offset 0: "local" root. offset 7: "www" then a pointer back to 0.
    const uint8_t pkt[] = {5, 'l', 'o', 'c', 'a', 'l', 0, 3, 'w', 'w', 'w', 0xC0, 0x00};
    char name[64];
    size_t next = 0;

    TEST_ASSERT_FALSE(pc_dns_name_decode(pkt, sizeof(pkt), 7, name, sizeof(name), &next, PROTO_FALSE));

    TEST_ASSERT_TRUE(pc_dns_name_decode(pkt, sizeof(pkt), 7, name, sizeof(name), &next, PROTO_TRUE));
    TEST_ASSERT_EQUAL_STRING("www.local", name);
    // next is two bytes past the pointer, not the end of what it pointed at: the caller keeps
    // walking the record the name sat in.
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt), next);
}

// A pointer to itself must terminate on the hop limit rather than spin.
void test_decode_pointer_loop_terminates()
{
    const uint8_t pkt[] = {0xC0, 0x00};
    char name[64];
    TEST_ASSERT_FALSE(pc_dns_name_decode(pkt, sizeof(pkt), 0, name, sizeof(name), NULL, PROTO_TRUE));
}

void test_decode_null_args()
{
    const uint8_t pkt[] = {0};
    char name[8];
    TEST_ASSERT_FALSE(pc_dns_name_decode(NULL, 1, 0, name, sizeof(name), NULL, PROTO_TRUE));
    TEST_ASSERT_FALSE(pc_dns_name_decode(pkt, 1, 0, NULL, 8, NULL, PROTO_TRUE));
    TEST_ASSERT_FALSE(pc_dns_name_decode(pkt, 1, 0, name, 0, NULL, PROTO_TRUE));
}

// --- encode -------------------------------------------------------------------------------------

void test_encode_multi_label()
{
    uint8_t out[32];
    size_t n = pc_dns_name_encode(out, sizeof(out), "myhst.local");
    const uint8_t want[] = {5, 'm', 'y', 'h', 's', 't', 5, 'l', 'o', 'c', 'a', 'l', 0};
    TEST_ASSERT_EQUAL_UINT(sizeof(want), n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, sizeof(want));
}

// A trailing dot is the root, so it encodes the same as the name without one.
void test_encode_trailing_dot()
{
    uint8_t with[32], without[32];
    size_t a = pc_dns_name_encode(with, sizeof(with), "foo.local.");
    size_t b = pc_dns_name_encode(without, sizeof(without), "foo.local");
    TEST_ASSERT_EQUAL_UINT(b, a);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(without, with, b);
}

void test_encode_empty_is_root()
{
    uint8_t out[8];
    size_t n = pc_dns_name_encode(out, sizeof(out), "");
    TEST_ASSERT_EQUAL_UINT(1, n);
    TEST_ASSERT_EQUAL_UINT8(0, out[0]);
}

// An empty label inside a name has no encoding; a label past 63 has no length byte that fits.
void test_encode_rejects_bad_labels()
{
    uint8_t out[128];
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_name_encode(out, sizeof(out), "foo..local"));
    char big[80];
    memset(big, 'a', 64);
    big[64] = '\0';
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_name_encode(out, sizeof(out), big));
}

// The root byte has to fit after the last label, so a buffer one short refuses.
void test_encode_cap()
{
    uint8_t exact[7]; // 1 + 5 + root = 7
    TEST_ASSERT_EQUAL_UINT(7, pc_dns_name_encode(exact, sizeof(exact), "local"));
    uint8_t tight[6];
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_name_encode(tight, sizeof(tight), "local"));
}

void test_encode_null_args()
{
    uint8_t out[8];
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_name_encode(NULL, 8, "a"));
    TEST_ASSERT_EQUAL_UINT(0, pc_dns_name_encode(out, sizeof(out), NULL));
}

// What encode writes is what decode reads back.
void test_encode_decode_round_trip()
{
    uint8_t wire[64];
    size_t n = pc_dns_name_encode(wire, sizeof(wire), "_http._tcp.local");
    TEST_ASSERT_TRUE(n > 0);
    char name[64];
    size_t next = 0;
    TEST_ASSERT_TRUE(pc_dns_name_decode(wire, n, 0, name, sizeof(name), &next, PROTO_FALSE));
    TEST_ASSERT_EQUAL_STRING("_http._tcp.local", name);
    TEST_ASSERT_EQUAL_UINT(n, next);
}

// --- compare ------------------------------------------------------------------------------------

void test_name_eq_ignores_case()
{
    TEST_ASSERT_TRUE(pc_dns_name_eq("MyHost.Local", "myhost.local"));
    TEST_ASSERT_TRUE(pc_dns_name_eq("", ""));
    TEST_ASSERT_FALSE(pc_dns_name_eq("myhost.local", "myhost.lan"));
    TEST_ASSERT_FALSE(pc_dns_name_eq("myhost", "myhost.local")); // a prefix is not a match
    TEST_ASSERT_FALSE(pc_dns_name_eq("myhost.local", "myhost"));
    TEST_ASSERT_FALSE(pc_dns_name_eq(NULL, "a"));
    TEST_ASSERT_FALSE(pc_dns_name_eq("a", NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_decode_single_label);
    RUN_TEST(test_decode_multi_label);
    RUN_TEST(test_decode_root_is_empty);
    RUN_TEST(test_decode_truncated);
    RUN_TEST(test_decode_undefined_label_type);
    RUN_TEST(test_decode_out_cap);
    RUN_TEST(test_decode_pointer_refused_and_followed);
    RUN_TEST(test_decode_pointer_loop_terminates);
    RUN_TEST(test_decode_null_args);
    RUN_TEST(test_encode_multi_label);
    RUN_TEST(test_encode_trailing_dot);
    RUN_TEST(test_encode_empty_is_root);
    RUN_TEST(test_encode_rejects_bad_labels);
    RUN_TEST(test_encode_cap);
    RUN_TEST(test_encode_null_args);
    RUN_TEST(test_encode_decode_round_trip);
    RUN_TEST(test_name_eq_ignores_case);
    return UNITY_END();
}
