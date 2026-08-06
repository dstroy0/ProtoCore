// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file response.c
 * @brief Response building: template rendering, chunked/streaming responses, response headers and
 *        cookies, MIME typing.
 *
 * The {{name}} template is walked twice - once to size the body, once to stream it - so a rendered
 * response costs no buffer regardless of its length. A chunked body that outruns the TCP send
 * window is paged across worker loops by the pump, whose per-slot state this file owns and no
 * other file can name.
 */

#include "mmgr/membuild.h"                 // pc_sb frame builder (replaces snprintf)
#include "network_drivers/transport/tcp.h" // conn_pool, Tcp.conn->send, TcpConn/ConnState
#include "protocore.h"
#include "network_drivers/presentation/http/http.h"
#include "shared_primitives/hex.h"  // pc_hex_u32 (chunk size-line writer)
#include "shared_primitives/mime.h" // PC_MIME_*, mime tables
#include "mmgr/protostr.h" // str.len: send_text measures the body it was handed

#if PC_ENABLE_METRICS || PC_ENABLE_STATS
#include "network_drivers/application/web_assets.h" // PC_STATS_JSON / PC_METRICS_PROM (generated)
#include "server/clock/clock.h"                     // pc_millis: the library clock, not the platform's

// Render @p v as decimal into the fixed field @p dst. Both exposition snapshots below fill a
// dozen of these. Unlike snprintf, pc_sb_finish() does NOT terminate when the value would not
// fit - it reports 0 and leaves the buffer untouched - so an over-long value must be turned
// into an empty field explicitly, or the exposition would serve the PREVIOUS snapshot's digits.
static void num_field(char *dst, size_t cap, uint32_t v)
{
    pc_sb b = {dst, cap, 0, PROTO_TRUE};
    pc_sb_u32(&b, v);
    if (pc_sb_finish(&b) == 0)
    {
        dst[0] = '\0';
    }
}
#endif

// ---------------------------------------------------------------------------
// Chunked-send state - owned here
// ---------------------------------------------------------------------------
//
// A chunked body larger than the TCP send window cannot go out in one dispatch, so send_chunked()
// records what remains and chunk_send_pump() pages it out across worker loops. The state is one
// entry per slot and nothing outside this file can name it: the poll asks pc_resp_holds_slot()
// instead of reading it.

// Per-slot chunked-send continuation: what is left to emit and how to frame it.
typedef struct
{
    ChunkSource source; ///< body generator (active==false means none).
    void *ctx;          ///< caller state passed to source (must outlive the send).
    int status;         ///< response status, for note_response.
    int total;          ///< body bytes emitted so far (excludes framing).
    proto_bool keep;    ///< keep-alive vs close at completion.
    proto_bool active;  ///< a chunked response is in progress on this slot.
    proto_bool raw;     ///< HTTP/1.0 client: stream the body unframed, close-delimited (no chunk wrapping).
} ChunkSend;

/** @brief The response state this TU owns: what is in flight, and what every response carries. */
typedef struct
{
    ChunkSend chunk[MAX_CONNS];

    // The header blocks every response emits. They live with the code that emits them rather than on
    // the server object they used to hang off, which is why the readers below are functions: a
    // caller needs the bytes, not the storage.
    proto_bool cors_enabled;
    char cors_header_buf[CORS_HDR_BUF_SIZE];
    char cache_control_buf[CACHE_CONTROL_BUF_SIZE];
    char extra_hdr[CONN_POOL_SLOTS][EXTRA_HDR_BUF_SIZE];
} RespCtx;
static RespCtx s_resp;

proto_bool pc_resp_cors_enabled(void)
{
    return s_resp.cors_enabled;
}

const char *pc_resp_cors_header(void)
{
    return s_resp.cors_header_buf;
}

const char *pc_resp_cache_control(void)
{
    return s_resp.cache_control_buf;
}

char *pc_resp_extra_hdr(uint8_t slot)
{
    return s_resp.extra_hdr[slot];
}

/*
 * Enable CORS and pre-build the Access-Control response header block.
 *
 * The header string is constructed once here rather than at response time, so the hot path emits
 * bytes it already has. It is injected verbatim into every response while cors_enabled is set.
 *
 * These two setters live beside the buffers they fill. They were in protocore.c, reaching across
 * for storage this file owns - which is why the readers above had to hand out a writable pointer to
 * a buffer whose capacity only this file knows. A reader hands back bytes; a writer is the owner.
 *
 * Passing an empty or null origin disables CORS: only the flag matters at dispatch time.
 *
 * @param origin Value for the Access-Control-Allow-Origin header, e.g. "*".
 */
void set_cors(const char *origin)
{
    if (!origin || origin[0] == '\0')
    {
        s_resp.cors_enabled = PROTO_FALSE;
        s_resp.cors_header_buf[0] = '\0';
        return;
    }
    pc_sb sb = {s_resp.cors_header_buf, sizeof(s_resp.cors_header_buf), 0, PROTO_TRUE};
    pc_sb_put(&sb, "Access-Control-Allow-Origin: ");
    pc_sb_put(&sb, origin);
    pc_sb_put(&sb, "\r\nAccess-Control-Allow-Methods: GET, POST, PUT, DELETE, PATCH, HEAD, "
                   "OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\n");
    if (pc_sb_finish(&sb) == 0)
    {
        s_resp.cors_header_buf[0] = '\0';
    }
    s_resp.cors_enabled = PROTO_TRUE;
}

