// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file response.cpp
 * @brief Response building for PC: template rendering, chunked/streaming responses,
 *        response headers + cookies, MIME typing, and the stats / Prometheus-metrics endpoints.
 *
 * Split out of protocore.cpp (single-purpose server files). The two-pass {{name}} template walk,
 * the cross-loop chunked-send pump (paged like the file pump so a body is unbounded in constant
 * memory), the per-response header/cookie buffer API, mime_type(), and the built-in stats/metrics
 * handlers (rendered through the template engine from generated web assets). The chunked pump
 * shares the per-slot SendCtx owned by protocore.cpp. Behavior is identical to the pre-split code.
 */

#include "network_drivers/transport/tcp.h" // conn_pool, pc_conn_send, TcpConn/ConnState
#include "protocore.h"
#include "server/protocore_internal.h" // status_text, req_is_head, SendCtx s_send
#include "shared_primitives/hex.h"     // pc_hex_u32 (chunk size-line writer)
#include "shared_primitives/mime.h"    // PC_MIME_*, mime tables
#include "shared_primitives/strbuf.h"  // pc_sb frame builder (replaces snprintf)
#include <stdio.h>
#include <string.h>
#if PC_ENABLE_METRICS || PC_ENABLE_STATS
#include "network_drivers/application/web_assets.h" // PC_STATS_JSON / PC_METRICS_PROM (generated)

// Render @p v as decimal into the fixed field @p dst. Both exposition snapshots below fill a
// dozen of these. Unlike snprintf, pc_sb_finish() does NOT terminate when the value would not
// fit - it reports 0 and leaves the buffer untouched - so an over-long value must be turned
// into an empty field explicitly, or the exposition would serve the PREVIOUS snapshot's digits.
static void num_field(char *dst, size_t cap, uint32_t v)
{
    pc_sb b = {dst, cap, 0, true};
    pc_sb_u32(&b, v);
    if (pc_sb_finish(&b) == 0)
    {
        dst[0] = '\0';
    }
}
#endif

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
static void tmpl_take_placeholder(uint8_t slot, const char *&p, TemplateVar resolver, bool emit, size_t &total)
{
    const char *end = strstr(p + 2, "}}");
    size_t nlen = end ? (size_t)(end - (p + 2)) : 0;
    if (!end || nlen > 32)
    {
        // Unterminated or over-long placeholder: emit "{{" literally.
        total += 2;
        if (emit)
        {
            pc_conn_send(slot, "{{", 2);
        }
        p += 2;
        return;
    }
    char name[33];
    memcpy(name, p + 2, nlen);
    name[nlen] = '\0';
    const char *val = resolver ? resolver(name) : nullptr;
    if (!val)
    {
        val = "";
    }
    size_t vlen = strnlen(val, 0xFFFF);
    total += vlen;
    if (emit && vlen)
    {
        pc_conn_send(slot, val, (u16_t)vlen);
    }
    p = end + 2;
}

