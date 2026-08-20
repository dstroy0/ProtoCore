// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file webdav_handler.c
 * @brief WebDAV (RFC 4918) filesystem-backed request handling.
 *
 * The pure core - method classification, the 207 Multi-Status XML builder, header parsing - lives
 * in network_drivers/application/webdav/; this file is the half that needs a real filesystem
 * (PROPFIND/PUT/COPY/MOVE over a mounted subtree). WEBDAV requires FILE_SERVING, so the
 * file-serving helpers it calls are always present.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t mnt_work[16]; // the borrow an entry takes; Mnt never reads it

static uint8_t webdav_work[16]; // the borrow an entry takes; Webdav never reads it

#if PROTOCORE_ENABLE_WEBDAV

#include "crypto/rng/rng.h" // Rng: the lock token's unpredictable half
#include "mmgr/membuild/membuild.h"
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h" // str.has: the traversal marker in a resolved subpath, tokens in a body
#include "mmgr/secure/secure.h"     // the persistent end this module's state is taken from
#include "network_drivers/application/webdav/webdav.h"
#include "network_drivers/presentation/http/http.h"
#include "network_drivers/presentation/http/http_parser/http_parser.h"
#include "network_drivers/presentation/http/route/http_route.h"
#include "network_drivers/transport/tcp/tcp.h"
#include "protocore.h"
#include "server/clock/clock.h"
#include "server/io/webdav_handler.h"
#include "shared/mime/mime.h"

PROTOCORE_BEGIN_DECLS

// The parser's streaming-body sink is a single global hook (http_parser_set_stream_hooks): the last
// registrar wins, so an OTA or upload service registering after dav() takes the sink away and a
// bodied PUT to a DAV route buffers (bounded by BODY_BUF_SIZE) instead of streaming. The
// buffered-PUT fallback below assumes that cannot happen, so the combination is rejected here.
#if PROTOCORE_ENABLE_OTA || PROTOCORE_ENABLE_UPLOAD
#error                                                                                                                 \
    "PROTOCORE_ENABLE_WEBDAV cannot be combined with PROTOCORE_ENABLE_OTA or PROTOCORE_ENABLE_UPLOAD: the parser's \
streaming-body sink is a single global hook, so whichever registers last silently disables the others."
#endif

// Floor on the bytes one <D:response> costs. The fixed text of protocore_webdav_ms_entry is 204 (27 href
// prologue + 66 prop/resourcetype opening + 18 resourcetype close + 93 propstat/response close) and
// the href adds at least one more, so 192 under-states every real element. That makes
// BUF_SIZE / 192 an over-estimate of how many entries the buffer holds, and the assert below still
// requires it to come out under MAX_ENTRIES - which is what keeps the buffer, not the count, the
// bound the Depth-1 PROPFIND listing loop stops on.
#define PROTOCORE_WEBDAV_MIN_ENTRY_BYTES 192u
static_assert(PROTOCORE_WEBDAV_BUF_SIZE / PROTOCORE_WEBDAV_MIN_ENTRY_BYTES < PROTOCORE_WEBDAV_MAX_ENTRIES,
              "PROTOCORE_WEBDAV_BUF_SIZE is large enough to hold PROTOCORE_WEBDAV_MAX_ENTRIES entries: raise "
              "PROTOCORE_WEBDAV_MAX_ENTRIES or lower PROTOCORE_WEBDAV_BUF_SIZE so the buffer bound stays the "
              "binding one (see the PROPFIND listing loop).");

#if PROTOCORE_ENABLE_STREAM_BODY
// Per-connection streaming-PUT state: each slot streams its body to its own file, so concurrent
// PUTs never clobber one another and a transfer is never bounded by BODY_BUF_SIZE. Indexed by the
// request's slot (req - http_pool).
typedef struct
{
    int fh;             ///< accessor handle for this slot's destination file; only valid while active.
    proto_bool active;  ///< file opened for the current PUT.
    proto_bool error;   ///< a write (or the open) failed.
    proto_bool existed; ///< target existed before this PUT (204 vs 201).
    proto_bool locked;  ///< a lock blocked this PUT: consume the body but write nothing, then answer 423.
    size_t written;     ///< bytes written so far.
} DavPut;
#endif // PROTOCORE_ENABLE_STREAM_BODY

