// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_FILE_SERVING

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "network_drivers/application/file_serving/file_serving.h"
#include "network_drivers/presentation/http/http.h"
#include "network_drivers/session/session.h"                 // file_send: the transfer the connection carries
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the slot a response is written on

#include "mmgr/membuild/membuild.h" // protocore_sb frame builder
#include "mmgr/protostr/protostr.h" // str.find / str.has: the month table, traversal and gzip markers
#include "network_drivers/application/http_range/http_range.h" // http_parse_byte_range (shared with the edge cache)
#include "network_drivers/presentation/http/route/http_route.h"
#include "network_drivers/transport/tcp/tcp.h" // conn_pool, protocore_conn_*, TcpConn/ConnState
#include "protocore.h"
#include "server/storage/filesystem/filesystem.h" // protocore_fs_* - the accessor owns the root, the join, and the .. guard
#include "shared/mime/mime.h"                     // mime_type, PROTOCORE_MIME_*
#include "shared/time_compat/time_compat.h"       // protocore_gmtime_r (portable reentrant UTC)
#include <stdio.h>                                // snprintf, sscanf
#include <time.h> // strftime (RFC 1123 / conditional-GET dates) (RFC 1123 / conditional-GET dates)

static uint8_t http_routes_work[16]; // the borrow an entry takes; HttpRoutes never reads it

static uint8_t mnt_work[16]; // the borrow an entry takes; Mnt never reads it

static uint8_t time_compat_work[16]; // the borrow an entry takes; TimeCompat never reads it

static uint8_t http_range_work[16]; // the borrow an entry takes; HttpRange never reads it

// ---------------------------------------------------------------------------
// File serving
// ---------------------------------------------------------------------------

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// File-send state - owned here
// ---------------------------------------------------------------------------
//
// A file larger than the TCP send window cannot go out in one dispatch: tcp_write returns ERR_MEM
// once the window fills and the remainder would be dropped. serve_file_internal sends the headers,
// opens the file and hands it to this per-slot state; file_send_pump pages out at most
// ConnPool.sndbuf() bytes per worker loop and resumes as the window drains. One transfer per slot.
// Nothing outside this file can name the state: the poll asks protocore_file_holds_slot().

/** @brief What this TU owns: the accessor root. The per-slot transfer is session's. */
typedef struct
{
    int root; ///< the accessor root everything here resolves against (see file_root).
} FileCtx;

// Unbound is -1, not the zero static storage would give: root 0 is a valid root, so a zeroed field
// would resolve every path against somebody else's storage before file_root() ever ran.
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define FILE_SERVING_OFF_CTX 0u
static_assert(FILE_SERVING_OFF_CTX + sizeof(FileCtx) <= PROTOCORE_FILE_SERVING_BORROW,
              "PROTOCORE_FILE_SERVING_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define FILE_SERVING_CTX(w) ((FileCtx *)(void *)((w) + FILE_SERVING_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_FILE_SERVING_BORROW persistent bytes
} FileServingOwnCtx;
static FileServingOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_file_serving_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_FILE_SERVING_BORROW).buf;
        // A borrow arrives zeroed, and these do not start at zero.
        FILE_SERVING_CTX(s_own.span)->root = -1;
    }
    return s_own.span;
}

// The root file serving resolves against: the whole mount. A static route carries its own subtree as
// a request-path piece (the mount point mnt_id names), so the subtree is part of the request rather than part
// of the root - which is what lets one bound root serve every static mount the application
// registers, instead of spending a root table entry per serve_static() call.
//
// Bound on first use because file serving has no begin(): a route can be registered before or after
// the mount is set up, and serve_file() is reachable without any serve_static() at all. Re-binding a
// name already bound hands back the same handle, so this settles after the first call.
static int file_root(uint8_t *restrict work)
{
    if (FILE_SERVING_CTX(work)->root < 0)
    {
        Fs.mount = "/";
        Fs.begin(protocore_filesystem_span());
        FILE_SERVING_CTX(work)->root = Fs.i32;
    }
    return FILE_SERVING_CTX(work)->root;
}

