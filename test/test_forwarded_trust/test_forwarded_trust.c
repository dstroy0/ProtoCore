// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the trusted-reverse-proxy forwarded-client resolver (services/security/forwarded_trust).
// A Forwarded / X-Forwarded-For address is client-spoofable, so it may only be believed when the real
// TCP peer is a configured trusted upstream. The resolver is pure (no sockets), so the host drives it
// directly. The security-critical property under test: a direct/untrusted peer's forwarded header is
// NEVER honored, and any malformed / obfuscated / unspecified token falls back to the TCP peer.

#include "network_drivers/network/ip.h"
#include "services/security/forwarded_trust/forwarded_trust.h"
#include <unity.h>

static pc_ip v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return pc_ip_from_v4_octets(a, b, c, d);
}

static pc_ip v6(const char *s)
{
    pc_ip ip;
    ip.family = PC_IP_NONE;
    TEST_ASSERT_TRUE(pc_ip_parse(s, &ip));
    return ip;
}

void setUp()
{
    pc_forwarded_trust_reset();
}
void tearDown()
{
}

// An empty table trusts no header: contains() is always false and effective_ip keeps the TCP peer.
void test_empty_table_trusts_nothing()
{
    pc_ip peer = v4(203, 0, 113, 7);
    TEST_ASSERT_FALSE(pc_forwarded_trust_contains(&peer));
    pc_ip out;
    TEST_ASSERT_FALSE(pc_forwarded_effective_ip(&peer, "198.51.100.9", &out));
    TEST_ASSERT_TRUE(pc_ip_equal(&out, &peer)); // fell back to the real peer
}

// A v4 CIDR matches inside its range and rejects outside / a v6 peer.
void test_v4_cidr_membership()
{
    TEST_ASSERT_TRUE(pc_forwarded_trust_add_cidr("10.0.0.0/8"));
    pc_ip in = v4(10, 4, 4, 1);
    pc_ip out_of = v4(11, 0, 0, 1);
    pc_ip six = v6("2001:db8::1");
    TEST_ASSERT_TRUE(pc_forwarded_trust_contains(&in));
    TEST_ASSERT_FALSE(pc_forwarded_trust_contains(&out_of));
    TEST_ASSERT_FALSE(pc_forwarded_trust_contains(&six)); // family mismatch never matches a v4 rule
}

// A v6 CIDR matches; a bare address is a host route (exact match only).
void test_v6_cidr_and_host_route()
{
    TEST_ASSERT_TRUE(pc_forwarded_trust_add_cidr("2001:db8::/32"));
    TEST_ASSERT_TRUE(pc_forwarded_trust_add_cidr("192.0.2.5")); // bare = /32 host route
    pc_ip v6in = v6("2001:db8:abcd::1");
    pc_ip host = v4(192, 0, 2, 5);
    pc_ip host_nbr = v4(192, 0, 2, 6);
    TEST_ASSERT_TRUE(pc_forwarded_trust_contains(&v6in));
    TEST_ASSERT_TRUE(pc_forwarded_trust_contains(&host));
    TEST_ASSERT_FALSE(pc_forwarded_trust_contains(&host_nbr)); // the neighbor is not the host route
}

// Malformed CIDR strings are rejected and add nothing.
void test_add_cidr_rejects_malformed()
{
    TEST_ASSERT_FALSE(pc_forwarded_trust_add_cidr(NULL));
    TEST_ASSERT_FALSE(pc_forwarded_trust_add_cidr("not-an-ip"));
    TEST_ASSERT_FALSE(pc_forwarded_trust_add_cidr("10.0.0.0/"));      // empty prefix
    TEST_ASSERT_FALSE(pc_forwarded_trust_add_cidr("10.0.0.0/33"));    // over-long v4 prefix
    TEST_ASSERT_FALSE(pc_forwarded_trust_add_cidr("2001:db8::/129")); // over-long v6 prefix
    TEST_ASSERT_FALSE(pc_forwarded_trust_add_cidr("10.0.0.0/x"));     // non-digit prefix
    pc_ip any = v4(10, 0, 0, 1);
    TEST_ASSERT_FALSE(pc_forwarded_trust_contains(&any)); // nothing was added
}

// The table is bounded: adding past PC_TRUSTED_PROXY_MAX fails.
void test_table_full()
{
    for (int i = 0; i < PC_TRUSTED_PROXY_MAX; i++)
    {
        char cidr[24];
        snprintf(cidr, sizeof(cidr), "10.%d.0.0/16", i);
        TEST_ASSERT_TRUE(pc_forwarded_trust_add_cidr(cidr));
    }
    TEST_ASSERT_FALSE(pc_forwarded_trust_add_cidr("172.16.0.0/12")); // one past capacity
}

// A trusted proxy's valid forwarded client is honored (the lockout keys on the real client).
void test_trusted_peer_honors_forwarded()
{
    TEST_ASSERT_TRUE(pc_forwarded_trust_add_cidr("10.0.0.0/8"));
    pc_ip proxy = v4(10, 1, 2, 3);
    pc_ip out;
    TEST_ASSERT_TRUE(pc_forwarded_effective_ip(&proxy, "198.51.100.42", &out));
    pc_ip client = v4(198, 51, 100, 42);
    TEST_ASSERT_TRUE(pc_ip_equal(&out, &client));
}

