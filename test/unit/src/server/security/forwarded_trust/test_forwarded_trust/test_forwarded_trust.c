// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the trusted-reverse-proxy client resolver
// (server/security/forwarded_trust/forwarded_trust.h).
//
// RFC 7239 sec 8.1 states the rule this module exists to enforce: "the header field is entirely
// under the control of the client ... an attacker can forge it", so a forwarded client address may
// only be believed when the connection's own peer is an upstream the operator trusts.
// test_an_untrusted_peer_can_never_forge_a_client_address is therefore the load-bearing case - if
// it fails, a direct client spoofs its way out of, or another address into, every per-address
// control the server has. The CIDR containment the trust table is keyed on is RFC 4632 sec 3.1.
//
// PROTOCORE_TRUSTED_PROXY_MAX is small, so the table cases below are written against the configured
// bound rather than a fixed number.

#include "server/security/forwarded_trust/forwarded_trust.h"
#include "shared/ip/ip.h"

#include <unity.h>

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

void setUp(void)
{
    ForwardedTrust.reset(protocore_forwarded_trust_span());
}
void tearDown(void)
{
}

static protocore_ip parsed(const char *text)
{
    protocore_ip ip;
    IpV.args.text = text;
    IpV.args.out = &ip;
    Ip.parse(ip_work);
    TEST_ASSERT_TRUE_MESSAGE(IpV.ok, text);
    return ip;
}

static proto_bool same(const protocore_ip *a, const protocore_ip *b)
{
    IpV.args.ip = a;
    IpV.args.b = b;
    Ip.equal(ip_work);
    return IpV.ok;
}

// An empty table trusts nothing, so no header is ever believed. This is the fail-safe default: a
// server with no proxy configured is a server behind no proxy.
void test_an_empty_table_trusts_nothing(void)
{
    const protocore_ip peer = parsed("10.0.0.5");
    ForwardedTrustV.contains_args.peer = &peer;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);

    protocore_ip out;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.peer = &peer;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.fwd_ip_str = "203.0.113.9";
    ForwardedTrustV.protocore_forwarded_effective_ip_args.out = &out;
    ForwardedTrust.protocore_forwarded_effective_ip(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
    TEST_ASSERT_TRUE(same(&peer, &out)); // the real TCP peer was kept
}

// RFC 4632 sec 3.1 CIDR containment, at and either side of the prefix boundary.
void test_a_cidr_covers_its_block_and_nothing_else(void)
{
    ForwardedTrustV.add_cidr_args.cidr = "10.0.0.0/8";
    ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);

    const protocore_ip inside_low = parsed("10.0.0.0");
    const protocore_ip inside_high = parsed("10.255.255.255");
    const protocore_ip below = parsed("9.255.255.255");
    const protocore_ip above = parsed("11.0.0.0");
    ForwardedTrustV.contains_args.peer = &inside_low;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    ForwardedTrustV.contains_args.peer = &inside_high;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    ForwardedTrustV.contains_args.peer = &below;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
    ForwardedTrustV.contains_args.peer = &above;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
}

// A bare address with no slash is a host route: exactly that address and no neighbour.
void test_a_bare_address_is_a_host_route(void)
{
    ForwardedTrustV.add_cidr_args.cidr = "192.0.2.10";
    ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);

    const protocore_ip exact = parsed("192.0.2.10");
    const protocore_ip neighbour = parsed("192.0.2.11");
    ForwardedTrustV.contains_args.peer = &exact;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    ForwardedTrustV.contains_args.peer = &neighbour;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
}

// A v6 CIDR covers its own block and no v4 address, whatever the octets: the families are separate.
void test_a_v6_cidr_covers_only_v6(void)
{
    ForwardedTrustV.add_cidr_args.cidr = "2001:db8::/32";
    ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);

    const protocore_ip inside = parsed("2001:db8:1234::1");
    const protocore_ip outside = parsed("2001:db9::1");
    ForwardedTrustV.contains_args.peer = &inside;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    const protocore_ip four = parsed("32.1.13.184"); // the same leading octets, as v4
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    ForwardedTrustV.contains_args.peer = &outside;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
    ForwardedTrustV.contains_args.peer = &four;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
}

