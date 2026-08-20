// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_server.c
 * @brief HTTP/3 server glue - implementation. See protocore_quic_server.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t quic_packet_work[16]; // the borrow an entry takes; QuicPacket never reads it

static uint8_t quic_tp_work[16]; // the borrow an entry takes; QuicTp never reads it

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

#if PROTOCORE_ENABLE_HTTP3

#include "mmgr/protomem/protomem.h"
#include "network_drivers/presentation/http/http3/quic_server/quic_server.h"

#include "mmgr/plaintext/plaintext.h" // the two engines' byte spans
#include "mmgr/ring.h"                // protocore_atomic
#include "mmgr/secure/secure.h"       // the QUIC context span is key material
#include "network_drivers/presentation/http/http3/quic_packet/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_tls/quic_tls.h" // QuicTlsConfig
#include "network_drivers/presentation/http/http3/quic_tp/quic_tp.h"

#include "network_drivers/transport/udp/server/server.h" // UdpListener: the bound port datagrams arrive on

// The pool (QuicConn + H3Conn per slot) and the ingest ring are large, so on a PSRAM board they can
// be moved to external RAM (like the HTTP/2 pool). Default is internal DRAM; a build that overflows
// sets PROTOCORE_QUIC_SERVER_IN_PSRAM=1 on a core built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY.
#ifndef PROTOCORE_QUIC_SERVER_IN_PSRAM
#define PROTOCORE_QUIC_SERVER_IN_PSRAM 0
#endif
#ifndef PROTOCORE_QUIC_SERVER_ACK_DRAM
#define PROTOCORE_QUIC_SERVER_ACK_DRAM 0 ///< consciously accept the pool in internal DRAM (roomy S3 / P4)
#endif
// The QuicConn + H3Conn pool plus the ingest ring are tens of KB; on a device that is a deliberate
// footprint choice. Fail fast (like PROTOCORE_ENABLE_SSH_ZLIB / PROTOCORE_ENABLE_HTTP2) so it is not an
// accidental DRAM overflow: move the pool to PSRAM, or acknowledge the internal-DRAM cost.
#if PROTOCORE_HAS_BOUNDED_DRAM && !PROTOCORE_QUIC_SERVER_IN_PSRAM && !PROTOCORE_QUIC_SERVER_ACK_DRAM
#error                                                                                                                 \
    "ProtoCore: PROTOCORE_ENABLE_HTTP3 - the protocore_quic_server QuicConn+H3Conn pool + ingest ring are tens of KB. Set PROTOCORE_QUIC_SERVER_IN_PSRAM=1 on a PSRAM board (S3 / P4 / WROVER built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y, tools/psram/README.md), OR set PROTOCORE_QUIC_SERVER_ACK_DRAM=1 to accept the internal-DRAM cost (fits a small pool on a roomy chip)."
#endif
#if PROTOCORE_QUIC_SERVER_IN_PSRAM && PROTOCORE_HAS_PSRAM
#include <esp_attr.h>
#if defined(EXT_RAM_BSS_ATTR)
#define PROTOCORE_QUIC_POOL_ATTR EXT_RAM_BSS_ATTR
#elif defined(EXT_RAM_ATTR)
#define PROTOCORE_QUIC_POOL_ATTR EXT_RAM_ATTR
#else
#define PROTOCORE_QUIC_POOL_ATTR
#endif
#else
#define PROTOCORE_QUIC_POOL_ATTR
#endif

// One buffered inbound datagram (payload + the peer it arrived from).
typedef struct
{
    uint8_t data[PROTOCORE_QUIC_MAX_DATAGRAM];
    uint16_t len;
    char ip[16];
    uint16_t port;
} QuicIngest;

// One pool slot: a QUIC connection, its HTTP/3 engine, and the peer to reply to.
typedef struct
{
    proto_bool used;
    uint32_t id;  ///< stable handle for protocore_quic_server_respond()
    uint8_t *qc;  ///< PROTOCORE_QUIC_CONN_CTX_BORROW secure bytes: the QUIC connection
    uint8_t *qcb; ///< PROTOCORE_QUIC_CONN_BORROW plaintext bytes it owes its streams
    uint8_t *h3;  ///< PROTOCORE_H3_CONN_BORROW plaintext bytes: the HTTP/3 connection
    char peer_ip[16];
    uint16_t peer_port;
    uint32_t last_ms; ///< protocore_millis() of the last datagram received (idle-reaping clock)
} QuicSlot;

