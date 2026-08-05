// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the interface forwarding plane (services/net/forward): default-deny, an
// ALLOW rule forwards, a DENY wins, multi-destination fan-out, no reflection to the
// source, the per-rule rate cap (driven by the host test clock), send-failure counting,
// and the interface / rule table limits. Pure host tests. The DMA-driven wiring (DMA-
// complete -> FORWARD lane -> ingress -> egress DMA) is HW-verified separately.
//
// The env sizes PC_FWD_MAX_IFACES = 4, PC_FWD_MAX_RULES = 4.

#include "services/net/forward/forward.h"

#include <unity.h>

// Egress capture, indexed by interface id. Fixed table rather than a map: the ids the tests
// register are 1..5 and no test captures more than three frames on one interface.
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
        return PROTO_FALSE; // simulate a full / failed interface
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
    return pc_forward_add_if(id, PC_IF_OTHER, cap_send, NULL);
}

static uint8_t ingress(uint8_t src, const char *s)
{
    return pc_forward_ingress(src, (const uint8_t *)s, (uint16_t)strlen(s));
}

static pc_forward_stats stats(void)
{
    pc_forward_stats st;
    pc_forward_get_stats(&st);
    return st;
}

void setUp()
{
    cap_reset();
    pc_forward_reset();
    pc_forward_test_set_now(0);
}
void tearDown()
{
    pc_forward_reset();
}

void test_default_deny()
{
    TEST_ASSERT_TRUE(add_if(1));
    TEST_ASSERT_TRUE(add_if(2));
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "hi")); // no rule -> nothing forwarded
    TEST_ASSERT_EQUAL_size_t(0, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().frames_in);
    TEST_ASSERT_EQUAL_UINT32(0, stats().forwarded);
}

void test_allow_forwards()
{
    add_if(1);
    add_if(2);
    TEST_ASSERT_TRUE(pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0));
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "abc"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_size_t(0, g_cap[1].count); // source not touched
    TEST_ASSERT_EQUAL_MEMORY("abc", g_cap[2].buf[0], 3);
    TEST_ASSERT_EQUAL_UINT32(1, stats().forwarded);
}

void test_no_self_forward()
{
    add_if(1);
    pc_forward_add_rule(1, 1, PC_FWD_ALLOW, 0); // even an explicit self rule
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "loop"));
    TEST_ASSERT_EQUAL_size_t(0, g_cap[1].count);
}

void test_deny_wins_over_allow()
{
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);
    pc_forward_add_rule(1, 2, PC_FWD_DENY, 0); // deny wins regardless of order
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "x"));
    TEST_ASSERT_EQUAL_size_t(0, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().blocked);
}

void test_multi_destination_fanout()
{
    add_if(1);
    add_if(2);
    add_if(3);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);
    pc_forward_add_rule(1, 3, PC_FWD_ALLOW, 0);
    TEST_ASSERT_EQUAL_UINT8(2, ingress(1, "bcast"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_size_t(1, g_cap[3].count);
}

void test_rate_cap_drops_then_reopens()
{
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 2); // 2 frames / second
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "a"));
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "b"));
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "c")); // 3rd in the window -> dropped
    TEST_ASSERT_EQUAL_size_t(2, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().rate_dropped);
    pc_forward_test_set_now(1000); // next window
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "d"));
    TEST_ASSERT_EQUAL_size_t(3, g_cap[2].count);
}

void test_send_failure_counted()
{
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);
    g_cap[2].accept = PROTO_FALSE; // interface refuses
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "x"));
    TEST_ASSERT_EQUAL_UINT32(1, stats().send_fail);
    TEST_ASSERT_EQUAL_UINT32(0, stats().forwarded);
}

void test_add_if_validation_and_table_full()
{
    TEST_ASSERT_TRUE(add_if(1));
    TEST_ASSERT_FALSE(add_if(1));                                     // duplicate id
    TEST_ASSERT_FALSE(pc_forward_add_if(9, PC_IF_OTHER, NULL, NULL)); // null send
    TEST_ASSERT_TRUE(add_if(2));
    TEST_ASSERT_TRUE(add_if(3));
    TEST_ASSERT_TRUE(add_if(4));
    TEST_ASSERT_FALSE(add_if(5)); // table full (PC_FWD_MAX_IFACES = 4)
}

void test_add_rule_table_full()
{
    for (int i = 0; i < PC_FWD_MAX_RULES; i++)
    {
        TEST_ASSERT_TRUE(pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0));
    }
    TEST_ASSERT_FALSE(pc_forward_add_rule(1, 3, PC_FWD_ALLOW, 0)); // full
}

void test_unregistered_destination_is_inert()
{
    add_if(1);
    pc_forward_add_rule(1, 9, PC_FWD_ALLOW, 0);  // dst 9 never registered
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "x")); // nothing to forward to
}

