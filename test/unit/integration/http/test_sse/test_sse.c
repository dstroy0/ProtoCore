// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/presentation/http/sse/sse.h"
#include "network_drivers/presentation/presentation.h"
#include "network_drivers/transport/tcp/common.h"
#include <string.h>

#include "network_drivers/transport/tcp/tcp.h"
#include <unity.h>

void setUp()
{
    Sse.init(Sse.internal);
    for (int i = 0; i < MAX_CONNS; i++)
    {
        conn_pool[i] = (TcpConn){0};
        conn_pool[i].id = (uint8_t)i;
        conn_pool[i].state = CONN_ACTIVE;
        conn_pool[i].pcb = protocore_net_host_pcb();
    }
}

void tearDown()
{
}

void test_sse_pool_size()
{
    TEST_ASSERT_EQUAL(2, MAX_SSE_CONNS);
}

void test_sse_ids_match_indices_after_init()
{
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        TEST_ASSERT_EQUAL(i, (int)protocore_sse_pool[i].protocore_sse_id);
    }
}

void test_sse_all_inactive_after_init()
{
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        TEST_ASSERT_FALSE(protocore_sse_pool[i].active);
    }
}

void test_sse_path_empty_after_init()
{
    for (int i = 0; i < MAX_SSE_CONNS; i++)
    {
        TEST_ASSERT_EQUAL('\0', protocore_sse_pool[i].path[0]);
    }
}

void test_sse_alloc_returns_non_null()
{
    TEST_ASSERT_NOT_NULL(protocore_sse_alloc(0, "/events"));
}

void test_sse_alloc_sets_active()
{
    SseConn *sse = protocore_sse_alloc(0, "/events");
    TEST_ASSERT_TRUE(sse->active);
}

void test_sse_alloc_sets_slot_id()
{
    SseConn *sse = protocore_sse_alloc(0, "/events");
    TEST_ASSERT_EQUAL(0, (int)sse->slot_id);
}

void test_sse_alloc_stores_path()
{
    SseConn *sse = protocore_sse_alloc(0, "/sensors");
    TEST_ASSERT_EQUAL_STRING("/sensors", sse->path);
}

void test_sse_alloc_stores_different_paths_per_slot()
{
    SseConn *s0 = protocore_sse_alloc(0, "/events");
    SseConn *s1 = protocore_sse_alloc(1, "/metrics");
    TEST_ASSERT_EQUAL_STRING("/events", s0->path);
    TEST_ASSERT_EQUAL_STRING("/metrics", s1->path);
}

void test_sse_alloc_path_truncated_to_max()
{

    char long_path[MAX_PATH_LEN + 16];
    long_path[0] = '/';
    for (int i = 1; i < MAX_PATH_LEN + 15; i++)
    {
        long_path[i] = 'x';
    }
    long_path[MAX_PATH_LEN + 15] = '\0';

    SseConn *sse = protocore_sse_alloc(0, long_path);
    TEST_ASSERT_NOT_NULL(sse);
    TEST_ASSERT_EQUAL(MAX_PATH_LEN - 1, (int)strlen(sse->path));
    TEST_ASSERT_EQUAL('\0', sse->path[MAX_PATH_LEN - 1]);
}

void test_sse_alloc_pool_full_returns_null()
{
    TEST_ASSERT_NOT_NULL(protocore_sse_alloc(0, "/a"));
    TEST_ASSERT_NOT_NULL(protocore_sse_alloc(1, "/b"));
    TEST_ASSERT_NULL(protocore_sse_alloc(2, "/c"));
}

void test_sse_alloc_sse_id_is_pool_index()
{

    SseConn *s0 = protocore_sse_alloc(0, "/a");
    TEST_ASSERT_EQUAL(0, (int)s0->protocore_sse_id);

    SseConn *s1 = protocore_sse_alloc(1, "/b");
    TEST_ASSERT_EQUAL(1, (int)s1->protocore_sse_id);
}

void test_sse_find_returns_correct_conn()
{
    SseConn *allocated = protocore_sse_alloc(0, "/events");
    SseConn *found = protocore_sse_find(0);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_PTR(allocated, found);
}

void test_sse_find_returns_null_when_empty()
{
    TEST_ASSERT_NULL(protocore_sse_find(0));
}

void test_sse_find_returns_null_for_different_slot()
{
    protocore_sse_alloc(0, "/events");
    TEST_ASSERT_NULL(protocore_sse_find(1));
}

void test_sse_find_after_both_slots_allocated()
{
    protocore_sse_alloc(0, "/a");
    protocore_sse_alloc(1, "/b");
    TEST_ASSERT_NOT_NULL(protocore_sse_find(0));
    TEST_ASSERT_NOT_NULL(protocore_sse_find(1));
}

