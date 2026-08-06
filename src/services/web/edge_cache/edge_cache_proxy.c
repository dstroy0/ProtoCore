// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_cache_proxy.c
 * @brief CDN edge-cache tier - server glue. See edge_cache_proxy.h.
 */

#include "services/web/edge_cache/edge_cache_proxy.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_EDGE_CACHE

#include "network_drivers/presentation/http/http_parser/http_parser.h" // HttpReq, http_get_header, http_pool
#include "network_drivers/presentation/http/http.h"                    // Http.set_edge_poll
#include "network_drivers/transport/tcp.h"                          // pc_client_*
#include "network_drivers/transport/tcp.h"                             // pc_conn_active
#include "protocore.h"                                                 // PC, Middleware, MwResult, ChunkSource
#include "server/clock/clock.h"                                        // pc_millis
#include "services/web/edge_cache/edge_fetch.h"
#if PC_ENABLE_DBM
#include "services/web/edge_cache/edge_cache_sd.h" // L2 SD tier
#endif
#include "network_drivers/application/http_range.h" // http_parse_byte_range (Range/206 support)
#include "services/net/http_client/http_client.h"   // http_client_parse_url
#include "shared_primitives/mime.h"                 // PC_MIME_TEXT_PLAIN
#if PC_ENABLE_EDGE_ORIGIN_TLS
#include "network_drivers/tls/tls.h" // pc_tls_client_session_* (TLS upstream origin fetch)
#include <mbedtls/ssl.h>             // MBEDTLS_ERR_SSL_WANT_READ / WANT_WRITE
#endif
#if PC_ENABLE_EDGE_MESH
#include "network_drivers/session/proto_handler.h" // ProtoHandler / proto_register (PROTO_MESH serving)
#include "services/web/edge_cache/edge_mesh.h"     // mesh sibling-cache codec + peer-query engine
#endif
#include <stdio.h>

typedef struct
{
    proto_bool used;
    char prefix[MAX_PATH_LEN];
    char origin_host[PC_EDGE_ORIGIN_URL_MAX];
    uint16_t origin_port;
    proto_bool https; ///< fetch this origin over TLS (PC_ENABLE_EDGE_ORIGIN_TLS)
} EdgeRouteMap;

#if PC_ENABLE_EDGE_MESH
// A fetch runs the mesh phase (query siblings) first on a full miss, then falls to the origin.
typedef enum PROTO_ENUM_PACKED
{
    EDGE_FETCH_PHASE_MESH,
    EDGE_FETCH_PHASE_ORIGIN,
} EdgeFetchPhase;
#endif

typedef struct
{
    proto_bool used;
    EdgeFetch f;
    uint8_t client_slot;
    proto_bool revalidate;
    EdgeEntry *reval_entry;              // the stale entry being revalidated (nullptr for a plain miss)
    const EdgeFetchTransport *transport; // plaintext or TLS transport, chosen per route at start_fetch
    char canon[PC_EDGE_KEY_MAX];
    EdgeRouteMap *route;     // the origin route (stable in s_ctx.maps) - lets the origin fetch begin later
    char path[MAX_PATH_LEN]; // request path/query captured at mw time (http_pool[slot] is reused by poll time)
    char query[MAX_QUERY_LEN];
#if PC_ENABLE_EDGE_MESH
    EdgeFetchPhase phase;
    EdgeMeshFetch mf;
    uint8_t peer_idx;                   // sibling currently being queried
    uint8_t mreq[PC_EDGE_MESH_REQ_MAX]; // the mesh request, built once (reused across peers)
    size_t mreq_len;
#endif
} EdgeFetchSlot;

typedef struct
{
    proto_bool active;
    uint8_t fetch_idx;
} EdgePending;

// The ChunkSource ctx for a paged send; must outlive the response, so it lives in the owned Ctx.
typedef struct
{
    proto_bool active;
    EdgeEntry *entry;
    uint32_t off, end;
} EdgeServeCursor;

#if PC_ENABLE_EDGE_MESH
// A configured sibling peer to query on a local miss.
typedef struct
{
    proto_bool used;
    char host[PC_MESH_HOST_MAX];
    uint16_t port;
} MeshPeer;

// One in-flight inbound peer-serve connection: accumulate the request, answer from the local cache, page out.
typedef struct
{
    proto_bool active;
    uint8_t conn_slot;
    uint16_t req_len; // request bytes accumulated
    uint8_t reqbuf[PC_EDGE_MESH_REQ_MAX];
    proto_bool responded; // the whole response is built (out_len) and paging out
    uint16_t out_off, out_len;
    uint8_t outbuf[PC_EDGE_MESH_RESP_MAX];
} MeshConn;
#endif

// The single owned file-static: all of this subsystem's mutable state.
typedef struct
{
    proto_bool registered;
    EdgeCacheStore store;
    EdgeRouteMap maps[PC_EDGE_MAP_MAX];
    EdgeFetchSlot fetches[PC_EDGE_FETCH_SLOTS];
    EdgePending pending[MAX_CONNS];
    EdgeServeCursor serve[MAX_CONNS];
    EdgeFetchTransport transport;
#if PC_ENABLE_EDGE_ORIGIN_TLS
    EdgeFetchTransport transport_tls; // TLS binding over pc_tls_csess, used for https routes
    int tls_cid;                      // underlying pc_client cid of the in-flight TLS fetch (singleton session)
    proto_bool tls_peer_closed;       // latched when the TLS session reports closed / errored
#endif
    char reqbuf[MAX_PATH_LEN + MAX_QUERY_LEN + 256]; // scratch for one origin request line (freed by send)
#if PC_ENABLE_RANGE
    // The client's Range header, captured per slot at middleware time. serve_hit runs for a miss/stale
    // entry from the poll loop *after* the async fetch has reused http_pool[slot], so the original request
    // is no longer readable there - the window must be resolved against this captured copy.
    char range_hdr[MAX_CONNS][48];
#endif
#if PC_ENABLE_DBM
    struct pc_dbm *l2;                    // the persistent L2 tier (nullptr = L1-only)
    uint8_t sd_buf[PC_EDGE_SD_VALUE_MAX]; // serialize/deserialize scratch for one L2 value
#endif
#if PC_ENABLE_EDGE_MESH
    MeshPeer peers[PC_MESH_MAX_PEERS]; // static sibling list queried on a full miss
    MeshConn mesh_conns[PC_MESH_MAX_CONNS];
    char mesh_hdrs[PC_MESH_HDRS_MAX]; // scratch: a served request's header snapshot (serve is single-threaded)
    proto_bool mesh_registered;       // the PROTO_MESH serving handler is installed
#endif
} EdgeCacheProxyCtx;
static EdgeCacheProxyCtx s_ctx;

#if PC_ENABLE_DBM
// L1 write-back hook: spill an evicted victim to L2 (edge_sd_put skips no-validator / oversize entries).
static void edge_on_evict(void *ctx, const EdgeEntry *victim)
{
    (void)ctx;
    if (s_ctx.l2 && edge_sd_put(s_ctx.l2, victim, s_ctx.sd_buf, sizeof(s_ctx.sd_buf)))
    {
        s_ctx.store.stats.l2_spills++;
    }
}
#endif