// Two-pass: pass 1 sizes the body (emit=false), pass 2 streams it (emit=true).
static size_t tmpl_walk(uint8_t slot, const char *tmpl, TemplateVar resolver, bool emit)
{
    size_t total = 0;
    const char *p = tmpl;
    while (*p)
    {
        if (p[0] == '{' && p[1] == '{')
        {
            tmpl_take_placeholder(slot, p, resolver, emit, total);
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
        // GCOVR_EXCL_BR_START  rlen == 0 cannot fire: control only reaches here when p is NOT at a
        // "{{", so the scan loop above always advances p at least one byte and a literal run is
        // always >= 1. (The vlen test in tmpl_take_placeholder, which CAN be 0, is exercised.)
        if (emit && rlen)
        {
            pc_conn_send(slot, run, (u16_t)rlen);
        }
        // GCOVR_EXCL_BR_STOP
    }
    return total;
}

void PC::send_template(uint8_t slot_id, int code, const char *content_type, const char *tmpl, TemplateVar resolver)
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
    size_t body_len = tmpl_walk(slot_id, tmpl, resolver, false);

    bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    char header[RESP_HDR_BUF_SIZE];
    pc_sb hb = {header, RESP_HDR_BUF_SIZE, 0, true};
    pc_sb_lit(&hb, "HTTP/1.1 ");
    pc_sb_u32(&hb, (uint32_t)code);
    pc_sb_lit(&hb, " ");
    pc_sb_put(&hb, status_text(code));
    pc_sb_lit(&hb, "\r\nContent-Type: ");
    pc_sb_put(&hb, content_type);
    pc_sb_lit(&hb, "\r\nContent-Length: ");
    pc_sb_u32(&hb, (uint32_t)body_len);
    pc_sb_lit(&hb, "\r\n");
    int hlen = (int)pc_sb_finish(&hb);
    hlen = append_resp_trailer(header, RESP_HDR_BUF_SIZE, hlen, slot_id, cl);

    bool head = req_is_head(slot_id);

    pc_conn_send(slot_id, header, (u16_t)hlen);
    // Pass 2: stream the rendered body (HEAD carries headers only).
    if (!head && body_len > 0)
    {
        tmpl_walk(slot_id, tmpl, resolver, true);
    }

    pc_resp_end(slot_id, code, (int)body_len, keep);
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

void PC::send_chunked(uint8_t slot_id, int code, const char *content_type, ChunkSource source, void *ctx)
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

    bool keep;
    const char *cl = pc_resp_conn_hdr(slot_id, &keep);

    // RFC 7230 3.3.1: chunked is an HTTP/1.1 transfer-coding - it MUST NOT be sent
    // to an HTTP/1.0 (or unknown-version) client. Fall back to a close-delimited
    // body: omit Transfer-Encoding, force Connection: close, stream the body
    // unframed, and signal its end by closing the connection (RFC 7230 3.3.3).
    bool raw = (http_pool[slot_id].version != HttpVersion::HTTP_11);

    char header[RESP_HDR_BUF_SIZE];
    pc_sb hb2 = {header, RESP_HDR_BUF_SIZE, 0, true};
    if (raw)
    {
        keep = false; // close-delimited: the connection close IS the message boundary
        cl = "Connection: close\r\n";
        pc_sb_put(&hb2, "HTTP/1.0 ");
    }
    else
    {
        pc_sb_put(&hb2, "HTTP/1.1 ");
    }
    pc_sb_u32(&hb2, (uint32_t)code);
    pc_sb_put(&hb2, " ");
    pc_sb_put(&hb2, status_text(code));
    pc_sb_put(&hb2, "\r\nContent-Type: ");
    pc_sb_put(&hb2, content_type);
    pc_sb_put(&hb2, raw ? "\r\n" : "\r\nTransfer-Encoding: chunked\r\n");
    int hlen = (int)pc_sb_finish(&hb2);
    hlen = append_resp_trailer(header, RESP_HDR_BUF_SIZE, hlen, slot_id, cl);

    pc_conn_send(slot_id, header, (u16_t)hlen);

    // HEAD carries the headers but no body or terminator.
    if (req_is_head(slot_id) || !source)
    {
        pc_resp_end(slot_id, code, 0, keep);
        return;
    }

    ChunkSend &s = s_send.chunk[slot_id];
    s.source = source;
    s.ctx = ctx;
    s.status = code;
    s.total = 0;
    s.keep = keep;
    s.active = true;
    s.raw = raw;
    chunk_send_pump(slot_id);
}

