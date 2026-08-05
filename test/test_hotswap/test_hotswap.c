// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the removable-storage state machine (services/storage/hotswap): the fault threshold and
// what resets it, probe pacing (including across a millis() rollover), present-but-unmountable,
// the removal/reinsertion cycle, and that the whole thing is fail-closed. Plus the owned binding -
// how poll()/io() drive the app's mount / unmount / card-detect callbacks and the state-change event.

#include "server/clock/clock.h"
#include "services/storage/hotswap/hotswap.h"
#include <stdio.h>

#include <unity.h>

static HotswapCore c;

// Host clock seam so pc_hotswap_begin()/poll() are deterministic.
static uint32_t g_ms = 0;
static uint32_t test_clock()
{
    return g_ms;
}

// --- fake device binding ---------------------------------------------------

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
    pc_hotswap_core_init(&c, 3, 2000, 100000);
    pc_set_clock(test_clock, 1000);
    g_mount_ok = PROTO_TRUE;
    g_present_ok = PROTO_TRUE;
    reset_counts();
}
void tearDown()
{
}

// Bring the volume up so a test can start from a healthy mount.
static void mount_it(uint32_t now)
{
    pc_hotswap_core_probe(&c, PROTO_TRUE, PROTO_TRUE, now);
}

// --- initial state --------------------------------------------------------

void test_starts_absent_not_ready()
{
    // Starting STORAGE_STATE_READY would let a caller write before anything was ever mounted.
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(0, c.mounts);
    TEST_ASSERT_EQUAL_UINT32(0, c.faults);
}

void test_first_probe_is_due_immediately()
{
    // Back-dated last_probe: a card already present at boot must mount now, not one interval later.
    TEST_ASSERT_TRUE(pc_hotswap_core_due(&c, 100000));
}

void test_first_probe_is_due_when_init_time_is_near_zero()
{
    // Real case: begin() runs a few ms after boot, so `now - probe_interval` underflows past zero.
    // Unsigned arithmetic has to carry that correctly or a device would refuse to mount its card
    // for the first ~49 days of uptime.
    pc_hotswap_core_init(&c, 3, 2000, 5);
    TEST_ASSERT_TRUE(pc_hotswap_core_due(&c, 5));
    TEST_ASSERT_TRUE(pc_hotswap_core_due(&c, 6));
}

