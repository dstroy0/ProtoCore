// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file webdav.c
 * @brief WebDAV (RFC 4918) filesystem-backed request handling.
 *
 * The pure core - method classification, the 207 Multi-Status XML builder, header parsing - lives
 * in network_drivers/application/webdav/; this file is the half that needs a real filesystem
 * (PROPFIND/PUT/COPY/MOVE over a mounted subtree). WEBDAV requires FILE_SERVING, so the
 * file-serving helpers it calls are always present.
 */

#include "network_drivers/application/webdav/webdav.h"
#include "mmgr/membuild.h"
#include "network_drivers/network/route.h"
#include "network_drivers/transport/tcp.h"
#include "protocore.h"
#include "server/clock/clock.h"
#include "shared_primitives/mime.h"
#include <string.h>

#if PC_ENABLE_WEBDAV

// The parser's streaming-body sink is a single global hook (http_parser_set_stream_hooks): the last
// registrar wins, so an OTA or upload service registering after dav() takes the sink away and a
// bodied PUT to a DAV route buffers (bounded by BODY_BUF_SIZE) instead of streaming. The
// buffered-PUT fallback below assumes that cannot happen, so the combination is rejected here.
#if PC_ENABLE_OTA || PC_ENABLE_UPLOAD
#error "PC_ENABLE_WEBDAV cannot be combined with PC_ENABLE_OTA or PC_ENABLE_UPLOAD: the parser's \
streaming-body sink is a single global hook, so whichever registers last silently disables the others."
#endif

// Floor on the bytes one <D:response> costs. The fixed text of pc_webdav_ms_entry is 204 (27 href
// prologue + 66 prop/resourcetype opening + 18 resourcetype close + 93 propstat/response close) and
// the href adds at least one more, so 192 under-states every real element. That makes
// BUF_SIZE / 192 an over-estimate of how many entries the buffer holds, and the assert below still
// requires it to come out under MAX_ENTRIES - which is what keeps the buffer, not the count, the
// bound the Depth-1 PROPFIND listing loop stops on.
#define PC_WEBDAV_MIN_ENTRY_BYTES 192u
static_assert(PC_WEBDAV_BUF_SIZE / PC_WEBDAV_MIN_ENTRY_BYTES < PC_WEBDAV_MAX_ENTRIES,
              "PC_WEBDAV_BUF_SIZE is large enough to hold PC_WEBDAV_MAX_ENTRIES entries: raise "
              "PC_WEBDAV_MAX_ENTRIES or lower PC_WEBDAV_BUF_SIZE so the buffer bound stays the "
              "binding one (see the PROPFIND listing loop).");

// WebDAV response scratch: the 207 Multi-Status build buffer (BSS).
typedef struct
{
    // The accessor root every operation here resolves against, bound in dav(). It is the whole
    // mount: a DAV route carries its own subtree as a request-path piece (Route::static_root), so
    // the subtree is part of the request and one root serves every mount registered.
    int root;

    char buf[PC_WEBDAV_BUF_SIZE];

    // One directory entry's own name, for the Depth-1 PROPFIND listing - the only thing here that
    // walks anything, and it walks exactly one level. Removing and copying a tree are the
    // accessor's operations (pc_fs_remove, pc_fs_copy), so neither needs a stack here.
    char child[PC_FILESYSTEM_PATH_MAX];
} DavBufCtx;

// Unbound is -1, not the zero static storage would give: root 0 is a valid root, so a zeroed field
// would resolve every path against somebody else's storage before dav() ever ran.
static DavBufCtx s_dav = {.root = -1};

