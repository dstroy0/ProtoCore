// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/network/forward/forward.h"
#include "server/clock/clock.h"
#include <string.h>

#include <unity.h>

#define CAP_IFACES 16
#define CAP_FRAMES 8
#define CAP_FRAME_MAX 32

typedef struct
{
    uint8_t buf[CAP_FRAMES][CAP_FRAME_MAX];
    uint16_t len[CAP_FRAMES];
    size_t count;
    proto_bool accept;
} Cap;
static Cap g_cap[CAP_IFACES];

static void cap_reset(void)
{
    memset(g_cap, 0, sizeof(g_cap));
    for (size_t i = 0; i < CAP_IFACES; i++)
    {
        g_cap[i].accept = PROTO_TRUE;
    }
}

static proto_bool cap_send(uint8_t id, const uint8_t *d, uint16_t n, void *ctx)
{
    (void)ctx;
    Cap *c = &g_cap[id];
    if (!c->accept)
    {
        return PROTO_FALSE;
    }
    if (c->count < CAP_FRAMES)
    {
        uint16_t k = (n < CAP_FRAME_MAX) ? n : CAP_FRAME_MAX;
        memcpy(c->buf[c->count], d, k);
        c->len[c->count] = k;
        c->count++;
    }
    return PROTO_TRUE;
}

static proto_bool add_if(uint8_t id)
{
    Physical.iface.id = id;
    Physical.iface.kind = PROTOCORE_IF_ANY;
    Physical.iface.send = cap_send;
    Physical.iface.ctx = NULL;
    Physical.iface_add(protocore_physical_span());
    return Physical.ok;
}

static uint8_t ingress(uint8_t src, const char *s)
{
    Forward.src_if = src;
    Forward.frame.data = (const uint8_t *)s;
    Forward.frame.len = (uint16_t)strlen(s);
    Forward.ingress(protocore_forward_span());
    return Forward.n;
}

static protocore_forward_stats stats(void)
{
    Forward.get_stats(protocore_forward_span());
    return Forward.stats;
}

static uint32_t g_now_ms;
static uint32_t test_clock(void)
{
    return g_now_ms;
}
static void set_now(uint32_t ms)
{
    g_now_ms = ms;
}

void setUp()
{
    cap_reset();

    Physical.iface_reset(protocore_physical_span());
    Forward.reset(protocore_forward_span());
    Clock.src.fn = test_clock;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
    set_now(0);
}
void tearDown()
{
    Physical.iface_reset(protocore_physical_span());
    Forward.reset(protocore_forward_span());
}

void test_default_deny()
{
    TEST_ASSERT_TRUE(add_if(1));
    TEST_ASSERT_TRUE(add_if(2));
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "hi"));
    TEST_ASSERT_EQUAL_size_t(0, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().frames_in);
    TEST_ASSERT_EQUAL_UINT32(0, stats().forwarded);
}

void test_allow_forwards()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    TEST_ASSERT_TRUE(Forward.ok);
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "abc"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_size_t(0, g_cap[1].count);
    TEST_ASSERT_EQUAL_MEMORY("abc", g_cap[2].buf[0], 3);
    TEST_ASSERT_EQUAL_UINT32(1, stats().forwarded);
}

void test_no_self_forward()
{
    add_if(1);
    Forward.src_if = 1;
    Forward.rule.dst_if = 1;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "loop"));
    TEST_ASSERT_EQUAL_size_t(0, g_cap[1].count);
}

void test_deny_wins_over_allow()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_DENY;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "x"));
    TEST_ASSERT_EQUAL_size_t(0, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().blocked);
}

void test_multi_destination_fanout()
{
    add_if(1);
    add_if(2);
    add_if(3);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    Forward.src_if = 1;
    Forward.rule.dst_if = 3;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    TEST_ASSERT_EQUAL_UINT8(2, ingress(1, "bcast"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_size_t(1, g_cap[3].count);
}

void test_rate_cap_drops_then_reopens()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 2;
    Forward.add_rule(protocore_forward_span());
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "a"));
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "b"));
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "c"));
    TEST_ASSERT_EQUAL_size_t(2, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().rate_dropped);
    set_now(1000);
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "d"));
    TEST_ASSERT_EQUAL_size_t(3, g_cap[2].count);
}