// --- pc_client transport seam -------------------------------------------------------------------
static int t_open(void *c, const char *host, uint16_t port, uint32_t timeout)
{
    (void)c;
    return Tcp.client->open(host, port, timeout);
}
static proto_bool t_send(void *c, int cid, const void *d, size_t l)
{
    (void)c;
    return Tcp.client->send(cid, d, l);
}
static size_t t_read(void *c, int cid, uint8_t *b, size_t cap)
{
    (void)c;
    return Tcp.client->read(cid, b, cap);
}
static proto_bool t_closed(void *c, int cid)
{
    (void)c;
    return Tcp.client->is_closed(cid);
}
static void t_close(void *c, int cid)
{
    (void)c;
    Tcp.client->close(cid);
}

#if PC_ENABLE_EDGE_ORIGIN_TLS
// --- TLS transport seam (pc_tls_csess layered over pc_client) -----------------------------------
// The client TLS session is a singleton, so the underlying cid + peer-closed latch live in the owned Ctx
// (one TLS fetch at a time, enforced by pc_tls_client_session_active() in start_fetch). The BIO callbacks move
// ciphertext over pc_client's wire ring for that cid - the same bridge the MQTT/WS clients use.
static int edge_tls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx;
    size_t cap = len > 0xFFFF ? 0xFFFF : len;
    return Tcp.client->send(s_ctx.tls_cid, buf, cap) ? (int)cap : MBEDTLS_ERR_SSL_WANT_WRITE;
}
static int edge_tls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    size_t n = Tcp.client->read(s_ctx.tls_cid, buf, len);
    if (n == 0)
    {
        return Tcp.client->is_closed(s_ctx.tls_cid) ? 0 : MBEDTLS_ERR_SSL_WANT_READ;
    }
    return (int)n;
}

static int t_tls_open(void *c, const char *host, uint16_t port, uint32_t timeout)
{
    (void)c;
    s_ctx.tls_cid = Tcp.client->open(host, port, timeout);
    if (s_ctx.tls_cid < 0)
    {
        return -1;
    }
    s_ctx.tls_peer_closed = PROTO_FALSE;
    if (!pc_tls_client_session_begin(host, edge_tls_bio_send, edge_tls_bio_recv))
    {
        Tcp.client->close(s_ctx.tls_cid);
        s_ctx.tls_cid = -1;
        return -1;
    }
    // Block through the handshake at connect - the same brief block the MQTT/WS clients take (and that the
    // plaintext Tcp.client->open already takes on DNS+connect). edge_fetch_begin sends the request
    // synchronously right after open returns, so the session must be established first. pcdelay() yields to
    // the network stack between handshake flights so the peer's records arrive.
    uint32_t deadline = pc_millis() + timeout;
    int h = 0;
    while ((h = pc_tls_client_session_handshake()) == 0 && !Tcp.client->is_closed(s_ctx.tls_cid) &&
           (int32_t)(deadline - pc_millis()) > 0)
    {
        pcdelay(5);
    }
    if (h != 1)
    {
        pc_tls_client_session_end();
        Tcp.client->close(s_ctx.tls_cid);
        s_ctx.tls_cid = -1;
        return -1;
    }
    return s_ctx.tls_cid;
}
static proto_bool t_tls_send(void *c, int cid, const void *d, size_t l)
{
    (void)c;
    (void)cid;
    return pc_tls_client_session_write((const uint8_t *)d, l) == (int)l;
}
static size_t t_tls_read(void *c, int cid, uint8_t *b, size_t cap)
{
    (void)c;
    (void)cid;
    int n = pc_tls_client_session_read(b, cap);
    if (n < 0)
    {
        s_ctx.tls_peer_closed = PROTO_TRUE; // close_notify / decrypt error -> report closed via t_tls_closed
    }
    return n > 0 ? (size_t)n : 0;
}
static proto_bool t_tls_closed(void *c, int cid)
{
    (void)c;
    return s_ctx.tls_peer_closed || Tcp.client->is_closed(cid);
}
static void t_tls_close(void *c, int cid)
{
    (void)c;
    pc_tls_client_session_end();
    Tcp.client->close(cid);
    s_ctx.tls_cid = -1;
}
#endif // PC_ENABLE_EDGE_ORIGIN_TLS

// Request-header lookup used to (re)serialize the Vary secondary key; ctx is the client HttpReq.
static const char *req_lookup(void *ctx, const char *name)
{
    return http_get_header((const HttpReq *)ctx, name);
}

static EdgeRouteMap *map_match(const char *path)
{
    for (int i = 0; i < PC_EDGE_MAP_MAX; i++)
    {
        if (!s_ctx.maps[i].used)
        {
            continue;
        }
        size_t pl = strnlen(s_ctx.maps[i].prefix, sizeof(s_ctx.maps[i].prefix));
        if (strncmp(path, s_ctx.maps[i].prefix, pl) == 0)
        {
            return &s_ctx.maps[i];
        }
    }
    return NULL;
}

static int alloc_fetch()
{
    for (int i = 0; i < PC_EDGE_FETCH_SLOTS; i++)
    {
        if (!s_ctx.fetches[i].used)
        {
            return i;
        }
    }
    return -1;
}

// The ChunkSource: page the cached body to the client. On completion (or exhaustion) release a
// transient passthrough entry (key ""). A dropped connection leaves the transient LRU-reclaimable.
static size_t edge_chunk_source(uint8_t *buf, size_t cap, void *ctx)
{
    EdgeServeCursor *c = (EdgeServeCursor *)ctx;
    if (!c->active || !c->entry)
    {
        return 0;
    }
    size_t remaining = c->end - c->off;
    if (remaining == 0)
    {
        c->active = PROTO_FALSE;
        if (c->entry->key[0] == '\0') // transient passthrough entry -> free its slot
        {
            edge_store_free_entry(&s_ctx.store, c->entry);
        }
        c->entry = NULL;
        return 0;
    }
    size_t n = remaining < cap ? remaining : cap;
    memcpy(buf, c->entry->body + c->off, n);
    c->off += n;
    return n;
}