// Join an FS root and a subpath into @p out (the separator handling serve_static_request uses).
// Returns false on overflow.
static proto_bool dav_join(const char *root, const char *sub, char *out, size_t cap)
{
    size_t rlen = strnlen(root, MAX_PATH_LEN);
    proto_bool root_slash = (rlen > 0 && root[rlen - 1] == '/');
    if (root_slash && sub[0] == '/')
    {
        sub++;
    }
    proto_bool sub_slash = (sub[0] == '/');
    const char *sep = (root_slash || sub_slash) ? "" : "/";
    pc_sb sb_out = {out, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb_out, root);
    pc_sb_put(&sb_out, sep);
    pc_sb_put(&sb_out, sub);
    int wn = (int)pc_sb_finish(&sb_out);
    // wn <= 0 cannot fire: snprintf only returns negative on an encoding error, which "%s%s%s"
    // cannot raise, and sep is "/" whenever root and sub are both empty, so the shortest join is
    // one byte. The truncation half (wn >= cap) is exercised.
    return wn > 0 && wn < (int)cap; // GCOVR_EXCL_BR_LINE  wn <= 0 unreachable (see above)
}

// Map a WebDAV request path to its on-disk path under the mount @p r. Strips the
// mount prefix, rejects traversal, joins onto the FS root, and drops a trailing
// '/'. Returns 0 on success, else the HTTP error code (403 traversal, 414 too
// long) - the single source of truth for the path check, shared by the request
// handler and the streaming-PUT begin hook.
static int dav_resolve_path(const Route *r, const char *reqpath, char *out, size_t cap)
{
    size_t plen = strnlen(r->path, MAX_PATH_LEN);
    // plen == 0 is unreachable: dav() always stores at least "*" - it appends the wildcard when the
    // prefix lacks one, so even dav("") yields a one-character pattern.
    if (plen > 0 && r->path[plen - 1] == '*') // GCOVR_EXCL_BR_LINE  plen == 0 unreachable (see above)
    {
        plen--;
    }
    // GCOVR_EXCL_BR_START  the "" arm is unreachable: both callers reached here through
    // path_matches() against this same route, which already required reqpath to carry the mount
    // prefix, so the length test always holds. Kept so a future caller that resolves without
    // matching first still cannot index past the end of reqpath.
    const char *sub = (strnlen(reqpath, MAX_PATH_LEN) >= plen) ? reqpath + plen : "";
    // GCOVR_EXCL_BR_STOP
    if (strstr(sub, ".."))
    {
        return 403;
    }
    const char *root = r->static_root ? r->static_root : "";
    if (!dav_join(root, sub, out, cap))
    {
        return 414;
    }
    size_t fpl = strnlen(out, cap);
    if (fpl > 1 && out[fpl - 1] == '/')
    {
        out[fpl - 1] = '\0';
    }
    return 0;
}

#if PC_ENABLE_STREAM_BODY
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

// The one place here that holds a file handle across calls, and it has to: a streaming PUT is
// opened by one callback, written by another, and closed by the handler. Every other method
// reaches storage through a single accessor call.
typedef struct
{
    DavPut put[MAX_CONNS];
} DavPutCtx;
static DavPutCtx s_davput;

// The server-global lock table (RFC 4918 §6-7). Zero-initialized, so every slot starts inactive and
// nothing is locked until a LOCK stores a token.
typedef struct
{
    DavLockTable table;
} DavLockCtx;
static DavLockCtx s_dav_lock;

// True if a write to the URL @p path is blocked by a lock the request does not present a token for. The
// token, if any, comes from the request's If header (RFC 4918 §10.4 / §7).
static proto_bool dav_write_blocked(HttpReq *req, const char *path)
{
    const char *if_hdr = http_get_header(req, "If");
    char tok[PC_DAV_LOCK_TOKEN_MAX];
    const char *presented = (if_hdr && pc_dav_if_token(if_hdr, tok, sizeof(tok))) ? tok : NULL;
    return !pc_dav_lock_can_write(&s_dav_lock.table, path, presented);
}

// True if the (always NUL-terminated) request body contains @p needle - used to spot a <shared> lockscope.
static proto_bool dav_body_has(HttpReq *req, const char *needle)
{
    return strstr((const char *)req->body, needle) != NULL;
}

