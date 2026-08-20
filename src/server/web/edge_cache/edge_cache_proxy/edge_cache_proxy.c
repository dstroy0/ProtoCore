// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_cache_proxy.c
 * @brief CDN edge-cache tier - server glue. See edge_cache_proxy.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t httpcache_work[16]; // the borrow an entry takes; Httpcache never reads it

static uint8_t http_range_work[16]; // the borrow an entry takes; HttpRange never reads it

#if PROTOCORE_ENABLE_EDGE_CACHE

#include "mmgr/membuild/membuild.h"   // protocore_sb frame builder
#include "mmgr/plaintext/plaintext.h" // the persistent end the cached bytes are taken from
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"
#include "mmgr/secure/secure.h" // and the secure end the TLS half is taken from
#include "network_drivers/presentation/http/httpcache/httpcache.h"
#include "server/web/edge_cache/edge_cache/edge_cache.h"
#include "server/web/edge_cache/edge_cache_proxy/edge_cache_proxy.h"
#include "services/storage/dbm/dbm.h"
#include "shared/http_date/http_date.h"

static uint8_t edge_cache_work[16]; // the borrow an entry takes; EdgeCache never reads it

static uint8_t edge_mesh_work[16]; // the borrow an entry takes; EdgeMesh never reads it

static uint8_t edge_cache_sd_work[16]; // the borrow an entry takes; EdgeCacheSd never reads it

static uint8_t edge_fetch_work[16]; // the borrow an entry takes; EdgeFetch never reads it

#include "network_drivers/presentation/http/http.h"                    // Http.set_edge_poll
#include "network_drivers/presentation/http/http_parser/http_parser.h" // HttpReq, http_get_header, http_pool
#include "network_drivers/transport/tcp/client/client.h"               // TcpClient: the dialed connection
#include "network_drivers/transport/tcp/protocol/protocol.h"           // ConnPool: the accepted slot
#include "network_drivers/transport/tcp/tcp.h"                         // protocore_client_*
#include "protocore.h"                                                 // PC, Middleware, MwResult, ChunkSource
#include "server/clock/clock.h"                                        // protocore_millis
#include "server/web/edge_cache/edge_fetch/edge_fetch.h"
PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_DBM
#include "server/web/edge_cache/edge_cache_sd/edge_cache_sd.h" // L2 SD tier
#endif
#include "network_drivers/application/http_range/http_range.h" // http_parse_byte_range (Range/206 support)
#include "services/net/http_client/http_client.h"              // HttpClient.parse_target_uri
#include "shared/mime/mime.h"                                  // PROTOCORE_MIME_TEXT_PLAIN
#if PROTOCORE_ENABLE_EDGE_ORIGIN_TLS
#include "network_drivers/tls/tls.h" // protocore_tls_client_session_* (TLS upstream origin fetch)
#endif
#if PROTOCORE_ENABLE_EDGE_MESH
#include "server/core/proto_handler.h"                 // ProtoHandler / Session.proto->add(PROTO_MESH serving)
#include "server/web/edge_cache/edge_mesh/edge_mesh.h" // mesh sibling-cache codec + peer-query engine
#endif

typedef struct
{
    proto_bool used;
    char prefix[MAX_PATH_LEN];
    char origin_host[PROTOCORE_EDGE_ORIGIN_URL_MAX];
    uint16_t origin_port;
    proto_bool https; ///< fetch this origin over TLS (PROTOCORE_ENABLE_EDGE_ORIGIN_TLS)
} EdgeRouteMap;

#if PROTOCORE_ENABLE_EDGE_MESH
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
    char canon[PROTOCORE_EDGE_KEY_MAX];
    EdgeRouteMap
        *route; // the origin route (stable in EDGE_CACHE_PROXY_CTX(work)->maps) - lets the origin fetch begin later
    char path[MAX_PATH_LEN]; // request path/query captured at mw time (http_pool[slot] is reused by poll time)
    char query[MAX_QUERY_LEN];
#if PROTOCORE_ENABLE_EDGE_MESH
    EdgeFetchPhase phase;
    EdgeMeshFetch mf;
    uint8_t peer_idx;                          // sibling currently being queried
    uint8_t mreq[PROTOCORE_EDGE_MESH_REQ_MAX]; // the mesh request, built once (reused across peers)
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

#if PROTOCORE_ENABLE_EDGE_MESH
// A configured sibling peer to query on a local miss.
typedef struct
{
    proto_bool used;
    char host[PROTOCORE_MESH_HOST_MAX];
    uint16_t port;
} MeshPeer;

// One in-flight inbound peer-serve connection: accumulate the request, answer from the local cache, page out.
typedef struct
{
    proto_bool active;
    uint8_t conn_slot;
    uint16_t req_len; // request bytes accumulated
    uint8_t reqbuf[PROTOCORE_EDGE_MESH_REQ_MAX];
    proto_bool responded; // the whole response is built (out_len) and paging out
    uint16_t out_off, out_len;
    uint8_t outbuf[PROTOCORE_EDGE_MESH_RESP_MAX];
} MeshConn;
#endif

#if PROTOCORE_ENABLE_EDGE_ORIGIN_TLS
// The TLS half, apart from the rest: these name the client-TLS session an https origin fetch runs
// over, so they take a SECURE borrow. Everything below is cached origin bytes, route maps and
// per-connection scratch, and takes the plaintext borrow beside it - the split quic_server makes.
typedef struct
{
    EdgeFetchTransport transport_tls; // TLS binding over protocore_tls_csess, used for https routes
    int tls_cid;                      // underlying protocore_client cid of the in-flight TLS fetch (singleton session)
    proto_bool tls_peer_closed;       // latched when the TLS session reports closed / errored
    proto_bool tls_ready;             // the handshake completed, so the session carries application bytes
} EdgeProxyTlsCtx;

// The one owned instance of the pointer to those bytes, private to this TU.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_EDGE_PROXY_TLS_BORROW secure bytes
} EdgeProxyTlsOwnCtx;
static EdgeProxyTlsOwnCtx s_tls_own;

// Not an entry: the TLS members are reached through this, the way an entry reaches its borrow.
static EdgeProxyTlsCtx *edge_tls(void)
{
    if (s_tls_own.span == NULL)
    {
        s_tls_own.span = protocore_secure_persist_span(PROTOCORE_EDGE_PROXY_TLS_BORROW).buf;
    }
    return (EdgeProxyTlsCtx *)(void *)s_tls_own.span; // null while the pool was short
}
static_assert(sizeof(EdgeProxyTlsCtx) <= PROTOCORE_EDGE_PROXY_TLS_BORROW,
              "PROTOCORE_EDGE_PROXY_TLS_BORROW is short of the TLS context - raise it in protocore_config.h,"
              " which sums it into its arena");
#endif // PROTOCORE_ENABLE_EDGE_ORIGIN_TLS

// The single owned file-static: all of this subsystem's mutable state.
typedef struct
{
    proto_bool registered;
    EdgeCacheStore store;
    EdgeRouteMap maps[PROTOCORE_EDGE_MAP_MAX];
    EdgeFetchSlot fetches[PROTOCORE_EDGE_FETCH_SLOTS];
    EdgePending pending[MAX_CONNS];
    EdgeServeCursor serve[MAX_CONNS];
    EdgeFetchTransport transport;
    char reqbuf[MAX_PATH_LEN + MAX_QUERY_LEN + 256]; // scratch for one origin request line (freed by send)
#if PROTOCORE_ENABLE_RANGE
    // The client's Range header, captured per slot at middleware time. serve_hit runs for a miss/stale
    // entry from the poll loop *after* the async fetch has reused http_pool[slot], so the original request
    // is no longer readable there - the window must be resolved against this captured copy.
    char range_hdr[MAX_CONNS][48];
#endif
#if PROTOCORE_ENABLE_DBM
    struct protocore_dbm *l2;                    // the persistent L2 tier (nullptr = L1-only)
    uint8_t sd_buf[PROTOCORE_EDGE_SD_VALUE_MAX]; // serialize/deserialize scratch for one L2 value
#endif
#if PROTOCORE_ENABLE_EDGE_MESH
    MeshPeer peers[PROTOCORE_MESH_MAX_PEERS]; // static sibling list queried on a full miss
    MeshConn mesh_conns[PROTOCORE_MESH_MAX_CONNS];
    char mesh_hdrs[PROTOCORE_MESH_HDRS_MAX]; // scratch: a served request's header snapshot (serve is single-threaded)
    proto_bool mesh_registered;              // the PROTO_MESH serving handler is installed
#endif
} EdgeCacheProxyCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define EDGE_CACHE_PROXY_OFF_CTX 0u
static_assert(EDGE_CACHE_PROXY_OFF_CTX + sizeof(EdgeCacheProxyCtx) <= PROTOCORE_EDGE_PROXY_BORROW,
              "PROTOCORE_EDGE_PROXY_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(EDGE_CACHE_PROXY_OFF_CTX % _Alignof(EdgeCacheProxyCtx) == 0,
              "EDGE_CACHE_PROXY_OFF_CTX is not a multiple of alignof(EdgeCacheProxyCtx) - EDGE_CACHE_PROXY_CTX() would "
              "return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define EDGE_CACHE_PROXY_CTX(w) ((EdgeCacheProxyCtx *)(void *)((w) + EDGE_CACHE_PROXY_OFF_CTX))

#if PROTOCORE_ENABLE_DBM
// L1 write-back hook: spill an evicted victim to L2 (edge_sd_put skips no-validator / oversize entries).
static void edge_on_evict(void *ctx, const EdgeEntry *victim)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_edge_cache_proxy_span();

    (void)ctx;
    EdgeCacheSd.put_args.db = EDGE_CACHE_PROXY_CTX(work)->l2;
    EdgeCacheSd.put_args.e = victim;
    EdgeCacheSd.put_args.scratch = EDGE_CACHE_PROXY_CTX(work)->sd_buf;
    EdgeCacheSd.put_args.scratch_cap = sizeof(EDGE_CACHE_PROXY_CTX(work)->sd_buf);
    EdgeCacheSd.put(edge_cache_sd_work);
    if (EDGE_CACHE_PROXY_CTX(work)->l2 && EdgeCacheSd.ok)
    {
        EDGE_CACHE_PROXY_CTX(work)->store.stats.l2_spills++;
    }
}
#endif

