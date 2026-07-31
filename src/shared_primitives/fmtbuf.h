// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fmtbuf.h
 * @brief printf-append into a bounded caller-owned buffer that fails closed - one source of truth.
 *
 * The JSON emitters that build with @c snprintf (dashboard, gpio_map, partition_monitor, ...) each carried a
 * byte-identical `json_append(out, cap, *pos, fmt, ...)` helper: format at @c out[*pos], advance @c *pos, and
 * return -1 (leaving the buffer truncated) if it would not fit, so callers fail closed instead of overflowing.
 * It lives here once.
 *
 * Distinct from strbuf.h (a raw bump-appender that stays stdio-free): this one deliberately pulls in
 * @c <stdio.h> / @c <stdarg.h> for the @c vsnprintf formatting. Header-only, no heap.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FMTBUF_H
#define PROTOCORE_FMTBUF_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/**
 * @brief Append @p fmt (printf-style) at @p out[*pos], bounded by @p cap; advances @p *pos on success.
 * @return 0 on success, or -1 if the buffer is already full or the fragment would not fit (the buffer is
 *         left truncated, so the caller fails closed rather than overflowing).
 *
 * This is deliberately a C-varargs printf wrapper: it exists to forward a caller's format + args straight
 * to vsnprintf from one place, so the ellipsis and the (always-literal, internal) runtime format string
 * are intrinsic to its purpose, not a smell.
 */
inline int pc_fmt_append(char *out, size_t cap, size_t *pos, const char *fmt, ...) // NOSONAR forwarding wrapper
{
    if (*pos >= cap)
    {
        return -1;
    }
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(out + *pos, cap - *pos, fmt, ap); // NOSONAR fmt is an internal compile-time literal
    va_end(ap);
    // w < 0 (a vsnprintf output-encoding error) is unreachable on a host build: every caller in this
    // codebase passes one of its own internal, compile-time-literal format strings (see the file-level
    // doc comment above), built from plain %s/%g/%d conversions that never fault. Forcing a genuine
    // negative return (e.g. via an oversized field width) does not fail gracefully on this toolchain -
    // it aborts the process - so it cannot be exercised from a host test without crashing the runner.
    // Split out from the fits-in-buffer check below so the exclusion covers only this truly-unreachable
    // case: the fits-in-buffer branch is real and already exercised (both ways) by the fmtbuf/dashboard
    // truncation tests, and folding it into the same compound condition would hide that from coverage too.
    if (w < 0) // GCOVR_EXCL_LINE
    {
        return -1; // GCOVR_EXCL_LINE
    }
    if ((size_t)w >= cap - *pos)
    {
        return -1;
    }
    *pos += (size_t)w;
    return 0;
}

#endif // PROTOCORE_FMTBUF_H
