// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/clock/clock.h"
#include "server/storage/hotswap/hotswap.h"
#include <stdio.h>

#include <unity.h>

static HotswapCore c;

static uint32_t g_ms = 0;
static uint32_t test_clock()
{
    return g_ms;
}

static int g_mount_calls = 0;
static int g_unmount_calls = 0;
static int g_present_calls = 0;
static int g_event_calls = 0;
static proto_bool g_mount_ok = PROTO_TRUE;
static proto_bool g_present_ok = PROTO_TRUE;
static StorageState g_event_from = STORAGE_STATE_ABSENT;
static StorageState g_event_to = STORAGE_STATE_ABSENT;
static void *g_seen_ctx = NULL;
static int g_ctx_token = 0;

static proto_bool fake_mount(void *ctx)
{
    g_mount_calls++;
    g_seen_ctx = ctx;
    return g_mount_ok;
}
static void fake_unmount(void *ctx)
{
    g_unmount_calls++;
    g_seen_ctx = ctx;
}
static proto_bool fake_present(void *ctx)
{
    g_present_calls++;
    g_seen_ctx = ctx;
    return g_present_ok;
}
static void fake_event(StorageState from, StorageState to, void *ctx)
{
    g_event_calls++;
    g_event_from = from;
    g_event_to = to;
    g_seen_ctx = ctx;
}

static void reset_counts()
{
    g_mount_calls = 0;
    g_unmount_calls = 0;
    g_present_calls = 0;
    g_event_calls = 0;
    g_seen_ctx = NULL;
}

void setUp()
{
    protocore_hotswap_core_init(&c, 3, 2000, 100000);
    protocore_set_clock(test_clock, 1000);
    g_mount_ok = PROTO_TRUE;
    g_present_ok = PROTO_TRUE;
    reset_counts();
}
void tearDown()
{
}

static void mount_it(uint32_t now)
{
    protocore_hotswap_core_probe(&c, PROTO_TRUE, PROTO_TRUE, now);
}

void test_starts_absent_not_ready()
{

    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(0, c.mounts);
    TEST_ASSERT_EQUAL_UINT32(0, c.faults);
}

void test_first_probe_is_due_immediately()
{

    TEST_ASSERT_TRUE(protocore_hotswap_core_due(&c, 100000));
}

void test_first_probe_is_due_when_init_time_is_near_zero()
{

    protocore_hotswap_core_init(&c, 3, 2000, 5);
    TEST_ASSERT_TRUE(protocore_hotswap_core_due(&c, 5));
    TEST_ASSERT_TRUE(protocore_hotswap_core_due(&c, 6));
}

