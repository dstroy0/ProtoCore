// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the DNS name codec (network_drivers/network/dns/dns_wire.h).
//
// RFC 1035 sec 4.1.4 prints one worked message: F.ISI.ARPA laid out at offset 20, FOO.F.ISI.ARPA at
// 40 ending in a pointer to 20, ARPA at 64 as a bare pointer to 26 (the middle of the first name),
// and the root as a single zero octet at 92. That figure exercises every shape a name can take -
// labels ending in a zero octet, labels ending in a pointer, a pointer alone - against octets the
// standard itself publishes, including the offsets. test_rfc1035_worked_message rebuilds it byte for
// byte and is the load-bearing case: a decoder that gets it right cannot be reading OFFSET from the
// wrong bits or resuming the walk in the wrong place.

#include "network_drivers/network/dns/dns_wire/dns_wire.h"
#include <string.h>

#include <unity.h>

static uint8_t dns_wire_work[16]; // the borrow an entry takes; DnsWire never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static proto_bool decode(const uint8_t *pkt, size_t len, size_t off, char *out, size_t cap, proto_bool allow_ptr)
{
    DnsWireV.msg.pkt = pkt;
    DnsWireV.msg.len = len;
    DnsWireV.msg.off = off;
    DnsWireV.msg.out = out;
    DnsWireV.msg.out_cap = cap;
    DnsWireV.msg.allow_ptr = allow_ptr;
    DnsWire.decode(dns_wire_work);
    return DnsWireV.ok;
}

static size_t encode(const char *dotted, uint8_t *out, size_t cap)
{
    DnsWireV.text.dotted = dotted;
    DnsWireV.text.out = out;
    DnsWireV.text.out_cap = cap;
    DnsWire.encode(dns_wire_work);
    return DnsWireV.n;
}

static proto_bool eq(const char *a, const char *b)
{
    DnsWireV.cmp.a = a;
    DnsWireV.cmp.b = b;
    DnsWire.eq(dns_wire_work);
    return DnsWireV.ok;
}

// RFC 1035 sec 4.1.4's figure, transcribed at the offsets it names. Octets it does not show are
// zero, which is what a length octet of an unused region would be anyway.
//   20: 1 F  3 I  S I  4 A  R P  A 0        F.ISI.ARPA, ends at 32
//   40: 3 F  O O  11|20                     FOO. then a pointer to 20, ends at 46
//   64: 11|26                               a pointer to the ARPA label at 26, ends at 66
//   92: 0                                   the root, ends at 93
static void build_rfc1035_message(uint8_t *m, size_t cap)
{
    memset(m, 0, cap);
    m[20] = 1;
    m[21] = 'F';
    m[22] = 3;
    m[23] = 'I';
    m[24] = 'S';
    m[25] = 'I';
    m[26] = 4;
    m[27] = 'A';
    m[28] = 'R';
    m[29] = 'P';
    m[30] = 'A';
    m[31] = 0;

    m[40] = 3;
    m[41] = 'F';
    m[42] = 'O';
    m[43] = 'O';
    m[44] = 0xC0; // 1 1 | OFFSET high six bits
    m[45] = 20;   // OFFSET low eight bits

    m[64] = 0xC0;
    m[65] = 26;

    m[92] = 0;
}

void test_rfc1035_worked_message(void)
{
    uint8_t msg[96];
    build_rfc1035_message(msg, sizeof(msg));
    char out[256];

    TEST_ASSERT_TRUE(decode(msg, sizeof(msg), 20, out, sizeof(out), PROTO_TRUE));
    TEST_ASSERT_EQUAL_STRING("F.ISI.ARPA", out);
    TEST_ASSERT_EQUAL_size_t(32, DnsWireV.next); // the zero octet at 31 ends it

    TEST_ASSERT_TRUE(decode(msg, sizeof(msg), 40, out, sizeof(out), PROTO_TRUE));
    TEST_ASSERT_EQUAL_STRING("FOO.F.ISI.ARPA", out);
    TEST_ASSERT_EQUAL_size_t(46, DnsWireV.next); // two octets past the pointer, not past what it named

    TEST_ASSERT_TRUE(decode(msg, sizeof(msg), 64, out, sizeof(out), PROTO_TRUE));
    TEST_ASSERT_EQUAL_STRING("ARPA", out);
    TEST_ASSERT_EQUAL_size_t(66, DnsWireV.next);

    TEST_ASSERT_TRUE(decode(msg, sizeof(msg), 92, out, sizeof(out), PROTO_TRUE));
    TEST_ASSERT_EQUAL_STRING("", out); // "the root domain name has no labels"
    TEST_ASSERT_EQUAL_size_t(93, DnsWireV.next);
}

