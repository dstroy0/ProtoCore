// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the multi-source time fallback matrix (services/timing_position/time_source/time_source.h).
//
// The registry itself has no governing standard, so those cases are PROPERTIES: ascending priority
// order, first-nonzero-wins, and that a source below the one that answered is never called (reading
// an RTC or a GPS costs time and current).
//
// What this registry feeds is the HTTP Date header, and that joining - a registered source's epoch
// rendered as RFC 9110 sec 5.6.7's own IMF-fixdate example - is test_http_clock's. It used to be
// here because the joining lived in an escape hatch inside this module, kept outside the module's
// own enable gate so response.c could reach it; that is what kept time_source in every binary.

#include "services/timing_position/time_source/time_source.h"
#include <string.h>

#include <unity.h>

// What each mock source reports, and how many times it was asked.
static uint32_t g_epoch_a, g_epoch_b, g_epoch_c;
static int g_calls_a, g_calls_b, g_calls_c;

static uint32_t src_a(void)
{
    g_calls_a++;
    return g_epoch_a;
}
static uint32_t src_b(void)
{
    g_calls_b++;
    return g_epoch_b;
}
static uint32_t src_c(void)
{
    g_calls_c++;
    return g_epoch_c;
}

void setUp(void)
{
    protocore_time_source_reset();
    g_epoch_a = g_epoch_b = g_epoch_c = 0;
    g_calls_a = g_calls_b = g_calls_c = 0;
}
void tearDown(void)
{
    protocore_time_source_reset();
}

// Ascending priority, not registration order: the lower value is queried first.
void test_lowest_priority_value_is_queried_first(void)
{
    g_epoch_a = 1000u;
    g_epoch_b = 2000u;
    TEST_ASSERT_TRUE(protocore_time_source_add("rtc", 9, src_a));
    TEST_ASSERT_TRUE(protocore_time_source_add("gnss", 1, src_b));
    TEST_ASSERT_EQUAL_UINT32(2000u, protocore_time_now());
}

// A source with no valid time reports 0, and the scan continues down the priority order.
void test_a_source_with_no_time_falls_through(void)
{
    g_epoch_a = 0u; // GNSS has lost its fix
    g_epoch_b = 0u; // NTP has never synchronized
    g_epoch_c = 4242u;
    TEST_ASSERT_TRUE(protocore_time_source_add("gnss", 0, src_a));
    TEST_ASSERT_TRUE(protocore_time_source_add("ntp", 1, src_b));
    TEST_ASSERT_TRUE(protocore_time_source_add("rtc", 2, src_c));

    TEST_ASSERT_EQUAL_UINT32(4242u, protocore_time_now());
    TEST_ASSERT_EQUAL_INT(1, g_calls_a);
    TEST_ASSERT_EQUAL_INT(1, g_calls_b);
    TEST_ASSERT_EQUAL_INT(1, g_calls_c);
}

// Once a source answers, nothing below it is invoked.
void test_an_answer_stops_the_scan(void)
{
    g_epoch_a = 7u;
    g_epoch_b = 8u;
    g_epoch_c = 9u;
    TEST_ASSERT_TRUE(protocore_time_source_add("rtc", 5, src_c));
    TEST_ASSERT_TRUE(protocore_time_source_add("ntp", 3, src_b));
    TEST_ASSERT_TRUE(protocore_time_source_add("gnss", 1, src_a));

    TEST_ASSERT_EQUAL_UINT32(7u, protocore_time_now());
    TEST_ASSERT_EQUAL_INT(1, g_calls_a);
    TEST_ASSERT_EQUAL_INT(0, g_calls_b);
    TEST_ASSERT_EQUAL_INT(0, g_calls_c);
}

// The active name is the label of the source that satisfied the last query.
void test_active_names_the_source_that_answered(void)
{
    g_epoch_a = 0u;
    g_epoch_b = 555u;
    TEST_ASSERT_TRUE(protocore_time_source_add("gnss", 0, src_a));
    TEST_ASSERT_TRUE(protocore_time_source_add("rtc", 1, src_b));

    TEST_ASSERT_EQUAL_UINT32(555u, protocore_time_now());
    TEST_ASSERT_NOT_NULL(protocore_time_source_active());
    TEST_ASSERT_EQUAL_STRING("rtc", protocore_time_source_active());

    // the GNSS regains its fix and takes the query back
    g_epoch_a = 111u;
    TEST_ASSERT_EQUAL_UINT32(111u, protocore_time_now());
    TEST_ASSERT_EQUAL_STRING("gnss", protocore_time_source_active());
}

// No source with valid time is 0 and no active name, never a stale one.
void test_no_valid_time_reports_zero_and_clears_active(void)
{
    g_epoch_a = 99u;
    TEST_ASSERT_TRUE(protocore_time_source_add("gnss", 0, src_a));
    TEST_ASSERT_EQUAL_UINT32(99u, protocore_time_now());
    TEST_ASSERT_EQUAL_STRING("gnss", protocore_time_source_active());

    g_epoch_a = 0u;
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_time_now());
    TEST_ASSERT_NULL(protocore_time_source_active());
}

// An empty registry answers 0, and a null callback is refused rather than stored and called.
void test_empty_registry_and_null_callback(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_time_now());
    TEST_ASSERT_NULL(protocore_time_source_active());
    TEST_ASSERT_FALSE(protocore_time_source_add("bad", 0, NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_time_now());
}

// The table is fixed at PROTOCORE_TIME_SOURCE_MAX entries; the one past it is refused, not written.
void test_registry_is_bounded(void)
{
    for (int i = 0; i < PROTOCORE_TIME_SOURCE_MAX; i++)
    {
        TEST_ASSERT_TRUE(protocore_time_source_add("fill", (uint8_t)i, src_a));
    }
    TEST_ASSERT_FALSE(protocore_time_source_add("overflow", 200, src_b));

    g_epoch_a = 0u;
    g_epoch_b = 1234u;
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_time_now()); // the refused source is not consulted
    TEST_ASSERT_EQUAL_INT(0, g_calls_b);
}

// Reset empties the table and drops the active name.
void test_reset_clears_the_registry(void)
{
    g_epoch_a = 321u;
    TEST_ASSERT_TRUE(protocore_time_source_add("gnss", 0, src_a));
    TEST_ASSERT_EQUAL_UINT32(321u, protocore_time_now());

    protocore_time_source_reset();
    TEST_ASSERT_EQUAL_UINT32(0u, protocore_time_now());
    TEST_ASSERT_NULL(protocore_time_source_active());

    // and the freed slot takes a registration again
    TEST_ASSERT_TRUE(protocore_time_source_add("gnss", 0, src_a));
    TEST_ASSERT_EQUAL_UINT32(321u, protocore_time_now());
}
