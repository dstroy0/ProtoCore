// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Happy Eyeballs v2 destination ordering and attempt gate
// (network_drivers/transport/happy_eyeballs/happy_eyeballs.h).
//
// RFC 8305 sec 4 states the one property that makes this module worth having: "Whichever address
// family is first in the list should be followed by an address of the other address family; that
// is, if the first address in the sorted list is IPv6, then the first IPv4 address should be moved
// up in the list to be second in the list." test_rfc8305_4_interleaves_families is that sentence as
// a test - without it, an impaired IPv6 path makes a client walk every AAAA before its first A, and
// the whole fallback is lost. sec 5 and sec 8 fix the Connection Attempt Delay the gate enforces.

#include "network_drivers/transport/happy_eyeballs/happy_eyeballs.h"
#include "shared/ip/ip.h"
#include <string.h>

#include <unity.h>

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static protocore_ip addr(const char *text)
{
    protocore_ip ip;
    IpV.args.text = text;
    IpV.args.out = &ip;
    Ip.parse(ip_work);
    TEST_ASSERT_TRUE_MESSAGE(IpV.ok, text);
    return ip;
}

// The canonical text of list[i], so an ordering assertion reads as the addresses themselves.
static const char *at(const protocore_ip *list, size_t i)
{
    static char buf[PROTOCORE_IP_STR_MAX];
    IpV.args.ip = &list[i];
    IpV.args.buf = buf;
    IpV.args.cap = sizeof(buf);
    Ip.format(ip_work);
    return buf;
}

// RFC 8305 sec 4: "if the first address in the sorted list is IPv6, then the first IPv4 address
// should be moved up in the list to be second in the list", and successive attempts keep
// alternating. Every address below is global, so the sort leaves only the family rule to act.
// One score, captured. Two scores are compared in a single statement below, and the namespace
// reports one result at a time, so each call's value is taken out before the next call runs.
static int pref(const protocore_ip *ip)
{
    HappyEyeballsV.pref_args.ip = ip;
    HappyEyeballs.pref(protocore_happy_eyeballs_span());
    return HappyEyeballsV.n;
}

void test_rfc8305_4_interleaves_families(void)
{
    protocore_ip list[6];
    list[0] = addr("2001:db8::1");
    list[1] = addr("2001:db8::2");
    list[2] = addr("2001:db8::3");
    list[3] = addr("198.51.100.1");
    list[4] = addr("198.51.100.2");
    list[5] = addr("198.51.100.3");

    HappyEyeballsV.order_args.list = list;
    HappyEyeballsV.order_args.n = 6;
    HappyEyeballs.order(protocore_happy_eyeballs_span());

    TEST_ASSERT_EQUAL_STRING("2001:db8::1", at(list, 0));
    TEST_ASSERT_EQUAL_STRING("198.51.100.1", at(list, 1));
    TEST_ASSERT_EQUAL_STRING("2001:db8::2", at(list, 2));
    TEST_ASSERT_EQUAL_STRING("198.51.100.2", at(list, 3));
    TEST_ASSERT_EQUAL_STRING("2001:db8::3", at(list, 4));
    TEST_ASSERT_EQUAL_STRING("198.51.100.3", at(list, 5));
}

