// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_date.h
 * @brief Render a Unix epoch as an RFC 7231 IMF-fixdate.
 *
 * One formatter serves every header that carries a timestamp (`Date`, `Last-Modified`, `Expires`),
 * so the wire rendering is fixed in one place. The break-down is reentrant (`gmtime_r`, never the
 * shared static `tm`), which is what makes it callable from any worker.
 *
 * Every rejection - no epoch, no buffer, a time that will not break down - returns 0 and leaves
 * @p out an empty string, so a caller that ignores the length still emits a well-formed header
 * value rather than whatever the buffer held.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HTTP_DATE_H
#define PROTOCORE_HTTP_DATE_H

#include <stdint.h>
#include <time.h>

#include "shared_primitives/time_compat.h"

/**
 * @brief Smallest buffer that holds an RFC 7231 IMF-fixdate plus its NUL.
 *
 * `Sun, 06 Nov 1994 08:49:37 GMT` is 29 characters and the format is fixed-width, so this is a
 * property of the protocol rather than a tuning choice. A smaller buffer truncates silently,
 * because strftime writes nothing and returns 0 when the result does not fit.
 */
#define PC_HTTP_DATE_MAX 30

/**
 * @brief Write @p epoch into @p out as an IMF-fixdate in GMT.
 * @return characters written, excluding the NUL, or 0 with an empty @p out on any rejection.
 */
inline uint8_t pc_http_date(time_t epoch, char *out, uint32_t out_cap)
{
    if (!out || out_cap == 0)
    {
        return 0;
    }
    if (epoch == 0)
    {
        out[0] = '\0';
        return 0;
    }
    struct tm broken_down;
    if (!pc_gmtime_r(&epoch, &broken_down))
    {
        out[0] = '\0';
        return 0;
    }
    return (uint8_t)strftime(out, out_cap, "%a, %d %b %Y %H:%M:%S GMT", &broken_down);
}

#endif // PROTOCORE_HTTP_DATE_H