void test_rule_with_mismatched_src_is_ignored()
{
    add_if(1);
    add_if(2);
    pc_forward_add_rule(9, 2, PC_FWD_ALLOW, 0);  // registered but for a different src
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "x")); // no applicable rule -> default deny
    TEST_ASSERT_EQUAL_UINT32(0, stats().forwarded);
}

void test_duplicate_allow_rule_first_one_governs()
{
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0); // first ALLOW: unlimited, governs
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 1); // duplicate ALLOW for the same pair: ignored
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "a"));
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "b"));
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "c")); // would be capped at 1/sec if the duplicate rule governed
    TEST_ASSERT_EQUAL_UINT32(0, stats().rate_dropped);
}

void test_get_stats_null_pointer_is_noop()
{
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);
    ingress(1, "x");
    pc_forward_get_stats(NULL);                     // must be a safe no-op
    TEST_ASSERT_EQUAL_UINT32(1, stats().forwarded); // state unaffected
}

// --- Ingress ACL tests ----------------------------------------------------------------

// Forward one byte array on interface 1 (bytes let us hit specific ACL patterns).
static uint8_t in1(const uint8_t *b, uint16_t n)
{
    return pc_forward_ingress(1, b, n);
}

void test_acl_deny_by_byte_pattern()
{
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);
    uint8_t pat[1] = {0xFF}, msk[1] = {0xFF};
    TEST_ASSERT_TRUE(pc_forward_acl_add(1, 0, pat, msk, 1, PC_FWD_DENY)); // deny byte0 == 0xFF

    uint8_t ok[3] = {'a', 'b', 'c'};
    uint8_t bad[3] = {0xFF, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8(1, in1(ok, 3));  // no ACE match -> default allow -> forwarded
    TEST_ASSERT_EQUAL_UINT8(0, in1(bad, 3)); // ACE denies at ingress
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().acl_denied);
}

void test_acl_allowlist_default_deny()
{
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);
    pc_forward_acl_set_default(PC_FWD_DENY); // allowlist: only permitted frames pass
    uint8_t pat[1] = {0xAA}, msk[1] = {0xFF};
    pc_forward_acl_add(1, 0, pat, msk, 1, PC_FWD_ALLOW); // permit byte0 == 0xAA

    uint8_t good[2] = {0xAA, 0x01};
    uint8_t other[2] = {0xBB, 0x01};
    TEST_ASSERT_EQUAL_UINT8(1, in1(good, 2));
    TEST_ASSERT_EQUAL_UINT8(0, in1(other, 2)); // no ACE match -> default deny
    TEST_ASSERT_EQUAL_UINT32(1, stats().acl_denied);
}

void test_acl_first_match_wins()
{
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);
    uint8_t p1[1] = {0x01}, m1[1] = {0xFF};
    pc_forward_acl_add(1, 0, p1, m1, 1, PC_FWD_ALLOW);                // 1st: permit byte0 == 0x01
    pc_forward_acl_add(PC_FWD_IF_ANY, 0, NULL, NULL, 0, PC_FWD_DENY); // 2nd: deny everything

    uint8_t a[1] = {0x01};
    uint8_t b[1] = {0x02};
    TEST_ASSERT_EQUAL_UINT8(1, in1(a, 1)); // first entry permits
    TEST_ASSERT_EQUAL_UINT8(0, in1(b, 1)); // falls through to the deny-all entry
}

void test_acl_src_any_content_wildcard()
{
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);
    pc_forward_acl_add(PC_FWD_IF_ANY, 0, NULL, NULL, 0, PC_FWD_DENY); // any src, any content
    uint8_t x[2] = {0x12, 0x34};
    TEST_ASSERT_EQUAL_UINT8(0, in1(x, 2));
    TEST_ASSERT_EQUAL_UINT32(1, stats().acl_denied);
}

void test_acl_entry_src_mismatch_falls_through()
{
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);
    uint8_t pat[1] = {0xAA}, msk[1] = {0xFF};
    pc_forward_acl_add(2, 0, pat, msk, 1, PC_FWD_DENY); // entry scoped to src 2 only

    uint8_t frame[1] = {0xAA};
    TEST_ASSERT_EQUAL_UINT8(1, in1(frame, 1)); // frame is from src 1 -> entry doesn't apply -> default allow
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(0, stats().acl_denied);
}

void test_acl_short_frame_skips_entry()
{
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);
    uint8_t pat[2] = {0x11, 0x22}, msk[2] = {0xFF, 0xFF};
    pc_forward_acl_add(1, 4, pat, msk, 2, PC_FWD_DENY); // needs len >= 6
    uint8_t shortf[3] = {0x11, 0x22, 0x33};             // too short at offset 4 -> ACE inapplicable
    TEST_ASSERT_EQUAL_UINT8(1, in1(shortf, 3));         // default allow -> forwarded
}