// All WebDAV state, owned by one instance (internal linkage): the accessor root, the 207
// Multi-Status build buffer, one directory entry's name, the per-slot streaming-PUT table, and
// the lock table.
typedef struct
{
    // The accessor root every operation here resolves against, bound in dav(). It is the whole
    // mount: a DAV route carries its own subtree as a request-path piece (the mount point), so
    // the subtree is part of the request and one root serves every mount registered.
    int root;
    proto_bool bound; ///< dav() ran; until it has, the root reports -1

    char buf[PROTOCORE_WEBDAV_BUF_SIZE];

    // One directory entry's own name, for the Depth-1 PROPFIND listing - the only thing here that
    // walks anything, and it walks exactly one level. Removing and copying a tree are the
    // accessor's operations (protocore_fs_remove, protocore_fs_copy), so neither needs a stack here.
    char child[PROTOCORE_FILESYSTEM_PATH_MAX];

#if PROTOCORE_ENABLE_STREAM_BODY
    // One streaming-PUT destination per connection slot, so concurrent PUTs never clobber one
    // another. The only file handle held across calls: opened by one callback, written by another,
    // and closed by the handler.
    DavPut put[MAX_CONNS];
#endif

    // The server-global lock table (RFC 4918 sections 6-7). Zeroed, so every slot starts inactive
    // and nothing is locked until a LOCK stores a token.
    DavLockTable table;
} DavCtx;

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define WEBDAV_HANDLER_OFF_CTX 0u
static_assert(WEBDAV_HANDLER_OFF_CTX + sizeof(DavCtx) <= PROTOCORE_WEBDAV_BORROW,
              "PROTOCORE_WEBDAV_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    WEBDAV_HANDLER_OFF_CTX % _Alignof(DavCtx) == 0,
    "WEBDAV_HANDLER_OFF_CTX is not a multiple of alignof(DavCtx) - WEBDAV_HANDLER_CTX() would return a misaligned "
    "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define WEBDAV_HANDLER_CTX(w) ((DavCtx *)(void *)((w) + WEBDAV_HANDLER_OFF_CTX))

// The accessor root, or -1 for "not bound yet". Stated here rather than as an initializer so the
// context carries none and can live in a borrow that arrives zeroed. It takes a flag rather than a
// sentinel value because root 0 is a valid root, so zero cannot mean unset - a zeroed field would
// otherwise resolve every path against somebody else's storage before dav() ever ran.
static int dav_root(uint8_t *restrict work)
{
    return WEBDAV_HANDLER_CTX(work)->bound ? WEBDAV_HANDLER_CTX(work)->root : -1;
}

// Join an FS root and a subpath into @p out (the separator handling serve_static_request uses).
// Returns false on overflow.
static proto_bool dav_join(const char *root, const char *sub, char *out, size_t cap)
{
    size_t rlen = str.len(root, MAX_PATH_LEN);
    proto_bool root_slash = (rlen > 0 && root[rlen - 1] == '/');
    if (root_slash && sub[0] == '/')
    {
        sub++;
    }
    proto_bool sub_slash = (sub[0] == '/');
    const char *sep = (root_slash || sub_slash) ? "" : "/";
    protocore_sb sb_out = {out, cap, 0, PROTO_TRUE};
    Sb.put(&sb_out, root);
    Sb.put(&sb_out, sep);
    Sb.put(&sb_out, sub);
    int wn = (int)Sb.finish(&sb_out);
    // wn <= 0 cannot fire: snprintf only returns negative on an encoding error, which "%s%s%s"
    // cannot raise, and sep is "/" whenever root and sub are both empty, so the shortest join is
    // one byte. The truncation half (wn >= cap) is exercised.
    return wn > 0 && wn < (int)cap;
}

// Map a WebDAV request path to its on-disk path under the mount @p r. Strips the
// mount prefix, rejects traversal, joins onto the FS root, and drops a trailing
// '/'. Returns 0 on success, else the HTTP error code (403 traversal, 414 too
// long) - the single source of truth for the path check, shared by the request
// handler and the streaming-PUT begin hook.
static int dav_resolve_path(const HttpRoute *r, const char *reqpath, char *out, size_t cap)
{
    size_t plen = str.len(r->path, MAX_PATH_LEN);
    // plen == 0 is unreachable: dav() always stores at least "*" - it appends the wildcard when the
    // prefix lacks one, so even dav("") yields a one-character pattern.
    if (plen > 0 && r->path[plen - 1] == '*')
    {
        plen--;
    }
    // Http.path_matches() against this same route, which already required reqpath to carry the mount
    // prefix, so the length test always holds. Kept so a future caller that resolves without
    // matching first still cannot index past the end of reqpath.
    const char *sub = (str.len(reqpath, MAX_PATH_LEN) >= plen) ? reqpath + plen : "";
    if (str.has(sub, MAX_PATH_LEN - plen, "..", sizeof(".."), PROTO_FALSE))
    {
        return 403;
    }
    Mnt.args.id = r->mnt_id;
    Mnt.root_of(mnt_work);
    const char *root = Mnt.text;
    if (!dav_join(root, sub, out, cap))
    {
        return 414;
    }
    size_t fpl = str.len(out, cap);
    if (fpl > 1 && out[fpl - 1] == '/')
    {
        out[fpl - 1] = '\0';
    }
    return 0;
}

#if PROTOCORE_ENABLE_STREAM_BODY
// The server-global lock table (RFC 4918 §6-7). Zero-initialized, so every slot starts inactive and

// True if a write to the URL @p path is blocked by a lock the request does not present a token for. The
// token, if any, comes from the request's If header (RFC 4918 §10.4 / §7).
static proto_bool dav_write_blocked(uint8_t *restrict work, HttpReq *req, const char *path)
{
    HttpParserV.get_header_args.req = req;
    HttpParserV.get_header_args.key = "If";
    HttpParser.get_header(protocore_http_parser_span());
    const char *if_hdr = HttpParserV.text;
    char tok[PROTOCORE_DAV_LOCK_TOKEN_MAX];
    const char *presented = NULL;
    if (if_hdr)
    {
        Webdav.if_token_args.if_header = if_hdr;
        Webdav.if_token_args.out = tok;
        Webdav.if_token_args.cap = sizeof(tok);
        Webdav.if_token(webdav_work);
        presented = Webdav.ok ? tok : NULL;
    }
    Webdav.lock_can_write_args.t = &WEBDAV_HANDLER_CTX(work)->table;
    Webdav.lock_can_write_args.path = path;
    Webdav.lock_can_write_args.presented_token = presented;
    Webdav.lock_can_write(webdav_work);
    return !Webdav.ok;
}

// True if the (always NUL-terminated) request body contains @p needle - used to spot a <shared> lockscope.
static proto_bool dav_body_has(HttpReq *req, const char *needle)
{
    return str.has((const char *)req->body, req->body_len + 1u, needle, str.len(needle, 0xFFFF) + 1u, PROTO_FALSE);
}

// Extract the token from a Lock-Token Coded-URL ("<opaquelocktoken:...>") into @p out; false if malformed.
static proto_bool dav_coded_url_token(const char *coded, char *out, size_t cap)
{
    const char *lt = str.find(coded, MAX_VAL_LEN, "<", sizeof("<"), PROTO_FALSE);
    if (!lt)
    {
        return PROTO_FALSE;
    }
    const char *gt = str.find(lt, MAX_VAL_LEN - (size_t)(lt - coded), ">", sizeof(">"), PROTO_FALSE);
    if (!gt)
    {
        return PROTO_FALSE;
    }
    size_t n = (size_t)(gt - lt - 1);
    if (n + 1 > cap)
    {
        return PROTO_FALSE;
    }
    mem.cpy(out, lt + 1, n);
    out[n] = 0;
    return PROTO_TRUE;
}

// Registered with http_parser_set_stream_hooks in dav().
static void dav_put_abort_tramp(HttpReq *req)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_webdav_handler_span();

    // The PUT was torn down before the handler ran: close the half-written file so
    // the handle is not leaked (a leak eventually exhausts the filesystem's open slots).
    uint8_t slot = (uint8_t)(req - http_pool);
    // long, but the streaming-body hooks are driven only by the HTTP/1.x byte parser, which never
    // parses for the internal dispatch slots at and above MAX_CONNS. WEBDAV_HANDLER_CTX(work)->put[] is MAX_CONNS
    // long, so the bound still has to be tested here.
    if (slot < MAX_CONNS && WEBDAV_HANDLER_CTX(work)->put[slot].active)
    {
        Fs.io.handle = WEBDAV_HANDLER_CTX(work)->put[slot].fh;
        Fs.close(protocore_filesystem_span());
        WEBDAV_HANDLER_CTX(work)->put[slot].active = PROTO_FALSE;
    }
}