// Extract the token from a Lock-Token Coded-URL ("<opaquelocktoken:...>") into @p out; false if malformed.
static proto_bool dav_coded_url_token(const char *coded, char *out, size_t cap)
{
    const char *lt = strchr(coded, '<');
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

// Registered with http_parser_set_stream_hooks in dav().
void dav_put_abort_tramp(HttpReq *req)
{
    // The PUT was torn down before the handler ran: close the half-written file so
    // the handle is not leaked (a leak eventually exhausts LittleFS's open slots).
    uint8_t slot = (uint8_t)(req - http_pool);
    // GCOVR_EXCL_BR_START  the slot >= MAX_CONNS half is unreachable: http_pool is CONN_POOL_SLOTS
    // long, but the streaming-body hooks are driven only by the HTTP/1.x byte parser, which never
    // parses for the internal dispatch slots at and above MAX_CONNS. s_davput.put[] is MAX_CONNS
    // long, so the bound still has to be tested here.
    if (slot < MAX_CONNS && s_davput.put[slot].active)
    {
        pc_fs_close(s_davput.put[slot].fh);
        s_davput.put[slot].active = PROTO_FALSE;
    }
    // GCOVR_EXCL_BR_STOP
}

proto_bool dav_stream_put_begin(HttpReq *req)
{
    if (strcmp(req->method, "PUT") != 0)
    {
        return PROTO_FALSE;
    }
    uint8_t slot = (uint8_t)(req - http_pool);
    for (uint8_t i = 0; i < pc_route_count(); i++)
    {
        Route *r = pc_route_at(i);
        // The !is_active half cannot fire: every entry below route_count was filled by
        // fill_route_base, which sets is_active, and nothing ever clears it again.
        if (!r->is_active || r->type != ROUTE_DAV) // GCOVR_EXCL_BR_LINE  see above
        {
            continue;
        }
        if (!path_matches(r->path, r->is_wildcard, req->path))
        {
            continue;
        }
        // GCOVR_EXCL_START  dav() has no interface-filtered overload, so a ROUTE_DAV entry's
        // iface_filter is always PC_IFACE_ANY and this gate never rejects. Kept so a DAV mount picks
        // up the per-route interface gate for free if that overload is ever added.
        if (r->iface_filter != PC_IFACE_ANY && r->iface_filter != pc_conn_iface(slot))
        {
            continue;
        }
        // GCOVR_EXCL_STOP
        char fs_path[256];
        if (dav_resolve_path(r, req->path, fs_path, sizeof(fs_path)) != 0)
        {
            return PROTO_FALSE; // traversal / too long - let it buffer; the handler answers 403/414
        }
        DavPut *d = &s_davput.put[slot];
        d->active = PROTO_FALSE;
        d->error = PROTO_FALSE;
        d->locked = PROTO_FALSE;
        d->written = 0;
        if (dav_write_blocked(req, req->path))
        {
            // Locked by another principal: consume the body but open no file, so the resource is not
            // touched; the PUT handler answers 423 (RFC 4918 §7).
            d->locked = PROTO_TRUE;
            return PROTO_TRUE;
        }
        d->existed = pc_fs_exists(s_dav.root, fs_path, "");
        d->fh = pc_fs_open(s_dav.root, fs_path, "", PC_MNT_WRITE);
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

void dav_stream_put_data(HttpReq *req, const uint8_t *data, size_t len)
{
    uint8_t slot = (uint8_t)(req - http_pool);
    // GCOVR_EXCL_START  http_pool is CONN_POOL_SLOTS (MAX_CONNS + PC_INTERNAL_SLOTS) long, so an index >=
    // MAX_CONNS is a real address - but it belongs to an internal dispatch slot (e.g. HTTP/3), and the
    // streaming-body hooks this runs from are driven only by the HTTP/1.x byte parser, which never parses
    // for those slots. s_davput.put[] is MAX_CONNS long, so the bound still has to be here.
    if (slot >= MAX_CONNS)
    {
        return;
    }
    // GCOVR_EXCL_STOP
    DavPut *d = &s_davput.put[slot];
    if (d->active && !d->error)
    {
        if (pc_fs_write(d->fh, data, len) != (int)len)
        {
            d->error = PROTO_TRUE;
        }
        else
        {
            d->written += len;
        }
    }
}
#endif // PC_ENABLE_STREAM_BODY

void dav(const char *url_prefix, const pc_mnt_backend *file_sys, const char *fs_root)
{
    Route *r = pc_route_add();
    if (r == NULL)
    {
        return;
    }

    char pat[MAX_PATH_LEN];
    size_t n = strnlen(url_prefix, MAX_PATH_LEN);
    if (n > 0 && url_prefix[n - 1] == '*')
    {
        pc_sb sb_pat = {pat, sizeof(pat), 0, PROTO_TRUE};
        pc_sb_put(&sb_pat, url_prefix);
        if (pc_sb_finish(&sb_pat) == 0)
        {
            pat[0] = '\0';
        }
    }
    else
    {
        pc_sb sb_pat2 = {pat, sizeof(pat), 0, PROTO_TRUE};
        pc_sb_put(&sb_pat2, url_prefix);
        pc_sb_put(&sb_pat2, "*");
        if (pc_sb_finish(&sb_pat2) == 0)
        {
            pat[0] = '\0';
        }
    }
    fill_route_base(r, pat);
    r->type = ROUTE_DAV;
    r->method = HTTP_GET;    // unused: WebDAV dispatch keys off the raw method token
    r->static_fs = file_sys; // null is legal: the accessor uses whatever is mounted
    r->static_root = fs_root;

    // Bind the root every operation in this file resolves against. Re-binding a name already bound
    // hands back the same handle, so a second mount costs nothing and both see the same storage.
    s_dav.root = pc_fs_begin("/");

#if PC_ENABLE_STREAM_BODY
    // Stream PUT bodies straight to the file (one global sink; see PC_ENABLE_STREAM_BODY).
    http_parser_set_stream_hooks(dav_stream_put_begin, dav_stream_put_data, dav_put_abort_tramp);
#endif
}

void dav_send_status(uint8_t slot_id, int code, const char *extra_headers)
{
    if (!pc_conn_active(slot_id))
    {
        http_reset(slot_id);
        return;
    }
    proto_bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);
    char header[RESP_HDR_BUF_SIZE];
    // GCOVR_EXCL_BR_START  the null arm of the extra_headers ternary is unreachable: every call site
    // in this file passes either "" or a string literal. Kept so the parameter stays optional.
    pc_sb sb_header = {header, sizeof(header), 0, PROTO_TRUE};
    pc_sb_put(&sb_header, "HTTP/1.1 ");
    pc_sb_i64(&sb_header, (int64_t)(code));
    pc_sb_put(&sb_header, " ");
    pc_sb_put(&sb_header, status_text(code));
    pc_sb_put(&sb_header, "\r\n");
    pc_sb_put(&sb_header, extra_headers ? extra_headers : "");
    pc_sb_put(&sb_header, "Content-Length: 0\r\n");
    pc_sb_put(&sb_header, cl);
    pc_sb_put(&sb_header, "\r\n");
    int hlen = (int)pc_sb_finish(&sb_header);
    // GCOVR_EXCL_BR_STOP
    pc_conn_send(slot_id, header, (proto_u16)hlen);
    pc_resp_end(slot_id, code, 0, keep, /*pre_flushed=*/PROTO_FALSE);
}

proto_bool try_serve_dav(uint8_t slot_id, HttpReq *req)
{
    for (uint8_t i = 0; i < pc_route_count(); i++)
    {
        Route *r = pc_route_at(i);
        // The !is_active half cannot fire: every entry below route_count was filled by
        // fill_route_base, which sets is_active, and nothing ever clears it again.
        if (!r->is_active || r->type != ROUTE_DAV) // GCOVR_EXCL_BR_LINE  see above
        {
            continue;
        }
        if (!path_matches(r->path, r->is_wildcard, req->path))
        {
            continue;
        }
        // GCOVR_EXCL_START  dav() has no interface-filtered overload, so a ROUTE_DAV entry's
        // iface_filter is always PC_IFACE_ANY and this gate never rejects. Kept so a DAV mount picks
        // up the per-route interface gate for free if that overload is ever added.
        if (r->iface_filter != PC_IFACE_ANY && r->iface_filter != pc_conn_iface(slot_id))
        {
            continue;
        }
        // GCOVR_EXCL_STOP
        serve_dav_request(slot_id, req, r);
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

void serve_dav_request(uint8_t slot_id, HttpReq *req, const Route *r)
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
    size_t plen = strnlen(r->path, MAX_PATH_LEN);
    if (plen > 0 && r->path[plen - 1] == '*') // GCOVR_EXCL_BR_LINE  plen == 0 unreachable (see above)
    {
        plen--;
    }
    const char *root = r->static_root ? r->static_root : "";

    // Expire any timed-out locks (RFC 4918 §6.6) before this request consults the table, so a stale lock
    // never gates a write. The clock is pc_millis() (pluggable); seconds are enough for lock lifetimes.
    uint32_t dav_now_s = (uint32_t)(pc_millis() / 1000u);
    pc_dav_lock_sweep(&s_dav_lock.table, dav_now_s);

    switch (pc_webdav_method(req->method))
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
        pc_mnt_stat gst;
        if (!pc_fs_stat(s_dav.root, fs_path, "", &gst))
        {
            dav_send_status(slot_id, 404, "");
            return;
        }
        if (gst.is_dir)
        {
            dav_send_status(slot_id, 405, ""); // GET on a collection is not a download
            return;
        }
        serve_file_internal(slot_id, pc_webdav_method(req->method) == DAV_M_HEAD, r->static_fs, fs_path,
                            mime_type(fs_path), NULL);
        return;
    }

    case DAV_M_PUT: {
#if PC_ENABLE_STREAM_BODY
        if (req->body_streaming)
        {
            // The body was written to this slot's file as it arrived (dav_stream_put_*).
            DavPut *d = &s_davput.put[slot_id];
            if (d->locked)
            {
                d->locked = PROTO_FALSE;
                dav_send_status(slot_id, 423, ""); // Locked: the body was consumed but nothing was written
                return;
            }
            if (d->active)
            {
                pc_fs_close(d->fh);
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
        if (dav_write_blocked(req, req->path))
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
        proto_bool existed = pc_fs_exists(s_dav.root, fs_path, "");
        if (!pc_fs_write_file(s_dav.root, fs_path, "", req->body, req->body_len))
        {
            dav_send_status(slot_id, 409, ""); // parent missing / not writable
            return;
        }
        dav_send_status(slot_id, existed ? 204 : 201, "");
        return;
    }

    case DAV_M_DELETE: {
        if (dav_write_blocked(req, req->path))
        {
            dav_send_status(slot_id, 423, "");
            return;
        }
        if (!pc_fs_exists(s_dav.root, fs_path, ""))
        {
            dav_send_status(slot_id, 404, "");
            return;
        }
        // A collection and its members go in one call: the accessor owns the walk, so the target
        // being a file or a tree does not change what DELETE does here.
        dav_send_status(slot_id, pc_fs_remove(s_dav.root, fs_path, "") ? 204 : 403, "");
        return;
    }

    case DAV_M_MKCOL:
        if (dav_write_blocked(req, req->path))
        {
            dav_send_status(slot_id, 423, "");
            return;
        }
        if (pc_fs_exists(s_dav.root, fs_path, ""))
        {
            dav_send_status(slot_id, 405, ""); // already exists
            return;
        }
        dav_send_status(slot_id, pc_fs_mkdir(s_dav.root, fs_path, "") ? 201 : 409, "");
        return;

    case DAV_M_COPY:
    case DAV_M_MOVE: {
        const char *dest_hdr = http_get_header(req, "Destination");
        char dest_url[256];
        if (!dest_hdr || !pc_webdav_dest_path(dest_hdr, dest_url, sizeof(dest_url)))
        {
            dav_send_status(slot_id, 400, "");
            return;
        }
        // The destination must live under this same mount.
        if (strncmp(dest_url, r->path, plen) != 0)
        {
            dav_send_status(slot_id, 502, "");
            return;
        }
        const char *dest_sub = dest_url + plen;
        if (strstr(dest_sub, ".."))
        {
            dav_send_status(slot_id, 403, "");
            return;
        }
        // Both COPY and MOVE write the destination; MOVE additionally removes the source. Each locked
        // target needs the matching token in the If header (RFC 4918 §7).
        proto_bool is_move = pc_webdav_method(req->method) == DAV_M_MOVE;
        if (dav_write_blocked(req, dest_url) || (is_move && dav_write_blocked(req, req->path)))
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
        size_t dpl = strnlen(dest_fs, sizeof(dest_fs));
        if (dpl > 1 && dest_fs[dpl - 1] == '/')
        {
            dest_fs[dpl - 1] = '\0';
        }

        const char *ow = http_get_header(req, "Overwrite");
        proto_bool overwrite = !(ow && (ow[0] == 'F' || ow[0] == 'f'));
        proto_bool dest_exists = pc_fs_exists(s_dav.root, dest_fs, "");
        if (dest_exists && !overwrite)
        {
            dav_send_status(slot_id, 412, "");
            return;
        }

        if (is_move)
        {
            if (dest_exists)
            {
                pc_fs_remove(s_dav.root, dest_fs, ""); // replace
            }
            proto_bool moved = pc_fs_rename(s_dav.root, fs_path, "", dest_fs, "");
            dav_send_status(slot_id, moved ? (dest_exists ? 204 : 201) : 409, "");
            return;
        }

        // COPY: a file or a whole collection (RFC 4918 9.8). Depth applies to a collection source:
        // "0" copies just the collection itself, "infinity" (the default, also when absent) copies
        // the entire tree. One stat says whether the source exists and which of those it is.
        pc_mnt_stat sst;
        if (!pc_fs_stat(s_dav.root, fs_path, "", &sst))
        {
            dav_send_status(slot_id, 404, "");
            return;
        }

        const char *depth_h = http_get_header(req, "Depth");
        proto_bool shallow = depth_h && depth_h[0] == '0'; // Depth: 0

        if (dest_exists)
        {
            pc_fs_remove(s_dav.root, dest_fs, ""); // overwrite: clear the target first
        }

        proto_bool ok;
        if (sst.is_dir && shallow)
        {
            ok = pc_fs_mkdir(s_dav.root, dest_fs, ""); // collection, Depth:0 - no members
        }
        else
        {
            ok = pc_fs_copy(s_dav.root, fs_path, "", dest_fs, "");
        }
        dav_send_status(slot_id, ok ? (dest_exists ? 204 : 201) : 409, "");
        return;
    }

    case DAV_M_LOCK: {
        const uint32_t timeout_s = 3600; // the lock lifetime advertised in <D:timeout> below
        uint32_t expiry_s = dav_now_s + timeout_s;

        // A LOCK carrying the token in its If header is a refresh (RFC 4918 §9.10.2): extend the held
        // lock's timeout rather than taking a new one.
        const char *if_hdr = http_get_header(req, "If");
        char iftok[PC_DAV_LOCK_TOKEN_MAX];
        const DavLock *lk = NULL;
        if (if_hdr && pc_dav_if_token(if_hdr, iftok, sizeof(iftok)))
        {
            lk = pc_dav_lock_refresh(&s_dav_lock.table, iftok, expiry_s);
        }

        char token[PC_DAV_LOCK_TOKEN_MAX];
        proto_bool shared, depth_inf;
        if (lk) // refreshed an existing lock: echo its stored scope / depth / token
        {
            pc_sb sb_token = {token, sizeof(token), 0, PROTO_TRUE};
            pc_sb_put(&sb_token, lk->token);
            if (pc_sb_finish(&sb_token) == 0)
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
            depth_inf = pc_webdav_depth(http_get_header(req, "Depth"), PC_DAV_DEPTH_INFINITY) != 0;
            unsigned long tok = (unsigned long)millis();
#if PROTOCORE_HOT
            tok ^= (unsigned long)pc_platform_rand_u32();
#endif
            pc_sb sb_token2 = {token, sizeof(token), 0, PROTO_TRUE};
            pc_sb_put(&sb_token2, "opaquelocktoken:");
            pc_sb_hex(&sb_token2, (uint64_t)(tok), 8);
            pc_sb_put(&sb_token2, "-pc");
            if (pc_sb_finish(&sb_token2) == 0)
            {
                token[0] = '\0';
            }
            if (!pc_dav_lock_acquire(&s_dav_lock.table, req->path, token, /*exclusive=*/!shared, depth_inf, expiry_s))
            {
                dav_send_status(slot_id, 423, ""); // a conflicting lock already holds this resource / subtree
                return;
            }
        }
        pc_sb sb_buf = {s_dav.buf, sizeof(s_dav.buf), 0, PROTO_TRUE};
        pc_sb_put(
            &sb_buf,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<D:prop "
            "xmlns:D=\"DAV:\"><D:lockdiscovery><D:activelock><D:locktype><D:write/></D:locktype><D:lockscope><D:");
        pc_sb_put(&sb_buf, shared ? "shared" : "exclusive");
        pc_sb_put(&sb_buf, "/></D:lockscope><D:depth>");
        pc_sb_put(&sb_buf, depth_inf ? "infinity" : "0");
        pc_sb_put(&sb_buf, "</D:depth><D:timeout>Second-");
        pc_sb_u32(&sb_buf, (uint32_t)((unsigned long)timeout_s));
        pc_sb_put(&sb_buf, "</D:timeout><D:locktoken><D:href>");
        pc_sb_put(&sb_buf, token);
        pc_sb_put(&sb_buf, "</D:href></D:locktoken></D:activelock></D:lockdiscovery></D:prop>\n");
        if (pc_sb_finish(&sb_buf) == 0)
        {
            s_dav.buf[0] = '\0';
        }
        // RFC 4918 §10.5: Lock-Token uses a Coded-URL (angle-bracketed).
        char lt[64];
        pc_sb sb_lt = {lt, sizeof(lt), 0, PROTO_TRUE};
        pc_sb_put(&sb_lt, "<");
        pc_sb_put(&sb_lt, token);
        pc_sb_put(&sb_lt, ">");
        if (pc_sb_finish(&sb_lt) == 0)
        {
            lt[0] = '\0';
        }
        proto_add_response_header(slot_id, "Lock-Token", lt);
        send_text(slot_id, 200, "application/xml; charset=utf-8", s_dav.buf);
        return;
    }

    case DAV_M_UNLOCK: {
        // Release the lock named by the Lock-Token header (a Coded-URL: "<opaquelocktoken:...>").
        const char *lt = http_get_header(req, "Lock-Token");
        char token[PC_DAV_LOCK_TOKEN_MAX];
        if (!lt || !dav_coded_url_token(lt, token, sizeof(token)) || !pc_dav_lock_release(&s_dav_lock.table, token))
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
        pc_mnt_stat fst;
        if (!pc_fs_stat(s_dav.root, fs_path, "", &fst))
        {
            dav_send_status(slot_id, 404, "");
            return;
        }
        proto_bool isdir = fst.is_dir;
        uint32_t fsize = (uint32_t)fst.size;
        time_t mtime = (time_t)fst.mtime;

        int depth = pc_webdav_depth(http_get_header(req, "Depth"), 1);

        // RFC 4918 9.1.1: this server lists at most one level, so a Depth: infinity
        // PROPFIND is rejected with 403 + the propfind-finite-depth precondition rather
        // than silently returning a partial (one-level) 207 the client would read as
        // complete. Clients wanting a listing use Depth: 0 or 1.
        if (depth == PC_DAV_DEPTH_INFINITY)
        {
            static const char body[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
                                       "<D:error xmlns:D=\"DAV:\"><D:propfind-finite-depth/></D:error>\r\n";
            send_text(slot_id, 403, "application/xml", body);
            return;
        }

        // Self href: the request path, with a trailing '/' for a collection.
        char self_href[MAX_PATH_LEN + 2];
        pc_sb sb_self_href = {self_href, sizeof(self_href), 0, PROTO_TRUE};
        pc_sb_put(&sb_self_href, req->path);
        if (pc_sb_finish(&sb_self_href) == 0)
        {
            self_href[0] = '\0';
        }
        size_t sl = strnlen(self_href, sizeof(self_href));
        // GCOVR_EXCL_BR_START  two halves here cannot fire, both for the same reason: req->path is
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
        // GCOVR_EXCL_BR_STOP

        size_t cap = sizeof(s_dav.buf);
        size_t len = 0;
        len = pc_webdav_ms_begin(s_dav.buf, cap, len);
        char mt[40];
        http_rfc1123(mtime, mt, sizeof(mt));
        len = pc_webdav_ms_entry(s_dav.buf, cap, len, self_href, isdir, fsize, mt, isdir ? "" : mime_type(fs_path));

        if (isdir && depth >= 1)
        {
            int d = pc_fs_opendir(s_dav.root, fs_path, "");
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
                pc_mnt_stat cst;
                if (!pc_fs_readdir(d, &cst, s_dav.child, sizeof(s_dav.child)))
                {
                    break;
                }
                // GCOVR_EXCL_START  the buffer-full break below always preempts this cap: the
                // static_assert at the top of this file pins PC_WEBDAV_BUF_SIZE small enough that
                // s_dav.buf cannot hold PC_WEBDAV_MAX_ENTRIES entries. Kept so the count is bounded
                // whatever those two knobs are set to.
                if (count >= PC_WEBDAV_MAX_ENTRIES)
                {
                    break;
                }
                // GCOVR_EXCL_STOP
                char chref[MAX_PATH_LEN + 80];
                pc_sb sb_chref = {chref, sizeof(chref), 0, PROTO_TRUE};
                pc_sb_put(&sb_chref, self_href);
                pc_sb_put(&sb_chref, s_dav.child);
                pc_sb_put(&sb_chref, cst.is_dir ? "/" : "");
                if (pc_sb_finish(&sb_chref) == 0)
                {
                    chref[0] = '\0';
                }
                char cmtbuf[40];
                http_rfc1123((time_t)cst.mtime, cmtbuf, sizeof(cmtbuf));
                size_t before = len;
                len = pc_webdav_ms_entry(s_dav.buf, cap, len, chref, cst.is_dir, (uint32_t)cst.size, cmtbuf,
                                         cst.is_dir ? "" : mime_type(s_dav.child));
                if (len == before)
                {
                    break; // buffer full - stop listing
                }
                count++;
            }
            pc_fs_close(d);
        }
        len = pc_webdav_ms_end(s_dav.buf, cap, len);
        send_text(slot_id, 207, "application/xml; charset=utf-8", s_dav.buf);
        return;
    }

    case DAV_M_PROPPATCH: {
        // Read-only properties (no dead-property store): answer 207 with each
        // requested property refused 403, rather than 405 - keeps Explorer/Finder,
        // which PROPPATCH a timestamp right after a PUT, from erroring.
        if (!pc_fs_exists(s_dav.root, fs_path, ""))
        {
            dav_send_status(slot_id, 404, "");
            return;
        }
        size_t n =
            pc_webdav_proppatch_ms(s_dav.buf, sizeof(s_dav.buf), req->path, (const char *)req->body, req->body_len);
        // GCOVR_EXCL_START  the builder cannot run out of room at this env's PC_WEBDAV_BUF_SIZE: its
        // output is the ~120-byte prologue, the escaped href (capped at 256 by the builder's own esc
        // buffer), at most PC_WEBDAV_MAX_PROPS echoed tags whose bytes all come out of req->body
        // (capped at BODY_BUF_SIZE), and the ~110-byte epilogue - about 1.2 KB against a 2 KB buffer.
        // It IS reachable at the 256-byte floor protocore_config.h enforces, so the guard stays.
        if (!n)
        {
            dav_send_status(slot_id, 507, ""); // Insufficient Storage: response did not fit the buffer
            return;
        }
        // GCOVR_EXCL_STOP
        send_text(slot_id, 207, "application/xml; charset=utf-8", s_dav.buf);
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
#endif // PC_ENABLE_WEBDAV