void test_acl_add_validation_and_table_full()
{
    uint8_t big[PC_FWD_ACL_PATLEN + 1] = {0}, bm[PC_FWD_ACL_PATLEN + 1] = {0};
    TEST_ASSERT_FALSE(pc_forward_acl_add(1, 0, big, bm, PC_FWD_ACL_PATLEN + 1, PC_FWD_DENY)); // patlen too big
    for (int i = 0; i < PC_FWD_MAX_ACL; i++)
    {
        TEST_ASSERT_TRUE(pc_forward_acl_add(PC_FWD_IF_ANY, 0, NULL, NULL, 0, PC_FWD_ALLOW));
    }
    TEST_ASSERT_FALSE(pc_forward_acl_add(PC_FWD_IF_ANY, 0, NULL, NULL, 0, PC_FWD_ALLOW)); // full
}

void test_acl_add_null_pointer_validation()
{
    uint8_t pat[1] = {0x01}, msk[1] = {0xFF};
    TEST_ASSERT_FALSE(pc_forward_acl_add(1, 0, NULL, msk, 1, PC_FWD_DENY)); // null pattern
    TEST_ASSERT_FALSE(pc_forward_acl_add(1, 0, pat, NULL, 1, PC_FWD_DENY)); // null mask
}

// --- policy routes (route-by-tag to interface) ---

// Add a policy route matching frames whose first byte is @p c, bound to @p egress.
static proto_bool route_firstbyte(uint8_t src, char c, uint8_t egress, uint16_t cap)
{
    uint8_t pat[1] = {(uint8_t)c};
    uint8_t msk[1] = {0xFF};
    return pc_forward_route_add(src, 0, pat, msk, 1, egress, cap);
}

void test_route_selects_egress_and_falls_through()
{
    add_if(1);
    add_if(2);
    add_if(3);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);                  // normal path 1 -> 2
    TEST_ASSERT_TRUE(route_firstbyte(PC_FWD_IF_ANY, 'X', 3, 0)); // policy: 'X...' -> if 3 only

    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "Xyz")); // matched -> routed only to if 3
    TEST_ASSERT_EQUAL_size_t(1, g_cap[3].count);
    TEST_ASSERT_EQUAL_size_t(0, g_cap[2].count); // the fan-out rule was skipped
    TEST_ASSERT_EQUAL_UINT32(1, stats().policy_routed);

    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "abc")); // no route -> normal rule -> if 2
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().policy_routed); // unchanged
}

void test_route_never_reflects_to_source()
{
    add_if(1);
    add_if(2);
    route_firstbyte(PC_FWD_IF_ANY, 'X', 1, 0); // egress == source
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "Xyz"));
    TEST_ASSERT_EQUAL_size_t(0, g_cap[1].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().policy_routed);
}

void test_route_unregistered_egress_fail_closed()
{
    add_if(1);
    route_firstbyte(PC_FWD_IF_ANY, 'X', 9, 0); // if 9 is not registered
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "Xyz"));
    TEST_ASSERT_EQUAL_UINT32(1, stats().policy_routed);
    TEST_ASSERT_EQUAL_UINT32(1, stats().send_fail);
}

void test_route_src_specific_filters_by_source()
{
    add_if(1);
    add_if(2);
    add_if(3);
    TEST_ASSERT_TRUE(route_firstbyte(1, 'Y', 3, 0)); // route scoped to src 1 only
    pc_forward_add_rule(2, 3, PC_FWD_ALLOW, 0);      // normal fallback path for src 2

    TEST_ASSERT_EQUAL_UINT8(1, ingress(2, "Yes")); // route src doesn't match -> falls through to the rule
    TEST_ASSERT_EQUAL_size_t(1, g_cap[3].count);
    TEST_ASSERT_EQUAL_UINT32(0, stats().policy_routed);

    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "Yz")); // route src matches -> routed
    TEST_ASSERT_EQUAL_size_t(2, g_cap[3].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().policy_routed);
}

void test_route_send_failure_counted()
{
    add_if(1);
    add_if(2);
    TEST_ASSERT_TRUE(route_firstbyte(PC_FWD_IF_ANY, 'Z', 2, 0));
    g_cap[2].accept = PROTO_FALSE; // egress interface refuses
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "Zzz"));
    TEST_ASSERT_EQUAL_UINT32(1, stats().policy_routed);
    TEST_ASSERT_EQUAL_UINT32(1, stats().send_fail);
    TEST_ASSERT_EQUAL_UINT32(0, stats().forwarded);
}