/**
 * @brief The server's control state: what the ingest ring, the config and the identity counter need.
 *
 * @var QuicServerCtx::ring_head   producer (udp / ingest) advances
 * @var QuicServerCtx::ring_tail   consumer (poll) advances
 * @var QuicServerCtx::cfg         the installed certificate, key and randomness source
 * @var QuicServerCtx::on_request  what a completed request is delivered to
 * @var QuicServerCtx::app         the opaque pointer that callback is given back
 * @var QuicServerCtx::port        the bound UDP port
 * @var QuicServerCtx::running     the server is up
 * @var QuicServerCtx::next_id     set to 1 by begin; never handed out as 0
 */
typedef struct
{
    _Atomic size_t ring_head;
    _Atomic size_t ring_tail;
    QuicServerConfig cfg;
    QuicServerRequestFn on_request;
    void *app;
    uint16_t port;
    proto_bool running;
    uint32_t next_id;
} QuicServerCtx;

// The caller's borrow, split: the control state, then the connection pool, then the ingest ring.
// One pointer arrives and every region is that pointer plus a compile-time offset, so the assert
// below proves the span covers them before anything runs.
#define QSRV_OFF_CTX 0u
#define QSRV_OFF_POOL (QSRV_OFF_CTX + sizeof(QuicServerCtx))
#define QSRV_OFF_RING (QSRV_OFF_POOL + (size_t)PROTOCORE_QUIC_MAX_CONNS * sizeof(QuicSlot))
static_assert(QSRV_OFF_RING + (size_t)PROTOCORE_QUIC_INGEST_RING * sizeof(QuicIngest) <= PROTOCORE_QUIC_SERVER_BORROW,
              "PROTOCORE_QUIC_SERVER_BORROW is short of the server - raise it in protocore_config.h, which"
              " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(QSRV_OFF_CTX % _Alignof(QuicServerCtx) == 0,
              "QSRV_OFF_CTX is not a multiple of alignof(QuicServerCtx) - QSRV_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");
static_assert(QSRV_OFF_POOL % _Alignof(QuicSlot) == 0,
              "QSRV_OFF_POOL is not a multiple of alignof(QuicSlot) - QSRV_POOL() would return a misaligned "
              "pointer; pad the region ahead of it");
static_assert(QSRV_OFF_RING % _Alignof(QuicIngest) == 0,
              "QSRV_OFF_RING is not a multiple of alignof(QuicIngest) - QSRV_RING() would return a misaligned "
              "pointer; pad the region ahead of it");

// The regions, at their offsets in the caller's borrow.
#define QSRV_CTX(w) ((QuicServerCtx *)(void *)((w) + QSRV_OFF_CTX))
#define QSRV_POOL(w) ((QuicSlot *)(void *)((w) + QSRV_OFF_POOL))
#define QSRV_RING(w) ((QuicIngest *)(void *)((w) + QSRV_OFF_RING))

// The one owned instance, private to this TU: the pointer to the bytes this server took for
// itself. The two callbacks below carry the transport's signature and reach it through here.
static uint8_t *s_span;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_quic_server_span(void)
{
    if (s_span == NULL)
    {
        s_span = protocore_plaintext_persist_span(PROTOCORE_QUIC_SERVER_BORROW).buf;
    }
    return s_span;
}

// Copy @p src into @p dst, stopping at the NUL or one short of @p cap, and terminate.
static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    while (src[n] && n + 1 < cap)
    {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
}

static proto_bool cid_eq(const uint8_t *a, uint8_t alen, const uint8_t *b, uint8_t blen)
{
    return alen == blen && mem.cmp(a, b, alen) == 0;
}

static void server_send(uint8_t *restrict work, const char *ip, uint16_t port, const uint8_t *data, size_t len)
{
    protocore_ip dst = {PROTOCORE_IP_NONE, {0}};
    IpV.args.text = ip;
    IpV.args.out = &dst;
    Ip.parse(ip_work);
    if (IpV.ok)
    {
        UdpListenerV.port = QSRV_CTX(work)->port;
        UdpListenerV.send_args.dst = &dst;
        UdpListenerV.send_args.dst_port = port;
        UdpListenerV.send_args.data = data;
        UdpListenerV.send_args.len = len;
        UdpListener.sendto(protocore_udp_listener_span());
    }
}