// The entries this file calls before reaching their definitions.
static void file_serving_file_send_pump(uint8_t *restrict work);
static void file_serving_http_rfc1123(uint8_t *restrict work);
static void file_serving_serve_file_internal(uint8_t *restrict work);

static void file_serving_holds_slot(uint8_t *restrict work)
{
    (void)work;
    uint8_t slot = FileServing.holds_slot_args.slot;

    FileServing.ok = file_send[slot].active;
}

// HTTP-date helpers (shared by file serving's Last-Modified / If-Modified-Since and
// WebDAV's getlastmodified / creationdate). WEBDAV requires FILE_SERVING, so this is
// the single home for both. Format a time_t as an RFC 1123 GMT date; leaves @p out
// empty when the timestamp is zero/unavailable.
static void file_serving_http_rfc1123(uint8_t *restrict work)
{
    (void)work;
    int64_t epoch = FileServing.http_rfc1123_args.epoch;
    char *out = FileServing.http_rfc1123_args.out;
    size_t cap = FileServing.http_rfc1123_args.cap;

    out[0] = '\0';
    if (epoch <= 0)
    {
        return;
    }
    // The API states its own width; time_t is whatever the toolchain picked (32 or 64 bit) and
    // only the conversion seam is allowed to name it.
    struct tm tmv;
    TimeCompat.args.epoch = epoch;
    TimeCompat.args.out = &tmv;
    TimeCompat.gmtime(time_compat_work); // reentrant: never the shared static buffer (worker-safe)
    if (!TimeCompat.tm_out)
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
    const char *mp = str.find(MONTHS, sizeof(MONTHS), mon, sizeof(mon), PROTO_FALSE);
    // Must align to a 3-char month boundary: a malformed token like "ebM" appears in
    // the table at a non-multiple-of-3 offset and would otherwise mis-parse as a month.
    if (!mp || ((mp - MONTHS) % 3) != 0)
    {
        return PROTO_FALSE;
    }
    int imon = (int)(mp - MONTHS) / 3; // 0-based, matches struct tm tm_mon

    struct tm tf;
    TimeCompat.args.epoch = (uint32_t)mtime;
    TimeCompat.args.out = &tf;
    TimeCompat.gmtime(time_compat_work); // reentrant: never the shared static buffer (worker-safe)
    if (!TimeCompat.tm_out)
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
    size_t etlen = str.len(etag, 40);
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
            const char *end = str.find(tag + 1, str.len(tag + 1, MAX_VAL_LEN) + 1u, "\"", sizeof("\""), PROTO_FALSE);
            if (end && (size_t)(end - tag + 1) == etlen && str.diff(tag, etag, etlen, PROTO_FALSE) == etlen)
            {
                return PROTO_TRUE;
            }
        }
        const char *comma = str.find(p, str.len(p, MAX_VAL_LEN) + 1u, ",", sizeof(","), PROTO_FALSE);
        if (!comma)
        {
            break;
        }
        p = comma + 1;
    }
    return PROTO_FALSE;
}