void test_route_rate_cap()
{
    add_if(1);
    add_if(2);
    route_firstbyte(PC_FWD_IF_ANY, 'X', 2, 1); // 1 frame/sec to the egress
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "X1"));
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "X2")); // over cap -> dropped
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(1, stats().rate_dropped);
    pc_forward_test_set_now(1000); // next window
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "X3"));
    TEST_ASSERT_EQUAL_size_t(2, g_cap[2].count);
}

void test_route_default_any_content()
{
    add_if(1);
    add_if(2);
    TEST_ASSERT_TRUE(pc_forward_route_add(PC_FWD_IF_ANY, 0, NULL, NULL, 0, 2, 0)); // patlen 0
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "anything"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
}

void test_route_first_match_wins()
{
    add_if(1);
    add_if(2);
    add_if(3);
    route_firstbyte(PC_FWD_IF_ANY, 'X', 2, 0); // added first -> if 2
    route_firstbyte(PC_FWD_IF_ANY, 'X', 3, 0); // also matches -> if 3
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "Xy"));
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_size_t(0, g_cap[3].count);
}

void test_route_add_validation_and_table_full()
{
    uint8_t pat[PC_FWD_ACL_PATLEN + 1] = {0}, msk[PC_FWD_ACL_PATLEN + 1] = {0};
    TEST_ASSERT_FALSE(pc_forward_route_add(PC_FWD_IF_ANY, 0, pat, msk, PC_FWD_ACL_PATLEN + 1, 2, 0)); // patlen big
    TEST_ASSERT_FALSE(pc_forward_route_add(PC_FWD_IF_ANY, 0, NULL, msk, 1, 2, 0)); // null pattern, patlen > 0
    TEST_ASSERT_FALSE(pc_forward_route_add(PC_FWD_IF_ANY, 0, pat, NULL, 1, 2, 0)); // null mask, patlen > 0
    for (int i = 0; i < PC_FWD_MAX_ROUTES; i++)
    {
        TEST_ASSERT_TRUE(route_firstbyte(PC_FWD_IF_ANY, 'A', 2, 0));
    }
    TEST_ASSERT_FALSE(route_firstbyte(PC_FWD_IF_ANY, 'A', 2, 0)); // table full
}

// --- inspection hook (PC_FWD_INSPECT) ---

#if PC_FWD_INSPECT
static int g_inspect_calls = 0;
// Inspector that drops frames whose first byte is 'D', passes the rest, and counts calls.
static pc_fwd_verdict inspect_drop_D(uint8_t src, const uint8_t *d, uint16_t n, void *ctx)
{
    (void)src;
    (void)ctx;
    g_inspect_calls++;
    if (n > 0 && d[0] == 'D')
    {
        return PC_FWD_INSPECT_DROP;
    }
    return PC_FWD_INSPECT_PASS;
}

void test_inspect_pass_and_drop()
{
    g_inspect_calls = 0;
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);
    pc_forward_set_inspector(inspect_drop_D, NULL);

    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "ok")); // passes inspection -> forwarded
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "Drop it")); // inspector drops
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);       // not forwarded
    TEST_ASSERT_EQUAL_UINT32(1, stats().inspect_dropped);
    TEST_ASSERT_EQUAL_INT(2, g_inspect_calls); // called for every ingress that passed the ACL
}

void test_inspect_runs_after_acl()
{
    g_inspect_calls = 0;
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);
    pc_forward_set_inspector(inspect_drop_D, NULL);
    // deny 'X...' at the ACL: the inspector must not even see an ACL-denied frame
    uint8_t pat[1] = {'X'}, msk[1] = {0xFF};
    pc_forward_acl_add(PC_FWD_IF_ANY, 0, pat, msk, 1, PC_FWD_DENY);

    TEST_ASSERT_EQUAL_UINT8(0, ingress(1, "Xhi")); // ACL-denied
    TEST_ASSERT_EQUAL_INT(0, g_inspect_calls);     // inspector never called
    TEST_ASSERT_EQUAL_UINT32(1, stats().acl_denied);
}

void test_inspect_cleared_by_null()
{
    add_if(1);
    add_if(2);
    pc_forward_add_rule(1, 2, PC_FWD_ALLOW, 0);
    pc_forward_set_inspector(inspect_drop_D, NULL);
    pc_forward_set_inspector(NULL, NULL);           // clear it
    TEST_ASSERT_EQUAL_UINT8(1, ingress(1, "Drop")); // would drop, but inspector is gone
    TEST_ASSERT_EQUAL_size_t(1, g_cap[2].count);
    TEST_ASSERT_EQUAL_UINT32(0, stats().inspect_dropped);
}
#endif // PC_FWD_INSPECT

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
#if PC_FWD_INSPECT
    RUN_TEST(test_inspect_pass_and_drop);
    RUN_TEST(test_inspect_runs_after_acl);
    RUN_TEST(test_inspect_cleared_by_null);
#endif
    return UNITY_END();
}
