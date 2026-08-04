// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file file_serving.c
 * @brief Filesystem-backed static file serving for PC (GET/HEAD through a mount backend).
 *
 * The conditional-GET validators (ETag / Last-Modified / If-None-Match / If-Modified-Since),
 * byte-range requests (RFC 7233), pre-compressed .gz variants, and the cross-loop file-send pump
 * that pages a large body out without truncating or blocking the worker. The shared RFC 1123 date
 * helper (http_rfc1123) lives here because WEBDAV requires FILE_SERVING, so this TU is its single
 * always-present home.
 */

#include "network_drivers/application/file_serving/file_serving.h"

#include "mmgr/membuild.h"                          // pc_sb frame builder
#include "network_drivers/application/http_range.h" // http_parse_byte_range (shared with the edge cache)
#include "network_drivers/network/route.h"
#include "network_drivers/transport/tcp.h" // conn_pool, pc_conn_*, TcpConn/ConnState
#include "protocore.h"
#include "server/filesystem/filesystem.h"  // pc_fs_* - the accessor owns the root, the join, and the .. guard
#include "shared_primitives/mime.h"        // mime_type, PC_MIME_*
#include "shared_primitives/time_compat.h" // pc_gmtime_r (portable reentrant UTC)
#include <stdio.h>                         // snprintf, sscanf
#include <string.h>                        // strncasecmp, strchr, strstr, strncmp, strnlen
#include <time.h> // strftime (RFC 1123 / conditional-GET dates) (RFC 1123 / conditional-GET dates)

// ---------------------------------------------------------------------------
// File serving
// ---------------------------------------------------------------------------

#if PC_ENABLE_FILE_SERVING

// ---------------------------------------------------------------------------
// File-send state - owned here
// ---------------------------------------------------------------------------
//
// A file larger than the TCP send window cannot go out in one dispatch: tcp_write returns ERR_MEM
// once the window fills and the remainder would be dropped. serve_file_internal sends the headers,
// opens the file and hands it to this per-slot state; file_send_pump pages out at most
// pc_conn_sndbuf() bytes per worker loop and resumes as the window drains. One transfer per slot.
// Nothing outside this file can name the state: the poll asks pc_file_holds_slot().

// Per-slot file-send continuation: the open file and how much of it is left.
typedef struct
{
    int fh;            ///< accessor handle for the open source file, held across loops.
    size_t off;        ///< absolute file offset of the next byte to send.
    size_t remaining;  ///< body bytes still to send.
    int status;        ///< response status (200 / 206) for note_response.
    int total;         ///< total body length, for the access log.
    proto_bool keep;   ///< keep-alive vs close at completion.
    proto_bool active; ///< a transfer is in progress on this slot.
} FileSend;

/** @brief The file-send state this TU owns, one entry per connection slot. */
typedef struct
{
    FileSend send[MAX_CONNS];
    int root; ///< the accessor root everything here resolves against (see file_root).
} FileCtx;

// Unbound is -1, not the zero static storage would give: root 0 is a valid root, so a zeroed field
// would resolve every path against somebody else's storage before file_root() ever ran.
static FileCtx s_file = {.root = -1};

// The root file serving resolves against: the whole mount. A static route carries its own subtree as
// a request-path piece (Route::static_root), so the subtree is part of the request rather than part
// of the root - which is what lets one bound root serve every static mount the application
// registers, instead of spending a root table entry per serve_static() call.
//
// Bound on first use because file serving has no begin(): a route can be registered before or after
// the mount is set up, and serve_file() is reachable without any serve_static() at all. Re-binding a
// name already bound hands back the same handle, so this settles after the first call.
static int file_root(void)
{
    if (s_file.root < 0)
    {
        s_file.root = pc_fs_begin("/");
    }
    return s_file.root;
}

proto_bool pc_file_holds_slot(uint8_t slot)
{
    return s_file.send[slot].active;
}

// HTTP-date helpers (shared by file serving's Last-Modified / If-Modified-Since and
// WebDAV's getlastmodified / creationdate). WEBDAV requires FILE_SERVING, so this is
// the single home for both. Format a time_t as an RFC 1123 GMT date; leaves @p out
// empty when the timestamp is zero/unavailable.
void http_rfc1123(int64_t epoch, char *out, size_t cap)
{
    out[0] = '\0';
    if (epoch <= 0)
    {
        return;
    }
    // The API states its own width; time_t is whatever the toolchain picked (32 or 64 bit) and
    // only the conversion seam is allowed to name it.
    time_t t = (time_t)epoch;
    struct tm tmv;
    if (!pc_gmtime_r(&t, &tmv)) // reentrant: never the shared static buffer (worker-safe)
    {
        return;
    }
    strftime(out, cap, "%a, %d %b %Y %H:%M:%S GMT", &tmv);
}

