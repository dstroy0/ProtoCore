// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_date.c
 * @brief The IMF-fixdate an HTTP Date header carries. See http_date.h.
 *
 * Pure: the destination is the caller's and nothing is held between calls, so there is no storage
 * member.
 */

#include "shared/http_date/http_date.h"

#include <time.h> // strftime: the one formatter, over broken-down UTC

/**
 * @brief The formatter's call - what HttpDateNs points at.
 *
 * @var HttpDateInternal::ns  the handle a caller sets the call's members on
 */
struct HttpDateInternal
{
    HttpDateNs *ns;
};

static struct HttpDateInternal s_date = {.ns = &HttpDate};

static void http_date_format(struct HttpDateInternal *restrict ctx)
{
    char *out = ctx->ns->args.out;
    const uint32_t out_cap = ctx->ns->args.out_cap;

    ctx->ns->n = 0;
    if (!out || out_cap == 0)
    {
        return;
    }
    if (ctx->ns->args.epoch == 0)
    {
        out[0] = '\0';
        return;
    }
    struct tm broken_down;
    TimeCompat.args.epoch = ctx->ns->args.epoch;
    TimeCompat.args.out = &broken_down;
    TimeCompat.gmtime(TimeCompat.internal);
    if (!TimeCompat.tm_out)
    {
        out[0] = '\0';
        return;
    }
    // strftime writes nothing and reports 0 when the result does not fit, which is why a buffer
    // smaller than PROTOCORE_HTTP_DATE_MAX truncates to empty rather than to a partial date.
    ctx->ns->n = (uint8_t)strftime(out, out_cap, "%a, %d %b %Y %H:%M:%S GMT", &broken_down);
}

HttpDateNs HttpDate = {.format = http_date_format, .internal = &s_date};
