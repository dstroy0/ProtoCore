// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file webdav.c
 * @brief WebDAV wire format (RFC 4918): method classification, header parsing,
 *        and the 207 Multi-Status XML builder. Pure - no sockets, no filesystem.
 */

#include "network_drivers/application/webdav/webdav.h"
#include "shared_primitives/hex.h"

#if PC_ENABLE_WEBDAV

#include <string.h>

WebDavMethod pc_webdav_method(const char *m)
{
    if (!m)
    {
        return DAV_M_UNSUPPORTED;
    }
    if (!strcmp(m, "OPTIONS"))
    {
        return DAV_M_OPTIONS;
    }
    if (!strcmp(m, "GET"))
    {
        return DAV_M_GET;
    }
    if (!strcmp(m, "HEAD"))
    {
        return DAV_M_HEAD;
    }
    if (!strcmp(m, "PUT"))
    {
        return DAV_M_PUT;
    }
    if (!strcmp(m, "DELETE"))
    {
        return DAV_M_DELETE;
    }
    if (!strcmp(m, "PROPFIND"))
    {
        return DAV_M_PROPFIND;
    }
    if (!strcmp(m, "PROPPATCH"))
    {
        return DAV_M_PROPPATCH;
    }
    if (!strcmp(m, "MKCOL"))
    {
        return DAV_M_MKCOL;
    }
    if (!strcmp(m, "COPY"))
    {
        return DAV_M_COPY;
    }
    if (!strcmp(m, "MOVE"))
    {
        return DAV_M_MOVE;
    }
    if (!strcmp(m, "LOCK"))
    {
        return DAV_M_LOCK;
    }
    if (!strcmp(m, "UNLOCK"))
    {
        return DAV_M_UNLOCK;
    }
    return DAV_M_UNSUPPORTED;
}

int pc_webdav_depth(const char *depth_hdr, int dflt)
{
    if (!depth_hdr || !depth_hdr[0])
    {
        return dflt;
    }
    if (!strcmp(depth_hdr, "0"))
    {
        return 0;
    }
    if (!strcmp(depth_hdr, "1"))
    {
        return 1;
    }
    if (!strcmp(depth_hdr, "infinity"))
    {
        return PC_DAV_DEPTH_INFINITY;
    }
    return dflt;
}

// Append a NUL-terminated string if it fits; returns false (leaving *len and the
// NUL terminator intact) when it would overflow.
static proto_bool app(char *buf, size_t cap, size_t *len, const char *s)
{
    size_t n = strnlen(s, cap + 1);
    if (*len + n + 1 > cap)
    {
        return PROTO_FALSE;
    }
    memcpy(buf + *len, s, n);
    *len += n;
    buf[*len] = '\0';
    return PROTO_TRUE;
}

size_t pc_webdav_xml_escape(char *dst, size_t cap, const char *src)
{
    size_t o = 0;
    if (cap == 0)
    {
        return 0;
    }
    for (const char *p = src; *p; p++)
    {
        const char *rep = NULL;
        switch (*p)
        {
        case '&':
            rep = "&amp;";
            break;
        case '<':
            rep = "&lt;";
            break;
        case '>':
            rep = "&gt;";
            break;
        case '"':
            rep = "&quot;";
            break;
        case '\'':
            rep = "&apos;";
            break;
        default:
            break;
        }
        if (rep)
        {
            size_t rn = strnlen(rep, cap + 1);
            if (o + rn + 1 > cap)
            {
                break;
            }
            memcpy(dst + o, rep, rn);
            o += rn;
        }
        else
        {
            if (o + 1 + 1 > cap)
            {
                break;
            }
            dst[o++] = *p;
        }
    }
    dst[o] = '\0';
    return o;
}