// True if a resource last modified at @p mtime is NOT newer than the client's
// If-Modified-Since date @p ims (RFC 1123 form), i.e. a conditional GET should
// answer 304. Parses the date by hand (sscanf, no stdlib) and compares the two
// broken-down times field by field, so no timegm()/epoch round-trip is needed.
// Returns false (serve 200) when there is no usable date - mtime is 0 (no clock),
// @p ims is absent, or it does not parse.
static proto_bool http_not_modified_since(time_t mtime, const char *ims)
{
    if (mtime <= 0 || !ims)
    {
        return PROTO_FALSE;
    }
    char mon[4] = {0};
    int day = 0;
    int year = 0;
    int hh = 0;
    int mm = 0;
    int ss = 0;
    // "Sun, 06 Nov 1994 08:49:37 GMT" - skip the weekday, read the rest.
    if (sscanf(ims, "%*3s, %d %3s %d %d:%d:%d", &day, mon, &year, &hh, &mm, &ss) != 6)
    {
        return PROTO_FALSE;
    }
    static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *mp = strstr(MONTHS, mon);
    // Must align to a 3-char month boundary: a malformed token like "ebM" appears in
    // the table at a non-multiple-of-3 offset and would otherwise mis-parse as a month.
    if (!mp || ((mp - MONTHS) % 3) != 0)
    {
        return PROTO_FALSE;
    }
    int imon = (int)(mp - MONTHS) / 3; // 0-based, matches struct tm tm_mon

    struct tm tf;
    if (!pc_gmtime_r(&mtime, &tf)) // reentrant: never the shared static buffer (worker-safe)
    {
        return PROTO_FALSE;
    }
    // Compare file (tf) vs If-Modified-Since fields, most significant first.
    int fy = tf.tm_year + 1900;
    if (fy != year)
    {
        return fy < year;
    }
    if (tf.tm_mon != imon)
    {
        return tf.tm_mon < imon;
    }
    if (tf.tm_mday != day)
    {
        return tf.tm_mday < day;
    }
    if (tf.tm_hour != hh)
    {
        return tf.tm_hour < hh;
    }
    if (tf.tm_min != mm)
    {
        return tf.tm_min < mm;
    }
    return tf.tm_sec <= ss;
}

// RFC 9110 13.1.2: If-None-Match comparison. Supports "*" (matches any current
// representation), a comma-separated list of entity-tags, and weak comparison
// (an inbound W/"x" matches our strong "x"). @p etag is our tag, quotes included.
static proto_bool inm_matches(const char *inm, const char *etag)
{
    // Leading OWS is stripped by the HTTP/1.x byte parser, but NOT on a semantic ingress
    // (HTTP/2 / HTTP/3): those hand over HPACK/QPACK-decoded values verbatim, so a
    // `if-none-match: <SP>"abc"` reaches here with the whitespace intact.
    while (*inm == ' ' || *inm == '\t')
    {
        inm++;
    }
    if (inm[0] == '*')
    {
        return PROTO_TRUE; // "*" matches the existing representation
    }
    size_t etlen = strnlen(etag, 40);
    const char *p = inm;
    while (*p)
    {
        while (*p == ' ' || *p == '\t' || *p == ',')
        {
            p++;
        }
        if (!*p)
        {
            break;
        }
        const char *tag = p;
        if (tag[0] == 'W' && tag[1] == '/') // weak validator: ignore the W/ prefix
        {
            tag += 2;
        }
        if (tag[0] == '"')
        {
            const char *end = strchr(tag + 1, '"');
            if (end && (size_t)(end - tag + 1) == etlen && strncmp(tag, etag, etlen) == 0)
            {
                return PROTO_TRUE;
            }
        }
        const char *comma = strchr(p, ',');
        if (!comma)
        {
            break;
        }
        p = comma + 1;
    }
    return PROTO_FALSE;
}