// Same sentence read the other way: when the first sorted address is IPv4 the alternation starts
// with IPv4. Loopback outranks nothing here - all six are global - so the leading family is decided
// by the list, and an all-IPv4 list is left alone.
void test_rfc8305_4_leading_family_follows_the_first_address(void)
{
    // Only IPv4 candidates: nothing to interleave with, order preserved.
    protocore_ip v4only[3];
    v4only[0] = addr("198.51.100.1");
    v4only[1] = addr("198.51.100.2");
    v4only[2] = addr("198.51.100.3");
    HappyEyeballsV.order_args.list = v4only;
    HappyEyeballsV.order_args.n = 3;
    HappyEyeballs.order(protocore_happy_eyeballs_span());
    TEST_ASSERT_EQUAL_STRING("198.51.100.1", at(v4only, 0));
    TEST_ASSERT_EQUAL_STRING("198.51.100.2", at(v4only, 1));
    TEST_ASSERT_EQUAL_STRING("198.51.100.3", at(v4only, 2));

    // Only IPv6 candidates, likewise.
    protocore_ip v6only[3];
    v6only[0] = addr("2001:db8::1");
    v6only[1] = addr("2001:db8::2");
    v6only[2] = addr("2001:db8::3");
    HappyEyeballsV.order_args.list = v6only;
    HappyEyeballsV.order_args.n = 3;
    HappyEyeballs.order(protocore_happy_eyeballs_span());
    TEST_ASSERT_EQUAL_STRING("2001:db8::1", at(v6only, 0));
    TEST_ASSERT_EQUAL_STRING("2001:db8::2", at(v6only, 1));
    TEST_ASSERT_EQUAL_STRING("2001:db8::3", at(v6only, 2));

    // One of each: two candidates alternate trivially, whichever family leads.
    protocore_ip pair[2];
    pair[0] = addr("198.51.100.9");
    pair[1] = addr("2001:db8::9");
    HappyEyeballsV.order_args.list = pair;
    HappyEyeballsV.order_args.n = 2;
    HappyEyeballs.order(protocore_happy_eyeballs_span());
    TEST_ASSERT_EQUAL_STRING("2001:db8::9", at(pair, 0)); // native v6 outranks v4 at equal scope
    TEST_ASSERT_EQUAL_STRING("198.51.100.9", at(pair, 1));
}

// The interleave drains the shorter family and then continues with the longer one, so no candidate
// is dropped and none is duplicated whatever the family counts are.
void test_interleave_drains_the_shorter_family(void)
{
    protocore_ip list[5];
    list[0] = addr("2001:db8::1");
    list[1] = addr("2001:db8::2");
    list[2] = addr("2001:db8::3");
    list[3] = addr("2001:db8::4");
    list[4] = addr("198.51.100.1");

    HappyEyeballsV.order_args.list = list;
    HappyEyeballsV.order_args.n = 5;
    HappyEyeballs.order(protocore_happy_eyeballs_span());

    TEST_ASSERT_EQUAL_STRING("2001:db8::1", at(list, 0));
    TEST_ASSERT_EQUAL_STRING("198.51.100.1", at(list, 1));
    TEST_ASSERT_EQUAL_STRING("2001:db8::2", at(list, 2));
    TEST_ASSERT_EQUAL_STRING("2001:db8::3", at(list, 3));
    TEST_ASSERT_EQUAL_STRING("2001:db8::4", at(list, 4));
}