void set_cache_control(const char *value)
{
    if (!value || value[0] == '\0')
    {
        s_resp.cache_control_buf[0] = '\0';
        return;
    }
    pc_sb sb = {s_resp.cache_control_buf, sizeof(s_resp.cache_control_buf), 0, PROTO_TRUE};
    pc_sb_put(&sb, "Cache-Control: ");
    pc_sb_put(&sb, value);
    pc_sb_put(&sb, "\r\n");
    if (pc_sb_finish(&sb) == 0)
    {
        s_resp.cache_control_buf[0] = '\0';
    }
}

proto_bool pc_resp_holds_slot(uint8_t slot)
{
    return s_resp.chunk[slot].active;
}

void pc_resp_reset(void)
{
    // Everything this file owns is per-run configuration or an in-flight transfer, and a test case
    // starts with neither. One store: the whole context is trivially copyable and its zero state is
    // its initial state (no chunk active, CORS off, no cached header text).
    s_resp = (RespCtx){0};
}

// ---------------------------------------------------------------------------
// Template rendering
//
// Walk a template once: when @p pcb is null only the output length is summed
// (pass 1); when @p pcb is set each literal run and resolved {{name}} value is
// written to it (pass 2). Walking twice avoids buffering the whole body, so
// memory use is constant. The resolver must be deterministic across the two
// passes. A "{{" with no matching "}}", or a name longer than 32 chars, is
// emitted literally.
// ---------------------------------------------------------------------------
// Consume one "{{name}}" placeholder at @p p (advancing it), sizing into @p total and, when
// @p emit, streaming the resolved value. An unterminated or over-long (> 32 char) name is emitted
// as a literal "{{" and the scan resumes just past it.
static void tmpl_take_placeholder(uint8_t slot, const char **p, TemplateVar resolver, proto_bool emit, size_t *total)
{
    const char *at = *p;
    const char *end = strstr(at + 2, "}}");
    size_t nlen = end ? (size_t)(end - (at + 2)) : 0;
    if (!end || nlen > 32)
    {
        // Unterminated or over-long placeholder: emit "{{" literally.
        *total += 2;
        if (emit)
        {
            Tcp.conn->send(slot, "{{", 2);
        }
        *p = at + 2;
        return;
    }
    char name[33];
    memcpy(name, at + 2, nlen);
    name[nlen] = '\0';
    const char *val = resolver ? resolver(name) : NULL;
    if (!val)
    {
        val = "";
    }
    // Bounded by what the send can carry, which is the width of its length parameter. Spelling that
    // as a literal stated the same bound twice, in two places that could disagree.
    size_t vlen = strnlen(val, UINT16_MAX);
    *total += vlen;
    if (emit && vlen)
    {
        Tcp.conn->send(slot, val, (proto_u16)vlen);
    }
    *p = end + 2;
}

// Two-pass: pass 1 sizes the body (emit=false), pass 2 streams it (emit=true).
static size_t tmpl_walk(uint8_t slot, const char *tmpl, TemplateVar resolver, proto_bool emit)
{
    size_t total = 0;
    const char *p = tmpl;
    while (*p)
    {
        if (p[0] == '{' && p[1] == '{')
        {
            tmpl_take_placeholder(slot, &p, resolver, emit, &total);
            continue;
        }

        // Literal run up to the next "{{".
        const char *run = p;
        while (*p && !(p[0] == '{' && p[1] == '{'))
        {
            p++;
        }
        size_t rlen = (size_t)(p - run);
        total += rlen;
        // "{{", so the scan loop above always advances p at least one byte and a literal run is
        // always >= 1. (The vlen test in tmpl_take_placeholder, which CAN be 0, is exercised.)
        if (emit && rlen)
        {
            Tcp.conn->send(slot, run, (proto_u16)rlen);
        }
    }
    return total;
}

void send_template(uint8_t slot_id, int code, const char *content_type, const char *tmpl, TemplateVar resolver)
{
    if (slot_id >= MAX_CONNS)
    {
        return;
    }
    if (!pc_conn_active(slot_id))
    {
        http_reset(slot_id);
        return;
    }

    // Pass 1: size the rendered body (no writes).
    size_t body_len = tmpl_walk(slot_id, tmpl, resolver, PROTO_FALSE);

    proto_bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    char header[RESP_HDR_BUF_SIZE];
    pc_sb hb = {header, RESP_HDR_BUF_SIZE, 0, PROTO_TRUE};
    pc_sb_lit(&hb, "HTTP/1.1 ");
    pc_sb_u32(&hb, (uint32_t)code);
    pc_sb_lit(&hb, " ");
    pc_sb_put(&hb, Http.status_text(code));
    pc_sb_lit(&hb, "\r\nContent-Type: ");
    pc_sb_put(&hb, content_type);
    pc_sb_lit(&hb, "\r\nContent-Length: ");
    pc_sb_u32(&hb, (uint32_t)body_len);
    pc_sb_lit(&hb, "\r\n");
    int hlen = (int)pc_sb_finish(&hb);
    hlen = proto_append_resp_trailer(header, RESP_HDR_BUF_SIZE, hlen, slot_id, cl);

    proto_bool head = Http.req_is_head(slot_id);

    Tcp.conn->send(slot_id, header, (proto_u16)hlen);
    // Pass 2: stream the rendered body (HEAD carries headers only).
    if (!head && body_len > 0)
    {
        tmpl_walk(slot_id, tmpl, resolver, PROTO_TRUE);
    }

    pc_resp_end(slot_id, code, (int)body_len, keep, /*pre_flushed=*/PROTO_FALSE);
}