void test_sse_find_checks_slot_id_not_sse_id()
{

    SseConn *sse = protocore_sse_alloc(3, "/x");
    TEST_ASSERT_NULL(protocore_sse_find(0));
    TEST_ASSERT_NOT_NULL(protocore_sse_find(3));
    TEST_ASSERT_EQUAL_PTR(sse, protocore_sse_find(3));
}

void test_sse_free_deactivates_slot()
{
    protocore_sse_alloc(0, "/events");
    protocore_sse_free(0);
    TEST_ASSERT_FALSE(protocore_sse_pool[0].active);
}

void test_sse_free_restores_sse_id()
{
    protocore_sse_alloc(0, "/events");
    protocore_sse_free(0);
    TEST_ASSERT_EQUAL(0, (int)protocore_sse_pool[0].protocore_sse_id);
}

void test_sse_free_makes_slot_findable_as_null()
{
    protocore_sse_alloc(0, "/events");
    protocore_sse_free(0);
    TEST_ASSERT_NULL(protocore_sse_find(0));
}

void test_sse_free_clears_path()
{
    protocore_sse_alloc(0, "/events");
    protocore_sse_free(0);
    TEST_ASSERT_EQUAL('\0', protocore_sse_pool[0].path[0]);
}

void test_sse_free_nop_on_unallocated()
{
    protocore_sse_free(2);

    TEST_ASSERT_FALSE(protocore_sse_pool[0].active);
    TEST_ASSERT_FALSE(protocore_sse_pool[1].active);
    TEST_PASS();
}

void test_sse_alloc_after_free_succeeds()
{
    protocore_sse_alloc(0, "/events");
    protocore_sse_free(0);
    SseConn *sse = protocore_sse_alloc(0, "/new");
    TEST_ASSERT_NOT_NULL(sse);
    TEST_ASSERT_TRUE(sse->active);
    TEST_ASSERT_EQUAL_STRING("/new", sse->path);
}

void test_sse_free_only_frees_matching_slot()
{
    protocore_sse_alloc(0, "/a");
    protocore_sse_alloc(1, "/b");
    protocore_sse_free(0);
    TEST_ASSERT_FALSE(protocore_sse_pool[0].active);
    TEST_ASSERT_TRUE(protocore_sse_pool[1].active);
    TEST_ASSERT_EQUAL_STRING("/b", protocore_sse_pool[1].path);
}

void test_sse_write_null_data_returns_false()
{
    SseConn *sse = protocore_sse_alloc(0, "/events");
    TEST_ASSERT_FALSE(protocore_sse_write(sse, NULL, NULL, NULL));
}

void test_sse_write_returns_false_when_conn_not_active()
{
    SseConn *sse = protocore_sse_alloc(0, "/events");
    conn_pool[0].state = CONN_FREE;
    TEST_ASSERT_FALSE(protocore_sse_write(sse, "hello", NULL, NULL));
}

void test_sse_write_returns_false_when_pcb_null()
{
    SseConn *sse = protocore_sse_alloc(0, "/events");
    conn_pool[0].pcb = NULL;
    TEST_ASSERT_FALSE(protocore_sse_write(sse, "data", NULL, NULL));
}

void test_sse_write_data_only_returns_true()
{
    SseConn *sse = protocore_sse_alloc(0, "/events");
    TEST_ASSERT_TRUE(protocore_sse_write(sse, "hello", NULL, NULL));
}

void test_sse_write_with_event_returns_true()
{
    SseConn *sse = protocore_sse_alloc(0, "/events");
    TEST_ASSERT_TRUE(protocore_sse_write(sse, "payload", "update", NULL));
}

void test_sse_write_with_id_returns_true()
{
    SseConn *sse = protocore_sse_alloc(0, "/events");
    TEST_ASSERT_TRUE(protocore_sse_write(sse, "payload", NULL, "42"));
}

void test_sse_write_with_all_fields_returns_true()
{
    SseConn *sse = protocore_sse_alloc(0, "/events");
    TEST_ASSERT_TRUE(protocore_sse_write(sse, "body", "status", "1"));
}

void test_sse_write_does_not_affect_other_slots()
{
    SseConn *s0 = protocore_sse_alloc(0, "/a");
    SseConn *s1 = protocore_sse_alloc(1, "/b");

    protocore_sse_write(s0, "msg", NULL, NULL);
    TEST_ASSERT_TRUE(s1->active);
    TEST_ASSERT_EQUAL_STRING("/b", s1->path);
    TEST_ASSERT_EQUAL(1, (int)s1->slot_id);
}

void test_http_conn_open_releases_stale_sse_binding()
{
    protocore_sse_alloc(0, "/events");
    TEST_ASSERT_NOT_NULL(protocore_sse_find(0));
    HttpConn.slot = 0;
    HttpConn.conn_open(HttpConn.internal);
    TEST_ASSERT_NULL(protocore_sse_find(0));
}