// --- protocore_client transport seam -------------------------------------------------------------------
static int t_open(void *c, const char *host, uint16_t port, uint32_t timeout)
{
    (void)c;
    TcpClient.dial.host = host;
    TcpClient.dial.port = port;
    TcpClient.dial.timeout_ms = timeout;
    TcpClient.open(protocore_tcp_client_span());
    return TcpClient.i32;
}
static proto_bool t_connected(void *c, int cid)
{
    (void)c;
    TcpClient.cid = cid;
    TcpClient.connected(protocore_tcp_client_span());
    return TcpClient.ok;
}
static proto_bool t_send(void *c, int cid, const void *d, size_t l)
{
    (void)c;
    TcpClient.cid = cid;
    TcpClient.io.data = d;
    TcpClient.io.len = l;
    TcpClient.send(protocore_tcp_client_span());
    return TcpClient.ok;
}
static size_t t_read(void *c, int cid, uint8_t *b, size_t cap)
{
    (void)c;
    TcpClient.cid = cid;
    TcpClient.io.buf = b;
    TcpClient.io.cap = cap;
    TcpClient.read(protocore_tcp_client_span());
    return TcpClient.n;
}
static proto_bool t_closed(void *c, int cid)
{
    (void)c;
    TcpClient.cid = cid;
    TcpClient.is_closed(protocore_tcp_client_span());
    return TcpClient.ok;
}
static void t_close(void *c, int cid)
{
    (void)c;
    TcpClient.cid = cid;
    TcpClient.close(protocore_tcp_client_span());
}

#if PROTOCORE_ENABLE_EDGE_ORIGIN_TLS
// --- TLS transport seam (protocore_tls_csess layered over protocore_client) -----------------------------------
// The client TLS session is a singleton, so the underlying cid + peer-closed latch live in the owned Ctx
// (one TLS fetch at a time, enforced by protocore_tls_client_session_active() in start_fetch). The BIO callbacks move
// ciphertext over protocore_client's wire ring for that cid - the same bridge the MQTT/WS clients use.
static int edge_tls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx;
    size_t cap = len > 0xFFFF ? 0xFFFF : len;
    TcpClient.cid = edge_tls()->tls_cid;
    TcpClient.io.data = buf;
    TcpClient.io.len = cap;
    TcpClient.send(protocore_tcp_client_span());
    return TcpClient.ok ? (int)cap : PROTOCORE_PLATFORM_TLS_WANT_WRITE;
}
static int edge_tls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    TcpClient.cid = edge_tls()->tls_cid;
    TcpClient.io.buf = buf;
    TcpClient.io.cap = len;
    TcpClient.read(protocore_tcp_client_span());
    size_t n = TcpClient.n;
    if (n == 0)
    {
        TcpClient.cid = edge_tls()->tls_cid;
        TcpClient.is_closed(protocore_tcp_client_span());
        return TcpClient.ok ? 0 : PROTOCORE_PLATFORM_TLS_WANT_READ;
    }
    return (int)n;
}

static int t_tls_open(void *c, const char *host, uint16_t port, uint32_t timeout)
{
    (void)c;
    TcpClient.dial.host = host;
    TcpClient.dial.port = port;
    TcpClient.dial.timeout_ms = timeout;
    TcpClient.open(protocore_tcp_client_span());
    edge_tls()->tls_cid = TcpClient.i32;
    if (edge_tls()->tls_cid < 0)
    {
        return -1;
    }
    edge_tls()->tls_peer_closed = PROTO_FALSE;
    edge_tls()->tls_ready = PROTO_FALSE;
    if (!protocore_tls_client_session_begin(host, edge_tls_bio_send, edge_tls_bio_recv))
    {
        TcpClient.cid = edge_tls()->tls_cid;
        TcpClient.close(protocore_tcp_client_span());
        edge_tls()->tls_cid = -1;
        return -1;
    }
    return edge_tls()->tls_cid;
}

// Step the TCP open, then the handshake, one flight per call. The BIO reads the wire ring, so a
// flight the peer has not sent yet leaves the handshake at 0 and the next call takes it further.
// The fetch pump is what calls this, and its own timeout bounds the whole thing.
static proto_bool t_tls_connected(void *c, int cid)
{
    (void)c;
    TcpClient.cid = cid;
    TcpClient.connected(protocore_tcp_client_span());
    if (!TcpClient.ok)
    {
        return PROTO_FALSE;
    }
    if (edge_tls()->tls_ready)
    {
        return PROTO_TRUE;
    }
    int h = protocore_tls_client_session_handshake();
    if (h == 1)
    {
        edge_tls()->tls_ready = PROTO_TRUE;
        return PROTO_TRUE;
    }
    if (h < 0)
    {
        edge_tls()->tls_peer_closed = PROTO_TRUE;
    }
    return PROTO_FALSE;
}
static proto_bool t_tls_send(void *c, int cid, const void *d, size_t l)
{
    (void)c;
    (void)cid;
    return protocore_tls_client_session_write((const uint8_t *)d, l) == (int)l;
}
static size_t t_tls_read(void *c, int cid, uint8_t *b, size_t cap)
{
    (void)c;
    (void)cid;
    int n = protocore_tls_client_session_read(b, cap);
    if (n < 0)
    {
        edge_tls()->tls_peer_closed = PROTO_TRUE; // close_notify / decrypt error -> report closed via t_tls_closed
    }
    return n > 0 ? (size_t)n : 0;
}
static proto_bool t_tls_closed(void *c, int cid)
{
    (void)c;
    TcpClient.cid = cid;
    TcpClient.is_closed(protocore_tcp_client_span());
    return edge_tls()->tls_peer_closed || TcpClient.ok;
}
static void t_tls_close(void *c, int cid)
{
    (void)c;
    protocore_tls_client_session_end();
    TcpClient.cid = cid;
    TcpClient.close(protocore_tcp_client_span());
    edge_tls()->tls_cid = -1;
    edge_tls()->tls_ready = PROTO_FALSE;
}
#endif // PROTOCORE_ENABLE_EDGE_ORIGIN_TLS

// Request-header lookup used to (re)serialize the Vary secondary key; ctx is the client HttpReq.
static const char *req_lookup(void *ctx, const char *name)
{
    HttpParserV.get_header_args.req = (const HttpReq *)ctx;
    HttpParserV.get_header_args.key = name;
    HttpParser.get_header(protocore_http_parser_span());
    return HttpParserV.text;
}

static EdgeRouteMap *map_match(uint8_t *restrict work, const char *path)
{
    for (int i = 0; i < PROTOCORE_EDGE_MAP_MAX; i++)
    {
        if (!EDGE_CACHE_PROXY_CTX(work)->maps[i].used)
        {
            continue;
        }
        if (str.starts(path, EDGE_CACHE_PROXY_CTX(work)->maps[i].prefix,
                       sizeof(EDGE_CACHE_PROXY_CTX(work)->maps[i].prefix), PROTO_FALSE))
        {
            return &EDGE_CACHE_PROXY_CTX(work)->maps[i];
        }
    }
    return NULL;
}

static int alloc_fetch(uint8_t *restrict work)
{
    for (int i = 0; i < PROTOCORE_EDGE_FETCH_SLOTS; i++)
    {
        if (!EDGE_CACHE_PROXY_CTX(work)->fetches[i].used)
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
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_edge_cache_proxy_span();

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
            EdgeCache.store_free_entry_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
            EdgeCache.store_free_entry_args.e = c->entry;
            EdgeCache.store_free_entry(edge_cache_work);
        }
        c->entry = NULL;
        return 0;
    }
    size_t n = remaining < cap ? remaining : cap;
    mem.cpy(buf, c->entry->body + c->off, n);
    c->off += n;
    return n;
}

