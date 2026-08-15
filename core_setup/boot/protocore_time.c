// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_time.c
 * @brief The broken-down UTC conversion a freestanding image has no C library to ask for.
 *
 * shared/time_compat asks the runtime for gmtime_r, which is the one libc call a bare-metal link
 * cannot satisfy. Nothing about the conversion needs a library: it is integer arithmetic over the
 * proleptic Gregorian calendar, so it is supplied here alongside the other symbols the image owes -
 * the block functions, the assert hook, the read-modify-write atomics.
 *
 * Shifting the year to start in March puts the leap day at the end of it, which is what turns the
 * month into a single division and the 400-year leap cycle into a fixed 146097 days.
 */

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/** @brief Days from 1970-01-01 to @p y - @p m - @p d. The inverse of the walk in gmtime_r. */
static int64_t days_from_civil(int64_t y, int64_t m, int64_t d)
{
    y -= (m <= 2);
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;                                  // [0, 399]
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
    return era * 146097 + doe - 719468;
}

struct tm *gmtime_r(const time_t *epoch, struct tm *out);

struct tm *gmtime_r(const time_t *epoch, struct tm *out)
{
    if (!epoch || !out)
    {
        return NULL;
    }

    // Whole days and the second within the day. C division truncates toward zero, so a negative
    // epoch lands one day high with a negative remainder; adding a day back moves both to the floor
    // the calendar math assumes.
    int64_t days = (int64_t)*epoch / 86400;
    int64_t rem = (int64_t)*epoch % 86400;
    if (rem < 0)
    {
        rem += 86400;
        days -= 1;
    }

    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;                                     // [0, 146096]
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);               // [0, 365]
    const int64_t mp = (5 * doy + 2) / 153;                                    // [0, 11], March = 0
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;                            // [1, 31]
    const int64_t m = mp + (mp < 10 ? 3 : -9);                                 // [1, 12]
    const int64_t y = yoe + era * 400 + (m <= 2 ? 1 : 0);

    out->tm_sec = (int)(rem % 60);
    out->tm_min = (int)((rem / 60) % 60);
    out->tm_hour = (int)(rem / 3600);
    out->tm_mday = (int)d;
    out->tm_mon = (int)m - 1;
    out->tm_year = (int)(y - 1900);
    // 1970-01-01 was a Thursday, so day 0 is weekday 4. The floored modulus keeps that true before
    // the epoch as well.
    out->tm_wday = (int)(((days % 7) + 11) % 7);
    out->tm_yday = (int)(days - days_from_civil(y, 1, 1));
    out->tm_isdst = 0; // UTC has no daylight rule
    return out;
}
