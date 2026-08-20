// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file webdav.c
 * @brief WebDAV wire format (RFC 4918): method classification, header parsing,
 *        and the 207 Multi-Status XML builder. Pure - no sockets, no filesystem.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t hex_work[16]; // the borrow an entry takes; Hex never reads it

#if PROTOCORE_ENABLE_WEBDAV

#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h" // str.find: the scheme marker inside a Destination header
#include "network_drivers/application/webdav/webdav.h"
#include "shared/hex/hex.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void webdav_xml_escape(uint8_t *restrict work);

static void webdav_method(uint8_t *restrict work)
{
    (void)work;
    const char *m = Webdav.method_args.m;

    if (!m)
    {
        Webdav.value = DAV_M_UNSUPPORTED;
        return;
    }
    if (str.eq(m, "OPTIONS", sizeof("OPTIONS"), PROTO_FALSE))
    {
        Webdav.value = DAV_M_OPTIONS;
        return;
    }
    if (str.eq(m, "GET", sizeof("GET"), PROTO_FALSE))
    {
        Webdav.value = DAV_M_GET;
        return;
    }
    if (str.eq(m, "HEAD", sizeof("HEAD"), PROTO_FALSE))
    {
        Webdav.value = DAV_M_HEAD;
        return;
    }
    if (str.eq(m, "PUT", sizeof("PUT"), PROTO_FALSE))
    {
        Webdav.value = DAV_M_PUT;
        return;
    }
    if (str.eq(m, "DELETE", sizeof("DELETE"), PROTO_FALSE))
    {
        Webdav.value = DAV_M_DELETE;
        return;
    }
    if (str.eq(m, "PROPFIND", sizeof("PROPFIND"), PROTO_FALSE))
    {
        Webdav.value = DAV_M_PROPFIND;
        return;
    }
    if (str.eq(m, "PROPPATCH", sizeof("PROPPATCH"), PROTO_FALSE))
    {
        Webdav.value = DAV_M_PROPPATCH;
        return;
    }
    if (str.eq(m, "MKCOL", sizeof("MKCOL"), PROTO_FALSE))
    {
        Webdav.value = DAV_M_MKCOL;
        return;
    }
    if (str.eq(m, "COPY", sizeof("COPY"), PROTO_FALSE))
    {
        Webdav.value = DAV_M_COPY;
        return;
    }
    if (str.eq(m, "MOVE", sizeof("MOVE"), PROTO_FALSE))
    {
        Webdav.value = DAV_M_MOVE;
        return;
    }
    if (str.eq(m, "LOCK", sizeof("LOCK"), PROTO_FALSE))
    {
        Webdav.value = DAV_M_LOCK;
        return;
    }
    if (str.eq(m, "UNLOCK", sizeof("UNLOCK"), PROTO_FALSE))
    {
        Webdav.value = DAV_M_UNLOCK;
        return;
    }
    Webdav.value = DAV_M_UNSUPPORTED;
}

static void webdav_depth(uint8_t *restrict work)
{
    (void)work;
    const char *depth_hdr = Webdav.depth_args.depth_hdr;
    int dflt = Webdav.depth_args.dflt;

    if (!depth_hdr || !depth_hdr[0])
    {
        Webdav.i32 = dflt;
        return;
    }
    if (str.eq(depth_hdr, "0", sizeof("0"), PROTO_FALSE))
    {
        Webdav.i32 = 0;
        return;
    }
    if (str.eq(depth_hdr, "1", sizeof("1"), PROTO_FALSE))
    {
        Webdav.i32 = 1;
        return;
    }
    if (str.eq(depth_hdr, "infinity", sizeof("infinity"), PROTO_FALSE))
    {
        Webdav.i32 = PROTOCORE_DAV_DEPTH_INFINITY;
        return;
    }
    Webdav.i32 = dflt;
}