// --- ingest ring (SPSC: one producer fills, protocore_quic_server_poll consumes) ------------------------
static proto_bool ring_push(uint8_t *restrict work, const uint8_t *dg, size_t len, const char *ip, uint16_t port)
{
    if (len == 0 || len > PROTOCORE_QUIC_MAX_DATAGRAM)
    {
        return PROTO_FALSE;
    }
    size_t head = QSRV_CTX(work)->ring_head;
    size_t next = (head + 1) % PROTOCORE_QUIC_INGEST_RING;
    if (next == (size_t)QSRV_CTX(work)->ring_tail)
    {
        return PROTO_FALSE; // ring full: drop (QUIC recovers via retransmission)
    }
    QuicIngest *e = &QSRV_RING(work)[head];
    mem.cpy(e->data, dg, len);
    e->len = (uint16_t)len;
    copy_str(e->ip, sizeof e->ip, ip);
    e->port = port;
    QSRV_CTX(work)->ring_head = next; // publish after the record is fully written
    return PROTO_TRUE;
}

static proto_bool ring_pop(uint8_t *restrict work, QuicIngest *out)
{
    size_t tail = QSRV_CTX(work)->ring_tail;
    if (tail == (size_t)QSRV_CTX(work)->ring_head)
    {
        return PROTO_FALSE;
    }
    *out = QSRV_RING(work)[tail];
    QSRV_CTX(work)->ring_tail = (tail + 1) % PROTOCORE_QUIC_INGEST_RING;
    return PROTO_TRUE;
}

// --- slot pool --------------------------------------------------------------------------------
static QuicSlot *slot_by_id(uint8_t *restrict work, uint32_t id)
{
    for (uint8_t i = 0; i < PROTOCORE_QUIC_MAX_CONNS; i++)
    {
        if (QSRV_POOL(work)[i].used && QSRV_POOL(work)[i].id == id)
        {
            return &QSRV_POOL(work)[i];
        }
    }
    return NULL;
}

static QuicSlot *alloc_slot(uint8_t *restrict work)
{
    for (uint8_t i = 0; i < PROTOCORE_QUIC_MAX_CONNS; i++)
    {
        if (!QSRV_POOL(work)[i].used)
        {
            QuicSlot *s = &QSRV_POOL(work)[i];
            // Both engines' borrows are bound to the slot, not to the connection on it: they are
            // carried across the zero, or every claim would draw a fresh one from an end that is
            // never given back. Each init wipes the bytes it just re-reached.
            uint8_t *qc_ctx = s->qc;
            uint8_t *qc_bytes = s->qcb;
            uint8_t *h3_bytes = s->h3;
            mem.set(s, 0, sizeof *s);
            s->qc = qc_ctx;
            s->qcb = qc_bytes;
            s->h3 = h3_bytes;
            s->used = PROTO_TRUE;
            s->id = QSRV_CTX(work)->next_id++;
            if (QSRV_CTX(work)->next_id == 0)
            // true branch (wrap) is unreachable - it needs 2^32 allocations in
            // one process lifetime (next_id is never reset except by begin()),
            // which no host test can drive
            {
                QSRV_CTX(work)->next_id = 1;
            }
            return s;
        }
    }
    return NULL;
}

// The HTTP/3 engine surfaces a completed request here; forward it to the application by conn id.
static void protocore_h3_on_request(void *app, uint8_t * /*h3*/, uint64_t stream_id, const char *method,
                                    const char *path, const char *authority, const uint8_t *body, size_t body_len)
{
    uint8_t *restrict work = protocore_quic_server_span();
    QuicSlot *s = (QuicSlot *)app;
    if (QSRV_CTX(work)->on_request)
    {
        QSRV_CTX(work)->on_request(QSRV_CTX(work)->app, s->id, stream_id, method, path, authority, body, body_len);
    }
}

