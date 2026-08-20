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
} TimeCompatVars;

/** @brief The operands and the outcome. */
extern TimeCompatVars TimeCompatV;

/** @brief The entries. */
typedef struct
{
    void (*const gmtime)(uint8_t *restrict work);
} TimeCompatNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in TimeCompatV or a region of the borrow at a fixed offset.
void protocore_time_compat_gmtime(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `TimeCompat.gmtime(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const TimeCompatNs TimeCompat __attribute__((unused)) = {
    .gmtime = protocore_time_compat_gmtime,
};

#endif // PROTOCORE_TIME_COMPAT_H