// Serve a cache entry, replaying its validators + Age, tagged with @p xcache. A client `Range` request
// (PC_ENABLE_RANGE) is answered with a 206 window (or 416 if unsatisfiable); otherwise a full 200.
static void serve_hit(uint8_t slot, EdgeEntry *e, uint32_t now, const char *xcache)
{
    EdgeServeCursor *c = &s_ctx.serve[slot];
    c->active = PROTO_TRUE;
    c->entry = e;
    c->off = 0;
    c->end = e->body_len;
    int status = 200;

#if PC_ENABLE_RANGE
    const char *range = s_ctx.range_hdr[slot]; // captured at mw time (http_pool[slot] is stale post-fetch)
    if (range[0])
    {
        size_t rs = 0;
        size_t re = 0;
        int rr = http_parse_byte_range(range, e->body_len, &rs, &re);
        if (rr < 0) // syntactically valid but unsatisfiable -> 416, no body window served
        {
            char cr[48];
            pc_sb sb_cr = {cr, sizeof(cr), 0, PROTO_TRUE};
            pc_sb_put(&sb_cr, "bytes */");
            pc_sb_u32(&sb_cr, (uint32_t)((unsigned)e->body_len));
            if (pc_sb_finish(&sb_cr) == 0)
            {
                cr[0] = '\0';
            }
            proto_add_response_header(slot, "Content-Range", cr);
            c->active = PROTO_FALSE;
            c->entry = NULL;
            send_text(slot, 416, PC_MIME_TEXT_PLAIN, "Range Not Satisfiable");
            return;
        }
        if (rr > 0) // satisfiable -> 206 with just the requested window [rs, re]
        {
            status = 206;
            c->off = (uint32_t)rs;
            c->end = (uint32_t)re + 1;
            char cr[48];
            pc_sb sb_cr2 = {cr, sizeof(cr), 0, PROTO_TRUE};
            pc_sb_put(&sb_cr2, "bytes ");
            pc_sb_u32(&sb_cr2, (uint32_t)((unsigned)rs));
            pc_sb_put(&sb_cr2, "-");
            pc_sb_u32(&sb_cr2, (uint32_t)((unsigned)re));
            pc_sb_put(&sb_cr2, "/");
            pc_sb_u32(&sb_cr2, (uint32_t)((unsigned)e->body_len));
            if (pc_sb_finish(&sb_cr2) == 0)
            {
                cr[0] = '\0';
            }
            proto_add_response_header(slot, "Content-Range", cr);
        }
    }
    proto_add_response_header(slot, "Accept-Ranges", "bytes"); // advertise range support
#endif

    proto_add_response_header(slot, "X-Cache", xcache);
    if (e->etag[0])
    {
        proto_add_response_header(slot, "ETag", e->etag);
    }
    if (e->last_modified[0])
    {
        proto_add_response_header(slot, "Last-Modified", e->last_modified);
    }
    if (e->content_encoding[0])
    {
        proto_add_response_header(slot, "Content-Encoding", e->content_encoding);
    }
    long age = edge_current_age(e->initial_age, e->insert_ms, now);
    if (age < 0)
    {
        age = 0;
    }
    char agebuf[12];
    pc_sb sb_agebuf = {agebuf, sizeof(agebuf), 0, PROTO_TRUE};
    pc_sb_i64(&sb_agebuf, (int64_t)(age));
    if (pc_sb_finish(&sb_agebuf) == 0)
    {
        agebuf[0] = '\0';
    }
    proto_add_response_header(slot, "Age", agebuf);
    const char *ct = e->content_type[0] ? e->content_type : "application/octet-stream";
    send_chunked(slot, status, ct, edge_chunk_source, c);
}

// Serve a non-cacheable / non-200 origin response through a transient unindexed store slot, so the
// serve source outlives the fetch (which the caller frees) and no-store content is never re-served.
static void serve_passthrough(uint8_t slot, EdgeFetch *f)
{
    EdgeEntry *e = edge_store_alloc(&s_ctx.store, "", ""); // key "" -> never matched by a lookup
    if (!e)
    {
        send_text(slot, 502, PC_MIME_TEXT_PLAIN, "Bad Gateway");
        return;
    }
    s_ctx.store.stats.stores--; // a transient is not a cache store
    e->status = f->status;
    if (!edge_header_value((const char *)f->buf, f->head_len, "Content-Type", e->content_type, sizeof(e->content_type)))
    {
        strncpy(e->content_type, "application/octet-stream", sizeof(e->content_type) - 1);
    }
    edge_header_value((const char *)f->buf, f->head_len, "Content-Encoding", e->content_encoding,
                      sizeof(e->content_encoding));
    size_t bl = f->body_len;
    if (bl > PC_EDGE_BODY_MAX)
    {
        bl = PC_EDGE_BODY_MAX;
    }
    memcpy(e->body, f->buf + f->body_off, bl);
    e->body_len = (uint16_t)bl;

    EdgeServeCursor *c = &s_ctx.serve[slot];
    c->active = PROTO_TRUE;
    c->entry = e;
    c->off = 0;
    c->end = e->body_len;
    proto_add_response_header(slot, "X-Cache", "MISS");
    if (e->content_encoding[0])
    {
        proto_add_response_header(slot, "Content-Encoding", e->content_encoding);
    }
    const char *ct = e->content_type[0] ? e->content_type : "application/octet-stream";
    send_chunked(slot, e->status ? e->status : 200, ct, edge_chunk_source, c);
}

// Store a cacheable 200 response into a fresh entry and serve it.
static void store_response(uint8_t slot, EdgeFetchSlot *fs, HttpReq *req, const pc_cache_control *cc,
                           const char *vary_hdr, uint32_t now)
{
    EdgeFetch *f = &fs->f;
    const char *head = (const char *)f->buf;
    size_t head_len = f->head_len;

    char vary_vals[PC_EDGE_VARY_MAX];
    edge_vary_serialize(vary_hdr[0] ? vary_hdr : NULL, req_lookup, req, vary_vals, sizeof(vary_vals));

    EdgeEntry *e = edge_store_alloc(&s_ctx.store, fs->canon, vary_vals);
    if (!e)
    {
        serve_passthrough(slot, f);
        return;
    }
    e->status = 200;
    edge_header_value(head, head_len, "Content-Type", e->content_type, sizeof(e->content_type));
    edge_header_value(head, head_len, "Content-Encoding", e->content_encoding, sizeof(e->content_encoding));
    edge_header_value(head, head_len, "ETag", e->etag, sizeof(e->etag));
    edge_header_value(head, head_len, "Last-Modified", e->last_modified, sizeof(e->last_modified));
    size_t vhl = strnlen(vary_hdr, sizeof(e->vary_names));
    if (vary_hdr[0] && vhl < sizeof(e->vary_names))
    {
        memcpy(e->vary_names, vary_hdr, vhl + 1);
    }

    size_t bl = f->body_len;
    if (bl > PC_EDGE_BODY_MAX)
    {
        bl = PC_EDGE_BODY_MAX;
    }
    memcpy(e->body, f->buf + f->body_off, bl);
    e->body_len = (uint16_t)bl;
    s_ctx.store.stats.bytes_stored += bl;

    int64_t date = -1;
    int64_t expires = -1;
    int64_t last_mod = -1;
    int32_t age = 0;
    char v[64];
    if (edge_header_value(head, head_len, "Date", v, sizeof(v)))
    {
        date = edge_parse_http_date(v, strnlen(v, sizeof(v)));
    }
    if (edge_header_value(head, head_len, "Expires", v, sizeof(v)))
    {
        expires = edge_parse_http_date(v, strnlen(v, sizeof(v)));
    }
    if (e->last_modified[0])
    {
        last_mod = edge_parse_http_date(e->last_modified, strnlen(e->last_modified, sizeof(e->last_modified)));
    }
    if (edge_header_value(head, head_len, "Age", v, sizeof(v)))
    {
        // Clamp every digit so the accumulator stays at or below INT32_MAX and the next multiply-add
        // stays inside int64_t however many digits the origin sent.
        int64_t a = 0;
        for (const char *p = v; *p >= '0' && *p <= '9'; p++)
        {
            a = a * 10 + (*p - '0');
            if (a > INT32_MAX)
            {
                a = INT32_MAX;
            }
        }
        age = (int32_t)a;
    }
    edge_entry_set_freshness(e, cc, /*shared=*/PROTO_TRUE, date, expires, last_mod, age, /*response_time=*/-1, now);
    serve_hit(slot, e, now, "MISS");
}