// Serve a cache entry, replaying its validators + Age, tagged with @p xcache. A client `Range` request
// (PROTOCORE_ENABLE_RANGE) is answered with a 206 window (or 416 if unsatisfiable); otherwise a full 200.
static void serve_hit(uint8_t *restrict work, uint8_t slot, EdgeEntry *e, uint32_t now, const char *xcache)
{
    EdgeServeCursor *c = &EDGE_CACHE_PROXY_CTX(work)->serve[slot];
    c->active = PROTO_TRUE;
    c->entry = e;
    c->off = 0;
    c->end = e->body_len;
    int status = 200;

#if PROTOCORE_ENABLE_RANGE
    const char *range =
        EDGE_CACHE_PROXY_CTX(work)->range_hdr[slot]; // captured at mw time (http_pool[slot] is stale post-fetch)
    if (range[0])
    {
        size_t rs = 0;
        size_t re = 0;
        HttpRangeV.http_parse_byte_range_args.hdr = range;
        HttpRangeV.http_parse_byte_range_args.size = e->body_len;
        HttpRangeV.http_parse_byte_range_args.out_start = &rs;
        HttpRangeV.http_parse_byte_range_args.out_end = &re;
        HttpRange.http_parse_byte_range(http_range_work);
        int rr = HttpRangeV.n;
        if (rr < 0) // syntactically valid but unsatisfiable -> 416, no body window served
        {
            char cr[48];
            protocore_sb sb_cr = {cr, sizeof(cr), 0, PROTO_TRUE};
            Sb.put(&sb_cr, "bytes */");
            Sb.u32(&sb_cr, (uint32_t)((unsigned)e->body_len));
            if (Sb.finish(&sb_cr) == 0)
            {
                cr[0] = '\0';
            }
            proto_add_response_header(slot, "Content-Range", cr);
            c->active = PROTO_FALSE;
            c->entry = NULL;
            send_text(slot, 416, PROTOCORE_MIME_TEXT_PLAIN, "Range Not Satisfiable");
            return;
        }
        if (rr > 0) // satisfiable -> 206 with just the requested window [rs, re]
        {
            status = 206;
            c->off = (uint32_t)rs;
            c->end = (uint32_t)re + 1;
            char cr[48];
            protocore_sb sb_cr2 = {cr, sizeof(cr), 0, PROTO_TRUE};
            Sb.put(&sb_cr2, "bytes ");
            Sb.u32(&sb_cr2, (uint32_t)((unsigned)rs));
            Sb.put(&sb_cr2, "-");
            Sb.u32(&sb_cr2, (uint32_t)((unsigned)re));
            Sb.put(&sb_cr2, "/");
            Sb.u32(&sb_cr2, (uint32_t)((unsigned)e->body_len));
            if (Sb.finish(&sb_cr2) == 0)
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
    EdgeCache.current_age_args.initial_age = e->initial_age;
    EdgeCache.current_age_args.insert_ms = e->insert_ms;
    EdgeCache.current_age_args.now_ms = now;
    EdgeCache.current_age(edge_cache_work);
    long age = EdgeCache.secs;
    if (age < 0)
    {
        age = 0;
    }
    char agebuf[12];
    protocore_sb sb_agebuf = {agebuf, sizeof(agebuf), 0, PROTO_TRUE};
    Sb.i64(&sb_agebuf, (int64_t)(age));
    if (Sb.finish(&sb_agebuf) == 0)
    {
        agebuf[0] = '\0';
    }
    proto_add_response_header(slot, "Age", agebuf);
    const char *ct = e->content_type[0] ? e->content_type : "application/octet-stream";
    send_chunked(slot, status, ct, edge_chunk_source, c);
}

// Serve a non-cacheable / non-200 origin response through a transient unindexed store slot, so the
// serve source outlives the fetch (which the caller frees) and no-store content is never re-served.
static void serve_passthrough(uint8_t *restrict work, uint8_t slot, EdgeFetch *f)
{
    EdgeCache.store_alloc_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
    EdgeCache.store_alloc_args.canon = "";
    EdgeCache.store_alloc_args.vary_key = "";
    EdgeCache.store_alloc(edge_cache_work);
    EdgeEntry *e = EdgeCache.entry; // key "" -> never matched by a lookup
    if (!e)
    {
        send_text(slot, 502, PROTOCORE_MIME_TEXT_PLAIN, "Bad Gateway");
        return;
    }
    EDGE_CACHE_PROXY_CTX(work)->store.stats.stores--; // a transient is not a cache store
    e->status = f->status;
    EdgeCache.header_value_args.hdrs = (const char *)f->buf;
    EdgeCache.header_value_args.len = f->head_len;
    EdgeCache.header_value_args.name = "Content-Type";
    EdgeCache.header_value_args.out = e->content_type;
    EdgeCache.header_value_args.out_cap = sizeof(e->content_type);
    EdgeCache.header_value(edge_cache_work);
    if (!EdgeCache.ok)
    {
        str.copy(e->content_type, "application/octet-stream", sizeof(e->content_type));
    }
    EdgeCache.header_value_args.hdrs = (const char *)f->buf;
    EdgeCache.header_value_args.len = f->head_len;
    EdgeCache.header_value_args.name = "Content-Encoding";
    EdgeCache.header_value_args.out = e->content_encoding;
    EdgeCache.header_value_args.out_cap = sizeof(e->content_encoding);
    EdgeCache.header_value(edge_cache_work);
    size_t bl = f->body_len;
    if (bl > PROTOCORE_EDGE_BODY_MAX)
    {
        bl = PROTOCORE_EDGE_BODY_MAX;
    }
    mem.cpy(e->body, f->buf + f->body_off, bl);
    e->body_len = (uint16_t)bl;

    EdgeServeCursor *c = &EDGE_CACHE_PROXY_CTX(work)->serve[slot];
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
static void store_response(uint8_t *restrict work, uint8_t slot, EdgeFetchSlot *fs, HttpReq *req,
                           const protocore_cache_control *cc, const char *vary_hdr, uint32_t now)
{
    EdgeFetch *f = &fs->f;
    const char *head = (const char *)f->buf;
    size_t head_len = f->head_len;

    char vary_vals[PROTOCORE_EDGE_VARY_MAX];
    EdgeCache.vary_serialize_args.vary_header = vary_hdr[0] ? vary_hdr : NULL;
    EdgeCache.vary_serialize_args.lookup = req_lookup;
    EdgeCache.vary_serialize_args.ctx = req;
    EdgeCache.vary_serialize_args.out = vary_vals;
    EdgeCache.vary_serialize_args.out_cap = sizeof(vary_vals);
    EdgeCache.vary_serialize(edge_cache_work);

    EdgeCache.store_alloc_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
    EdgeCache.store_alloc_args.canon = fs->canon;
    EdgeCache.store_alloc_args.vary_key = vary_vals;
    EdgeCache.store_alloc(edge_cache_work);
    EdgeEntry *e = EdgeCache.entry;
    if (!e)
    {
        serve_passthrough(work, slot, f);
        return;
    }
    e->status = 200;
    EdgeCache.header_value_args.hdrs = head;
    EdgeCache.header_value_args.len = head_len;
    EdgeCache.header_value_args.name = "Content-Type";
    EdgeCache.header_value_args.out = e->content_type;
    EdgeCache.header_value_args.out_cap = sizeof(e->content_type);
    EdgeCache.header_value(edge_cache_work);
    EdgeCache.header_value_args.hdrs = head;
    EdgeCache.header_value_args.len = head_len;
    EdgeCache.header_value_args.name = "Content-Encoding";
    EdgeCache.header_value_args.out = e->content_encoding;
    EdgeCache.header_value_args.out_cap = sizeof(e->content_encoding);
    EdgeCache.header_value(edge_cache_work);
    EdgeCache.header_value_args.hdrs = head;
    EdgeCache.header_value_args.len = head_len;
    EdgeCache.header_value_args.name = "ETag";
    EdgeCache.header_value_args.out = e->etag;
    EdgeCache.header_value_args.out_cap = sizeof(e->etag);
    EdgeCache.header_value(edge_cache_work);
    EdgeCache.header_value_args.hdrs = head;
    EdgeCache.header_value_args.len = head_len;
    EdgeCache.header_value_args.name = "Last-Modified";
    EdgeCache.header_value_args.out = e->last_modified;
    EdgeCache.header_value_args.out_cap = sizeof(e->last_modified);
    EdgeCache.header_value(edge_cache_work);
    size_t vhl = str.len(vary_hdr, sizeof(e->vary_names));
    if (vary_hdr[0] && vhl < sizeof(e->vary_names))
    {
        mem.cpy(e->vary_names, vary_hdr, vhl + 1);
    }

    size_t bl = f->body_len;
    if (bl > PROTOCORE_EDGE_BODY_MAX)
    {
        bl = PROTOCORE_EDGE_BODY_MAX;
    }
    mem.cpy(e->body, f->buf + f->body_off, bl);
    e->body_len = (uint16_t)bl;
    EDGE_CACHE_PROXY_CTX(work)->store.stats.bytes_stored += bl;

    int64_t date = -1;
    int64_t expires = -1;
    int64_t last_mod = -1;
    int32_t age = 0;
    char v[64];
    EdgeCache.header_value_args.hdrs = head;
    EdgeCache.header_value_args.len = head_len;
    EdgeCache.header_value_args.name = "Date";
    EdgeCache.header_value_args.out = v;
    EdgeCache.header_value_args.out_cap = sizeof(v);
    EdgeCache.header_value(edge_cache_work);
    if (EdgeCache.ok)
    {
        EdgeCache.parse_http_date_args.s = v;
        EdgeCache.parse_http_date_args.len = str.len(v, sizeof(v));
        EdgeCache.parse_http_date(edge_cache_work);
        date = EdgeCache.epoch;
    }
    EdgeCache.header_value_args.hdrs = head;
    EdgeCache.header_value_args.len = head_len;
    EdgeCache.header_value_args.name = "Expires";
    EdgeCache.header_value_args.out = v;
    EdgeCache.header_value_args.out_cap = sizeof(v);
    EdgeCache.header_value(edge_cache_work);
    if (EdgeCache.ok)
    {
        EdgeCache.parse_http_date_args.s = v;
        EdgeCache.parse_http_date_args.len = str.len(v, sizeof(v));
        EdgeCache.parse_http_date(edge_cache_work);
        expires = EdgeCache.epoch;
    }
    if (e->last_modified[0])
    {
        EdgeCache.parse_http_date_args.s = e->last_modified;
        EdgeCache.parse_http_date_args.len = str.len(e->last_modified, sizeof(e->last_modified));
        EdgeCache.parse_http_date(edge_cache_work);
        last_mod = EdgeCache.epoch;
    }
    EdgeCache.header_value_args.hdrs = head;
    EdgeCache.header_value_args.len = head_len;
    EdgeCache.header_value_args.name = "Age";
    EdgeCache.header_value_args.out = v;
    EdgeCache.header_value_args.out_cap = sizeof(v);
    EdgeCache.header_value(edge_cache_work);
    if (EdgeCache.ok)
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
    EdgeCache.entry_set_freshness_args.e = e;
    EdgeCache.entry_set_freshness_args.cc = cc;
    EdgeCache.entry_set_freshness_args.shared = /*shared=*/PROTO_TRUE;
    EdgeCache.entry_set_freshness_args.date_epoch = date;
    EdgeCache.entry_set_freshness_args.expires_epoch = expires;
    EdgeCache.entry_set_freshness_args.last_modified_epoch = last_mod;
    EdgeCache.entry_set_freshness_args.age_hdr = age;
    EdgeCache.entry_set_freshness_args.response_time_epoch = /*response_time=*/-1;
    EdgeCache.entry_set_freshness_args.now_ms = now;
    EdgeCache.entry_set_freshness(edge_cache_work);
    serve_hit(work, slot, e, now, "MISS");
}

// A completed origin fetch: revalidation 304 / store 200 / pass through anything else.
static void on_fetch_done(uint8_t *restrict work, uint8_t slot, EdgeFetchSlot *fs, uint32_t now)
{
    EdgeFetch *f = &fs->f;
    const char *head = (const char *)f->buf;
    size_t head_len = f->head_len;
    HttpReq *req = &http_pool[slot];

    if (fs->revalidate && f->status == 304 && fs->reval_entry)
    {
        EdgeCache.apply_304_args.e = fs->reval_entry;
        EdgeCache.apply_304_args.new_hdrs = head;
        EdgeCache.apply_304_args.hdr_len = head_len;
        EdgeCache.apply_304_args.response_time_epoch = -1;
        EdgeCache.apply_304_args.now_ms = now;
        EdgeCache.apply_304(edge_cache_work);
        EDGE_CACHE_PROXY_CTX(work)->store.stats.revalidations_304++;
        serve_hit(work, slot, fs->reval_entry, now, "REVALIDATED");
        return;
    }
    if (f->status == 200)
    {
        protocore_cache_control cc;
        HttpcacheV.control_init_args.cc = &cc;
        Httpcache.control_init(httpcache_work);
        char v[128];
        EdgeCache.header_value_args.hdrs = head;
        EdgeCache.header_value_args.len = head_len;
        EdgeCache.header_value_args.name = "Cache-Control";
        EdgeCache.header_value_args.out = v;
        EdgeCache.header_value_args.out_cap = sizeof(v);
        EdgeCache.header_value(edge_cache_work);
        if (EdgeCache.ok)
        {
            HttpcacheV.control_parse_args.s = v;
            HttpcacheV.control_parse_args.len = str.len(v, sizeof(v));
            HttpcacheV.control_parse_args.cc = &cc;
            Httpcache.control_parse(httpcache_work);
        }
        char vary_hdr[PROTOCORE_EDGE_VARY_MAX];
        vary_hdr[0] = '\0';
        EdgeCache.header_value_args.hdrs = head;
        EdgeCache.header_value_args.len = head_len;
        EdgeCache.header_value_args.name = "Vary";
        EdgeCache.header_value_args.out = vary_hdr;
        EdgeCache.header_value_args.out_cap = sizeof(vary_hdr);
        EdgeCache.header_value(edge_cache_work);
        EdgeCache.is_storeable_args.status = 200;
        EdgeCache.is_storeable_args.method = "GET";
        EdgeCache.is_storeable_args.cc = &cc;
        EdgeCache.is_storeable_args.vary_header = vary_hdr[0] ? vary_hdr : NULL;
        EdgeCache.is_storeable_args.body_len = f->body_len;
        EdgeCache.is_storeable(edge_cache_work);
        if (EdgeCache.ok)
        {
            if (fs->revalidate && fs->reval_entry) // 200 on a revalidation replaces the stale entry
            {
                EdgeCache.store_free_entry_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
                EdgeCache.store_free_entry_args.e = fs->reval_entry;
                EdgeCache.store_free_entry(edge_cache_work);
                EDGE_CACHE_PROXY_CTX(work)->store.stats.replaces_200++;
            }
            store_response(work, slot, fs, req, &cc, vary_hdr, now);
            return;
        }
        serve_passthrough(work, slot, f); // 200 but not storeable
        return;
    }
    serve_passthrough(work, slot, f); // non-200 status
}

// Forward decls for the seam functions installed by protocore_edge_cache_enable().
static MwResult edge_cache_mw(uint8_t slot, HttpReq *req);
static proto_bool edge_cache_poll(uint8_t slot);

// Build + begin the origin fetch for @p fs from its captured route/path/query (so it can begin either
// immediately at mw time or later, after the mesh phase exhausts its peers). Picks the plaintext or TLS
// transport; a revalidation adds the conditional headers. @return false if no fetch could start (fail open).
static proto_bool begin_origin_fetch(uint8_t *restrict work, EdgeFetchSlot *fs, uint32_t now)
{
    EdgeRouteMap *m = fs->route;
    const EdgeFetchTransport *tport = &EDGE_CACHE_PROXY_CTX(work)->transport;
#if PROTOCORE_ENABLE_EDGE_ORIGIN_TLS
    if (m->https)
    {
        if (protocore_tls_client_session_active())
        {
            return PROTO_FALSE; // the shared client-TLS session is busy -> fail open (never tear down a live one)
        }
        tport = &edge_tls()->transport_tls;
    }
#endif
    char cond[192];
    cond[0] = '\0';
    if (fs->reval_entry)
    {
        EdgeCache.build_conditional_args.e = fs->reval_entry;
        EdgeCache.build_conditional_args.out = cond;
        EdgeCache.build_conditional_args.cap = sizeof(cond);
        EdgeCache.build_conditional(edge_cache_work);
    }
    protocore_sb sb_reqbuf = {EDGE_CACHE_PROXY_CTX(work)->reqbuf, sizeof(EDGE_CACHE_PROXY_CTX(work)->reqbuf), 0,
                              PROTO_TRUE};
    Sb.put(&sb_reqbuf, "GET ");
    Sb.put(&sb_reqbuf, fs->path);
    Sb.put(&sb_reqbuf, fs->query[0] ? "?" : "");
    Sb.put(&sb_reqbuf, fs->query);
    Sb.put(&sb_reqbuf, " HTTP/1.1\r\nHost: ");
    Sb.put(&sb_reqbuf, m->origin_host);
    Sb.put(&sb_reqbuf, "\r\nUser-Agent: PC-EdgeCache\r\nConnection: close\r\n");
    Sb.put(&sb_reqbuf, cond);
    Sb.put(&sb_reqbuf, "\r\n");
    int rl = (int)Sb.finish(&sb_reqbuf);
    if (rl <= 0 || (size_t)rl >= sizeof(EDGE_CACHE_PROXY_CTX(work)->reqbuf))
    {
        return PROTO_FALSE;
    }
    EdgeFetcher.begin_args.f = &fs->f;
    EdgeFetcher.begin_args.t = tport;
    EdgeFetcher.begin_args.host = m->origin_host;
    EdgeFetcher.begin_args.port = m->origin_port;
    EdgeFetcher.begin_args.request = EDGE_CACHE_PROXY_CTX(work)->reqbuf;
    EdgeFetcher.begin_args.req_len = (size_t)rl;
    EdgeFetcher.begin_args.now_ms = now;
    EdgeFetcher.begin(edge_fetch_work);
    if (fs->f.st == EDGE_FETCH_STATUS_FAILED)
    {
        EdgeFetcher.end_args.f = &fs->f;
        EdgeFetcher.end_args.t = tport;
        EdgeFetcher.end(edge_fetch_work);
        return PROTO_FALSE;
    }
    fs->transport = tport;
    return PROTO_TRUE;
}

#if PROTOCORE_ENABLE_EDGE_MESH
static int mesh_peer_count(uint8_t *restrict work)
{
    int n = 0;
    for (int i = 0; i < PROTOCORE_MESH_MAX_PEERS; i++)
    {
        if (EDGE_CACHE_PROXY_CTX(work)->peers[i].used)
        {
            n++;
        }
    }
    return n;
}

// The @p n-th used peer in slot order, or nullptr.
static MeshPeer *mesh_peer_nth(uint8_t *restrict work, int n)
{
    for (int i = 0; i < PROTOCORE_MESH_MAX_PEERS; i++)
    {
        if (EDGE_CACHE_PROXY_CTX(work)->peers[i].used && n-- == 0)
        {
            return &EDGE_CACHE_PROXY_CTX(work)->peers[i];
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
        size_t kl = str.len(k, MAX_KEY_LEN);
        size_t vl = str.len(v, MAX_VAL_LEN);
        if (pos + kl + 1 + vl + 1 >= cap)
        {
            break;
        }
        mem.cpy(out + pos, k, kl);
        pos += kl;
        out[pos++] = '\x1e';
        mem.cpy(out + pos, v, vl);
        pos += vl;
        out[pos++] = '\x1f';
    }
    out[pos] = '\0';
}

// The peer query reuses the slot's origin response buffer (the mesh and origin phases never run together).
#if PROTOCORE_EDGE_FETCH_BUF < PROTOCORE_EDGE_MESH_RESP_MAX
#error "PROTOCORE_EDGE_FETCH_BUF must hold a mesh response (>= PROTOCORE_EDGE_MESH_RESP_MAX). It defaults to that "\
       "floor, so this only fires if you pinned it lower; raise PROTOCORE_EDGE_FETCH_BUF or lower PROTOCORE_EDGE_BODY_MAX."
#endif

// Begin the mesh query against the peer at fs->peer_idx. @return false if there is no such peer.
static proto_bool mesh_begin_peer(uint8_t *restrict work, EdgeFetchSlot *fs, uint32_t now)
{
    MeshPeer *p = mesh_peer_nth(work, fs->peer_idx);
    if (!p)
    {
        return PROTO_FALSE;
    }
    EdgeMesh.fetch_begin_args.m = &fs->mf;
    EdgeMesh.fetch_begin_args.t = &EDGE_CACHE_PROXY_CTX(work)->transport;
    EdgeMesh.fetch_begin_args.host = p->host;
    EdgeMesh.fetch_begin_args.port = p->port;
    EdgeMesh.fetch_begin_args.request = fs->mreq;
    EdgeMesh.fetch_begin_args.req_len = fs->mreq_len;
    EdgeMesh.fetch_begin_args.buf = fs->f.buf;
    EdgeMesh.fetch_begin_args.cap = sizeof(fs->f.buf);
    EdgeMesh.fetch_begin_args.now_ms = now;
    EdgeMesh.fetch_begin(edge_mesh_work);
    return PROTO_TRUE;
}

// A peer HIT: rehydrate the entry into a fresh L1 slot, verify it matches the request, and serve it as fresh
// (age propagated). @return true if it was served; false (freeing the slot) if corrupt / wrong / already stale.
static proto_bool mesh_store_and_serve(uint8_t *restrict work, uint8_t slot, EdgeFetchSlot *fs, uint32_t now)
{
    EdgeCache.store_alloc_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
    EdgeCache.store_alloc_args.canon = fs->canon;
    EdgeCache.store_alloc_args.vary_key = "";
    EdgeCache.store_alloc(edge_cache_work);
    EdgeEntry *e = EdgeCache.entry;
    if (!e)
    {
        return PROTO_FALSE;
    }
    EdgeMesh.deserialize_entry_args.entry_buf = EDGE_CACHE_PROXY_CTX(work)->store.digest_work;
    EdgeMesh.deserialize_entry_args.buf = fs->mf.buf + fs->mf.entry_off;
    EdgeMesh.deserialize_entry_args.len = fs->mf.entry_len;
    EdgeMesh.deserialize_entry_args.e = e;
    EdgeMesh.deserialize_entry_args.now_ms = now;
    EdgeMesh.deserialize_entry(edge_mesh_work);
    EdgeCache.entry_fresh_args.e = e;
    EdgeCache.entry_fresh_args.now_ms = now;
    EdgeCache.entry_fresh(edge_cache_work);
    if (!EdgeMesh.ok || !str.eq(e->key, fs->canon, sizeof(fs->canon), PROTO_FALSE) || !EdgeCache.ok)
    {
        EdgeCache.store_free_entry_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
        EdgeCache.store_free_entry_args.e = e;
        EdgeCache.store_free_entry(edge_cache_work);
        return PROTO_FALSE;
    }
    EDGE_CACHE_PROXY_CTX(work)->store.stats.bytes_stored += e->body_len;
    serve_hit(work, slot, e, now, "MESH");
    return PROTO_TRUE;
}

// The current peer query ended without a served hit: try the next sibling, else begin the origin fetch.
// @return true if the slot still owns work (mesh continues or origin began); false = give up.
static proto_bool mesh_advance_or_origin(uint8_t *restrict work, EdgeFetchSlot *fs, uint32_t now)
{
    fs->peer_idx++;
    if (mesh_begin_peer(work, fs, now))
    {
        return PROTO_TRUE; // querying the next sibling (still MESH phase)
    }
    EDGE_CACHE_PROXY_CTX(work)->store.stats.mesh_misses++;
    if (begin_origin_fetch(work, fs, now))
    {
        fs->phase = EDGE_FETCH_PHASE_ORIGIN;
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}
#endif // PROTOCORE_ENABLE_EDGE_MESH

static proto_bool start_fetch(uint8_t *restrict work, uint8_t slot, HttpReq *req, EdgeRouteMap *m, const char *canon,
                              EdgeEntry *reval, uint32_t now)
{
    int fi = alloc_fetch(work);
    if (fi < 0)
    {
        return PROTO_FALSE;
    }
    EdgeFetchSlot *fs = &EDGE_CACHE_PROXY_CTX(work)->fetches[fi];
    fs->client_slot = slot;
    fs->revalidate = (reval != NULL);
    fs->reval_entry = reval;
    fs->route = m;
    mem.cpy(fs->canon, canon, str.len(canon, sizeof(fs->canon) - 1) + 1);
    str.copy(fs->path, req->path, sizeof(fs->path));
    str.copy(fs->query, req->query, sizeof(fs->query));

#if PROTOCORE_ENABLE_EDGE_MESH
    // On a full miss (not a revalidation) with >= 1 sibling, query the mesh before the origin.
    if (!reval && mesh_peer_count(work) > 0)
    {
        uint8_t digest[32];
        EdgeCache.key_digest_args.digest_work = EDGE_CACHE_PROXY_CTX(work)->store.digest_work;
        EdgeCache.key_digest_args.canon = canon;
        EdgeCache.key_digest_args.len = str.len(canon, PROTOCORE_EDGE_KEY_MAX);
        EdgeCache.key_digest_args.digest = digest;
        EdgeCache.key_digest(edge_cache_work);
        mesh_snapshot_headers(req, EDGE_CACHE_PROXY_CTX(work)->mesh_hdrs,
                              sizeof(EDGE_CACHE_PROXY_CTX(work)->mesh_hdrs));
        EdgeMesh.build_request_args.digest = digest;
        EdgeMesh.build_request_args.canon = canon;
        EdgeMesh.build_request_args.req_hdrs = EDGE_CACHE_PROXY_CTX(work)->mesh_hdrs;
        EdgeMesh.build_request_args.out = fs->mreq;
        EdgeMesh.build_request_args.cap = sizeof(fs->mreq);
        EdgeMesh.build_request(edge_mesh_work);
        fs->mreq_len = EdgeMesh.n;
        fs->peer_idx = 0;
        if (fs->mreq_len > 0 && mesh_begin_peer(work, fs, now))
        {
            fs->phase = EDGE_FETCH_PHASE_MESH;
            fs->used = PROTO_TRUE;
            EDGE_CACHE_PROXY_CTX(work)->pending[slot].active = PROTO_TRUE;
            EDGE_CACHE_PROXY_CTX(work)->pending[slot].fetch_idx = (uint8_t)fi;
            return PROTO_TRUE;
        }
    }
    fs->phase = EDGE_FETCH_PHASE_ORIGIN;
#endif
    if (!begin_origin_fetch(work, fs, now))
    {
        return PROTO_FALSE; // fs->used stays false -> the slot is reclaimed
    }
    fs->used = PROTO_TRUE;
    EDGE_CACHE_PROXY_CTX(work)->pending[slot].active = PROTO_TRUE;
    EDGE_CACHE_PROXY_CTX(work)->pending[slot].fetch_idx = (uint8_t)fi;
    return PROTO_TRUE;
}

#if PROTOCORE_ENABLE_DBM
// Promote a reboot-surviving entry from L2 into a fresh L1 slot, forced stale so the caller revalidates it
// (the monotonic insert time is meaningless across a reboot). @return the promoted entry, or nullptr.
static EdgeEntry *try_promote_l2(uint8_t *restrict work, const char *canon, uint32_t now)
{
    uint8_t digest[32];
    EdgeCache.key_digest_args.digest_work = EDGE_CACHE_PROXY_CTX(work)->store.digest_work;
    EdgeCache.key_digest_args.canon = canon;
    EdgeCache.key_digest_args.len = str.len(canon, PROTOCORE_EDGE_KEY_MAX);
    EdgeCache.key_digest_args.digest = digest;
    EdgeCache.key_digest(edge_cache_work);
    EdgeCache.store_alloc_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
    EdgeCache.store_alloc_args.canon = canon;
    EdgeCache.store_alloc_args.vary_key = "";
    EdgeCache.store_alloc(edge_cache_work);
    EdgeEntry *e = EdgeCache.entry; // may evict + write-back an L1 victim first
    if (!e)
    {
        return NULL;
    }
    EdgeCacheSd.get_args.entry_buf = EDGE_CACHE_PROXY_CTX(work)->store.digest_work;
    EdgeCacheSd.get_args.db = EDGE_CACHE_PROXY_CTX(work)->l2;
    EdgeCacheSd.get_args.digest = digest;
    EdgeCacheSd.get_args.e = e;
    EdgeCacheSd.get_args.scratch = EDGE_CACHE_PROXY_CTX(work)->sd_buf;
    EdgeCacheSd.get_args.scratch_cap = sizeof(EDGE_CACHE_PROXY_CTX(work)->sd_buf);
    EdgeCacheSd.get(edge_cache_sd_work);
    if (!EdgeCacheSd.ok || !str.eq(e->key, canon, sizeof(e->key), PROTO_FALSE))
    {
        EdgeCache.store_free_entry_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
        EdgeCache.store_free_entry_args.e = e;
        EdgeCache.store_free_entry(edge_cache_work); // L2 miss or digest collision -> not promoted
        return NULL;
    }
    e->lifetime_s = 0; // force stale: freshness is untrustworthy across a reboot -> caller revalidates
    e->initial_age = 0;
    e->date_epoch = e->expires_epoch = -1;
    e->age_hdr = 0;
    e->insert_ms = now;
    e->last_used_ms = now;
    EDGE_CACHE_PROXY_CTX(work)->store.stats.l2_promotes++;
    return e;
}
#endif

// The cache middleware: fresh hit -> serve; stale/miss -> start an async origin fetch (suspend); else
// fall through (fail open).
static MwResult edge_cache_mw(uint8_t slot, HttpReq *req)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_edge_cache_proxy_span();

    // `registered` is the whole test now: it was always the real question, and the stored server
    // pointer it was AND-ed with was set by the same call that set it.
    if (!EDGE_CACHE_PROXY_CTX(work)->registered || slot >= MAX_CONNS)
    {
        return MW_NEXT;
    }
    proto_bool is_get = str.eq(req->method, "GET", sizeof("GET"), PROTO_FALSE);
    proto_bool is_head = str.eq(req->method, "HEAD", sizeof("HEAD"), PROTO_FALSE);
    if (!is_get && !is_head)
    {
        return MW_NEXT; // only cache safe methods
    }
    HttpParserV.get_header_args.req = req;
    HttpParserV.get_header_args.key = "Authorization";
    HttpParser.get_header(protocore_http_parser_span());
    if (HttpParserV.text)
    {
        return MW_NEXT; // never cache authorized/private requests
    }
    EdgeRouteMap *m = map_match(work, req->path);
    if (!m)
    {
        return MW_NEXT; // not a mapped origin
    }

    HttpParserV.get_header_args.req = req;
    HttpParserV.get_header_args.key = "Host";
    HttpParser.get_header(protocore_http_parser_span());
    const char *host = HttpParserV.text;
    if (!host)
    {
        host = "";
    }
    char canon[PROTOCORE_EDGE_KEY_MAX];
    EdgeCache.key_canon_args.method = "GET";
    EdgeCache.key_canon_args.host = host;
    EdgeCache.key_canon_args.path = req->path;
    EdgeCache.key_canon_args.query = req->query;
    EdgeCache.key_canon_args.include_query = /*include_query=*/PROTO_TRUE;
    EdgeCache.key_canon_args.out = canon;
    EdgeCache.key_canon_args.out_cap = sizeof(canon);
    EdgeCache.key_canon(edge_cache_work);
    if (EdgeCache.n == 0)
    {
        return MW_NEXT; // key too long -> uncacheable, fail open
    }

#if PROTOCORE_ENABLE_RANGE
    // Capture the Range header now, while http_pool[slot] is the client request: a miss serves from the
    // poll after the async fetch has reused that buffer, so serve_hit resolves the window against this copy.
    HttpParserV.get_header_args.req = req;
    HttpParserV.get_header_args.key = "Range";
    HttpParser.get_header(protocore_http_parser_span());
    const char *rh = HttpParserV.text;
    str.copy(EDGE_CACHE_PROXY_CTX(work)->range_hdr[slot], rh ? rh : "",
             sizeof(EDGE_CACHE_PROXY_CTX(work)->range_hdr[slot]));
#endif

    uint32_t now = Clock.ms;
    EdgeCache.store_find_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
    EdgeCache.store_find_args.canon = canon;
    EdgeCache.store_find_args.lookup = req_lookup;
    EdgeCache.store_find_args.ctx = req;
    EdgeCache.store_find_args.now_ms = now;
    EdgeCache.store_find(edge_cache_work);
    EdgeEntry *e = EdgeCache.entry;
    EdgeCache.entry_fresh_args.e = e;
    EdgeCache.entry_fresh_args.now_ms = now;
    EdgeCache.entry_fresh(edge_cache_work);
    if (e && EdgeCache.ok)
    {
        EDGE_CACHE_PROXY_CTX(work)->store.stats.hits++;
        serve_hit(work, slot, e, now, "HIT");
        return MW_HALT;
    }
#if PROTOCORE_ENABLE_DBM
    if (!e && EDGE_CACHE_PROXY_CTX(work)
                  ->l2) // L1 miss: try promoting a reboot-surviving entry from L2 (force-stale -> revalidate)
    {
        e = try_promote_l2(work, canon, now);
    }
#endif
    EDGE_CACHE_PROXY_CTX(work)->store.stats.misses++;
    EdgeCache.entry_has_validator_args.e = e;
    EdgeCache.entry_has_validator(edge_cache_work);
    EdgeEntry *reval = (e && EdgeCache.ok) ? e : NULL;
    if (!start_fetch(work, slot, req, m, canon, reval, now))
    {
        return MW_NEXT; // no fetch slot / origin open failed -> fail open to normal dispatch
    }
    return MW_HALT; // client request suspended until the fetch completes
}

// Per-slot poll hook: drive an in-flight sibling query then origin fetch, then serve. Returns true while it
// owns the slot.
static proto_bool edge_cache_poll(uint8_t slot)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_edge_cache_proxy_span();

    if (slot >= MAX_CONNS || !EDGE_CACHE_PROXY_CTX(work)->pending[slot].active)
    {
        return PROTO_FALSE;
    }
    uint8_t fi = EDGE_CACHE_PROXY_CTX(work)->pending[slot].fetch_idx;
    EdgeFetchSlot *fs = &EDGE_CACHE_PROXY_CTX(work)->fetches[fi];
    uint32_t now = Clock.ms;

#if PROTOCORE_ENABLE_EDGE_MESH
    if (fs->phase == EDGE_FETCH_PHASE_MESH)
    {
        ConnPool.slot = slot;
        ConnPool.active(protocore_conn_pool_span());
        if (!ConnPool.ok) // client vanished mid-query: abort
        {
            EdgeMesh.fetch_end_args.m = &fs->mf;
            EdgeMesh.fetch_end_args.t = &EDGE_CACHE_PROXY_CTX(work)->transport;
            EdgeMesh.fetch_end(edge_mesh_work);
            fs->used = PROTO_FALSE;
            EDGE_CACHE_PROXY_CTX(work)->pending[slot].active = PROTO_FALSE;
            return PROTO_TRUE;
        }
        EdgeMesh.fetch_pump_args.m = &fs->mf;
        EdgeMesh.fetch_pump_args.t = &EDGE_CACHE_PROXY_CTX(work)->transport;
        EdgeMesh.fetch_pump_args.now_ms = now;
        EdgeMesh.fetch_pump(edge_mesh_work);
        EdgeMeshStatus ms = EdgeMesh.status;
        if (ms == EDGE_MESH_STATUS_PENDING)
        {
            return PROTO_TRUE; // still querying this sibling
        }
        proto_bool served = (ms == EDGE_MESH_STATUS_HIT) && mesh_store_and_serve(work, slot, fs, now);
        EdgeMesh.fetch_end_args.m = &fs->mf;
        EdgeMesh.fetch_end_args.t = &EDGE_CACHE_PROXY_CTX(work)->transport;
        EdgeMesh.fetch_end(edge_mesh_work);
        if (served)
        {
            EDGE_CACHE_PROXY_CTX(work)->store.stats.mesh_hits++;
            fs->used = PROTO_FALSE;
            EDGE_CACHE_PROXY_CTX(work)->pending[slot].active = PROTO_FALSE;
            return PROTO_TRUE;
        }
        if (mesh_advance_or_origin(work, fs, now)) // try the next sibling, else begin the origin fetch
        {
            return PROTO_TRUE;
        }
        send_text(slot, 502, PROTOCORE_MIME_TEXT_PLAIN, "Bad Gateway"); // no sibling + origin start failed
        fs->used = PROTO_FALSE;
        EDGE_CACHE_PROXY_CTX(work)->pending[slot].active = PROTO_FALSE;
        return PROTO_TRUE;
    }
#endif

    const EdgeFetchTransport *tport = fs->transport; // the transport chosen for this fetch (plaintext or TLS)
    ConnPool.slot = slot;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPool.ok) // client vanished mid-fetch: abort
    {
        EdgeFetcher.end_args.f = &fs->f;
        EdgeFetcher.end_args.t = tport;
        EdgeFetcher.end(edge_fetch_work);
        fs->used = PROTO_FALSE;
        EDGE_CACHE_PROXY_CTX(work)->pending[slot].active = PROTO_FALSE;
        return PROTO_TRUE;
    }

    EdgeFetcher.pump_args.f = &fs->f;
    EdgeFetcher.pump_args.t = tport;
    EdgeFetcher.pump_args.now_ms = now;
    EdgeFetcher.pump(edge_fetch_work);
    EdgeFetchStatus st = EdgeFetcher.status;
    if (st == EDGE_FETCH_STATUS_PENDING)
    {
        return PROTO_TRUE; // still receiving; owns the slot
    }

    if (st == EDGE_FETCH_STATUS_DONE)
    {
        on_fetch_done(work, slot, fs, now);
    }
    else if (st == EDGE_FETCH_STATUS_FAILED && fs->revalidate && fs->reval_entry)
    {
        serve_hit(work, slot, fs->reval_entry, now, "STALE"); // stale-if-error: serve the last good copy
    }
    else // FAILED miss / OVERSIZE
    {
        send_text(slot, 502, PROTOCORE_MIME_TEXT_PLAIN, "Bad Gateway");
    }
    EdgeFetcher.end_args.f = &fs->f;
    EdgeFetcher.end_args.t = tport;
    EdgeFetcher.end(edge_fetch_work);
    fs->used = PROTO_FALSE;
    EDGE_CACHE_PROXY_CTX(work)->pending[slot].active = PROTO_FALSE;
    return PROTO_TRUE;
}

#if PROTOCORE_ENABLE_EDGE_MESH
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
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_edge_cache_proxy_span();

    MeshLookupCtx *lc = (MeshLookupCtx *)ctx;
    size_t nl = str.len(name, MAX_KEY_LEN);
    const char *p = lc->blob;
    while (*p)
    {
        const char *rs = str.find(p, sizeof(EDGE_CACHE_PROXY_CTX(work)->mesh_hdrs) - (size_t)(p - lc->blob), "\x1e",
                                  sizeof("\x1e"), PROTO_FALSE);
        if (!rs)
        {
            break;
        }
        const char *us = str.find(rs + 1, sizeof(EDGE_CACHE_PROXY_CTX(work)->mesh_hdrs) - (size_t)(rs + 1 - lc->blob),
                                  "\x1f", sizeof("\x1f"), PROTO_FALSE);
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
            mem.cpy(lc->valbuf, rs + 1, vl);
            lc->valbuf[vl] = '\0';
            return lc->valbuf;
        }
        p = us + 1;
    }
    return NULL;
}

