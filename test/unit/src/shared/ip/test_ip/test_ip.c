// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the IP address core (shared/ip/ip.h).
//
// Two specifications carry this module. RFC 4291 sec 2.2 gives the text forms an IPv6 address may
// be written in, and RFC 5952 sec 4 fixes the ONE form an implementation must produce: lowercase
// hex, no leading zeros in a field, "::" compressing the longest run of zero fields (and never a
// single field), leftmost run on a tie. A parser that accepts every input form but emits a
// non-canonical one breaks every consumer that compares addresses as text.
//
// test_rfc5952_canonical_output is therefore the load-bearing case: each input below is a legal
// spelling from RFC 4291 and each expected output is the canonical spelling RFC 5952 requires,
// taken from the rules rather than from what this formatter happens to emit.

#include "shared/ip/ip.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static proto_bool parse(const char *s, protocore_ip *out)
{
    Ip.args.text = s;
    Ip.args.out = out;
    Ip.parse(Ip.internal);
    return Ip.ok;
}

static const char *format(const protocore_ip *ip, char *buf, size_t cap)
{
    Ip.args.ip = ip;
    Ip.args.buf = buf;
    Ip.args.cap = cap;
    Ip.format(Ip.internal);
    return buf;
}

static protocore_ip_scope scope_of(const char *s)
{
    protocore_ip ip;
    TEST_ASSERT_TRUE_MESSAGE(parse(s, &ip), s);
    Ip.args.ip = &ip;
    Ip.classify(Ip.internal);
    return Ip.scope;
}

// Parse then format must return the input unchanged, for text that is already canonical.
static void round_trip(const char *s)
{
    protocore_ip ip;
    char out[PROTOCORE_IP_STR_MAX];
    TEST_ASSERT_TRUE_MESSAGE(parse(s, &ip), s);
    TEST_ASSERT_EQUAL_STRING(s, format(&ip, out, sizeof(out)));
}

void test_v4_round_trip(void)
{
    round_trip("0.0.0.0");
    round_trip("127.0.0.1");
    round_trip("10.0.0.1");
    round_trip("192.168.1.254");
    round_trip("255.255.255.255");
}

// RFC 5952 sec 4: lowercase, shortest field form, "::" over the longest zero run.
void test_rfc5952_canonical_output(void)
{
    struct
    {
        const char *in;
        const char *want;
    } static const CASES[] = {
        // sec 4.1: leading zeros in a field are suppressed
        {"2001:0db8:0000:0000:0000:0000:0000:0001", "2001:db8::1"},
        // sec 4.2.1: "::" must compress a run, and sec 4.2.2 forbids it for a single field
        {"2001:db8:0:1:1:1:1:1", "2001:db8:0:1:1:1:1:1"},
        // sec 4.2.3: on a tie the leftmost run is compressed
        {"2001:0:0:1:0:0:0:1", "2001:0:0:1::1"},
        // sec 4.3: hex is lowercase
        {"2001:DB8::AB", "2001:db8::ab"},
        // the all-zero address and the loopback, sec 2.2 of RFC 4291
        {"::", "::"},
        {"::1", "::1"},
        // a full-length address with nothing to compress
        {"2001:db8:1:2:3:4:5:6", "2001:db8:1:2:3:4:5:6"},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        protocore_ip ip;
        char out[PROTOCORE_IP_STR_MAX];
        TEST_ASSERT_TRUE_MESSAGE(parse(CASES[i].in, &ip), CASES[i].in);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(CASES[i].want, format(&ip, out, sizeof(out)), CASES[i].in);
    }
}

// RFC 5952 sec 5: an IPv4-mapped address keeps its dotted tail rather than becoming hex.
void test_v4_mapped(void)
{
    protocore_ip ip;
    char out[PROTOCORE_IP_STR_MAX];
    TEST_ASSERT_TRUE(parse("::ffff:10.0.0.1", &ip));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_V6, ip.family);
    TEST_ASSERT_TRUE(protocore_ip_is_v4_mapped(&ip));
    TEST_ASSERT_EQUAL_STRING("::ffff:10.0.0.1", format(&ip, out, sizeof(out)));

    // a plain v4 address is not a mapped one
    protocore_ip v4;
    TEST_ASSERT_TRUE(parse("10.0.0.1", &v4));
    TEST_ASSERT_FALSE(protocore_ip_is_v4_mapped(&v4));
}

// Malformed text is refused rather than partially accepted.
void test_malformed_text_is_refused(void)
{
    static const char *const BAD[] = {
        "",         "1.2.3",      "1.2.3.4.5", "256.0.0.1",
        "1.2.3.-1", "...",        ":",         "1::2::3", // sec 4.2: only one "::" is allowed
        "12345::",                                        // a field wider than four hex digits
        "0xg::",    "1.2.3.4:80", "unknown",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        protocore_ip ip;
        TEST_ASSERT_FALSE_MESSAGE(parse(BAD[i], &ip), BAD[i]);
    }
}

// The constructors build the same value the parser does.
void test_constructors_match_the_parser(void)
{
    protocore_ip built = protocore_ip_from_v4_octets(192, 168, 1, 1);
    protocore_ip parsed;
    TEST_ASSERT_TRUE(parse("192.168.1.1", &parsed));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_V4, built.family);

    Ip.args.ip = &built;
    Ip.args.b = &parsed;
    Ip.equal(Ip.internal);
    TEST_ASSERT_TRUE(Ip.ok);

    static const uint8_t SIX[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};
    protocore_ip v6 = protocore_ip_from_v6_bytes(SIX);
    protocore_ip p6;
    TEST_ASSERT_TRUE(parse("2001:db8::1", &p6));
    Ip.args.ip = &v6;
    Ip.args.b = &p6;
    Ip.equal(Ip.internal);
    TEST_ASSERT_TRUE(Ip.ok);
}