// A completed origin fetch: revalidation 304 / store 200 / pass through anything else.
static void on_fetch_done(uint8_t slot, EdgeFetchSlot *fs, uint32_t now)
{
    EdgeFetch *f = &fs->f;
    const char *head = (const char *)f->buf;
    size_t head_len = f->head_len;
    HttpReq *req = &http_pool[slot];

    if (fs->revalidate && f->status == 304 && fs->reval_entry)
    {
        edge_apply_304(fs->reval_entry, head, head_len, -1, now);
        s_ctx.store.stats.revalidations_304++;
        serve_hit(slot, fs->reval_entry, now, "REVALIDATED");
        return;
    }
    if (f->status == 200)
    {
        pc_cache_control cc;
        cache_control_init(&cc);
        char v[128];
        if (edge_header_value(head, head_len, "Cache-Control", v, sizeof(v)))
        {
            cache_control_parse(v, strnlen(v, sizeof(v)), &cc);
        }
        char vary_hdr[PC_EDGE_VARY_MAX];
        vary_hdr[0] = '\0';
        edge_header_value(head, head_len, "Vary", vary_hdr, sizeof(vary_hdr));
        if (edge_is_storeable(200, "GET", &cc, vary_hdr[0] ? vary_hdr : NULL, f->body_len))
        {
            if (fs->revalidate && fs->reval_entry) // 200 on a revalidation replaces the stale entry
            {
                edge_store_free_entry(&s_ctx.store, fs->reval_entry);
                s_ctx.store.stats.replaces_200++;
            }
            store_response(slot, fs, req, &cc, vary_hdr, now);
            return;
        }
        serve_passthrough(slot, f); // 200 but not storeable
        return;
    }
    serve_passthrough(slot, f); // non-200 status
}

// Forward decls for the seam functions installed by pc_edge_cache_enable().
static MwResult edge_cache_mw(uint8_t slot, HttpReq *req);
static proto_bool edge_cache_poll(uint8_t slot);

// Build + begin the origin fetch for @p fs from its captured route/path/query (so it can begin either
// immediately at mw time or later, after the mesh phase exhausts its peers). Picks the plaintext or TLS
// transport; a revalidation adds the conditional headers. @return false if no fetch could start (fail open).
static proto_bool begin_origin_fetch(EdgeFetchSlot *fs, uint32_t now)
{
    EdgeRouteMap *m = fs->route;
    const EdgeFetchTransport *tport = &s_ctx.transport;
#if PC_ENABLE_EDGE_ORIGIN_TLS
    if (m->https)
    {
        if (pc_tls_client_session_active())
        {
            return PROTO_FALSE; // the shared client-TLS session is busy -> fail open (never tear down a live one)
        }
        tport = &s_ctx.transport_tls;
    }
#endif
    char cond[192];
    cond[0] = '\0';
    if (fs->reval_entry)
    {
        edge_build_conditional(fs->reval_entry, cond, sizeof(cond));
    }
    pc_sb sb_reqbuf = {s_ctx.reqbuf, sizeof(s_ctx.reqbuf), 0, PROTO_TRUE};
    pc_sb_put(&sb_reqbuf, "GET ");
    pc_sb_put(&sb_reqbuf, fs->path);
    pc_sb_put(&sb_reqbuf, fs->query[0] ? "?" : "");
    pc_sb_put(&sb_reqbuf, fs->query);
    pc_sb_put(&sb_reqbuf, " HTTP/1.1\r\nHost: ");
    pc_sb_put(&sb_reqbuf, m->origin_host);
    pc_sb_put(&sb_reqbuf, "\r\nUser-Agent: PC-EdgeCache\r\nConnection: close\r\n");
    pc_sb_put(&sb_reqbuf, cond);
    pc_sb_put(&sb_reqbuf, "\r\n");
    int rl = (int)pc_sb_finish(&sb_reqbuf);
    if (rl <= 0 || (size_t)rl >= sizeof(s_ctx.reqbuf))
    {
        return PROTO_FALSE;
    }
    edge_fetch_begin(&fs->f, tport, m->origin_host, m->origin_port, s_ctx.reqbuf, (size_t)rl, now);
    if (fs->f.st == EDGE_FETCH_STATUS_FAILED)
    {
        edge_fetch_end(&fs->f, tport);
        return PROTO_FALSE;
    }
    fs->transport = tport;
    return PROTO_TRUE;
}

#if PC_ENABLE_EDGE_MESH
static int mesh_peer_count()
{
    int n = 0;
    for (int i = 0; i < PC_MESH_MAX_PEERS; i++)
    {
        if (s_ctx.peers[i].used)
        {
            n++;
        }
    }
    return n;
}

// The @p n-th used peer in slot order, or nullptr.
static MeshPeer *mesh_peer_nth(int n)
{
    for (int i = 0; i < PC_MESH_MAX_PEERS; i++)
    {
        if (s_ctx.peers[i].used && n-- == 0)
        {
            return &s_ctx.peers[i];
        }
    }
    return NULL;
}

// Snapshot the request headers as `name RS value US ...` so a peer can re-run the Vary matcher. Headers past
// the cap are dropped (at worst a safe mesh miss, never wrong content).
static void mesh_snapshot_headers(const HttpReq *req, char *out, size_t cap)
{
    size_t pos = 0;
    out[0] = '\0';
    for (uint8_t i = 0; i < req->header_count; i++)
    {
        const char *k = req->headers[i].key;
        const char *v = req->headers[i].val;
        size_t kl = strnlen(k, MAX_KEY_LEN);
        size_t vl = strnlen(v, MAX_VAL_LEN);
        if (pos + kl + 1 + vl + 1 >= cap)
        {
            break;
        }
        memcpy(out + pos, k, kl);
        pos += kl;
        out[pos++] = '\x1e';
        memcpy(out + pos, v, vl);
        pos += vl;
        out[pos++] = '\x1f';
    }
    out[pos] = '\0';
}

// The peer query reuses the slot's origin response buffer (the mesh and origin phases never run together).
#if PC_EDGE_FETCH_BUF < PC_EDGE_MESH_RESP_MAX
#error "PC_EDGE_FETCH_BUF must hold a mesh response (>= PC_EDGE_MESH_RESP_MAX). It defaults to that "\
       "floor, so this only fires if you pinned it lower; raise PC_EDGE_FETCH_BUF or lower PC_EDGE_BODY_MAX."