void serve_file_internal(uint8_t slot_id, proto_bool head, const pc_mnt_backend *file_sys, const char *fs_path,
                         const char *content_type, const char *content_encoding)
{
    int fh = pc_fs_open(file_root(), fs_path, "", PC_MNT_READ);
    if (fh < 0)
    {
        send_text(slot_id, 404, PC_MIME_TEXT_PLAIN, "Not Found");
        return;
    }

    if (!pc_conn_active(slot_id))
    {
        pc_fs_close(fh);
        http_reset(slot_id);
        return;
    }

    // Size and mtime come from one stat, not two calls on the handle: they are two fields of the same
    // directory record, and asking separately is two lookups of what one read already had.
    pc_mnt_stat st;
    if (!pc_fs_stat(file_root(), fs_path, "", &st))
    {
        pc_fs_close(fh);
        send_text(slot_id, 404, PC_MIME_TEXT_PLAIN, "Not Found");
        return;
    }
    size_t file_size = (size_t)(st.size);

    proto_bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    // Optional Content-Encoding line (e.g. gzip for pre-compressed assets).
    char enc_line[40];
    enc_line[0] = '\0';
    if (content_encoding)
    {
        pc_sb sb_enc_line = {enc_line, sizeof(enc_line), 0, PROTO_TRUE};
        pc_sb_put(&sb_enc_line, "Content-Encoding: ");
        pc_sb_put(&sb_enc_line, content_encoding);
        pc_sb_put(&sb_enc_line, "\r\n");
        if (pc_sb_finish(&sb_enc_line) == 0)
        {
            enc_line[0] = '\0';
        }
    }

#if PC_ENABLE_ETAG
    // Conditional GET. Strong validator (ETag) from size + mtime; plus a
    // Last-Modified date validator. A conditional request answers 304 when either
    // the client's If-None-Match matches the ETag, or - per RFC 9110, only when no
    // If-None-Match is present - its If-Modified-Since is not older than the file.
    time_t mtime = (time_t)(st.mtime);
    char etag[40];
    pc_sb sb_etag = {etag, sizeof(etag), 0, PROTO_TRUE};
    pc_sb_put(&sb_etag, "\"");
    pc_sb_hex(&sb_etag, (uint64_t)((unsigned)file_size), 1);
    pc_sb_put(&sb_etag, "-");
    pc_sb_hex(&sb_etag, (uint64_t)((unsigned long)mtime), 1);
    pc_sb_put(&sb_etag, "\"");
    if (pc_sb_finish(&sb_etag) == 0)
    {
        etag[0] = '\0';
    }

    char lm_date[40];
    char lastmod_line[17 + sizeof(lm_date)]; // "Last-Modified: " + date + "\r\n" + NUL
    lastmod_line[0] = '\0';
    http_rfc1123(mtime, lm_date, sizeof(lm_date));
    if (lm_date[0])
    {
        pc_sb sb_lastmod_line = {lastmod_line, sizeof(lastmod_line), 0, PROTO_TRUE};
        pc_sb_put(&sb_lastmod_line, "Last-Modified: ");
        pc_sb_put(&sb_lastmod_line, lm_date);
        pc_sb_put(&sb_lastmod_line, "\r\n");
        if (pc_sb_finish(&sb_lastmod_line) == 0)
        {
            lastmod_line[0] = '\0';
        }
    }

    const char *inm = http_get_header(&http_pool[slot_id], "If-None-Match");
    proto_bool not_modified =
        inm ? inm_matches(inm, etag)
            : http_not_modified_since(mtime, http_get_header(&http_pool[slot_id], "If-Modified-Since"));
    if (not_modified)
    {
        pc_fs_close(fh);
        char h304[RESP_HDR_BUF_SIZE];
        pc_sb sb_h304 = {h304, sizeof(h304), 0, PROTO_TRUE};
        pc_sb_put(&sb_h304, "HTTP/1.1 304 Not Modified\r\nETag: ");
        pc_sb_put(&sb_h304, etag);
        pc_sb_put(&sb_h304, "\r\n");
        pc_sb_put(&sb_h304, lastmod_line);
        pc_sb_put(&sb_h304, pc_resp_cache_control());
        pc_sb_put(&sb_h304, pc_resp_cors_enabled() ? pc_resp_cors_header() : "");
        pc_sb_put(&sb_h304, cl);
        pc_sb_put(&sb_h304, "\r\n");
        int n304 = (int)pc_sb_finish(&sb_h304);
        pc_conn_send_flush(slot_id, h304, (proto_u16)n304); // header-only reply: write and flush in one marshal
        pc_resp_end(slot_id, 304, 0, keep, /*pre_flushed=*/PROTO_TRUE);
        return;
    }
    char etag_line[48];
    pc_sb sb_etag_line = {etag_line, sizeof(etag_line), 0, PROTO_TRUE};
    pc_sb_put(&sb_etag_line, "ETag: ");
    pc_sb_put(&sb_etag_line, etag);
    pc_sb_put(&sb_etag_line, "\r\n");
    if (pc_sb_finish(&sb_etag_line) == 0)
    {
        etag_line[0] = '\0';
    }
#else
    const char *etag_line = "";
    const char *lastmod_line = "";
#endif

    // Default: full 200 response covering the whole file.
    int status = 200;
    size_t body_len = file_size;
    size_t body_off = 0; // file offset the body starts at (nonzero for a Range)
    const char *accept_ranges = "";
    char range_line[64];
    range_line[0] = '\0';

#if PC_ENABLE_RANGE
    accept_ranges = "Accept-Ranges: bytes\r\n"; // advertise range support on every file response
    size_t r_start = 0;
    size_t r_end = 0;
    int rr = http_parse_byte_range(http_get_header(&http_pool[slot_id], "Range"), file_size, &r_start, &r_end);
    if (rr < 0)
    {
        // Unsatisfiable range -> 416 with Content-Range: bytes */<size>.
        pc_fs_close(fh);
        char h416[RESP_HDR_BUF_SIZE];
        pc_sb sb_h416 = {h416, sizeof(h416), 0, PROTO_TRUE};
        pc_sb_put(&sb_h416, "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */");
        pc_sb_u32(&sb_h416, (uint32_t)((unsigned)file_size));
        pc_sb_put(&sb_h416, "\r\nContent-Length: 0\r\n");
        pc_sb_put(&sb_h416, pc_resp_cors_enabled() ? pc_resp_cors_header() : "");
        pc_sb_put(&sb_h416, cl);
        pc_sb_put(&sb_h416, "\r\n");
        int n416 = (int)pc_sb_finish(&sb_h416);
        pc_conn_send_flush(slot_id, h416, (proto_u16)n416);
        pc_resp_end(slot_id, 416, 0, keep, /*pre_flushed=*/PROTO_TRUE);
        return;
    }
    if (rr > 0)
    {
        status = 206;
        body_len = r_end - r_start + 1;
        pc_sb sb_range_line = {range_line, sizeof(range_line), 0, PROTO_TRUE};
        pc_sb_put(&sb_range_line, "Content-Range: bytes ");
        pc_sb_u32(&sb_range_line, (uint32_t)((unsigned)r_start));
        pc_sb_put(&sb_range_line, "-");
        pc_sb_u32(&sb_range_line, (uint32_t)((unsigned)r_end));
        pc_sb_put(&sb_range_line, "/");
        pc_sb_u32(&sb_range_line, (uint32_t)((unsigned)file_size));
        pc_sb_put(&sb_range_line, "\r\n");
        if (pc_sb_finish(&sb_range_line) == 0)
        {
            range_line[0] = '\0';
        }
        pc_fs_seek(fh, (uint64_t)r_start);
        body_off = r_start;
    }
#endif

    char header[RESP_HDR_BUF_SIZE];
    pc_sb sb_header = {header, sizeof(header), 0, PROTO_TRUE};
    pc_sb_put(&sb_header, "HTTP/1.1 ");
    pc_sb_i64(&sb_header, (int64_t)(status));
    pc_sb_put(&sb_header, " ");
    pc_sb_put(&sb_header, status_text(status));
    pc_sb_put(&sb_header, "\r\nContent-Type: ");
    pc_sb_put(&sb_header, content_type);
    pc_sb_put(&sb_header, "\r\nContent-Length: ");
    pc_sb_u32(&sb_header, (uint32_t)((unsigned)body_len));
    pc_sb_put(&sb_header, "\r\n");
    pc_sb_put(&sb_header, accept_ranges);
    pc_sb_put(&sb_header, range_line);
    pc_sb_put(&sb_header, enc_line);
    pc_sb_put(&sb_header, etag_line);
    pc_sb_put(&sb_header, lastmod_line);
    pc_sb_put(&sb_header, pc_resp_cache_control());
    pc_sb_put(&sb_header, pc_resp_cors_enabled() ? pc_resp_cors_header() : "");
    pc_sb_put(&sb_header, cl);
    pc_sb_put(&sb_header, "\r\n");
    int hlen = (int)pc_sb_finish(&sb_header);
    if (hlen == 0)
    {
        header[0] = '\0';
    }

    pc_conn_send(slot_id, header, (proto_u16)hlen);

    // HEAD or empty body: headers only, finish now.
    if (head || body_len == 0)
    {
        pc_fs_close(fh);
        pc_resp_end(slot_id, status, 0, keep, /*pre_flushed=*/PROTO_FALSE);
        return;
    }

    // Hand the body to the cross-loop pump: it pages out at most one send-buffer
    // window now and resumes on later loops as the window drains, so a file larger
    // than TCP_SND_BUF is never truncated. The pump owns the file and calls
    // pc_resp_end() at completion - do not close f or end the response here.
    FileSend *s = &s_file.send[slot_id];
    s->fh = fh;
    s->off = body_off;
    s->remaining = body_len;
    s->status = status;
    s->total = (int)body_len;
    s->keep = keep;
    s->active = PROTO_TRUE;
    file_send_pump(slot_id);
}

