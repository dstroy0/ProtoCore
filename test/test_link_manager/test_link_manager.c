// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/link_manager: egress selection, graceful escalation, failover.

#include "server/signaling/link_manager.h"
#include <unity.h>

// Eth (prio 20), WiFi STA (prio 10), softAP (prio 5).
static LinkIface g_ifaces[3];
static LinkManager g_m;

void setUp(void)
{
    g_ifaces[0] = (LinkIface){LINK_KIND_ETH, 20, PROTO_FALSE};
    g_ifaces[1] = (LinkIface){LINK_KIND_WIFI_STA, 10, PROTO_FALSE};
    g_ifaces[2] = (LinkIface){LINK_KIND_WIFI_AP, 5, PROTO_FALSE};
    pc_link_init(&g_m, g_ifaces, 3);
}
void tearDown(void)
{
}

void test_init_none_up(void)
{
    TEST_ASSERT_EQUAL_INT(-1, pc_link_active(&g_m));
}

void test_escalation_and_failover(void)
{
    int from = 0, to = 0;
    // WiFi STA comes up first -> it becomes active.
    TEST_ASSERT_TRUE(pc_link_set(&g_m, 1, PROTO_TRUE, &from, &to));
    TEST_ASSERT_EQUAL_INT(-1, from);
    TEST_ASSERT_EQUAL_INT(1, to);
    // Ethernet comes up (higher priority) -> escalate to it.
    TEST_ASSERT_TRUE(pc_link_set(&g_m, 0, PROTO_TRUE, &from, &to));
    TEST_ASSERT_EQUAL_INT(1, from);
    TEST_ASSERT_EQUAL_INT(0, to);
    // softAP comes up (lower priority) -> no change.
    TEST_ASSERT_FALSE(pc_link_set(&g_m, 2, PROTO_TRUE, &from, &to));
    TEST_ASSERT_EQUAL_INT(0, pc_link_active(&g_m));
    // Ethernet drops -> fail over to the next best up (WiFi STA).
    TEST_ASSERT_TRUE(pc_link_set(&g_m, 0, PROTO_FALSE, &from, &to));
    TEST_ASSERT_EQUAL_INT(0, from);
    TEST_ASSERT_EQUAL_INT(1, to);
    // WiFi STA drops too -> fail over to softAP.
    TEST_ASSERT_TRUE(pc_link_set(&g_m, 1, PROTO_FALSE, &from, &to));
    TEST_ASSERT_EQUAL_INT(2, to);
    // softAP drops -> nothing up.
    TEST_ASSERT_TRUE(pc_link_set(&g_m, 2, PROTO_FALSE, &from, &to));
    TEST_ASSERT_EQUAL_INT(-1, pc_link_active(&g_m));
}

void test_tie_break_lower_index(void)
{
    // Two interfaces at equal priority: the lower index wins.
    LinkIface pair[2] = {{LINK_KIND_ETH, 10, PROTO_TRUE}, {LINK_KIND_WIFI_STA, 10, PROTO_TRUE}};
    LinkManager m;
    pc_link_init(&m, pair, 2);
    TEST_ASSERT_EQUAL_INT(0, pc_link_active(&m));
}

void test_select_escalates_to_later_higher_priority(void)
{
    // Both up, but the higher priority sits at the *later* index: the scan must still pick it,
    // exercising the right-hand side of `best < 0 || priority[i] > priority[best]` evaluating true.
    LinkIface ifaces[2] = {{LINK_KIND_WIFI_AP, 5, PROTO_TRUE}, {LINK_KIND_ETH, 20, PROTO_TRUE}};
    LinkManager m;
    pc_link_init(&m, ifaces, 2);
    TEST_ASSERT_EQUAL_INT(1, pc_link_active(&m));
}

void test_out_of_range_no_change(void)
{
    pc_link_set(&g_m, 1, PROTO_TRUE, NULL, NULL);
    int from = 5, to = 5;
    TEST_ASSERT_FALSE(pc_link_set(&g_m, 9, PROTO_TRUE, &from, &to));
    TEST_ASSERT_EQUAL_INT(1, from);
    TEST_ASSERT_EQUAL_INT(1, to);
}

// select guards a null manager and a manager with a null interface table.
void test_select_null_guards(void)
{
    TEST_ASSERT_EQUAL_INT(-1, pc_link_select(NULL));
    LinkManager m;
    pc_link_init(&m, NULL, 3); // null ifaces -> n forced to 0, active -1
    TEST_ASSERT_EQUAL_INT(-1, pc_link_select(&m));
    TEST_ASSERT_EQUAL_INT(-1, pc_link_active(&m));
}

// init and active tolerate a null manager (no crash, active reports -1).
void test_init_and_active_null(void)
{
    pc_link_init(NULL, g_ifaces, 3); // must simply return
    TEST_ASSERT_EQUAL_INT(-1, pc_link_active(NULL));
}

// set's failure paths still write from/to (or tolerate null out-pointers): null manager, null interface
// table, and an out-of-range index with null out-pointers.
void test_set_guard_paths(void)
{
    int from = 7, to = 7;
    // Null manager: reports -1 for both previous and new active, returns false.
    TEST_ASSERT_FALSE(pc_link_set(NULL, 0, PROTO_TRUE, &from, &to));
    TEST_ASSERT_EQUAL_INT(-1, from);
    TEST_ASSERT_EQUAL_INT(-1, to);

    // Null interface table: reports the manager's active (-1), returns false.
    LinkManager m;
    pc_link_init(&m, NULL, 3);
    from = 7;
    to = 7;
    TEST_ASSERT_FALSE(pc_link_set(&m, 0, PROTO_TRUE, &from, &to));
    TEST_ASSERT_EQUAL_INT(-1, from);
    TEST_ASSERT_EQUAL_INT(-1, to);

    // Out-of-range index with null out-pointers: guard path must not dereference them.
    TEST_ASSERT_FALSE(pc_link_set(&g_m, 9, PROTO_TRUE, NULL, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_none_up);
    RUN_TEST(test_escalation_and_failover);
    RUN_TEST(test_tie_break_lower_index);
    RUN_TEST(test_select_escalates_to_later_higher_priority);
    RUN_TEST(test_out_of_range_no_change);
    RUN_TEST(test_select_null_guards);
    RUN_TEST(test_init_and_active_null);
    RUN_TEST(test_set_guard_paths);
    return UNITY_END();
}