// Open a connection for a client's first Initial packet.
static QuicSlot *open_conn(uint8_t *restrict work, const QuicLongHeader *lh, const char *ip, uint16_t port)
{
    QuicSlot *s = alloc_slot(work);
    if (!s)
    {
        return NULL;
    }

    QuicTlsConfig tc;
    mem.set(&tc, 0, sizeof tc);
    tc.cert_der = QSRV_CTX(work)->cfg.cert_der;
    tc.cert_len = QSRV_CTX(work)->cfg.cert_len;
    mem.cpy(tc.ed25519_seed, QSRV_CTX(work)->cfg.ed25519_seed, sizeof tc.ed25519_seed);
    QuicTpV.defaults_args.tp = &tc.params;
    QuicTp.defaults(quic_tp_work);
    // A real HTTP/3 endpoint must advertise flow-control room, or every request stream (and the
    // client's control / QPACK streams) is blocked - the RFC 9000 sec 18.2 defaults are all zero.
    tc.params.initial_max_data = 1048576;
    tc.params.initial_max_sd_bidi_remote = 262144; // client-initiated request streams
    tc.params.initial_max_sd_uni = 262144;         // client control / QPACK encoder+decoder streams
    tc.params.initial_max_streams_bidi = PROTOCORE_H3_MAX_STREAMS;
    tc.params.initial_max_streams_uni = PROTOCORE_H3_MAX_STREAMS;
    tc.params.max_idle_timeout = PROTOCORE_QUIC_IDLE_MS; // both ends reclaim the connection after this idle
    QSRV_CTX(work)->cfg.rng(tc.ephemeral_priv, sizeof tc.ephemeral_priv);
    QSRV_CTX(work)->cfg.rng(tc.random, sizeof tc.random);
#if PROTOCORE_ENABLE_PQC_KEX
    QSRV_CTX(work)->cfg.rng(tc.mlkem_m, sizeof tc.mlkem_m); // fresh ML-KEM Encaps randomness per handshake
#endif

    uint8_t our_scid[PROTOCORE_QUIC_SCID_LEN];
    QSRV_CTX(work)->cfg.rng(our_scid, sizeof our_scid);

    // The slot's spans, taken once from each pool's persistent end and reused by every connection
    // this slot carries. The context span is secure because the TLS handshake in it is key material.
    if (s->qc == NULL)
    {
        protocore_span c = protocore_secure_persist_span(PROTOCORE_QUIC_CONN_CTX_BORROW);
        protocore_span b = protocore_plaintext_persist_span(PROTOCORE_QUIC_CONN_BORROW);
        protocore_span h = protocore_plaintext_persist_span(PROTOCORE_H3_CONN_BORROW);
        if (!span.ok(c) || !span.ok(b) || !span.ok(h))
        {
            return NULL; // no bytes to run out of; the slot opens no connection
        }
        s->qc = c.buf;
        s->qcb = b.buf;
        s->h3 = h.buf;
    }

    QuicConnCallbacks cb;
    mem.set(&cb, 0, sizeof cb); // H3Conn.init installs the real callbacks
    QuicConnV.bind.b = s->qcb;
    QuicConnV.cb = cb;
    QuicConnV.init_args.cfg = &tc;
    QuicConnV.init_args.odcid = lh->dcid;
    QuicConnV.init_args.odcid_len = lh->dcid_len;
    QuicConnV.init_args.peer_scid = lh->scid;
    QuicConnV.init_args.peer_scid_len = lh->scid_len;
    QuicConnV.init_args.our_scid = our_scid;
    QuicConnV.init_args.our_scid_len = PROTOCORE_QUIC_SCID_LEN;
    QuicConn.init(s->qc);

    H3ConnV.bind.qc = s->qc;
    H3ConnV.app_args.on_request = protocore_h3_on_request;
    H3ConnV.app_args.app = s;
    H3Conn.init(s->h3);

    copy_str(s->peer_ip, sizeof s->peer_ip, ip);
    s->peer_port = port;
    return s; // last_ms is set by the poll that received this datagram
}