// Page a pending chunked response: pull pieces from the source and frame them into
// the send window each worker loop, resuming on later loops as the window drains.
void PC::chunk_send_pump(uint8_t slot_id)
{
    ChunkSend &s = s_send.chunk[slot_id];
    // GCOVR_EXCL_START  unreachable: both callers already established the state - send_chunked() sets
    // s.active immediately before its call, and the poll loop in protocore.cpp only pumps a slot whose
    // s_send.chunk[i].active is set. Kept so the pump is safe to call unconditionally.
    if (!s.active)
    {
        return;
    }
    // GCOVR_EXCL_STOP

    if (!pc_conn_active(slot_id))
    {
        s.active = false; // connection gone mid-stream
        return;
    }

    // A body still being paged out is active, not idle: keep the CONN_TIMEOUT_MS idle sweep off
    // it so a transient send stall on a large stream cannot reap the slot mid-transfer.
    pc_conn_touch_active(slot_id);

    // Frame each chunk in ONE buffer so it goes out in a single tcpip_thread round-trip (was three -
    // size line, body, CRLF - each a ~23 us marshal on-device). Reserve CHUNK_HDR_RESERVE bytes ahead
    // of the body for the "<hex>\r\n" size line and 2 after for the trailing CRLF, so the source writes
    // the body in place and the whole "<hex>\r\n<body>\r\n" is one pc_conn_send with no extra copy.
    // FRAME reserves send-window room for that framing; the raw (HTTP/1.0) path sends the body verbatim.
    static const u16_t CHUNK_HDR_RESERVE = 8; // "<hex>\r\n" is <= 6 bytes for a chunk <= 0xFFFF
    const u16_t FRAME = s.raw ? 0 : 12;
    uint8_t framed[CHUNK_HDR_RESERVE + CHUNK_BUF_SIZE + 2];
    for (;;)
    {
        u16_t avail = pc_conn_sndbuf(slot_id);
        if (avail <= FRAME)
        {
            pc_conn_flush(slot_id); // no room for a useful chunk; resume next loop
            return;
        }
        size_t cap = (size_t)(avail - FRAME);
        if (cap > CHUNK_BUF_SIZE)
        {
            cap = CHUNK_BUF_SIZE;
        }

        uint8_t *body = framed + CHUNK_HDR_RESERVE;
        size_t n = s.source(body, cap, s.ctx);
        if (n == 0)
        {
            if (!s.raw)
            {
                pc_conn_send(slot_id, "0\r\n\r\n", 5); // terminating chunk (1.1 only)
            }
            pc_conn_flush(slot_id);
            s.active = false;
            pc_resp_end(slot_id, s.status, s.total, s.keep); // raw: keep==false -> connection close ends the body
            return;
        }
        if (n > cap)
        {
            n = cap; // defensive: a misbehaving source must not overrun the window
        }

        if (s.raw)
        {
            pc_conn_send(slot_id, body, (u16_t)n); // close-delimited: no chunk framing
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
            pc_conn_send(slot_id, start, (u16_t)(sn + n + 2));
        }
        s.total += (int)n;
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

void PC::add_response_header(uint8_t slot_id, const char *name, const char *value)
{
    if (slot_id >= MAX_CONNS || name == nullptr || value == nullptr)
    {
        return;
    }

    char *buf = _extra_hdr[slot_id];
    size_t used = strnlen(buf, EXTRA_HDR_BUF_SIZE);
    size_t room = EXTRA_HDR_BUF_SIZE - used;
    pc_sb hb3 = {buf + used, room, 0, true};
    pc_sb_put(&hb3, name);
    pc_sb_put(&hb3, ": ");
    pc_sb_put(&hb3, value);
    pc_sb_put(&hb3, "\r\n");
    // A latched builder may have written the pieces that did fit, so rewinding to `used` is what
    // drops the header whole - the same contract snprintf's truncation test enforced.
    if (pc_sb_finish(&hb3) == 0)
    {
        buf[used] = '\0';
    }
}

void PC::set_cookie(uint8_t slot_id, const char *name, const char *value, const char *attrs)
{
    if (slot_id >= MAX_CONNS || name == nullptr || value == nullptr)
    {
        return;
    }

    char *buf = _extra_hdr[slot_id];
    size_t used = strnlen(buf, EXTRA_HDR_BUF_SIZE);
    size_t room = EXTRA_HDR_BUF_SIZE - used;
    pc_sb cb = {buf + used, room, 0, true};
    pc_sb_put(&cb, "Set-Cookie: ");
    pc_sb_put(&cb, name);
    pc_sb_put(&cb, "=");
    pc_sb_put(&cb, value);
    if (attrs != nullptr && attrs[0] != '\0')
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

void PC::clear_response_headers(uint8_t slot_id)
{
    if (slot_id >= MAX_CONNS)
    {
        return;
    }
    _extra_hdr[slot_id][0] = '\0';
}

// ---------------------------------------------------------------------------
// MIME type lookup by extension
// ---------------------------------------------------------------------------

const char *PC::mime_type(const char *path)
{
    if (!path)
    {
        return PC_MIME_OCTET_STREAM;
    }

    // Find the last '.' after the last '/'.
    const char *dot = nullptr;
    for (const char *p = path; *p; p++)
    {
        if (*p == '/')
        {
            dot = nullptr;
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
        bool eq = true;
        while (*a && *b)
        {
            char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
            char cb = *b; // table is already lowercase
            if (ca != cb)
            {
                eq = false;
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
struct StatsCtx
{
    char uptime[12];
    char requests[12];
    char n2xx[12];
    char n4xx[12];
    char n5xx[12];
    char active[8];
    char heap[12];
};
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
    if (!strcmp(name, "free_heap")) // GCOVR_EXCL_BR_LINE  always matches (see above)
    {
        return s_stats.heap;
    }
    return nullptr; // GCOVR_EXCL_LINE  unreachable: every PC_STATS_JSON name resolves above
}

void PC::stats(uint8_t slot_id)
{
    int active = pc_conn_active_count();

    unsigned long up = millis();
#ifdef ARDUINO
    uint32_t heap = ESP.getFreeHeap();
#else
    uint32_t heap = 0;
#endif

    // millis() is a 32-bit tick counter, so the uptime field wraps with it exactly as before.
    num_field(s_stats.uptime, sizeof(s_stats.uptime), (uint32_t)up);
    num_field(s_stats.requests, sizeof(s_stats.requests), (uint32_t)_stat_requests);
    num_field(s_stats.n2xx, sizeof(s_stats.n2xx), (uint32_t)_stat_2xx);
    num_field(s_stats.n4xx, sizeof(s_stats.n4xx), (uint32_t)_stat_4xx);
    num_field(s_stats.n5xx, sizeof(s_stats.n5xx), (uint32_t)_stat_5xx);
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
struct MetricsCtx
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
};
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
    if (!strcmp(name, "max_alloc_heap")) // GCOVR_EXCL_BR_LINE - no placeholder falls past here
    {
        return s_metrics.maxalloc;
    }
    return nullptr; // GCOVR_EXCL_LINE - see above
}

void PC::metrics(uint8_t slot_id)
{
    int active = pc_conn_active_count();

    unsigned long up = millis();
#ifdef ARDUINO
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

    num_field(s_metrics.uptime, sizeof(s_metrics.uptime), (uint32_t)(up / 1000UL));
    num_field(s_metrics.requests, sizeof(s_metrics.requests), (uint32_t)_stat_requests);
    num_field(s_metrics.n2xx, sizeof(s_metrics.n2xx), (uint32_t)_stat_2xx);
    num_field(s_metrics.n4xx, sizeof(s_metrics.n4xx), (uint32_t)_stat_4xx);
    num_field(s_metrics.n5xx, sizeof(s_metrics.n5xx), (uint32_t)_stat_5xx);
    num_field(s_metrics.active, sizeof(s_metrics.active), (uint32_t)(active < 0 ? 0 : active));
    num_field(s_metrics.max, sizeof(s_metrics.max), (uint32_t)MAX_CONNS);
    num_field(s_metrics.heap, sizeof(s_metrics.heap), heap);
    num_field(s_metrics.minheap, sizeof(s_metrics.minheap), min_heap);
    num_field(s_metrics.heapsize, sizeof(s_metrics.heapsize), heap_size);
    num_field(s_metrics.maxalloc, sizeof(s_metrics.maxalloc), max_alloc);

    send_template(slot_id, 200, "text/plain; version=0.0.4; charset=utf-8", PC_METRICS_PROM, metrics_var);
}
#endif // PC_ENABLE_METRICS