static proto_bool dav_stream_put_begin(HttpReq *req)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_webdav_handler_span();

    if (!str.eq(req->method, "PUT", sizeof("PUT"), PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    uint8_t slot = (uint8_t)(req - http_pool);
    HttpRoutes.count(protocore_http_route_span());
    for (uint8_t i = 0; i < HttpRoutesV.value; i++)
    {
        HttpRoutesV.at_args.i = i;
        HttpRoutes.at(protocore_http_route_span());
        HttpRoute *r = HttpRoutesV.ptr;
        // The !is_active half cannot fire: every entry below route_count was filled by
        // fill_route_base, which sets is_active, and nothing ever clears it again.
        if (!r->is_active || r->type != ROUTE_DAV)
        {
            continue;
        }
        HttpV.route_args.route = r->path;
        HttpV.route_args.is_wildcard = r->is_wildcard;
        HttpV.route_args.path = req->path;
        Http.path_matches(protocore_http_span());
        if (!HttpV.ok)
        {
            continue;
        }
        ConnPool.slot = slot;
        ConnPool.iface(protocore_conn_pool_span());
        if (r->iface_filter != PROTOCORE_IF_ANY && r->iface_filter != ConnPool.if_kind)
        {
            continue;
        }
        char fs_path[256];
        if (dav_resolve_path(r, req->path, fs_path, sizeof(fs_path)) != 0)
        {
            return PROTO_FALSE; // traversal / too long - let it buffer; the handler answers 403/414
        }
        DavPut *d = &WEBDAV_HANDLER_CTX(work)->put[slot];
        d->active = PROTO_FALSE;
        d->error = PROTO_FALSE;
        d->locked = PROTO_FALSE;
        d->written = 0;
        if (dav_write_blocked(work, req, req->path))
        {
            // Locked by another principal: consume the body but open no file, so the resource is not
            // touched; the PUT handler answers 423 (RFC 4918 §7).
            d->locked = PROTO_TRUE;
            return PROTO_TRUE;
        }
        Fs.path.root = dav_root(work);
        Fs.path.dir = fs_path;
        Fs.path.name = "";
        Fs.exists(protocore_filesystem_span());
        d->existed = Fs.ok;
        Fs.path.root = dav_root(work);
        Fs.path.dir = fs_path;
        Fs.path.name = "";
        Fs.io.mode = PROTOCORE_MNT_WRITE;
        Fs.open(protocore_filesystem_span());
        d->fh = Fs.i32;
        if (d->fh >= 0)
        {
            d->active = PROTO_TRUE;
        }
        else
        {
            d->error = PROTO_TRUE;
        }
        return PROTO_TRUE; // stream regardless so the body is consumed and the handler replies
    }
    return PROTO_FALSE;
}

static void dav_stream_put_data(HttpReq *req, const uint8_t *data, size_t len)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_webdav_handler_span();

    uint8_t slot = (uint8_t)(req - http_pool);
    if (slot >= MAX_CONNS)
    {
        return;
    }
    DavPut *d = &WEBDAV_HANDLER_CTX(work)->put[slot];
    if (d->active && !d->error)
    {
        Fs.io.handle = d->fh;
        Fs.io.wbuf = data;
        Fs.io.n = len;
        Fs.write(protocore_filesystem_span());
        if (Fs.i32 != (int)len)
        {
            d->error = PROTO_TRUE;
        }
        else
        {
            d->written += len;
        }
    }
}
#endif // PROTOCORE_ENABLE_STREAM_BODY

void dav(const char *url_prefix, const protocore_mnt_backend *file_sys, const char *fs_root)
{
    // Public API with a signature protocore.h fixes, so the borrow comes from the accessor rather
    // than a parameter - the same way a callback reaches it.
    uint8_t *restrict work = protocore_webdav_handler_span();
    HttpRoutes.add(protocore_http_route_span());
    HttpRoute *r = HttpRoutesV.ptr;
    if (r == NULL)
    {
        return;
    }

    char pat[MAX_PATH_LEN];
    size_t n = str.len(url_prefix, MAX_PATH_LEN);
    if (n > 0 && url_prefix[n - 1] == '*')
    {
        protocore_sb sb_pat = {pat, sizeof(pat), 0, PROTO_TRUE};
        Sb.put(&sb_pat, url_prefix);
        if (Sb.finish(&sb_pat) == 0)
        {
            pat[0] = '\0';
        }
    }
    else
    {
        protocore_sb sb_pat2 = {pat, sizeof(pat), 0, PROTO_TRUE};
        Sb.put(&sb_pat2, url_prefix);
        Sb.put(&sb_pat2, "*");
        if (Sb.finish(&sb_pat2) == 0)
        {
            pat[0] = '\0';
        }
    }
    fill_route_base(r, pat);
    r->type = ROUTE_DAV;
    r->method = HTTP_GET; // unused: WebDAV dispatch keys off the raw method token
    Mnt.args.backend = file_sys;
    Mnt.args.root = fs_root;
    Mnt.point_add(mnt_work); // null backend is legal: whatever is mounted
    r->mnt_id = Mnt.u8;

    // Bind the root every operation in this file resolves against. Re-binding a name already bound
    // hands back the same handle, so a second mount costs nothing and both see the same storage.
    Fs.mount = "/";
    Fs.begin(protocore_filesystem_span());
    WEBDAV_HANDLER_CTX(work)->root = Fs.i32;
    WEBDAV_HANDLER_CTX(work)->bound = PROTO_TRUE;

#if PROTOCORE_ENABLE_STREAM_BODY
    // Stream PUT bodies straight to the file (one global sink; see PROTOCORE_ENABLE_STREAM_BODY).
    HttpParserV.set_stream_hooks_args.begin = dav_stream_put_begin;
    HttpParserV.set_stream_hooks_args.data = dav_stream_put_data;
    HttpParserV.set_stream_hooks_args.abort = dav_put_abort_tramp;
    HttpParser.set_stream_hooks(protocore_http_parser_span());
#endif
}