void test_zero_threshold_is_clamped_to_one()
{
    pc_hotswap_core_init(&c, 0, 2000, 0);
    TEST_ASSERT_EQUAL_UINT8(1, c.fail_threshold);
    mount_it(0);
    // With an unclamped 0 the volume would fault before any failure was reported.
    TEST_ASSERT_TRUE(pc_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
}

// --- the fault threshold --------------------------------------------------

void test_one_failure_does_not_fault_a_healthy_volume()
{
    mount_it(100000);
    TEST_ASSERT_FALSE(pc_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
    TEST_ASSERT_EQUAL_UINT8(1, c.fail_run);
}

void test_threshold_run_faults_and_counts()
{
    mount_it(100000);
    TEST_ASSERT_FALSE(pc_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_FALSE(pc_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_TRUE(pc_hotswap_core_io(&c, PROTO_FALSE)); // 3rd == threshold -> state change
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(1, c.faults);
}

void test_a_success_resets_the_failure_run()
{
    mount_it(100000);
    pc_hotswap_core_io(&c, PROTO_FALSE);
    pc_hotswap_core_io(&c, PROTO_FALSE);
    pc_hotswap_core_io(&c, PROTO_TRUE); // proves the medium is still there
    TEST_ASSERT_EQUAL_UINT8(0, c.fail_run);
    // So intermittent noise never accumulates into a false removal.
    TEST_ASSERT_FALSE(pc_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_FALSE(pc_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
}

void test_further_failures_while_faulted_are_ignored()
{
    mount_it(100000);
    for (int i = 0; i < 3; i++)
    {
        pc_hotswap_core_io(&c, PROTO_FALSE);
    }
    TEST_ASSERT_EQUAL_UINT32(1, c.faults);
    // A caller honoring ready() stops here; a stray report must not re-fire the transition.
    TEST_ASSERT_FALSE(pc_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_FALSE(pc_hotswap_core_io(&c, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(1, c.faults);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
}

void test_io_while_absent_is_ignored()
{
    TEST_ASSERT_FALSE(pc_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(0, c.faults);
}

void test_fail_run_saturates_instead_of_wrapping()
{
    pc_hotswap_core_init(&c, 255, 2000, 0);
    mount_it(0);
    for (int i = 0; i < 400; i++)
    {
        pc_hotswap_core_io(&c, PROTO_FALSE);
    }
    // A wrapping counter would drop back under the threshold and un-fault the volume.
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
}

void test_fail_run_at_the_uint8_ceiling_does_not_wrap()
{
    // The saturation guard itself, with the counter already parked at the ceiling: the
    // next failure must leave it at 0xFF rather than roll it to 0 (which would drop the
    // run back under the threshold and silently un-fault a card that is gone).
    pc_hotswap_core_init(&c, 255, 2000, 0);
    mount_it(0);
    c.fail_run = 0xFF;
    TEST_ASSERT_TRUE(pc_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT8(0xFF, c.fail_run);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);
}

// --- probe pacing ---------------------------------------------------------

void test_no_probe_while_ready()
{
    mount_it(100000);
    TEST_ASSERT_FALSE(pc_hotswap_core_due(&c, 100000 + 999999));
}

void test_probe_is_rate_limited_while_absent()
{
    pc_hotswap_core_probe(&c, PROTO_FALSE, PROTO_FALSE, 100000); // nothing there
    TEST_ASSERT_FALSE(pc_hotswap_core_due(&c, 100000 + 1999));
    TEST_ASSERT_TRUE(pc_hotswap_core_due(&c, 100000 + 2000));
}

void test_probe_pacing_is_wrapsafe_across_rollover()
{
    // Last probe just before the 32-bit millis rollover; "now" just after it.
    pc_hotswap_core_probe(&c, PROTO_FALSE, PROTO_FALSE, 0xFFFFF000u);
    TEST_ASSERT_FALSE(pc_hotswap_core_due(&c, 0xFFFFF000u + 1999));
    TEST_ASSERT_TRUE(pc_hotswap_core_due(&c, 0xFFFFF000u + 2000)); // wraps past 0
}

// --- mounting -------------------------------------------------------------

void test_present_but_unmountable_stays_absent()
{
    // A card that will not mount is not storage, however present the detect pin says it is.
    TEST_ASSERT_FALSE(pc_hotswap_core_probe(&c, PROTO_TRUE, PROTO_FALSE, 100000));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(0, c.mounts);
}

void test_mount_counts_only_on_transition()
{
    TEST_ASSERT_TRUE(pc_hotswap_core_probe(&c, PROTO_TRUE, PROTO_TRUE, 100000));
    TEST_ASSERT_EQUAL_UINT32(1, c.mounts);
    // A redundant probe of an already-mounted volume is not another insertion.
    TEST_ASSERT_FALSE(pc_hotswap_core_probe(&c, PROTO_TRUE, PROTO_TRUE, 101000));
    TEST_ASSERT_EQUAL_UINT32(1, c.mounts);
}

void test_full_removal_and_reinsertion_cycle()
{
    mount_it(100000);
    TEST_ASSERT_EQUAL_UINT32(1, c.mounts);

    // Card pulled: writes start failing.
    for (int i = 0; i < 3; i++)
    {
        pc_hotswap_core_io(&c, PROTO_FALSE);
    }
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)c.state);

    // Probe while it is still out.
    TEST_ASSERT_TRUE(pc_hotswap_core_due(&c, 102000));
    pc_hotswap_core_probe(&c, PROTO_FALSE, PROTO_FALSE, 102000);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)c.state);

    // Card back in.
    TEST_ASSERT_TRUE(pc_hotswap_core_due(&c, 104000));
    TEST_ASSERT_TRUE(pc_hotswap_core_probe(&c, PROTO_TRUE, PROTO_TRUE, 104000));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(2, c.mounts);
    TEST_ASSERT_EQUAL_UINT32(1, c.faults);
    TEST_ASSERT_EQUAL_UINT8(0, c.fail_run); // a fresh mount starts with a clean run

    // And it is usable again: the old failures must not carry over.
    TEST_ASSERT_FALSE(pc_hotswap_core_io(&c, PROTO_FALSE));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
}

void test_faulted_volume_can_go_straight_back_to_ready()
{
    // A card reseated quickly enough that the probe finds it mounted without an STORAGE_STATE_ABSENT step.
    mount_it(100000);
    for (int i = 0; i < 3; i++)
    {
        pc_hotswap_core_io(&c, PROTO_FALSE);
    }
    TEST_ASSERT_TRUE(pc_hotswap_core_probe(&c, PROTO_TRUE, PROTO_TRUE, 102000));
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)c.state);
    TEST_ASSERT_EQUAL_UINT32(2, c.mounts);
}

// --- null safety + serialization -----------------------------------------

void test_null_core_is_not_a_crash()
{
    pc_hotswap_core_init(NULL, 3, 2000, 0);
    TEST_ASSERT_FALSE(pc_hotswap_core_io(NULL, PROTO_FALSE));
    TEST_ASSERT_FALSE(pc_hotswap_core_due(NULL, 0));
    TEST_ASSERT_FALSE(pc_hotswap_core_probe(NULL, PROTO_TRUE, PROTO_TRUE, 0));
}

void test_state_names()
{
    TEST_ASSERT_EQUAL_STRING("absent", pc_hotswap_state_name(STORAGE_STATE_ABSENT));
    TEST_ASSERT_EQUAL_STRING("ready", pc_hotswap_state_name(STORAGE_STATE_READY));
    TEST_ASSERT_EQUAL_STRING("faulted", pc_hotswap_state_name(STORAGE_STATE_FAULTED));
}

void test_json_and_overflow_is_fail_closed()
{
    char buf[64];
    size_t n = pc_hotswap_json(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"storage\":\"absent\",\"mounts\":0,\"faults\":0}", buf);

    char tiny[8];
    TEST_ASSERT_EQUAL_UINT32(0, pc_hotswap_json(tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("", tiny); // never a truncated half-object
    TEST_ASSERT_EQUAL_UINT32(0, pc_hotswap_json(NULL, 16));
    // A real buffer with no room at all: rejected before snprintf is handed a zero cap.
    TEST_ASSERT_EQUAL_UINT32(0, pc_hotswap_json(buf, 0));
}

// --- the owned binding ----------------------------------------------------
//
// These drive the single owned instance (s_hs), so they run after the pure-core tests;
// the first one deliberately runs before any pc_hotswap_begin().

void test_binding_poll_before_begin_does_nothing()
{
    // No callbacks installed yet: poll must not probe or claim storage. (Must be the
    // first binding test - begun latches true for the rest of the run.)
    pc_hotswap_poll_at(500000);
    TEST_ASSERT_EQUAL_INT(0, g_mount_calls);
    TEST_ASSERT_EQUAL_INT(0, g_present_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)pc_hotswap_state());
    TEST_ASSERT_FALSE(pc_hotswap_ready());
}

// Bring the owned binding up to a healthy mount at @p now, then zero the call counters.
static void bind_and_mount(uint32_t now)
{
    g_present_ok = PROTO_TRUE;
    g_mount_ok = PROTO_TRUE;
    g_ms = now;
    pc_hotswap_begin(fake_mount, fake_unmount, fake_present, &g_ctx_token);
    pc_hotswap_set_event_cb(fake_event);
    pc_hotswap_poll_at(now);
    reset_counts();
}

void test_binding_mounts_on_the_first_poll_and_notifies()
{
    // begin() back-dates the probe clock, so a card already in the slot mounts on the
    // first poll instead of one interval later - and the app hears about it.
    g_present_ok = PROTO_TRUE;
    g_mount_ok = PROTO_TRUE;
    g_ms = 10000;
    pc_hotswap_begin(fake_mount, fake_unmount, fake_present, &g_ctx_token);
    pc_hotswap_set_event_cb(fake_event);
    TEST_ASSERT_FALSE(pc_hotswap_ready()); // begin() resets to STORAGE_STATE_ABSENT
    reset_counts();

    pc_hotswap_poll_at(10000);
    TEST_ASSERT_TRUE(pc_hotswap_ready());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)pc_hotswap_state());
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);
    TEST_ASSERT_EQUAL_INT(1, g_mount_calls);
    TEST_ASSERT_EQUAL_INT(0, g_unmount_calls);
    TEST_ASSERT_EQUAL_PTR(&g_ctx_token, g_seen_ctx); // the app's ctx is handed back
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)g_event_from);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)g_event_to);
}