// A /0 covers the whole family, which is what an operator writes to trust every upstream.
void test_a_zero_prefix_covers_the_family(void)
{
    ForwardedTrustV.add_cidr_args.cidr = "0.0.0.0/0";
    ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    const protocore_ip a = parsed("8.8.8.8");
    const protocore_ip b = parsed("198.51.100.1");
    const protocore_ip six = parsed("2001:db8::1");
    ForwardedTrustV.contains_args.peer = &a;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    ForwardedTrustV.contains_args.peer = &b;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    ForwardedTrustV.contains_args.peer = &six;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok); // still not the other family
}

// Malformed CIDR text is refused, and refusing it leaves the table trusting nothing rather than
// half-adding a rule.
void test_malformed_cidr_text_is_refused(void)
{
    static const char *const BAD[] = {
        NULL,
        "",
        "/8",
        "10.0.0.0/",
        "10.0.0.0/33", // over the v4 width
        "10.0.0.0/999",
        "10.0.0.0/8x", // a non-digit in the prefix
        "10.0.0.0/-1",
        "999.0.0.0/8", // not an address
        "not-an-address",
        "2001:db8::/129", // over the v6 width
        "1::2::3/64",     // two zero runs
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        ForwardedTrustV.add_cidr_args.cidr = BAD[i];
        ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
        TEST_ASSERT_FALSE_MESSAGE(ForwardedTrustV.ok, BAD[i] ? BAD[i] : "(null)");
    }

    const protocore_ip peer = parsed("10.0.0.5");
    ForwardedTrustV.contains_args.peer = &peer;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
}