proto_bool pc_webdav_dest_path(const char *destination, char *out, size_t cap)
{
    if (!destination || !out || cap == 0)
    {
        return PROTO_FALSE;
    }

    // Skip an absolute-URI scheme + authority: after "://", advance to the first
    // '/' (the path). An abs-path value ("/p/q") is used as-is.
    const char *p = destination;
    const char *scheme = strstr(destination, "://");
    if (scheme)
    {
        p = scheme + 3;
        while (*p && *p != '/')
        {
            p++;
        }
        if (*p != '/')
        {
            return PROTO_FALSE; // authority with no path
        }
    }
    else if (*p != '/')
    {
        return PROTO_FALSE; // not an absolute path
    }

    // Percent-decode into out. A while loop so the %XX case can consume its two
    // extra hex digits without mutating a for-loop counter.
    size_t o = 0;
    while (*p)
    {
        char c = *p;
        if (c == '%')
        {
            int hi = pc_hex_val(p[1]);
            int lo = (hi >= 0) ? pc_hex_val(p[2]) : -1;
            if (hi < 0 || lo < 0)
            {
                return PROTO_FALSE; // malformed escape
            }
            c = (char)((hi << 4) | lo);
            p += 2;
        }
        if (o + 1 >= cap)
        {
            return PROTO_FALSE; // no room for char + NUL
        }
        out[o++] = c;
        p++;
    }
    out[o] = '\0';
    return PROTO_TRUE;
}

size_t pc_webdav_ms_begin(char *buf, size_t cap, size_t len)
{
    app(buf, cap, &len, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<D:multistatus xmlns:D=\"DAV:\">\n");
    return len;
}

size_t pc_webdav_ms_entry(char *buf, size_t cap, size_t len, const char *href, proto_bool is_collection, uint32_t size,
                          const char *rfc1123_mtime, const char *content_type)
{
    // Build the whole <response> in a temp first so the append is atomic: a
    // partial element is never left in the document when the buffer fills.
    char tmp[512];
    size_t t = 0;
    char esc[256];

    pc_webdav_xml_escape(esc, sizeof(esc), href);
    // The href block is at most 27 + esc(<=255) + 66 == 348 bytes, and adding the collection
    // marker + resourcetype close reaches <=381 - all well within tmp[512], so these three
    // atomic-append guards cannot fire (esc is capped by its own 256-byte buffer above).
    //
    // GCOVR_EXCL_START  buffer-overflow guard, unreachable per the budget above. A block (not
    // per-line markers) because gcov attributes this multi-line OR-condition's uncovered branch to
    // the call-opening line of the third app() (the wrapped string-literal argument line), not to
    // the line carrying the comment - a per-line marker on the wrong line leaves the branch
    // reported as a real gap.
    if (!app(tmp, sizeof(tmp), &t, "  <D:response>\n    <D:href>") || !app(tmp, sizeof(tmp), &t, esc) ||
        !app(tmp, sizeof(tmp), &t, "</D:href>\n    <D:propstat>\n      <D:prop>\n        <D:resourcetype>"))
    {
        return len;
    }
    // GCOVR_EXCL_STOP

    if (is_collection && !app(tmp, sizeof(tmp), &t, "<D:collection/>")) // GCOVR_EXCL_BR_LINE app overflow unreachable
    {
        return len; // GCOVR_EXCL_LINE unreachable: <=363 < tmp[512] (see above)
    }
    if (!app(tmp, sizeof(tmp), &t, "</D:resourcetype>\n")) // GCOVR_EXCL_LINE unreachable: <=381 < tmp[512] (see above)
    {
        return len; // GCOVR_EXCL_LINE unreachable: <=381 < tmp[512] (see above)
    }

    if (!is_collection)
    {
        char num[24];
        unsigned long s = (unsigned long)size;
        // minimal itoa to avoid pulling in snprintf in the pure core
        char rev[24];
        int rn = 0;
        do
        {
            rev[rn++] = (char)('0' + (int)(s % 10));
            s /= 10;
        } while (s && rn < (int)sizeof(rev)); // GCOVR_EXCL_BR_LINE unreachable rn-bound: a uint32_t is <=10 digits, rn
                                              // never reaches sizeof(rev)==24
        int ni = 0;
        while (rn > 0)
        {
            num[ni++] = rev[--rn];
        }
        num[ni] = '\0';
        // The href block above tops out at <=381 bytes (<=348 plus the optional
        // <D:collection/> + </D:resourcetype> close, though this branch only runs for
        // !is_collection so it's really <=366); this fixed getcontentlength markup + a
        // 10-digit uint32_t max add <=60 more, so the running total never nears tmp[512]
        // and these atomic-append guards cannot fire.
        if (!app(tmp, sizeof(tmp), &t, "        <D:getcontentlength>") ||
            !app(tmp, sizeof(tmp), &t, num) ||                     // GCOVR_EXCL_LINE unreachable: see comment above
            !app(tmp, sizeof(tmp), &t, "</D:getcontentlength>\n")) // GCOVR_EXCL_LINE unreachable: see comment above
        {
            return len; // GCOVR_EXCL_LINE unreachable: see comment above
        }
        // content_type block. The append-overflow arm is unreachable per the budget above (running
        // total <=~446 < tmp[512]); gcov lumps the multi-app OR onto one line, so the whole merged
        // guard is excluded from coverage rather than carrying a misplaced per-line branch marker.
        // GCOVR_EXCL_START
        if (content_type && content_type[0] &&
            (!app(tmp, sizeof(tmp), &t, "        <D:getcontenttype>") || !app(tmp, sizeof(tmp), &t, content_type) ||
             !app(tmp, sizeof(tmp), &t, "</D:getcontenttype>\n")))
        {
            return len;
        }
        // GCOVR_EXCL_STOP
    }

    if (rfc1123_mtime && rfc1123_mtime[0])
    {
        if (!app(tmp, sizeof(tmp), &t, "        <D:getlastmodified>") || !app(tmp, sizeof(tmp), &t, rfc1123_mtime) ||
            !app(tmp, sizeof(tmp), &t, "</D:getlastmodified>\n"))
        {
            return len;
        }
    }

    if (!app(tmp, sizeof(tmp), &t,
             "      </D:prop>\n      <D:status>HTTP/1.1 200 OK</D:status>\n"
             "    </D:propstat>\n  </D:response>\n"))
    {
        return len;
    }

    // Atomic commit: app() appends the finished element only if it fits and leaves
    // len unchanged on no-room, so the caller sees an unchanged len and stops adding.
    app(buf, cap, &len, tmp);
    return len;
}

