// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "protocore_config.h" // the entry point: PC_INLINE

/**
 * @brief Convert @p epoch to broken-down UTC in caller storage (reentrant).
 * @param epoch  Seconds since the Unix epoch.
 * @param out    Destination `struct tm` (must be non-null).
 * @return @p out on success, or NULL if @p epoch cannot be represented.
 */
PC_INLINE struct tm *pc_gmtime_r(const time_t *epoch, struct tm *out)
{
#if defined(_WIN32)
    // MS runtime: gmtime_s(tm, time) - arguments reversed vs POSIX, returns errno_t (0 == ok).
    return gmtime_s(out, epoch) == 0 ? out : NULL;
#else
    return gmtime_r(epoch, out);
#endif
}

#endif // PROTOCORE_TIME_COMPAT_H