static MeshConn *mesh_conn_by_slot(uint8_t *restrict work, uint8_t slot)
{
    for (int i = 0; i < PROTOCORE_MESH_MAX_CONNS; i++)
    {
        if (EDGE_CACHE_PROXY_CTX(work)->mesh_conns[i].active &&
            EDGE_CACHE_PROXY_CTX(work)->mesh_conns[i].conn_slot == slot)
        {
            return &EDGE_CACHE_PROXY_CTX(work)->mesh_conns[i];
        }
    }
    return NULL;
}

// Build the response for a parsed request into mc->outbuf: a HIT carrying a fresh local variant, else a MISS.
static void mesh_answer(uint8_t *restrict work, MeshConn *mc, const uint8_t digest[32], const char *canon, uint32_t now)
{
    proto_bool hit = PROTO_FALSE;
    uint8_t verify[32];
    EdgeCache.key_digest_args.digest_work = EDGE_CACHE_PROXY_CTX(work)->store.digest_work;
    EdgeCache.key_digest_args.canon = canon;
    EdgeCache.key_digest_args.len = str.len(canon, PROTOCORE_EDGE_KEY_MAX);
    EdgeCache.key_digest_args.digest = verify;
    EdgeCache.key_digest(edge_cache_work);
    if (mem.cmp(verify, digest, 32) == 0) // integrity: the canonical key must hash to the advertised digest
    {
        MeshLookupCtx lc;
        lc.blob = EDGE_CACHE_PROXY_CTX(work)->mesh_hdrs;
        EdgeCache.store_find_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
        EdgeCache.store_find_args.canon = canon;
        EdgeCache.store_find_args.lookup = mesh_hdr_lookup;
        EdgeCache.store_find_args.ctx = &lc;
        EdgeCache.store_find_args.now_ms = now;
        EdgeCache.store_find(edge_cache_work);
        EdgeEntry *e = EdgeCache.entry;
        EdgeCache.entry_fresh_args.e = e;
        EdgeCache.entry_fresh_args.now_ms = now;
        EdgeCache.entry_fresh(edge_cache_work);
        if (e && EdgeCache.ok)
        {
            EdgeCache.current_age_args.initial_age = e->initial_age;
            EdgeCache.current_age_args.insert_ms = e->insert_ms;
            EdgeCache.current_age_args.now_ms = now;
            EdgeCache.current_age(edge_cache_work);
            long age = EdgeCache.secs;
            if (age < 0)
            {
                age = 0;
            }
            // Serialize the entry directly after the 6-byte response header to avoid a large stack temp.
            EdgeMesh.serialize_entry_args.e = e;
            EdgeMesh.serialize_entry_args.current_age = age;
            EdgeMesh.serialize_entry_args.out = mc->outbuf + 6;
            EdgeMesh.serialize_entry_args.cap = sizeof(mc->outbuf) - 6;
            EdgeMesh.serialize_entry(edge_mesh_work);
            size_t fn = EdgeMesh.n;
            if (fn > 0 && fn <= 0xFFFFu)
            {
                mc->outbuf[0] = PROTOCORE_EDGE_MESH_MAGIC0;
                mc->outbuf[1] = PROTOCORE_EDGE_MESH_MAGIC1;
                mc->outbuf[2] = PROTOCORE_EDGE_MESH_VERSION;
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
        EdgeMesh.build_response_args.hit = PROTO_FALSE;
        EdgeMesh.build_response_args.entry = NULL;
        EdgeMesh.build_response_args.entry_len = 0;
        EdgeMesh.build_response_args.out = mc->outbuf;
        EdgeMesh.build_response_args.cap = sizeof(mc->outbuf);
        EdgeMesh.build_response(edge_mesh_work);
        mc->out_len = (uint16_t)EdgeMesh.n;
    }
    mc->out_off = 0;
    mc->responded = PROTO_TRUE;
}

static void mesh_serve_end(MeshConn *mc)
{
    mc->active = PROTO_FALSE;
    ConnPool.slot = mc->conn_slot;
    ConnPool.close(protocore_conn_pool_span());
}

// Drive one serve connection: accumulate the request, answer it, then page the response out with backpressure.
static void mesh_serve_pump(uint8_t *restrict work, MeshConn *mc)
{
    uint8_t slot = mc->conn_slot;
    if (!mc->responded)
    {
        ConnPool.slot = slot;
        ConnPool.available(protocore_conn_pool_span());
        if (ConnPool.n && mc->req_len < sizeof(mc->reqbuf))
        {
            ConnPool.slot = slot;
            ConnPool.io.buf = mc->reqbuf + mc->req_len;
            ConnPool.io.cap = sizeof(mc->reqbuf) - mc->req_len;
            ConnPool.read(protocore_conn_pool_span());
            mc->req_len += (uint16_t)ConnPool.n;
        }
        uint8_t digest[32];
        char canon[PROTOCORE_EDGE_KEY_MAX];
        EdgeMesh.parse_request_args.buf = mc->reqbuf;
        EdgeMesh.parse_request_args.len = mc->req_len;
        EdgeMesh.parse_request_args.digest_out = digest;
        EdgeMesh.parse_request_args.canon_out = canon;
        EdgeMesh.parse_request_args.canon_cap = sizeof(canon);
        EdgeMesh.parse_request_args.hdrs_out = EDGE_CACHE_PROXY_CTX(work)->mesh_hdrs;
        EdgeMesh.parse_request_args.hdrs_cap = sizeof(EDGE_CACHE_PROXY_CTX(work)->mesh_hdrs);
        EdgeMesh.parse_request(edge_mesh_work);
        EdgeMeshParse p = EdgeMesh.parse;
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
        mesh_answer(work, mc, digest, canon, Clock.ms);
    }
    while (mc->out_off < mc->out_len)
    {
        ConnPool.slot = slot;
        ConnPool.sndbuf(protocore_conn_pool_span());
        proto_u16 room = ConnPool.u16;
        if (room == 0)
        {
            return; // backpressure; retry next poll
        }
        uint16_t remaining = (uint16_t)(mc->out_len - mc->out_off);
        proto_u16 n = remaining < room ? remaining : room;
        ConnPool.slot = slot;
        ConnPool.io.data = mc->outbuf + mc->out_off;
        ConnPool.io.len = n;
        ConnPool.send(protocore_conn_pool_span());
        if (!ConnPool.ok)
        {
            return; // retry next poll
        }
        mc->out_off = (uint16_t)(mc->out_off + n);
    }
    // Whole response queued: flush it out, then dwell in CONN_CLOSING until the peer ACKs (a plain
    // Tcp.conn->close would RST and discard the response the peer has not read yet). Tcp.conn->send already
    // COPY'd the bytes into the TCP buffer and the graceful finalize does not call on_close, so free the
    // MeshConn now - the transport owns the drain from here.
    ConnPool.slot = slot;
    ConnPool.flush(protocore_conn_pool_span());
    ConnPool.slot = slot;
    ConnPool.begin_close(protocore_conn_pool_span());
    mc->active = PROTO_FALSE;
}

static void mesh_on_accept(uint8_t slot)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_edge_cache_proxy_span();

    for (int i = 0; i < PROTOCORE_MESH_MAX_CONNS; i++)
    {
        if (!EDGE_CACHE_PROXY_CTX(work)->mesh_conns[i].active)
        {
            MeshConn *mc = &EDGE_CACHE_PROXY_CTX(work)->mesh_conns[i];
            mc->active = PROTO_TRUE;
            mc->conn_slot = slot;
            mc->req_len = 0;
            mc->responded = PROTO_FALSE;
            mc->out_off = 0;
            mc->out_len = 0;
            return;
        }
    }
    ConnPool.slot = slot;
    ConnPool.close(protocore_conn_pool_span()); // no free serve slot
}

static void mesh_on_data(uint8_t slot)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_edge_cache_proxy_span();

    MeshConn *mc = mesh_conn_by_slot(work, slot);
    if (mc)
    {
        mesh_serve_pump(work, mc);
    }
}