size_t pc_webdav_ms_end(char *buf, size_t cap, size_t len)
{
    app(buf, cap, &len, "</D:multistatus>\n");
    return len;
}

// True for a byte that ends an XML element name (whitespace, '/', '>').
static proto_bool name_end_char(char c)
{
    // The only caller scans a name span bounded by the '>' index (its tag-end loop stops there),
    // so this helper never sees '>'; that leg is a defensive, host-unreachable arm. gcov attributes
    // every operand's branch to this one line, so BR_LINE also drops the (exercised) ws/'/' arms.
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/' || c == '>'; // GCOVR_EXCL_BR_LINE
}

size_t pc_webdav_proppatch_ms(char *buf, size_t cap, const char *href, const char *body, size_t body_len)
{
    size_t len = 0;
    if (cap)
    {
        buf[0] = '\0'; // always a valid C-string, even if nothing below fits
    }
    char esc[256];
    pc_webdav_xml_escape(esc, sizeof(esc), href);
    if (!app(buf, cap, &len,
             "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<D:multistatus xmlns:D=\"DAV:\">\n"
             "  <D:response>\n    <D:href>") ||
        !app(buf, cap, &len, esc) || !app(buf, cap, &len, "</D:href>\n    <D:propstat>\n      <D:prop>\n"))
    {
        return 0;
    }

    // Walk the request and echo every element that sits directly inside a <prop>
    // (across all <set>/<remove> blocks) as a self-closed element. The wrappers
    // (propertyupdate / set / remove / prop) are skipped; only the properties are
    // reflected, each refused 403 below.
    int emitted = 0;
    proto_bool in_prop = PROTO_FALSE;
    size_t i = 0;
    while (i < body_len && emitted < PC_WEBDAV_MAX_PROPS)
    {
        if (body[i] != '<')
        {
            i++;
            continue;
        }
        size_t start = i + 1;
        if (start < body_len && (body[start] == '?' || body[start] == '!'))
        {
            while (i < body_len && body[i] != '>') // skip PI / comment / declaration
            {
                i++;
            }
            i++;
            continue;
        }
        proto_bool closing = (start < body_len && body[start] == '/');
        if (closing)
        {
            start++;
        }
        size_t end = start;
        while (end < body_len && body[end] != '>')
        {
            end++;
        }
        if (end >= body_len)
        {
            break; // unterminated tag
        }
        proto_bool self_closed = (end > start && body[end - 1] == '/');
        size_t name_end = start;
        while (name_end < end && !name_end_char(body[name_end]))
        {
            name_end++;
        }
        size_t local = start; // local name = after the last ':' in the qualified name
        for (size_t k = start; k < name_end; k++)
        {
            if (body[k] == ':')
            {
                local = k + 1;
            }
        }
        proto_bool is_prop = (name_end - local) == 4 && !strncmp(&body[local], "prop", 4);

        if (closing)
        {
            if (is_prop)
            {
                in_prop = PROTO_FALSE;
            }
            i = end + 1;
            continue;
        }
        if (is_prop)
        {
            if (!self_closed) // <prop> opens a block; <prop/> is an empty block
            {
                in_prop = PROTO_TRUE;
            }
            i = end + 1;
            continue;
        }
        if (in_prop)
        {
            // Echo this property element self-closed. Copy the open-tag content
            // (name + its own xmlns/attrs), dropping a trailing '/' and trailing
            // whitespace; reject a span containing '<' so nothing is injected.
            size_t copy_end = self_closed ? end - 1 : end;
            while (copy_end > start && (body[copy_end - 1] == ' ' || body[copy_end - 1] == '\t' ||
                                        body[copy_end - 1] == '\r' || body[copy_end - 1] == '\n'))
            {
                copy_end--;
            }
            proto_bool ok = copy_end > start;
            for (size_t k = start; k < copy_end && ok; k++)
            {
                if (body[k] == '<')
                {
                    ok = PROTO_FALSE;
                }
            }
            char tag[256];
            size_t tl = copy_end - start;
            if (ok && tl < sizeof(tag))
            {
                memcpy(tag, &body[start], tl);
                tag[tl] = '\0';
                if (app(buf, cap, &len, "        <") && app(buf, cap, &len, tag) && app(buf, cap, &len, "/>\n"))
                {
                    emitted++;
                }
            }
            if (!self_closed)
            {
                // Skip the property's value up to its close tag (no nesting expected).
                size_t j = end + 1;
                while (j + 1 < body_len && !(body[j] == '<' && body[j + 1] == '/'))
                {
                    j++;
                }
                while (j < body_len && body[j] != '>')
                {
                    j++;
                }
                i = (j < body_len) ? j + 1 : body_len;
            }
            else
            {
                i = end + 1;
            }
            continue;
        }
        i = end + 1;
    }

    if (!app(buf, cap, &len,
             "      </D:prop>\n      <D:status>HTTP/1.1 403 Forbidden</D:status>\n"
             "    </D:propstat>\n  </D:response>\n</D:multistatus>\n"))
    {
        return 0;
    }
    return len;
}