static void file_serving_serve_file_internal(uint8_t *restrict work)
{
    uint8_t slot_id = FileServing.serve_file_internal_args.slot_id;
    proto_bool head = FileServing.serve_file_internal_args.head;
    const protocore_mnt_backend *file_sys = FileServing.serve_file_internal_args.file_sys;
    const char *fs_path = FileServing.serve_file_internal_args.fs_path;
    const char *content_type = FileServing.serve_file_internal_args.content_type;
    const char *content_encoding = FileServing.serve_file_internal_args.content_encoding;

    Fs.path.root = file_root(work);
    Fs.path.dir = fs_path;
    Fs.path.name = "";
    Fs.io.mode = PROTOCORE_MNT_READ;
    Fs.open(protocore_filesystem_span());
    int fh = Fs.i32;
    if (fh < 0)
    {
        send_text(slot_id, 404, PROTOCORE_MIME_TEXT_PLAIN, "Not Found");
        return;
    }

    ConnPool.slot = slot_id;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPool.ok)
    {
        Fs.io.handle = fh;
        Fs.close(protocore_filesystem_span());
        HttpParser.reset_args.req = &http_pool[slot_id];
        HttpParser.reset(protocore_http_parser_span());
        return;
    }

    // Size and mtime come from one stat, not two calls on the handle: they are two fields of the same
    // directory record, and asking separately is two lookups of what one read already had.
    protocore_mnt_stat st;
    Fs.path.root = file_root(work);
    Fs.path.dir = fs_path;
    Fs.path.name = "";
    Fs.io.stat = &st;
    Fs.stat(protocore_filesystem_span());
    if (!Fs.ok)
    {
        Fs.io.handle = fh;
        Fs.close(protocore_filesystem_span());
        send_text(slot_id, 404, PROTOCORE_MIME_TEXT_PLAIN, "Not Found");
        return;
    }
    size_t file_size = (size_t)(st.size);

    proto_bool keep;
    const char *cl = protocore_resp_conn_hdr(slot_id, &keep);

    // Optional Content-Encoding line (e.g. gzip for pre-compressed assets).
    char enc_line[40];
    enc_line[0] = '\0';
    if (content_encoding)
    {
        protocore_sb sb_enc_line = {enc_line, sizeof(enc_line), 0, PROTO_TRUE};
        Sb.put(&sb_enc_line, "Content-Encoding: ");
        Sb.put(&sb_enc_line, content_encoding);
        Sb.put(&sb_enc_line, "\r\n");
        if (Sb.finish(&sb_enc_line) == 0)
        {
            enc_line[0] = '\0';
        }
    }

#if PROTOCORE_ENABLE_ETAG
    // Conditional GET. Strong validator (ETag) from size + mtime; plus a
    // Last-Modified date validator. A conditional request answers 304 when either
    // the client's If-None-Match matches the ETag, or - per RFC 9110, only when no
    // If-None-Match is present - its If-Modified-Since is not older than the file.
    time_t mtime = (time_t)(st.mtime);
    char etag[40];
    protocore_sb sb_etag = {etag, sizeof(etag), 0, PROTO_TRUE};
    Sb.put(&sb_etag, "\"");
    Sb.hex(&sb_etag, (uint64_t)((unsigned)file_size), 1);
    Sb.put(&sb_etag, "-");
    Sb.hex(&sb_etag, (uint64_t)((unsigned long)mtime), 1);
    Sb.put(&sb_etag, "\"");
    if (Sb.finish(&sb_etag) == 0)
    {
        etag[0] = '\0';
    }

    char lm_date[40];
    char lastmod_line[17 + sizeof(lm_date)]; // "Last-Modified: " + date + "\r\n" + NUL
    lastmod_line[0] = '\0';
    FileServing.http_rfc1123_args.epoch = mtime;
    FileServing.http_rfc1123_args.out = lm_date;
    FileServing.http_rfc1123_args.cap = sizeof(lm_date);
    file_serving_http_rfc1123(work);
    if (lm_date[0])
    {
        protocore_sb sb_lastmod_line = {lastmod_line, sizeof(lastmod_line), 0, PROTO_TRUE};
        Sb.put(&sb_lastmod_line, "Last-Modified: ");
        Sb.put(&sb_lastmod_line, lm_date);
        Sb.put(&sb_lastmod_line, "\r\n");
        if (Sb.finish(&sb_lastmod_line) == 0)
        {
            lastmod_line[0] = '\0';
        }
    }

    // Both reads are staged before the choice: each is a lookup over the request's own headers, so
    // taking both costs a scan and neither can be left in the middle of the conditional.
    HttpParser.get_header_args.req = &http_pool[slot_id];
    HttpParser.get_header_args.key = "If-None-Match";
    HttpParser.get_header(protocore_http_parser_span());
    const char *inm = HttpParser.text;
    HttpParser.get_header_args.req = &http_pool[slot_id];
    HttpParser.get_header_args.key = "If-Modified-Since";
    HttpParser.get_header(protocore_http_parser_span());
    const char *ims = HttpParser.text;
    proto_bool not_modified = inm ? inm_matches(inm, etag) : http_not_modified_since(mtime, ims);
    if (not_modified)
    {
        Fs.io.handle = fh;
        Fs.close(protocore_filesystem_span());
        char h304[RESP_HDR_BUF_SIZE];
        protocore_sb sb_h304 = {h304, sizeof(h304), 0, PROTO_TRUE};
        Sb.put(&sb_h304, "HTTP/1.1 304 Not Modified\r\nETag: ");
        Sb.put(&sb_h304, etag);
        Sb.put(&sb_h304, "\r\n");
        Sb.put(&sb_h304, lastmod_line);
        Sb.put(&sb_h304, protocore_resp_cache_control());
        Sb.put(&sb_h304, protocore_resp_cors_enabled() ? protocore_resp_cors_header() : "");
        Sb.put(&sb_h304, cl);
        Sb.put(&sb_h304, "\r\n");
        int n304 = (int)Sb.finish(&sb_h304);
        ConnPool.slot = slot_id;
        ConnPool.io.data = h304;
        ConnPool.io.len = (proto_u16)n304;
        ConnPool.send_flush(protocore_conn_pool_span()); // header-only reply: write and flush in one marshal
        protocore_resp_end(slot_id, 304, 0, keep, /*pre_flushed=*/PROTO_TRUE);
        return;
    }
    char etag_line[48];
    protocore_sb sb_etag_line = {etag_line, sizeof(etag_line), 0, PROTO_TRUE};
    Sb.put(&sb_etag_line, "ETag: ");
    Sb.put(&sb_etag_line, etag);
    Sb.put(&sb_etag_line, "\r\n");
    if (Sb.finish(&sb_etag_line) == 0)
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

