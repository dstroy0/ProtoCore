// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file httpcache.c
 * @brief Cache-Control builder / parser / freshness implementation (see httpcache.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP_CACHE

#include "httpcache.h"
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"

PROTOCORE_BEGIN_DECLS

static const size_t CC_SENT = (size_t)-1; // overflow sentinel threaded through the emitters

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_httpcache_control_init(uint8_t *restrict work);

void protocore_httpcache_control_init(uint8_t *restrict work)
{
    (void)work;
    protocore_cache_control *cc = HttpcacheV.control_init_args.cc;

    cc->cc_public = PROTO_FALSE;
    cc->cc_private = PROTO_FALSE;
    cc->no_store = PROTO_FALSE;
    cc->no_cache = PROTO_FALSE;
    cc->no_transform = PROTO_FALSE;
    cc->must_revalidate = PROTO_FALSE;
    cc->proxy_revalidate = PROTO_FALSE;
    cc->must_understand = PROTO_FALSE;
    cc->cc_immutable = PROTO_FALSE;
    cc->only_if_cached = PROTO_FALSE;
    cc->max_age = -1;
    cc->s_maxage = -1;
    cc->stale_while_revalidate = -1;
    cc->stale_if_error = -1;
    cc->max_stale = -1;
    cc->min_fresh = -1;
}

// --- build -----------------------------------------------------------------

static size_t cc_emit_uint(char *buf, size_t cap, size_t n, unsigned v)
{
    char rev[10];
    int ri = 0;
    if (v == 0)
    {
        rev[ri++] = '0';
    }
    else
    {
        while (v)
        {
            rev[ri++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    if (n + (size_t)ri > cap)
    {
        return CC_SENT;
    }
    for (int k = 0; k < ri; k++)
    {
        buf[n + k] = rev[ri - 1 - k];
    }
    return n + (size_t)ri;
}

// Emit one bare token (with the ", " separator before all but the first).
static size_t cc_tok(char *buf, size_t cap, size_t n, proto_bool *first, const char *tok)
{
    if (n == CC_SENT)
    {
        return CC_SENT;
    }
    size_t tlen = str.len(tok, cap);
    size_t need = (*first ? 0 : 2) + tlen;
    if (n + need > cap)
    {
        return CC_SENT;
    }
    if (!*first)
    {
        buf[n++] = ',';
        buf[n++] = ' ';
    }
    mem.cpy(buf + n, tok, tlen);
    *first = PROTO_FALSE;
    return n + tlen;
}

// Emit "key=value".
static size_t cc_kv(char *buf, size_t cap, size_t n, proto_bool *first, const char *key, long v)
{
    n = cc_tok(buf, cap, n, first, key);
    if (n == CC_SENT || n >= cap)
    {
        return CC_SENT;
    }
    buf[n++] = '=';
    return cc_emit_uint(buf, cap, n, (unsigned)v);
}

void protocore_httpcache_control_build(uint8_t *restrict work)
{
    (void)work;
    char *buf = HttpcacheV.control_build_args.buf;
    size_t cap = HttpcacheV.control_build_args.cap;
    const protocore_cache_control *cc = HttpcacheV.control_build_args.cc;

    if (!buf || !cc || cap == 0)
    {
        HttpcacheV.n = 0;
        return;
    }
    size_t n = 0;
    proto_bool first = PROTO_TRUE;

    if (cc->cc_public)
    {
        n = cc_tok(buf, cap, n, &first, "public");
    }
    if (cc->cc_private)
    {
        n = cc_tok(buf, cap, n, &first, "private");
    }
    if (cc->no_store)
    {
        n = cc_tok(buf, cap, n, &first, "no-store");
    }
    if (cc->no_cache)
    {
        n = cc_tok(buf, cap, n, &first, "no-cache");
    }
    if (cc->max_age >= 0)
    {
        n = cc_kv(buf, cap, n, &first, "max-age", cc->max_age);
    }
    if (cc->s_maxage >= 0)
    {
        n = cc_kv(buf, cap, n, &first, "s-maxage", cc->s_maxage);
    }
    if (cc->must_revalidate)
    {
        n = cc_tok(buf, cap, n, &first, "must-revalidate");
    }
    if (cc->proxy_revalidate)
    {
        n = cc_tok(buf, cap, n, &first, "proxy-revalidate");
    }
    if (cc->no_transform)
    {
        n = cc_tok(buf, cap, n, &first, "no-transform");
    }
    if (cc->must_understand)
    {
        n = cc_tok(buf, cap, n, &first, "must-understand");
    }
    if (cc->cc_immutable)
    {
        n = cc_tok(buf, cap, n, &first, "immutable");
    }
    if (cc->stale_while_revalidate >= 0)
    {
        n = cc_kv(buf, cap, n, &first, "stale-while-revalidate", cc->stale_while_revalidate);
    }
    if (cc->stale_if_error >= 0)
    {
        n = cc_kv(buf, cap, n, &first, "stale-if-error", cc->stale_if_error);
    }
    if (cc->only_if_cached)
    {
        n = cc_tok(buf, cap, n, &first, "only-if-cached");
    }
    if (cc->max_stale == -2)
    {
        n = cc_tok(buf, cap, n, &first, "max-stale");
    }
    else if (cc->max_stale >= 0)
    {
        n = cc_kv(buf, cap, n, &first, "max-stale", cc->max_stale);
    }
    if (cc->min_fresh >= 0)
    {
        n = cc_kv(buf, cap, n, &first, "min-fresh", cc->min_fresh);
    }

    if (n == CC_SENT || first || n + 1 > cap)
    {
        HttpcacheV.n = 0; // overflow, or nothing was emitted, or no room for the NUL
        return;
    }
    buf[n] = 0;
    HttpcacheV.n = n;
}

// --- parse -----------------------------------------------------------------

// Case-insensitive compare of [s,s+len) to the (lowercase) NUL-terminated @p target.
static proto_bool cc_ci_eq(const char *s, size_t len, const char *target)
{
    size_t i = 0;
    for (; i < len && target[i]; i++)
    {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
        {
            c = (char)(c + 32);
        }
        if (c != target[i])
        {
            return PROTO_FALSE;
        }
    }
    return i == len && target[i] == 0;
}

// Parse a non-negative delta-seconds from [v,v+vlen) (tolerates surrounding quotes / spaces).
// Returns the value clamped to INT32_MAX, or -1 if no digits are present.
static int32_t cc_parse_delta(const char *v, size_t vlen)
{
    if (!v)
    {
        return -1;
    }
    size_t i = 0;
    while (i < vlen && (v[i] == ' ' || v[i] == '\t' || v[i] == '"'))
    {
        i++;
    }
    // int64_t, not long: long is 32-bit on LLP64 (Windows), so `val * 10` overflowed (signed UB, wrapped)
    // before the clamp below could ever observe a value above INT32_MAX. Clamping every iteration keeps
    // val <= INT32_MAX, so the next multiply-add stays far inside int64_t no matter how many digits arrive.
    int64_t val = -1;
    proto_bool any = PROTO_FALSE;
    while (i < vlen && v[i] >= '0' && v[i] <= '9')
    {
        if (!any)
        {
            val = 0;
            any = PROTO_TRUE;
        }
        val = val * 10 + (v[i] - '0');
        if (val > 2147483647)
        {
            val = 2147483647;
        }
        i++;
    }
    return any ? (int32_t)val : -1;
}

static proto_bool cc_match(protocore_cache_control *cc, const char *name, size_t nlen, const char *val, size_t vlen)
{
    if (cc_ci_eq(name, nlen, "public"))
    {
        cc->cc_public = PROTO_TRUE;
    }
    else if (cc_ci_eq(name, nlen, "private"))
    {
        cc->cc_private = PROTO_TRUE;
    }
    else if (cc_ci_eq(name, nlen, "no-store"))
    {
        cc->no_store = PROTO_TRUE;
    }
    else if (cc_ci_eq(name, nlen, "no-cache"))
    {
        cc->no_cache = PROTO_TRUE;
    }
    else if (cc_ci_eq(name, nlen, "no-transform"))
    {
        cc->no_transform = PROTO_TRUE;
    }
    else if (cc_ci_eq(name, nlen, "must-revalidate"))
    {
        cc->must_revalidate = PROTO_TRUE;
    }
    else if (cc_ci_eq(name, nlen, "proxy-revalidate"))
    {
        cc->proxy_revalidate = PROTO_TRUE;
    }
    else if (cc_ci_eq(name, nlen, "must-understand"))
    {
        cc->must_understand = PROTO_TRUE;
    }
    else if (cc_ci_eq(name, nlen, "immutable"))
    {
        cc->cc_immutable = PROTO_TRUE;
    }
    else if (cc_ci_eq(name, nlen, "only-if-cached"))
    {
        cc->only_if_cached = PROTO_TRUE;
    }
    else if (cc_ci_eq(name, nlen, "max-age"))
    {
        cc->max_age = cc_parse_delta(val, vlen);
    }
    else if (cc_ci_eq(name, nlen, "s-maxage"))
    {
        cc->s_maxage = cc_parse_delta(val, vlen);
    }
    else if (cc_ci_eq(name, nlen, "stale-while-revalidate"))
    {
        cc->stale_while_revalidate = cc_parse_delta(val, vlen);
    }
    else if (cc_ci_eq(name, nlen, "stale-if-error"))
    {
        cc->stale_if_error = cc_parse_delta(val, vlen);
    }
    else if (cc_ci_eq(name, nlen, "max-stale"))
    {
        cc->max_stale = val ? cc_parse_delta(val, vlen) : -2; // present with no value = "any"
    }
    else if (cc_ci_eq(name, nlen, "min-fresh"))
    {
        cc->min_fresh = cc_parse_delta(val, vlen);
    }
    else
    {
        return PROTO_FALSE; // unknown directive - ignored
    }
    return PROTO_TRUE;
}

// Parse one comma-separated directive starting at *i (advancing past it) and apply it; returns true
// if a known directive matched.
static proto_bool cache_parse_one_directive(const char *s, size_t len, size_t *i, protocore_cache_control *cc)
{
    while (*i < len && (s[*i] == ',' || s[*i] == ' ' || s[*i] == '\t'))
    {
        (*i)++; // skip separators / OWS
    }
    if (*i >= len)
    {
        return PROTO_FALSE;
    }
    size_t start = *i;
    while (*i < len && s[*i] != ',')
    {
        (*i)++; // to the next comma
    }
    size_t end = *i;
    // `end > start` cannot go false: the skip-separators loop above already advanced *i past every
    // leading ',', ' ', and '\t', so s[start] is never one of those, which forces the "to the next
    // comma" loop to advance *i at least one past start - end is always >= start+1 on entry, and
    // since s[start] itself is never a space/tab, this trim can never step end back down to start.
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t'))
    {
        end--; // trim trailing OWS
    }
    size_t eq = start;
    while (eq < end && s[eq] != '=')
    {
        eq++;
    }
    size_t nlen = (eq < end ? eq : end) - start;
    while (nlen > 0 && (s[start + nlen - 1] == ' ' || s[start + nlen - 1] == '\t'))
    {
        nlen--; // trim trailing OWS from name
    }
    const char *val = (eq < end) ? s + eq + 1 : NULL;
    size_t vlen = (eq < end) ? end - (eq + 1) : 0;
    return nlen && cc_match(cc, s + start, nlen, val, vlen);
}

void protocore_httpcache_control_parse(uint8_t *restrict work)
{
    const char *s = HttpcacheV.control_parse_args.s;
    size_t len = HttpcacheV.control_parse_args.len;
    protocore_cache_control *cc = HttpcacheV.control_parse_args.cc;

    HttpcacheV.control_init_args.cc = cc;
    protocore_httpcache_control_init(work);
    if (!s)
    {
        HttpcacheV.ok = PROTO_FALSE;
        return;
    }
    proto_bool found = PROTO_FALSE;
    size_t i = 0;
    while (i < len)
    {
        if (cache_parse_one_directive(s, len, &i, cc))
        {
            found = PROTO_TRUE;
        }
    }
    HttpcacheV.ok = found;
}

// --- presets + freshness ---------------------------------------------------

void protocore_httpcache_immutable_asset(uint8_t *restrict work)
{
    protocore_cache_control *cc = HttpcacheV.immutable_asset_args.cc;
    uint32_t max_age = HttpcacheV.immutable_asset_args.max_age;

    HttpcacheV.control_init_args.cc = cc;
    protocore_httpcache_control_init(work);
    cc->cc_public = PROTO_TRUE;
    cc->max_age = (int32_t)(max_age > 2147483647u ? 2147483647u : max_age);
    cc->cc_immutable = PROTO_TRUE;
}

void protocore_httpcache_revalidatable(uint8_t *restrict work)
{
    protocore_cache_control *cc = HttpcacheV.revalidatable_args.cc;
    uint32_t max_age = HttpcacheV.revalidatable_args.max_age;
    int32_t stale_while_revalidate = HttpcacheV.revalidatable_args.stale_while_revalidate;

    HttpcacheV.control_init_args.cc = cc;
    protocore_httpcache_control_init(work);
    cc->cc_public = PROTO_TRUE;
    cc->max_age = (int32_t)(max_age > 2147483647u ? 2147483647u : max_age);
    if (stale_while_revalidate >= 0)
    {
        cc->stale_while_revalidate = stale_while_revalidate;
    }
}

void protocore_httpcache_no_store(uint8_t *restrict work)
{
    protocore_cache_control *cc = HttpcacheV.no_store_args.cc;

    HttpcacheV.control_init_args.cc = cc;
    protocore_httpcache_control_init(work);
    cc->no_store = PROTO_TRUE;
}

void protocore_httpcache_shared(uint8_t *restrict work)
{
    protocore_cache_control *cc = HttpcacheV.shared_args.cc;
    uint32_t max_age = HttpcacheV.shared_args.max_age;
    uint32_t s_maxage = HttpcacheV.shared_args.s_maxage;

    HttpcacheV.control_init_args.cc = cc;
    protocore_httpcache_control_init(work);
    cc->cc_public = PROTO_TRUE;
    cc->max_age = (int32_t)(max_age > 2147483647u ? 2147483647u : max_age);
    cc->s_maxage = (int32_t)(s_maxage > 2147483647u ? 2147483647u : s_maxage);
}

void protocore_httpcache_freshness_lifetime(uint8_t *restrict work)
{
    (void)work;
    const protocore_cache_control *cc = HttpcacheV.freshness_lifetime_args.cc;
    proto_bool shared = HttpcacheV.freshness_lifetime_args.shared;
    long expires_minus_date = HttpcacheV.freshness_lifetime_args.expires_minus_date;

    if (shared && cc->s_maxage >= 0)
    {
        HttpcacheV.value = cc->s_maxage;
        return;
    }
    if (cc->max_age >= 0)
    {
        HttpcacheV.value = cc->max_age;
        return;
    }
    if (expires_minus_date >= 0)
    {
        HttpcacheV.value = expires_minus_date;
        return;
    }
    HttpcacheV.value = -1; // no explicit expiration - the caller applies a heuristic
}

/** @brief The operands and the outcome. */
HttpcacheVars HttpcacheV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP_CACHE
