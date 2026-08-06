// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit and stress tests for the Server-Sent Events connection pool (sse.h/cpp).
//
// Sections:
//   POOL        -- pc_sse_init / pc_sse_alloc / pc_sse_find / pc_sse_free invariants
//   WRITE       -- pc_sse_write() guard conditions and return values
//   STRESS      -- sustained alloc/free cycles and multi-slot isolation

#include "network_drivers/presentation/http/sse/sse.h"
#include "network_drivers/presentation/presentation.h" // http_conn_open (SSE-teardown regression)
#include <string.h>

#include <unity.h>
#include "network_drivers/transport/tcp.h"

void setUp()
{
    pc_sse_init();
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].pcb = pc_net_host_pcb();
    }
}

void tearDown()
{
}

// ====================================================================
// POOL TESTS - pc_sse_init()
// ====================================================================

void test_sse_pool_size()
{
    TEST_ASSERT_EQUAL(2, MAX_SSE_CONNS); // default
}

void test_sse_ids_match_indices_after_init()
{
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(i, (int)pc_sse_pool[i].pc_sse_id);
    }
}

void test_sse_all_inactive_after_init()
{
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        TEST_ASSERT_FALSE(pc_sse_pool[i].active);
    }
}

void test_sse_path_empty_after_init()
{
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        TEST_ASSERT_EQUAL('\0', pc_sse_pool[i].path[0]);
    }
}

// ====================================================================
// POOL TESTS - pc_sse_alloc()
// ====================================================================

void test_sse_alloc_returns_non_null()
{
    TEST_ASSERT_NOT_NULL(pc_sse_alloc(0, "/events"));
}

void test_sse_alloc_sets_active()
{
    SseConn *sse = pc_sse_alloc(0, "/events");
    TEST_ASSERT_TRUE(sse->active);
}

void test_sse_alloc_sets_slot_id()
{
    SseConn *sse = pc_sse_alloc(0, "/events");
    TEST_ASSERT_EQUAL(0, (int)sse->slot_id);
}

void test_sse_alloc_stores_path()
{
    SseConn *sse = pc_sse_alloc(0, "/sensors");
    TEST_ASSERT_EQUAL_STRING("/sensors", sse->path);
}

void test_sse_alloc_stores_different_paths_per_slot()
{
    SseConn *s0 = pc_sse_alloc(0, "/events");
    SseConn *s1 = pc_sse_alloc(1, "/metrics");
    TEST_ASSERT_EQUAL_STRING("/events", s0->path);
    TEST_ASSERT_EQUAL_STRING("/metrics", s1->path);
}

void test_sse_alloc_path_truncated_to_max()
{
    // Build a path longer than MAX_PATH_LEN
    char long_path[MAX_PATH_LEN + 16];
    long_path[0] = '/';
    for (int i = 1; i < MAX_PATH_LEN + 15; i++)
    {
        long_path[i] = 'x';
    }
    long_path[MAX_PATH_LEN + 15] = '\0';

    SseConn *sse = pc_sse_alloc(0, long_path);
    TEST_ASSERT_NOT_NULL(sse);
    TEST_ASSERT_EQUAL(MAX_PATH_LEN - 1, (int)strlen(sse->path));
    TEST_ASSERT_EQUAL('\0', sse->path[MAX_PATH_LEN - 1]);
}

void test_sse_alloc_pool_full_returns_null()
{
    TEST_ASSERT_NOT_NULL(pc_sse_alloc(0, "/a"));
    TEST_ASSERT_NOT_NULL(pc_sse_alloc(1, "/b"));
    TEST_ASSERT_NULL(pc_sse_alloc(2, "/c")); // MAX_SSE_CONNS = 2
}