// ---------------------------------------------------------------------------
// Chunked (streaming) responses
//
// send_chunked() writes the headers, then pulls the body from a ChunkSource one
// piece at a time, emitting each as an HTTP/1.1 chunk ("<hexlen>\r\n<data>\r\n",
// RFC 7230 §4.1) and finally the terminating "0\r\n\r\n". Like the file pump, the
// body pages across worker loops as the TCP send window drains (chunk_send_pump,
// resumed by the sent callback), so a response is unbounded in constant memory and
// never truncated at the window. The source's ctx must outlive the response (see
// ChunkSource). One chunked response per slot at a time.
// ---------------------------------------------------------------------------

void send_chunked(uint8_t slot_id, int code, const char *content_type, ChunkSource source, void *ctx)
{
    if (slot_id >= MAX_CONNS)
    {
        return;
    }
    if (!pc_conn_active(slot_id))
    {
        http_reset(slot_id);
        return;
    }

    proto_bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    // RFC 7230 3.3.1: chunked is an HTTP/1.1 transfer-coding - it MUST NOT be sent
    // to an HTTP/1.0 (or unknown-version) client. Fall back to a close-delimited
    // body: omit Transfer-Encoding, force Connection: close, stream the body
    // unframed, and signal its end by closing the connection (RFC 7230 3.3.3).
    proto_bool raw = (http_pool[slot_id].version != HTTP_11);

    char header[RESP_HDR_BUF_SIZE];
    pc_sb hb2 = {header, RESP_HDR_BUF_SIZE, 0, PROTO_TRUE};
    if (raw)
    {
        keep = PROTO_FALSE; // close-delimited: the connection close IS the message boundary
        cl = "Connection: close\r\n";
        pc_sb_put(&hb2, "HTTP/1.0 ");
    }
    else
    {
        pc_sb_put(&hb2, "HTTP/1.1 ");
    }
    pc_sb_u32(&hb2, (uint32_t)code);
    pc_sb_put(&hb2, " ");
    pc_sb_put(&hb2, Http.status_text(code));
    pc_sb_put(&hb2, "\r\nContent-Type: ");
    pc_sb_put(&hb2, content_type);
    pc_sb_put(&hb2, raw ? "\r\n" : "\r\nTransfer-Encoding: chunked\r\n");
    int hlen = (int)pc_sb_finish(&hb2);
    hlen = proto_append_resp_trailer(header, RESP_HDR_BUF_SIZE, hlen, slot_id, cl);

    Tcp.conn->send(slot_id, header, (proto_u16)hlen);

    // HEAD carries the headers but no body or terminator.
    if (Http.req_is_head(slot_id) || !source)
    {
        pc_resp_end(slot_id, code, 0, keep, /*pre_flushed=*/PROTO_FALSE);
        return;
    }

    ChunkSend *s = &s_resp.chunk[slot_id];
    s->source = source;
    s->ctx = ctx;
    s->status = code;
    s->total = 0;
    s->keep = keep;
    s->active = PROTO_TRUE;
    s->raw = raw;
    chunk_send_pump(slot_id);
}

// Page a pending chunked response: pull pieces from the source and frame them into
// the send window each worker loop, resuming on later loops as the window drains.
void chunk_send_pump(uint8_t slot_id)
{
    ChunkSend *s = &s_resp.chunk[slot_id];
    if (!s->active)
    {
        return;
    }

    if (!pc_conn_active(slot_id))
    {
        s->active = PROTO_FALSE; // connection gone mid-stream
        return;
    }

    // A body still being paged out is active, not idle: keep the CONN_TIMEOUT_MS idle sweep off
    // it so a transient send stall on a large stream cannot reap the slot mid-transfer.
    Tcp.conn->touch_active(slot_id);

    // Frame each chunk in ONE buffer so it goes out in a single tcpip_thread round-trip (was three -
    // size line, body, CRLF - each a ~23 us marshal on-device). Reserve CHUNK_HDR_RESERVE bytes ahead
    // of the body for the "<hex>\r\n" size line and 2 after for the trailing CRLF, so the source writes
    // the body in place and the whole "<hex>\r\n<body>\r\n" is one Tcp.conn->send with no extra copy.
    // FRAME reserves send-window room for that framing; the raw (HTTP/1.0) path sends the body verbatim.
    static const proto_u16 CHUNK_HDR_RESERVE = 8; // "<hex>\r\n" is <= 6 bytes for a chunk <= 0xFFFF
    const proto_u16 FRAME = s->raw ? 0 : 12;
    uint8_t framed[CHUNK_HDR_RESERVE + CHUNK_BUF_SIZE + 2];
    for (;;)
    {
        proto_u16 avail = Tcp.conn->sndbuf(slot_id);
        if (avail <= FRAME)
        {
            Tcp.conn->flush(slot_id); // no room for a useful chunk; resume next loop
            return;
        }
        size_t cap = (size_t)(avail - FRAME);
        if (cap > CHUNK_BUF_SIZE)
        {
            cap = CHUNK_BUF_SIZE;
        }

        uint8_t *body = framed + CHUNK_HDR_RESERVE;
        size_t n = s->source(body, cap, s->ctx);
        if (n == 0)
        {
            if (!s->raw)
            {
                Tcp.conn->send(slot_id, "0\r\n\r\n", 5); // terminating chunk (1.1 only)
            }
            Tcp.conn->flush(slot_id);
            s->active = PROTO_FALSE;
            pc_resp_end(slot_id, s->status, s->total, s->keep,
                        /*pre_flushed=*/PROTO_FALSE); // raw: keep==false -> connection close ends the body
            return;
        }
        if (n > cap)
        {
            n = cap; // defensive: a misbehaving source must not overrun the window
        }

        if (s->raw)
        {
            Tcp.conn->send(slot_id, body, (proto_u16)n); // close-delimited: no chunk framing
        }
        else
        {
            // Prepend the size line (right-justified against the body) + append the trailing CRLF,
            // then send the framed chunk in one call. The size line is a hand-written hex (pc_hex_u32),
            // not snprintf("%x") - the format-string parse dwarfed the few nibble writes on the hot
            // per-chunk path (performance_benching/server/send_pump: ~9x on the host, more on the ESP32).
            char digits[8];
            size_t nd = pc_hex_u32((uint32_t)n, digits);
            size_t sn = nd + 2; // "<hex>\r\n"
            uint8_t *start = body - sn;
            memcpy(start, digits, nd);
            start[nd] = '\r';
            start[nd + 1] = '\n';
            body[n] = '\r';
            body[n + 1] = '\n';
            Tcp.conn->send(slot_id, start, (proto_u16)(sn + n + 2));
        }
        s->total += (int)n;
    }
}

