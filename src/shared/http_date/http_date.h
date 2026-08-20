// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "shared/time_compat/time_compat.h" // ::TimeCompat, time_t, and the entry point behind it

/**
 * @brief Smallest buffer that holds an RFC 7231 IMF-fixdate plus its NUL.
 *
 * `Sun, 06 Nov 1994 08:49:37 GMT` is 29 characters and the format is fixed-width, so this is a
 * property of the protocol rather than a tuning choice. A smaller buffer truncates silently,
 * because the builder writes nothing and reports 0 when the result does not fit.
 */
#define PROTOCORE_HTTP_DATE_MAX 30

/** @brief The instant a format renders, and where it lands. */
typedef struct
{
    time_t epoch;     ///< seconds since the Unix epoch; 0 renders empty
    char *out;        ///< where the date lands
    uint32_t out_cap; ///< how much room it has; ::PROTOCORE_HTTP_DATE_MAX holds the whole form
} HttpDateArgs;

/**
 * @brief The IMF-fixdate an HTTP Date header carries.
 *
 * @var HttpDateNs::args      the instant a format renders, and where it lands
 * @var HttpDateNs::n         characters written, excluding the NUL, or 0 on any rejection
 * @var HttpDateNs::format    render the instant in GMT
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
} HttpDateVars;

/** @brief The operands and the outcome. */
extern HttpDateVars HttpDateV;

/** @brief The entries. */
typedef struct
{
    void (*const format)(uint8_t *restrict work);
} HttpDateNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in HttpDateV or a region of the borrow at a fixed offset.
void protocore_http_date_format(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `HttpDate.format(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HttpDateNs HttpDate __attribute__((unused)) = {
    .format = protocore_http_date_format,
};

#endif // PROTOCORE_HTTP_DATE_H