static void mesh_on_poll(uint8_t slot)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_edge_cache_proxy_span();

    ConnPool.slot = slot;
    ConnPool.active(protocore_conn_pool_span());
    if (!ConnPool.ok)
    {
        return;
    }
    MeshConn *mc = mesh_conn_by_slot(work, slot);
    if (mc)
    {
        mesh_serve_pump(work, mc);
    }
}

static void mesh_on_close(uint8_t slot)
{
    // The signature belongs to whoever dispatches this, so the borrow comes from the
    // accessor rather than a parameter.
    uint8_t *restrict work = protocore_edge_cache_proxy_span();

    MeshConn *mc = mesh_conn_by_slot(work, slot);
    if (mc)
    {
        mc->active = PROTO_FALSE; // the transport owns the closing slot
    }
}

// Designated, so a member's position in the struct does not decide what it binds to. on_abort is
// unset: a null one falls back to on_close.
static const ProtoHandler s_mesh_handler = {
    .on_accept = mesh_on_accept, .on_data = mesh_on_data, .on_close = mesh_on_close, .on_poll = mesh_on_poll};
#endif // PROTOCORE_ENABLE_EDGE_MESH

// --- public API ----------------------------------------------------------------------------------

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_EDGE_PROXY_BORROW persistent PLAINTEXT bytes
} EdgeProxyOwnCtx;
static EdgeProxyOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_edge_cache_proxy_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_EDGE_PROXY_BORROW).buf;
    }
    return s_own.span;
}