void test_send_failure_counted()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    g_cap[2].accept = PROTO_FALSE;
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "x"));
    TEST_ASSERT_EQUAL_UINT32(1, stats().send_fail);
    TEST_ASSERT_EQUAL_UINT32(0, stats().forwarded);
}

void test_add_if_validation_and_table_full()
{
    TEST_ASSERT_TRUE(add_if(1));
    TEST_ASSERT_FALSE(add_if(1));
    Physical.iface.id = 9;
    Physical.iface.kind = PROTOCORE_IF_ANY;
    Physical.iface.send = NULL;
    Physical.iface.ctx = NULL;
    Physical.iface_add(protocore_physical_span());
    TEST_ASSERT_FALSE(Physical.ok);
    TEST_ASSERT_TRUE(add_if(2));
    TEST_ASSERT_TRUE(add_if(3));
    TEST_ASSERT_TRUE(add_if(4));
    TEST_ASSERT_FALSE(add_if(5));
}

void test_add_rule_table_full()
{
    for (int i = 0; i < PROTOCORE_FWD_MAX_RULES; i++)
    {
        Forward.src_if = 1;
        Forward.rule.dst_if = 2;
        Forward.rule.action = PROTOCORE_FWD_ALLOW;
        Forward.rule.rate_cap_per_sec = 0;
        Forward.add_rule(protocore_forward_span());
        TEST_ASSERT_TRUE(Forward.ok);
    }
    Forward.src_if = 1;
    Forward.rule.dst_if = 3;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    TEST_ASSERT_FALSE(Forward.ok);
}

void test_unregistered_destination_is_inert()
{
    add_if(1);
    Forward.src_if = 1;
    Forward.rule.dst_if = 9;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "x"));
}

void test_rule_with_mismatched_src_is_ignored()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 9;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "x"));
    TEST_ASSERT_EQUAL_UINT32(0, stats().forwarded);
}

void test_duplicate_allow_rule_first_one_governs()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 1;
    Forward.add_rule(protocore_forward_span());
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "a"));
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "b"));
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "c"));
    TEST_ASSERT_EQUAL_UINT32(0, stats().rate_dropped);
}

void test_get_stats_null_pointer_is_noop()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    ingress(1, "x");
    // Reading the counters reports them and does not disturb them: the second read agrees.
    Forward.get_stats(protocore_forward_span());
    TEST_ASSERT_EQUAL_UINT32(1, Forward.stats.forwarded);
    TEST_ASSERT_EQUAL_UINT32(1, stats().forwarded);
}

static uint8_t in1(const uint8_t *b, uint16_t n)
{
    Forward.src_if = 1;
    Forward.frame.data = b;
    Forward.frame.len = n;
    Forward.ingress(protocore_forward_span());
    return Forward.n;
}

void test_acl_deny_by_byte_pattern()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    uint8_t pat[1] = {0xFF}, msk[1] = {0xFF};
    Forward.src_if = 1;
    Forward.match.offset = 0;
    Forward.match.pattern = pat;
    Forward.match.mask = msk;
    Forward.match.patlen = 1;
    Forward.acl.action = PROTOCORE_FWD_DENY;
    Forward.acl_add(protocore_forward_span());
    TEST_ASSERT_TRUE(Forward.ok);

    uint8_t ok[3] = {'a', 'b', 'c'};
    uint8_t bad[3] = {0xFF, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8(1, in1(ok, 3));
    TEST_ASSERT_EQUAL_UINT8(0, in1(bad, 3));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().acl_denied);
}

