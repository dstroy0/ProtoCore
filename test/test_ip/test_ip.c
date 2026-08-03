// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the pc_ip address core (network_drivers/network/pc_ip): RFC 4291 text
// parsing, RFC 5952 canonical formatting, scope classification, v4-mapped handling, and
// malformed-input rejection. Pure host tests.

#include "network_drivers/network/ip.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// Parse s, format it back, and assert the canonical text equals expect.
static void canon(const char *s, const char *expect)
{
    pc_ip ip;
    TEST_ASSERT_TRUE_MESSAGE(pc_ip_parse(s, &ip), s);
    char out[PC_IP_STR_MAX];
    size_t n = pc_ip_format(&ip, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT((unsigned)strlen(expect), (unsigned)n);
    TEST_ASSERT_EQUAL_STRING(expect, out);
}

void test_v4_round_trip()
{
    canon("192.168.1.1", "192.168.1.1");
    canon("0.0.0.0", "0.0.0.0");
    canon("255.255.255.255", "255.255.255.255");
    canon("8.8.8.8", "8.8.8.8");
}

void test_v6_canonical_5952()
{
    // RFC 5952: lower-case, no leading zeros, longest zero run -> "::".
    canon("2001:0db8:0000:0000:0000:0000:0000:0001", "2001:db8::1");
    canon("2001:DB8::1", "2001:db8::1");
    canon("0:0:0:0:0:0:0:0", "::");
    canon("0:0:0:0:0:0:0:1", "::1");
    canon("fe80:0:0:0:0:0:0:1", "fe80::1");
    canon("2001:0db8:0000:0000:0000:ff00:0042:8329", "2001:db8::ff00:42:8329");
    // Two equal-length zero runs -> compress the leftmost (RFC 5952 4.2.3).
    canon("2001:db8:0:0:1:0:0:1", "2001:db8::1:0:0:1");
    // A single zero group is NOT compressed.
    canon("2001:db8:0:1:1:1:1:1", "2001:db8:0:1:1:1:1:1");
    // Full 8 groups, no zeros.
    canon("2001:db8:1:2:3:4:5:6", "2001:db8:1:2:3:4:5:6");
}

void test_v4_mapped()
{
    canon("::ffff:192.168.1.1", "::ffff:192.168.1.1");
    canon("::ffff:0:0", "::ffff:0.0.0.0");
    pc_ip ip;
    TEST_ASSERT_TRUE(pc_ip_parse("::ffff:10.0.0.1", &ip));
    TEST_ASSERT_TRUE(pc_ip_is_v4_mapped(&ip));
    TEST_ASSERT_EQUAL_HEX32(0x0A000001u, pc_ip_to_v4_be(&ip));
    // Guards: a null pointer and a plain v4-family address are both "not mapped".
    TEST_ASSERT_FALSE(pc_ip_is_v4_mapped(NULL));
    pc_ip v4;
    TEST_ASSERT_TRUE(pc_ip_parse("10.0.0.1", &v4));
    TEST_ASSERT_FALSE(pc_ip_is_v4_mapped(&v4));
}

void test_classify_v4()
{
    pc_ip ip;
    pc_ip_parse("0.0.0.0", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_UNSPECIFIED, pc_ip_classify(&ip));
    pc_ip_parse("127.0.0.1", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_LOOPBACK, pc_ip_classify(&ip));
    pc_ip_parse("169.254.3.4", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_LINK_LOCAL, pc_ip_classify(&ip));
    pc_ip_parse("10.1.2.3", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_PRIVATE, pc_ip_classify(&ip));
    pc_ip_parse("172.16.0.1", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_PRIVATE, pc_ip_classify(&ip));
    pc_ip_parse("172.32.0.1", &ip); // just outside 172.16/12
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_GLOBAL, pc_ip_classify(&ip));
    pc_ip_parse("192.168.5.5", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_PRIVATE, pc_ip_classify(&ip));
    pc_ip_parse("224.0.0.1", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_MULTICAST, pc_ip_classify(&ip));
    pc_ip_parse("8.8.8.8", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_GLOBAL, pc_ip_classify(&ip));
    // Each byte of the all-zero check failing individually (not just the first).
    pc_ip_parse("0.1.0.0", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_GLOBAL, pc_ip_classify(&ip));
    pc_ip_parse("0.0.1.0", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_GLOBAL, pc_ip_classify(&ip));
    pc_ip_parse("0.0.0.1", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_GLOBAL, pc_ip_classify(&ip));
    pc_ip_parse("169.1.2.3", &ip); // first octet matches link-local, second doesn't
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_GLOBAL, pc_ip_classify(&ip));
    pc_ip_parse("172.10.0.1", &ip); // 172 but second octet below the 16-31 private range
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_GLOBAL, pc_ip_classify(&ip));
    pc_ip_parse("192.1.2.3", &ip); // 192 but second octet isn't 168
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_GLOBAL, pc_ip_classify(&ip));
    pc_ip_parse("250.1.2.3", &ip); // above the 224-239 multicast range
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_GLOBAL, pc_ip_classify(&ip));
}