static void edge_cache_proxy_enable(uint8_t *restrict work)
{
    EdgeCache.store_init_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
    EdgeCache.store_init(edge_cache_work);
    for (int i = 0; i < PROTOCORE_EDGE_FETCH_SLOTS; i++)
    {
        EDGE_CACHE_PROXY_CTX(work)->fetches[i].used = PROTO_FALSE;
        EDGE_CACHE_PROXY_CTX(work)->fetches[i].f.cid = -1;
    }
    for (int i = 0; i < MAX_CONNS; i++)
    {
        EDGE_CACHE_PROXY_CTX(work)->pending[i].active = PROTO_FALSE;
        EDGE_CACHE_PROXY_CTX(work)->serve[i].active = PROTO_FALSE;
        EDGE_CACHE_PROXY_CTX(work)->serve[i].entry = NULL;
#if PROTOCORE_ENABLE_RANGE
        EDGE_CACHE_PROXY_CTX(work)->range_hdr[i][0] = '\0';
#endif
    }
    EDGE_CACHE_PROXY_CTX(work)->transport.open = t_open;
    EDGE_CACHE_PROXY_CTX(work)->transport.connected = t_connected;
    EDGE_CACHE_PROXY_CTX(work)->transport.send = t_send;
    EDGE_CACHE_PROXY_CTX(work)->transport.read = t_read;
    EDGE_CACHE_PROXY_CTX(work)->transport.closed = t_closed;
    EDGE_CACHE_PROXY_CTX(work)->transport.close = t_close;
    EDGE_CACHE_PROXY_CTX(work)->transport.ctx = NULL;
#if PROTOCORE_ENABLE_EDGE_ORIGIN_TLS
    edge_tls()->transport_tls.open = t_tls_open;
    edge_tls()->transport_tls.connected = t_tls_connected;
    edge_tls()->transport_tls.send = t_tls_send;
    edge_tls()->transport_tls.read = t_tls_read;
    edge_tls()->transport_tls.closed = t_tls_closed;
    edge_tls()->transport_tls.close = t_tls_close;
    edge_tls()->transport_tls.ctx = NULL;
    edge_tls()->tls_cid = -1;
    edge_tls()->tls_peer_closed = PROTO_FALSE;
    edge_tls()->tls_ready = PROTO_FALSE;
#endif
#if PROTOCORE_ENABLE_DBM
    EDGE_CACHE_PROXY_CTX(work)->store.on_evict =
        EDGE_CACHE_PROXY_CTX(work)->l2 ? edge_on_evict : NULL; // re-arm write-back after edge_store_init
    EDGE_CACHE_PROXY_CTX(work)->store.evict_ctx = NULL;
#endif
#if PROTOCORE_ENABLE_EDGE_MESH
    for (int i = 0; i < PROTOCORE_MESH_MAX_CONNS; i++)
    {
        EDGE_CACHE_PROXY_CTX(work)->mesh_conns[i].active = PROTO_FALSE;
    }
#endif
    if (!EDGE_CACHE_PROXY_CTX(work)->registered)
    {
        use(edge_cache_mw);
        HttpV.edge_poll = edge_cache_poll;
        Http.set_edge_poll(protocore_http_span());
        EDGE_CACHE_PROXY_CTX(work)->registered = PROTO_TRUE;
    }
}

