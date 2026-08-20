// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file time_compat.c
 * @brief Broken-down UTC in caller storage, whichever runtime is underneath. See time_compat.h.
 *
 * Pure: the destination is the caller's and nothing is held between calls, so there is no storage
 * member. The one thing that differs between runtimes is the argument order and the return, which
 * is what this file exists to hide.
 */

#include "shared/time_compat/time_compat.h"

static void time_gmtime(uint8_t *restrict work)
{
    (void)work;
    const time_t epoch = TimeCompatV.args.epoch;
    struct tm *out = TimeCompatV.args.out;

#if defined(_WIN32)
    // One runtime takes (tm, time) and returns an errno_t; the other takes (time, tm) and returns
    // the destination. Both are reduced to "the destination, or NULL" here.
    TimeCompatV.tm_out = (gmtime_s(out, &epoch) == 0) ? out : NULL;
#else
    TimeCompatV.tm_out = gmtime_r(&epoch, out);
#endif
}

/** @brief The operands and the outcome. */
TimeCompatVars TimeCompatV;