// ---------------------------------------------------------------------------
// Custom response headers / cookies
//
// Appended to a fixed per-slot buffer during a handler and injected into the
// send paths above. A header that would overflow the buffer is dropped whole
// (the buffer is rewound to its prior length) so a malformed half-line never
// reaches the wire.
// ---------------------------------------------------------------------------

void proto_add_response_header(uint8_t slot_id, const char *name, const char *value)
{
    if (slot_id >= MAX_CONNS || name == NULL || value == NULL)
    {
        return;
    }

    char *buf = s_resp.extra_hdr[slot_id];
    size_t used = strnlen(buf, EXTRA_HDR_BUF_SIZE);
    size_t room = EXTRA_HDR_BUF_SIZE - used;
    pc_sb hb3 = {buf + used, room, 0, PROTO_TRUE};
    pc_sb_put(&hb3, name);
    pc_sb_put(&hb3, ": ");
    pc_sb_put(&hb3, value);
    pc_sb_put(&hb3, "\r\n");
    // A latched builder may have written the pieces that did fit, so rewinding to `used` is what
    // drops the header whole rather than leaving a truncated one.
    if (pc_sb_finish(&hb3) == 0)
    {
        buf[used] = '\0';
    }
}

void set_cookie(uint8_t slot_id, const char *name, const char *value, const char *attrs)
{
    if (slot_id >= MAX_CONNS || name == NULL || value == NULL)
    {
        return;
    }

    char *buf = s_resp.extra_hdr[slot_id];
    size_t used = strnlen(buf, EXTRA_HDR_BUF_SIZE);
    size_t room = EXTRA_HDR_BUF_SIZE - used;
    pc_sb cb = {buf + used, room, 0, PROTO_TRUE};
    pc_sb_put(&cb, "Set-Cookie: ");
    pc_sb_put(&cb, name);
    pc_sb_put(&cb, "=");
    pc_sb_put(&cb, value);
    if (attrs != NULL && attrs[0] != '\0')
    {
        pc_sb_put(&cb, "; ");
        pc_sb_put(&cb, attrs);
    }
    pc_sb_put(&cb, "\r\n");
    if (pc_sb_finish(&cb) == 0)
    {
        buf[used] = '\0'; // would not fit: drop this cookie entirely
    }
}

void clear_response_headers(uint8_t slot_id)
{
    if (slot_id >= MAX_CONNS)
    {
        return;
    }
    s_resp.extra_hdr[slot_id][0] = '\0';
}

// ---------------------------------------------------------------------------
// MIME type lookup by extension
// ---------------------------------------------------------------------------