#if PROTOCORE_ENABLE_RANGE
    accept_ranges = "Accept-Ranges: bytes\r\n"; // advertise range support on every file response
    size_t r_start = 0;
    size_t r_end = 0;
    HttpParser.get_header_args.req = &http_pool[slot_id];
    HttpParser.get_header_args.key = "Range";
    HttpParser.get_header(protocore_http_parser_span());
    HttpRange.http_parse_byte_range_args.hdr = HttpParser.text;
    HttpRange.http_parse_byte_range_args.size = file_size;
    HttpRange.http_parse_byte_range_args.out_start = &r_start;
    HttpRange.http_parse_byte_range_args.out_end = &r_end;
    HttpRange.http_parse_byte_range(http_range_work);
    int rr = HttpRange.n;
    if (rr < 0)
    {
        // Unsatisfiable range -> 416 with Content-Range: bytes */<size>.
        Fs.io.handle = fh;
        Fs.close(protocore_filesystem_span());
        char h416[RESP_HDR_BUF_SIZE];
        protocore_sb sb_h416 = {h416, sizeof(h416), 0, PROTO_TRUE};
        Sb.put(&sb_h416, "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */");
        Sb.u32(&sb_h416, (uint32_t)((unsigned)file_size));
        Sb.put(&sb_h416, "\r\nContent-Length: 0\r\n");
        Sb.put(&sb_h416, protocore_resp_cors_enabled() ? protocore_resp_cors_header() : "");
        Sb.put(&sb_h416, cl);
        Sb.put(&sb_h416, "\r\n");
        int n416 = (int)Sb.finish(&sb_h416);
        ConnPool.slot = slot_id;
        ConnPool.io.data = h416;
        ConnPool.io.len = (proto_u16)n416;
        ConnPool.send_flush(protocore_conn_pool_span());
        protocore_resp_end(slot_id, 416, 0, keep, /*pre_flushed=*/PROTO_TRUE);
        return;
    }
    if (rr > 0)
    {
        status = 206;
        body_len = r_end - r_start + 1;
        protocore_sb sb_range_line = {range_line, sizeof(range_line), 0, PROTO_TRUE};
        Sb.put(&sb_range_line, "Content-Range: bytes ");
        Sb.u32(&sb_range_line, (uint32_t)((unsigned)r_start));
        Sb.put(&sb_range_line, "-");
        Sb.u32(&sb_range_line, (uint32_t)((unsigned)r_end));
        Sb.put(&sb_range_line, "/");
        Sb.u32(&sb_range_line, (uint32_t)((unsigned)file_size));
        Sb.put(&sb_range_line, "\r\n");
        if (Sb.finish(&sb_range_line) == 0)
        {
            range_line[0] = '\0';
        }
        // A backend that cannot seek serves the whole representation instead, which keeps the body
        // matching the headers. RFC 9110 14.2 permits a server to ignore Range.
        Fs.io.handle = fh;
        Fs.io.off = (uint64_t)r_start;
        Fs.seek(protocore_filesystem_span());
        if (Fs.ok)
        {
            body_off = r_start;
        }
        else
        {
            status = 200;
            body_len = file_size;
            range_line[0] = '\0';
        }
    }
