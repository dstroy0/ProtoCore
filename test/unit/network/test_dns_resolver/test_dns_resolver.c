// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the DNS resolver (network_drivers/network/dns/dns_resolver): the answer classifier
// and verifier, and the resolve itself, driven through the mocked lwIP stack in test/mocks/lwip.

#include "lwip/dns.h" // g_mock_dns_answer: what the stack answers a lookup with
#include "network_drivers/network/dns/dns_resolver.h"
#include <unity.h>

// The answer the mocked stack returns, host order; 0 means the name does not resolve.
static void set_answer(uint32_t ip)
{
    g_mock_dns_answer = ip;
}

#define IPV4(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

void setUp()
{
}
void tearDown()
{
}

void test_classify()
{
    TEST_ASSERT_EQUAL_INT(PC_IP_UNSPECIFIED, Resolver.classify(0u));
    TEST_ASSERT_EQUAL_INT(PC_IP_BROADCAST, Resolver.classify(0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_INT(PC_IP_LOOPBACK, Resolver.classify(IPV4(127, 0, 0, 1)));
    TEST_ASSERT_EQUAL_INT(PC_IP_PRIVATE, Resolver.classify(IPV4(10, 0, 0, 5)));
    TEST_ASSERT_EQUAL_INT(PC_IP_PRIVATE, Resolver.classify(IPV4(172, 16, 0, 1)));
    TEST_ASSERT_EQUAL_INT(PC_IP_PRIVATE, Resolver.classify(IPV4(192, 168, 1, 1)));
    TEST_ASSERT_EQUAL_INT(PC_IP_LINKLOCAL, Resolver.classify(IPV4(169, 254, 1, 1)));
    TEST_ASSERT_EQUAL_INT(PC_IP_MULTICAST, Resolver.classify(IPV4(224, 0, 0, 1)));
    TEST_ASSERT_EQUAL_INT(PC_IP_PUBLIC, Resolver.classify(IPV4(8, 8, 8, 8)));
    // 172.32.x is OUTSIDE the 172.16/12 private block -> public.
    TEST_ASSERT_EQUAL_INT(PC_IP_PUBLIC, Resolver.classify(IPV4(172, 32, 0, 1)));
    // 172.10.x is BELOW the 172.16/12 private block (b < 16) -> public.
    TEST_ASSERT_EQUAL_INT(PC_IP_PUBLIC, Resolver.classify(IPV4(172, 10, 0, 1)));
    // 192.x (x != 168) is outside the 192.168/16 private block -> public.
    TEST_ASSERT_EQUAL_INT(PC_IP_PUBLIC, Resolver.classify(IPV4(192, 1, 1, 1)));
    // 169.x (x != 254) is outside the 169.254/16 link-local block -> public.
    TEST_ASSERT_EQUAL_INT(PC_IP_PUBLIC, Resolver.classify(IPV4(169, 1, 1, 1)));
    // 240.x is ABOVE the 224-239 multicast range -> public.
    TEST_ASSERT_EQUAL_INT(PC_IP_PUBLIC, Resolver.classify(IPV4(240, 0, 0, 1)));
}

void test_verify_rejects_suspicious()
{
    TEST_ASSERT_FALSE(Resolver.verify(IPV4(0, 0, 0, 0)));         // blocked / no answer
    TEST_ASSERT_FALSE(Resolver.verify(IPV4(127, 0, 0, 1)));       // rebinding to localhost
    TEST_ASSERT_FALSE(Resolver.verify(IPV4(255, 255, 255, 255))); // broadcast
    TEST_ASSERT_FALSE(Resolver.verify(IPV4(224, 0, 0, 1)));       // multicast
}

void test_verify_accepts_plausible()
{
    TEST_ASSERT_TRUE(Resolver.verify(IPV4(8, 8, 8, 8)));      // public
    TEST_ASSERT_TRUE(Resolver.verify(IPV4(192, 168, 1, 50))); // private (LAN host)
    TEST_ASSERT_TRUE(Resolver.verify(IPV4(169, 254, 0, 2)));  // link-local
}

// A name the stack has no answer for is refused, and the caller's word is left alone.
void test_resolve_reports_no_answer()
{
    uint32_t ip = 0xDEADBEEF;
    set_answer(0);
    TEST_ASSERT_FALSE(Resolver.resolve("example.com", &ip));
    TEST_ASSERT_FALSE(Resolver.resolve_verified("example.com", &ip));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEF, ip);
}

void test_resolve_verified_paths()
{
    uint32_t ip = 0;
    // no answer -> false.
    set_answer(0);
    TEST_ASSERT_FALSE(Resolver.resolve_verified("example.com", &ip));
    // an answer that is a loopback (DNS-rebinding) -> verify rejects it.
    set_answer(IPV4(127, 0, 0, 1));
    TEST_ASSERT_FALSE(Resolver.resolve_verified("example.com", &ip));
    // a plausible public address -> true, out_ip set.
    set_answer(IPV4(8, 8, 8, 8));
    TEST_ASSERT_TRUE(Resolver.resolve_verified("example.com", &ip));
    TEST_ASSERT_EQUAL_UINT32(IPV4(8, 8, 8, 8), ip);
    TEST_ASSERT_TRUE(Resolver.resolve_verified("example.com", NULL)); // null out_ip ok
    set_answer(0);
}

// A dotted quad never reaches the stack: it is returned straight from the literal, so the answer
// the stack would have given is irrelevant.
void test_resolve_literal_skips_dns()
{
    uint32_t ip = 0;
    set_answer(IPV4(8, 8, 8, 8)); // what a lookup would answer, if one happened
    TEST_ASSERT_TRUE(Resolver.resolve("192.168.1.50", &ip));
    TEST_ASSERT_EQUAL_UINT32(IPV4(192, 168, 1, 50), ip);
    set_answer(0);
}

// A resolve with nowhere to put the answer is refused before the stack is asked, whatever the
// stack would have answered - there is no caller for a lookup whose result is discarded.
void test_resolve_refuses_a_null_out_ip()
{
    set_answer(IPV4(8, 8, 8, 8));
    TEST_ASSERT_FALSE(Resolver.resolve("example.com", NULL));
    TEST_ASSERT_FALSE(Resolver.resolve(NULL, NULL));
    set_answer(0);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_classify);
    RUN_TEST(test_verify_rejects_suspicious);
    RUN_TEST(test_verify_accepts_plausible);
    RUN_TEST(test_resolve_reports_no_answer);
    RUN_TEST(test_resolve_verified_paths);
    RUN_TEST(test_resolve_literal_skips_dns);
    RUN_TEST(test_resolve_refuses_a_null_out_ip);
    return UNITY_END();
}