void test_sse_alloc_sse_id_is_pool_index()
{
    // First free slot is 0 → pc_sse_id should be 0
    SseConn *s0 = pc_sse_alloc(0, "/a");
    TEST_ASSERT_EQUAL(0, (int)s0->pc_sse_id);
    // Second free slot is 1 → pc_sse_id should be 1
    SseConn *s1 = pc_sse_alloc(1, "/b");
    TEST_ASSERT_EQUAL(1, (int)s1->pc_sse_id);
}

// ====================================================================
// POOL TESTS - pc_sse_find()
// ====================================================================

void test_sse_find_returns_correct_conn()
{
    SseConn *allocated = pc_sse_alloc(0, "/events");
    SseConn *found = pc_sse_find(0);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_PTR(allocated, found);
}

void test_sse_find_returns_null_when_empty()
{
    TEST_ASSERT_NULL(pc_sse_find(0));
}

void test_sse_find_returns_null_for_different_slot()
{
    pc_sse_alloc(0, "/events");
    TEST_ASSERT_NULL(pc_sse_find(1));
}

void test_sse_find_after_both_slots_allocated()
{
    pc_sse_alloc(0, "/a");
    pc_sse_alloc(1, "/b");
    TEST_ASSERT_NOT_NULL(pc_sse_find(0));
    TEST_ASSERT_NOT_NULL(pc_sse_find(1));
}

void test_sse_find_checks_slot_id_not_sse_id()
{
    // pc_sse_pool[0] → slot 3; pc_sse_find(3) must return it, not pc_sse_find(0)
    SseConn *sse = pc_sse_alloc(3, "/x");
    TEST_ASSERT_NULL(pc_sse_find(0));
    TEST_ASSERT_NOT_NULL(pc_sse_find(3));
    TEST_ASSERT_EQUAL_PTR(sse, pc_sse_find(3));
}

// ====================================================================
// POOL TESTS - pc_sse_free()
// ====================================================================

void test_sse_free_deactivates_slot()
{
    pc_sse_alloc(0, "/events");
    pc_sse_free(0);
    TEST_ASSERT_FALSE(pc_sse_pool[0].active);
}

void test_sse_free_restores_sse_id()
{
    pc_sse_alloc(0, "/events");
    pc_sse_free(0);
    TEST_ASSERT_EQUAL(0, (int)pc_sse_pool[0].pc_sse_id);
}

void test_sse_free_makes_slot_findable_as_null()
{
    pc_sse_alloc(0, "/events");
    pc_sse_free(0);
    TEST_ASSERT_NULL(pc_sse_find(0));
}

void test_sse_free_clears_path()
{
    pc_sse_alloc(0, "/events");
    pc_sse_free(0);
    TEST_ASSERT_EQUAL('\0', pc_sse_pool[0].path[0]);
}

void test_sse_free_nop_on_unallocated()
{
    pc_sse_free(2); // slot 2 was never allocated
    // No crash; pool state unchanged
    TEST_ASSERT_FALSE(pc_sse_pool[0].active);
    TEST_ASSERT_FALSE(pc_sse_pool[1].active);
    TEST_PASS();
}

void test_sse_alloc_after_free_succeeds()
{
    pc_sse_alloc(0, "/events");
    pc_sse_free(0);
    SseConn *sse = pc_sse_alloc(0, "/new");
    TEST_ASSERT_NOT_NULL(sse);
    TEST_ASSERT_TRUE(sse->active);
    TEST_ASSERT_EQUAL_STRING("/new", sse->path);
}

void test_sse_free_only_frees_matching_slot()
{
    pc_sse_alloc(0, "/a");
    pc_sse_alloc(1, "/b");
    pc_sse_free(0);
    TEST_ASSERT_FALSE(pc_sse_pool[0].active);
    TEST_ASSERT_TRUE(pc_sse_pool[1].active);
    TEST_ASSERT_EQUAL_STRING("/b", pc_sse_pool[1].path);
}

// ====================================================================
// WRITE TESTS - pc_sse_write()
// ====================================================================