// Page out a pending file response across worker loops: send up to pc_conn_sndbuf()
// bytes now and return; the next loop resumes (woken by the sent callback) until the
// whole body has been queued, then finish the response. Bounded per loop, never
// truncates, never blocks the worker.
void file_send_pump(uint8_t slot_id)
{
    FileSend *s = &s_file.send[slot_id];
    // sets s->active immediately before its call, and the poll loop in protocore.c only pumps a slot
    // whose s_file.send[i].active is set. Kept so the pump is safe to call unconditionally.
    if (!s->active)
    {
        return;
    }

    if (!pc_conn_active(slot_id))
    {
        // Connection went away mid-transfer: drop the source and the continuation.
        pc_fs_close(s->fh);
        s->active = PROTO_FALSE;
        return;
    }

    // A file body still being paged out is active, not idle: keep the CONN_TIMEOUT_MS idle sweep
    // off it so a transient send stall on a large file cannot reap the slot mid-transfer.
    pc_conn_touch_active(slot_id);

    uint8_t chunk[FILE_CHUNK_SIZE];
    while (s->remaining > 0)
    {
        proto_u16 avail = pc_conn_sndbuf(slot_id);
        if (avail == 0)
        {
            pc_conn_flush(slot_id); // push what is queued; resume on a later loop
            return;
        }
        size_t want = s->remaining < sizeof(chunk) ? s->remaining : sizeof(chunk);
        if (want > avail)
        {
            want = avail;
        }
        // The backend reports a fault as -1 and end-of-data as 0; both stop the transfer, and the
        // comparison is <= so a fault can never be added to the offset as a negative count.
        int n = pc_fs_read(s->fh, chunk, want);
        if (n <= 0)
        {
            s->remaining = 0; // read error / short file: stop (response will be short)
            break;
        }
        if (!pc_conn_send(slot_id, chunk, (proto_u16)n))
        {
            pc_fs_seek(s->fh, s->off); // un-read the bytes that did not go out; retry next loop
            pc_conn_flush(slot_id);
            return;
        }
        s->off += (size_t)(n);
        s->remaining -= (size_t)(n);
    }

    // Whole body queued: finish the response (flush, keep-alive/close, log, reset).
    pc_fs_close(s->fh);
    s->active = PROTO_FALSE;
    pc_conn_flush(slot_id);
    pc_resp_end(slot_id, s->status, s->total, s->keep, /*pre_flushed=*/PROTO_FALSE);
}