// HttpRoute a datagram to its connection by Destination Connection ID. Sets *is_initial when it is an
// unmatched Initial (the caller opens a new connection) and copies the parsed long header out.
static QuicSlot *route(uint8_t *restrict work, const uint8_t *dg, size_t len, proto_bool *is_initial,
                       QuicLongHeader *lh_out)
{
    *is_initial = PROTO_FALSE;
    if (len < 1)
    // only the true branch is unreachable - route(work)'s sole call site is
    // protocore_quic_server_poll, fed by ring_pop(work) from a ring that ring_push(work) (both
    // ingest paths) already refuses to fill with len==0, so len>=1 always holds here
    {
        return NULL;
    }
    QuicPacketV.is_long_header_args.first = dg[0];
    QuicPacket.is_long_header(quic_packet_work);
    if (QuicPacketV.ok)
    {
        QuicPacketV.parse_long_header_args.buf = dg;
        QuicPacketV.parse_long_header_args.len = len;
        QuicPacketV.parse_long_header_args.out = lh_out;
        QuicPacket.parse_long_header(quic_packet_work);
        if (!QuicPacketV.ok)
        {
            return NULL;
        }
        for (uint8_t i = 0; i < PROTOCORE_QUIC_MAX_CONNS; i++)
        {
            QuicSlot *s = &QSRV_POOL(work)[i];
            if (!s->used)
            {
                continue;
            }
            QuicConnV.owns_args.dcid = lh_out->dcid;
            QuicConnV.owns_args.dcid_len = lh_out->dcid_len;
            QuicConn.owns(s->qc);
            if (QuicConnV.ok)
            {
                return s;
            }
        }
        if (lh_out->version == QUIC_VERSION_1 && lh_out->type == QUIC_LP_INITIAL)
        {
            *is_initial = PROTO_TRUE;
        }
        return NULL;
    }
    // Short header (1-RTT): the DCID is our chosen SCID, whose length only we know.
    if (len < (size_t)1 + PROTOCORE_QUIC_SCID_LEN)
    {
        return NULL;
    }
    for (uint8_t i = 0; i < PROTOCORE_QUIC_MAX_CONNS; i++)
    {
        QuicSlot *s = &QSRV_POOL(work)[i];
        if (!s->used)
        {
            continue;
        }
        // A short header carries no length, so the id is read at the one length this server chooses.
        QuicConnV.owns_args.dcid = dg + 1;
        QuicConnV.owns_args.dcid_len = PROTOCORE_QUIC_SCID_LEN;
        QuicConn.owns(s->qc);
        if (QuicConnV.ok)
        {
            return s;
        }
    }
    return NULL;
}

static void flush_and_reap(uint8_t *restrict work, uint32_t now_ms)
{
    uint8_t out[PROTOCORE_QUIC_MAX_DATAGRAM];
    for (uint8_t i = 0; i < PROTOCORE_QUIC_MAX_CONNS; i++)
    {
        QuicSlot *s = &QSRV_POOL(work)[i];
        if (!s->used)
        {
            continue;
        }
        QuicConnV.bind.b = s->qcb;
        QuicConnV.timeout_args.now_ms = now_ms;
        QuicConn.on_timeout(s->qc); // retransmit a lost handshake flight (PTO)
        // Drained: send is called until it reports nothing left, so the call is the condition.
        for (;;)
        {
            QuicConnV.send_args.out = out;
            QuicConnV.send_args.cap = sizeof out;
            QuicConn.send(s->qc);
            if (QuicConnV.n == 0)
            {
                break;
            }
            server_send(work, s->peer_ip, s->peer_port, out, QuicConnV.n);
        }
        // Reap a closed connection, or one idle past the timeout (wrap-safe delta) so a client that
        // never closes cannot leak the fixed pool.
        QuicConn.is_closed(s->qc);
        if (QuicConnV.closed || (uint32_t)(now_ms - s->last_ms) >= PROTOCORE_QUIC_IDLE_MS)
        {
            s->used = PROTO_FALSE;
        }
    }
}

static void udp_ingest_cb(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *ctx)
{
    (void)ctx;
    char ip[16];
    uint16_t port = 0;
    UdpListenerV.peer_args.peer = peer;
    UdpListenerV.peer_args.ip_out = ip;
    UdpListenerV.peer_args.ip_cap = sizeof ip;
    UdpListenerV.peer_args.port_out = &port;
    UdpListener.peer_addr(protocore_udp_listener_span());
    if (!UdpListenerV.ok)
    {
        return;
    }
    (void)ring_push(protocore_quic_server_span(), data, len, ip, port);
}

