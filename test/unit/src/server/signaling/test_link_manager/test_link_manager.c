// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the multi-interface egress policy (server/signaling/link_manager.h).
//
// No standard publishes an interface-failover policy, so every expectation here is category 3: a
// property the selection must hold whatever the implementation. The load-bearing one is
// test_selection_is_total_order_over_priority_then_index - the header states "higher priority wins;
// the lower index breaks a tie", which makes selection a total order, and a policy that is not
// deterministic reconfigures the netif on a tie and flaps traffic between two equal links forever.

#include "server/signaling/link_manager.h"

#include <unity.h>

// Eth (prio 20), WiFi STA (prio 10), softAP (prio 5) - a device with all three brought up.
static LinkIface g_ifaces[3];
static LinkManager g_m;

static void bind(LinkManager *m, LinkIface *ifaces, size_t n)
{
    Link.args.m = m;
    Link.args.ifaces = ifaces;
    Link.args.n = n;
    Link.init(Link.internal);
}

static int active(const LinkManager *m)
{
    Link.args.m_ro = m;
    Link.active(Link.internal);
    return Link.i32;
}

static int select_best(const LinkManager *m)
{
    Link.args.m_ro = m;
    Link.select(Link.internal);
    return Link.i32;
}

static proto_bool set_up(LinkManager *m, size_t idx, proto_bool up)
{
    Link.args.m = m;
    Link.args.idx = idx;
    Link.args.up = up;
    Link.set(Link.internal);
    return Link.changed;
}

void setUp(void)
{
    g_ifaces[0] = (LinkIface){LINK_KIND_ETH, 20, PROTO_FALSE};
    g_ifaces[1] = (LinkIface){LINK_KIND_WIFI_STA, 10, PROTO_FALSE};
    g_ifaces[2] = (LinkIface){LINK_KIND_WIFI_AP, 5, PROTO_FALSE};
    bind(&g_m, g_ifaces, 3);
}
void tearDown(void)
{
}

// With no carrier anywhere there is no egress, and -1 is the only value that can say so: every
// index from 0 up names a real interface.
void test_no_interface_up_selects_nothing(void)
{
    TEST_ASSERT_EQUAL_INT(-1, active(&g_m));
    TEST_ASSERT_EQUAL_INT(-1, select_best(&g_m));
}

// "Higher priority wins; the lower index breaks a tie" orders every pair of up interfaces, so the
// winner is the same whatever order the table is walked in and wherever the winner sits.
void test_selection_is_total_order_over_priority_then_index(void)
{
    // The higher priority sits at the later index: position does not decide.
    LinkIface later[2] = {{LINK_KIND_WIFI_AP, 5, PROTO_TRUE}, {LINK_KIND_ETH, 20, PROTO_TRUE}};
    LinkManager m;
    bind(&m, later, 2);
    TEST_ASSERT_EQUAL_INT(1, active(&m));

    // The same two the other way round: the winner is the same interface, now at index 0.
    LinkIface earlier[2] = {{LINK_KIND_ETH, 20, PROTO_TRUE}, {LINK_KIND_WIFI_AP, 5, PROTO_TRUE}};
    bind(&m, earlier, 2);
    TEST_ASSERT_EQUAL_INT(0, active(&m));

    // Equal priority: the lower index, so the choice is not left to the walk.
    LinkIface tied[2] = {{LINK_KIND_ETH, 10, PROTO_TRUE}, {LINK_KIND_WIFI_STA, 10, PROTO_TRUE}};
    bind(&m, tied, 2);
    TEST_ASSERT_EQUAL_INT(0, active(&m));

    // Three at the same priority still picks the first.
    LinkIface all_tied[3] = {
        {LINK_KIND_ETH, 7, PROTO_TRUE}, {LINK_KIND_WIFI_STA, 7, PROTO_TRUE}, {LINK_KIND_WIFI_AP, 7, PROTO_TRUE}};
    bind(&m, all_tied, 3);
    TEST_ASSERT_EQUAL_INT(0, active(&m));
}