void test_zero_threshold_is_clamped_to_one()
{
    protocore_hotswap_core_init(&c, 0, 2000, 0);
    TEST_ASSERT_EQUAL_UINT8(1, c.fail_threshold);
    mount_it(0);

    TEST_ASSERT_TRUE(protocore_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
}

void test_one_failure_does_not_fault_a_healthy_volume()
{
    mount_it(100000);
    TEST_ASSERT_FALSE(protocore_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
    TEST_ASSERT_EQUAL_UINT8(1, c.fail_run);
}

void test_threshold_run_faults_and_counts()
{
    mount_it(100000);
    TEST_ASSERT_FALSE(protocore_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_FALSE(protocore_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_TRUE(protocore_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(1, c.faults);
}

void test_a_success_resets_the_failure_run()
{
    mount_it(100000);
    protocore_hotswap_core_io(&c, PROTO_FALSE);
    protocore_hotswap_core_io(&c, PROTO_FALSE);
    protocore_hotswap_core_io(&c, PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT8(0, c.fail_run);

    TEST_ASSERT_FALSE(protocore_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_FALSE(protocore_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
}

void test_further_failures_while_faulted_are_ignored()
{
    mount_it(100000);
    for (int i = 0; i < 3; i++)
    {
        protocore_hotswap_core_io(&c, PROTO_FALSE);
    }
    TEST_ASSERT_EQUAL_UINT32(1, c.faults);

    TEST_ASSERT_FALSE(protocore_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_FALSE(protocore_hotswap_core_io(&c, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(1, c.faults);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
}

void test_io_while_absent_is_ignored()
{
    TEST_ASSERT_FALSE(protocore_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(0, c.faults);
}

void test_fail_run_saturates_instead_of_wrapping()
{
    protocore_hotswap_core_init(&c, 255, 2000, 0);
    mount_it(0);
    for (int i = 0; i < 400; i++)
    {
        protocore_hotswap_core_io(&c, PROTO_FALSE);
    }

    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
}

void test_fail_run_at_the_uint8_ceiling_does_not_wrap()
{

    protocore_hotswap_core_init(&c, 255, 2000, 0);
    mount_it(0);
    c.fail_run = 0xFF;
    TEST_ASSERT_TRUE(protocore_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT8(0xFF, c.fail_run);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
}

void test_no_probe_while_ready()
{
    mount_it(100000);
    TEST_ASSERT_FALSE(protocore_hotswap_core_due(&c, 100000 + 999999));
}

void test_probe_is_rate_limited_while_absent()
{
    protocore_hotswap_core_probe(&c, PROTO_FALSE, PROTO_FALSE, 100000);
    TEST_ASSERT_FALSE(protocore_hotswap_core_due(&c, 100000 + 1999));
    TEST_ASSERT_TRUE(protocore_hotswap_core_due(&c, 100000 + 2000));
}

void test_probe_pacing_is_wrapsafe_across_rollover()
{

    protocore_hotswap_core_probe(&c, PROTO_FALSE, PROTO_FALSE, 0xFFFFF000u);
    TEST_ASSERT_FALSE(protocore_hotswap_core_due(&c, 0xFFFFF000u + 1999));
    TEST_ASSERT_TRUE(protocore_hotswap_core_due(&c, 0xFFFFF000u + 2000));
}

void test_present_but_unmountable_stays_absent()
{

    TEST_ASSERT_FALSE(protocore_hotswap_core_probe(&c, PROTO_TRUE, PROTO_FALSE, 100000));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(0, c.mounts);
}

void test_mount_counts_only_on_transition()
{
    TEST_ASSERT_TRUE(protocore_hotswap_core_probe(&c, PROTO_TRUE, PROTO_TRUE, 100000));
    TEST_ASSERT_EQUAL_UINT32(1, c.mounts);

    TEST_ASSERT_FALSE(protocore_hotswap_core_probe(&c, PROTO_TRUE, PROTO_TRUE, 101000));
    TEST_ASSERT_EQUAL_UINT32(1, c.mounts);
}

void test_full_removal_and_reinsertion_cycle()
{
    mount_it(100000);
    TEST_ASSERT_EQUAL_UINT32(1, c.mounts);

    for (int i = 0; i < 3; i++)
    {
        protocore_hotswap_core_io(&c, PROTO_FALSE);
    }
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);

    TEST_ASSERT_TRUE(protocore_hotswap_core_due(&c, 102000));
    protocore_hotswap_core_probe(&c, PROTO_FALSE, PROTO_FALSE, 102000);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)c.state);

    TEST_ASSERT_TRUE(protocore_hotswap_core_due(&c, 104000));
    TEST_ASSERT_TRUE(protocore_hotswap_core_probe(&c, PROTO_TRUE, PROTO_TRUE, 104000));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(2, c.mounts);
    TEST_ASSERT_EQUAL_UINT32(1, c.faults);
    TEST_ASSERT_EQUAL_UINT8(0, c.fail_run);

    TEST_ASSERT_FALSE(protocore_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
}

void test_faulted_volume_can_go_straight_back_to_ready()
{

    mount_it(100000);
    for (int i = 0; i < 3; i++)
    {
        protocore_hotswap_core_io(&c, PROTO_FALSE);
    }
    TEST_ASSERT_TRUE(protocore_hotswap_core_probe(&c, PROTO_TRUE, PROTO_TRUE, 102000));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(2, c.mounts);
}

void test_null_core_is_not_a_crash()
{
    protocore_hotswap_core_init(NULL, 3, 2000, 0);
    TEST_ASSERT_FALSE(protocore_hotswap_core_io(NULL, PROTO_FALSE));
    TEST_ASSERT_FALSE(protocore_hotswap_core_due(NULL, 0));
    TEST_ASSERT_FALSE(protocore_hotswap_core_probe(NULL, PROTO_TRUE, PROTO_TRUE, 0));
}

void test_state_names()
{
    TEST_ASSERT_EQUAL_STRING("absent", protocore_hotswap_state_name(STORAGE_STATE_ABSENT));
    TEST_ASSERT_EQUAL_STRING("ready", protocore_hotswap_state_name(STORAGE_STATE_READY));
    TEST_ASSERT_EQUAL_STRING("faulted", protocore_hotswap_state_name(STORAGE_STATE_FAULTED));
}

void test_json_and_overflow_is_fail_closed()
{
    char buf[64];
    size_t n = protocore_hotswap_json(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"storage\":\"absent\",\"mounts\":0,\"faults\":0}", buf);

    char tiny[8];
    TEST_ASSERT_EQUAL_UINT32(0, protocore_hotswap_json(tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("", tiny);
    TEST_ASSERT_EQUAL_UINT32(0, protocore_hotswap_json(NULL, 16));

    TEST_ASSERT_EQUAL_UINT32(0, protocore_hotswap_json(buf, 0));
}

void test_binding_poll_before_begin_does_nothing()
{

    protocore_hotswap_poll_at(500000);
    TEST_ASSERT_EQUAL_INT(0, g_mount_calls);
    TEST_ASSERT_EQUAL_INT(0, g_present_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)protocore_hotswap_state());
    TEST_ASSERT_FALSE(protocore_hotswap_ready());
}

static void bind_and_mount(uint32_t now)
{
    g_present_ok = PROTO_TRUE;
    g_mount_ok = PROTO_TRUE;
    g_ms = now;
    protocore_hotswap_begin(fake_mount, fake_unmount, fake_present, &g_ctx_token);
    protocore_hotswap_set_event_cb(fake_event);
    protocore_hotswap_poll_at(now);
    reset_counts();
}

void test_binding_mounts_on_the_first_poll_and_notifies()
{

    g_present_ok = PROTO_TRUE;
    g_mount_ok = PROTO_TRUE;
    g_ms = 10000;
    protocore_hotswap_begin(fake_mount, fake_unmount, fake_present, &g_ctx_token);
    protocore_hotswap_set_event_cb(fake_event);
    TEST_ASSERT_FALSE(protocore_hotswap_ready());
    reset_counts();

    protocore_hotswap_poll_at(10000);
    TEST_ASSERT_TRUE(protocore_hotswap_ready());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)protocore_hotswap_state());
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);
    TEST_ASSERT_EQUAL_INT(1, g_mount_calls);
    TEST_ASSERT_EQUAL_INT(0, g_unmount_calls);
    TEST_ASSERT_EQUAL_PTR(&g_ctx_token, g_seen_ctx);
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)g_event_from);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)g_event_to);
}

void test_binding_ready_volume_is_never_reprobed()
{

    bind_and_mount(20000);
    protocore_hotswap_poll_at(20000 + 999999);
    TEST_ASSERT_EQUAL_INT(0, g_present_calls);
    TEST_ASSERT_EQUAL_INT(0, g_mount_calls);
    TEST_ASSERT_EQUAL_INT(0, g_event_calls);
    TEST_ASSERT_TRUE(protocore_hotswap_ready());
}

void test_binding_io_fault_unmounts_immediately_and_notifies()
{

    bind_and_mount(30000);
    protocore_hotswap_io(PROTO_FALSE);
    protocore_hotswap_io(PROTO_FALSE);
    TEST_ASSERT_TRUE(protocore_hotswap_ready());
    TEST_ASSERT_EQUAL_INT(0, g_unmount_calls);

    protocore_hotswap_io(PROTO_FALSE);
    TEST_ASSERT_FALSE(protocore_hotswap_ready());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)protocore_hotswap_state());
    TEST_ASSERT_EQUAL_INT(1, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)g_event_from);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)g_event_to);

    protocore_hotswap_io(PROTO_FALSE);
    protocore_hotswap_io(PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(1, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);
}

void test_binding_drops_a_faulted_mount_before_retrying()
{

    bind_and_mount(50000);
    for (int i = 0; i < 3; i++)
    {
        protocore_hotswap_io(PROTO_FALSE);
    }
    TEST_ASSERT_EQUAL_INT(1, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)protocore_hotswap_state());

    protocore_hotswap_poll_at(52000);
    TEST_ASSERT_EQUAL_INT(2, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_mount_calls);
    TEST_ASSERT_TRUE(protocore_hotswap_ready());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)g_event_from);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)g_event_to);
}