// The big-endian 32-bit form a socket API wants, for v4 and for a v4-mapped v6.
void test_to_v4_be(void)
{
    protocore_ip ip;
    TEST_ASSERT_TRUE(parse("1.2.3.4", &ip));
    TEST_ASSERT_EQUAL_HEX32(0x01020304u, protocore_ip_to_v4_be(&ip));

    TEST_ASSERT_TRUE(parse("::ffff:1.2.3.4", &ip));
    TEST_ASSERT_EQUAL_HEX32(0x01020304u, protocore_ip_to_v4_be(&ip));

    // a v6 address that is not v4-mapped has no v4 form
    TEST_ASSERT_TRUE(parse("2001:db8::1", &ip));
    TEST_ASSERT_EQUAL_HEX32(0u, protocore_ip_to_v4_be(&ip));
}

// Addresses of different families are never equal, whatever their octets.
void test_equal_separates_families(void)
{
    protocore_ip a, b;
    TEST_ASSERT_TRUE(parse("0.0.0.0", &a));
    TEST_ASSERT_TRUE(parse("::", &b));
    Ip.args.ip = &a;
    Ip.args.b = &b;
    Ip.equal(Ip.internal);
    TEST_ASSERT_FALSE(Ip.ok);
}

void test_is_unspecified(void)
{
    protocore_ip ip;
    TEST_ASSERT_TRUE(parse("0.0.0.0", &ip));
    Ip.args.ip = &ip;
    Ip.is_unspecified(Ip.internal);
    TEST_ASSERT_TRUE(Ip.ok);

    TEST_ASSERT_TRUE(parse("::", &ip));
    Ip.args.ip = &ip;
    Ip.is_unspecified(Ip.internal);
    TEST_ASSERT_TRUE(Ip.ok);

    TEST_ASSERT_TRUE(parse("0.0.0.1", &ip));
    Ip.args.ip = &ip;
    Ip.is_unspecified(Ip.internal);
    TEST_ASSERT_FALSE(Ip.ok);
}

// RFC 1122 sec 3.2.1.3 loopback, RFC 1918 private, RFC 3927 link-local, RFC 5771 multicast.
void test_classify_v4(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_UNSPECIFIED, scope_of("0.0.0.0"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_LOOPBACK, scope_of("127.0.0.1"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_LOOPBACK, scope_of("127.255.255.254"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_LINK_LOCAL, scope_of("169.254.1.1"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_PRIVATE, scope_of("10.0.0.1"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_PRIVATE, scope_of("172.16.0.1"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_PRIVATE, scope_of("172.31.255.254"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_PRIVATE, scope_of("192.168.0.1"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_MULTICAST, scope_of("224.0.0.1"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_GLOBAL, scope_of("8.8.8.8"));
    // 172.15 and 172.32 sit either side of the RFC 1918 /12, so they are global
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_GLOBAL, scope_of("172.15.255.255"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_GLOBAL, scope_of("172.32.0.0"));
}

// RFC 4291 sec 2.4 / RFC 4193 ULA / RFC 4291 sec 2.5.6 link-local.
void test_classify_v6(void)
{
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_UNSPECIFIED, scope_of("::"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_LOOPBACK, scope_of("::1"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_LINK_LOCAL, scope_of("fe80::1"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_PRIVATE, scope_of("fc00::1"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_PRIVATE, scope_of("fd00::1"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_MULTICAST, scope_of("ff02::1"));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_SCOPE_GLOBAL, scope_of("2001:db8::1"));
}

// RFC 4632 CIDR containment, at and either side of the prefix boundary.
void test_prefix_match(void)
{
    protocore_ip net, addr;
    TEST_ASSERT_TRUE(parse("192.168.1.0", &net));

    TEST_ASSERT_TRUE(parse("192.168.1.42", &addr));
    Ip.args.ip = &addr;
    Ip.args.b = &net;
    Ip.args.prefix_len = 24;
    Ip.prefix_match(Ip.internal);
    TEST_ASSERT_TRUE(Ip.ok);

    TEST_ASSERT_TRUE(parse("192.168.2.42", &addr));
    Ip.args.ip = &addr;
    Ip.args.b = &net;
    Ip.args.prefix_len = 24;
    Ip.prefix_match(Ip.internal);
    TEST_ASSERT_FALSE(Ip.ok);

    // /0 contains everything
    TEST_ASSERT_TRUE(parse("8.8.8.8", &addr));
    Ip.args.ip = &addr;
    Ip.args.b = &net;
    Ip.args.prefix_len = 0;
    Ip.prefix_match(Ip.internal);
    TEST_ASSERT_TRUE(Ip.ok);
}

// A buffer too small for the canonical text reports 0 rather than writing a truncated address.
void test_format_refuses_a_short_buffer(void)
{
    protocore_ip ip;
    char small[4];
    TEST_ASSERT_TRUE(parse("2001:db8:1:2:3:4:5:6", &ip));
    Ip.args.ip = &ip;
    Ip.args.buf = small;
    Ip.args.cap = sizeof(small);
    Ip.format(Ip.internal);
    TEST_ASSERT_EQUAL_UINT(0u, Ip.n);
}