// RFC 8305 sec 4: "the client MUST sort the addresses received up to this point using Destination
// Address Selection ([RFC6724], Section 6)". The scope ladder that selection walks is the one this
// score follows: a global destination is preferred to a site/ULA one, that to link-local, and an
// unusable address (rule 1) comes last.
void test_preference_follows_the_scope_ladder(void)
{
    protocore_ip global6 = addr("2001:db8::1");
    protocore_ip ula = addr("fd00::1");
    protocore_ip ll6 = addr("fe80::1");
    protocore_ip lo6 = addr("::1");
    protocore_ip mc6 = addr("ff02::1");
    protocore_ip any6 = addr("::");

    TEST_ASSERT_TRUE(pref(&global6) > pref(&ula));
    TEST_ASSERT_TRUE(pref(&ula) > pref(&ll6));
    TEST_ASSERT_TRUE(pref(&ll6) > pref(&lo6));
    TEST_ASSERT_TRUE(pref(&lo6) > pref(&mc6));
    TEST_ASSERT_TRUE(pref(&mc6) > pref(&any6));

    // The same ladder over IPv4 (RFC 1918 private, RFC 3927 link-local, RFC 1122 loopback).
    protocore_ip global4 = addr("198.51.100.1");
    protocore_ip priv4 = addr("10.0.0.1");
    protocore_ip ll4 = addr("169.254.1.1");
    protocore_ip lo4 = addr("127.0.0.1");
    TEST_ASSERT_TRUE(pref(&global4) > pref(&priv4));
    TEST_ASSERT_TRUE(pref(&priv4) > pref(&ll4));
    TEST_ASSERT_TRUE(pref(&ll4) > pref(&lo4));

    // Within one scope, native IPv6 is tried before IPv4 (RFC 6724 rule 10 default policy table).
    TEST_ASSERT_TRUE(pref(&global6) > pref(&global4));
    TEST_ASSERT_TRUE(pref(&priv4) > pref(&ll6));

    // An address that names nothing has no preference at all.
    protocore_ip none;
    memset(&none, 0, sizeof(none));
    none.family = PROTOCORE_IP_NONE;
    HappyEyeballsV.pref_args.ip = &none;
    HappyEyeballs.pref(protocore_happy_eyeballs_span());
    TEST_ASSERT_EQUAL_INT(-1, HappyEyeballsV.n);
    HappyEyeballsV.pref_args.ip = NULL;
    HappyEyeballs.pref(protocore_happy_eyeballs_span());
    TEST_ASSERT_EQUAL_INT(-1, HappyEyeballsV.n);
}

// RFC 4291 sec 2.5.5.2: an IPv4-mapped address (::ffff:a.b.c.d) carries an IPv4 destination inside
// an IPv6 literal, so it must race as IPv4 and never be counted toward the IPv6 half.
void test_v4_mapped_counts_as_ipv4(void)
{
    protocore_ip mapped = addr("::ffff:198.51.100.7");
    protocore_ip plain4 = addr("198.51.100.7");
    protocore_ip native6 = addr("2001:db8::7");

    TEST_ASSERT_EQUAL_INT(pref(&plain4), pref(&mapped));
    TEST_ASSERT_TRUE(pref(&native6) > pref(&mapped));

    // A list of one native v6 and two mapped v4s alternates as if the mapped ones were plain v4.
    protocore_ip list[3];
    list[0] = addr("::ffff:198.51.100.1");
    list[1] = addr("2001:db8::1");
    list[2] = addr("::ffff:198.51.100.2");
    HappyEyeballsV.order_args.list = list;
    HappyEyeballsV.order_args.n = 3;
    HappyEyeballs.order(protocore_happy_eyeballs_span());
    TEST_ASSERT_EQUAL_STRING("2001:db8::1", at(list, 0));
    TEST_ASSERT_EQUAL_STRING("::ffff:198.51.100.1", at(list, 1));
    TEST_ASSERT_EQUAL_STRING("::ffff:198.51.100.2", at(list, 2));
}

// Sorting is stable: two candidates of equal preference keep the order the resolver returned them
// in, which is what lets a caller's own RTT or last-used ordering survive the sort (sec 4's added
// rules sit between RFC 6724 rules 8 and 9, above nothing this score decides).
void test_equal_preference_keeps_input_order(void)
{
    protocore_ip list[4];
    list[0] = addr("198.51.100.3");
    list[1] = addr("198.51.100.1");
    list[2] = addr("198.51.100.4");
    list[3] = addr("198.51.100.2");

    HappyEyeballsV.order_args.list = list;
    HappyEyeballsV.order_args.n = 4;
    HappyEyeballs.order(protocore_happy_eyeballs_span());

    TEST_ASSERT_EQUAL_STRING("198.51.100.3", at(list, 0));
    TEST_ASSERT_EQUAL_STRING("198.51.100.1", at(list, 1));
    TEST_ASSERT_EQUAL_STRING("198.51.100.4", at(list, 2));
    TEST_ASSERT_EQUAL_STRING("198.51.100.2", at(list, 3));
}

