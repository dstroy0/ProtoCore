// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

// Move time the way a service pass does: the source advances, then ONE read stamps Clock.ms, which
// is what every reader in the library sees. Setting the source alone leaves Clock.ms where it was,
// so the binding would poll the same instant forever.
static void advance_to(uint32_t now)
{
    g_ms = now;
    Clock.millis(Clock.internal);
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
    Hotswap.core_init_args.c = &c;
    Hotswap.core_init_args.fail_threshold = 3;
    Hotswap.core_init_args.probe_interval_ms = 2000;
    Hotswap.core_init_args.now = 100000;
    Hotswap.core_init(protocore_hotswap_span());
    Clock.src.fn = test_clock;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
    Clock.millis(Clock.internal); // set_ms installs the source; this is what stamps Clock.ms from it
    g_mount_ok = PROTO_TRUE;
    g_present_ok = PROTO_TRUE;
    reset_counts();
}
void tearDown()
{
}

static void mount_it(uint32_t now)
{
    Hotswap.core_probe_args.c = &c;
    Hotswap.core_probe_args.present = PROTO_TRUE;
    Hotswap.core_probe_args.mounted = PROTO_TRUE;
    Hotswap.core_probe_args.now = now;
    Hotswap.core_probe(protocore_hotswap_span());
}

void test_starts_absent_not_ready()
{

    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(0, c.mounts);
    TEST_ASSERT_EQUAL_UINT32(0, c.faults);
}

