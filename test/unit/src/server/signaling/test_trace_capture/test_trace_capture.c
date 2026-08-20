// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the pre/post-trigger window assembler (server/signaling/trace_capture.h).
//
// No standard publishes a capture-window layout, so every expectation here is category 3: a
// property the assembler must hold whatever the implementation. The load-bearing one is
// test_window_is_the_pre_roll_then_the_post_trigger_samples - the trigger is detected only after
// the pre-trigger samples have already gone by, so the window is only trustworthy if the ring hands
// back the last pretrigger_samples IN ARRIVAL ORDER across a wrap. A ring read from the wrong
// cursor still fills a window of the right length, with the samples rotated.
//
// The env sizes PROTOCORE_TC_MAX_WINDOW_SAMPLES at 32.

#include "server/signaling/trace_capture/trace_capture.h"

#include <unity.h>

#define MAX_WINDOWS 8

static uint16_t g_samples[MAX_WINDOWS][PROTOCORE_TC_MAX_WINDOW_SAMPLES];
static uint16_t g_n_samples[MAX_WINDOWS];
static uint16_t g_pre[MAX_WINDOWS];
static uint32_t g_trace_id[MAX_WINDOWS];
static size_t g_windows;
static void *g_ctx_seen;

static void on_window(const protocore_tc_window *w, void *ctx)
{
    if (g_windows >= MAX_WINDOWS)
    {
        return;
    }
    for (uint16_t i = 0; i < w->n_samples && i < PROTOCORE_TC_MAX_WINDOW_SAMPLES; i++)
    {
        g_samples[g_windows][i] = w->samples[i];
    }
    g_n_samples[g_windows] = w->n_samples;
    g_pre[g_windows] = w->pretrigger_samples;
    g_trace_id[g_windows] = w->trace_id;
    g_ctx_seen = ctx;
    g_windows++;
}

static proto_bool begin(uint16_t pre, uint16_t post, void *ctx)
{
    static protocore_tc_config cfg;
    cfg.pretrigger_samples = pre;
    cfg.posttrigger_samples = post;
    cfg.sink = on_window;
    cfg.ctx = ctx;
    TraceCapture.cfg = &cfg;
    TraceCapture.begin(protocore_trace_capture_span());
    return TraceCapture.ok;
}

static uint16_t feed(const uint16_t *s, uint16_t n)
{
    TraceCapture.feed.samples = s;
    TraceCapture.feed.n = n;
    TraceCapture.feed_in(protocore_trace_capture_span());
    return TraceCapture.accepted;
}

static proto_bool trigger(void)
{
    TraceCapture.trigger(protocore_trace_capture_span());
    return TraceCapture.ok;
}

static proto_bool capturing(void)
{
    TraceCapture.capturing(protocore_trace_capture_span());
    return TraceCapture.ok;
}

static protocore_tc_stats stats(void)
{
    protocore_tc_stats st;
    TraceCapture.feed.stats = &st;
    TraceCapture.get_stats(protocore_trace_capture_span());
    return st;
}

// Arming zeroes the whole capture - ring, cursors and tallies - so a begin/end pair is what gives
// each case a clean slate; end alone only disarms.
void setUp(void)
{
    g_windows = 0;
    g_ctx_seen = NULL;
    (void)begin(1, 1, NULL);
    TraceCapture.end(protocore_trace_capture_span());
}
void tearDown(void)
{
    TraceCapture.end(protocore_trace_capture_span());
}

