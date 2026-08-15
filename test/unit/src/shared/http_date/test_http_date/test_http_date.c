// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the IMF-fixdate formatter (shared/http_date/http_date.h).
//
// The load-bearing test is test_rfc9110_published_example: RFC 9110 sec 5.6.7 prints exactly one
// example of the preferred format, and reproducing it octet for octet is what makes this formatter
// trustworthy. Every other expected string here is derived by arithmetic from the epoch's own
// definition - shown in the comment that carries it - rather than taken from another date library,
// so a wrong day-name table or an off-by-one month index cannot be reproduced by accident.
//
// The day and month names matter beyond spelling. The grammar writes them with ABNF's
// case-sensitive %s prefix and sec 5.6.7 states "HTTP-date is case sensitive", so these are fixed
// US-ASCII octets, not whatever a locale would render.

#include "shared/http_date/http_date.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Format @p epoch and return the buffer, so each case reads as one assertion.
static char g_out[PROTOCORE_HTTP_DATE_MAX];

static const char *fmt(time_t epoch)
{
    HttpDate.args.epoch = epoch;
    HttpDate.args.out = g_out;
    HttpDate.args.out_cap = (uint32_t)sizeof(g_out);
    HttpDate.format(HttpDate.internal);
    return g_out;
}

// RFC 9110 sec 5.6.7: "An example of the preferred format is
//     Sun, 06 Nov 1994 08:49:37 GMT    ; IMF-fixdate"
//
// Its epoch, from the definition of the Unix epoch alone:
//   1970-01-01 .. 1994-01-01 = 24 years, of which 1972/76/80/84/88/92 are leap
//                            = 24*365 + 6            = 8766 days
//   1994-01-01 .. 1994-11-06 = 31+28+31+30+31+30+31+31+30+31 = 304 days to Oct 31,
//                              +6 to Nov 6, less the 1st itself = 309 days
//   (8766 + 309) * 86400                             = 784080000
//   08:49:37 = 8*3600 + 49*60 + 37                   =     31777
//                                                      ----------
//                                                      784111777
void test_rfc9110_published_example(void)
{
    TEST_ASSERT_EQUAL_STRING("Sun, 06 Nov 1994 08:49:37 GMT", fmt((time_t)784111777));
}

// sec 5.6.7 fixes every field's width, so the whole form is 29 octets whatever the instant.
void test_form_is_fixed_width(void)
{
    (void)fmt((time_t)784111777);
    TEST_ASSERT_EQUAL_UINT(29u, HttpDate.n);
    (void)fmt((time_t)1);
    TEST_ASSERT_EQUAL_UINT(29u, HttpDate.n);
    (void)fmt((time_t)2147483647);
    TEST_ASSERT_EQUAL_UINT(29u, HttpDate.n);
}

// One second past the epoch's own definition: 1970-01-01 00:00:00 UTC was a Thursday.
void test_one_second_past_the_epoch(void)
{
    TEST_ASSERT_EQUAL_STRING("Thu, 01 Jan 1970 00:00:01 GMT", fmt((time_t)1));
}

// 2^31-1: the last instant a signed 32-bit time_t can name.
void test_signed_32_bit_limit(void)
{
    TEST_ASSERT_EQUAL_STRING("Tue, 19 Jan 2038 03:14:07 GMT", fmt((time_t)2147483647));
}

// Every day-name in the table, walked one 86400-second step at a time from the RFC's own Sunday.
// One anchor plus arithmetic covers all seven without a second date source to be wrong about.
void test_day_names_cycle_from_the_anchor(void)
{
    static const char *const DAY[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    for (int i = 0; i < 7; i++)
    {
        const char *s = fmt((time_t)(784111777 + (i * 86400)));
        char got[4] = {s[0], s[1], s[2], '\0'};
        TEST_ASSERT_EQUAL_STRING(DAY[i], got);
    }
}

// 1900 and 2100 are not leap years but 2000 is: the Gregorian century rule. Feb 29 2000 exists, so
// the day after it is Mar 1 - a month index that only lands right if February's length is correct.
//
//   1970-01-01 .. 2000-01-01 = 30 years, leap 1972..1996 every 4th = 7 leap days
//                            = 30*365 + 7 = 10957 days
//   +31 (January) +28 = 10957 + 59 = 11016 days to 2000-02-29
//   11016 * 86400 = 951782400
void test_leap_day_2000(void)
{
    TEST_ASSERT_EQUAL_STRING("Tue, 29 Feb 2000 00:00:00 GMT", fmt((time_t)951782400));
    TEST_ASSERT_EQUAL_STRING("Wed, 01 Mar 2000 00:00:00 GMT", fmt((time_t)(951782400 + 86400)));
}

// Epoch 0 renders empty rather than 1970: the module's contract, so a caller that never obtained a
// wall clock emits no Date header instead of a false one.
void test_epoch_zero_renders_empty(void)
{
    TEST_ASSERT_EQUAL_STRING("", fmt((time_t)0));
    TEST_ASSERT_EQUAL_UINT(0u, HttpDate.n);
}

// A buffer one octet short of the fixed width truncates to empty, never to a partial date: half an
// IMF-fixdate is a different instant, and a peer would parse it as one.
void test_short_buffer_yields_empty_not_partial(void)
{
    char small[PROTOCORE_HTTP_DATE_MAX - 1];
    HttpDate.args.epoch = (time_t)784111777;
    HttpDate.args.out = small;
    HttpDate.args.out_cap = (uint32_t)sizeof(small);
    HttpDate.format(HttpDate.internal);
    TEST_ASSERT_EQUAL_UINT(0u, HttpDate.n);
    TEST_ASSERT_EQUAL_CHAR('\0', small[0]);
}

// A null destination is reported, not written through.
void test_null_destination_is_refused(void)
{
    HttpDate.args.epoch = (time_t)784111777;
    HttpDate.args.out = NULL;
    HttpDate.args.out_cap = PROTOCORE_HTTP_DATE_MAX;
    HttpDate.format(HttpDate.internal);
    TEST_ASSERT_EQUAL_UINT(0u, HttpDate.n);
}

// A zero capacity is the same refusal, with a non-null buffer.
void test_zero_capacity_is_refused(void)
{
    char one[1];
    one[0] = 'x';
    HttpDate.args.epoch = (time_t)784111777;
    HttpDate.args.out = one;
    HttpDate.args.out_cap = 0;
    HttpDate.format(HttpDate.internal);
    TEST_ASSERT_EQUAL_UINT(0u, HttpDate.n);
    TEST_ASSERT_EQUAL_CHAR('x', one[0]); // untouched, not terminated into
}