// Append a NUL-terminated string if it fits; returns false (leaving *len and the
// NUL terminator intact) when it would overflow.
static proto_bool app(char *buf, size_t cap, size_t *len, const char *s)
{
    size_t n = str.len(s, cap + 1);
    if (*len + n + 1 > cap)
    {
        return PROTO_FALSE;
    }
    mem.cpy(buf + *len, s, n);
    *len += n;
    buf[*len] = '\0';
    return PROTO_TRUE;
}

static void webdav_xml_escape(uint8_t *restrict work)
{
    (void)work;
    char *dst = Webdav.xml_escape_args.dst;
    size_t cap = Webdav.xml_escape_args.cap;
    const char *src = Webdav.xml_escape_args.src;

    size_t o = 0;
    if (cap == 0)
    {
        Webdav.n = 0;
        return;
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
            size_t rn = str.len(rep, cap + 1);
            if (o + rn + 1 > cap)
            {
                break;
            }
            mem.cpy(dst + o, rep, rn);
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
    Webdav.n = o;
}

static void webdav_dest_path(uint8_t *restrict work)
{
    (void)work;
    const char *destination = Webdav.dest_path_args.destination;
    char *out = Webdav.dest_path_args.out;
    size_t cap = Webdav.dest_path_args.cap;

    if (!destination || !out || cap == 0)
    {
        Webdav.ok = PROTO_FALSE;
        return;
    }

    // Skip an absolute-URI scheme + authority: after "://", advance to the first
    // '/' (the path). An abs-path value ("/p/q") is used as-is.
    const char *p = destination;
    const char *scheme = str.find(destination, MAX_VAL_LEN, "://", sizeof("://"), PROTO_FALSE);
    if (scheme)
    {
        p = scheme + 3;
        while (*p && *p != '/')
        {
            p++;
        }
        if (*p != '/')
        {
            Webdav.ok = PROTO_FALSE; // authority with no path
            return;
        }
    }
    else if (*p != '/')
    {
        Webdav.ok = PROTO_FALSE; // not an absolute path
        return;
    }

    // Percent-decode into out. A while loop so the %XX case can consume its two
    // extra hex digits without mutating a for-loop counter.
    size_t o = 0;
    while (*p)
    {
        char c = *p;
        if (c == '%')
        {
            HexV.args.ch = p[1];
            Hex.val(hex_work);
            const int hi = HexV.i8;
            int lo = -1;
            if (hi >= 0)
            {
                HexV.args.ch = p[2];
                Hex.val(hex_work);
                lo = HexV.i8;
            }
            if (hi < 0 || lo < 0)
            {
                Webdav.ok = PROTO_FALSE; // malformed escape
                return;
            }
            c = (char)((hi << 4) | lo);
            p += 2;
        }
        if (o + 1 >= cap)
        {
            Webdav.ok = PROTO_FALSE; // no room for char + NUL
            return;
        }
        out[o++] = c;
        p++;
    }
    out[o] = '\0';
    Webdav.ok = PROTO_TRUE;
}

static void webdav_ms_begin(uint8_t *restrict work)
{
    (void)work;
    char *buf = Webdav.ms_begin_args.buf;
    size_t cap = Webdav.ms_begin_args.cap;
    size_t len = Webdav.ms_begin_args.len;

    app(buf, cap, &len, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<D:multistatus xmlns:D=\"DAV:\">\n");
    Webdav.n = len;
}

static void webdav_ms_entry(uint8_t *restrict work)
{
    char *buf = Webdav.ms_entry_args.buf;
    size_t cap = Webdav.ms_entry_args.cap;
    size_t len = Webdav.ms_entry_args.len;
    const char *href = Webdav.ms_entry_args.href;
    proto_bool is_collection = Webdav.ms_entry_args.is_collection;
    uint32_t size = Webdav.ms_entry_args.size;
    const char *rfc1123_mtime = Webdav.ms_entry_args.rfc1123_mtime;
    const char *content_type = Webdav.ms_entry_args.content_type;

    // Build the whole <response> in a temp first so the append is atomic: a
    // partial element is never left in the document when the buffer fills.
    char tmp[512];
    size_t t = 0;
    char esc[256];

    Webdav.xml_escape_args.dst = esc;
    Webdav.xml_escape_args.cap = sizeof(esc);
    Webdav.xml_escape_args.src = href;
    webdav_xml_escape(work);
    // Open the response element and write the escaped href. The block runs at most 27 + esc(<=255)
    // + 66 == 348 bytes against tmp[512].
    if (!app(tmp, sizeof(tmp), &t, "  <D:response>\n    <D:href>") || !app(tmp, sizeof(tmp), &t, esc) ||
        !app(tmp, sizeof(tmp), &t, "</D:href>\n    <D:propstat>\n      <D:prop>\n        <D:resourcetype>"))
    {
        Webdav.n = len;
        return;
    }

    if (is_collection && !app(tmp, sizeof(tmp), &t, "<D:collection/>"))
    {
        Webdav.n = len;
        return;
    }
    if (!app(tmp, sizeof(tmp), &t, "</D:resourcetype>\n"))
    {
        Webdav.n = len;
        return;
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
        } while (s && rn < (int)sizeof(rev));
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
        if (!app(tmp, sizeof(tmp), &t, "        <D:getcontentlength>") || !app(tmp, sizeof(tmp), &t, num) ||
            !app(tmp, sizeof(tmp), &t, "</D:getcontentlength>\n"))
        {
            Webdav.n = len;
            return;
        }
        // content_type block. The append-overflow arm is unreachable per the budget above (running
        // total <=~446 < tmp[512]); gcov lumps the multi-app OR onto one line, so the whole merged
        // guard is excluded from coverage rather than carrying a misplaced per-line branch marker.
        if (content_type && content_type[0] &&
            (!app(tmp, sizeof(tmp), &t, "        <D:getcontenttype>") || !app(tmp, sizeof(tmp), &t, content_type) ||
             !app(tmp, sizeof(tmp), &t, "</D:getcontenttype>\n")))
        {
            Webdav.n = len;
            return;
        }
    }

    if (rfc1123_mtime && rfc1123_mtime[0])
    {
        if (!app(tmp, sizeof(tmp), &t, "        <D:getlastmodified>") || !app(tmp, sizeof(tmp), &t, rfc1123_mtime) ||
            !app(tmp, sizeof(tmp), &t, "</D:getlastmodified>\n"))
        {
            Webdav.n = len;
            return;
        }
    }

    if (!app(tmp, sizeof(tmp), &t,
             "      </D:prop>\n      <D:status>HTTP/1.1 200 OK</D:status>\n"
             "    </D:propstat>\n  </D:response>\n"))
    {
        Webdav.n = len;
        return;
    }

    // Atomic commit: app() appends the finished element only if it fits and leaves
    // len unchanged on no-room, so the caller sees an unchanged len and stops adding.
    app(buf, cap, &len, tmp);
    Webdav.n = len;
}