void test_http_conn_open_leaves_other_slot_sse_binding()
{
    protocore_sse_alloc(0, "/events");
    protocore_sse_alloc(1, "/metrics");
    HttpConn.slot = 0;
    HttpConn.conn_open(HttpConn.internal);
    TEST_ASSERT_NULL(protocore_sse_find(0));
    TEST_ASSERT_NOT_NULL(protocore_sse_find(1));
}

void test_sse_format_data_only()
{
    char buf[64];
    int n = protocore_sse_format(buf, sizeof(buf), "hello", NULL, NULL);
    TEST_ASSERT_EQUAL_STRING("data: hello\n\n", buf);
    TEST_ASSERT_EQUAL((int)strlen("data: hello\n\n"), n);
}

void test_sse_format_event_and_data()
{
    char buf[64];
    int n = protocore_sse_format(buf, sizeof(buf), "payload", "update", NULL);
    TEST_ASSERT_EQUAL_STRING("event: update\ndata: payload\n\n", buf);
    TEST_ASSERT_EQUAL((int)strlen("event: update\ndata: payload\n\n"), n);
}

void test_sse_format_id_and_data()
{
    char buf[64];
    int n = protocore_sse_format(buf, sizeof(buf), "payload", NULL, "42");
    TEST_ASSERT_EQUAL_STRING("id: 42\ndata: payload\n\n", buf);
    TEST_ASSERT_EQUAL((int)strlen("id: 42\ndata: payload\n\n"), n);
}

void test_sse_format_all_fields_ordering()
{

    char buf[64];
    int n = protocore_sse_format(buf, sizeof(buf), "body", "status", "1");
    TEST_ASSERT_EQUAL_STRING("event: status\nid: 1\ndata: body\n\n", buf);
    TEST_ASSERT_EQUAL((int)strlen("event: status\nid: 1\ndata: body\n\n"), n);
}

void test_sse_format_null_data_returns_zero()
{
    char buf[64];
    TEST_ASSERT_EQUAL(0, protocore_sse_format(buf, sizeof(buf), NULL, "x", "1"));
}

void test_sse_format_overflow_returns_zero()
{

    char buf[8];
    TEST_ASSERT_EQUAL(0, protocore_sse_format(buf, sizeof(buf), "a-long-payload-value", "an-event", "99"));
}

void test_sse_format_zero_size_returns_zero()
{
    char buf[8];
    TEST_ASSERT_EQUAL(0, protocore_sse_format(buf, 0, "data", NULL, NULL));
}

void test_sse_format_event_prefix_itself_overflows()
{
    char buf[5];
    TEST_ASSERT_EQUAL(0, protocore_sse_format(buf, sizeof(buf), "y", "x", NULL));
}

void test_sse_format_event_newline_overflows()
{
    char buf[10];
    TEST_ASSERT_EQUAL(0, protocore_sse_format(buf, sizeof(buf), "unused", "ab", NULL));
}

void test_sse_format_id_block_failure_arms()
{
    char a[4];
    TEST_ASSERT_EQUAL(0, protocore_sse_format(a, sizeof(a), "d", NULL, "z"));

    char b[7];
    TEST_ASSERT_EQUAL(0, protocore_sse_format(b, sizeof(b), "d", NULL, "XYZ"));

    char c[7];
    TEST_ASSERT_EQUAL(0, protocore_sse_format(c, sizeof(c), "d", NULL, "XY"));
}

void test_sse_format_data_block_failure_arms()
{
    char a[5];
    TEST_ASSERT_EQUAL(0, protocore_sse_format(a, sizeof(a), "abcdef", NULL, NULL));

    char b[10];
    TEST_ASSERT_EQUAL(0, protocore_sse_format(b, sizeof(b), "abcdef", NULL, NULL));

    char c[9];
    TEST_ASSERT_EQUAL(0, protocore_sse_format(c, sizeof(c), "ab", NULL, NULL));
}

void stress_sse_alloc_free_100_cycles()
{
    for (int i = 0; i < 100; i++)
    {
        SseConn *sse = protocore_sse_alloc(0, "/events");
        TEST_ASSERT_NOT_NULL_MESSAGE(sse, "alloc failed");
        TEST_ASSERT_TRUE_MESSAGE(sse->active, "not active");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("/events", sse->path, "path wrong");
        protocore_sse_free(0);
        TEST_ASSERT_FALSE_MESSAGE(protocore_sse_pool[0].active, "still active after free");
    }
}