void test_sse_write_null_data_returns_false()
{
    SseConn *sse = pc_sse_alloc(0, "/events");
    TEST_ASSERT_FALSE(pc_sse_write(sse, NULL, NULL, NULL));
}

void test_sse_write_returns_false_when_conn_not_active()
{
    SseConn *sse = pc_sse_alloc(0, "/events");
    conn_pool[0].state = CONN_FREE; // slot not active
    TEST_ASSERT_FALSE(pc_sse_write(sse, "hello", NULL, NULL));
}

void test_sse_write_returns_false_when_pcb_null()
{
    SseConn *sse = pc_sse_alloc(0, "/events");
    conn_pool[0].pcb = NULL;
    TEST_ASSERT_FALSE(pc_sse_write(sse, "data", NULL, NULL));
}

void test_sse_write_data_only_returns_true()
{
    SseConn *sse = pc_sse_alloc(0, "/events");
    TEST_ASSERT_TRUE(pc_sse_write(sse, "hello", NULL, NULL));
}

void test_sse_write_with_event_returns_true()
{
    SseConn *sse = pc_sse_alloc(0, "/events");
    TEST_ASSERT_TRUE(pc_sse_write(sse, "payload", "update", NULL));
}

void test_sse_write_with_id_returns_true()
{
    SseConn *sse = pc_sse_alloc(0, "/events");
    TEST_ASSERT_TRUE(pc_sse_write(sse, "payload", NULL, "42"));
}

void test_sse_write_with_all_fields_returns_true()
{
    SseConn *sse = pc_sse_alloc(0, "/events");
    TEST_ASSERT_TRUE(pc_sse_write(sse, "body", "status", "1"));
}

void test_sse_write_does_not_affect_other_slots()
{
    SseConn *s0 = pc_sse_alloc(0, "/a");
    SseConn *s1 = pc_sse_alloc(1, "/b");
    // Write to slot 0 -- slot 1 state must be unchanged
    pc_sse_write(s0, "msg", NULL, NULL);
    TEST_ASSERT_TRUE(s1->active);
    TEST_ASSERT_EQUAL_STRING("/b", s1->path);
    TEST_ASSERT_EQUAL(1, (int)s1->slot_id);
}

// ====================================================================
// TEARDOWN REGRESSION - a reused HTTP slot must not inherit a stale SSE binding
// ====================================================================
//
// Regression for the SSE-teardown slot leak (docs/BUGS.md): pc_sse_free() had no caller, so a closed or
// idle-reaped SSE stream left its pc_sse_pool entry active. When a new HTTP connection reused that conn
// slot, http_poll_slot() saw pc_sse_find(slot) and skipped HTTP dispatch, wedging the server (a live,
// HW-reproduced DoS). http_conn_open() now releases any stale WS/SSE binding for the slot.

void test_http_conn_open_releases_stale_sse_binding()
{
    pc_sse_alloc(0, "/events");
    TEST_ASSERT_NOT_NULL(pc_sse_find(0)); // slot 0 has an SSE binding
    http_conn_open(0);                    // a fresh HTTP connection reuses the slot
    TEST_ASSERT_NULL(pc_sse_find(0));     // ...and must NOT inherit the stale binding
}

void test_http_conn_open_leaves_other_slot_sse_binding()
{
    pc_sse_alloc(0, "/events");
    pc_sse_alloc(1, "/metrics");
    http_conn_open(0);                    // reuse slot 0 only
    TEST_ASSERT_NULL(pc_sse_find(0));     // slot 0 cleared
    TEST_ASSERT_NOT_NULL(pc_sse_find(1)); // slot 1's binding is untouched
}

// ====================================================================
// FORMAT TESTS - pc_sse_format() exact wire bytes (WHATWG event-stream)
// ====================================================================