static void webdav_ms_end(uint8_t *restrict work)
{
    (void)work;
    char *buf = Webdav.ms_end_args.buf;
    size_t cap = Webdav.ms_end_args.cap;
    size_t len = Webdav.ms_end_args.len;

    app(buf, cap, &len, "</D:multistatus>\n");
    Webdav.n = len;
}

// True for a byte that ends an XML element name (whitespace, '/', '>').
static proto_bool name_end_char(char c)
{
    // The only caller scans a name span bounded by the '>' index (its tag-end loop stops there),
    // so this helper never sees '>'; that leg is a defensive, host-unreachable arm. gcov attributes
    // every operand's branch to this one line, so BR_LINE also drops the (exercised) ws/'/' arms.
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/' || c == '>';
}

static void webdav_proppatch_ms(uint8_t *restrict work)
{
    char *buf = Webdav.proppatch_ms_args.buf;
    size_t cap = Webdav.proppatch_ms_args.cap;
    const char *href = Webdav.proppatch_ms_args.href;
    const char *body = Webdav.proppatch_ms_args.body;
    size_t body_len = Webdav.proppatch_ms_args.body_len;

    size_t len = 0;
    if (cap)
    {
        buf[0] = '\0'; // always a valid C-string, even if nothing below fits
    }
    char esc[256];
    Webdav.xml_escape_args.dst = esc;
    Webdav.xml_escape_args.cap = sizeof(esc);
    Webdav.xml_escape_args.src = href;
    webdav_xml_escape(work);
    if (!app(buf, cap, &len,
             "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<D:multistatus xmlns:D=\"DAV:\">\n"
             "  <D:response>\n    <D:href>") ||
        !app(buf, cap, &len, esc) || !app(buf, cap, &len, "</D:href>\n    <D:propstat>\n      <D:prop>\n"))
    {
        Webdav.n = 0;
        return;
    }

    // Walk the request and echo every element that sits directly inside a <prop>
    // (across all <set>/<remove> blocks) as a self-closed element. The wrappers
    // (propertyupdate / set / remove / prop) are skipped; only the properties are
    // reflected, each refused 403 below.
    int emitted = 0;
    proto_bool in_prop = PROTO_FALSE;
    size_t i = 0;
    while (i < body_len && emitted < PROTOCORE_WEBDAV_MAX_PROPS)
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
        proto_bool is_prop = (name_end - local) == 4 && str.diff(&body[local], "prop", 4, PROTO_FALSE) == 4;

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
                mem.cpy(tag, &body[start], tl);
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
        Webdav.n = 0;
        return;
    }
    Webdav.n = len;
}