void test_binding_faults_and_retries_without_an_unmount_callback()
{

    g_present_ok = PROTO_TRUE;
    g_mount_ok = PROTO_TRUE;
    g_ms = 40000;
    protocore_hotswap_begin(fake_mount, NULL, fake_present, &g_ctx_token);
    protocore_hotswap_set_event_cb(fake_event);
    protocore_hotswap_poll_at(40000);
    reset_counts();
    TEST_ASSERT_TRUE(protocore_hotswap_ready());

    for (int i = 0; i < 3; i++)
    {
        protocore_hotswap_io(PROTO_FALSE);
    }
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)protocore_hotswap_state());
    TEST_ASSERT_EQUAL_INT(0, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);

    g_present_ok = PROTO_FALSE;
    protocore_hotswap_poll_at(42000);
    TEST_ASSERT_EQUAL_INT(0, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);
    TEST_ASSERT_EQUAL_INT(0, g_mount_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)protocore_hotswap_state());
    TEST_ASSERT_EQUAL_INT(2, g_event_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)g_event_to);
}

void test_binding_without_card_detect_lets_the_mount_decide()
{

    g_ms = 60000;
    g_mount_ok = PROTO_FALSE;
    protocore_hotswap_begin(fake_mount, fake_unmount, NULL, &g_ctx_token);
    protocore_hotswap_set_event_cb(fake_event);
    reset_counts();

    protocore_hotswap_poll_at(60000);
    TEST_ASSERT_EQUAL_INT(0, g_present_calls);
    TEST_ASSERT_EQUAL_INT(1, g_mount_calls);
    TEST_ASSERT_FALSE(protocore_hotswap_ready());
    TEST_ASSERT_EQUAL_INT(0, g_event_calls);

    g_mount_ok = PROTO_TRUE;
    protocore_hotswap_poll_at(62000);
    TEST_ASSERT_TRUE(protocore_hotswap_ready());
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)g_event_to);
}