// The widest prefix each family allows is accepted, one wider is not.
void test_the_prefix_width_bound_per_family(void)
{
    const protocore_ip four = parsed("10.0.0.1");
    const protocore_ip six = parsed("2001:db8::1");

    ForwardedTrustV.add_args.network = &four;
    ForwardedTrustV.add_args.prefix_len = 32;
    ForwardedTrust.add(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    ForwardedTrustV.add_args.network = &four;
    ForwardedTrustV.add_args.prefix_len = 33;
    ForwardedTrust.add(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);

    ForwardedTrust.reset(protocore_forwarded_trust_span());
    ForwardedTrustV.add_args.network = &six;
    ForwardedTrustV.add_args.prefix_len = 128;
    ForwardedTrust.add(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    ForwardedTrustV.add_args.network = &six;
    ForwardedTrustV.add_args.prefix_len = 129;
    ForwardedTrust.add(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);

    // an address with no family is not a network
    protocore_ip none;
    none.family = PROTOCORE_IP_NONE;
    ForwardedTrust.reset(protocore_forwarded_trust_span());
    ForwardedTrustV.add_args.network = &none;
    ForwardedTrustV.add_args.prefix_len = 0;
    ForwardedTrust.add(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
    ForwardedTrustV.add_args.network = NULL;
    ForwardedTrustV.add_args.prefix_len = 8;
    ForwardedTrust.add(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
}

// The table is bounded: past its capacity an add is refused rather than overwriting a rule the
// operator already installed.
void test_the_table_is_bounded(void)
{
    for (int i = 0; i < PROTOCORE_TRUSTED_PROXY_MAX; i++)
    {
        const protocore_ip net = protocore_ip_from_v4_octets(10, (uint8_t)i, 0, 0);
        ForwardedTrustV.add_args.network = &net;
        ForwardedTrustV.add_args.prefix_len = 16;
        ForwardedTrust.add(protocore_forwarded_trust_span());
        TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    }
    const protocore_ip extra = protocore_ip_from_v4_octets(10, (uint8_t)PROTOCORE_TRUSTED_PROXY_MAX, 0, 0);
    ForwardedTrustV.add_args.network = &extra;
    ForwardedTrustV.add_args.prefix_len = 16;
    ForwardedTrust.add(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
    ForwardedTrustV.contains_args.peer = &extra;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);

    // the rules already installed are untouched
    const protocore_ip first = protocore_ip_from_v4_octets(10, 0, 0, 1);
    ForwardedTrustV.contains_args.peer = &first;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
}

// Reset empties the table, and after it the same peer is no longer trusted.
void test_reset_empties_the_table(void)
{
    ForwardedTrustV.add_cidr_args.cidr = "10.0.0.0/8";
    ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    const protocore_ip peer = parsed("10.1.2.3");
    ForwardedTrustV.contains_args.peer = &peer;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);

    ForwardedTrust.reset(protocore_forwarded_trust_span());
    ForwardedTrustV.contains_args.peer = &peer;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
    ForwardedTrustV.add_cidr_args.cidr = "10.0.0.0/8";
    ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok); // room again
}

// RFC 7239 sec 8.1: the header is client-controlled. A peer outside every trusted CIDR is a client,
// so whatever it claims is discarded and its own TCP address is what the server acts on.
void test_an_untrusted_peer_can_never_forge_a_client_address(void)
{
    ForwardedTrustV.add_cidr_args.cidr = "10.0.0.0/8";
    ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);

    const protocore_ip attacker = parsed("203.0.113.9");
    static const char *const CLAIMS[] = {
        "10.0.0.1",       // claiming to be the proxy itself
        "127.0.0.1",      // claiming to be local
        "198.51.100.7",   // claiming to be somebody else
        "::1",            //
        "2001:db8::dead", //
    };
    for (size_t i = 0; i < sizeof(CLAIMS) / sizeof(CLAIMS[0]); i++)
    {
        protocore_ip out;
        ForwardedTrustV.protocore_forwarded_effective_ip_args.peer = &attacker;
        ForwardedTrustV.protocore_forwarded_effective_ip_args.fwd_ip_str = CLAIMS[i];
        ForwardedTrustV.protocore_forwarded_effective_ip_args.out = &out;
        ForwardedTrust.protocore_forwarded_effective_ip(protocore_forwarded_trust_span());
        TEST_ASSERT_FALSE_MESSAGE(ForwardedTrustV.ok, CLAIMS[i]);
        TEST_ASSERT_TRUE_MESSAGE(same(&attacker, &out), CLAIMS[i]);
    }
}

// A trusted upstream's forwarded client IS believed, for both families.
void test_a_trusted_upstream_is_believed(void)
{
    ForwardedTrustV.add_cidr_args.cidr = "10.0.0.0/8";
    ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    const protocore_ip proxy = parsed("10.0.0.1");

    protocore_ip out;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.peer = &proxy;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.fwd_ip_str = "203.0.113.9";
    ForwardedTrustV.protocore_forwarded_effective_ip_args.out = &out;
    ForwardedTrust.protocore_forwarded_effective_ip(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    const protocore_ip want4 = parsed("203.0.113.9");
    TEST_ASSERT_TRUE(same(&want4, &out));

    ForwardedTrustV.protocore_forwarded_effective_ip_args.peer = &proxy;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.fwd_ip_str = "2001:db8::5";
    ForwardedTrustV.protocore_forwarded_effective_ip_args.out = &out;
    ForwardedTrust.protocore_forwarded_effective_ip(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    const protocore_ip want6 = parsed("2001:db8::5");
    TEST_ASSERT_TRUE(same(&want6, &out));
}

// A trusted upstream that sends no forwarded client, or one that cannot be parsed, falls back to
// the proxy's own address rather than to nothing. RFC 7239 sec 6.3 defines the obfuscated node
// identifier ("_hidden") a proxy may send instead of an address; that is not an address, so it
// falls back too.
void test_a_trusted_upstream_with_no_usable_client_falls_back(void)
{
    ForwardedTrustV.add_cidr_args.cidr = "10.0.0.0/8";
    ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    const protocore_ip proxy = parsed("10.0.0.1");

    static const char *const UNUSABLE[] = {
        NULL,               // no header at all
        "",                 // an empty value
        "_hidden",          // RFC 7239 sec 6.3 obfuscated identifier
        "unknown",          // RFC 7239 sec 6.3 unknown identifier
        "0.0.0.0",          // unspecified, so it names no client
        "::",               //
        "garbage",          //
        "1.2.3",            // truncated dotted quad
        "203.0.113.9:8080", // an address with a port is not an address
    };
    for (size_t i = 0; i < sizeof(UNUSABLE) / sizeof(UNUSABLE[0]); i++)
    {
        protocore_ip out;
        const char *what = UNUSABLE[i] ? UNUSABLE[i] : "(null)";
        ForwardedTrustV.protocore_forwarded_effective_ip_args.peer = &proxy;
        ForwardedTrustV.protocore_forwarded_effective_ip_args.fwd_ip_str = UNUSABLE[i];
        ForwardedTrustV.protocore_forwarded_effective_ip_args.out = &out;
        ForwardedTrust.protocore_forwarded_effective_ip(protocore_forwarded_trust_span());
        TEST_ASSERT_FALSE_MESSAGE(ForwardedTrustV.ok, what);
        TEST_ASSERT_TRUE_MESSAGE(same(&proxy, &out), what);
    }
}

// The destination is always written, whatever the outcome, so a caller that ignores the return
// value still acts on a real address rather than on whatever the buffer held.
void test_the_destination_is_always_written(void)
{
    ForwardedTrustV.add_cidr_args.cidr = "10.0.0.0/8";
    ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    const protocore_ip proxy = parsed("10.0.0.1");
    const protocore_ip direct = parsed("203.0.113.9");

    protocore_ip out;
    out.family = PROTOCORE_IP_V6;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.peer = &proxy;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.fwd_ip_str = "198.51.100.4";
    ForwardedTrustV.protocore_forwarded_effective_ip_args.out = &out;
    ForwardedTrust.protocore_forwarded_effective_ip(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_V4, out.family);

    out.family = PROTOCORE_IP_V6;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.peer = &direct;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.fwd_ip_str = "198.51.100.4";
    ForwardedTrustV.protocore_forwarded_effective_ip_args.out = &out;
    ForwardedTrust.protocore_forwarded_effective_ip(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
    TEST_ASSERT_TRUE(same(&direct, &out));

    // no peer at all leaves an address that names nothing, not a stale one
    out = direct;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.peer = NULL;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.fwd_ip_str = "198.51.100.4";
    ForwardedTrustV.protocore_forwarded_effective_ip_args.out = &out;
    ForwardedTrust.protocore_forwarded_effective_ip(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_NONE, out.family);

    ForwardedTrustV.protocore_forwarded_effective_ip_args.peer = &proxy;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.fwd_ip_str = "198.51.100.4";
    ForwardedTrustV.protocore_forwarded_effective_ip_args.out = NULL;
    ForwardedTrust.protocore_forwarded_effective_ip(protocore_forwarded_trust_span());
    // a null destination is refused rather than written through
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
}

// Several upstream networks can be trusted at once, and a peer in any of them is trusted.
void test_several_upstreams_are_all_trusted(void)
{
    TEST_ASSERT_TRUE(PROTOCORE_TRUSTED_PROXY_MAX >= 2);
    ForwardedTrustV.add_cidr_args.cidr = "10.0.0.0/8";
    ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    ForwardedTrustV.add_cidr_args.cidr = "2001:db8::/32";
    ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);

    const protocore_ip four = parsed("10.9.9.9");
    const protocore_ip six = parsed("2001:db8::99");
    const protocore_ip neither = parsed("198.51.100.1");
    ForwardedTrustV.contains_args.peer = &four;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    ForwardedTrustV.contains_args.peer = &six;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    ForwardedTrustV.contains_args.peer = &neither;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);

    protocore_ip out;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.peer = &six;
    ForwardedTrustV.protocore_forwarded_effective_ip_args.fwd_ip_str = "203.0.113.1";
    ForwardedTrustV.protocore_forwarded_effective_ip_args.out = &out;
    ForwardedTrust.protocore_forwarded_effective_ip(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    const protocore_ip want = parsed("203.0.113.1");
    TEST_ASSERT_TRUE(same(&want, &out));
}

// A null peer is not a trusted upstream.
void test_a_null_peer_is_not_trusted(void)
{
    ForwardedTrustV.add_cidr_args.cidr = "0.0.0.0/0";
    ForwardedTrust.add_cidr(protocore_forwarded_trust_span());
    TEST_ASSERT_TRUE(ForwardedTrustV.ok);
    ForwardedTrustV.contains_args.peer = NULL;
    ForwardedTrust.contains(protocore_forwarded_trust_span());
    TEST_ASSERT_FALSE(ForwardedTrustV.ok);
}