#endif

    char header[RESP_HDR_BUF_SIZE];
    protocore_sb sb_header = {header, sizeof(header), 0, PROTO_TRUE};
    Sb.put(&sb_header, "HTTP/1.1 ");
    Sb.i64(&sb_header, (int64_t)(status));
    Sb.put(&sb_header, " ");
    Http.code = status;
    Http.status_text(protocore_http_span());
    Sb.put(&sb_header, Http.text);
    Sb.put(&sb_header, "\r\nContent-Type: ");
    Sb.put(&sb_header, content_type);
    Sb.put(&sb_header, "\r\nContent-Length: ");
    Sb.u32(&sb_header, (uint32_t)((unsigned)body_len));
    Sb.put(&sb_header, "\r\n");
    Sb.put(&sb_header, accept_ranges);
    Sb.put(&sb_header, range_line);
    Sb.put(&sb_header, enc_line);
    Sb.put(&sb_header, etag_line);
    Sb.put(&sb_header, lastmod_line);
    Sb.put(&sb_header, protocore_resp_cache_control());
    Sb.put(&sb_header, protocore_resp_cors_enabled() ? protocore_resp_cors_header() : "");
    Sb.put(&sb_header, cl);
    Sb.put(&sb_header, "\r\n");
    int hlen = (int)Sb.finish(&sb_header);
    if (hlen == 0)
    {
        header[0] = '\0';
    }

    ConnPool.slot = slot_id;
    ConnPool.io.data = header;
    ConnPool.io.len = (proto_u16)hlen;
    ConnPool.send(protocore_conn_pool_span());

    // HEAD or empty body: headers only, finish now.
    if (head || body_len == 0)
    {
        Fs.io.handle = fh;
        Fs.close(protocore_filesystem_span());
        protocore_resp_end(slot_id, status, 0, keep, /*pre_flushed=*/PROTO_FALSE);
        return;
    }

    // Hand the body to the cross-loop pump: it pages out at most one send-buffer
    // window now and resumes on later loops as the window drains, so a file larger
    // than TCP_SND_BUF is never truncated. The pump owns the file and calls
    // protocore_resp_end() at completion - do not close f or end the response here.
    FileSend *s = &file_send[slot_id];
    s->fh = fh;
    s->off = body_off;
    s->remaining = body_len;
    s->status = status;
    s->total = (int)body_len;
    s->keep = keep;
    s->active = PROTO_TRUE;
    FileServing.file_send_pump_args.slot_id = slot_id;
    file_serving_file_send_pump(work);
}