static void serve_dav_request(uint8_t *restrict work, uint8_t slot_id, HttpReq *req, const HttpRoute *r);

static void dav_send_status(uint8_t slot_id, int code, const char *extra_headers)
{
    ConnPool.slot = slot_id;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPool.ok)
    {
        HttpParserV.reset_args.req = &http_pool[slot_id];
        HttpParser.reset(protocore_http_parser_span());
        return;
    }
    proto_bool keep;
    const char *cl = protocore_resp_conn_hdr(slot_id, &keep);
    char header[RESP_HDR_BUF_SIZE];
    // in this file passes either "" or a string literal. Kept so the parameter stays optional.
    protocore_sb sb_header = {header, sizeof(header), 0, PROTO_TRUE};
    Sb.put(&sb_header, "HTTP/1.1 ");
    Sb.i64(&sb_header, (int64_t)(code));
    Sb.put(&sb_header, " ");
    HttpV.code = code;
    Http.status_text(protocore_http_span());
    Sb.put(&sb_header, HttpV.text);
    Sb.put(&sb_header, "\r\n");
    Sb.put(&sb_header, extra_headers ? extra_headers : "");
    Sb.put(&sb_header, "Content-Length: 0\r\n");
    Sb.put(&sb_header, cl);
    Sb.put(&sb_header, "\r\n");
    int hlen = (int)Sb.finish(&sb_header);
    ConnPool.slot = slot_id;
    ConnPool.io.data = header;
    ConnPool.io.len = (proto_u16)hlen;
    ConnPool.send(protocore_conn_pool_span());
    protocore_resp_end(slot_id, code, 0, keep, /*pre_flushed=*/PROTO_FALSE);
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_WEBDAV_BORROW persistent bytes
} DavOwnCtx;
static DavOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_webdav_handler_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_WEBDAV_BORROW).buf;
    }
    return s_own.span;
}

static void webdav_handler_try_serve_dav(uint8_t *restrict work)
{
    uint8_t slot_id = Dav.try_serve_dav_args.slot_id;
    HttpReq *req = Dav.try_serve_dav_args.req;

    HttpRoutes.count(protocore_http_route_span());
    for (uint8_t i = 0; i < HttpRoutesV.value; i++)
    {
        HttpRoutesV.at_args.i = i;
        HttpRoutes.at(protocore_http_route_span());
        HttpRoute *r = HttpRoutesV.ptr;
        // The !is_active half cannot fire: every entry below route_count was filled by
        // fill_route_base, which sets is_active, and nothing ever clears it again.
        if (!r->is_active || r->type != ROUTE_DAV)
        {
            continue;
        }
        HttpV.route_args.route = r->path;
        HttpV.route_args.is_wildcard = r->is_wildcard;
        HttpV.route_args.path = req->path;
        Http.path_matches(protocore_http_span());
        if (!HttpV.ok)
        {
            continue;
        }
        ConnPool.slot = slot_id;
        ConnPool.iface(protocore_conn_pool_span());
        if (r->iface_filter != PROTOCORE_IF_ANY && r->iface_filter != ConnPool.if_kind)
        {
            continue;
        }
        serve_dav_request(work, slot_id, req, r);
        Dav.ok = PROTO_TRUE;
        return;
    }
    Dav.ok = PROTO_FALSE;
}