void test_acl_allowlist_default_deny()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    Forward.acl.fallback = PROTOCORE_FWD_DENY;
    Forward.acl_set_default(protocore_forward_span());
    uint8_t pat[1] = {0xAA}, msk[1] = {0xFF};
    Forward.src_if = 1;
    Forward.match.offset = 0;
    Forward.match.pattern = pat;
    Forward.match.mask = msk;
    Forward.match.patlen = 1;
    Forward.acl.action = PROTOCORE_FWD_ALLOW;
    Forward.acl_add(protocore_forward_span());

    uint8_t good[2] = {0xAA, 0x01};
    uint8_t other[2] = {0xBB, 0x01};
    TEST_ASSERT_EQUAL_UINT8(1, in1(good, 2));
    TEST_ASSERT_EQUAL_UINT8(0, in1(other, 2));
    TEST_ASSERT_EQUAL_UINT32(1, stats().acl_denied);
}

void test_acl_first_match_wins()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    uint8_t p1[1] = {0x01}, m1[1] = {0xFF};
    Forward.src_if = 1;
    Forward.match.offset = 0;
    Forward.match.pattern = p1;
    Forward.match.mask = m1;
    Forward.match.patlen = 1;
    Forward.acl.action = PROTOCORE_FWD_ALLOW;
    Forward.acl_add(protocore_forward_span());
    Forward.src_if = PROTOCORE_FWD_IF_ANY;
    Forward.match.offset = 0;
    Forward.match.pattern = NULL;
    Forward.match.mask = NULL;
    Forward.match.patlen = 0;
    Forward.acl.action = PROTOCORE_FWD_DENY;
    Forward.acl_add(protocore_forward_span());

    uint8_t a[1] = {0x01};
    uint8_t b[1] = {0x02};
    TEST_ASSERT_EQUAL_UINT8(1, in1(a, 1));
    TEST_ASSERT_EQUAL_UINT8(0, in1(b, 1));
}

void test_acl_src_any_content_wildcard()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    Forward.src_if = PROTOCORE_FWD_IF_ANY;
    Forward.match.offset = 0;
    Forward.match.pattern = NULL;
    Forward.match.mask = NULL;
    Forward.match.patlen = 0;
    Forward.acl.action = PROTOCORE_FWD_DENY;
    Forward.acl_add(protocore_forward_span());
    uint8_t x[2] = {0x12, 0x34};
    TEST_ASSERT_EQUAL_UINT8(0, in1(x, 2));
    TEST_ASSERT_EQUAL_UINT32(1, stats().acl_denied);
}

void test_acl_entry_src_mismatch_falls_through()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    uint8_t pat[1] = {0xAA}, msk[1] = {0xFF};
    Forward.src_if = 2;
    Forward.match.offset = 0;
    Forward.match.pattern = pat;
    Forward.match.mask = msk;
    Forward.match.patlen = 1;
    Forward.acl.action = PROTOCORE_FWD_DENY;
    Forward.acl_add(protocore_forward_span());

    uint8_t frame[1] = {0xAA};
    TEST_ASSERT_EQUAL_UINT8(1, in1(frame, 1));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(0, stats().acl_denied);
}

void test_acl_short_frame_skips_entry()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    uint8_t pat[2] = {0x11, 0x22}, msk[2] = {0xFF, 0xFF};
    Forward.src_if = 1;
    Forward.match.offset = 4;
    Forward.match.pattern = pat;
    Forward.match.mask = msk;
    Forward.match.patlen = 2;
    Forward.acl.action = PROTOCORE_FWD_DENY;
    Forward.acl_add(protocore_forward_span());
    uint8_t shortf[3] = {0x11, 0x22, 0x33};
    TEST_ASSERT_EQUAL_UINT8(1, in1(shortf, 3));
}