static void begin(uint8_t *restrict work)
{
    const QuicServerConfig *cfg = QuicServer.begin_args.cfg;
    QuicServer.ok = PROTO_FALSE;
    if (!cfg || !cfg->rng)
    {
        return;
    }
    QSRV_CTX(work)->cfg = *cfg;
    QSRV_CTX(work)->on_request = QuicServer.begin_args.on_request;
    QSRV_CTX(work)->app = QuicServer.begin_args.app;
    QSRV_CTX(work)->port = QuicServer.begin_args.port ? QuicServer.begin_args.port : PROTOCORE_HTTP3_PORT;
    for (uint8_t i = 0; i < PROTOCORE_QUIC_MAX_CONNS; i++)
    {
        QSRV_POOL(work)[i].used = PROTO_FALSE;
    }
    QSRV_CTX(work)->ring_head = 0;
    QSRV_CTX(work)->ring_tail = 0;
    QSRV_CTX(work)->next_id = 1;
    QSRV_CTX(work)->running = PROTO_TRUE;
    UdpListenerV.port = QSRV_CTX(work)->port;
    UdpListenerV.bind.handler = udp_ingest_cb;
    UdpListenerV.bind.handler_ctx = NULL;
    UdpListener.listen(protocore_udp_listener_span());
    QuicServer.ok = UdpListenerV.ok;
}

static void poll(uint8_t *restrict work)
{
    const uint32_t now_ms = QuicServer.now_ms;
    if (!QSRV_CTX(work)->running)
    {
        return;
    }
    QuicIngest ig;
    while (ring_pop(work, &ig))
    {
        proto_bool is_initial = PROTO_FALSE;
        QuicLongHeader lh;
        QuicSlot *s = route(work, ig.data, ig.len, &is_initial, &lh);
        if (!s && is_initial)
        {
            s = open_conn(work, &lh, ig.ip, ig.port);
        }
        if (!s)
        {
            continue;
        }
        s->last_ms = now_ms; // liveness for idle reaping
        QuicConnV.bind.b = s->qcb;
        QuicConnV.recv_args.datagram = ig.data;
        QuicConnV.recv_args.len = ig.len;
        QuicConn.recv(s->qc);
    }
    flush_and_reap(work, now_ms);
}

static void respond(uint8_t *restrict work)
{
    QuicSlot *s = slot_by_id(work, QuicServer.stream.conn_id);
    if (!s)
    {
        QuicServer.ok = PROTO_FALSE;
        return;
    }
    H3ConnV.bind.qc = s->qc;
    H3ConnV.respond_args.stream_id = QuicServer.stream.stream_id;
    H3ConnV.respond_args.status = QuicServer.resp.status;
    H3ConnV.respond_args.content_type = QuicServer.resp.content_type;
    H3ConnV.respond_args.body = QuicServer.resp.body;
    H3ConnV.respond_args.body_len = QuicServer.resp.body_len;
    H3Conn.respond(s->h3);
    QuicServer.ok = H3ConnV.ok;
}

static void active_conns(uint8_t *restrict work)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < PROTOCORE_QUIC_MAX_CONNS; i++)
    {
        if (QSRV_POOL(work)[i].used)
        {
            n++;
        }
    }
    QuicServer.u8 = n;
}

static void stop(uint8_t *restrict work)
{
    UdpListenerV.port = QSRV_CTX(work)->port;
    UdpListener.close(protocore_udp_listener_span()); // drop the bind first: nothing more reaches the ring
    QSRV_CTX(work)->running = PROTO_FALSE;
    for (uint8_t i = 0; i < PROTOCORE_QUIC_MAX_CONNS; i++)
    {
        QSRV_POOL(work)[i].used = PROTO_FALSE;
    }
    QSRV_CTX(work)->ring_head = 0;
    QSRV_CTX(work)->ring_tail = 0;
}

// The HTTP/3 bridge installs this; its shape is the seam's, not this module's.
proto_bool protocore_quic_server_respond(uint32_t conn_id, uint64_t stream_id, int status, const char *content_type,
                                         const uint8_t *body, size_t body_len)
{
    QuicServer.stream.conn_id = conn_id;
    QuicServer.stream.stream_id = stream_id;
    QuicServer.resp.status = status;
    QuicServer.resp.content_type = content_type;
    QuicServer.resp.body = body;
    QuicServer.resp.body_len = body_len;
    respond(protocore_quic_server_span());
    return QuicServer.ok;
}

// Designated, so a member's position in the struct does not decide what it binds to.
QuicServerNs QuicServer = {
    .begin = begin, .poll = poll, .respond = respond, .active_conns = active_conns, .stop = stop};

#endif // PROTOCORE_ENABLE_HTTP3