// ── lock manager (RFC 4918 §6-7) ───────────────────────────────────────────────────────────────

// Copy src into dst[cap], NUL-terminated; false if it does not fit.
static proto_bool dav_lock_copy(char *dst, size_t cap, const char *src)
{
    size_t n = strnlen(src, cap);
    if (n + 1 > cap)
    {
        return PROTO_FALSE;
    }
    memcpy(dst, src, n + 1);
    return PROTO_TRUE;
}

// Normalize a path into dst, stripping trailing '/' (but keeping a lone root "/"). false on overflow.
static proto_bool dav_lock_norm(char *dst, size_t cap, const char *path)
{
    size_t n = strnlen(path, cap);
    while (n > 1 && path[n - 1] == '/') // drop trailing slashes so "/a/" and "/a" are one resource
    {
        n--;
    }
    if (n + 1 > cap)
    {
        return PROTO_FALSE;
    }
    memcpy(dst, path, n);
    dst[n] = 0;
    return PROTO_TRUE;
}

// True if `child` equals `parent` or lies (at a segment boundary) under it. Both trailing-slash-normalized.
static proto_bool dav_lock_same_or_under(const char *parent, const char *child)
{
    size_t pn = strnlen(parent, PC_DAV_LOCK_PATH_MAX);
    if (strncmp(parent, child, pn) != 0)
    {
        return PROTO_FALSE;
    }
    if (child[pn] == 0) // exactly equal
    {
        return PROTO_TRUE;
    }
    if (pn == 1 && parent[0] == '/') // root covers everything below it
    {
        return PROTO_TRUE;
    }
    return child[pn] == '/'; // only a real path-segment boundary, so "/a" does not cover "/ab"
}