// A less-preferred family member still sorts ahead of a more-preferred family member of a lower
// scope: scope dominates, so a global IPv4 beats a link-local IPv6 and leads the interleave.
void test_scope_beats_family(void)
{
    protocore_ip list[4];
    list[0] = addr("fe80::1");
    list[1] = addr("198.51.100.1");
    list[2] = addr("fe80::2");
    list[3] = addr("198.51.100.2");

    HappyEyeballsV.order_args.list = list;
    HappyEyeballsV.order_args.n = 4;
    HappyEyeballs.order(protocore_happy_eyeballs_span());

    TEST_ASSERT_EQUAL_STRING("198.51.100.1", at(list, 0));
    TEST_ASSERT_EQUAL_STRING("fe80::1", at(list, 1));
    TEST_ASSERT_EQUAL_STRING("198.51.100.2", at(list, 2));
    TEST_ASSERT_EQUAL_STRING("fe80::2", at(list, 3));
}

// RFC 8305 sec 5: "one connection attempt to a single address is started first, followed by the
// others in the list, one at a time", separated by the Connection Attempt Delay. The gate opens at
// the delay, not before it.
void test_rfc8305_5_attempt_delay_gate(void)
{
    // sec 8: "Connection Attempt Delay ... Recommended to be 250 milliseconds."
    TEST_ASSERT_EQUAL_UINT32(250u, (uint32_t)PROTOCORE_HE_ATTEMPT_DELAY_MS);

    HappyEyeballsV.attempt_due_args.last_start_ms = 1000u;
    HappyEyeballsV.attempt_due_args.now_ms = 1000u;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 250u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_FALSE(HappyEyeballsV.ok);
    HappyEyeballsV.attempt_due_args.last_start_ms = 1000u;
    HappyEyeballsV.attempt_due_args.now_ms = 1249u;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 250u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_FALSE(HappyEyeballsV.ok);
    HappyEyeballsV.attempt_due_args.last_start_ms = 1000u;
    HappyEyeballsV.attempt_due_args.now_ms = 1250u;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 250u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_TRUE(HappyEyeballsV.ok);
    HappyEyeballsV.attempt_due_args.last_start_ms = 1000u;
    HappyEyeballsV.attempt_due_args.now_ms = 5000u;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 250u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_TRUE(HappyEyeballsV.ok);

    // sec 8: "Minimum Connection Attempt Delay ... Recommended to be 100 milliseconds. MUST NOT be
    // less than 10 milliseconds." Both floors behave the same way at their own boundary.
    HappyEyeballsV.attempt_due_args.last_start_ms = 0u;
    HappyEyeballsV.attempt_due_args.now_ms = 99u;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 100u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_FALSE(HappyEyeballsV.ok);
    HappyEyeballsV.attempt_due_args.last_start_ms = 0u;
    HappyEyeballsV.attempt_due_args.now_ms = 100u;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 100u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_TRUE(HappyEyeballsV.ok);
    HappyEyeballsV.attempt_due_args.last_start_ms = 0u;
    HappyEyeballsV.attempt_due_args.now_ms = 9u;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 10u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_FALSE(HappyEyeballsV.ok);
    HappyEyeballsV.attempt_due_args.last_start_ms = 0u;
    HappyEyeballsV.attempt_due_args.now_ms = 10u;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 10u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_TRUE(HappyEyeballsV.ok);

    // sec 8: "Maximum Connection Attempt Delay ... Recommended to be 2 seconds."
    HappyEyeballsV.attempt_due_args.last_start_ms = 0u;
    HappyEyeballsV.attempt_due_args.now_ms = 1999u;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 2000u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_FALSE(HappyEyeballsV.ok);
    HappyEyeballsV.attempt_due_args.last_start_ms = 0u;
    HappyEyeballsV.attempt_due_args.now_ms = 2000u;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 2000u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_TRUE(HappyEyeballsV.ok);

    // A zero delay is always due.
    HappyEyeballsV.attempt_due_args.last_start_ms = 1234u;
    HappyEyeballsV.attempt_due_args.now_ms = 1234u;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 0u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_TRUE(HappyEyeballsV.ok);
}