void test_sse_format_data_only()
{
    char buf[64];
    int n = pc_sse_format(buf, sizeof(buf), "hello", NULL, NULL);
    TEST_ASSERT_EQUAL_STRING("data: hello\n\n", buf);
    TEST_ASSERT_EQUAL((int)strlen("data: hello\n\n"), n);
}

void test_sse_format_event_and_data()
{
    char buf[64];
    int n = pc_sse_format(buf, sizeof(buf), "payload", "update", NULL);
    TEST_ASSERT_EQUAL_STRING("event: update\ndata: payload\n\n", buf);
    TEST_ASSERT_EQUAL((int)strlen("event: update\ndata: payload\n\n"), n);
}

void test_sse_format_id_and_data()
{
    char buf[64];
    int n = pc_sse_format(buf, sizeof(buf), "payload", NULL, "42");
    TEST_ASSERT_EQUAL_STRING("id: 42\ndata: payload\n\n", buf);
    TEST_ASSERT_EQUAL((int)strlen("id: 42\ndata: payload\n\n"), n);
}

void test_sse_format_all_fields_ordering()
{
    // Field order per WHATWG: event, then id, then data (blank line terminates).
    char buf[64];
    int n = pc_sse_format(buf, sizeof(buf), "body", "status", "1");
    TEST_ASSERT_EQUAL_STRING("event: status\nid: 1\ndata: body\n\n", buf);
    TEST_ASSERT_EQUAL((int)strlen("event: status\nid: 1\ndata: body\n\n"), n);
}

void test_sse_format_null_data_returns_zero()
{
    char buf[64];
    TEST_ASSERT_EQUAL(0, pc_sse_format(buf, sizeof(buf), NULL, "x", "1"));
}

void test_sse_format_overflow_returns_zero()
{
    // A record that cannot fit must report 0, never a partial (truncated) frame.
    char buf[8];
    TEST_ASSERT_EQUAL(0, pc_sse_format(buf, sizeof(buf), "a-long-payload-value", "an-event", "99"));
}

void test_sse_format_zero_size_returns_zero()
{
    char buf[8];
    TEST_ASSERT_EQUAL(0, pc_sse_format(buf, 0, "data", NULL, NULL));
}

// The event block's own "event: " prefix append failing (distinct from the value append
// failing, which test_sse_format_overflow_returns_zero already covers).
void test_sse_format_event_prefix_itself_overflows()
{
    char buf[5];
    TEST_ASSERT_EQUAL(0, pc_sse_format(buf, sizeof(buf), "y", "x", NULL));
}

// The event block's trailing "\n" append failing: the prefix + value fit exactly, leaving
// no room for the newline.
void test_sse_format_event_newline_overflows()
{
    char buf[10]; // "event: " (7) + "ab" (2) == 9 == n-1; the '\n' has no room left
    TEST_ASSERT_EQUAL(0, pc_sse_format(buf, sizeof(buf), "unused", "ab", NULL));
}

// The id block's three internal appends ("id: " prefix, value, trailing "\n") each failing
// in turn - none of these arms are exercised by any success-path test above.
void test_sse_format_id_block_failure_arms()
{
    char a[4]; // "id: " (4) alone already exceeds n-1 (3) -> prefix append fails
    TEST_ASSERT_EQUAL(0, pc_sse_format(a, sizeof(a), "d", NULL, "z"));

    char b[7]; // "id: " (4) fits, "XYZ" (3) does not (n-1 == 6)
    TEST_ASSERT_EQUAL(0, pc_sse_format(b, sizeof(b), "d", NULL, "XYZ"));

    char c[7]; // "id: " (4) + "XY" (2) == 6 == n-1; the '\n' has no room left
    TEST_ASSERT_EQUAL(0, pc_sse_format(c, sizeof(c), "d", NULL, "XY"));
}

