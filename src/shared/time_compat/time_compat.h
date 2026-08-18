// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file time_compat.h
 * @brief Reentrant UTC broken-down time, portable across the host and target toolchains.
 *
 * Responses are formatted from worker threads, so every conversion must write into caller storage -
 * never the shared static `tm` that `gmtime()` returns. The toolchains disagree on how to spell
 * that: newlib and glibc have `gmtime_r`, the Windows CRT has only `gmtime_s`, with the arguments
 * reversed. This is the one seam that hides the difference, so callers write it once.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TIME_COMPAT_H
#define PROTOCORE_TIME_COMPAT_H

#include <time.h> // struct tm and the gmtime_r / gmtime_s the seam picks between

#include "protocore_config.h" // the entry point

/** @brief The instant a conversion reads, and the storage it fills. */
typedef struct
{
    time_t epoch;   ///< seconds since the Unix epoch
    struct tm *out; ///< the caller's destination; must be non-null
} TimeCompatArgs;

/**
 * @brief Broken-down UTC in caller storage, whichever runtime is underneath.
 *
 * @var TimeCompatNs::args      the instant a conversion reads, and the storage it fills
 * @var TimeCompatNs::tm_out    @c args.out on success, or NULL when the instant cannot be represented
 * @var TimeCompatNs::gmtime    convert to broken-down UTC
 *
 * Reentrant: the destination is the caller's. One runtime takes (tm, time) and reports an errno_t,
 * the other takes (time, tm) and reports the destination; both are reduced to @ref tm_out here.
 *
 * No storage member: nothing is held between calls.
 */
typedef struct
{
    TimeCompatArgs args;

    struct tm *tm_out;

    void (*const gmtime)(uint8_t *restrict work);
} TimeCompatNs;

/** @brief The one symbol this module exports. */
extern TimeCompatNs TimeCompat;

#endif // PROTOCORE_TIME_COMPAT_H