void serve_file(uint8_t slot_id, const pc_mnt_backend *file_sys, const char *fs_path, const char *content_type)
{
    serve_file_internal(slot_id, req_is_head(slot_id), file_sys, fs_path, content_type, NULL);
}

void serve_static(const char *url_prefix, const pc_mnt_backend *file_sys, const char *fs_root)
{
    Route *r = pc_route_add();
    if (r == NULL)
    {
        return;
    }
    // Store the pattern as a wildcard so path_matches() does a prefix match.
    //
    // The pattern is built BEFORE a route slot is taken, because a prefix that does not fit must
    // not be registered at all. Formatting this with snprintf truncated an over-long prefix to
    // MAX_PATH_LEN-1 and dropped the '*' with it, quietly turning a subtree mount into an
    // exact-match route for a path the caller never named - a route that serves something other
    // than what was asked for is worse than a route that does not exist.
    char pat[MAX_PATH_LEN];
    size_t n = strnlen(url_prefix, MAX_PATH_LEN);
    pc_sb sb_pat = {pat, sizeof(pat), 0, PROTO_TRUE};
    pc_sb_put(&sb_pat, url_prefix);
    if (n == 0 || url_prefix[n - 1] != '*')
    {
        pc_sb_put(&sb_pat, "*"); // not already a wildcard: append one
    }
    if (pc_sb_finish(&sb_pat) == 0)
    {
        return; // prefix + wildcard does not fit: register nothing
    }

    fill_route_base(r, pat);
    r->type = ROUTE_STATIC;
    r->method = HTTP_GET;
    r->static_fs = file_sys; // null is legal: the accessor uses whatever is mounted
    r->static_root = fs_root;
}

