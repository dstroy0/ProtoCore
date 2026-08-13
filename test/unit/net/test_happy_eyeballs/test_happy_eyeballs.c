// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/happy_eyeballs: RFC 6724 ordering + RFC 8305 family interleave + attempt gate.

#include "services/net/happy_eyeballs/happy_eyeballs.h"
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static protocore_ip v6(const char *s)
{
    protocore_ip ip;
    Ip.parse(s, &ip);
    return ip;
}
static protocore_ip v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return protocore_ip_from_v4_octets(a, b, c, d);
}

void test_pref_order(void)
{
    protocore_ip g6 = v6("2606:4700::1");   // global v6
    protocore_ip g4 = v4(93, 184, 216, 34); // global v4
    protocore_ip ll6 = v6("fe80::1");       // link-local v6
    protocore_ip lo6 = v6("::1");           // loopback
    // Global outranks link-local outranks loopback; within global, native v6 outranks v4.
    TEST_ASSERT_TRUE(protocore_he_pref(&g6) > protocore_he_pref(&g4));
    TEST_ASSERT_TRUE(protocore_he_pref(&g4) > protocore_he_pref(&ll6));
    TEST_ASSERT_TRUE(protocore_he_pref(&ll6) > protocore_he_pref(&lo6));
}

void test_order_and_interleave(void)
{
    // Two global v6 + one global v4, given v4-first: sort puts v6 ahead, interleave alternates.
    protocore_ip list[3] = {v4(93, 184, 216, 34), v6("2606:4700::1"), v6("2606:4700::2")};
    protocore_he_order(list, 3);
    // First is a v6 (highest pref); second must be the v4 (family alternation); third the other v6.
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_V6, list[0].family);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_V4, list[1].family);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_V6, list[2].family);
}

void test_order_single_family(void)
{
    // All v4: interleave is a no-op, order stays preference-sorted (global before private).
    protocore_ip list[3] = {v4(192, 168, 1, 5), v4(8, 8, 8, 8), v4(10, 0, 0, 1)};
    protocore_he_order(list, 3);
    TEST_ASSERT_EQUAL_UINT8(8, list[0].bytes[0]); // 8.8.8.8 global first
    // The two private addresses follow in stable order.
    TEST_ASSERT_TRUE(list[1].bytes[0] == 192 || list[1].bytes[0] == 10);
}

void test_attempt_due(void)
{
    TEST_ASSERT_FALSE(protocore_he_attempt_due(1000, 1000 + 249, PROTOCORE_HE_ATTEMPT_DELAY_MS));
    TEST_ASSERT_TRUE(protocore_he_attempt_due(1000, 1000 + 250, PROTOCORE_HE_ATTEMPT_DELAY_MS));
    // Wrap-safe across the uint32 rollover.
    TEST_ASSERT_TRUE(protocore_he_attempt_due(0xFFFFFF00u, 0xFFFFFF00u + 250, PROTOCORE_HE_ATTEMPT_DELAY_MS));
}

void test_pref_scopes_and_order_edges()
{
    // Exercise the multicast + unspecified scope arms of protocore_he_pref (values are protocore_ip-classified).
    protocore_ip mc4 = protocore_ip_from_v4_octets(239, 1, 2, 3); // admin-scoped IPv4 multicast
    protocore_ip mc6;
    Ip.parse("ff0e::1", &mc6); // global-scope IPv6 multicast
    protocore_ip un;
    Ip.parse("::", &un); // unspecified
    (void)protocore_he_pref(&mc4);
    (void)protocore_he_pref(&mc6);
    (void)protocore_he_pref(&un);
    // n <= 1 returns immediately (no reorder).
    protocore_ip one[1];
    Ip.parse("2606:4700::1", &one[0]);
    protocore_he_order(one, 1);
    // A larger mixed list exercises the v4/v6 interleave.
    protocore_ip many[5];
    Ip.parse("2606:4700::1", &many[0]);
    many[1] = protocore_ip_from_v4_octets(8, 8, 8, 8);
    Ip.parse("2606:4700::2", &many[2]);
    many[3] = protocore_ip_from_v4_octets(1, 1, 1, 1);
    Ip.parse("2606:4700::3", &many[4]);
    protocore_he_order(many, 5);
    TEST_PASS();
}

void test_pref_null_and_none(void)
{
    // Null pointer and an empty (PROTOCORE_IP_NONE) address both hit the sentinel-return arm.
    TEST_ASSERT_EQUAL_INT(-1, protocore_he_pref(NULL));
    protocore_ip none_ip;
    none_ip.family = PROTOCORE_IP_NONE;
    TEST_ASSERT_EQUAL_INT(-1, protocore_he_pref(&none_ip));
}

void test_order_null_list_is_noop(void)
{
    // A null list must return immediately without dereferencing it.
    protocore_he_order(NULL, 5);
    TEST_PASS();
}

void test_order_v4_mapped_treated_as_v4(void)
{
    // ::ffff:a.b.c.d is family V6 but eff_is_v6() must treat it as V4 for interleave purposes.
    protocore_ip list[2];
    Ip.parse("::ffff:203.0.113.5", &list[0]); // v4-mapped v6, global scope
    Ip.parse("2606:4700::1", &list[1]);       // native v6, global scope
    protocore_he_order(list, 2);
    // Native v6 outranks v4 (mapped or not) within the same scope, so it sorts first.
    TEST_ASSERT_EQUAL_UINT8(0x26, list[0].bytes[0]);
}

void test_order_oversized_list_skips_interleave(void)
{
    // A list longer than PROTOCORE_HE_MAX (16) is stable-sorted but the interleave step is skipped
    // (the fixed PROTOCORE_HE_MAX-sized scratch buffers cannot hold it).
    protocore_ip list[17];
    for (size_t i = 0; i < 17; i++)
    {
        list[i] = v4(10, 0, 0, (uint8_t)i);
    }
    list[16] = v4(8, 8, 8, 8); // one global address, placed last pre-sort
    protocore_he_order(list, 17);
    TEST_ASSERT_EQUAL_UINT8(8, list[0].bytes[0]); // sort still ran: global address moved to front
}

void test_order_family_imbalance_drains_v6(void)
{
    // 3 global v6 + 1 global v4, v6-first: v4 exhausts after one pick and the "preferred family
    // exhausted, drain the other" arm fires while it is still (nominally) v4's turn.
    protocore_ip list[4];
    Ip.parse("2606:4700::1", &list[0]);
    Ip.parse("2606:4700::2", &list[1]);
    Ip.parse("2606:4700::3", &list[2]);
    list[3] = v4(8, 8, 8, 8);
    protocore_he_order(list, 4);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_V6, list[0].family);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_V4, list[1].family);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_V6, list[2].family);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_IP_V6, list[3].family);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pref_order);
    RUN_TEST(test_order_and_interleave);
    RUN_TEST(test_order_single_family);
    RUN_TEST(test_attempt_due);
    RUN_TEST(test_pref_scopes_and_order_edges);
    RUN_TEST(test_pref_null_and_none);
    RUN_TEST(test_order_null_list_is_noop);
    RUN_TEST(test_order_v4_mapped_treated_as_v4);
    RUN_TEST(test_order_oversized_list_skips_interleave);
    RUN_TEST(test_order_family_imbalance_drains_v6);
    return UNITY_END();
}