void test_binding_ready_volume_is_never_reprobed()
{
    // Nothing to remount while STORAGE_STATE_READY, so the per-loop poll must cost no callbacks at all.
    bind_and_mount(20000);
    pc_hotswap_poll_at(20000 + 999999);
    TEST_ASSERT_EQUAL_INT(0, g_present_calls);
    TEST_ASSERT_EQUAL_INT(0, g_mount_calls);
    TEST_ASSERT_EQUAL_INT(0, g_event_calls);
    TEST_ASSERT_TRUE(pc_hotswap_ready());
}

void test_binding_io_fault_unmounts_immediately_and_notifies()
{
    // The point of the whole owner: on the failure that faults the volume the mount is
    // dropped there and then, not at the next poll, so nothing can write through it.
    bind_and_mount(30000);
    pc_hotswap_io(PROTO_FALSE);
    pc_hotswap_io(PROTO_FALSE);
    TEST_ASSERT_TRUE(pc_hotswap_ready()); // below the threshold: still usable
    TEST_ASSERT_EQUAL_INT(0, g_unmount_calls);

    pc_hotswap_io(PROTO_FALSE); // 3rd in a row == PC_HOTSWAP_FAIL_THRESHOLD
    TEST_ASSERT_FALSE(pc_hotswap_ready());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)pc_hotswap_state());
    TEST_ASSERT_EQUAL_INT(1, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)g_event_from);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)g_event_to);

    // Outcomes reported after the fault are ignored: no second unmount, no second event.
    pc_hotswap_io(PROTO_FALSE);
    pc_hotswap_io(PROTO_TRUE);
    TEST_ASSERT_EQUAL_INT(1, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);
}