// The final data block's three internal appends ("data: " prefix, value, "\n\n" terminator)
// each failing in turn - the event/id blocks are skipped (both null) so these are the first
// appends attempted.
void test_sse_format_data_block_failure_arms()
{
    char a[5]; // "data: " (6) alone already exceeds n-1 (4) -> prefix append fails
    TEST_ASSERT_EQUAL(0, pc_sse_format(a, sizeof(a), "abcdef", NULL, NULL));

    char b[10]; // "data: " (6) fits, "abcdef" (6) does not (n-1 == 9)
    TEST_ASSERT_EQUAL(0, pc_sse_format(b, sizeof(b), "abcdef", NULL, NULL));

    char c[9]; // "data: " (6) + "ab" (2) == 8 == n-1; the "\n\n" terminator has no room left
    TEST_ASSERT_EQUAL(0, pc_sse_format(c, sizeof(c), "ab", NULL, NULL));
}

// ====================================================================
// STRESS TESTS
// ====================================================================

// 100 alloc/free cycles on one slot -- no state accumulation
void stress_sse_alloc_free_100_cycles()
{
    for (int i = 0; i < 100; i++)
    {
        SseConn *sse = pc_sse_alloc(0, "/events");
        TEST_ASSERT_NOT_NULL_MESSAGE(sse, "alloc failed");
        TEST_ASSERT_TRUE_MESSAGE(sse->active, "not active");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("/events", sse->path, "path wrong");
        pc_sse_free(0);
        TEST_ASSERT_FALSE_MESSAGE(pc_sse_pool[0].active, "still active after free");
    }
}

// Alloc/free across both slots in alternating order
void stress_sse_alloc_free_both_slots_alternating()
{
    for (int cycle = 0; cycle < 50; cycle++)
    {
        SseConn *s0 = pc_sse_alloc(0, "/a");
        SseConn *s1 = pc_sse_alloc(1, "/b");
        TEST_ASSERT_NOT_NULL(s0);
        TEST_ASSERT_NOT_NULL(s1);
        TEST_ASSERT_NULL(pc_sse_alloc(2, "/c")); // pool full

        pc_sse_free(1);
        SseConn *s1b = pc_sse_alloc(1, "/new");
        TEST_ASSERT_NOT_NULL(s1b);
        TEST_ASSERT_EQUAL_STRING("/new", s1b->path);

        pc_sse_free(0);
        pc_sse_free(1);
    }
}

// 100 pc_sse_write calls on one slot -- no crash, no state corruption
void stress_sse_write_100_calls()
{
    SseConn *sse = pc_sse_alloc(0, "/events");
    for (int i = 0; i < 100; i++)
    {
        proto_bool ok = pc_sse_write(sse, "data", "update", "1");
        TEST_ASSERT_TRUE_MESSAGE(ok, "write failed");
    }
    // Slot still intact after 100 writes
    TEST_ASSERT_TRUE(sse->active);
    TEST_ASSERT_EQUAL(0, (int)sse->slot_id);
}

// find() across full pool -- returns correct entry regardless of pool order
void stress_sse_find_with_full_pool()
{
    SseConn *s0 = pc_sse_alloc(0, "/x");
    SseConn *s1 = pc_sse_alloc(1, "/y");
    for (int i = 0; i < 50; i++)
    {
        TEST_ASSERT_EQUAL_PTR(s0, pc_sse_find(0));
        TEST_ASSERT_EQUAL_PTR(s1, pc_sse_find(1));
        TEST_ASSERT_NULL(pc_sse_find(2));
        TEST_ASSERT_NULL(pc_sse_find(3));
    }
}

// Slot isolation: write to slot 0 must not corrupt slot 1 path or state
void stress_sse_write_slot_isolation()
{
    SseConn *s0 = pc_sse_alloc(0, "/events");
    SseConn *s1 = pc_sse_alloc(1, "/metrics");

    for (int i = 0; i < 50; i++)
    {
        pc_sse_write(s0, "event_data", "update", "123");
    }

    TEST_ASSERT_EQUAL_STRING("/metrics", s1->path);
    TEST_ASSERT_TRUE(s1->active);
    TEST_ASSERT_EQUAL(1, (int)s1->slot_id);
    TEST_ASSERT_EQUAL(1, (int)s1->pc_sse_id);
}