// ── lock manager (RFC 4918 §6-7) ───────────────────────────────────────────────────────────────

// Copy src into dst[cap], NUL-terminated; false if it does not fit.
static proto_bool dav_lock_copy(char *dst, size_t cap, const char *src)
{
    size_t n = str.len(src, cap);
    if (n + 1 > cap)
    {
        return PROTO_FALSE;
    }
    mem.cpy(dst, src, n + 1);
    return PROTO_TRUE;
}

// Normalize a path into dst, stripping trailing '/' (but keeping a lone root "/"). false on overflow.
static proto_bool dav_lock_norm(char *dst, size_t cap, const char *path)
{
    size_t n = str.len(path, cap);
    while (n > 1 && path[n - 1] == '/') // drop trailing slashes so "/a/" and "/a" are one resource
    {
        n--;
    }
    if (n + 1 > cap)
    {
        return PROTO_FALSE;
    }
    mem.cpy(dst, path, n);
    dst[n] = 0;
    return PROTO_TRUE;
}

// True if `child` equals `parent` or lies (at a segment boundary) under it. Both trailing-slash-normalized.
static proto_bool dav_lock_same_or_under(const char *parent, const char *child)
{
    size_t pn = str.len(parent, PROTOCORE_DAV_LOCK_PATH_MAX);
    if (str.diff(parent, child, pn, PROTO_FALSE) != pn)
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
    if (str.eq(pa, pb, PROTOCORE_DAV_LOCK_PATH_MAX, PROTO_FALSE))
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
    if (str.eq(l->path, np, sizeof(l->path), PROTO_FALSE))
    {
        return PROTO_TRUE;
    }
    return l->depth_infinity && dav_lock_same_or_under(l->path, np);
}

static void webdav_lock_init(uint8_t *restrict work)
{
    (void)work;
    DavLockTable *t = Webdav.lock_init_args.t;

    if (!t)
    {
        return;
    }
    for (size_t i = 0; i < PROTOCORE_DAV_LOCK_MAX; i++)
    {
        t->locks[i].active = PROTO_FALSE;
    }
}