// The window straddles the trigger: its first pretrigger_samples are the last samples that arrived
// BEFORE the trigger, oldest first, and the rest are the samples that arrived after. Six samples
// through a four-deep ring leave 2,3,4,5 - the ring has wrapped, so a read from the wrong cursor
// would give 4,5,2,3 instead.
void test_window_is_the_pre_roll_then_the_post_trigger_samples(void)
{
    TEST_ASSERT_TRUE(begin(4, 4, NULL));

    static const uint16_t before[] = {0, 1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_UINT16(6, feed(before, 6));
    TEST_ASSERT_FALSE(capturing());

    TEST_ASSERT_TRUE(trigger());
    TEST_ASSERT_TRUE(capturing());

    static const uint16_t after[] = {100, 101, 102, 103};
    TEST_ASSERT_EQUAL_UINT16(4, feed(after, 4));
    TEST_ASSERT_FALSE(capturing()); // the window completed inside the feed

    TEST_ASSERT_EQUAL_size_t(1u, g_windows);
    TEST_ASSERT_EQUAL_UINT16(8, g_n_samples[0]);
    TEST_ASSERT_EQUAL_UINT16(4, g_pre[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, g_trace_id[0]);

    static const uint16_t want[] = {2, 3, 4, 5, 100, 101, 102, 103};
    for (int i = 0; i < 8; i++)
    {
        TEST_ASSERT_EQUAL_UINT16(want[i], g_samples[0][i]);
    }

    protocore_tc_stats st = stats();
    TEST_ASSERT_EQUAL_UINT32(1u, st.windows_completed);
    TEST_ASSERT_EQUAL_UINT32(0u, st.triggers_dropped);
    TEST_ASSERT_EQUAL_UINT32(0u, st.samples_dropped);
}

// A ring that has not yet wrapped still reads oldest first: two samples into a four-deep ring put
// them at the front of the pre-roll, and the two the ring was cleared to sit behind them.
void test_a_partly_filled_pre_roll_still_reads_oldest_first(void)
{
    TEST_ASSERT_TRUE(begin(4, 2, NULL));

    static const uint16_t before[] = {77, 88};
    TEST_ASSERT_EQUAL_UINT16(2, feed(before, 2));
    TEST_ASSERT_TRUE(trigger());

    static const uint16_t after[] = {1, 2};
    TEST_ASSERT_EQUAL_UINT16(2, feed(after, 2));

    TEST_ASSERT_EQUAL_size_t(1u, g_windows);
    TEST_ASSERT_EQUAL_UINT16(6, g_n_samples[0]);
    TEST_ASSERT_EQUAL_UINT16(0, g_samples[0][0]);
    TEST_ASSERT_EQUAL_UINT16(0, g_samples[0][1]);
    TEST_ASSERT_EQUAL_UINT16(77, g_samples[0][2]);
    TEST_ASSERT_EQUAL_UINT16(88, g_samples[0][3]);
    TEST_ASSERT_EQUAL_UINT16(1, g_samples[0][4]);
    TEST_ASSERT_EQUAL_UINT16(2, g_samples[0][5]);
}

// One capture in flight, fail-closed: a trigger while a window is still filling is refused and
// counted, and the window that was already filling completes with its own samples intact.
void test_a_second_trigger_is_refused_and_counted(void)
{
    TEST_ASSERT_TRUE(begin(2, 4, NULL));

    static const uint16_t before[] = {9, 8};
    (void)feed(before, 2);
    TEST_ASSERT_TRUE(trigger());
    TEST_ASSERT_FALSE(trigger());
    TEST_ASSERT_FALSE(trigger());

    protocore_tc_stats st = stats();
    TEST_ASSERT_EQUAL_UINT32(2u, st.triggers_dropped);
    TEST_ASSERT_EQUAL_size_t(0u, g_windows); // still filling

    static const uint16_t after[] = {1, 2, 3, 4};
    (void)feed(after, 4);
    TEST_ASSERT_EQUAL_size_t(1u, g_windows);
    TEST_ASSERT_EQUAL_UINT16(9, g_samples[0][0]);
    TEST_ASSERT_EQUAL_UINT16(8, g_samples[0][1]);
    TEST_ASSERT_EQUAL_UINT16(1, g_samples[0][2]);
    TEST_ASSERT_EQUAL_UINT16(4, g_samples[0][5]);
}

// trace_id counts completed windows, so successive captures carry 0, 1, 2 and the tally agrees.
void test_trace_id_counts_completed_windows(void)
{
    TEST_ASSERT_TRUE(begin(1, 1, NULL));
    for (uint16_t i = 0; i < 3; i++)
    {
        const uint16_t before[1] = {(uint16_t)(100 + i)};
        const uint16_t after[1] = {(uint16_t)(200 + i)};
        (void)feed(before, 1);
        TEST_ASSERT_TRUE(trigger());
        (void)feed(after, 1);
    }

    TEST_ASSERT_EQUAL_size_t(3u, g_windows);
    for (uint32_t i = 0; i < 3; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(i, g_trace_id[i]);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(100 + i), g_samples[i][0]);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)(200 + i), g_samples[i][1]);
    }

    protocore_tc_stats st = stats();
    TEST_ASSERT_EQUAL_UINT32(3u, st.windows_completed);
}

// Arming is refused rather than half-applied: no config, no sink, nothing to capture, or a window
// wider than the static storage.
void test_arming_is_refused_when_it_cannot_be_honored(void)
{
    TraceCapture.cfg = NULL;
    TraceCapture.begin(protocore_trace_capture_span());
    TEST_ASSERT_FALSE(TraceCapture.ok);

    static protocore_tc_config cfg;
    cfg.pretrigger_samples = 4;
    cfg.posttrigger_samples = 4;
    cfg.sink = NULL;
    cfg.ctx = NULL;
    TraceCapture.cfg = &cfg;
    TraceCapture.begin(protocore_trace_capture_span());
    TEST_ASSERT_FALSE(TraceCapture.ok);

    TEST_ASSERT_FALSE(begin(0, 0, NULL)); // no samples on either side of the trigger

    // The split's sum is what has to fit, so a legal half plus an illegal one is still refused.
    TEST_ASSERT_FALSE(begin(0, PROTOCORE_TC_MAX_WINDOW_SAMPLES + 1, NULL));
    TEST_ASSERT_FALSE(begin(PROTOCORE_TC_MAX_WINDOW_SAMPLES, 1, NULL));

    // The whole of the static storage is arm-able.
    TEST_ASSERT_TRUE(begin(PROTOCORE_TC_MAX_WINDOW_SAMPLES / 2, PROTOCORE_TC_MAX_WINDOW_SAMPLES / 2, NULL));
}