#if PROTOCORE_ENABLE_DBM
static void edge_cache_proxy_bind_sd(uint8_t *restrict work)
{
    struct protocore_dbm *dbm = EdgeProxy.bind_sd_args.dbm;

    EDGE_CACHE_PROXY_CTX(work)->l2 = dbm;
    EDGE_CACHE_PROXY_CTX(work)->store.on_evict = dbm ? edge_on_evict : NULL;
    EDGE_CACHE_PROXY_CTX(work)->store.evict_ctx = NULL;
}
#endif

static void edge_cache_proxy_map(uint8_t *restrict work)
{
    const char *path_prefix = EdgeProxy.map_args.path_prefix;
    const char *origin_base_url = EdgeProxy.map_args.origin_base_url;

    if (!path_prefix || !origin_base_url)
    {
        EdgeProxy.ok = PROTO_FALSE;
        return;
    }
    if (str.len(path_prefix, sizeof(EDGE_CACHE_PROXY_CTX(work)->maps[0].prefix)) >=
        sizeof(EDGE_CACHE_PROXY_CTX(work)->maps[0].prefix))
    {
        EdgeProxy.ok = PROTO_FALSE;
        return;
    }
    char host[PROTOCORE_EDGE_ORIGIN_URL_MAX];
    char ignore_path[256];
    HttpClient.target.url = origin_base_url;
    HttpClient.target.host = host;
    HttpClient.target.host_cap = sizeof(host);
    HttpClient.target.path = ignore_path;
    HttpClient.target.path_cap = sizeof(ignore_path);
    // parse_target_uri reads the caller's buffer and holds nothing, so it takes no borrow.
    HttpClient.parse_target_uri(NULL);
    if (!HttpClient.ok)
    {
        EdgeProxy.ok = PROTO_FALSE;
        return;
    }
    const proto_bool https = HttpClient.target.https;
    const uint16_t port = HttpClient.target.port;
#if !PROTOCORE_ENABLE_EDGE_ORIGIN_TLS
    if (https)
    {
        EdgeProxy.ok = PROTO_FALSE;
        return; // plaintext origins only unless PROTOCORE_ENABLE_EDGE_ORIGIN_TLS is set
    }
#endif
    for (int i = 0; i < PROTOCORE_EDGE_MAP_MAX; i++)
    {
        if (EDGE_CACHE_PROXY_CTX(work)->maps[i].used)
        {
            continue;
        }
        str.copy(EDGE_CACHE_PROXY_CTX(work)->maps[i].prefix, path_prefix,
                 sizeof(EDGE_CACHE_PROXY_CTX(work)->maps[i].prefix));
        str.copy(EDGE_CACHE_PROXY_CTX(work)->maps[i].origin_host, host,
                 sizeof(EDGE_CACHE_PROXY_CTX(work)->maps[i].origin_host));
        EDGE_CACHE_PROXY_CTX(work)->maps[i].origin_port = port;
        EDGE_CACHE_PROXY_CTX(work)->maps[i].https = https;
        EDGE_CACHE_PROXY_CTX(work)->maps[i].used = PROTO_TRUE;
        EdgeProxy.ok = PROTO_TRUE;
        return;
    }
    EdgeProxy.ok = PROTO_FALSE; // map table full
}