static void webdav_lock_acquire(uint8_t *restrict work)
{
    (void)work;
    DavLockTable *t = Webdav.lock_acquire_args.t;
    const char *path = Webdav.lock_acquire_args.path;
    const char *token = Webdav.lock_acquire_args.token;
    proto_bool exclusive = Webdav.lock_acquire_args.exclusive;
    proto_bool depth_infinity = Webdav.lock_acquire_args.depth_infinity;
    uint32_t expiry_s = Webdav.lock_acquire_args.expiry_s;

    if (!t || !path || !token)
    {
        Webdav.ptr = NULL;
        return;
    }
    char np[PROTOCORE_DAV_LOCK_PATH_MAX];
    if (!dav_lock_norm(np, sizeof(np), path))
    {
        Webdav.ptr = NULL;
        return;
    }
    if (str.len(token, PROTOCORE_DAV_LOCK_TOKEN_MAX) + 1 > PROTOCORE_DAV_LOCK_TOKEN_MAX) // token would not fit
    {
        Webdav.ptr = NULL;
        return;
    }

    // Conflict: an exclusive request clashes with any overlapping lock; a shared one only with an
    // overlapping exclusive lock (two shared locks may coexist).
    for (size_t i = 0; i < PROTOCORE_DAV_LOCK_MAX; i++)
    {
        const DavLock *l = &t->locks[i];
        if (l->active && dav_lock_overlap(l->path, l->depth_infinity, np, depth_infinity) &&
            (exclusive || l->exclusive))
        {
            Webdav.ptr = NULL;
            return;
        }
    }

    for (size_t i = 0; i < PROTOCORE_DAV_LOCK_MAX; i++)
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
        Webdav.ptr = l;
        return;
    }
    Webdav.ptr = NULL; // table full
}

static void webdav_lock_sweep(uint8_t *restrict work)
{
    (void)work;
    DavLockTable *t = Webdav.lock_sweep_args.t;
    uint32_t now_s = Webdav.lock_sweep_args.now_s;

    if (!t)
    {
        Webdav.n = 0;
        return;
    }
    size_t dropped = 0;
    for (size_t i = 0; i < PROTOCORE_DAV_LOCK_MAX; i++)
    {
        DavLock *l = &t->locks[i];
        if (l->active && l->expiry_s != 0 && l->expiry_s <= now_s) // 0 = never expires
        {
            l->active = PROTO_FALSE;
            dropped++;
        }
    }
    Webdav.n = dropped;
}

static void webdav_lock_refresh(uint8_t *restrict work)
{
    (void)work;
    DavLockTable *t = Webdav.lock_refresh_args.t;
    const char *token = Webdav.lock_refresh_args.token;
    uint32_t new_expiry_s = Webdav.lock_refresh_args.new_expiry_s;

    if (!t || !token)
    {
        Webdav.ptr = NULL;
        return;
    }
    for (size_t i = 0; i < PROTOCORE_DAV_LOCK_MAX; i++)
    {
        DavLock *l = &t->locks[i];
        if (l->active && str.eq(l->token, token, sizeof(l->token), PROTO_FALSE))
        {
            l->expiry_s = new_expiry_s;
            Webdav.ptr = l;
            return;
        }
    }
    Webdav.ptr = NULL;
}

static void webdav_lock_find(uint8_t *restrict work)
{
    (void)work;
    const DavLockTable *t = Webdav.lock_find_args.t;
    const char *path = Webdav.lock_find_args.path;

    if (!t || !path)
    {
        Webdav.ptr = NULL;
        return;
    }
    char np[PROTOCORE_DAV_LOCK_PATH_MAX];
    if (!dav_lock_norm(np, sizeof(np), path))
    {
        Webdav.ptr = NULL;
        return;
    }
    for (size_t i = 0; i < PROTOCORE_DAV_LOCK_MAX; i++)
    {
        if (t->locks[i].active && dav_lock_covers(&t->locks[i], np))
        {
            Webdav.ptr = &t->locks[i];
            return;
        }
    }
    Webdav.ptr = NULL;
}

static void webdav_lock_release(uint8_t *restrict work)
{
    (void)work;
    DavLockTable *t = Webdav.lock_release_args.t;
    const char *token = Webdav.lock_release_args.token;

    if (!t || !token)
    {
        Webdav.ok = PROTO_FALSE;
        return;
    }
    for (size_t i = 0; i < PROTOCORE_DAV_LOCK_MAX; i++)
    {
        if (t->locks[i].active && str.eq(t->locks[i].token, token, sizeof(t->locks[i].token), PROTO_FALSE))
        {
            t->locks[i].active = PROTO_FALSE;
            Webdav.ok = PROTO_TRUE;
            return;
        }
    }
    Webdav.ok = PROTO_FALSE;
}