const char *mime_type(const char *path)
{
    if (!path)
    {
        return PC_MIME_OCTET_STREAM;
    }

    // Find the last '.' after the last '/'.
    const char *dot = NULL;
    for (const char *p = path; *p; p++)
    {
        if (*p == '/')
        {
            dot = NULL;
        }
        else if (*p == '.')
        {
            dot = p;
        }
    }
    if (!dot || dot[1] == '\0')
    {
        return PC_MIME_OCTET_STREAM;
    }
    const char *ext = dot + 1;

    // Case-insensitive compare against a small static table.
    static const struct
    {
        const char *ext;
        const char *type;
    } table[] = {
        {"html", PC_MIME_TEXT_HTML}, {"htm", PC_MIME_TEXT_HTML},   {"css", "text/css"},
        {"js", PC_MIME_JAVASCRIPT},  {"mjs", PC_MIME_JAVASCRIPT},  {"json", PC_MIME_JSON},
        {"xml", "application/xml"},  {"txt", PC_MIME_TEXT_PLAIN},  {"csv", "text/csv"},
        {"svg", "image/svg+xml"},    {"png", "image/png"},         {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},      {"gif", "image/gif"},         {"ico", "image/x-icon"},
        {"webp", "image/webp"},      {"wasm", "application/wasm"}, {"woff", "font/woff"},
        {"woff2", "font/woff2"},     {"ttf", "font/ttf"},          {"pdf", "application/pdf"},
        {"gz", "application/gzip"},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
    {
        const char *a = ext;
        const char *b = table[i].ext;
        proto_bool eq = PROTO_TRUE;
        while (*a && *b)
        {
            char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
            char cb = *b; // table is already lowercase
            if (ca != cb)
            {
                eq = PROTO_FALSE;
                break;
            }
            a++;
            b++;
        }
        if (eq && *a == '\0' && *b == '\0')
        {
            return table[i].type;
        }
    }
    return PC_MIME_OCTET_STREAM;
}

// ---------------------------------------------------------------------------
// Runtime stats endpoint
// ---------------------------------------------------------------------------

#if PC_ENABLE_STATS
// The stats body is an editable template asset (web_assets/input/PC_STATS_JSON.json)
// rendered through the {{name}} engine, like /metrics - values are substituted by
// name, with no printf-format coupling. Snapshot into statics just before the
// (twice-invoked, size + emit) resolver runs.
typedef struct
{
    char uptime[12];
    char requests[12];
    char n2xx[12];
    char n4xx[12];
    char n5xx[12];
    char active[8];
    char heap[12];
} StatsCtx;
static StatsCtx s_stats;

static const char *stats_var(const char *name)
{
    if (!strcmp(name, "uptime_ms"))
    {
        return s_stats.uptime;
    }
    if (!strcmp(name, "requests"))
    {
        return s_stats.requests;
    }
    if (!strcmp(name, "http_2xx"))
    {
        return s_stats.n2xx;
    }
    if (!strcmp(name, "http_4xx"))
    {
        return s_stats.n4xx;
    }
    if (!strcmp(name, "http_5xx"))
    {
        return s_stats.n5xx;
    }
    if (!strcmp(name, "active_conns"))
    {
        return s_stats.active;
    }
    // The not-found tail is unreachable: stats_var is only ever invoked by stats() against
    // PC_STATS_JSON, and that asset's seven placeholders are exactly the seven names tested here,
    // so the last one always matches. Kept because the resolver has to answer an unknown name.
    if (!strcmp(name, "free_heap"))
    {
        return s_stats.heap;
    }
    return NULL;
}

void stats(uint8_t slot_id)
{
    int active = Tcp.conn->active_count();

    unsigned long up = pc_millis();
#if PROTOCORE_HOT
    uint32_t heap = ESP.getFreeHeap();
#else
    uint32_t heap = 0;
#endif

    // One read of the bucket, not four reads of four owners: a report that gathered field by field
    // could straddle two server states while it was still formatting the first.
    pc_signal_snapshot sig;
    pc_signal_know(&sig);

    // millis() is a 32-bit tick counter, so the uptime field wraps with it.
    num_field(s_stats.uptime, sizeof(s_stats.uptime), (uint32_t)up);
    num_field(s_stats.requests, sizeof(s_stats.requests), sig.requests_total);
    num_field(s_stats.n2xx, sizeof(s_stats.n2xx), sig.responses_2xx);
    num_field(s_stats.n4xx, sizeof(s_stats.n4xx), sig.responses_4xx);
    num_field(s_stats.n5xx, sizeof(s_stats.n5xx), sig.responses_5xx);
    num_field(s_stats.active, sizeof(s_stats.active), (uint32_t)(active < 0 ? 0 : active));
    num_field(s_stats.heap, sizeof(s_stats.heap), heap);

    send_template(slot_id, 200, PC_MIME_JSON, PC_STATS_JSON, stats_var);
}
#endif // PC_ENABLE_STATS

#if PC_ENABLE_METRICS
// The Prometheus exposition is an editable template asset (web_assets/input/
// PC_METRICS_PROM.txt) rendered through the {{name}} engine, so values are
// substituted by name (no printf format coupling). metrics() snapshots the live
// values into these statics just before send_template(), which invokes the
// resolver twice (size + emit) - deterministic because the snapshot is fixed.
typedef struct
{
    char uptime[12];
    char requests[12];
    char n2xx[12];
    char n4xx[12];
    char n5xx[12];
    char active[8];
    char max[8];
    char heap[12];
    char minheap[12];
    char heapsize[12];
    char maxalloc[12];
} MetricsCtx;
static MetricsCtx s_metrics;

static const char *metrics_var(const char *name)
{
    if (!strcmp(name, "uptime_seconds"))
    {
        return s_metrics.uptime;
    }
    if (!strcmp(name, "requests_total"))
    {
        return s_metrics.requests;
    }
    if (!strcmp(name, "resp_2xx"))
    {
        return s_metrics.n2xx;
    }
    if (!strcmp(name, "resp_4xx"))
    {
        return s_metrics.n4xx;
    }
    if (!strcmp(name, "resp_5xx"))
    {
        return s_metrics.n5xx;
    }
    if (!strcmp(name, "active_conns"))
    {
        return s_metrics.active;
    }
    if (!strcmp(name, "max_conns"))
    {
        return s_metrics.max;
    }
    if (!strcmp(name, "free_heap"))
    {
        return s_metrics.heap;
    }
    if (!strcmp(name, "min_free_heap"))
    {
        return s_metrics.minheap;
    }
    if (!strcmp(name, "heap_size"))
    {
        return s_metrics.heapsize;
    }
    // The not-found tail is unreachable: metrics_var is only ever driven by the placeholders in
    // PC_METRICS_PROM.txt, and every one of the 11 resolves to a case above. That is not an
    // assumption - test_metrics_emits_prometheus asserts every emitted sample line carries a
    // value, which fails the moment a placeholder stops resolving (as three of them silently did
    // until the resolver names were aligned with the template).
    if (!strcmp(name, "max_alloc_heap"))
    {
        return s_metrics.maxalloc;
    }
    return NULL;
}

void metrics(uint8_t slot_id)
{
    int active = Tcp.conn->active_count();

    unsigned long up = pc_millis();
#if PROTOCORE_HOT
    uint32_t heap = ESP.getFreeHeap();
    uint32_t min_heap = ESP.getMinFreeHeap();
    uint32_t heap_size = ESP.getHeapSize();
    uint32_t max_alloc = ESP.getMaxAllocHeap();
#else
    uint32_t heap = 0;
    uint32_t min_heap = 0;
    uint32_t heap_size = 0;
    uint32_t max_alloc = 0;
#endif

    pc_signal_snapshot sig;
    pc_signal_know(&sig);

    num_field(s_metrics.uptime, sizeof(s_metrics.uptime), (uint32_t)(up / 1000UL));
    num_field(s_metrics.requests, sizeof(s_metrics.requests), sig.requests_total);
    num_field(s_metrics.n2xx, sizeof(s_metrics.n2xx), sig.responses_2xx);
    num_field(s_metrics.n4xx, sizeof(s_metrics.n4xx), sig.responses_4xx);
    num_field(s_metrics.n5xx, sizeof(s_metrics.n5xx), sig.responses_5xx);
    num_field(s_metrics.active, sizeof(s_metrics.active), (uint32_t)(active < 0 ? 0 : active));
    num_field(s_metrics.max, sizeof(s_metrics.max), (uint32_t)MAX_CONNS);
    num_field(s_metrics.heap, sizeof(s_metrics.heap), heap);
    num_field(s_metrics.minheap, sizeof(s_metrics.minheap), min_heap);
    num_field(s_metrics.heapsize, sizeof(s_metrics.heapsize), heap_size);
    num_field(s_metrics.maxalloc, sizeof(s_metrics.maxalloc), max_alloc);

    send_template(slot_id, 200, "text/plain; version=0.0.4; charset=utf-8", PC_METRICS_PROM, metrics_var);
}
#endif // PC_ENABLE_METRICS

// Finish a response: flush, then either begin the graceful CONN_CLOSING dwell
// (close path) or leave the slot active for reuse (keep-alive). The HTTP parser
// is reset either way, returning a kept-alive slot to PARSE_METHOD ready for the
// next request. The slot stays CONN_ACTIVE through the write on BOTH paths so its
// callbacks stay live; the close path then dwells in CONN_CLOSING from here, so the
// slot is reclaimed only once the peer ACKs the response (or the CLOSING timeout fires), not
// before it is delivered.
//
// The connection is addressed by slot alone and the transport resolves the pcb internally, the
// same way the RX read path does: no pcb is threaded through the app layer, so the send target
// cannot disagree with the slot.
void pc_resp_end(uint8_t slot_id, int code, int body_len, proto_bool keep, proto_bool pre_flushed)
{
    if (!pre_flushed)
    {
        Tcp.conn->flush(slot_id); // a pre_flushed caller already did tcp_output in its final send
    }
    if (!keep)
    {
        Tcp.conn->begin_close(slot_id); // ACTIVE -> CONN_CLOSING; finalizes on ACK
    }
    note_response(slot_id, code, body_len);
    http_reset(slot_id);
}

// Resolve the Connection response header (and report keep-alive intent) in one
// place so every response path agrees. Keep-alive compiled out always closes.
const char *pc_resp_conn_hdr(uint8_t slot_id, proto_bool *keep_out)
{
    proto_bool keep = PROTO_FALSE;
#if PC_ENABLE_KEEPALIVE
    keep = keepalive_eval(slot_id);
#else
    (void)slot_id;
#endif
    // The null half cannot fire: every call site passes the address of its own local `keep`. Kept so
    // the signature keeps saying the report-back is optional.
    if (keep_out)
    {
        *keep_out = keep;
    }
    return keep ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
}

// Append the shared response trailer (CORS block + custom headers + Connection +
// the terminating blank line) to a header buffer already holding the status line
// and per-response headers. One owner for the trailer every dynamic response ends
// with. Returns the new total length.
const char PC_RESP_HDR_OVERFLOW[] = "HTTP/1.1 500 Internal Server Error\r\n"
                                    "Content-Length: 0\r\n"
                                    "Connection: close\r\n\r\n";
// Taken with sizeof at the definition, where the array bound is still visible. The send site sees
// only `extern const char[]`, so measuring it there would mean scanning a string whose length was
// known when it was written.
const size_t PC_RESP_HDR_OVERFLOW_LEN = sizeof(PC_RESP_HDR_OVERFLOW) - 1;

int proto_append_resp_trailer(char *buf, size_t cap, int hlen, uint8_t slot_id, const char *cl)
{
    // hlen is the caller's status-line length from pc_sb_finish, which reports 0 for a status line
    // that did not fit. Appending the trailer at offset 0 in that case would emit a response with
    // no status line at all, so 0 propagates as failure and the caller sends a canned reply.
    //
    // A response either fits or is refused; it is never clamped to cap and sent. A header block cut
    // mid-field has no terminating CRLF, so the peer reads the body as continued headers and the
    // connection desynchronizes - worse than sending nothing.
    if (hlen <= 0)
    {
        return 0;
    }
    if ((size_t)hlen >= cap)
    {
        return 0;
    }
#if PC_HTTP_EMIT_DATE
    // RFC 7231 7.1.1.2: emit Date only when a real wall-clock time exists; a clock-less device (no
    // synced/valid time source yet) omits it. The time comes from the multi-source registry (any
    // enabled NTP / GPS / RTC / ... by priority) when PC_ENABLE_TIME_SOURCE is set, else straight
    // from NTP.
    char date_hdr[48] = "";
    char imf[40];
#if PC_ENABLE_TIME_SOURCE
    if (pc_time_http_date(imf, sizeof(imf)) > 0)
#else
    if (pc_ntp_http_date(imf, sizeof(imf)) > 0)
#endif
    {
        pc_sb sb_date_hdr = {date_hdr, sizeof(date_hdr), 0, PROTO_TRUE};
        pc_sb_put(&sb_date_hdr, "Date: ");
        pc_sb_put(&sb_date_hdr, imf);
        pc_sb_put(&sb_date_hdr, "\r\n");
        if (pc_sb_finish(&sb_date_hdr) == 0)
        {
            date_hdr[0] = '\0';
        }
    }
#else
    const char *date_hdr = "";
#endif
    pc_sb sb411 = {buf + hlen, cap - (size_t)hlen, 0, PROTO_TRUE};
    pc_sb_put(&sb411, date_hdr);
    pc_sb_put(&sb411, pc_resp_cors_enabled() ? pc_resp_cors_header() : "");
    pc_sb_put(&sb411, pc_resp_extra_hdr(slot_id));
    pc_sb_put(&sb411, cl);
    pc_sb_put(&sb411, "\r\n");
    int n = (int)pc_sb_finish(&sb411);
    if (!sb411.ok)
    {
        return 0; // trailer does not fit: refuse the response rather than send a headless one
    }
    return hlen + n;
}

/**
 * @brief Send an HTTP response whose body is a null-terminated string.
 *
 * @param slot_id      Connection slot index.
 * @param code         HTTP status code, e.g. 200.
 * @param content_type MIME type string, e.g. "application/json".
 * @param payload      Null-terminated body string to send; null sends an empty body.
 */
void send_text(uint8_t slot_id, int code, const char *content_type, const char *payload)
{
    // Null-terminated convenience wrapper over the explicit-length send: the only difference between
    // the two is who scans for the length, so text is bin plus one scan rather than a second sender.
    // 0xFFFF is how far the scan is willing to look, not a claim the caller's string is that long:
    // a body is a handler's string of unstated capacity, and the bound is what keeps a missing
    // terminator from becoming an unbounded walk.
    send_bin(slot_id, code, content_type, (const uint8_t *)payload,
             (payload != NULL) ? str.len(payload, 0xFFFF) : 0);
}

void send_bin(uint8_t slot_id, int code, const char *content_type, const uint8_t *body, size_t body_len)
{
    if (slot_id >= CONN_POOL_SLOTS)
    {
        return; // guard the public entry: never index conn_pool out of range
    }
    const char *payload = (const char *)body;
    TcpConn *conn = &conn_pool[slot_id];
#if PC_ENABLE_HTTP2 || PC_ENABLE_HTTP3
    // A self-framing protocol (HTTP/2, HTTP/3) installed its own response sink at negotiation /
    // dispatch time; route through it and let it own its framing + connection lifecycle. This runs
    // before the HTTP/1.1 pcb check because that check is a TCP-transport concern (the HTTP/3 slot
    // has no pcb by design, and an h2 connection manages its own).
    if (conn->pc_resp_sink)
    {
        conn->pc_resp_sink(slot_id, code, content_type, payload, body_len);
        return;
    }
#endif
    if (conn->state != CONN_ACTIVE || conn->pcb == NULL)
    {
        http_reset(slot_id);
        return;
    }

    int payload_len = (int)(body_len > 0xFFFF ? 0xFFFF : body_len);

    proto_bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    char header[RESP_HDR_BUF_SIZE];
    pc_sb sb_header2 = {header, sizeof(header), 0, PROTO_TRUE};
    pc_sb_put(&sb_header2, "HTTP/1.1 ");
    pc_sb_i64(&sb_header2, (int64_t)(code));
    pc_sb_put(&sb_header2, " ");
    pc_sb_put(&sb_header2, Http.status_text(code));
    pc_sb_put(&sb_header2, "\r\nContent-Type: ");
    pc_sb_put(&sb_header2, content_type);
    pc_sb_put(&sb_header2, "\r\nContent-Length: ");
    pc_sb_i64(&sb_header2, (int64_t)(payload_len));
    pc_sb_put(&sb_header2, "\r\n");
    int hlen = (int)pc_sb_finish(&sb_header2);
    hlen = proto_append_resp_trailer(header, sizeof(header), hlen, slot_id, cl);
    if (hlen == 0)
    {
        // The headers do not fit RESP_HDR_BUF_SIZE (an over-long content type, or a custom-header
        // block that filled the buffer). Truncating them would emit a header block with no
        // terminating CRLF and desync the connection, so a fixed reply that always fits goes out
        // instead and the connection closes.
        Tcp.conn->send_flush(slot_id, PC_RESP_HDR_OVERFLOW, (proto_u16)PC_RESP_HDR_OVERFLOW_LEN);
        pc_resp_end(slot_id, 500, 0, PROTO_FALSE, /*pre_flushed=*/PROTO_FALSE);
        return;
    }

    // The slot stays CONN_ACTIVE through the write for both paths; pc_resp_end then
    // begins the CONN_CLOSING dwell on the close path (finalized once ACKed).

    proto_bool head = Http.req_is_head(slot_id);

    // HEAD responses carry the headers (incl. Content-Length) but no body. For a
    // body that fits the header scratch, coalesce headers+body into a single send
    // so the response costs one tcpip_thread round-trip rather than two. The final
    // write carries the flush (Tcp.conn->send_flush) and pc_resp_end skips it, so a
    // small keep-alive response is one marshal (write+output).
    if (!head && payload_len > 0 && (size_t)hlen + (size_t)payload_len <= sizeof(header))
    {
        proto_raw_read(header + hlen, payload, (size_t)payload_len);
        Tcp.conn->send_flush(slot_id, header, (proto_u16)(hlen + payload_len));
    }
    else if (!head && payload_len > 0)
    {
        Tcp.conn->send(slot_id, header, (proto_u16)hlen);
        Tcp.conn->send_flush(slot_id, payload, (proto_u16)payload_len);
    }
    else
    {
        Tcp.conn->send_flush(slot_id, header, (proto_u16)hlen);
    }

    pc_resp_end(slot_id, code, payload_len, keep, /*pre_flushed=*/PROTO_TRUE);
}

/**
 * @brief Send a status-line-and-headers response with `Content-Length: 0`.
 *
 * Used for CORS preflight (204) and any response where only status headers are needed. Takes the
 * same slot lifecycle as send_bin(): a self-framing protocol's sink wins if one is installed, an
 * inactive slot is reset without writing, and pc_resp_end() owns the close-or-recycle decision.
 *
 * @param slot_id Connection slot index.
 * @param code    HTTP status code, e.g. 204.
 */
void send_empty(uint8_t slot_id, int code)
{
    if (slot_id >= CONN_POOL_SLOTS)
    {
        return;
    }
    TcpConn *conn = &conn_pool[slot_id];
#if PC_ENABLE_HTTP2 || PC_ENABLE_HTTP3
    if (conn->pc_resp_sink)
    {
        conn->pc_resp_sink(slot_id, code, "text/plain", "", 0);
        return;
    }
#endif
    if (conn->state != CONN_ACTIVE || conn->pcb == NULL)
    {
        http_reset(slot_id);
        return;
    }

    proto_bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    char header[RESP_HDR_BUF_SIZE];
    pc_sb sb_header3 = {header, sizeof(header), 0, PROTO_TRUE};
    pc_sb_put(&sb_header3, "HTTP/1.1 ");
    pc_sb_i64(&sb_header3, (int64_t)(code));
    pc_sb_put(&sb_header3, " ");
    pc_sb_put(&sb_header3, Http.status_text(code));
    pc_sb_put(&sb_header3, "\r\nContent-Length: 0\r\n");
    int hlen = (int)pc_sb_finish(&sb_header3);
    hlen = proto_append_resp_trailer(header, sizeof(header), hlen, slot_id, cl);

    Tcp.conn->send_flush(slot_id, header, (proto_u16)hlen);

    pc_resp_end(slot_id, code, 0, keep, /*pre_flushed=*/PROTO_TRUE);
}

void redirect(uint8_t slot_id, int code, const char *location)
{
    if (slot_id >= MAX_CONNS)
    {
        return;
    }
    TcpConn *conn = &conn_pool[slot_id];
    if (conn->state != CONN_ACTIVE || conn->pcb == NULL)
    {
        http_reset(slot_id);
        return;
    }

    // Only the redirect status codes are valid here; anything else → 302.
    switch (code)
    {
    case 301:
    case 302:
    case 303:
    case 307:
    case 308:
        break;
    default:
        code = 302;
        break;
    }

    proto_bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    char header[RESP_HDR_BUF_SIZE];
    pc_sb sb_header4 = {header, sizeof(header), 0, PROTO_TRUE};
    pc_sb_put(&sb_header4, "HTTP/1.1 ");
    pc_sb_i64(&sb_header4, (int64_t)(code));
    pc_sb_put(&sb_header4, " ");
    pc_sb_put(&sb_header4, Http.status_text(code));
    pc_sb_put(&sb_header4, "\r\nLocation: ");
    pc_sb_put(&sb_header4, location);
    pc_sb_put(&sb_header4, "\r\nContent-Length: 0\r\n");
    int hlen = (int)pc_sb_finish(&sb_header4);
    hlen = proto_append_resp_trailer(header, sizeof(header), hlen, slot_id, cl);

    Tcp.conn->send_flush(slot_id, header, (proto_u16)hlen);

    pc_resp_end(slot_id, code, 0, keep, /*pre_flushed=*/PROTO_TRUE);
}