// Page out a pending file response across worker loops: send up to ConnPool.sndbuf()
// bytes now and return; the next loop resumes (woken by the sent callback) until the
// whole body has been queued, then finish the response. Bounded per loop, never
// truncates, never blocks the worker.
static void file_serving_file_send_pump(uint8_t *restrict work)
{
    (void)work;
    uint8_t slot_id = FileServing.file_send_pump_args.slot_id;

    FileSend *s = &file_send[slot_id];
    if (!s->active)
    {
        return;
    }

    ConnPool.slot = slot_id;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPool.ok)
    {
        // Connection went away mid-transfer: drop the source and the continuation.
        Fs.io.handle = s->fh;
        Fs.close(protocore_filesystem_span());
        s->active = PROTO_FALSE;
        return;
    }

    // A file body still being paged out is active, not idle: keep the CONN_TIMEOUT_MS idle sweep
    // off it so a transient send stall on a large file cannot reap the slot mid-transfer.
    ConnPool.slot = slot_id;
    ConnPool.touch_active(protocore_conn_pool_span());

    uint8_t chunk[FILE_CHUNK_SIZE];
    while (s->remaining > 0)
    {
        ConnPool.slot = slot_id;
        ConnPool.sndbuf(protocore_conn_pool_span());
        proto_u16 avail = ConnPool.u16;
        if (avail == 0)
        {
            ConnPool.slot = slot_id;
            ConnPool.flush(protocore_conn_pool_span()); // push what is queued; resume on a later loop
            return;
        }
        size_t want = s->remaining < sizeof(chunk) ? s->remaining : sizeof(chunk);
        if (want > avail)
        {
            want = avail;
        }
        // The backend reports a fault as -1 and end-of-data as 0; both stop the transfer, and the
        // comparison is <= so a fault can never be added to the offset as a negative count.
        Fs.io.handle = s->fh;
        Fs.io.buf = chunk;
        Fs.io.n = want;
        Fs.read(protocore_filesystem_span());
        int n = Fs.i32;
        if (n <= 0)
        {
            s->remaining = 0; // read error / short file: stop (response will be short)
            break;
        }
        ConnPool.slot = slot_id;
        ConnPool.io.data = chunk;
        ConnPool.io.len = (proto_u16)n;
        ConnPool.send(protocore_conn_pool_span());
        if (!ConnPool.ok)
        {
            // Un-read the bytes that did not go out so the next loop resends them. A backend that
            // cannot rewind would resume at the wrong offset, so the transfer ends there instead.
            Fs.io.handle = s->fh;
            Fs.io.off = s->off;
            Fs.seek(protocore_filesystem_span());
            if (!Fs.ok)
            {
                Fs.io.handle = s->fh;
                Fs.close(protocore_filesystem_span());
                s->active = PROTO_FALSE;
                s->remaining = 0;
            }
            ConnPool.slot = slot_id;
            ConnPool.flush(protocore_conn_pool_span());
            return;
        }
        s->off += (size_t)(n);
        s->remaining -= (size_t)(n);
    }

    // Whole body queued: finish the response (flush, keep-alive/close, log, reset).
    Fs.io.handle = s->fh;
    Fs.close(protocore_filesystem_span());
    s->active = PROTO_FALSE;
    ConnPool.slot = slot_id;
    ConnPool.flush(protocore_conn_pool_span());
    protocore_resp_end(slot_id, s->status, s->total, s->keep, /*pre_flushed=*/PROTO_FALSE);
}

static void file_serving_serve_file(uint8_t *restrict work)
{
    uint8_t slot_id = FileServing.serve_file_args.slot_id;
    const protocore_mnt_backend *file_sys = FileServing.serve_file_args.file_sys;
    const char *fs_path = FileServing.serve_file_args.fs_path;
    const char *content_type = FileServing.serve_file_args.content_type;

    Http.slot = slot_id;
    Http.req_is_head(protocore_http_span());
    FileServing.serve_file_internal_args.slot_id = slot_id;
    FileServing.serve_file_internal_args.head = Http.ok;
    FileServing.serve_file_internal_args.file_sys = file_sys;
    FileServing.serve_file_internal_args.fs_path = fs_path;
    FileServing.serve_file_internal_args.content_type = content_type;
    FileServing.serve_file_internal_args.content_encoding = NULL;
    file_serving_serve_file_internal(work);
}