// RFC 1035 sec 3.1: "a domain name ... is a sequence of labels, where each label consists of a
// length octet followed by that number of octets", terminated by "the null label of the root".
// So www.example.com encodes as 3 w w w 7 e x a m p l e 3 c o m 0, and it is 17 octets long.
void test_encode_is_length_prefixed_labels_and_a_root_octet(void)
{
    uint8_t out[64];
    static const uint8_t WANT[17] = {3, 'w', 'w', 'w', 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), encode("www.example.com", out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    // sec 3.1's own note: the trailing dot writes the same octets, because the root octet is
    // already implicit at the end of every encoded name.
    memset(out, 0xFF, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), encode("www.example.com.", out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, out, sizeof(WANT));

    // The root is the null label alone, and this module's spelling for it is the empty string.
    TEST_ASSERT_EQUAL_size_t(1, encode("", out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(0, out[0]);
}

// Encode then decode must return the text unchanged, for names of every shape.
void test_encode_decode_round_trip(void)
{
    static const char *const NAMES[] = {
        "example.com", "a.b.c.d.e.f", "_http._tcp.local", "x", "0-9.example", "ExAmPlE.CoM",
    };
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++)
    {
        uint8_t wire[300];
        char text[256];
        size_t n = encode(NAMES[i], wire, sizeof(wire));
        TEST_ASSERT_TRUE_MESSAGE(n > 0, NAMES[i]);
        TEST_ASSERT_TRUE_MESSAGE(decode(wire, n, 0, text, sizeof(text), PROTO_FALSE), NAMES[i]);
        TEST_ASSERT_EQUAL_STRING(NAMES[i], text);
        TEST_ASSERT_EQUAL_size_t(n, DnsWireV.next);
    }
}

// RFC 1035 sec 2.3.4: "labels          63 octets or less", and sec 4.1.4 gives the reason a longer
// one cannot even be written: "the label must begin with two zero bits because labels are
// restricted to 63 octets or less". So 63 encodes and 64 does not, and on the wire a length octet
// of 64 is 0x40, whose high bits are the reserved 01 type rather than a label at all.
void test_label_length_limit(void)
{
    char name[80];
    uint8_t wire[128];

    memset(name, 'a', 63);
    name[63] = '\0';
    TEST_ASSERT_EQUAL_size_t(65, encode(name, wire, sizeof(wire))); // 1 length + 63 + root

    memset(name, 'a', 64);
    name[64] = '\0';
    TEST_ASSERT_EQUAL_size_t(0, encode(name, wire, sizeof(wire)));

    uint8_t bad[80];
    char out[256];
    memset(bad, 'a', sizeof(bad));
    bad[0] = 64; // 0x40: the reserved 01 type, not a 64-octet label
    bad[65] = 0;
    TEST_ASSERT_FALSE(decode(bad, sizeof(bad), 0, out, sizeof(out), PROTO_FALSE));

    bad[0] = 63; // 0x3F: two zero bits, the longest label there is
    bad[64] = 0;
    TEST_ASSERT_TRUE(decode(bad, 65, 0, out, sizeof(out), PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(63, strlen(out));
}

// RFC 1035 sec 4.1.4: "The first two bits are ones" marks a pointer, and "the 10 and 01
// combinations are reserved for future use". A reserved type is not a label and not a pointer, so
// the name cannot be decoded at all.
void test_reserved_label_types_are_refused(void)
{
    char out[64];
    static const uint8_t TEN[4] = {0x80, 'a', 'b', 0};
    static const uint8_t OHONE[4] = {0x40, 'a', 'b', 0};
    TEST_ASSERT_FALSE(decode(TEN, sizeof(TEN), 0, out, sizeof(out), PROTO_TRUE));
    TEST_ASSERT_FALSE(decode(OHONE, sizeof(OHONE), 0, out, sizeof(out), PROTO_TRUE));
}

// sec 4.1.4 lets a pointer name any prior occurrence, which means a message can name itself. The
// walk is capped so a self-referencing message terminates instead of spinning.
void test_pointer_loops_terminate(void)
{
    char out[64];

    // A pointer at 0 aimed at 0.
    static const uint8_t SELF[2] = {0xC0, 0x00};
    TEST_ASSERT_FALSE(decode(SELF, sizeof(SELF), 0, out, sizeof(out), PROTO_TRUE));

    // Two pointers aimed at each other.
    static const uint8_t PAIR[4] = {0xC0, 0x02, 0xC0, 0x00};
    TEST_ASSERT_FALSE(decode(PAIR, sizeof(PAIR), 0, out, sizeof(out), PROTO_TRUE));
    TEST_ASSERT_FALSE(decode(PAIR, sizeof(PAIR), 2, out, sizeof(out), PROTO_TRUE));

    // A pointer past the end of the message names nothing.
    static const uint8_t OFF_END[4] = {0xC0, 0xFF, 0, 0};
    TEST_ASSERT_FALSE(decode(OFF_END, sizeof(OFF_END), 0, out, sizeof(out), PROTO_TRUE));
}

// The first name in a message has no prior occurrence to point at, so a caller reading a question
// clears allow_ptr and any pointer there is a decode failure rather than a name.
void test_pointers_are_refused_when_not_allowed(void)
{
    uint8_t msg[96];
    build_rfc1035_message(msg, sizeof(msg));
    char out[64];

    // Offset 20 is pure labels, so it decodes either way.
    TEST_ASSERT_TRUE(decode(msg, sizeof(msg), 20, out, sizeof(out), PROTO_FALSE));
    TEST_ASSERT_EQUAL_STRING("F.ISI.ARPA", out);

    // Offsets 40 and 64 end in / are a pointer.
    TEST_ASSERT_FALSE(decode(msg, sizeof(msg), 40, out, sizeof(out), PROTO_FALSE));
    TEST_ASSERT_FALSE(decode(msg, sizeof(msg), 64, out, sizeof(out), PROTO_FALSE));
}

// A name whose labels run past the end of the message, or whose length octet is the last octet, is
// truncated: refused rather than read past.
void test_truncated_names_are_refused(void)
{
    char out[64];

    static const uint8_t NO_ROOT[4] = {3, 'a', 'b', 'c'}; // labels but no terminating zero octet
    TEST_ASSERT_FALSE(decode(NO_ROOT, sizeof(NO_ROOT), 0, out, sizeof(out), PROTO_FALSE));

    static const uint8_t PAST_END[3] = {5, 'a', 'b'}; // claims 5 octets, carries 2
    TEST_ASSERT_FALSE(decode(PAST_END, sizeof(PAST_END), 0, out, sizeof(out), PROTO_FALSE));

    static const uint8_t LONE_LEN[1] = {3};
    TEST_ASSERT_FALSE(decode(LONE_LEN, sizeof(LONE_LEN), 0, out, sizeof(out), PROTO_FALSE));

    static const uint8_t HALF_PTR[1] = {0xC0}; // a pointer needs two octets
    TEST_ASSERT_FALSE(decode(HALF_PTR, sizeof(HALF_PTR), 0, out, sizeof(out), PROTO_TRUE));

    // Starting past the end of the message.
    static const uint8_t OK_NAME[5] = {3, 'a', 'b', 'c', 0};
    TEST_ASSERT_FALSE(decode(OK_NAME, sizeof(OK_NAME), 5, out, sizeof(out), PROTO_FALSE));
}

// A name that does not fit the caller's buffer is refused, not clipped: half a domain name is a
// different domain name, and a resolver would query it.
void test_output_buffer_bounds(void)
{
    uint8_t wire[64];
    // "abc.de" encodes as 3 a b c 2 d e 0 = 8 octets, and reads back as 6 text octets plus a NUL.
    size_t n = encode("abc.de", wire, sizeof(wire));
    TEST_ASSERT_EQUAL_size_t(8, n);

    char exact[7];
    TEST_ASSERT_TRUE(decode(wire, n, 0, exact, sizeof(exact), PROTO_FALSE));
    TEST_ASSERT_EQUAL_STRING("abc.de", exact);

    char one_short[6];
    TEST_ASSERT_FALSE(decode(wire, n, 0, one_short, sizeof(one_short), PROTO_FALSE));

    char none[1];
    TEST_ASSERT_FALSE(decode(wire, n, 0, none, 0, PROTO_FALSE));
    TEST_ASSERT_FALSE(decode(wire, n, 0, NULL, sizeof(none), PROTO_FALSE));
    TEST_ASSERT_FALSE(decode(NULL, n, 0, none, sizeof(none), PROTO_FALSE));
}

// The encoder needs room for every length octet, every label and the root octet. One octet short of
// that writes nothing.
void test_encode_buffer_bounds(void)
{
    uint8_t out[32];
    // "a.b" encodes as 1 a 1 b 0 = 5 octets, so four octets of room is a refusal.
    TEST_ASSERT_EQUAL_size_t(0, encode("a.b", out, 4));
    TEST_ASSERT_EQUAL_size_t(5, encode("a.b", out, 5));

    TEST_ASSERT_EQUAL_size_t(0, encode("a.b", NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, encode(NULL, out, sizeof(out)));
}

// An empty label inside a name has no encoding: a length octet of zero is the root, so "a..b" would
// serialize to a name that ends after "a".
//
// The lone "." falls under the same rule here. RFC 1035 sec 5.1 spells the root as a single dot in
// presentation format, so that spelling is one this encoder does not take; its input form for the
// root is the empty string, per the module's own contract.
void test_empty_labels_inside_a_name_are_refused(void)
{
    uint8_t out[32];
    TEST_ASSERT_EQUAL_size_t(0, encode("a..b", out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, encode(".a", out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, encode("a...", out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, encode(".", out, sizeof(out)));
}

// RFC 1035 sec 2.3.3: "all comparisons between domain names ... should be done in a case-insensitive
// manner", so the case a name was written in must not change what it matches.
void test_case_insensitive_comparison(void)
{
    TEST_ASSERT_TRUE(eq("example.com", "EXAMPLE.COM"));
    TEST_ASSERT_TRUE(eq("ExAmPlE.cOm", "eXaMpLe.CoM"));
    TEST_ASSERT_TRUE(eq("", ""));
    TEST_ASSERT_TRUE(eq("a", "A"));

    TEST_ASSERT_FALSE(eq("example.com", "example.org"));
    TEST_ASSERT_FALSE(eq("example.com", "example.com.")); // the trailing dot is an octet
    TEST_ASSERT_FALSE(eq("example.com", "example.co"));   // a prefix is not a match
    TEST_ASSERT_FALSE(eq("example.co", "example.com"));
    TEST_ASSERT_FALSE(eq("", "a"));

    // Only A-Z folds: the octets either side of the alphabetic ranges keep their identity, so
    // '_' (0x5F) and 0x7F do not collide with anything.
    TEST_ASSERT_FALSE(eq("_x", "?x")); // 0x5F vs 0x3F
    TEST_ASSERT_TRUE(eq("_http._tcp", "_HTTP._TCP"));

    TEST_ASSERT_FALSE(eq(NULL, "a"));
    TEST_ASSERT_FALSE(eq("a", NULL));
}

// The pointer hop cap is a fixed structural bound, and a chain within it still resolves: eight
// pointers laid end to end decode, the ninth does not.
void test_pointer_hop_cap(void)
{
    TEST_ASSERT_EQUAL_UINT(8u, PROTOCORE_DNS_PTR_HOPS);
    TEST_ASSERT_EQUAL_UINT(63u, PROTOCORE_DNS_LABEL_MAX);

    // Nine pointers, each aimed two octets further on, with a real label after the last.
    uint8_t msg[64];
    memset(msg, 0, sizeof(msg));
    const size_t chain = 9;
    for (size_t i = 0; i < chain; i++)
    {
        msg[i * 2] = 0xC0;
        msg[i * 2 + 1] = (uint8_t)((i + 1) * 2);
    }
    msg[chain * 2] = 1;
    msg[chain * 2 + 1] = 'z';
    msg[chain * 2 + 2] = 0;

    char out[64];
    // Entering at the second link follows eight pointers, exactly what the cap admits.
    TEST_ASSERT_TRUE(decode(msg, sizeof(msg), 2, out, sizeof(out), PROTO_TRUE));
    TEST_ASSERT_EQUAL_STRING("z", out);
    TEST_ASSERT_EQUAL_size_t(4, DnsWireV.next);

    // Entering at the head follows nine, one past it.
    TEST_ASSERT_FALSE(decode(msg, sizeof(msg), 0, out, sizeof(out), PROTO_TRUE));
}

// A failed decode reports next = 0 and a failed encode reports n = 0, so a caller that walks a
// record by these counters cannot be advanced by a name that was never read.
void test_failure_reports_zero_progress(void)
{
    char out[8];
    static const uint8_t TRUNC[3] = {5, 'a', 'b'};
    TEST_ASSERT_FALSE(decode(TRUNC, sizeof(TRUNC), 0, out, sizeof(out), PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(0, DnsWireV.next);

    uint8_t wire[4];
    TEST_ASSERT_EQUAL_size_t(0, encode("toolongforthisbuffer", wire, sizeof(wire)));
    TEST_ASSERT_EQUAL_size_t(0, DnsWireV.n);
}