void test_classify_v6()
{
    pc_ip ip;
    pc_ip_parse("::", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_UNSPECIFIED, pc_ip_classify(&ip));
    pc_ip_parse("::1", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_LOOPBACK, pc_ip_classify(&ip));
    pc_ip_parse("fe80::1", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_LINK_LOCAL, pc_ip_classify(&ip));
    pc_ip_parse("fc00::1", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_PRIVATE, pc_ip_classify(&ip)); // ULA
    pc_ip_parse("fd12:3456::1", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_PRIVATE, pc_ip_classify(&ip));
    pc_ip_parse("ff02::1", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_MULTICAST, pc_ip_classify(&ip));
    pc_ip_parse("2001:db8::1", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_GLOBAL, pc_ip_classify(&ip));
    // v4-mapped classifies as its v4 scope.
    pc_ip_parse("::ffff:127.0.0.1", &ip);
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_LOOPBACK, pc_ip_classify(&ip));
    pc_ip_parse("fe00::1", &ip); // b[0]==0xfe but the top two bits of b[1] aren't 0x80
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_GLOBAL, pc_ip_classify(&ip));
    // b[0..9] all zero and b[10]==0xff, but b[11] != 0xff: not v4-mapped (falls through to GLOBAL).
    pc_ip_parse("::ff00:0:0", &ip);
    TEST_ASSERT_FALSE(pc_ip_is_v4_mapped(&ip));
    TEST_ASSERT_EQUAL(PC_IP_SCOPE_GLOBAL, pc_ip_classify(&ip));
}

void test_reject_malformed()
{
    pc_ip ip;
    TEST_ASSERT_FALSE(pc_ip_parse("", &ip));
    TEST_ASSERT_FALSE(pc_ip_parse("256.0.0.1", &ip));             // octet > 255
    TEST_ASSERT_FALSE(pc_ip_parse("1.2.3", &ip));                 // too few octets
    TEST_ASSERT_FALSE(pc_ip_parse("1.2.3.4.5", &ip));             // too many
    TEST_ASSERT_FALSE(pc_ip_parse("1::2::3", &ip));               // two "::"
    TEST_ASSERT_FALSE(pc_ip_parse("1:2:3:4:5:6:7:8:9", &ip));     // 9 groups
    TEST_ASSERT_FALSE(pc_ip_parse("1:2:3:4:5:6:7", &ip));         // 7 groups, no "::"
    TEST_ASSERT_FALSE(pc_ip_parse("12345::1", &ip));              // hextet too long
    TEST_ASSERT_FALSE(pc_ip_parse("2001:db8:::1", &ip));          // triple colon
    TEST_ASSERT_FALSE(pc_ip_parse("2001:db8::1:", &ip));          // trailing single colon
    TEST_ASSERT_FALSE(pc_ip_parse("2001:xyz::1", &ip));           // bad hex
    TEST_ASSERT_FALSE(pc_ip_parse("::ffff:1.2.3.4:5", &ip));      // v4 not last
    TEST_ASSERT_FALSE(pc_ip_parse("hello", &ip));                 // no separators
    TEST_ASSERT_FALSE(pc_ip_parse("1..2.3", &ip));                // empty octet mid-address
    TEST_ASSERT_FALSE(pc_ip_parse("1.2.3.", &ip));                // empty trailing octet
    TEST_ASSERT_FALSE(pc_ip_parse("!.2.3.4", &ip));               // char below '0' in a v4 octet
    TEST_ASSERT_FALSE(pc_ip_parse("2001:!db8::1", &ip));          // char below '0' in a v6 hextet
    TEST_ASSERT_FALSE(pc_ip_parse(":", &ip));                     // a single ':' (len==1, no second colon)
    TEST_ASSERT_FALSE(pc_ip_parse("::1.2.3.999", &ip));           // embedded v4 tail octet out of range
    TEST_ASSERT_FALSE(pc_ip_parse("1:2:3:4:5:6:7:1.2.3.4", &ip)); // too many groups before embedded v4
}