#endif

// Begin the mesh query against the peer at fs->peer_idx. @return false if there is no such peer.
static proto_bool mesh_begin_peer(EdgeFetchSlot *fs, uint32_t now)
{
    MeshPeer *p = mesh_peer_nth(fs->peer_idx);
    if (!p)
    {
        return PROTO_FALSE;
    }
    edge_mesh_fetch_begin(&fs->mf, &s_ctx.transport, p->host, p->port, fs->mreq, fs->mreq_len, fs->f.buf,
                          sizeof(fs->f.buf), now);
    return PROTO_TRUE;
}

// A peer HIT: rehydrate the entry into a fresh L1 slot, verify it matches the request, and serve it as fresh
// (age propagated). @return true if it was served; false (freeing the slot) if corrupt / wrong / already stale.
static proto_bool mesh_store_and_serve(uint8_t slot, EdgeFetchSlot *fs, uint32_t now)
{
    EdgeEntry *e = edge_store_alloc(&s_ctx.store, fs->canon, "");
    if (!e)
    {
        return PROTO_FALSE;
    }
    if (!edge_mesh_deserialize_entry(fs->mf.buf + fs->mf.entry_off, fs->mf.entry_len, e, now) ||
        strcmp(e->key, fs->canon) != 0 || !edge_entry_fresh(e, now))
    {
        edge_store_free_entry(&s_ctx.store, e);
        return PROTO_FALSE;
    }
    s_ctx.store.stats.bytes_stored += e->body_len;
    serve_hit(slot, e, now, "MESH");
    return PROTO_TRUE;
}