void test_first_probe_is_due_immediately()
{

    Hotswap.core_due_args.c = &c;
    Hotswap.core_due_args.now = 100000;
    Hotswap.core_due(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
}

void test_first_probe_is_due_when_init_time_is_near_zero()
{

    Hotswap.core_init_args.c = &c;
    Hotswap.core_init_args.fail_threshold = 3;
    Hotswap.core_init_args.probe_interval_ms = 2000;
    Hotswap.core_init_args.now = 5;
    Hotswap.core_init(protocore_hotswap_span());
    Hotswap.core_due_args.c = &c;
    Hotswap.core_due_args.now = 5;
    Hotswap.core_due(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    Hotswap.core_due_args.c = &c;
    Hotswap.core_due_args.now = 6;
    Hotswap.core_due(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
}

void test_zero_threshold_is_clamped_to_one()
{
    Hotswap.core_init_args.c = &c;
    Hotswap.core_init_args.fail_threshold = 0;
    Hotswap.core_init_args.probe_interval_ms = 2000;
    Hotswap.core_init_args.now = 0;
    Hotswap.core_init(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_UINT8(1, c.fail_threshold);
    mount_it(0);

    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
}

void test_one_failure_does_not_fault_a_healthy_volume()
{
    mount_it(100000);
    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
    TEST_ASSERT_EQUAL_UINT8(1, c.fail_run);
}

void test_threshold_run_faults_and_counts()
{
    mount_it(100000);
    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(1, c.faults);
}

void test_a_success_resets_the_failure_run()
{
    mount_it(100000);
    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_TRUE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_UINT8(0, c.fail_run);

    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
}

void test_further_failures_while_faulted_are_ignored()
{
    mount_it(100000);
    for (int i = 0; i < 3; i++)
    {
        Hotswap.core_io_args.c = &c;
        Hotswap.core_io_args.ok = PROTO_FALSE;
        Hotswap.core_io(protocore_hotswap_span());
    }
    TEST_ASSERT_EQUAL_UINT32(1, c.faults);

    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_TRUE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    TEST_ASSERT_EQUAL_UINT32(1, c.faults);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
}

void test_io_while_absent_is_ignored()
{
    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(0, c.faults);
}

void test_fail_run_saturates_instead_of_wrapping()
{
    Hotswap.core_init_args.c = &c;
    Hotswap.core_init_args.fail_threshold = 255;
    Hotswap.core_init_args.probe_interval_ms = 2000;
    Hotswap.core_init_args.now = 0;
    Hotswap.core_init(protocore_hotswap_span());
    mount_it(0);
    for (int i = 0; i < 400; i++)
    {
        Hotswap.core_io_args.c = &c;
        Hotswap.core_io_args.ok = PROTO_FALSE;
        Hotswap.core_io(protocore_hotswap_span());
    }

    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
}

void test_fail_run_at_the_uint8_ceiling_does_not_wrap()
{

    Hotswap.core_init_args.c = &c;
    Hotswap.core_init_args.fail_threshold = 255;
    Hotswap.core_init_args.probe_interval_ms = 2000;
    Hotswap.core_init_args.now = 0;
    Hotswap.core_init(protocore_hotswap_span());
    mount_it(0);
    c.fail_run = 0xFF;
    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    TEST_ASSERT_EQUAL_UINT8(0xFF, c.fail_run);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
}

void test_no_probe_while_ready()
{
    mount_it(100000);
    Hotswap.core_due_args.c = &c;
    Hotswap.core_due_args.now = 100000 + 999999;
    Hotswap.core_due(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
}

void test_probe_is_rate_limited_while_absent()
{
    Hotswap.core_probe_args.c = &c;
    Hotswap.core_probe_args.present = PROTO_FALSE;
    Hotswap.core_probe_args.mounted = PROTO_FALSE;
    Hotswap.core_probe_args.now = 100000;
    Hotswap.core_probe(protocore_hotswap_span());
    Hotswap.core_due_args.c = &c;
    Hotswap.core_due_args.now = 100000 + 1999;
    Hotswap.core_due(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    Hotswap.core_due_args.c = &c;
    Hotswap.core_due_args.now = 100000 + 2000;
    Hotswap.core_due(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
}

void test_probe_pacing_is_wrapsafe_across_rollover()
{

    Hotswap.core_probe_args.c = &c;
    Hotswap.core_probe_args.present = PROTO_FALSE;
    Hotswap.core_probe_args.mounted = PROTO_FALSE;
    Hotswap.core_probe_args.now = 0xFFFFF000u;
    Hotswap.core_probe(protocore_hotswap_span());
    Hotswap.core_due_args.c = &c;
    Hotswap.core_due_args.now = 0xFFFFF000u + 1999;
    Hotswap.core_due(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    Hotswap.core_due_args.c = &c;
    Hotswap.core_due_args.now = 0xFFFFF000u + 2000;
    Hotswap.core_due(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
}

void test_present_but_unmountable_stays_absent()
{

    Hotswap.core_probe_args.c = &c;
    Hotswap.core_probe_args.present = PROTO_TRUE;
    Hotswap.core_probe_args.mounted = PROTO_FALSE;
    Hotswap.core_probe_args.now = 100000;
    Hotswap.core_probe(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(0, c.mounts);
}

void test_mount_counts_only_on_transition()
{
    Hotswap.core_probe_args.c = &c;
    Hotswap.core_probe_args.present = PROTO_TRUE;
    Hotswap.core_probe_args.mounted = PROTO_TRUE;
    Hotswap.core_probe_args.now = 100000;
    Hotswap.core_probe(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    TEST_ASSERT_EQUAL_UINT32(1, c.mounts);

    Hotswap.core_probe_args.c = &c;
    Hotswap.core_probe_args.present = PROTO_TRUE;
    Hotswap.core_probe_args.mounted = PROTO_TRUE;
    Hotswap.core_probe_args.now = 101000;
    Hotswap.core_probe(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    TEST_ASSERT_EQUAL_UINT32(1, c.mounts);
}

void test_full_removal_and_reinsertion_cycle()
{
    mount_it(100000);
    TEST_ASSERT_EQUAL_UINT32(1, c.mounts);

    for (int i = 0; i < 3; i++)
    {
        Hotswap.core_io_args.c = &c;
        Hotswap.core_io_args.ok = PROTO_FALSE;
        Hotswap.core_io(protocore_hotswap_span());
    }
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);

    Hotswap.core_due_args.c = &c;
    Hotswap.core_due_args.now = 102000;
    Hotswap.core_due(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    Hotswap.core_probe_args.c = &c;
    Hotswap.core_probe_args.present = PROTO_FALSE;
    Hotswap.core_probe_args.mounted = PROTO_FALSE;
    Hotswap.core_probe_args.now = 102000;
    Hotswap.core_probe(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)c.state);

    Hotswap.core_due_args.c = &c;
    Hotswap.core_due_args.now = 104000;
    Hotswap.core_due(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    Hotswap.core_probe_args.c = &c;
    Hotswap.core_probe_args.present = PROTO_TRUE;
    Hotswap.core_probe_args.mounted = PROTO_TRUE;
    Hotswap.core_probe_args.now = 104000;
    Hotswap.core_probe(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(2, c.mounts);
    TEST_ASSERT_EQUAL_UINT32(1, c.faults);
    TEST_ASSERT_EQUAL_UINT8(0, c.fail_run);

    Hotswap.core_io_args.c = &c;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
}

void test_faulted_volume_can_go_straight_back_to_ready()
{

    mount_it(100000);
    for (int i = 0; i < 3; i++)
    {
        Hotswap.core_io_args.c = &c;
        Hotswap.core_io_args.ok = PROTO_FALSE;
        Hotswap.core_io(protocore_hotswap_span());
    }
    Hotswap.core_probe_args.c = &c;
    Hotswap.core_probe_args.present = PROTO_TRUE;
    Hotswap.core_probe_args.mounted = PROTO_TRUE;
    Hotswap.core_probe_args.now = 102000;
    Hotswap.core_probe(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(2, c.mounts);
}

void test_null_core_is_not_a_crash()
{
    Hotswap.core_init_args.c = NULL;
    Hotswap.core_init_args.fail_threshold = 3;
    Hotswap.core_init_args.probe_interval_ms = 2000;
    Hotswap.core_init_args.now = 0;
    Hotswap.core_init(protocore_hotswap_span());
    Hotswap.core_io_args.c = NULL;
    Hotswap.core_io_args.ok = PROTO_FALSE;
    Hotswap.core_io(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    Hotswap.core_due_args.c = NULL;
    Hotswap.core_due_args.now = 0;
    Hotswap.core_due(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    Hotswap.core_probe_args.c = NULL;
    Hotswap.core_probe_args.present = PROTO_TRUE;
    Hotswap.core_probe_args.mounted = PROTO_TRUE;
    Hotswap.core_probe_args.now = 0;
    Hotswap.core_probe(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
}

void test_state_names()
{
    Hotswap.state_name_args.s = STORAGE_STATE_ABSENT;
    Hotswap.state_name(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_STRING("absent", Hotswap.text);
    Hotswap.state_name_args.s = STORAGE_STATE_READY;
    Hotswap.state_name(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_STRING("ready", Hotswap.text);
    Hotswap.state_name_args.s = STORAGE_STATE_FAULTED;
    Hotswap.state_name(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_STRING("faulted", Hotswap.text);
}

void test_json_and_overflow_is_fail_closed()
{
    char buf[64];
    Hotswap.json_args.out = buf;
    Hotswap.json_args.cap = sizeof(buf);
    Hotswap.json(protocore_hotswap_span());
    size_t n = Hotswap.n;
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"storage\":\"absent\",\"mounts\":0,\"faults\":0}", buf);

    char tiny[8];
    Hotswap.json_args.out = tiny;
    Hotswap.json_args.cap = sizeof(tiny);
    Hotswap.json(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_UINT32(0, Hotswap.n);
    TEST_ASSERT_EQUAL_STRING("", tiny);
    Hotswap.json_args.out = NULL;
    Hotswap.json_args.cap = 16;
    Hotswap.json(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_UINT32(0, Hotswap.n);

    Hotswap.json_args.out = buf;
    Hotswap.json_args.cap = 0;
    Hotswap.json(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_UINT32(0, Hotswap.n);
}

void test_binding_poll_before_begin_does_nothing()
{

    Hotswap.poll_at_args.now = 500000;
    Hotswap.poll_at(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT(0, g_mount_calls);
    TEST_ASSERT_EQUAL_INT(0, g_present_calls);
    Hotswap.state(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)Hotswap.value);
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
}

static void bind_and_mount(uint32_t now)
{
    g_present_ok = PROTO_TRUE;
    g_mount_ok = PROTO_TRUE;
    advance_to(now);
    Hotswap.begin_args.mount = fake_mount;
    Hotswap.begin_args.unmount = fake_unmount;
    Hotswap.begin_args.present = fake_present;
    Hotswap.begin_args.ctx = &g_ctx_token;
    Hotswap.begin(protocore_hotswap_span());
    Hotswap.set_event_cb_args.cb = fake_event;
    Hotswap.set_event_cb(protocore_hotswap_span());
    Hotswap.poll_at_args.now = now;
    Hotswap.poll_at(protocore_hotswap_span());
    reset_counts();
}

void test_binding_mounts_on_the_first_poll_and_notifies()
{

    g_present_ok = PROTO_TRUE;
    g_mount_ok = PROTO_TRUE;
    advance_to(10000);
    Hotswap.begin_args.mount = fake_mount;
    Hotswap.begin_args.unmount = fake_unmount;
    Hotswap.begin_args.present = fake_present;
    Hotswap.begin_args.ctx = &g_ctx_token;
    Hotswap.begin(protocore_hotswap_span());
    Hotswap.set_event_cb_args.cb = fake_event;
    Hotswap.set_event_cb(protocore_hotswap_span());
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    reset_counts();

    Hotswap.poll_at_args.now = 10000;
    Hotswap.poll_at(protocore_hotswap_span());
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    Hotswap.state(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)Hotswap.value);
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
    Hotswap.poll_at_args.now = 20000 + 999999;
    Hotswap.poll_at(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT(0, g_present_calls);
    TEST_ASSERT_EQUAL_INT(0, g_mount_calls);
    TEST_ASSERT_EQUAL_INT(0, g_event_calls);
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
}

void test_binding_io_fault_unmounts_immediately_and_notifies()
{

    bind_and_mount(30000);
    Hotswap.io_args.ok = PROTO_FALSE;
    Hotswap.io(protocore_hotswap_span());
    Hotswap.io_args.ok = PROTO_FALSE;
    Hotswap.io(protocore_hotswap_span());
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT(0, g_unmount_calls);

    Hotswap.io_args.ok = PROTO_FALSE;
    Hotswap.io(protocore_hotswap_span());
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    Hotswap.state(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)Hotswap.value);
    TEST_ASSERT_EQUAL_INT(1, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)g_event_from);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)g_event_to);

    Hotswap.io_args.ok = PROTO_FALSE;
    Hotswap.io(protocore_hotswap_span());
    Hotswap.io_args.ok = PROTO_TRUE;
    Hotswap.io(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT(1, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);
}

void test_binding_drops_a_faulted_mount_before_retrying()
{

    bind_and_mount(50000);
    for (int i = 0; i < 3; i++)
    {
        Hotswap.io_args.ok = PROTO_FALSE;
        Hotswap.io(protocore_hotswap_span());
    }
    TEST_ASSERT_EQUAL_INT(1, g_unmount_calls);
    Hotswap.state(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)Hotswap.value);

    Hotswap.poll_at_args.now = 52000;
    Hotswap.poll_at(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT(2, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_mount_calls);
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)g_event_from);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)g_event_to);
}

void test_binding_faults_and_retries_without_an_unmount_callback()
{

    g_present_ok = PROTO_TRUE;
    g_mount_ok = PROTO_TRUE;
    advance_to(40000);
    Hotswap.begin_args.mount = fake_mount;
    Hotswap.begin_args.unmount = NULL;
    Hotswap.begin_args.present = fake_present;
    Hotswap.begin_args.ctx = &g_ctx_token;
    Hotswap.begin(protocore_hotswap_span());
    Hotswap.set_event_cb_args.cb = fake_event;
    Hotswap.set_event_cb(protocore_hotswap_span());
    Hotswap.poll_at_args.now = 40000;
    Hotswap.poll_at(protocore_hotswap_span());
    reset_counts();
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);

    for (int i = 0; i < 3; i++)
    {
        Hotswap.io_args.ok = PROTO_FALSE;
        Hotswap.io(protocore_hotswap_span());
    }
    Hotswap.state(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)Hotswap.value);
    TEST_ASSERT_EQUAL_INT(0, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);

    g_present_ok = PROTO_FALSE;
    Hotswap.poll_at_args.now = 42000;
    Hotswap.poll_at(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT(0, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);
    TEST_ASSERT_EQUAL_INT(0, g_mount_calls);
    Hotswap.state(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)Hotswap.value);
    TEST_ASSERT_EQUAL_INT(2, g_event_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)g_event_to);
}

void test_binding_without_card_detect_lets_the_mount_decide()
{

    advance_to(60000);
    g_mount_ok = PROTO_FALSE;
    Hotswap.begin_args.mount = fake_mount;
    Hotswap.begin_args.unmount = fake_unmount;
    Hotswap.begin_args.present = NULL;
    Hotswap.begin_args.ctx = &g_ctx_token;
    Hotswap.begin(protocore_hotswap_span());
    Hotswap.set_event_cb_args.cb = fake_event;
    Hotswap.set_event_cb(protocore_hotswap_span());
    reset_counts();

    Hotswap.poll_at_args.now = 60000;
    Hotswap.poll_at(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT(0, g_present_calls);
    TEST_ASSERT_EQUAL_INT(1, g_mount_calls);
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT(0, g_event_calls);

    g_mount_ok = PROTO_TRUE;
    Hotswap.poll_at_args.now = 62000;
    Hotswap.poll_at(protocore_hotswap_span());
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)g_event_to);
}

void test_binding_without_a_mount_callback_never_becomes_ready()
{

    advance_to(70000);
    g_present_ok = PROTO_TRUE;
    Hotswap.begin_args.mount = NULL;
    Hotswap.begin_args.unmount = fake_unmount;
    Hotswap.begin_args.present = fake_present;
    Hotswap.begin_args.ctx = &g_ctx_token;
    Hotswap.begin(protocore_hotswap_span());
    Hotswap.set_event_cb_args.cb = fake_event;
    Hotswap.set_event_cb(protocore_hotswap_span());
    reset_counts();

    Hotswap.poll_at_args.now = 70000;
    Hotswap.poll_at(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);
    TEST_ASSERT_EQUAL_INT(0, g_mount_calls);
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);
    Hotswap.state(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)Hotswap.value);
}

void test_binding_event_callback_is_optional()
{

    Hotswap.set_event_cb_args.cb = NULL;
    Hotswap.set_event_cb(protocore_hotswap_span());
    advance_to(80000);
    g_present_ok = PROTO_TRUE;
    g_mount_ok = PROTO_TRUE;
    Hotswap.begin_args.mount = fake_mount;
    Hotswap.begin_args.unmount = fake_unmount;
    Hotswap.begin_args.present = fake_present;
    Hotswap.begin_args.ctx = &g_ctx_token;
    Hotswap.begin(protocore_hotswap_span());
    reset_counts();

    Hotswap.poll_at_args.now = 80000;
    Hotswap.poll_at(protocore_hotswap_span());
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
    TEST_ASSERT_EQUAL_INT(0, g_event_calls);
    for (int i = 0; i < 3; i++)
    {
        Hotswap.io_args.ok = PROTO_FALSE;
        Hotswap.io(protocore_hotswap_span());
    }
    Hotswap.state(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)Hotswap.value);
    TEST_ASSERT_EQUAL_INT(1, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(0, g_event_calls);
}

void test_binding_poll_reads_the_library_clock()
{

    advance_to(90000);
    g_present_ok = PROTO_FALSE;
    g_mount_ok = PROTO_TRUE;
    Hotswap.begin_args.mount = fake_mount;
    Hotswap.begin_args.unmount = fake_unmount;
    Hotswap.begin_args.present = fake_present;
    Hotswap.begin_args.ctx = &g_ctx_token;
    Hotswap.begin(protocore_hotswap_span());
    Hotswap.set_event_cb_args.cb = fake_event;
    Hotswap.set_event_cb(protocore_hotswap_span());
    reset_counts();

    Hotswap.poll(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_FALSE(Hotswap.ok);

    advance_to(91000);
    Hotswap.poll(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);

    advance_to(92000);
    g_present_ok = PROTO_TRUE;
    Hotswap.poll(protocore_hotswap_span());
    TEST_ASSERT_EQUAL_INT(2, g_present_calls);
    Hotswap.ready(protocore_hotswap_span());
    TEST_ASSERT_TRUE(Hotswap.ok);
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