static void serve_dav_request(uint8_t *restrict work, uint8_t slot_id, HttpReq *req, const HttpRoute *r)
{
    char fs_path[256];
    int rc = dav_resolve_path(r, req->path, fs_path, sizeof(fs_path));
    if (rc != 0)
    {
        dav_send_status(slot_id, rc, ""); // 403 traversal / 414 too long
        return;
    }

    // Mount-prefix length and FS root, used by COPY/MOVE to resolve the Destination. As in
    // dav_resolve_path, plen == 0 is unreachable: dav() always stores at least "*".
    size_t plen = str.len(r->path, MAX_PATH_LEN);
    if (plen > 0 && r->path[plen - 1] == '*')
    {
        plen--;
    }
    Mnt.args.id = r->mnt_id;
    Mnt.root_of(mnt_work);
    const char *root = Mnt.text;

    // Expire any timed-out locks (RFC 4918 §6.6) before this request consults the table, so a stale lock
    // never gates a write. The clock is protocore_millis() (pluggable); seconds are enough for lock lifetimes.
    uint32_t dav_now_s = (uint32_t)(Clock.ms / 1000u);
    Webdav.lock_sweep_args.t = &WEBDAV_HANDLER_CTX(work)->table;
    Webdav.lock_sweep_args.now_s = dav_now_s;
    Webdav.lock_sweep(webdav_work);

    Webdav.method_args.m = req->method;
    Webdav.method(webdav_work);
    switch (Webdav.value)
    {
    case DAV_M_OPTIONS:
        proto_add_response_header(slot_id, "DAV", "1, 2");
        proto_add_response_header(
            slot_id, "Allow", "OPTIONS, GET, HEAD, PUT, DELETE, PROPFIND, PROPPATCH, MKCOL, COPY, MOVE, LOCK, UNLOCK");
        proto_add_response_header(slot_id, "MS-Author-Via", "DAV");
        send_empty(slot_id, 200);
        return;

    case DAV_M_GET:
    case DAV_M_HEAD: {
        // One stat answers both questions this method asks: does it exist, and is it a collection.
        protocore_mnt_stat gst;
        Fs.path.root = dav_root(work);
        Fs.path.dir = fs_path;
        Fs.path.name = "";
        Fs.io.stat = &gst;
        Fs.stat(protocore_filesystem_span());
        if (!Fs.ok)
        {
            dav_send_status(slot_id, 404, "");
            return;
        }
        if (gst.is_dir)
        {
            dav_send_status(slot_id, 405, ""); // GET on a collection is not a download
            return;
        }
        Mnt.args.id = r->mnt_id;
        Mnt.point_of(mnt_work);
        Webdav.method_args.m = req->method;
        Webdav.method(webdav_work);
        FileServingV.serve_file_internal_args.slot_id = slot_id;
        FileServingV.serve_file_internal_args.head = Webdav.value == DAV_M_HEAD;
        FileServingV.serve_file_internal_args.file_sys = Mnt.backend;
        FileServingV.serve_file_internal_args.fs_path = fs_path;
        FileServingV.serve_file_internal_args.content_type = mime_type(fs_path);
        FileServingV.serve_file_internal_args.content_encoding = NULL;
        FileServing.serve_file_internal(protocore_file_serving_span());
        return;
    }

    case DAV_M_PUT: {
#if PROTOCORE_ENABLE_STREAM_BODY
        if (req->body_streaming)
        {
            // The body was written to this slot's file as it arrived (dav_stream_put_*).
            DavPut *d = &WEBDAV_HANDLER_CTX(work)->put[slot_id];
            if (d->locked)
            {
                d->locked = PROTO_FALSE;
                dav_send_status(slot_id, 423, ""); // Locked: the body was consumed but nothing was written
                return;
            }
            if (d->active)
            {
                Fs.io.handle = d->fh;
                Fs.close(protocore_filesystem_span());
                d->active = PROTO_FALSE; // closed here: the abort hook must not double-close
            }
            else
            {
                dav_send_status(slot_id, 409, ""); // parent missing / not writable
                return;
            }
            if (d->error)
            {
                dav_send_status(slot_id, 507, ""); // a write failed (e.g. disk full)
                return;
            }
            dav_send_status(slot_id, d->existed ? 204 : 201, "");
            return;
        }
#endif
        // Buffered fallback (streaming disabled): body bounded by BODY_BUF_SIZE.
        if (dav_write_blocked(work, req, req->path))
        {
            dav_send_status(slot_id, 423, ""); // Locked: no / wrong lock token in the If header
            return;
        }
        // One call creates, writes, and closes, so no handle is held across a statement here.
        //
        // Only an empty CL:0 PUT reaches this buffered path, so body_len is normally 0: a bodied PUT
        // to a DAV route always streams (dav() registers the sink, and the #error at the top of this
        // file is what makes that hold - no other service can have taken the single global hook), and
        // stream_begin's only decline reasons for a matched DAV route are the ones that also fail the
        // top-level resolve above. The body is written anyway so a caller that somehow does arrive
        // buffered stores it instead of having it silently dropped.
        Fs.path.root = dav_root(work);
        Fs.path.dir = fs_path;
        Fs.path.name = "";
        Fs.exists(protocore_filesystem_span());
        proto_bool existed = Fs.ok;
        Fs.path.root = dav_root(work);
        Fs.path.dir = fs_path;
        Fs.path.name = "";
        Fs.io.wbuf = req->body;
        Fs.io.n = req->body_len;
        Fs.write_file(protocore_filesystem_span());
        if (!Fs.ok)
        {
            dav_send_status(slot_id, 409, ""); // parent missing / not writable
            return;
        }
        dav_send_status(slot_id, existed ? 204 : 201, "");
        return;
    }

    case DAV_M_DELETE: {
        if (dav_write_blocked(work, req, req->path))
        {
            dav_send_status(slot_id, 423, "");
            return;
        }
        Fs.path.root = dav_root(work);
        Fs.path.dir = fs_path;
        Fs.path.name = "";
        Fs.exists(protocore_filesystem_span());
        if (!Fs.ok)
        {
            dav_send_status(slot_id, 404, "");
            return;
        }
        // A collection and its members go in one call: the accessor owns the walk, so the target
        // being a file or a tree does not change what DELETE does here.
        Fs.path.root = dav_root(work);
        Fs.path.dir = fs_path;
        Fs.path.name = "";
        Fs.remove(protocore_filesystem_span());
        dav_send_status(slot_id, Fs.ok ? 204 : 403, "");
        return;
    }

    case DAV_M_MKCOL:
        if (dav_write_blocked(work, req, req->path))
        {
            dav_send_status(slot_id, 423, "");
            return;
        }
        Fs.path.root = dav_root(work);
        Fs.path.dir = fs_path;
        Fs.path.name = "";
        Fs.exists(protocore_filesystem_span());
        if (Fs.ok)
        {
            dav_send_status(slot_id, 405, ""); // already exists
            return;
        }
        Fs.path.root = dav_root(work);
        Fs.path.dir = fs_path;
        Fs.path.name = "";
        Fs.mkdir(protocore_filesystem_span());
        dav_send_status(slot_id, Fs.ok ? 201 : 409, "");
        return;

    case DAV_M_COPY:
    case DAV_M_MOVE: {
        HttpParserV.get_header_args.req = req;
        HttpParserV.get_header_args.key = "Destination";
        HttpParser.get_header(protocore_http_parser_span());
        const char *dest_hdr = HttpParserV.text;
        char dest_url[256];
        proto_bool dest_ok = PROTO_FALSE;
        if (dest_hdr)
        {
            Webdav.dest_path_args.destination = dest_hdr;
            Webdav.dest_path_args.out = dest_url;
            Webdav.dest_path_args.cap = sizeof(dest_url);
            Webdav.dest_path(webdav_work);
            dest_ok = Webdav.ok;
        }
        if (!dest_ok)
        {
            dav_send_status(slot_id, 400, "");
            return;
        }
        // The destination must live under this same mount.
        if (str.diff(dest_url, r->path, plen, PROTO_FALSE) != plen)
        {
            dav_send_status(slot_id, 502, "");
            return;
        }
        const char *dest_sub = dest_url + plen;
        if (str.has(dest_sub, sizeof(dest_url) - plen, "..", sizeof(".."), PROTO_FALSE))
        {
            dav_send_status(slot_id, 403, "");
            return;
        }
        // Both COPY and MOVE write the destination; MOVE additionally removes the source. Each locked
        // target needs the matching token in the If header (RFC 4918 §7).
        Webdav.method_args.m = req->method;
        Webdav.method(webdav_work);
        proto_bool is_move = Webdav.value == DAV_M_MOVE;
        if (dav_write_blocked(work, req, dest_url) || (is_move && dav_write_blocked(work, req, req->path)))
        {
            dav_send_status(slot_id, 423, "");
            return;
        }
        char dest_fs[256];
        if (!dav_join(root, dest_sub, dest_fs, sizeof(dest_fs)))
        {
            dav_send_status(slot_id, 414, "");
            return;
        }
        size_t dpl = str.len(dest_fs, sizeof(dest_fs));
        if (dpl > 1 && dest_fs[dpl - 1] == '/')
        {
            dest_fs[dpl - 1] = '\0';
        }

        HttpParserV.get_header_args.req = req;
        HttpParserV.get_header_args.key = "Overwrite";
        HttpParser.get_header(protocore_http_parser_span());
        const char *ow = HttpParserV.text;
        proto_bool overwrite = !(ow && (ow[0] == 'F' || ow[0] == 'f'));
        Fs.path.root = dav_root(work);
        Fs.path.dir = dest_fs;
        Fs.path.name = "";
        Fs.exists(protocore_filesystem_span());
        proto_bool dest_exists = Fs.ok;
        if (dest_exists && !overwrite)
        {
            dav_send_status(slot_id, 412, "");
            return;
        }

        if (is_move)
        {
            if (dest_exists)
            {
                Fs.path.root = dav_root(work);
                Fs.path.dir = dest_fs;
                Fs.path.name = "";
                Fs.remove(protocore_filesystem_span()); // replace
            }
            Fs.path.root = dav_root(work);
            Fs.path.dir = fs_path;
            Fs.path.name = "";
            Fs.dest.dir = dest_fs;
            Fs.dest.name = "";
            Fs.rename(protocore_filesystem_span());
            proto_bool moved = Fs.ok;
            dav_send_status(slot_id, moved ? (dest_exists ? 204 : 201) : 409, "");
            return;
        }

        // COPY: a file or a whole collection (RFC 4918 9.8). Depth applies to a collection source:
        // "0" copies just the collection itself, "infinity" (the default, also when absent) copies
        // the entire tree. One stat says whether the source exists and which of those it is.
        protocore_mnt_stat sst;
        Fs.path.root = dav_root(work);
        Fs.path.dir = fs_path;
        Fs.path.name = "";
        Fs.io.stat = &sst;
        Fs.stat(protocore_filesystem_span());
        if (!Fs.ok)
        {
            dav_send_status(slot_id, 404, "");
            return;
        }

        HttpParserV.get_header_args.req = req;
        HttpParserV.get_header_args.key = "Depth";
        HttpParser.get_header(protocore_http_parser_span());
        const char *depth_h = HttpParserV.text;
        proto_bool shallow = depth_h && depth_h[0] == '0'; // Depth: 0

        if (dest_exists)
        {
            Fs.path.root = dav_root(work);
            Fs.path.dir = dest_fs;
            Fs.path.name = "";
            Fs.remove(protocore_filesystem_span()); // overwrite: clear the target first
        }

        proto_bool ok;
        if (sst.is_dir && shallow)
        {
            Fs.path.root = dav_root(work);
            Fs.path.dir = dest_fs;
            Fs.path.name = "";
            Fs.mkdir(protocore_filesystem_span()); // collection, Depth:0 - no members
            ok = Fs.ok;
        }
        else
        {
            Fs.path.root = dav_root(work);
            Fs.path.dir = fs_path;
            Fs.path.name = "";
            Fs.dest.dir = dest_fs;
            Fs.dest.name = "";
            Fs.copy(protocore_filesystem_span());
            ok = Fs.ok;
        }
        dav_send_status(slot_id, ok ? (dest_exists ? 204 : 201) : 409, "");
        return;
    }

    case DAV_M_LOCK: {
        const uint32_t timeout_s = 3600; // the lock lifetime advertised in <D:timeout> below
        uint32_t expiry_s = dav_now_s + timeout_s;

        // A LOCK carrying the token in its If header is a refresh (RFC 4918 §9.10.2): extend the held
        // lock's timeout rather than taking a new one.
        HttpParserV.get_header_args.req = req;
        HttpParserV.get_header_args.key = "If";
        HttpParser.get_header(protocore_http_parser_span());
        const char *if_hdr = HttpParserV.text;
        char iftok[PROTOCORE_DAV_LOCK_TOKEN_MAX];
        const DavLock *lk = NULL;
        proto_bool have_token = PROTO_FALSE;
        if (if_hdr)
        {
            Webdav.if_token_args.if_header = if_hdr;
            Webdav.if_token_args.out = iftok;
            Webdav.if_token_args.cap = sizeof(iftok);
            Webdav.if_token(webdav_work);
            have_token = Webdav.ok;
        }
        if (have_token)
        {
            Webdav.lock_refresh_args.t = &WEBDAV_HANDLER_CTX(work)->table;
            Webdav.lock_refresh_args.token = iftok;
            Webdav.lock_refresh_args.new_expiry_s = expiry_s;
            Webdav.lock_refresh(webdav_work);
            lk = Webdav.ptr;
        }

        char token[PROTOCORE_DAV_LOCK_TOKEN_MAX];
        proto_bool shared, depth_inf;
        if (lk) // refreshed an existing lock: echo its stored scope / depth / token
        {
            protocore_sb sb_token = {token, sizeof(token), 0, PROTO_TRUE};
            Sb.put(&sb_token, lk->token);
            if (Sb.finish(&sb_token) == 0)
            {
                token[0] = '\0';
            }
            shared = !lk->exclusive;
            depth_inf = lk->depth_infinity;
        }
        else
        {
            // New lock: a lockinfo body naming <shared> is a shared lock (else exclusive); a LOCK defaults
            // to Depth: infinity when the header is absent (RFC 4918 §9.10.3).
            shared = req->body_len && dav_body_has(req, "shared");
            HttpParserV.get_header_args.req = req;
            HttpParserV.get_header_args.key = "Depth";
            HttpParser.get_header(protocore_http_parser_span());
            Webdav.depth_args.depth_hdr = HttpParserV.text;
            Webdav.depth_args.dflt = PROTOCORE_DAV_DEPTH_INFINITY;
            Webdav.depth(webdav_work);
            depth_inf = Webdav.i32 != 0;
            unsigned long tok = (unsigned long)Clock.ms;
            uint32_t tok_rand = 0;
            RngV.fill_args.out = (uint8_t *)&tok_rand;
            RngV.fill_args.len = sizeof(tok_rand);
            Rng.fill(protocore_rng_span()); // boundary: bytes into the scalar
            tok ^= (unsigned long)tok_rand;
            protocore_sb sb_token2 = {token, sizeof(token), 0, PROTO_TRUE};
            Sb.put(&sb_token2, "opaquelocktoken:");
            Sb.hex(&sb_token2, (uint64_t)(tok), 8);
            Sb.put(&sb_token2, "-pc");
            if (Sb.finish(&sb_token2) == 0)
            {
                token[0] = '\0';
            }
            Webdav.lock_acquire_args.t = &WEBDAV_HANDLER_CTX(work)->table;
            Webdav.lock_acquire_args.path = req->path;
            Webdav.lock_acquire_args.token = token;
            Webdav.lock_acquire_args.exclusive = /*exclusive=*/!shared;
            Webdav.lock_acquire_args.depth_infinity = depth_inf;
            Webdav.lock_acquire_args.expiry_s = expiry_s;
            Webdav.lock_acquire(webdav_work);
            if (!Webdav.ptr)
            {
                dav_send_status(slot_id, 423, ""); // a conflicting lock already holds this resource / subtree
                return;
            }
        }
        protocore_sb sb_buf = {WEBDAV_HANDLER_CTX(work)->buf, sizeof(WEBDAV_HANDLER_CTX(work)->buf), 0, PROTO_TRUE};
        Sb.put(&sb_buf,
               "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<D:prop "
               "xmlns:D=\"DAV:\"><D:lockdiscovery><D:activelock><D:locktype><D:write/></D:locktype><D:lockscope><D:");
        Sb.put(&sb_buf, shared ? "shared" : "exclusive");
        Sb.put(&sb_buf, "/></D:lockscope><D:depth>");
        Sb.put(&sb_buf, depth_inf ? "infinity" : "0");
        Sb.put(&sb_buf, "</D:depth><D:timeout>Second-");
        Sb.u32(&sb_buf, (uint32_t)((unsigned long)timeout_s));
        Sb.put(&sb_buf, "</D:timeout><D:locktoken><D:href>");
        Sb.put(&sb_buf, token);
        Sb.put(&sb_buf, "</D:href></D:locktoken></D:activelock></D:lockdiscovery></D:prop>\n");
        if (Sb.finish(&sb_buf) == 0)
        {
            WEBDAV_HANDLER_CTX(work)->buf[0] = '\0';
        }
        // RFC 4918 §10.5: Lock-Token uses a Coded-URL (angle-bracketed).
        char lt[64];
        protocore_sb sb_lt = {lt, sizeof(lt), 0, PROTO_TRUE};
        Sb.put(&sb_lt, "<");
        Sb.put(&sb_lt, token);
        Sb.put(&sb_lt, ">");
        if (Sb.finish(&sb_lt) == 0)
        {
            lt[0] = '\0';
        }
        proto_add_response_header(slot_id, "Lock-Token", lt);
        send_text(slot_id, 200, "application/xml; charset=utf-8", WEBDAV_HANDLER_CTX(work)->buf);
        return;
    }

    case DAV_M_UNLOCK: {
        // Release the lock named by the Lock-Token header (a Coded-URL: "<opaquelocktoken:...>").
        HttpParserV.get_header_args.req = req;
        HttpParserV.get_header_args.key = "Lock-Token";
        HttpParser.get_header(protocore_http_parser_span());
        const char *lt = HttpParserV.text;
        char token[PROTOCORE_DAV_LOCK_TOKEN_MAX];
        proto_bool released = PROTO_FALSE;
        if (lt && dav_coded_url_token(lt, token, sizeof(token)))
        {
            Webdav.lock_release_args.t = &WEBDAV_HANDLER_CTX(work)->table;
            Webdav.lock_release_args.token = token;
            Webdav.lock_release(webdav_work);
            released = Webdav.ok;
        }
        if (!released)
        {
            dav_send_status(slot_id, 409, ""); // no such lock to release (RFC 4918 §9.11.1)
            return;
        }
        dav_send_status(slot_id, 204, "");
        return;
    }

    case DAV_M_PROPFIND: {
        // Every property reported for the target - collection or not, size, mtime - is a field of
        // one directory record, so one stat reads all three.
        protocore_mnt_stat fst;
        Fs.path.root = dav_root(work);
        Fs.path.dir = fs_path;
        Fs.path.name = "";
        Fs.io.stat = &fst;
        Fs.stat(protocore_filesystem_span());
        if (!Fs.ok)
        {
            dav_send_status(slot_id, 404, "");
            return;
        }
        proto_bool isdir = fst.is_dir;
        uint32_t fsize = (uint32_t)fst.size;
        time_t mtime = (time_t)fst.mtime;

        HttpParserV.get_header_args.req = req;
        HttpParserV.get_header_args.key = "Depth";
        HttpParser.get_header(protocore_http_parser_span());
        Webdav.depth_args.depth_hdr = HttpParserV.text;
        Webdav.depth_args.dflt = 1;
        Webdav.depth(webdav_work);
        int depth = Webdav.i32;

        // RFC 4918 9.1.1: this server lists at most one level, so a Depth: infinity
        // PROPFIND is rejected with 403 + the propfind-finite-depth precondition rather
        // than silently returning a partial (one-level) 207 the client would read as
        // complete. Clients wanting a listing use Depth: 0 or 1.
        if (depth == PROTOCORE_DAV_DEPTH_INFINITY)
        {
            static const char body[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                                       "<D:error xmlns:D=\"DAV:\"><D:propfind-finite-depth/></D:error>\r\n";
            send_text(slot_id, 403, "application/xml", body);
            return;
        }

        // Self href: the request path, with a trailing '/' for a collection.
        char self_href[MAX_PATH_LEN + 2];
        protocore_sb sb_self_href = {self_href, sizeof(self_href), 0, PROTO_TRUE};
        Sb.put(&sb_self_href, req->path);
        if (Sb.finish(&sb_self_href) == 0)
        {
            self_href[0] = '\0';
        }
        size_t sl = str.len(self_href, sizeof(self_href));
        // HttpReq::path[MAX_PATH_LEN] and the parser always leaves at least "/" in it, so sl is
        // between 1 and MAX_PATH_LEN-1. That makes `sl == 0` impossible, and makes the room test
        // below always true (self_href is MAX_PATH_LEN+2). Both are kept as bounds on an index.
        if (isdir && (sl == 0 || self_href[sl - 1] != '/'))
        {
            if (sl + 1 < sizeof(self_href))
            {
                self_href[sl++] = '/';
                self_href[sl] = '\0';
            }
        }

        size_t cap = sizeof(WEBDAV_HANDLER_CTX(work)->buf);
        size_t len = 0;
        Webdav.ms_begin_args.buf = WEBDAV_HANDLER_CTX(work)->buf;
        Webdav.ms_begin_args.cap = cap;
        Webdav.ms_begin_args.len = len;
        Webdav.ms_begin(webdav_work);
        len = Webdav.n;
        char mt[40];
        FileServingV.http_rfc1123_args.epoch = mtime;
        FileServingV.http_rfc1123_args.out = mt;
        FileServingV.http_rfc1123_args.cap = sizeof(mt);
        FileServing.http_rfc1123(protocore_file_serving_span());
        Webdav.ms_entry_args.buf = WEBDAV_HANDLER_CTX(work)->buf;
        Webdav.ms_entry_args.cap = cap;
        Webdav.ms_entry_args.len = len;
        Webdav.ms_entry_args.href = self_href;
        Webdav.ms_entry_args.is_collection = isdir;
        Webdav.ms_entry_args.size = fsize;
        Webdav.ms_entry_args.rfc1123_mtime = mt;
        Webdav.ms_entry_args.content_type = isdir ? "" : mime_type(fs_path);
        Webdav.ms_entry(webdav_work);
        len = Webdav.n;

        if (isdir && depth >= 1)
        {
            Fs.path.root = dav_root(work);
            Fs.path.dir = fs_path;
            Fs.path.name = "";
            Fs.opendir(protocore_filesystem_span());
            int d = Fs.i32;
            if (d < 0)
            {
                dav_send_status(slot_id, 404, "");
                return;
            }
            int count = 0;
            for (;;)
            {
                // One readdir hands back the entry's facts and its own name together, so a child
                // costs one call and the name it writes is already the leaf.
                protocore_mnt_stat cst;
                Fs.io.handle = d;
                Fs.io.stat = &cst;
                Fs.io.name_out = WEBDAV_HANDLER_CTX(work)->child;
                Fs.io.name_cap = sizeof(WEBDAV_HANDLER_CTX(work)->child);
                Fs.readdir(protocore_filesystem_span());
                if (!Fs.ok)
                {
                    break;
                }
                if (count >= PROTOCORE_WEBDAV_MAX_ENTRIES)
                {
                    break;
                }
                char chref[MAX_PATH_LEN + 80];
                protocore_sb sb_chref = {chref, sizeof(chref), 0, PROTO_TRUE};
                Sb.put(&sb_chref, self_href);
                Sb.put(&sb_chref, WEBDAV_HANDLER_CTX(work)->child);
                Sb.put(&sb_chref, cst.is_dir ? "/" : "");
                if (Sb.finish(&sb_chref) == 0)
                {
                    chref[0] = '\0';
                }
                char cmtbuf[40];
                FileServingV.http_rfc1123_args.epoch = (time_t)cst.mtime;
                FileServingV.http_rfc1123_args.out = cmtbuf;
                FileServingV.http_rfc1123_args.cap = sizeof(cmtbuf);
                FileServing.http_rfc1123(protocore_file_serving_span());
                size_t before = len;
                Webdav.ms_entry_args.buf = WEBDAV_HANDLER_CTX(work)->buf;
                Webdav.ms_entry_args.cap = cap;
                Webdav.ms_entry_args.len = len;
                Webdav.ms_entry_args.href = chref;
                Webdav.ms_entry_args.is_collection = cst.is_dir;
                Webdav.ms_entry_args.size = (uint32_t)cst.size;
                Webdav.ms_entry_args.rfc1123_mtime = cmtbuf;
                Webdav.ms_entry_args.content_type = cst.is_dir ? "" : mime_type(WEBDAV_HANDLER_CTX(work)->child);
                Webdav.ms_entry(webdav_work);
                len = Webdav.n;
                if (len == before)
                {
                    break; // buffer full - stop listing
                }
                count++;
            }
            Fs.io.handle = d;
            Fs.close(protocore_filesystem_span());
        }
        Webdav.ms_end_args.buf = WEBDAV_HANDLER_CTX(work)->buf;
        Webdav.ms_end_args.cap = cap;
        Webdav.ms_end_args.len = len;
        Webdav.ms_end(webdav_work);
        len = Webdav.n;
        send_text(slot_id, 207, "application/xml; charset=utf-8", WEBDAV_HANDLER_CTX(work)->buf);
        return;
    }

    case DAV_M_PROPPATCH: {
        // Read-only properties (no dead-property store): answer 207 with each
        // requested property refused 403, rather than 405 - keeps Explorer/Finder,
        // which PROPPATCH a timestamp right after a PUT, from erroring.
        Fs.path.root = dav_root(work);
        Fs.path.dir = fs_path;
        Fs.path.name = "";
        Fs.exists(protocore_filesystem_span());
        if (!Fs.ok)
        {
            dav_send_status(slot_id, 404, "");
            return;
        }
        Webdav.proppatch_ms_args.buf = WEBDAV_HANDLER_CTX(work)->buf;
        Webdav.proppatch_ms_args.cap = sizeof(WEBDAV_HANDLER_CTX(work)->buf);
        Webdav.proppatch_ms_args.href = req->path;
        Webdav.proppatch_ms_args.body = (const char *)req->body;
        Webdav.proppatch_ms_args.body_len = req->body_len;
        Webdav.proppatch_ms(webdav_work);
        size_t n = Webdav.n;
        if (!n)
        {
            dav_send_status(slot_id, 507, ""); // Insufficient Storage: response did not fit the buffer
            return;
        }
        send_text(slot_id, 207, "application/xml; charset=utf-8", WEBDAV_HANDLER_CTX(work)->buf);
        return;
    }

    case DAV_M_UNSUPPORTED:
    default:
        dav_send_status(
            slot_id, 405,
            "Allow: OPTIONS, GET, HEAD, PUT, DELETE, PROPFIND, PROPPATCH, MKCOL, COPY, MOVE, LOCK, UNLOCK\r\n");
        return;
    }
}
DavNs Dav = {.try_serve_dav = webdav_handler_try_serve_dav};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_WEBDAV