void test_equal_and_from_v4()
{
    pc_ip a = pc_ip_from_v4_octets(192, 168, 1, 1);
    pc_ip b;
    TEST_ASSERT_TRUE(pc_ip_parse("192.168.1.1", &b));
    TEST_ASSERT_TRUE(pc_ip_equal(&a, &b));
    TEST_ASSERT_EQUAL_HEX32(0xC0A80101u, pc_ip_to_v4_be(&a));

    pc_ip c;
    TEST_ASSERT_TRUE(pc_ip_parse("192.168.1.2", &c));
    TEST_ASSERT_FALSE(pc_ip_equal(&a, &c));

    // Different families never compare equal.
    pc_ip v6;
    TEST_ASSERT_TRUE(pc_ip_parse("::c0a8:101", &v6));
    TEST_ASSERT_FALSE(pc_ip_equal(&a, &v6));

    // Null guards on either side.
    TEST_ASSERT_FALSE(pc_ip_equal(NULL, &b));
    TEST_ASSERT_FALSE(pc_ip_equal(&a, NULL));
}

void test_from_v6_bytes()
{
    // 2001:db8::1 as raw network-order bytes -> pc_ip -> canonical text.
    const uint8_t raw[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    pc_ip ip = pc_ip_from_v6_bytes(raw);
    TEST_ASSERT_EQUAL_UINT8(PC_IP_V6, ip.family);
    char s[PC_IP_STR_MAX];
    pc_ip_format(&ip, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("2001:db8::1", s);

    pc_ip parsed;
    TEST_ASSERT_TRUE(pc_ip_parse("2001:db8::1", &parsed));
    TEST_ASSERT_TRUE(pc_ip_equal(&ip, &parsed));
}

void test_is_unspecified()
{
    pc_ip none;
    none.family = PC_IP_NONE;
    TEST_ASSERT_TRUE(pc_ip_is_unspecified(&none));
    TEST_ASSERT_TRUE(pc_ip_is_unspecified(NULL));

    pc_ip z4, z6, host;
    TEST_ASSERT_TRUE(pc_ip_parse("0.0.0.0", &z4));
    TEST_ASSERT_TRUE(pc_ip_parse("::", &z6));
    TEST_ASSERT_TRUE(pc_ip_parse("192.168.1.1", &host));
    TEST_ASSERT_TRUE(pc_ip_is_unspecified(&z4)); // 0.0.0.0 is untrackable
    TEST_ASSERT_TRUE(pc_ip_is_unspecified(&z6)); // :: is untrackable
    TEST_ASSERT_FALSE(pc_ip_is_unspecified(&host));
}

void test_prefix_match()
{
    // IPv4 CIDR containment (the allowlist primitive - full address, no hashing).
    pc_ip net24, in24, out24;
    TEST_ASSERT_TRUE(pc_ip_parse("192.168.1.0", &net24));
    TEST_ASSERT_TRUE(pc_ip_parse("192.168.1.200", &in24));
    TEST_ASSERT_TRUE(pc_ip_parse("192.168.2.1", &out24));
    TEST_ASSERT_TRUE(pc_ip_prefix_match(&in24, &net24, 24));
    TEST_ASSERT_FALSE(pc_ip_prefix_match(&out24, &net24, 24));

    // Host bits outside the prefix are masked at compare time (10.1.2.3/8 == 10.0.0.0/8).
    pc_ip net8, in8, out8;
    TEST_ASSERT_TRUE(pc_ip_parse("10.1.2.3", &net8));
    TEST_ASSERT_TRUE(pc_ip_parse("10.255.255.255", &in8));
    TEST_ASSERT_TRUE(pc_ip_parse("11.0.0.1", &out8));
    TEST_ASSERT_TRUE(pc_ip_prefix_match(&in8, &net8, 8));
    TEST_ASSERT_FALSE(pc_ip_prefix_match(&out8, &net8, 8));

    // /32 is a single host; /0 matches everything of the family.
    pc_ip any;
    TEST_ASSERT_TRUE(pc_ip_parse("203.0.113.7", &any));
    TEST_ASSERT_TRUE(pc_ip_prefix_match(&any, &any, 32));
    TEST_ASSERT_FALSE(pc_ip_prefix_match(&out24, &any, 32));
    TEST_ASSERT_TRUE(pc_ip_prefix_match(&any, &net8, 0));

    // IPv6 CIDR containment on the full 128-bit address.
    pc_ip v6net, v6in, v6out;
    TEST_ASSERT_TRUE(pc_ip_parse("2001:db8::", &v6net));
    TEST_ASSERT_TRUE(pc_ip_parse("2001:db8:0:0:1234::abcd", &v6in));
    TEST_ASSERT_TRUE(pc_ip_parse("2001:db9::1", &v6out));
    TEST_ASSERT_TRUE(pc_ip_prefix_match(&v6in, &v6net, 32));
    TEST_ASSERT_FALSE(pc_ip_prefix_match(&v6out, &v6net, 32));

    // Cross-family never matches, and an over-long prefix is rejected.
    TEST_ASSERT_FALSE(pc_ip_prefix_match(&v6in, &net24, 24));  // v6 addr vs v4 net
    TEST_ASSERT_FALSE(pc_ip_prefix_match(&in24, &v6net, 24));  // v4 addr vs v6 net
    TEST_ASSERT_FALSE(pc_ip_prefix_match(&in24, &net24, 33));  // prefix > 32 for v4
    TEST_ASSERT_FALSE(pc_ip_prefix_match(&v6in, &v6net, 129)); // prefix > 128 for v6

    // Null guards on either side.
    TEST_ASSERT_FALSE(pc_ip_prefix_match(NULL, &net24, 24));
    TEST_ASSERT_FALSE(pc_ip_prefix_match(&in24, NULL, 24));
}

void test_ip_classify_equal_cidr_and_parse_edges()
{
    // classify: null and a PC_IP_NONE address are UNSPECIFIED.
    TEST_ASSERT_EQUAL_INT(PC_IP_SCOPE_UNSPECIFIED, pc_ip_classify(NULL));
    pc_ip none = {};
    pc_ip none2 = {};
    TEST_ASSERT_EQUAL_INT(PC_IP_SCOPE_UNSPECIFIED, pc_ip_classify(&none));
    // equal: two PC_IP_NONE addresses compare equal (same non-address family).
    TEST_ASSERT_TRUE(pc_ip_equal(&none, &none2));
    // CIDR with a non-byte-aligned prefix: the partial high bits of the boundary byte must match.
    pc_ip net = pc_ip_from_v4_octets(192, 168, 0, 0);
    pc_ip in = pc_ip_from_v4_octets(192, 168, 15, 255); // 3rd byte 0x0F, high nibble 0 -> in /20
    pc_ip out = pc_ip_from_v4_octets(192, 168, 16, 0);  // 3rd byte 0x10, high nibble 1 -> out of /20
    TEST_ASSERT_TRUE(pc_ip_prefix_match(&in, &net, 20));
    TEST_ASSERT_FALSE(pc_ip_prefix_match(&out, &net, 20));
    // Both PC_IP_NONE (same family, so the family check passes): the bits-for-family
    // ternary falls through both arms to 0, which the bits==0 guard then rejects.
    TEST_ASSERT_FALSE(pc_ip_prefix_match(&none, &none2, 0));
    // Parse edges: a lone leading ':' and an over-long string are rejected.
    pc_ip p;
    TEST_ASSERT_FALSE(pc_ip_parse(":1", &p));
    char toolong[80];
    for (int i = 0; i < 79; i++)
    {
        toolong[i] = '1';
    }
    toolong[79] = '\0';
    TEST_ASSERT_FALSE(pc_ip_parse(toolong, &p));
    TEST_ASSERT_FALSE(pc_ip_parse("1.2.3", &p));             // too few octets
    TEST_ASSERT_FALSE(pc_ip_parse("1.2.3.4.5", &p));         // too many octets
    TEST_ASSERT_FALSE(pc_ip_parse("256.0.0.1", &p));         // octet out of range
    TEST_ASSERT_FALSE(pc_ip_parse("gg::1", &p));             // invalid hex
    TEST_ASSERT_FALSE(pc_ip_parse("1:2:3:4:5:6:7:8:9", &p)); // too many v6 groups
    TEST_ASSERT_FALSE(pc_ip_parse(NULL, &p));             // null string
    TEST_ASSERT_FALSE(pc_ip_parse("1.2.3.4", NULL));      // null out
    TEST_ASSERT_FALSE(pc_ip_parse("1234.5.6.7", &p));        // octet has 4 digits
    TEST_ASSERT_FALSE(pc_ip_parse("1.2.3.z", &p));           // invalid char in v4
    // format guards: null ptr, non-v6/v4 family, and buffers too small for v4/v6/mapped.
    char buf[64];
    TEST_ASSERT_EQUAL_UINT(0, pc_ip_format(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT(0, pc_ip_format(&none, buf, sizeof(buf)));     // PC_IP_NONE family
    TEST_ASSERT_EQUAL_UINT(0, pc_ip_format(&none, NULL, sizeof(buf))); // null out buffer
    TEST_ASSERT_EQUAL_UINT(0, pc_ip_format(&none, buf, 0));               // cap == 0
    pc_ip v4 = pc_ip_from_v4_octets(255, 255, 255, 255);
    TEST_ASSERT_EQUAL_UINT(0, pc_ip_format(&v4, buf, 4)); // "255.255.255.255" needs 16
    pc_ip v6;
    TEST_ASSERT_TRUE(pc_ip_parse("2001:db8::1", &v6));
    TEST_ASSERT_EQUAL_UINT(0, pc_ip_format(&v6, buf, 4)); // v6 text needs more than 4
    pc_ip mapped;
    TEST_ASSERT_TRUE(pc_ip_parse("::ffff:1.2.3.4", &mapped));
    TEST_ASSERT_EQUAL_UINT(0, pc_ip_format(&mapped, buf, 5)); // "::ffff:1.2.3.4" tail overflows
    // to_v4_be guards: null and a pure (non-mapped) v6 both yield 0.
    TEST_ASSERT_EQUAL_UINT(0, pc_ip_to_v4_be(NULL));
    TEST_ASSERT_EQUAL_UINT(0, pc_ip_to_v4_be(&v6));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_v4_round_trip);
    RUN_TEST(test_from_v6_bytes);
    RUN_TEST(test_is_unspecified);
    RUN_TEST(test_prefix_match);
    RUN_TEST(test_v6_canonical_5952);
    RUN_TEST(test_v4_mapped);
    RUN_TEST(test_classify_v4);
    RUN_TEST(test_classify_v6);
    RUN_TEST(test_reject_malformed);
    RUN_TEST(test_equal_and_from_v4);
    RUN_TEST(test_ip_classify_equal_cidr_and_parse_edges);
    return UNITY_END();
}