static void file_serving_serve_static(uint8_t *restrict work)
{
    (void)work;
    const char *url_prefix = FileServing.serve_static_args.url_prefix;
    const protocore_mnt_backend *file_sys = FileServing.serve_static_args.file_sys;
    const char *fs_root = FileServing.serve_static_args.fs_root;

    HttpRoutes.add(http_routes_work);
    HttpRoute *r = HttpRoutes.ptr;
    if (r == NULL)
    {
        return;
    }
    // Store the pattern as a wildcard so Http.path_matches() does a prefix match.
    //
    // The pattern is built BEFORE a route slot is taken, because a prefix that does not fit must
    // not be registered at all. Formatting this with snprintf truncated an over-long prefix to
    // MAX_PATH_LEN-1 and dropped the '*' with it, quietly turning a subtree mount into an
    // exact-match route for a path the caller never named - a route that serves something other
    // than what was asked for is worse than a route that does not exist.
    char pat[MAX_PATH_LEN];
    size_t n = str.len(url_prefix, MAX_PATH_LEN);
    protocore_sb sb_pat = {pat, sizeof(pat), 0, PROTO_TRUE};
    Sb.put(&sb_pat, url_prefix);
    if (n == 0 || url_prefix[n - 1] != '*')
    {
        Sb.put(&sb_pat, "*"); // not already a wildcard: append one
    }
    if (Sb.finish(&sb_pat) == 0)
    {
        return; // prefix + wildcard does not fit: register nothing
    }

    fill_route_base(r, pat);
    r->type = ROUTE_STATIC;
    r->method = HTTP_GET;
    Mnt.args.backend = file_sys;
    Mnt.args.root = fs_root;
    Mnt.point_add(mnt_work); // null backend is legal: whatever is mounted
    r->mnt_id = Mnt.u8;
}