void stress_sse_alloc_free_both_slots_alternating()
{
    for (int cycle = 0; cycle < 50; cycle++)
    {
        SseConn *s0 = protocore_sse_alloc(0, "/a");
        SseConn *s1 = protocore_sse_alloc(1, "/b");
        TEST_ASSERT_NOT_NULL(s0);
        TEST_ASSERT_NOT_NULL(s1);
        TEST_ASSERT_NULL(protocore_sse_alloc(2, "/c"));

        protocore_sse_free(1);
        SseConn *s1b = protocore_sse_alloc(1, "/new");
        TEST_ASSERT_NOT_NULL(s1b);
        TEST_ASSERT_EQUAL_STRING("/new", s1b->path);

        protocore_sse_free(0);
        protocore_sse_free(1);
    }
}

void stress_sse_write_100_calls()
{
    SseConn *sse = protocore_sse_alloc(0, "/events");
    for (int i = 0; i < 100; i++)
    {
        proto_bool ok = protocore_sse_write(sse, "data", "update", "1");
        TEST_ASSERT_TRUE_MESSAGE(ok, "write failed");
    }

    TEST_ASSERT_TRUE(sse->active);
    TEST_ASSERT_EQUAL(0, (int)sse->slot_id);
}

void stress_sse_find_with_full_pool()
{
    SseConn *s0 = protocore_sse_alloc(0, "/x");
    SseConn *s1 = protocore_sse_alloc(1, "/y");
    for (int i = 0; i < 50; i++)
    {
        TEST_ASSERT_EQUAL_PTR(s0, protocore_sse_find(0));
        TEST_ASSERT_EQUAL_PTR(s1, protocore_sse_find(1));
        TEST_ASSERT_NULL(protocore_sse_find(2));
        TEST_ASSERT_NULL(protocore_sse_find(3));
    }
}

void stress_sse_write_slot_isolation()
{
    SseConn *s0 = protocore_sse_alloc(0, "/events");
    SseConn *s1 = protocore_sse_alloc(1, "/metrics");

    for (int i = 0; i < 50; i++)
    {
        protocore_sse_write(s0, "event_data", "update", "123");
    }

    TEST_ASSERT_EQUAL_STRING("/metrics", s1->path);
    TEST_ASSERT_TRUE(s1->active);
    TEST_ASSERT_EQUAL(1, (int)s1->slot_id);
    TEST_ASSERT_EQUAL(1, (int)s1->protocore_sse_id);
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_sse_pool_size);
    RUN_TEST(test_sse_ids_match_indices_after_init);
    RUN_TEST(test_sse_all_inactive_after_init);
    RUN_TEST(test_sse_path_empty_after_init);

    RUN_TEST(test_sse_alloc_returns_non_null);
    RUN_TEST(test_sse_alloc_sets_active);
    RUN_TEST(test_sse_alloc_sets_slot_id);
    RUN_TEST(test_sse_alloc_stores_path);
    RUN_TEST(test_sse_alloc_stores_different_paths_per_slot);
    RUN_TEST(test_sse_alloc_path_truncated_to_max);
    RUN_TEST(test_sse_alloc_pool_full_returns_null);
    RUN_TEST(test_sse_alloc_sse_id_is_pool_index);

    RUN_TEST(test_sse_find_returns_correct_conn);
    RUN_TEST(test_sse_find_returns_null_when_empty);
    RUN_TEST(test_sse_find_returns_null_for_different_slot);
    RUN_TEST(test_sse_find_after_both_slots_allocated);
    RUN_TEST(test_sse_find_checks_slot_id_not_sse_id);

    RUN_TEST(test_sse_free_deactivates_slot);
    RUN_TEST(test_sse_free_restores_sse_id);
    RUN_TEST(test_sse_free_makes_slot_findable_as_null);
    RUN_TEST(test_sse_free_clears_path);
    RUN_TEST(test_sse_free_nop_on_unallocated);
    RUN_TEST(test_sse_alloc_after_free_succeeds);
    RUN_TEST(test_sse_free_only_frees_matching_slot);

    RUN_TEST(test_sse_write_null_data_returns_false);
    RUN_TEST(test_sse_write_returns_false_when_conn_not_active);
    RUN_TEST(test_sse_write_returns_false_when_pcb_null);
    RUN_TEST(test_sse_write_data_only_returns_true);
    RUN_TEST(test_sse_write_with_event_returns_true);
    RUN_TEST(test_sse_write_with_id_returns_true);
    RUN_TEST(test_sse_write_with_all_fields_returns_true);
    RUN_TEST(test_sse_write_does_not_affect_other_slots);

    RUN_TEST(test_http_conn_open_releases_stale_sse_binding);
    RUN_TEST(test_http_conn_open_leaves_other_slot_sse_binding);

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

    RUN_TEST(stress_sse_alloc_free_100_cycles);
    RUN_TEST(stress_sse_alloc_free_both_slots_alternating);
    RUN_TEST(stress_sse_write_100_calls);
    RUN_TEST(stress_sse_find_with_full_pool);
    RUN_TEST(stress_sse_write_slot_isolation);

    return UNITY_END();
}