// Samples that arrive when nothing is armed are counted dropped rather than kept, before begin and
// again after end. A null block while armed is dropped by its length, never read.
void test_samples_with_nothing_armed_are_counted_dropped(void)
{
    static const uint16_t s[] = {1, 2, 3};
    TEST_ASSERT_EQUAL_UINT16(0, feed(s, 3));
    TEST_ASSERT_EQUAL_UINT32(3u, stats().samples_dropped);

    TEST_ASSERT_TRUE(begin(2, 2, NULL));
    TEST_ASSERT_EQUAL_UINT16(0, feed(NULL, 5));
    TEST_ASSERT_EQUAL_UINT32(5u, stats().samples_dropped);

    TraceCapture.end(protocore_trace_capture_span());
    TEST_ASSERT_EQUAL_UINT16(0, feed(s, 3));
    TEST_ASSERT_EQUAL_UINT32(8u, stats().samples_dropped);
    TEST_ASSERT_FALSE(trigger()); // and a trigger with nothing armed is refused
    TEST_ASSERT_FALSE(capturing());
}

// Arming clears the tallies and the pre-roll: a capture starts from nothing carried over from the
// previous one.
void test_arming_clears_the_previous_capture(void)
{
    static const uint16_t s[] = {1, 2, 3};
    (void)feed(s, 3); // dropped, nothing armed
    TEST_ASSERT_EQUAL_UINT32(3u, stats().samples_dropped);

    TEST_ASSERT_TRUE(begin(2, 2, NULL));
    protocore_tc_stats st = stats();
    TEST_ASSERT_EQUAL_UINT32(0u, st.samples_dropped);
    TEST_ASSERT_EQUAL_UINT32(0u, st.windows_completed);
    TEST_ASSERT_EQUAL_UINT32(0u, st.triggers_dropped);

    // The pre-roll starts empty, so a trigger straight after arming reads zeros, not stale samples.
    TEST_ASSERT_TRUE(trigger());
    static const uint16_t after[] = {5, 6};
    (void)feed(after, 2);
    TEST_ASSERT_EQUAL_size_t(1u, g_windows);
    TEST_ASSERT_EQUAL_UINT16(0, g_samples[0][0]);
    TEST_ASSERT_EQUAL_UINT16(0, g_samples[0][1]);
}

// A trigger-only capture keeps no history, so its window is exactly the samples that followed the
// trigger.
void test_a_capture_with_no_pre_roll_is_all_post_trigger(void)
{
    TEST_ASSERT_TRUE(begin(0, 3, NULL));
    TEST_ASSERT_TRUE(trigger());

    static const uint16_t after[] = {7, 8, 9};
    TEST_ASSERT_EQUAL_UINT16(3, feed(after, 3));

    TEST_ASSERT_EQUAL_size_t(1u, g_windows);
    TEST_ASSERT_EQUAL_UINT16(0, g_pre[0]);
    TEST_ASSERT_EQUAL_UINT16(3, g_n_samples[0]);
    TEST_ASSERT_EQUAL_UINT16(7, g_samples[0][0]);
    TEST_ASSERT_EQUAL_UINT16(8, g_samples[0][1]);
    TEST_ASSERT_EQUAL_UINT16(9, g_samples[0][2]);
}

// A capture that collects nothing after the trigger never completes: the window fires when the last
// post-trigger sample lands, and there is no such sample. It stays in flight, and the samples still
// arriving are accepted into the ring.
void test_a_capture_with_no_post_trigger_never_completes(void)
{
    TEST_ASSERT_TRUE(begin(3, 0, NULL));

    static const uint16_t before[] = {1, 2, 3};
    (void)feed(before, 3);
    TEST_ASSERT_TRUE(trigger());
    TEST_ASSERT_TRUE(capturing());

    static const uint16_t more[] = {4, 5};
    TEST_ASSERT_EQUAL_UINT16(2, feed(more, 2));
    TEST_ASSERT_TRUE(capturing());
    TEST_ASSERT_EQUAL_size_t(0u, g_windows);
}

// The sink is handed back the opaque context it was armed with, unchanged.
void test_the_sink_gets_the_context_it_was_armed_with(void)
{
    int marker = 0;
    TEST_ASSERT_TRUE(begin(0, 1, &marker));
    TEST_ASSERT_TRUE(trigger());

    static const uint16_t after[] = {1};
    (void)feed(after, 1);
    TEST_ASSERT_EQUAL_size_t(1u, g_windows);
    TEST_ASSERT_EQUAL_PTR(&marker, g_ctx_seen);
}

// A stats read with nowhere to copy to writes nothing and does not follow the null; the tallies are
// still there for the next read.
void test_a_stats_read_with_no_destination_is_refused(void)
{
    static const uint16_t s[] = {1, 2};
    (void)feed(s, 2); // nothing armed: two samples dropped

    TraceCapture.feed.stats = NULL;
    TraceCapture.get_stats(protocore_trace_capture_span());
    TEST_ASSERT_EQUAL_UINT32(2u, stats().samples_dropped);
}