void serve_static_request(uint8_t slot_id, HttpReq *req, const Route *r)
{
    // No null-check on static_fs: storage is reached by layer, through the accessor, so the field
    // names a preference and never the path. A null one is what serve_static() documents as legal
    // and means "whatever is mounted"; 404-ing on it refused every request a caller made without
    // naming a backend it had no way to choose anyway.

    // Request path beyond the mount prefix (route path minus its trailing '*'). plen == 0 is
    // unreachable: serve_static() always stores at least "*" (it appends the wildcard when the
    // prefix lacks one), so the pattern is never empty.
    size_t plen = strnlen(r->path, MAX_PATH_LEN);
    if (plen > 0 && r->path[plen - 1] == '*')
    {
        plen--;
    }
    const char *sub = (strnlen(req->path, MAX_PATH_LEN) >= plen) ? req->path + plen : "";

    // Reject path traversal before touching the filesystem.
    if (strstr(sub, ".."))
    {
        send_text(slot_id, 404, PC_MIME_TEXT_PLAIN, "Not Found");
        return;
    }

    const char *root = r->static_root ? r->static_root : "";
    size_t rlen = strnlen(root, MAX_PATH_LEN);
    proto_bool root_slash = (rlen > 0 && root[rlen - 1] == '/');
    if (root_slash && sub[0] == '/') // avoid a doubled separator
    {
        sub++;
    }
    proto_bool sub_slash = (sub[0] == '/');
    const char *sep = (root_slash || sub_slash) ? "" : "/";

    // Directory or bare-prefix request → index.html.
    size_t slen = strnlen(sub, MAX_PATH_LEN);
    proto_bool dir = (slen == 0) || (sub[slen - 1] == '/');

    // A path that does not fit is refused, not truncated: a clipped path names a different file,
    // and serving one the caller never asked for is worse than a 404.
    char fs_path[256];
    pc_sb sb_path = {fs_path, sizeof(fs_path), 0, PROTO_TRUE};
    pc_sb_put(&sb_path, root);
    pc_sb_put(&sb_path, sep);
    pc_sb_put(&sb_path, sub);
    if (dir)
    {
        pc_sb_lit(&sb_path, "index.html");
    }
    if (pc_sb_finish(&sb_path) == 0)
    {
        send_text(slot_id, 404, PC_MIME_TEXT_PLAIN, "Not Found");
        return;
    }

    const char *ctype = mime_type(fs_path);
    proto_bool head = req_is_head(slot_id);

    // Pre-compressed variant: serve <path>.gz if the client accepts gzip and it
    // exists. Content-Type stays that of the original (uncompressed) resource.
    const char *ae = http_get_header(req, "Accept-Encoding");
    if (ae && strstr(ae, "gzip"))
    {
        char gz[260];
        pc_sb sb_gz = {gz, sizeof(gz), 0, PROTO_TRUE};
        pc_sb_put(&sb_gz, fs_path);
        pc_sb_put(&sb_gz, ".gz");
        int gn = (int)pc_sb_finish(&sb_gz);
        // Neither length half can fail: fs_path is a 256-byte buffer, so gn is at most 258 and
        // always under gz's 260. Both are kept because the two buffer sizes are independent
        // constants. The exclusion is per-line, so it also drops the exists() halves - those ARE
        // exercised both ways (see the gzip tests).
        if (gn > 0 && gn < (int)sizeof(gz) && pc_fs_exists(file_root(), gz, ""))
        {
            serve_file_internal(slot_id, head, r->static_fs, gz, ctype, "gzip");
            return;
        }
    }

    serve_file_internal(slot_id, head, r->static_fs, fs_path, ctype, NULL);
}
#endif // PC_ENABLE_FILE_SERVING