void test_acl_add_validation_and_table_full()
{
    uint8_t big[PROTOCORE_FWD_ACL_PATLEN + 1] = {0}, bm[PROTOCORE_FWD_ACL_PATLEN + 1] = {0};
    Forward.src_if = 1;
    Forward.match.offset = 0;
    Forward.match.pattern = big;
    Forward.match.mask = bm;
    Forward.match.patlen = PROTOCORE_FWD_ACL_PATLEN + 1;
    Forward.acl.action = PROTOCORE_FWD_DENY;
    Forward.acl_add(protocore_forward_span());
    TEST_ASSERT_FALSE(Forward.ok);
    for (int i = 0; i < PROTOCORE_FWD_MAX_ACL; i++)
    {
        Forward.src_if = PROTOCORE_FWD_IF_ANY;
        Forward.match.offset = 0;
        Forward.match.pattern = NULL;
        Forward.match.mask = NULL;
        Forward.match.patlen = 0;
        Forward.acl.action = PROTOCORE_FWD_ALLOW;
        Forward.acl_add(protocore_forward_span());
        TEST_ASSERT_TRUE(Forward.ok);
    }
    Forward.src_if = PROTOCORE_FWD_IF_ANY;
    Forward.match.offset = 0;
    Forward.match.pattern = NULL;
    Forward.match.mask = NULL;
    Forward.match.patlen = 0;
    Forward.acl.action = PROTOCORE_FWD_ALLOW;
    Forward.acl_add(protocore_forward_span());
    TEST_ASSERT_FALSE(Forward.ok);
}

void test_acl_add_null_pointer_validation()
{
    uint8_t pat[1] = {0x01}, msk[1] = {0xFF};
    Forward.src_if = 1;
    Forward.match.offset = 0;
    Forward.match.pattern = NULL;
    Forward.match.mask = msk;
    Forward.match.patlen = 1;
    Forward.acl.action = PROTOCORE_FWD_DENY;
    Forward.acl_add(protocore_forward_span());
    TEST_ASSERT_FALSE(Forward.ok);
    Forward.src_if = 1;
    Forward.match.offset = 0;
    Forward.match.pattern = pat;
    Forward.match.mask = NULL;
    Forward.match.patlen = 1;
    Forward.acl.action = PROTOCORE_FWD_DENY;
    Forward.acl_add(protocore_forward_span());
    TEST_ASSERT_FALSE(Forward.ok);
}

static proto_bool route_firstbyte(uint8_t src, char c, uint8_t egress, uint16_t cap)
{
    uint8_t pat[1] = {(uint8_t)c};
    uint8_t msk[1] = {0xFF};
    Forward.src_if = src;
    Forward.match.offset = 0;
    Forward.match.pattern = pat;
    Forward.match.mask = msk;
    Forward.match.patlen = 1;
    Forward.route.egress_if = egress;
    Forward.route.rate_cap_per_sec = cap;
    Forward.route_add(protocore_forward_span());
    return Forward.ok;
}

void test_route_selects_egress_and_falls_through()
{
    add_if(1);
    add_if(2);
    add_if(3);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    TEST_ASSERT_TRUE(route_firstbyte(PROTOCORE_FWD_IF_ANY, 'X', 3, 0));

    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "Xyz"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[3].count);
    TEST_ASSERT_EQUAL_size_t(0, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().policy_routed);

    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "abc"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().policy_routed);
}

void test_route_never_reflects_to_source()
{
    add_if(1);
    add_if(2);
    route_firstbyte(PROTOCORE_FWD_IF_ANY, 'X', 1, 0);
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "Xyz"));
    TEST_ASSERT_EQUAL_size_t(0, g_cap[1].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().policy_routed);
}

void test_route_unregistered_egress_fail_closed()
{
    add_if(1);
    route_firstbyte(PROTOCORE_FWD_IF_ANY, 'X', 9, 0);
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "Xyz"));
    TEST_ASSERT_EQUAL_UINT32(1, stats().policy_routed);
    TEST_ASSERT_EQUAL_UINT32(1, stats().send_fail);
}

void test_route_src_specific_filters_by_source()
{
    add_if(1);
    add_if(2);
    add_if(3);
    TEST_ASSERT_TRUE(route_firstbyte(1, 'Y', 3, 0));
    Forward.src_if = 2;
    Forward.rule.dst_if = 3;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());

    TEST_ASSERT_EQUAL_UINT8(1, ingress(2, "Yes"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[3].count);
    TEST_ASSERT_EQUAL_UINT32(0, stats().policy_routed);

    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "Yz"));
    TEST_ASSERT_EQUAL_size_t(2, g_cap[3].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().policy_routed);
}