static void webdav_lock_can_write(uint8_t *restrict work)
{
    (void)work;
    const DavLockTable *t = Webdav.lock_can_write_args.t;
    const char *path = Webdav.lock_can_write_args.path;
    const char *presented_token = Webdav.lock_can_write_args.presented_token;

    if (!t)
    {
        Webdav.ok = PROTO_TRUE; // no table => nothing is locked
        return;
    }
    if (!path)
    {
        Webdav.ok = PROTO_FALSE;
        return;
    }
    char np[PROTOCORE_DAV_LOCK_PATH_MAX];
    if (!dav_lock_norm(np, sizeof(np), path))
    {
        Webdav.ok = PROTO_TRUE; // an unparseable path is not something the lock table can guard
        return;
    }
    proto_bool covered = PROTO_FALSE;
    for (size_t i = 0; i < PROTOCORE_DAV_LOCK_MAX; i++)
    {
        const DavLock *l = &t->locks[i];
        if (!l->active || !dav_lock_covers(l, np))
        {
            continue;
        }
        covered = PROTO_TRUE;
        if (presented_token && str.eq(l->token, presented_token, sizeof(l->token), PROTO_FALSE))
        {
            Webdav.ok = PROTO_TRUE; // the request holds a covering lock's token
            return;
        }
    }
    Webdav.ok = !covered; // unlocked => allowed; locked with no / wrong token => denied
}

static void webdav_if_token(uint8_t *restrict work)
{
    (void)work;
    const char *if_header = Webdav.if_token_args.if_header;
    char *out = Webdav.if_token_args.out;
    size_t cap = Webdav.if_token_args.cap;

    if (!if_header || !out || cap == 0)
    {
        Webdav.ok = PROTO_FALSE;
        return;
    }
    // The state tokens live inside a condition list "( ... )"; take the first Coded-URL "<...>" within it
    // (which also skips the tagged-list resource URL that precedes the '(').
    // The terminator is what bounds the scans: finding it first is what makes the remaining length
    // at each marker a readable extent rather than an assumption about the caller's buffer.
    const size_t if_len = str.len(if_header, SIZE_MAX);
    const char *lp = str.find(if_header, if_len, "(", sizeof("("), PROTO_FALSE);
    if (!lp)
    {
        Webdav.ok = PROTO_FALSE;
        return;
    }
    const char *lt = str.find(lp, if_len - (size_t)(lp - if_header), "<", sizeof("<"), PROTO_FALSE);
    if (!lt)
    {
        Webdav.ok = PROTO_FALSE;
        return;
    }
    const char *gt = str.find(lt, if_len - (size_t)(lt - if_header), ">", sizeof(">"), PROTO_FALSE);
    if (!gt)
    {
        Webdav.ok = PROTO_FALSE;
        return;
    }
    size_t n = (size_t)(gt - lt - 1);
    if (n + 1 > cap)
    {
        Webdav.ok = PROTO_FALSE;
        return;
    }
    mem.cpy(out, lt + 1, n);
    out[n] = 0;
    Webdav.ok = PROTO_TRUE;
}

WebdavNs Webdav = {
    .method = webdav_method,
    .depth = webdav_depth,
    .xml_escape = webdav_xml_escape,
    .dest_path = webdav_dest_path,
    .ms_begin = webdav_ms_begin,
    .ms_entry = webdav_ms_entry,
    .ms_end = webdav_ms_end,
    .proppatch_ms = webdav_proppatch_ms,
    .lock_init = webdav_lock_init,
    .lock_acquire = webdav_lock_acquire,
    .lock_sweep = webdav_lock_sweep,
    .lock_refresh = webdav_lock_refresh,
    .lock_find = webdav_lock_find,
    .lock_release = webdav_lock_release,
    .lock_can_write = webdav_lock_can_write,
    .if_token = webdav_if_token,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WEBDAV