// The millisecond clock is a uint32 and wraps every 49.7 days. The gate reads the elapsed time as a
// modular difference, so an attempt started just before the wrap still becomes due 250 ms later
// rather than waiting out another whole period.
void test_attempt_gate_survives_the_millis_wrap(void)
{
    const uint32_t before = 0xFFFFFF00u; // 256 ms before the wrap
    HappyEyeballsV.attempt_due_args.last_start_ms = before;
    HappyEyeballsV.attempt_due_args.now_ms = before + 249u;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 250u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_FALSE(HappyEyeballsV.ok);
    HappyEyeballsV.attempt_due_args.last_start_ms = before;
    HappyEyeballsV.attempt_due_args.now_ms = before + 250u;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 250u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_TRUE(HappyEyeballsV.ok);

    // before + 300 wraps past zero to 0x2c; the difference is still 300.
    TEST_ASSERT_EQUAL_UINT32(0x2cu, (uint32_t)(before + 300u));
    HappyEyeballsV.attempt_due_args.last_start_ms = before;
    HappyEyeballsV.attempt_due_args.now_ms = 0x2cu;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 250u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_TRUE(HappyEyeballsV.ok);
    HappyEyeballsV.attempt_due_args.last_start_ms = before;
    HappyEyeballsV.attempt_due_args.now_ms = 0x2cu;
    HappyEyeballsV.attempt_due_args.attempt_delay_ms = 301u;
    HappyEyeballs.attempt_due(protocore_happy_eyeballs_span());
    TEST_ASSERT_FALSE(HappyEyeballsV.ok);
}

// Degenerate lists are left exactly as they are rather than read past.
void test_short_and_null_lists_are_left_alone(void)
{
    protocore_ip one = addr("2001:db8::1");
    protocore_ip saved = one;
    HappyEyeballsV.order_args.list = &one;
    HappyEyeballsV.order_args.n = 1;
    HappyEyeballs.order(protocore_happy_eyeballs_span());
    TEST_ASSERT_EQUAL_INT(0, memcmp(&saved, &one, sizeof(one)));

    HappyEyeballsV.order_args.list = &one;
    HappyEyeballsV.order_args.n = 0;
    HappyEyeballs.order(protocore_happy_eyeballs_span());
    TEST_ASSERT_EQUAL_INT(0, memcmp(&saved, &one, sizeof(one)));

    HappyEyeballsV.order_args.list = NULL;
    HappyEyeballsV.order_args.n = 4;
    HappyEyeballs.order(protocore_happy_eyeballs_span()); // no list to read
}

// Beyond PROTOCORE_HE_MAX the fixed interleave scratch cannot hold the list, so the ordering stops
// at the sort: every candidate is still present and preference order still holds.
void test_oversized_lists_are_sorted_without_interleaving(void)
{
    protocore_ip list[PROTOCORE_HE_MAX + 1];
    for (size_t i = 0; i < PROTOCORE_HE_MAX + 1; i++)
    {
        // Alternate a link-local v6 and a global v4 so the sort has something to do.
        list[i] = (i % 2 == 0) ? addr("fe80::1") : addr("198.51.100.1");
    }
    HappyEyeballsV.order_args.list = list;
    HappyEyeballsV.order_args.n = PROTOCORE_HE_MAX + 1;
    HappyEyeballs.order(protocore_happy_eyeballs_span());

    // Every global v4 comes first, then every link-local v6: sorted, not interleaved.
    size_t half = (PROTOCORE_HE_MAX + 1) / 2;
    for (size_t i = 0; i < half; i++)
    {
        TEST_ASSERT_EQUAL_STRING("198.51.100.1", at(list, i));
    }
    for (size_t i = half; i < PROTOCORE_HE_MAX + 1; i++)
    {
        TEST_ASSERT_EQUAL_STRING("fe80::1", at(list, i));
    }
}
