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

#include <time.h> // time_t and strftime

#include "shared/time_compat/time_compat.h" // ::TimeCompat, and the entry point behind it

/**
 * @brief Smallest buffer that holds an RFC 7231 IMF-fixdate plus its NUL.
 *
 * `Sun, 06 Nov 1994 08:49:37 GMT` is 29 characters and the format is fixed-width, so this is a
 * property of the protocol rather than a tuning choice. A smaller buffer truncates silently,
 * because strftime writes nothing and returns 0 when the result does not fit.
 */
#define PROTOCORE_HTTP_DATE_MAX 30

/** @brief The instant a format renders, and where it lands. */
typedef struct
{
    time_t epoch;      ///< seconds since the Unix epoch; 0 renders empty
    char *out;         ///< where the date lands
    uint32_t out_cap;  ///< how much room it has; ::PROTOCORE_HTTP_DATE_MAX holds the whole form
} HttpDateArgs;

/** @brief The formatter's own call, described only in http_date.c. */
struct HttpDateInternal;

/**
 * @brief The IMF-fixdate an HTTP Date header carries.
 *
 * @var HttpDateNs::args      the instant a format renders, and where it lands
 * @var HttpDateNs::n         characters written, excluding the NUL, or 0 on any rejection
 * @var HttpDateNs::format    render the instant in GMT
 * @var HttpDateNs::internal  the call that renders it
 *
 * A buffer smaller than ::PROTOCORE_HTTP_DATE_MAX truncates to empty rather than to a partial date,
 * because the formatter writes nothing and reports 0 when the result does not fit.
 *
 * No storage member: the destination is the caller's and nothing is held between calls.
 */
typedef struct
{
    HttpDateArgs args;

    uint8_t n;

    void (*format)(struct HttpDateInternal *ctx);

    struct HttpDateInternal *internal;
} HttpDateNs;

/** @brief The one symbol this module exports. */
extern HttpDateNs HttpDate;

#endif // PROTOCORE_HTTP_DATE_H