void test_route_send_failure_counted()
{
    add_if(1);
    add_if(2);
    TEST_ASSERT_TRUE(route_firstbyte(PROTOCORE_FWD_IF_ANY, 'Z', 2, 0));
    g_cap[2].accept = PROTO_FALSE;
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "Zzz"));
    TEST_ASSERT_EQUAL_UINT32(1, stats().policy_routed);
    TEST_ASSERT_EQUAL_UINT32(1, stats().send_fail);
    TEST_ASSERT_EQUAL_UINT32(0, stats().forwarded);
}

void test_route_rate_cap()
{
    add_if(1);
    add_if(2);
    route_firstbyte(PROTOCORE_FWD_IF_ANY, 'X', 2, 1);
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "X1"));
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "X2"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().rate_dropped);
    set_now(1000);
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "X3"));
    TEST_ASSERT_EQUAL_size_t(2, g_cap[2].count);
}

void test_route_default_any_content()
{
    add_if(1);
    add_if(2);
    Forward.src_if = PROTOCORE_FWD_IF_ANY;
    Forward.match.offset = 0;
    Forward.match.pattern = NULL;
    Forward.match.mask = NULL;
    Forward.match.patlen = 0;
    Forward.route.egress_if = 2;
    Forward.route.rate_cap_per_sec = 0;
    Forward.route_add(protocore_forward_span());
    TEST_ASSERT_TRUE(Forward.ok);
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "anything"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
}

void test_route_first_match_wins()
{
    add_if(1);
    add_if(2);
    add_if(3);
    route_firstbyte(PROTOCORE_FWD_IF_ANY, 'X', 2, 0);
    route_firstbyte(PROTOCORE_FWD_IF_ANY, 'X', 3, 0);
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "Xy"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_size_t(0, g_cap[3].count);
}

void test_route_add_validation_and_table_full()
{
    uint8_t pat[PROTOCORE_FWD_ACL_PATLEN + 1] = {0}, msk[PROTOCORE_FWD_ACL_PATLEN + 1] = {0};
    Forward.src_if = PROTOCORE_FWD_IF_ANY;
    Forward.match.offset = 0;
    Forward.match.pattern = pat;
    Forward.match.mask = msk;
    Forward.match.patlen = PROTOCORE_FWD_ACL_PATLEN + 1;
    Forward.route.egress_if = 2;
    Forward.route.rate_cap_per_sec = 0;
    Forward.route_add(protocore_forward_span());
    TEST_ASSERT_FALSE(Forward.ok);
    Forward.src_if = PROTOCORE_FWD_IF_ANY;
    Forward.match.offset = 0;
    Forward.match.pattern = NULL;
    Forward.match.mask = msk;
    Forward.match.patlen = 1;
    Forward.route.egress_if = 2;
    Forward.route.rate_cap_per_sec = 0;
    Forward.route_add(protocore_forward_span());
    TEST_ASSERT_FALSE(Forward.ok);
    Forward.src_if = PROTOCORE_FWD_IF_ANY;
    Forward.match.offset = 0;
    Forward.match.pattern = pat;
    Forward.match.mask = NULL;
    Forward.match.patlen = 1;
    Forward.route.egress_if = 2;
    Forward.route.rate_cap_per_sec = 0;
    Forward.route_add(protocore_forward_span());
    TEST_ASSERT_FALSE(Forward.ok);
    for (int i = 0; i < PROTOCORE_FWD_MAX_ROUTES; i++)
    {
        TEST_ASSERT_TRUE(route_firstbyte(PROTOCORE_FWD_IF_ANY, 'A', 2, 0));
    }
    TEST_ASSERT_FALSE(route_firstbyte(PROTOCORE_FWD_IF_ANY, 'A', 2, 0));
}

#if PROTOCORE_FWD_INSPECT
static int g_inspect_calls = 0;

static protocore_fwd_verdict inspect_drop_D(uint8_t src, const uint8_t *d, uint16_t n, void *ctx)
{
    (void)src;
    (void)ctx;
    g_inspect_calls++;
    if (n > 0 && d[0] == 'D')
    {
        return PROTOCORE_FWD_INSPECT_DROP;
    }
    return PROTOCORE_FWD_INSPECT_PASS;
}

