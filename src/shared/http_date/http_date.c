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

#include "mmgr/membuild.h" // ::Sb: the builder each fixed-width field is appended through

// The day-name and month literals of RFC 9110 sec 5.6.7, in the order struct tm numbers them:
// tm_wday counts from Sunday and tm_mon from January. The grammar spells both with ABNF's
// case-sensitive %s prefix and states "HTTP-date is case sensitive", so these are the exact octets.
static const char PROTOCORE_HTTP_DATE_DAY[7][3] = {{'S', 'u', 'n'}, {'M', 'o', 'n'}, {'T', 'u', 'e'}, {'W', 'e', 'd'},
                                                   {'T', 'h', 'u'}, {'F', 'r', 'i'}, {'S', 'a', 't'}};

static const char PROTOCORE_HTTP_DATE_MON[12][3] = {{'J', 'a', 'n'}, {'F', 'e', 'b'}, {'M', 'a', 'r'}, {'A', 'p', 'r'},
                                                    {'M', 'a', 'y'}, {'J', 'u', 'n'}, {'J', 'u', 'l'}, {'A', 'u', 'g'},
                                                    {'S', 'e', 'p'}, {'O', 'c', 't'}, {'N', 'o', 'v'}, {'D', 'e', 'c'}};

#define PROTOCORE_HTTP_DATE_2DIGIT 2 // day, hour, minute and second are each 2DIGIT
#define PROTOCORE_HTTP_DATE_4DIGIT 4 // year is 4DIGIT
#define PROTOCORE_HTTP_DATE_YEAR_BASE 1900 // tm_year counts from here
#define PROTOCORE_HTTP_DATE_YEAR_MAX 9999  // the widest year 4DIGIT holds

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
    const long year = (long)broken_down.tm_year + PROTOCORE_HTTP_DATE_YEAR_BASE;
    if (broken_down.tm_wday < 0 || broken_down.tm_wday > 6 || broken_down.tm_mon < 0 || broken_down.tm_mon > 11 ||
        year < 0 || year > PROTOCORE_HTTP_DATE_YEAR_MAX)
    {
        out[0] = '\0';
        return;
    }
    // IMF-fixdate (RFC 9110 sec 5.6.7):
    //   day-name "," SP day SP month SP year SP hour ":" minute ":" second SP "GMT"
    // Every field is fixed width, so the whole form is 29 octets and the builder's one bound decides
    // whether it lands. finish reports 0 when it did not fit, which empties the buffer rather than
    // leaving a partial date.
    protocore_sb b = {out, out_cap, 0, PROTO_TRUE};
    Sb.put_n(&b, PROTOCORE_HTTP_DATE_DAY[broken_down.tm_wday], sizeof PROTOCORE_HTTP_DATE_DAY[0]);
    Sb.put_n(&b, ", ", 2);
    Sb.u32w(&b, (uint32_t)broken_down.tm_mday, PROTOCORE_HTTP_DATE_2DIGIT);
    Sb.ch(&b, ' ');
    Sb.put_n(&b, PROTOCORE_HTTP_DATE_MON[broken_down.tm_mon], sizeof PROTOCORE_HTTP_DATE_MON[0]);
    Sb.ch(&b, ' ');
    Sb.u32w(&b, (uint32_t)year, PROTOCORE_HTTP_DATE_4DIGIT);
    Sb.ch(&b, ' ');
    Sb.u32w(&b, (uint32_t)broken_down.tm_hour, PROTOCORE_HTTP_DATE_2DIGIT);
    Sb.ch(&b, ':');
    Sb.u32w(&b, (uint32_t)broken_down.tm_min, PROTOCORE_HTTP_DATE_2DIGIT);
    Sb.ch(&b, ':');
    Sb.u32w(&b, (uint32_t)broken_down.tm_sec, PROTOCORE_HTTP_DATE_2DIGIT);
    Sb.put_n(&b, " GMT", 4);
    ctx->ns->n = (uint8_t)Sb.finish(&b);
    if (ctx->ns->n == 0)
    {
        // The builder appends as it goes, so an overflow leaves the octets it managed to write.
        // Half an IMF-fixdate is a different instant, so the destination is emptied.
        out[0] = '\0';
    }
}

HttpDateNs HttpDate = {.format = http_date_format, .internal = &s_date};