void test_binding_drops_a_faulted_mount_before_retrying()
{
    // The remount attempt unmounts first, so it starts clean instead of reusing handles
    // to a card that left.
    bind_and_mount(50000);
    for (int i = 0; i < 3; i++)
    {
        pc_hotswap_io(PROTO_FALSE);
    }
    TEST_ASSERT_EQUAL_INT(1, g_unmount_calls); // the fault itself
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)pc_hotswap_state());

    pc_hotswap_poll_at(52000); // one probe interval later
    TEST_ASSERT_EQUAL_INT(2, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_mount_calls);
    TEST_ASSERT_TRUE(pc_hotswap_ready());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)g_event_from);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)g_event_to);
}

void test_binding_faults_and_retries_without_an_unmount_callback()
{
    // unmount is optional. Without one the fault must still be recorded and notified,
    // and the retry must still run.
    g_present_ok = PROTO_TRUE;
    g_mount_ok = PROTO_TRUE;
    g_ms = 40000;
    pc_hotswap_begin(fake_mount, NULL, fake_present, &g_ctx_token);
    pc_hotswap_set_event_cb(fake_event);
    pc_hotswap_poll_at(40000);
    reset_counts();
    TEST_ASSERT_TRUE(pc_hotswap_ready());

    for (int i = 0; i < 3; i++)
    {
        pc_hotswap_io(PROTO_FALSE);
    }
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)pc_hotswap_state());
    TEST_ASSERT_EQUAL_INT(0, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);

    // Card is really gone now: the retry finds nothing and settles on STORAGE_STATE_ABSENT.
    g_present_ok = PROTO_FALSE;
    pc_hotswap_poll_at(42000);
    TEST_ASSERT_EQUAL_INT(0, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);
    TEST_ASSERT_EQUAL_INT(0, g_mount_calls); // not present -> mount is not attempted
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)pc_hotswap_state());
    TEST_ASSERT_EQUAL_INT(2, g_event_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)g_event_to);
}