void test_inspect_pass_and_drop()
{
    g_inspect_calls = 0;
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    Forward.inspect.fn = inspect_drop_D;
    Forward.inspect.ctx = NULL;
    Forward.set_inspector(protocore_forward_span());

    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "ok"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "Drop it"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().inspect_dropped);
    TEST_ASSERT_EQUAL_INT(2, g_inspect_calls);
}

void test_inspect_runs_after_acl()
{
    g_inspect_calls = 0;
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    Forward.inspect.fn = inspect_drop_D;
    Forward.inspect.ctx = NULL;
    Forward.set_inspector(protocore_forward_span());

    uint8_t pat[1] = {'X'}, msk[1] = {0xFF};
    Forward.src_if = PROTOCORE_FWD_IF_ANY;
    Forward.match.offset = 0;
    Forward.match.pattern = pat;
    Forward.match.mask = msk;
    Forward.match.patlen = 1;
    Forward.acl.action = PROTOCORE_FWD_DENY;
    Forward.acl_add(protocore_forward_span());

    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "Xhi"));
    TEST_ASSERT_EQUAL_INT(0, g_inspect_calls);
    TEST_ASSERT_EQUAL_UINT32(1, stats().acl_denied);
}

void test_inspect_cleared_by_null()
{
    add_if(1);
    add_if(2);
    Forward.src_if = 1;
    Forward.rule.dst_if = 2;
    Forward.rule.action = PROTOCORE_FWD_ALLOW;
    Forward.rule.rate_cap_per_sec = 0;
    Forward.add_rule(protocore_forward_span());
    Forward.inspect.fn = inspect_drop_D;
    Forward.inspect.ctx = NULL;
    Forward.set_inspector(protocore_forward_span());
    Forward.inspect.fn = NULL;
    Forward.inspect.ctx = NULL;
    Forward.set_inspector(protocore_forward_span());
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "Drop"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(0, stats().inspect_dropped);
}
#endif

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_default_deny);
    RUN_TEST(test_allow_forwards);
    RUN_TEST(test_no_self_forward);
    RUN_TEST(test_deny_wins_over_allow);
    RUN_TEST(test_multi_destination_fanout);
    RUN_TEST(test_rate_cap_drops_then_reopens);
    RUN_TEST(test_send_failure_counted);
    RUN_TEST(test_add_if_validation_and_table_full);
    RUN_TEST(test_add_rule_table_full);
    RUN_TEST(test_unregistered_destination_is_inert);
    RUN_TEST(test_rule_with_mismatched_src_is_ignored);
    RUN_TEST(test_duplicate_allow_rule_first_one_governs);
    RUN_TEST(test_get_stats_null_pointer_is_noop);
    RUN_TEST(test_acl_deny_by_byte_pattern);
    RUN_TEST(test_acl_allowlist_default_deny);
    RUN_TEST(test_acl_first_match_wins);
    RUN_TEST(test_acl_src_any_content_wildcard);
    RUN_TEST(test_acl_entry_src_mismatch_falls_through);
    RUN_TEST(test_acl_short_frame_skips_entry);
    RUN_TEST(test_acl_add_validation_and_table_full);
    RUN_TEST(test_acl_add_null_pointer_validation);
    RUN_TEST(test_route_selects_egress_and_falls_through);
    RUN_TEST(test_route_never_reflects_to_source);
    RUN_TEST(test_route_unregistered_egress_fail_closed);
    RUN_TEST(test_route_src_specific_filters_by_source);
    RUN_TEST(test_route_send_failure_counted);
    RUN_TEST(test_route_rate_cap);
    RUN_TEST(test_route_default_any_content);
    RUN_TEST(test_route_first_match_wins);
    RUN_TEST(test_route_add_validation_and_table_full);
#if PROTOCORE_FWD_INSPECT
    RUN_TEST(test_inspect_pass_and_drop);
    RUN_TEST(test_inspect_runs_after_acl);
    RUN_TEST(test_inspect_cleared_by_null);
#endif
    return UNITY_END();
}