static void file_serving_serve_static_request(uint8_t *restrict work)
{
    uint8_t slot_id = FileServing.serve_static_request_args.slot_id;
    HttpReq *req = FileServing.serve_static_request_args.req;
    const HttpRoute *r = FileServing.serve_static_request_args.r;

    // No null-check on the backend: storage is reached by layer, through the accessor, so a null
    // names a preference and never the path. A null one is what serve_static() documents as legal
    // and means "whatever is mounted"; 404-ing on it refused every request a caller made without
    // naming a backend it had no way to choose anyway.

    // Request path beyond the mount prefix (route path minus its trailing '*'). plen == 0 is
    // unreachable: serve_static() always stores at least "*" (it appends the wildcard when the
    // prefix lacks one), so the pattern is never empty.
    size_t plen = str.len(r->path, MAX_PATH_LEN);
    if (plen > 0 && r->path[plen - 1] == '*')
    {
        plen--;
    }
    const char *sub = (str.len(req->path, MAX_PATH_LEN) >= plen) ? req->path + plen : "";

    // Reject path traversal before touching the filesystem.
    if (str.has(sub, MAX_PATH_LEN - plen, "..", sizeof(".."), PROTO_FALSE))
    {
        send_text(slot_id, 404, PROTOCORE_MIME_TEXT_PLAIN, "Not Found");
        return;
    }

    Mnt.args.id = r->mnt_id;
    Mnt.root_of(mnt_work);
    const char *root = Mnt.text;
    size_t rlen = str.len(root, MAX_PATH_LEN);
    proto_bool root_slash = (rlen > 0 && root[rlen - 1] == '/');
    if (root_slash && sub[0] == '/') // avoid a doubled separator
    {
        sub++;
    }
    proto_bool sub_slash = (sub[0] == '/');
    const char *sep = (root_slash || sub_slash) ? "" : "/";

    // Directory or bare-prefix request → index.html.
    size_t slen = str.len(sub, MAX_PATH_LEN);
    proto_bool dir = (slen == 0) || (sub[slen - 1] == '/');

    // A path that does not fit is refused, not truncated: a clipped path names a different file,
    // and serving one the caller never asked for is worse than a 404.
    char fs_path[256];
    protocore_sb sb_path = {fs_path, sizeof(fs_path), 0, PROTO_TRUE};
    Sb.put(&sb_path, root);
    Sb.put(&sb_path, sep);
    Sb.put(&sb_path, sub);
    if (dir)
    {
        protocore_sb_lit(&sb_path, "index.html");
    }
    if (Sb.finish(&sb_path) == 0)
    {
        send_text(slot_id, 404, PROTOCORE_MIME_TEXT_PLAIN, "Not Found");
        return;
    }

    const char *ctype = mime_type(fs_path);
    Http.slot = slot_id;
    Http.req_is_head(protocore_http_span());
    proto_bool head = Http.ok;

    // Pre-compressed variant: serve <path>.gz if the client accepts gzip and it
    // exists. Content-Type stays that of the original (uncompressed) resource.
    HttpParser.get_header_args.req = req;
    HttpParser.get_header_args.key = "Accept-Encoding";
    HttpParser.get_header(protocore_http_parser_span());
    const char *ae = HttpParser.text;
    if (ae && str.has(ae, MAX_VAL_LEN, "gzip", sizeof("gzip"), PROTO_FALSE))
    {
        char gz[260];
        protocore_sb sb_gz = {gz, sizeof(gz), 0, PROTO_TRUE};
        Sb.put(&sb_gz, fs_path);
        Sb.put(&sb_gz, ".gz");
        int gn = (int)Sb.finish(&sb_gz);
        // Neither length half can fail: fs_path is a 256-byte buffer, so gn is at most 258 and
        // always under gz's 260. Both are kept because the two buffer sizes are independent
        // constants. The exclusion is per-line, so it also drops the exists() halves - those ARE
        // exercised both ways (see the gzip tests).
        Fs.path.root = file_root(work);
        Fs.path.dir = gz;
        Fs.path.name = "";
        Fs.exists(protocore_filesystem_span());
        if (gn > 0 && gn < (int)sizeof(gz) && Fs.ok)
        {
            Mnt.args.id = r->mnt_id;
            Mnt.point_of(mnt_work);
            FileServing.serve_file_internal_args.slot_id = slot_id;
            FileServing.serve_file_internal_args.head = head;
            FileServing.serve_file_internal_args.file_sys = Mnt.backend;
            FileServing.serve_file_internal_args.fs_path = gz;
            FileServing.serve_file_internal_args.content_type = ctype;
            FileServing.serve_file_internal_args.content_encoding = "gzip";
            file_serving_serve_file_internal(work);
            return;
        }
    }

    Mnt.args.id = r->mnt_id;
    Mnt.point_of(mnt_work);
    FileServing.serve_file_internal_args.slot_id = slot_id;
    FileServing.serve_file_internal_args.head = head;
    FileServing.serve_file_internal_args.file_sys = Mnt.backend;
    FileServing.serve_file_internal_args.fs_path = fs_path;
    FileServing.serve_file_internal_args.content_type = ctype;
    FileServing.serve_file_internal_args.content_encoding = NULL;
    file_serving_serve_file_internal(work);
}
FileServingNs FileServing = {
    .http_rfc1123 = file_serving_http_rfc1123,
    .serve_static_request = file_serving_serve_static_request,
    .serve_file_internal = file_serving_serve_file_internal,
    .file_send_pump = file_serving_file_send_pump,
    .holds_slot = file_serving_holds_slot,
    .serve_file = file_serving_serve_file,
    .serve_static = file_serving_serve_static,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FILE_SERVING