void test_binding_without_card_detect_lets_the_mount_decide()
{
    // A NULL present callback means "assume a card is there"; an unmountable volume
    // then stays STORAGE_STATE_ABSENT, and an unchanged state fires no event.
    g_ms = 60000;
    g_mount_ok = PROTO_FALSE;
    pc_hotswap_begin(fake_mount, fake_unmount, NULL, &g_ctx_token);
    pc_hotswap_set_event_cb(fake_event);
    reset_counts();

    pc_hotswap_poll_at(60000);
    TEST_ASSERT_EQUAL_INT(0, g_present_calls); // nothing to ask
    TEST_ASSERT_EQUAL_INT(1, g_mount_calls);
    TEST_ASSERT_FALSE(pc_hotswap_ready());
    TEST_ASSERT_EQUAL_INT(0, g_event_calls); // STORAGE_STATE_ABSENT -> STORAGE_STATE_ABSENT is not a transition

    g_mount_ok = PROTO_TRUE;
    pc_hotswap_poll_at(62000);
    TEST_ASSERT_TRUE(pc_hotswap_ready());
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_READY, (int)g_event_to);
}

void test_binding_without_a_mount_callback_never_becomes_ready()
{
    // No way to mount anything means no storage: it must stay fail-closed rather than
    // report STORAGE_STATE_READY on the strength of card-detect alone.
    g_ms = 70000;
    g_present_ok = PROTO_TRUE;
    pc_hotswap_begin(NULL, fake_unmount, fake_present, &g_ctx_token);
    pc_hotswap_set_event_cb(fake_event);
    reset_counts();

    pc_hotswap_poll_at(70000);
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);
    TEST_ASSERT_EQUAL_INT(0, g_mount_calls);
    TEST_ASSERT_FALSE(pc_hotswap_ready());
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_ABSENT, (int)pc_hotswap_state());
}

void test_binding_event_callback_is_optional()
{
    // Clearing the event callback must not stop the machine from running.
    pc_hotswap_set_event_cb(NULL);
    g_ms = 80000;
    g_present_ok = PROTO_TRUE;
    g_mount_ok = PROTO_TRUE;
    pc_hotswap_begin(fake_mount, fake_unmount, fake_present, &g_ctx_token);
    reset_counts();

    pc_hotswap_poll_at(80000);
    TEST_ASSERT_TRUE(pc_hotswap_ready());
    TEST_ASSERT_EQUAL_INT(0, g_event_calls);
    for (int i = 0; i < 3; i++)
    {
        pc_hotswap_io(PROTO_FALSE);
    }
    TEST_ASSERT_EQUAL_INT((int)STORAGE_STATE_FAULTED, (int)pc_hotswap_state());
    TEST_ASSERT_EQUAL_INT(1, g_unmount_calls);
    TEST_ASSERT_EQUAL_INT(0, g_event_calls);
}

void test_binding_poll_reads_the_library_clock()
{
    // poll() is poll_at(pc_millis()), so the same rate limit applies to the loop-driven
    // form: one probe per interval, no mount storm while a card is missing.
    g_ms = 90000;
    g_present_ok = PROTO_FALSE;
    g_mount_ok = PROTO_TRUE;
    pc_hotswap_begin(fake_mount, fake_unmount, fake_present, &g_ctx_token);
    pc_hotswap_set_event_cb(fake_event);
    reset_counts();

    pc_hotswap_poll(); // due immediately (begin back-dates the probe clock)
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);
    TEST_ASSERT_FALSE(pc_hotswap_ready());

    g_ms = 91000; // inside the interval
    pc_hotswap_poll();
    TEST_ASSERT_EQUAL_INT(1, g_present_calls);

    g_ms = 92000; // interval elapsed, and the card is back
    g_present_ok = PROTO_TRUE;
    pc_hotswap_poll();
    TEST_ASSERT_EQUAL_INT(2, g_present_calls);
    TEST_ASSERT_TRUE(pc_hotswap_ready());
    TEST_ASSERT_EQUAL_INT(1, g_event_calls);
}

int main(int, char **)
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
    // Binding: this one must stay first (it is the only chance to see the pre-begin state).
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