#if PROTOCORE_ENABLE_EDGE_ORIGIN_TLS
static void edge_cache_proxy_set_origin_ca(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *ca_pem = EdgeProxy.set_origin_ca_args.ca_pem;
    size_t len = EdgeProxy.set_origin_ca_args.len;

    protocore_tls_client_set_ca(ca_pem, len); // shared client-TLS trust store (also used by MQTTS/wss/HTTP client)
}
static void edge_cache_proxy_set_origin_pin(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *sha256 = EdgeProxy.set_origin_pin_args.sha256;

    protocore_tls_client_set_pin(sha256);
}
#endif

#if PROTOCORE_ENABLE_EDGE_MESH
static void edge_cache_proxy_add_peer(uint8_t *restrict work)
{
    const char *host = EdgeProxy.add_peer_args.host;
    uint16_t port = EdgeProxy.add_peer_args.port;

    if (!host)
    {
        EdgeProxy.ok = PROTO_FALSE;
        return;
    }
    size_t hl = str.len(host, PROTOCORE_MESH_HOST_MAX + 1);
    if (hl == 0 || hl >= PROTOCORE_MESH_HOST_MAX)
    {
        EdgeProxy.ok = PROTO_FALSE;
        return;
    }
    for (int i = 0; i < PROTOCORE_MESH_MAX_PEERS; i++)
    {
        if (!EDGE_CACHE_PROXY_CTX(work)->peers[i].used)
        {
            mem.cpy(EDGE_CACHE_PROXY_CTX(work)->peers[i].host, host, hl + 1);
            EDGE_CACHE_PROXY_CTX(work)->peers[i].port = port;
            EDGE_CACHE_PROXY_CTX(work)->peers[i].used = PROTO_TRUE;
            EdgeProxy.ok = PROTO_TRUE;
            return;
        }
    }
    EdgeProxy.ok = PROTO_FALSE; // peer table full
}

static void edge_cache_proxy_mesh_serve(uint8_t *restrict work)
{
    if (!EDGE_CACHE_PROXY_CTX(work)->mesh_registered)
    {
        SessionV.proto->proto = PROTO_MESH;
        SessionV.proto->h = &s_mesh_handler;
        SessionV.proto->add(protocore_session_span());
        EDGE_CACHE_PROXY_CTX(work)->mesh_registered = PROTO_TRUE;
    }
}
#endif // PROTOCORE_ENABLE_EDGE_MESH

static void edge_cache_proxy_reset(uint8_t *restrict work)
{
    EdgeCache.store_init_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
    EdgeCache.store_init(edge_cache_work);
#if PROTOCORE_ENABLE_DBM
    if (EDGE_CACHE_PROXY_CTX(work)->l2)
    {
        EdgeCacheSd.purge_all_args.db = EDGE_CACHE_PROXY_CTX(work)->l2;
        EdgeCacheSd.purge_all(edge_cache_sd_work);
        EDGE_CACHE_PROXY_CTX(work)->store.on_evict =
            edge_on_evict; // edge_store_init cleared it - re-arm the write-back hook
    }
#endif
    for (int i = 0; i < PROTOCORE_EDGE_MAP_MAX; i++)
    {
        EDGE_CACHE_PROXY_CTX(work)->maps[i].used = PROTO_FALSE;
    }
#if PROTOCORE_ENABLE_EDGE_MESH
    for (int i = 0; i < PROTOCORE_MESH_MAX_PEERS; i++)
    {
        EDGE_CACHE_PROXY_CTX(work)->peers[i].used = PROTO_FALSE;
    }
#endif
}

static void edge_cache_proxy_purge(uint8_t *restrict work)
{
    const char *canonical_key = EdgeProxy.purge_args.canonical_key;

    if (!canonical_key)
    {
        EdgeProxy.ok = PROTO_FALSE;
        return;
    }
    EdgeCache.store_purge_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
    EdgeCache.store_purge_args.canon = canonical_key;
    EdgeCache.store_purge(edge_cache_work);
    proto_bool purged = EdgeCache.count > 0;
#if PROTOCORE_ENABLE_DBM
    if (EDGE_CACHE_PROXY_CTX(work)->l2)
    {
        uint8_t digest[32];
        EdgeCache.key_digest_args.digest_work = EDGE_CACHE_PROXY_CTX(work)->store.digest_work;
        EdgeCache.key_digest_args.canon = canonical_key;
        EdgeCache.key_digest_args.len = str.len(canonical_key, PROTOCORE_EDGE_KEY_MAX);
        EdgeCache.key_digest_args.digest = digest;
        EdgeCache.key_digest(edge_cache_work);
        EdgeCacheSd.del_args.db = EDGE_CACHE_PROXY_CTX(work)->l2;
        EdgeCacheSd.del_args.digest = digest;
        EdgeCacheSd.del(edge_cache_sd_work);
        if (EdgeCacheSd.ok)
        {
            purged = PROTO_TRUE;
        }
    }
#endif
    EdgeProxy.ok = purged;
}

static void edge_cache_proxy_purge_prefix(uint8_t *restrict work)
{
    const char *path_prefix = EdgeProxy.purge_prefix_args.path_prefix;

    if (!path_prefix)
    {
        EdgeProxy.n = 0;
        return;
    }
    EdgeCache.store_purge_prefix_args.s = &EDGE_CACHE_PROXY_CTX(work)->store;
    EdgeCache.store_purge_prefix_args.prefix = path_prefix;
    EdgeCache.store_purge_prefix(edge_cache_work);
    uint32_t n = EdgeCache.count;
#if PROTOCORE_ENABLE_DBM
    if (EDGE_CACHE_PROXY_CTX(work)->l2)
    {
        EdgeCacheSd.purge_prefix_args.db = EDGE_CACHE_PROXY_CTX(work)->l2;
        EdgeCacheSd.purge_prefix_args.path_prefix = path_prefix;
        EdgeCacheSd.purge_prefix_args.scratch = EDGE_CACHE_PROXY_CTX(work)->sd_buf;
        EdgeCacheSd.purge_prefix_args.scratch_cap = sizeof(EDGE_CACHE_PROXY_CTX(work)->sd_buf);
        EdgeCacheSd.purge_prefix(edge_cache_sd_work);
        n += EdgeCacheSd.count;
    }
#endif
    EdgeProxy.n = n;
}

static void edge_cache_proxy_stats(uint8_t *restrict work)
{
    EdgeCacheStats *out = EdgeProxy.stats_args.out;

    if (out)
    {
        *out = EDGE_CACHE_PROXY_CTX(work)->store.stats;
    }
}

EdgeProxyNs EdgeProxy = {.enable = edge_cache_proxy_enable,
                         .map = edge_cache_proxy_map,
#if PROTOCORE_ENABLE_EDGE_ORIGIN_TLS
                         .set_origin_ca = edge_cache_proxy_set_origin_ca,
#endif
#if PROTOCORE_ENABLE_EDGE_ORIGIN_TLS
                         .set_origin_pin = edge_cache_proxy_set_origin_pin,
#endif
#if PROTOCORE_ENABLE_DBM
                         .bind_sd = edge_cache_proxy_bind_sd,
#endif
#if PROTOCORE_ENABLE_EDGE_MESH
                         .add_peer = edge_cache_proxy_add_peer,
#endif
#if PROTOCORE_ENABLE_EDGE_MESH
                         .mesh_serve = edge_cache_proxy_mesh_serve,
#endif
                         .reset = edge_cache_proxy_reset,
                         .purge = edge_cache_proxy_purge,
                         .purge_prefix = edge_cache_proxy_purge_prefix,
                         .stats = edge_cache_proxy_stats};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EDGE_CACHE