// A link coming up escalates only when it beats the one carrying traffic, and a link going down
// falls back to the best of what remains. from and to name the transition either way.
void test_escalation_and_failover_walk_the_priority_order(void)
{
    TEST_ASSERT_TRUE(set_up(&g_m, 1, PROTO_TRUE)); // WiFi STA first: nothing was up
    TEST_ASSERT_EQUAL_INT(-1, Link.from);
    TEST_ASSERT_EQUAL_INT(1, Link.to);

    TEST_ASSERT_TRUE(set_up(&g_m, 0, PROTO_TRUE)); // Ethernet beats it
    TEST_ASSERT_EQUAL_INT(1, Link.from);
    TEST_ASSERT_EQUAL_INT(0, Link.to);

    TEST_ASSERT_FALSE(set_up(&g_m, 2, PROTO_TRUE)); // softAP loses to both
    TEST_ASSERT_EQUAL_INT(0, Link.from);
    TEST_ASSERT_EQUAL_INT(0, Link.to);

    TEST_ASSERT_TRUE(set_up(&g_m, 0, PROTO_FALSE)); // Ethernet drops: next best is WiFi STA
    TEST_ASSERT_EQUAL_INT(0, Link.from);
    TEST_ASSERT_EQUAL_INT(1, Link.to);

    TEST_ASSERT_TRUE(set_up(&g_m, 1, PROTO_FALSE)); // and then the softAP
    TEST_ASSERT_EQUAL_INT(2, Link.to);

    TEST_ASSERT_TRUE(set_up(&g_m, 2, PROTO_FALSE)); // nothing left
    TEST_ASSERT_EQUAL_INT(-1, Link.to);
    TEST_ASSERT_EQUAL_INT(-1, active(&g_m));
}

// changed is what the app reconfigures on, so it reports a moved egress and nothing else. Setting a
// carrier state it already holds moves nothing, however many times it is set.
void test_changed_reports_a_moved_egress_only(void)
{
    TEST_ASSERT_TRUE(set_up(&g_m, 0, PROTO_TRUE));
    TEST_ASSERT_FALSE(set_up(&g_m, 0, PROTO_TRUE));
    TEST_ASSERT_FALSE(set_up(&g_m, 0, PROTO_TRUE));
    TEST_ASSERT_EQUAL_INT(0, active(&g_m));

    // A lower-priority link going up and back down never touches the active one.
    TEST_ASSERT_FALSE(set_up(&g_m, 2, PROTO_TRUE));
    TEST_ASSERT_FALSE(set_up(&g_m, 2, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT(0, active(&g_m));
}

// select answers from the table without touching it, so asking twice gives the same answer and the
// active interface is unmoved by the asking.
void test_select_does_not_move_the_active_interface(void)
{
    (void)set_up(&g_m, 1, PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(1, select_best(&g_m));
    TEST_ASSERT_EQUAL_INT(1, select_best(&g_m));
    TEST_ASSERT_EQUAL_INT(1, active(&g_m));
}

// An index past the end of the table names no interface, so the call changes nothing and reports
// the same interface as both the previous and the current one.
void test_an_index_past_the_table_changes_nothing(void)
{
    (void)set_up(&g_m, 1, PROTO_TRUE);

    TEST_ASSERT_FALSE(set_up(&g_m, 3, PROTO_TRUE));
    TEST_ASSERT_EQUAL_INT(1, Link.from);
    TEST_ASSERT_EQUAL_INT(1, Link.to);
    TEST_ASSERT_EQUAL_INT(1, active(&g_m));

    TEST_ASSERT_FALSE(set_up(&g_m, (size_t)-1, PROTO_TRUE));
    TEST_ASSERT_EQUAL_INT(1, active(&g_m));
}

// A manager with no table carries no interfaces however large its count claims to be, so its
// selection is the empty one and a set against it moves nothing.
void test_a_manager_with_no_table_carries_nothing(void)
{
    LinkManager m;
    bind(&m, NULL, 3);
    TEST_ASSERT_EQUAL_size_t(0u, m.n);
    TEST_ASSERT_EQUAL_INT(-1, select_best(&m));
    TEST_ASSERT_EQUAL_INT(-1, active(&m));

    TEST_ASSERT_FALSE(set_up(&m, 0, PROTO_TRUE));
    TEST_ASSERT_EQUAL_INT(-1, Link.from);
    TEST_ASSERT_EQUAL_INT(-1, Link.to);
}

// Every call tolerates a missing manager and reports the no-egress answer.
void test_a_missing_manager_is_refused(void)
{
    bind(NULL, g_ifaces, 3);
    TEST_ASSERT_EQUAL_INT(-1, active(NULL));
    TEST_ASSERT_EQUAL_INT(-1, select_best(NULL));

    TEST_ASSERT_FALSE(set_up(NULL, 0, PROTO_TRUE));
    TEST_ASSERT_EQUAL_INT(-1, Link.from);
    TEST_ASSERT_EQUAL_INT(-1, Link.to);
}
