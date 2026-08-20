// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for reentrant broken-down UTC (shared/time_compat/time_compat.h).
//
// This is the seam that hides gmtime_r (newlib, glibc) from gmtime_s (Windows CRT), which take
// their arguments in opposite orders and report success differently. The thing worth testing is
// that the seam reduces both to the same contract, and that it fills the CALLER's struct - a
// response is formatted from a worker thread, and the shared static that plain gmtime() returns
// would be overwritten by any other thread mid-format.
//
// The field conventions are C's, not this module's: tm_year counts from 1900, tm_mon from 0,
// tm_wday from Sunday, tm_yday from 0. Each expected value below is derived from the Unix epoch's
// own definition, shown in the comment that carries it.

#include "shared/time_compat/time_compat.h"
#include <string.h>

#include <unity.h>

static uint8_t time_compat_work[16]; // the borrow an entry takes; TimeCompat never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static struct tm *conv(time_t epoch, struct tm *dst)
{
    TimeCompat.args.epoch = epoch;
    TimeCompat.args.out = dst;
    TimeCompat.gmtime(time_compat_work);
    return TimeCompat.tm_out;
}

// Epoch 0 is 1970-01-01 00:00:00 UTC, a Thursday, by the definition of the Unix epoch.
// tm_year = 1970-1900 = 70; tm_mon = 0 (January); tm_wday = 4 (Thursday); tm_yday = 0.
void test_epoch_zero_is_the_definition(void)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    TEST_ASSERT_NOT_NULL(conv((time_t)0, &t));
    TEST_ASSERT_EQUAL_INT(70, t.tm_year);
    TEST_ASSERT_EQUAL_INT(0, t.tm_mon);
    TEST_ASSERT_EQUAL_INT(1, t.tm_mday);
    TEST_ASSERT_EQUAL_INT(0, t.tm_hour);
    TEST_ASSERT_EQUAL_INT(0, t.tm_min);
    TEST_ASSERT_EQUAL_INT(0, t.tm_sec);
    TEST_ASSERT_EQUAL_INT(4, t.tm_wday); // Thursday
    TEST_ASSERT_EQUAL_INT(0, t.tm_yday);
}

// The result is written into the caller's storage and reported as that same pointer, which is what
// makes it safe to call from two worker threads at once.
void test_fills_caller_storage(void)
{
    struct tm a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    struct tm *ra = conv((time_t)0, &a);
    struct tm *rb = conv((time_t)86400, &b);

    TEST_ASSERT_EQUAL_PTR(&a, ra);
    TEST_ASSERT_EQUAL_PTR(&b, rb);
    TEST_ASSERT_NOT_EQUAL(a.tm_mday, b.tm_mday); // the first did not follow the second
    TEST_ASSERT_EQUAL_INT(1, a.tm_mday);
    TEST_ASSERT_EQUAL_INT(2, b.tm_mday);
}

// 86400 seconds is exactly one day: 1970-01-02, a Friday.
void test_one_day_advances_one_date(void)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    TEST_ASSERT_NOT_NULL(conv((time_t)86400, &t));
    TEST_ASSERT_EQUAL_INT(70, t.tm_year);
    TEST_ASSERT_EQUAL_INT(0, t.tm_mon);
    TEST_ASSERT_EQUAL_INT(2, t.tm_mday);
    TEST_ASSERT_EQUAL_INT(5, t.tm_wday); // Friday
    TEST_ASSERT_EQUAL_INT(1, t.tm_yday);
}

// The last second of the first day, so every time field is at its maximum without the date moving.
void test_end_of_first_day(void)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    TEST_ASSERT_NOT_NULL(conv((time_t)86399, &t));
    TEST_ASSERT_EQUAL_INT(1, t.tm_mday);
    TEST_ASSERT_EQUAL_INT(23, t.tm_hour);
    TEST_ASSERT_EQUAL_INT(59, t.tm_min);
    TEST_ASSERT_EQUAL_INT(59, t.tm_sec);
}

// 2^31-1: the last instant a signed 32-bit time_t names, 2038-01-19 03:14:07 UTC, a Tuesday.
void test_signed_32_bit_limit(void)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    TEST_ASSERT_NOT_NULL(conv((time_t)2147483647, &t));
    TEST_ASSERT_EQUAL_INT(138, t.tm_year); // 2038-1900
    TEST_ASSERT_EQUAL_INT(0, t.tm_mon);    // January
    TEST_ASSERT_EQUAL_INT(19, t.tm_mday);
    TEST_ASSERT_EQUAL_INT(3, t.tm_hour);
    TEST_ASSERT_EQUAL_INT(14, t.tm_min);
    TEST_ASSERT_EQUAL_INT(7, t.tm_sec);
    TEST_ASSERT_EQUAL_INT(2, t.tm_wday); // Tuesday
}

// 2000 is a leap year (divisible by 400), so Feb 29 exists and tm_yday counts it.
//   1970-01-01 .. 2000-01-01 = 30*365 + 7 leap days = 10957 days
//   + 31 (January) + 28 = 11016 days -> 2000-02-29, tm_yday = 59 (0-based)
void test_leap_day_2000(void)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    TEST_ASSERT_NOT_NULL(conv((time_t)(11016 * 86400), &t));
    TEST_ASSERT_EQUAL_INT(100, t.tm_year); // 2000-1900
    TEST_ASSERT_EQUAL_INT(1, t.tm_mon);    // February
    TEST_ASSERT_EQUAL_INT(29, t.tm_mday);
    TEST_ASSERT_EQUAL_INT(59, t.tm_yday);
}

// A null destination is refused rather than written through, and reported as null.
void test_null_destination_is_refused(void)
{
    TEST_ASSERT_NULL(conv((time_t)0, NULL));
}