int main()
{
    UNITY_BEGIN();

    // Pool: init
    RUN_TEST(test_sse_pool_size);
    RUN_TEST(test_sse_ids_match_indices_after_init);
    RUN_TEST(test_sse_all_inactive_after_init);
    RUN_TEST(test_sse_path_empty_after_init);

    // Pool: alloc
    RUN_TEST(test_sse_alloc_returns_non_null);
    RUN_TEST(test_sse_alloc_sets_active);
    RUN_TEST(test_sse_alloc_sets_slot_id);
    RUN_TEST(test_sse_alloc_stores_path);
    RUN_TEST(test_sse_alloc_stores_different_paths_per_slot);
    RUN_TEST(test_sse_alloc_path_truncated_to_max);
    RUN_TEST(test_sse_alloc_pool_full_returns_null);
    RUN_TEST(test_sse_alloc_sse_id_is_pool_index);

    // Pool: find
    RUN_TEST(test_sse_find_returns_correct_conn);
    RUN_TEST(test_sse_find_returns_null_when_empty);
    RUN_TEST(test_sse_find_returns_null_for_different_slot);
    RUN_TEST(test_sse_find_after_both_slots_allocated);
    RUN_TEST(test_sse_find_checks_slot_id_not_sse_id);

    // Pool: free
    RUN_TEST(test_sse_free_deactivates_slot);
    RUN_TEST(test_sse_free_restores_sse_id);
    RUN_TEST(test_sse_free_makes_slot_findable_as_null);
    RUN_TEST(test_sse_free_clears_path);
    RUN_TEST(test_sse_free_nop_on_unallocated);
    RUN_TEST(test_sse_alloc_after_free_succeeds);
    RUN_TEST(test_sse_free_only_frees_matching_slot);

    // Write
    RUN_TEST(test_sse_write_null_data_returns_false);
    RUN_TEST(test_sse_write_returns_false_when_conn_not_active);
    RUN_TEST(test_sse_write_returns_false_when_pcb_null);
    RUN_TEST(test_sse_write_data_only_returns_true);
    RUN_TEST(test_sse_write_with_event_returns_true);
    RUN_TEST(test_sse_write_with_id_returns_true);
    RUN_TEST(test_sse_write_with_all_fields_returns_true);
    RUN_TEST(test_sse_write_does_not_affect_other_slots);

    // Teardown regression (SSE slot leak -> DoS)
    RUN_TEST(test_http_conn_open_releases_stale_sse_binding);
    RUN_TEST(test_http_conn_open_leaves_other_slot_sse_binding);

    // Format
    RUN_TEST(test_sse_format_data_only);
    RUN_TEST(test_sse_format_event_and_data);
    RUN_TEST(test_sse_format_id_and_data);
    RUN_TEST(test_sse_format_all_fields_ordering);
    RUN_TEST(test_sse_format_null_data_returns_zero);
    RUN_TEST(test_sse_format_overflow_returns_zero);
    RUN_TEST(test_sse_format_zero_size_returns_zero);
    RUN_TEST(test_sse_format_event_prefix_itself_overflows);
    RUN_TEST(test_sse_format_event_newline_overflows);
    RUN_TEST(test_sse_format_id_block_failure_arms);
    RUN_TEST(test_sse_format_data_block_failure_arms);

    // Stress
    RUN_TEST(stress_sse_alloc_free_100_cycles);
    RUN_TEST(stress_sse_alloc_free_both_slots_alternating);
    RUN_TEST(stress_sse_write_100_calls);
    RUN_TEST(stress_sse_find_with_full_pool);
    RUN_TEST(stress_sse_write_slot_isolation);

    return UNITY_END();
}