void test_binding_without_a_mount_callback_never_becomes_ready()
{

    g_ms = 70000;
    g_present_ok = PROTO_TRUE;
    protocore_hotswap_begin(NULL, fake_unmount, fake_present, &g_ctx_token);
    protocore_hotswap_set_event_cb(fake_event);
    reset_counts();

    protocore_hotswap_poll_at(70000);
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);
    TEST_ASSERT_EQUAL_INT(0, g_mount_calls);
    TEST_ASSERT_FALSE(protocore_hotswap_ready());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)protocore_hotswap_state());
}

void test_binding_event_callback_is_optional()
{

    protocore_hotswap_set_event_cb(NULL);
    g_ms = 80000;
    g_present_ok = PROTO_TRUE;
    g_mount_ok = PROTO_TRUE;
    protocore_hotswap_begin(fake_mount, fake_unmount, fake_present, &g_ctx_token);
    reset_counts();

    protocore_hotswap_poll_at(80000);
    TEST_ASSERT_TRUE(protocore_hotswap_ready());
    TEST_ASSERT_EQUAL_INT(0, g_event_calls);
    for (int i = 0; i < 3; i++)
    {
        protocore_hotswap_io(PROTO_FALSE);
    }
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)protocore_hotswap_state());
    TEST_ASSERT_EQUAL_INT(1, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(0, g_event_calls);
}

void test_binding_poll_reads_the_library_clock()
{

    g_ms = 90000;
    g_present_ok = PROTO_FALSE;
    g_mount_ok = PROTO_TRUE;
    protocore_hotswap_begin(fake_mount, fake_unmount, fake_present, &g_ctx_token);
    protocore_hotswap_set_event_cb(fake_event);
    reset_counts();

    protocore_hotswap_poll();
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);
    TEST_ASSERT_FALSE(protocore_hotswap_ready());

    g_ms = 91000;
    protocore_hotswap_poll();
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);

    g_ms = 92000;
    g_present_ok = PROTO_TRUE;
    protocore_hotswap_poll();
    TEST_ASSERT_EQUAL_INT(2, g_present_calls);
    TEST_ASSERT_TRUE(protocore_hotswap_ready());
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_absent_not_ready);
    RUN_TEST(test_first_probe_is_due_immediately);
    RUN_TEST(test_first_probe_is_due_when_init_time_is_near_zero);
    RUN_TEST(test_zero_threshold_is_clamped_to_one);
    RUN_TEST(test_one_failure_does_not_fault_a_healthy_volume);
    RUN_TEST(test_threshold_run_faults_and_counts);
    RUN_TEST(test_a_success_resets_the_failure_run);
    RUN_TEST(test_further_failures_while_faulted_are_ignored);
    RUN_TEST(test_io_while_absent_is_ignored);
    RUN_TEST(test_fail_run_saturates_instead_of_wrapping);
    RUN_TEST(test_fail_run_at_the_uint8_ceiling_does_not_wrap);
    RUN_TEST(test_no_probe_while_ready);
    RUN_TEST(test_probe_is_rate_limited_while_absent);
    RUN_TEST(test_probe_pacing_is_wrapsafe_across_rollover);
    RUN_TEST(test_present_but_unmountable_stays_absent);
    RUN_TEST(test_mount_counts_only_on_transition);
    RUN_TEST(test_full_removal_and_reinsertion_cycle);
    RUN_TEST(test_faulted_volume_can_go_straight_back_to_ready);
    RUN_TEST(test_null_core_is_not_a_crash);
    RUN_TEST(test_state_names);
    RUN_TEST(test_json_and_overflow_is_fail_closed);

    RUN_TEST(test_binding_poll_before_begin_does_nothing);
    RUN_TEST(test_binding_mounts_on_the_first_poll_and_notifies);
    RUN_TEST(test_binding_ready_volume_is_never_reprobed);
    RUN_TEST(test_binding_io_fault_unmounts_immediately_and_notifies);
    RUN_TEST(test_binding_drops_a_faulted_mount_before_retrying);
    RUN_TEST(test_binding_faults_and_retries_without_an_unmount_callback);
    RUN_TEST(test_binding_without_card_detect_lets_the_mount_decide);
    RUN_TEST(test_binding_without_a_mount_callback_never_becomes_ready);
    RUN_TEST(test_binding_event_callback_is_optional);
    RUN_TEST(test_binding_poll_reads_the_library_clock);
    return UNITY_END();
}