// Do two lock scopes overlap? A Depth-infinity lock's scope is its whole subtree.
static proto_bool dav_lock_overlap(const char *pa, proto_bool ia, const char *pb, proto_bool ib)
{
    if (strcmp(pa, pb) == 0)
    {
        return PROTO_TRUE;
    }
    if (ia && dav_lock_same_or_under(pa, pb)) // pb sits under the infinity lock pa
    {
        return PROTO_TRUE;
    }
    if (ib && dav_lock_same_or_under(pb, pa)) // pa sits under the infinity lock pb
    {
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// Does an active lock's scope cover the normalized query path?
static proto_bool dav_lock_covers(const DavLock *l, const char *np)
{
    if (strcmp(l->path, np) == 0)
    {
        return PROTO_TRUE;
    }
    return l->depth_infinity && dav_lock_same_or_under(l->path, np);
}

void pc_dav_lock_init(DavLockTable *t)
{
    if (!t)
    {
        return;
    }
    for (size_t i = 0; i < PC_DAV_LOCK_MAX; i++)
    {
        t->locks[i].active = PROTO_FALSE;
    }
}

const DavLock *pc_dav_lock_acquire(DavLockTable *t, const char *path, const char *token, proto_bool exclusive,
                                   proto_bool depth_infinity, uint32_t expiry_s)
{
    if (!t || !path || !token)
    {
        return NULL;
    }
    char np[PC_DAV_LOCK_PATH_MAX];
    if (!dav_lock_norm(np, sizeof(np), path))
    {
        return NULL;
    }
    if (strnlen(token, PC_DAV_LOCK_TOKEN_MAX) + 1 > PC_DAV_LOCK_TOKEN_MAX) // token would not fit
    {
        return NULL;
    }

    // Conflict: an exclusive request clashes with any overlapping lock; a shared one only with an
    // overlapping exclusive lock (two shared locks may coexist).
    for (size_t i = 0; i < PC_DAV_LOCK_MAX; i++)
    {
        const DavLock *l = &t->locks[i];
        if (l->active && dav_lock_overlap(l->path, l->depth_infinity, np, depth_infinity) &&
            (exclusive || l->exclusive))
        {
            return NULL;
        }
    }

    for (size_t i = 0; i < PC_DAV_LOCK_MAX; i++)
    {
        DavLock *l = &t->locks[i];
        if (l->active)
        {
            continue;
        }
        (void)dav_lock_copy(l->path, sizeof(l->path), np); // np already fits (same cap)
        (void)dav_lock_copy(l->token, sizeof(l->token), token);
        l->exclusive = exclusive;
        l->depth_infinity = depth_infinity;
        l->expiry_s = expiry_s;
        l->active = PROTO_TRUE;
        return l;
    }
    return NULL; // table full
}

size_t pc_dav_lock_sweep(DavLockTable *t, uint32_t now_s)
{
    if (!t)
    {
        return 0;
    }
    size_t dropped = 0;
    for (size_t i = 0; i < PC_DAV_LOCK_MAX; i++)
    {
        DavLock *l = &t->locks[i];
        if (l->active && l->expiry_s != 0 && l->expiry_s <= now_s) // 0 = never expires
        {
            l->active = PROTO_FALSE;
            dropped++;
        }
    }
    return dropped;
}

const DavLock *pc_dav_lock_refresh(DavLockTable *t, const char *token, uint32_t new_expiry_s)
{
    if (!t || !token)
    {
        return NULL;
    }
    for (size_t i = 0; i < PC_DAV_LOCK_MAX; i++)
    {
        DavLock *l = &t->locks[i];
        if (l->active && strcmp(l->token, token) == 0)
        {
            l->expiry_s = new_expiry_s;
            return l;
        }
    }
    return NULL;
}

const DavLock *pc_dav_lock_find(const DavLockTable *t, const char *path)
{
    if (!t || !path)
    {
        return NULL;
    }
    char np[PC_DAV_LOCK_PATH_MAX];
    if (!dav_lock_norm(np, sizeof(np), path))
    {
        return NULL;
    }
    for (size_t i = 0; i < PC_DAV_LOCK_MAX; i++)
    {
        if (t->locks[i].active && dav_lock_covers(&t->locks[i], np))
        {
            return &t->locks[i];
        }
    }
    return NULL;
}

proto_bool pc_dav_lock_release(DavLockTable *t, const char *token)
{
    if (!t || !token)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < PC_DAV_LOCK_MAX; i++)
    {
        if (t->locks[i].active && strcmp(t->locks[i].token, token) == 0)
        {
            t->locks[i].active = PROTO_FALSE;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

proto_bool pc_dav_lock_can_write(const DavLockTable *t, const char *path, const char *presented_token)
{
    if (!t)
    {
        return PROTO_TRUE; // no table => nothing is locked
    }
    if (!path)
    {
        return PROTO_FALSE;
    }
    char np[PC_DAV_LOCK_PATH_MAX];
    if (!dav_lock_norm(np, sizeof(np), path))
    {
        return PROTO_TRUE; // an unparseable path is not something the lock table can guard
    }
    proto_bool covered = PROTO_FALSE;
    for (size_t i = 0; i < PC_DAV_LOCK_MAX; i++)
    {
        const DavLock *l = &t->locks[i];
        if (!l->active || !dav_lock_covers(l, np))
        {
            continue;
        }
        covered = PROTO_TRUE;
        if (presented_token && strcmp(l->token, presented_token) == 0)
        {
            return PROTO_TRUE; // the request holds a covering lock's token
        }
    }
    return !covered; // unlocked => allowed; locked with no / wrong token => denied
}

proto_bool pc_dav_if_token(const char *if_header, char *out, size_t cap)
{
    if (!if_header || !out || cap == 0)
    {
        return PROTO_FALSE;
    }
    // The state tokens live inside a condition list "( ... )"; take the first Coded-URL "<...>" within it
    // (which also skips the tagged-list resource URL that precedes the '(').
    const char *lp = strchr(if_header, '(');
    if (!lp)
    {
        return PROTO_FALSE;
    }
    const char *lt = strchr(lp, '<');
    if (!lt)
    {
        return PROTO_FALSE;
    }
    const char *gt = strchr(lt, '>');
    if (!gt)
    {
        return PROTO_FALSE;
    }
    size_t n = (size_t)(gt - lt - 1);
    if (n + 1 > cap)
    {
        return PROTO_FALSE;
    }
    memcpy(out, lt + 1, n);
    out[n] = 0;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_WEBDAV