// A trusted proxy may forward a v6 client even over a v4 hop.
void test_trusted_peer_honors_v6_forwarded()
{
    TEST_ASSERT_TRUE(pc_forwarded_trust_add_cidr("10.0.0.0/8"));
    pc_ip proxy = v4(10, 9, 9, 9);
    pc_ip out;
    TEST_ASSERT_TRUE(pc_forwarded_effective_ip(&proxy, "2001:db8::abcd", &out));
    pc_ip client = v6("2001:db8::abcd");
    TEST_ASSERT_TRUE(pc_ip_equal(&out, &client));
}

// THE security property: an untrusted (direct) peer's forwarded header is IGNORED - no spoofing.
void test_untrusted_peer_ignores_forwarded()
{
    TEST_ASSERT_TRUE(pc_forwarded_trust_add_cidr("10.0.0.0/8"));
    pc_ip attacker = v4(203, 0, 113, 66); // not in the trusted range
    pc_ip out;
    // The attacker sets X-Forwarded-For to a victim's address to try to lock the victim out.
    TEST_ASSERT_FALSE(pc_forwarded_effective_ip(&attacker, "198.51.100.1", &out));
    TEST_ASSERT_TRUE(pc_ip_equal(&out, &attacker)); // keyed on the attacker's own address
}

// A trusted proxy with a malformed / obfuscated / unspecified / absent token keeps the TCP peer.
void test_trusted_peer_bad_token_falls_back()
{
    TEST_ASSERT_TRUE(pc_forwarded_trust_add_cidr("10.0.0.0/8"));
    pc_ip proxy = v4(10, 0, 0, 5);
    pc_ip out;

    TEST_ASSERT_FALSE(pc_forwarded_effective_ip(&proxy, "unknown", &out)); // RFC 7239 obfuscated
    TEST_ASSERT_TRUE(pc_ip_equal(&out, &proxy));

    TEST_ASSERT_FALSE(pc_forwarded_effective_ip(&proxy, NULL, &out)); // no header
    TEST_ASSERT_TRUE(pc_ip_equal(&out, &proxy));

    TEST_ASSERT_FALSE(pc_forwarded_effective_ip(&proxy, "", &out)); // empty
    TEST_ASSERT_TRUE(pc_ip_equal(&out, &proxy));

    TEST_ASSERT_FALSE(pc_forwarded_effective_ip(&proxy, "0.0.0.0", &out)); // unspecified
    TEST_ASSERT_TRUE(pc_ip_equal(&out, &proxy));
}

// Null-argument guards: null out fails; null peer leaves an unspecified out (never uninitialized).
void test_null_guards()
{
    pc_ip peer = v4(10, 0, 0, 1);
    TEST_ASSERT_FALSE(pc_forwarded_effective_ip(&peer, "1.2.3.4", NULL));
    pc_ip out;
    TEST_ASSERT_FALSE(pc_forwarded_effective_ip(NULL, "1.2.3.4", &out));
    TEST_ASSERT_TRUE(pc_ip_is_unspecified(&out)); // written, not left uninitialized
}

// pc_forwarded_trust_add() rejects a null network pointer outright.
void test_add_rejects_null_network()
{
    TEST_ASSERT_FALSE(pc_forwarded_trust_add(NULL, 8));
}

// pc_forwarded_trust_add() rejects an unrecognized address family (bits stays negative) and, on a
// separate call, an in-family but over-long prefix - two distinct ways the guard can fire.
void test_add_rejects_bad_family_and_over_long_prefix()
{
    pc_ip unset;
    unset.family = PC_IP_NONE;
    TEST_ASSERT_FALSE(pc_forwarded_trust_add(&unset, 0)); // family not V4/V6 -> bits stays negative

    pc_ip v4addr = v4(10, 0, 0, 1);
    TEST_ASSERT_FALSE(pc_forwarded_trust_add(&v4addr, 33)); // valid family, prefix past 32
}

// A CIDR string whose address portion overruns the bounded parse buffer is rejected before parsing.
void test_add_cidr_rejects_overlong_address()
{
    // PC_IP_STR_MAX is 46; this address text alone is well past that, with no slash reached first.
    const char *too_long = "1111111111111111111111111111111111111111111111111111111111";
    TEST_ASSERT_FALSE(pc_forwarded_trust_add_cidr(too_long));
}

// A prefix character below '0' (not just above '9') is also a non-digit and is rejected.
void test_add_cidr_rejects_prefix_below_digit_range()
{
    TEST_ASSERT_FALSE(pc_forwarded_trust_add_cidr("10.0.0.0/.5"));
}

// pc_forwarded_trust_contains() rejects a null peer pointer outright.
void test_contains_rejects_null_peer()
{
    TEST_ASSERT_FALSE(pc_forwarded_trust_contains(NULL));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_table_trusts_nothing);
    RUN_TEST(test_v4_cidr_membership);
    RUN_TEST(test_v6_cidr_and_host_route);
    RUN_TEST(test_add_cidr_rejects_malformed);
    RUN_TEST(test_table_full);
    RUN_TEST(test_trusted_peer_honors_forwarded);
    RUN_TEST(test_trusted_peer_honors_v6_forwarded);
    RUN_TEST(test_untrusted_peer_ignores_forwarded);
    RUN_TEST(test_trusted_peer_bad_token_falls_back);
    RUN_TEST(test_null_guards);
    RUN_TEST(test_add_rejects_null_network);
    RUN_TEST(test_add_rejects_bad_family_and_over_long_prefix);
    RUN_TEST(test_add_cidr_rejects_overlong_address);
    RUN_TEST(test_add_cidr_rejects_prefix_below_digit_range);
    RUN_TEST(test_contains_rejects_null_peer);
    return UNITY_END();
}
