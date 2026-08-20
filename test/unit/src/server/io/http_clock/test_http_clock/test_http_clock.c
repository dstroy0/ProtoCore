// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the HTTP Date header's clock (server/io/http_clock/http_clock.h).
//
// The load-bearing case is test_rfc9110_date_from_the_active_source. RFC 9110 sec 5.6.7 prints
// exactly one IMF-fixdate example, and reproducing it octet for octet from the epoch a registered
// source reported is what proves the registry and the formatter are joined correctly rather than
// each being right alone. These cases used to live in test_time_source and test_ntp_service,
// because the joining lived in an escape hatch inside each of those modules; it is one module with
// one consumer now, so the cases are here.
//
// The other half - "no clock yet means no header rather than a wrong one" - is RFC 9110's rule,
// not a preference: a Date is omitted when the origin has no trustworthy clock.
//
// NOT tested here: a caller-supplied buffer that is too short. There is no caller-supplied buffer -
// the entry renders into a region of the borrow it is handed - and the formatter's own refusal to
// write a partial date is test_http_date's test_short_buffer_yields_empty_not_partial.

#include "server/io/http_clock/http_clock.h"
#include "services/timing_position/time_source/time_source.h"
#include "shared/http_date/http_date.h"
#include <unity.h>

// RFC 9110 sec 5.6.7's own example, Sun, 06 Nov 1994 08:49:37 GMT, at its Unix epoch.
#define RFC9110_EPOCH 784111777u
#define RFC9110_TEXT "Sun, 06 Nov 1994 08:49:37 GMT"

// What the mock source reports. Zero is "no valid time yet", which is what a source answers before
// it has synced.
static uint32_t g_epoch;

static uint32_t src(void)
{
    return g_epoch;
}

void setUp(void)
{
    protocore_time_source_reset();
    g_epoch = 0;
    HttpClockV.n = 0;
    HttpClockV.imf = NULL;
}

void tearDown(void)
{
}

// The registry answers with the epoch its highest-priority valid source reported, and the entry
// renders that instant. Octet for octet against the standard's own example.
void test_rfc9110_date_from_the_active_source(void)
{
    g_epoch = RFC9110_EPOCH;
    TEST_ASSERT_TRUE(protocore_time_source_add("gnss", 0, src));

    HttpClock.date(protocore_http_clock_span());

    TEST_ASSERT_EQUAL_UINT(29u, HttpClockV.n);
    TEST_ASSERT_EQUAL_STRING(RFC9110_TEXT, HttpClockV.imf);
}

// A registered source that has no time yet reports 0, and 0 is not an instant - it is the absence
// of one. Rendering it would date every response to 1970.
void test_no_date_until_a_source_has_valid_time(void)
{
    TEST_ASSERT_TRUE(protocore_time_source_add("gnss", 0, src)); // g_epoch is 0
    HttpClock.date(protocore_http_clock_span());
    TEST_ASSERT_EQUAL_UINT(0u, HttpClockV.n);
}

// With nothing registered at all the registry has nothing to ask, which is the same answer by a
// different route - a clock-less boot before any source is added.
void test_no_date_with_no_source_registered(void)
{
    HttpClock.date(protocore_http_clock_span());
    TEST_ASSERT_EQUAL_UINT(0u, HttpClockV.n);
}

// The span is the module's own bytes and is the same span every call, so the rendered text stays
// where the caller was told to find it rather than moving under a second call.
void test_the_span_is_stable_across_calls(void)
{
    uint8_t *first = protocore_http_clock_span();
    g_epoch = RFC9110_EPOCH;
    TEST_ASSERT_TRUE(protocore_time_source_add("gnss", 0, src));

    HttpClock.date(protocore_http_clock_span());
    const char *rendered = HttpClockV.imf;
    HttpClock.date(protocore_http_clock_span());

    TEST_ASSERT_EQUAL_PTR(first, protocore_http_clock_span());
    TEST_ASSERT_EQUAL_PTR(rendered, HttpClockV.imf);
    TEST_ASSERT_EQUAL_STRING(RFC9110_TEXT, HttpClockV.imf);
}