// The current peer query ended without a served hit: try the next sibling, else begin the origin fetch.
// @return true if the slot still owns work (mesh continues or origin began); false = give up.
static proto_bool mesh_advance_or_origin(EdgeFetchSlot *fs, uint32_t now)
{
    fs->peer_idx++;
    if (mesh_begin_peer(fs, now))
    {
        return PROTO_TRUE; // querying the next sibling (still MESH phase)
    }
    s_ctx.store.stats.mesh_misses++;
    if (begin_origin_fetch(fs, now))
    {
        fs->phase = EDGE_FETCH_PHASE_ORIGIN;
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}
#endif // PC_ENABLE_EDGE_MESH

static proto_bool start_fetch(uint8_t slot, HttpReq *req, EdgeRouteMap *m, const char *canon, EdgeEntry *reval,
                              uint32_t now)
{
    int fi = alloc_fetch();
    if (fi < 0)
    {
        return PROTO_FALSE;
    }
    EdgeFetchSlot *fs = &s_ctx.fetches[fi];
    fs->client_slot = slot;
    fs->revalidate = (reval != NULL);
    fs->reval_entry = reval;
    fs->route = m;
    memcpy(fs->canon, canon, strnlen(canon, sizeof(fs->canon) - 1) + 1);
    strncpy(fs->path, req->path, sizeof(fs->path) - 1);
    fs->path[sizeof(fs->path) - 1] = '\0';
    strncpy(fs->query, req->query, sizeof(fs->query) - 1);
    fs->query[sizeof(fs->query) - 1] = '\0';

#if PC_ENABLE_EDGE_MESH
    // On a full miss (not a revalidation) with >= 1 sibling, query the mesh before the origin.
    if (!reval && mesh_peer_count() > 0)
    {
        uint8_t digest[32];
        edge_key_digest(canon, strnlen(canon, PC_EDGE_KEY_MAX), digest);
        mesh_snapshot_headers(req, s_ctx.mesh_hdrs, sizeof(s_ctx.mesh_hdrs));
        fs->mreq_len = edge_mesh_build_request(digest, canon, s_ctx.mesh_hdrs, fs->mreq, sizeof(fs->mreq));
        fs->peer_idx = 0;
        if (fs->mreq_len > 0 && mesh_begin_peer(fs, now))
        {
            fs->phase = EDGE_FETCH_PHASE_MESH;
            fs->used = PROTO_TRUE;
            s_ctx.pending[slot].active = PROTO_TRUE;
            s_ctx.pending[slot].fetch_idx = (uint8_t)fi;
            return PROTO_TRUE;
        }
    }
    fs->phase = EDGE_FETCH_PHASE_ORIGIN;
#endif
    if (!begin_origin_fetch(fs, now))
    {
        return PROTO_FALSE; // fs->used stays false -> the slot is reclaimed
    }
    fs->used = PROTO_TRUE;
    s_ctx.pending[slot].active = PROTO_TRUE;
    s_ctx.pending[slot].fetch_idx = (uint8_t)fi;
    return PROTO_TRUE;
}

#if PC_ENABLE_DBM
// Promote a reboot-surviving entry from L2 into a fresh L1 slot, forced stale so the caller revalidates it
// (the monotonic insert time is meaningless across a reboot). @return the promoted entry, or nullptr.
static EdgeEntry *try_promote_l2(const char *canon, uint32_t now)
{
    uint8_t digest[32];
    edge_key_digest(canon, strnlen(canon, PC_EDGE_KEY_MAX), digest);
    EdgeEntry *e = edge_store_alloc(&s_ctx.store, canon, ""); // may evict + write-back an L1 victim first
    if (!e)
    {
        return NULL;
    }
    if (!edge_sd_get(s_ctx.l2, digest, e, s_ctx.sd_buf, sizeof(s_ctx.sd_buf)) || strcmp(e->key, canon) != 0)
    {
        edge_store_free_entry(&s_ctx.store, e); // L2 miss or digest collision -> not promoted
        return NULL;
    }
    e->lifetime_s = 0; // force stale: freshness is untrustworthy across a reboot -> caller revalidates
    e->initial_age = 0;
    e->date_epoch = e->expires_epoch = -1;
    e->age_hdr = 0;
    e->insert_ms = now;
    e->last_used_ms = now;
    s_ctx.store.stats.l2_promotes++;
    return e;
}
#endif

// The cache middleware: fresh hit -> serve; stale/miss -> start an async origin fetch (suspend); else
// fall through (fail open).
static MwResult edge_cache_mw(uint8_t slot, HttpReq *req)
{
    // `registered` is the whole test now: it was always the real question, and the stored server
    // pointer it was AND-ed with was set by the same call that set it.
    if (!s_ctx.registered || slot >= MAX_CONNS)
    {
        return MW_NEXT;
    }
    proto_bool is_get = strcmp(req->method, "GET") == 0;
    proto_bool is_head = strcmp(req->method, "HEAD") == 0;
    if (!is_get && !is_head)
    {
        return MW_NEXT; // only cache safe methods
    }
    if (http_get_header(req, "Authorization"))
    {
        return MW_NEXT; // never cache authorized/private requests
    }
    EdgeRouteMap *m = map_match(req->path);
    if (!m)
    {
        return MW_NEXT; // not a mapped origin
    }

    const char *host = http_get_header(req, "Host");
    if (!host)
    {
        host = "";
    }
    char canon[PC_EDGE_KEY_MAX];
    if (edge_key_canon("GET", host, req->path, req->query, /*include_query=*/PROTO_TRUE, canon, sizeof(canon)) == 0)
    {
        return MW_NEXT; // key too long -> uncacheable, fail open
    }

#if PC_ENABLE_RANGE
    // Capture the Range header now, while http_pool[slot] is the client request: a miss serves from the
    // poll after the async fetch has reused that buffer, so serve_hit resolves the window against this copy.
    const char *rh = http_get_header(req, "Range");
    strncpy(s_ctx.range_hdr[slot], rh ? rh : "", sizeof(s_ctx.range_hdr[slot]) - 1);
    s_ctx.range_hdr[slot][sizeof(s_ctx.range_hdr[slot]) - 1] = '\0';
#endif

    uint32_t now = pc_millis();
    EdgeEntry *e = edge_store_find(&s_ctx.store, canon, req_lookup, req, now);
    if (e && edge_entry_fresh(e, now))
    {
        s_ctx.store.stats.hits++;
        serve_hit(slot, e, now, "HIT");
        return MW_HALT;
    }
#if PC_ENABLE_DBM
    if (!e && s_ctx.l2) // L1 miss: try promoting a reboot-surviving entry from L2 (force-stale -> revalidate)
    {
        e = try_promote_l2(canon, now);
    }
#endif
    s_ctx.store.stats.misses++;
    EdgeEntry *reval = (e && edge_entry_has_validator(e)) ? e : NULL;
    if (!start_fetch(slot, req, m, canon, reval, now))
    {
        return MW_NEXT; // no fetch slot / origin open failed -> fail open to normal dispatch
    }
    return MW_HALT; // client request suspended until the fetch completes
}

// Per-slot poll hook: drive an in-flight sibling query then origin fetch, then serve. Returns true while it
// owns the slot.
static proto_bool edge_cache_poll(uint8_t slot)
{
    if (slot >= MAX_CONNS || !s_ctx.pending[slot].active)
    {
        return PROTO_FALSE;
    }
    uint8_t fi = s_ctx.pending[slot].fetch_idx;
    EdgeFetchSlot *fs = &s_ctx.fetches[fi];
    uint32_t now = pc_millis();

#if PC_ENABLE_EDGE_MESH
    if (fs->phase == EDGE_FETCH_PHASE_MESH)
    {
        if (!pc_conn_active(slot)) // client vanished mid-query: abort
        {
            edge_mesh_fetch_end(&fs->mf, &s_ctx.transport);
            fs->used = PROTO_FALSE;
            s_ctx.pending[slot].active = PROTO_FALSE;
            return PROTO_TRUE;
        }
        EdgeMeshStatus ms = edge_mesh_fetch_pump(&fs->mf, &s_ctx.transport, now);
        if (ms == EDGE_MESH_STATUS_PENDING)
        {
            return PROTO_TRUE; // still querying this sibling
        }
        proto_bool served = (ms == EDGE_MESH_STATUS_HIT) && mesh_store_and_serve(slot, fs, now);
        edge_mesh_fetch_end(&fs->mf, &s_ctx.transport);
        if (served)
        {
            s_ctx.store.stats.mesh_hits++;
            fs->used = PROTO_FALSE;
            s_ctx.pending[slot].active = PROTO_FALSE;
            return PROTO_TRUE;
        }
        if (mesh_advance_or_origin(fs, now)) // try the next sibling, else begin the origin fetch
        {
            return PROTO_TRUE;
        }
        send_text(slot, 502, PC_MIME_TEXT_PLAIN, "Bad Gateway"); // no sibling + origin start failed
        fs->used = PROTO_FALSE;
        s_ctx.pending[slot].active = PROTO_FALSE;
        return PROTO_TRUE;
    }
#endif

    const EdgeFetchTransport *tport = fs->transport; // the transport chosen for this fetch (plaintext or TLS)
    if (!pc_conn_active(slot))                       // client vanished mid-fetch: abort
    {
        edge_fetch_end(&fs->f, tport);
        fs->used = PROTO_FALSE;
        s_ctx.pending[slot].active = PROTO_FALSE;
        return PROTO_TRUE;
    }

    EdgeFetchStatus st = edge_fetch_pump(&fs->f, tport, now);
    if (st == EDGE_FETCH_STATUS_PENDING)
    {
        return PROTO_TRUE; // still receiving; owns the slot
    }

    if (st == EDGE_FETCH_STATUS_DONE)
    {
        on_fetch_done(slot, fs, now);
    }
    else if (st == EDGE_FETCH_STATUS_FAILED && fs->revalidate && fs->reval_entry)
    {
        serve_hit(slot, fs->reval_entry, now, "STALE"); // stale-if-error: serve the last good copy
    }
    else // FAILED miss / OVERSIZE
    {
        send_text(slot, 502, PC_MIME_TEXT_PLAIN, "Bad Gateway");
    }
    edge_fetch_end(&fs->f, tport);
    fs->used = PROTO_FALSE;
    s_ctx.pending[slot].active = PROTO_FALSE;
    return PROTO_TRUE;
}

#if PC_ENABLE_EDGE_MESH
// --- PROTO_MESH serving side: answer a sibling's query from the LOCAL cache only (one hop, never recurses to
//     this node's own origin or peers, so the fleet cannot loop) -------------------------------------------

// Case-insensitive compare of the first @p n bytes (header names).
static proto_bool mesh_name_eq(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
        {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z')
        {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb)
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

// EdgeHdrLookup over a request-header snapshot blob (`name RS value US ...`); ctx is a MeshLookupCtx. The
// returned pointer is valid until the next call (edge_vary_serialize copies each value before re-looking up).
typedef struct
{
    const char *blob;
    char valbuf[MAX_VAL_LEN];
} MeshLookupCtx;
static const char *mesh_hdr_lookup(void *ctx, const char *name)
{
    MeshLookupCtx *lc = (MeshLookupCtx *)ctx;
    size_t nl = strnlen(name, MAX_KEY_LEN);
    const char *p = lc->blob;
    while (*p)
    {
        const char *rs = strchr(p, '\x1e');
        if (!rs)
        {
            break;
        }
        const char *us = strchr(rs + 1, '\x1f');
        if (!us)
        {
            break;
        }
        if ((size_t)(rs - p) == nl && mesh_name_eq(p, name, nl))
        {
            size_t vl = (size_t)(us - (rs + 1));
            if (vl >= sizeof(lc->valbuf))
            {
                vl = sizeof(lc->valbuf) - 1;
            }
            memcpy(lc->valbuf, rs + 1, vl);
            lc->valbuf[vl] = '\0';
            return lc->valbuf;
        }
        p = us + 1;
    }
    return NULL;
}

static MeshConn *mesh_conn_by_slot(uint8_t slot)
{
    for (int i = 0; i < PC_MESH_MAX_CONNS; i++)
    {
        if (s_ctx.mesh_conns[i].active && s_ctx.mesh_conns[i].conn_slot == slot)
        {
            return &s_ctx.mesh_conns[i];
        }
    }
    return NULL;
}

// Build the response for a parsed request into mc->outbuf: a HIT carrying a fresh local variant, else a MISS.
static void mesh_answer(MeshConn *mc, const uint8_t digest[32], const char *canon, uint32_t now)
{
    proto_bool hit = PROTO_FALSE;
    uint8_t verify[32];
    edge_key_digest(canon, strnlen(canon, PC_EDGE_KEY_MAX), verify);
    if (memcmp(verify, digest, 32) == 0) // integrity: the canonical key must hash to the advertised digest
    {
        MeshLookupCtx lc;
        lc.blob = s_ctx.mesh_hdrs;
        EdgeEntry *e = edge_store_find(&s_ctx.store, canon, mesh_hdr_lookup, &lc, now);
        if (e && edge_entry_fresh(e, now))
        {
            long age = edge_current_age(e->initial_age, e->insert_ms, now);
            if (age < 0)
            {
                age = 0;
            }
            // Serialize the entry directly after the 6-byte response header to avoid a large stack temp.
            size_t fn = edge_mesh_serialize_entry(e, age, mc->outbuf + 6, sizeof(mc->outbuf) - 6);
            if (fn > 0 && fn <= 0xFFFFu)
            {
                mc->outbuf[0] = PC_EDGE_MESH_MAGIC0;
                mc->outbuf[1] = PC_EDGE_MESH_MAGIC1;
                mc->outbuf[2] = PC_EDGE_MESH_VERSION;
                mc->outbuf[3] = 1; // HIT
                mc->outbuf[4] = (uint8_t)(fn & 0xFF);
                mc->outbuf[5] = (uint8_t)(fn >> 8);
                mc->out_len = (uint16_t)(6 + fn);
                hit = PROTO_TRUE;
            }
        }
    }
    if (!hit)
    {
        mc->out_len = (uint16_t)edge_mesh_build_response(PROTO_FALSE, NULL, 0, mc->outbuf, sizeof(mc->outbuf));
    }
    mc->out_off = 0;
    mc->responded = PROTO_TRUE;
}

static void mesh_serve_end(MeshConn *mc)
{
    mc->active = PROTO_FALSE;
    Tcp.conn->close(mc->conn_slot);
}

// Drive one serve connection: accumulate the request, answer it, then page the response out with backpressure.
static void mesh_serve_pump(MeshConn *mc)
{
    uint8_t slot = mc->conn_slot;
    if (!mc->responded)
    {
        if (pc_conn_available(slot) && mc->req_len < sizeof(mc->reqbuf))
        {
            mc->req_len += (uint16_t)pc_conn_read(slot, mc->reqbuf + mc->req_len, sizeof(mc->reqbuf) - mc->req_len);
        }
        uint8_t digest[32];
        char canon[PC_EDGE_KEY_MAX];
        EdgeMeshParse p = edge_mesh_parse_request(mc->reqbuf, mc->req_len, digest, canon, sizeof(canon),
                                                  s_ctx.mesh_hdrs, sizeof(s_ctx.mesh_hdrs));
        if (p == EDGE_MESH_PARSE_INCOMPLETE)
        {
            if (mc->req_len >= sizeof(mc->reqbuf))
            {
                mesh_serve_end(mc); // full buffer, still short -> junk, drop
            }
            return; // otherwise wait for more
        }
        if (p != EDGE_MESH_PARSE_HIT)
        {
            mesh_serve_end(mc); // malformed
            return;
        }
        mesh_answer(mc, digest, canon, pc_millis());
    }
    while (mc->out_off < mc->out_len)
    {
        proto_u16 room = Tcp.conn->sndbuf(slot);
        if (room == 0)
        {
            return; // backpressure; retry next poll
        }
        uint16_t remaining = (uint16_t)(mc->out_len - mc->out_off);
        proto_u16 n = remaining < room ? remaining : room;
        if (!Tcp.conn->send(slot, mc->outbuf + mc->out_off, n))
        {
            return; // retry next poll
        }
        mc->out_off = (uint16_t)(mc->out_off + n);
    }
    // Whole response queued: flush it out, then dwell in CONN_CLOSING until the peer ACKs (a plain
    // Tcp.conn->close would RST and discard the response the peer has not read yet). Tcp.conn->send already
    // COPY'd the bytes into the TCP buffer and the graceful finalize does not call on_close, so free the
    // MeshConn now - the transport owns the drain from here.
    Tcp.conn->flush(slot);
    Tcp.conn->begin_close(slot);
    mc->active = PROTO_FALSE;
}

static void mesh_on_accept(uint8_t slot)
{
    for (int i = 0; i < PC_MESH_MAX_CONNS; i++)
    {
        if (!s_ctx.mesh_conns[i].active)
        {
            MeshConn *mc = &s_ctx.mesh_conns[i];
            mc->active = PROTO_TRUE;
            mc->conn_slot = slot;
            mc->req_len = 0;
            mc->responded = PROTO_FALSE;
            mc->out_off = 0;
            mc->out_len = 0;
            return;
        }
    }
    Tcp.conn->close(slot); // no free serve slot
}

static void mesh_on_data(uint8_t slot)
{
    MeshConn *mc = mesh_conn_by_slot(slot);
    if (mc)
    {
        mesh_serve_pump(mc);
    }
}

static void mesh_on_poll(uint8_t slot)
{
    if (!pc_conn_active(slot))
    {
        return;
    }
    MeshConn *mc = mesh_conn_by_slot(slot);
    if (mc)
    {
        mesh_serve_pump(mc);
    }
}

static void mesh_on_close(uint8_t slot)
{
    MeshConn *mc = mesh_conn_by_slot(slot);
    if (mc)
    {
        mc->active = PROTO_FALSE; // the transport owns the closing slot
    }
}

static const ProtoHandler s_mesh_handler = {mesh_on_accept, mesh_on_data, mesh_on_close, mesh_on_poll};
#endif // PC_ENABLE_EDGE_MESH

// --- public API ----------------------------------------------------------------------------------

void pc_edge_cache_enable(void)
{
    edge_store_init(&s_ctx.store);
    for (int i = 0; i < PC_EDGE_FETCH_SLOTS; i++)
    {
        s_ctx.fetches[i].used = PROTO_FALSE;
        s_ctx.fetches[i].f.cid = -1;
    }
    for (int i = 0; i < MAX_CONNS; i++)
    {
        s_ctx.pending[i].active = PROTO_FALSE;
        s_ctx.serve[i].active = PROTO_FALSE;
        s_ctx.serve[i].entry = NULL;
#if PC_ENABLE_RANGE
        s_ctx.range_hdr[i][0] = '\0';
#endif
    }
    s_ctx.transport.open = t_open;
    s_ctx.transport.send = t_send;
    s_ctx.transport.read = t_read;
    s_ctx.transport.closed = t_closed;
    s_ctx.transport.close = t_close;
    s_ctx.transport.ctx = NULL;
#if PC_ENABLE_EDGE_ORIGIN_TLS
    s_ctx.transport_tls.open = t_tls_open;
    s_ctx.transport_tls.send = t_tls_send;
    s_ctx.transport_tls.read = t_tls_read;
    s_ctx.transport_tls.closed = t_tls_closed;
    s_ctx.transport_tls.close = t_tls_close;
    s_ctx.transport_tls.ctx = NULL;
    s_ctx.tls_cid = -1;
    s_ctx.tls_peer_closed = PROTO_FALSE;
#endif
#if PC_ENABLE_DBM
    s_ctx.store.on_evict = s_ctx.l2 ? edge_on_evict : NULL; // re-arm write-back after edge_store_init
    s_ctx.store.evict_ctx = NULL;
#endif
#if PC_ENABLE_EDGE_MESH
    for (int i = 0; i < PC_MESH_MAX_CONNS; i++)
    {
        s_ctx.mesh_conns[i].active = PROTO_FALSE;
    }
#endif
    if (!s_ctx.registered)
    {
        use(edge_cache_mw);
        Http.set_edge_poll(edge_cache_poll);
        s_ctx.registered = PROTO_TRUE;
    }
}

#if PC_ENABLE_DBM
void pc_edge_cache_bind_sd(struct pc_dbm *dbm)
{
    s_ctx.l2 = dbm;
    s_ctx.store.on_evict = dbm ? edge_on_evict : NULL;
    s_ctx.store.evict_ctx = NULL;
}
#endif

proto_bool pc_edge_cache_map(const char *path_prefix, const char *origin_base_url)
{
    if (!path_prefix || !origin_base_url)
    {
        return PROTO_FALSE;
    }
    if (strnlen(path_prefix, sizeof(s_ctx.maps[0].prefix)) >= sizeof(s_ctx.maps[0].prefix))
    {
        return PROTO_FALSE;
    }
    proto_bool https = PROTO_FALSE;
    char host[PC_EDGE_ORIGIN_URL_MAX];
    uint16_t port = 80;
    char ignore_path[256];
    if (!http_client_parse_url(origin_base_url, &https, host, sizeof(host), &port, ignore_path, sizeof(ignore_path)))
    {
        return PROTO_FALSE;
    }
#if !PC_ENABLE_EDGE_ORIGIN_TLS
    if (https)
    {
        return PROTO_FALSE; // plaintext origins only unless PC_ENABLE_EDGE_ORIGIN_TLS is set
    }
#endif
    for (int i = 0; i < PC_EDGE_MAP_MAX; i++)
    {
        if (s_ctx.maps[i].used)
        {
            continue;
        }
        strncpy(s_ctx.maps[i].prefix, path_prefix, sizeof(s_ctx.maps[i].prefix) - 1);
        s_ctx.maps[i].prefix[sizeof(s_ctx.maps[i].prefix) - 1] = '\0';
        strncpy(s_ctx.maps[i].origin_host, host, sizeof(s_ctx.maps[i].origin_host) - 1);
        s_ctx.maps[i].origin_host[sizeof(s_ctx.maps[i].origin_host) - 1] = '\0';
        s_ctx.maps[i].origin_port = port;
        s_ctx.maps[i].https = https;
        s_ctx.maps[i].used = PROTO_TRUE;
        return PROTO_TRUE;
    }
    return PROTO_FALSE; // map table full
}

#if PC_ENABLE_EDGE_ORIGIN_TLS
void pc_edge_cache_set_origin_ca(const uint8_t *ca_pem, size_t len)
{
    pc_tls_client_set_ca(ca_pem, len); // shared client-TLS trust store (also used by MQTTS/wss/HTTP client)
}
void pc_edge_cache_set_origin_pin(const uint8_t sha256[32])
{
    pc_tls_client_set_pin(sha256);
}
#endif

#if PC_ENABLE_EDGE_MESH
proto_bool pc_edge_cache_add_peer(const char *host, uint16_t port)
{
    if (!host)
    {
        return PROTO_FALSE;
    }
    size_t hl = strnlen(host, PC_MESH_HOST_MAX + 1);
    if (hl == 0 || hl >= PC_MESH_HOST_MAX)
    {
        return PROTO_FALSE;
    }
    for (int i = 0; i < PC_MESH_MAX_PEERS; i++)
    {
        if (!s_ctx.peers[i].used)
        {
            memcpy(s_ctx.peers[i].host, host, hl + 1);
            s_ctx.peers[i].port = port;
            s_ctx.peers[i].used = PROTO_TRUE;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE; // peer table full
}

void pc_edge_cache_mesh_serve(void)
{
    if (!s_ctx.mesh_registered)
    {
        proto_register(PROTO_MESH, &s_mesh_handler);
        s_ctx.mesh_registered = PROTO_TRUE;
    }
}
#endif // PC_ENABLE_EDGE_MESH

void pc_edge_cache_reset(void)
{
    edge_store_init(&s_ctx.store);
#if PC_ENABLE_DBM
    if (s_ctx.l2)
    {
        edge_sd_purge_all(s_ctx.l2);
        s_ctx.store.on_evict = edge_on_evict; // edge_store_init cleared it - re-arm the write-back hook
    }
#endif
    for (int i = 0; i < PC_EDGE_MAP_MAX; i++)
    {
        s_ctx.maps[i].used = PROTO_FALSE;
    }
#if PC_ENABLE_EDGE_MESH
    for (int i = 0; i < PC_MESH_MAX_PEERS; i++)
    {
        s_ctx.peers[i].used = PROTO_FALSE;
    }
#endif
}

proto_bool pc_edge_cache_purge(const char *canonical_key)
{
    if (!canonical_key)
    {
        return PROTO_FALSE;
    }
    proto_bool purged = edge_store_purge(&s_ctx.store, canonical_key) > 0;
#if PC_ENABLE_DBM
    if (s_ctx.l2)
    {
        uint8_t digest[32];
        edge_key_digest(canonical_key, strnlen(canonical_key, PC_EDGE_KEY_MAX), digest);
        if (edge_sd_del(s_ctx.l2, digest))
        {
            purged = PROTO_TRUE;
        }
    }
#endif
    return purged;
}

uint32_t pc_edge_cache_purge_prefix(const char *path_prefix)
{
    if (!path_prefix)
    {
        return 0;
    }
    uint32_t n = edge_store_purge_prefix(&s_ctx.store, path_prefix);
#if PC_ENABLE_DBM
    if (s_ctx.l2)
    {
        n += edge_sd_purge_prefix(s_ctx.l2, path_prefix, s_ctx.sd_buf, sizeof(s_ctx.sd_buf));
    }
#endif
    return n;
}

void pc_edge_cache_stats(EdgeCacheStats *out)
{
    if (out)
    {
        *out = s_ctx.store.stats;
    }
}

#endif // PC_ENABLE_EDGE_CACHE
