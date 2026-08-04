// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_quic_server.c
 * @brief HTTP/3 server glue - implementation. See pc_quic_server.h.
 */

#include "network_drivers/presentation/http/http3/quic_server.h"

#if PC_ENABLE_HTTP3

#include "network_drivers/presentation/http/http3/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_tp.h"
#include "shared_primitives/ring.h" // pc_atomic
#include <string.h>

#if PROTOCORE_HOT
#include "network_drivers/transport/udp.h"
#endif

// The pool (QuicConn + H3Conn per slot) and the ingest ring are large, so on a PSRAM board they can
// be moved to external RAM (like the HTTP/2 pool). Default is internal DRAM; a build that overflows
// sets PC_QUIC_SERVER_IN_PSRAM=1 on a core built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY.
#ifndef PC_QUIC_SERVER_IN_PSRAM
#define PC_QUIC_SERVER_IN_PSRAM 0
#endif
#ifndef PC_QUIC_SERVER_ACK_DRAM
#define PC_QUIC_SERVER_ACK_DRAM 0 ///< consciously accept the pool in internal DRAM (roomy S3 / P4)
#endif
// The QuicConn + H3Conn pool plus the ingest ring are tens of KB; on a device that is a deliberate
// footprint choice. Fail fast (like PC_ENABLE_SSH_ZLIB / PC_ENABLE_HTTP2) so it is not an
// accidental DRAM overflow: move the pool to PSRAM, or acknowledge the internal-DRAM cost.
#if PROTOCORE_HOT && !PC_QUIC_SERVER_IN_PSRAM && !PC_QUIC_SERVER_ACK_DRAM
#error                                                                                                                 \
    "ProtoCore: PC_ENABLE_HTTP3 - the pc_quic_server QuicConn+H3Conn pool + ingest ring are tens of KB. Set PC_QUIC_SERVER_IN_PSRAM=1 on a PSRAM board (S3 / P4 / WROVER built with CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y, tools/psram/README.md), OR set PC_QUIC_SERVER_ACK_DRAM=1 to accept the internal-DRAM cost (fits a small pool on a roomy chip)."
#endif
#if PC_QUIC_SERVER_IN_PSRAM && PROTOCORE_HOT
#include <esp_attr.h>
#if defined(EXT_RAM_BSS_ATTR)
#define PC_QUIC_POOL_ATTR EXT_RAM_BSS_ATTR
#elif defined(EXT_RAM_ATTR)
#define PC_QUIC_POOL_ATTR EXT_RAM_ATTR
#else
#define PC_QUIC_POOL_ATTR
#endif
#else
#define PC_QUIC_POOL_ATTR
#endif

// One buffered inbound datagram (payload + the peer it arrived from).
typedef struct
{
    uint8_t data[PC_QUIC_MAX_DATAGRAM];
    uint16_t len;
    char ip[16];
    uint16_t port;
} QuicIngest;

// One pool slot: a QUIC connection, its HTTP/3 engine, and the peer to reply to.
typedef struct
{
    proto_bool used;
    uint32_t id; ///< stable handle for pc_quic_server_respond()
    QuicConn qc;
    H3Conn h3;
    char peer_ip[16];
    uint16_t peer_port;
    uint32_t last_ms; ///< pc_millis() of the last datagram received (idle-reaping clock)
} QuicSlot;

// HTTP/3 QUIC connection + ingest buffers, owned by one instance (internal linkage). Placed in
// PSRAM (PC_QUIC_POOL_ATTR) when configured; kept separate from the DRAM control state below
// so only the large buffers move off internal RAM. One named owner, unreachable cross-TU.
typedef struct
{
    QuicSlot pool[PC_QUIC_MAX_CONNS];
    QuicIngest ring[PC_QUIC_INGEST_RING];
} QuicServerPoolCtx;
static PC_QUIC_POOL_ATTR QuicServerPoolCtx s_qpool;

// All HTTP/3 QUIC server control state, owned by one instance (internal linkage): the ingest
// ring cursors, the server config + request callback + app pointer, the bound port / running
// flag / next connection id, and (host) the outbound sink. One named owner, unreachable cross-TU.
typedef struct
{
    _Atomic size_t ring_head; ///< producer (udp / ingest) advances
    _Atomic size_t ring_tail; ///< consumer (poll) advances
    QuicServerConfig cfg;
    QuicServerRequestFn on_request;
    void *app;
    uint16_t port;
    proto_bool running;
    uint32_t next_id; ///< set to 1 by pc_quic_server_begin(); never handed out as 0
#if !PROTOCORE_HOT
    QuicServerOutFn out_sink;
    void *out_ctx;
#endif
} QuicServerCtx;
static QuicServerCtx s_quic;

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    if (src)
    {
        while (src[n] && n + 1 < cap)
        {
            dst[n] = src[n];
            n++;
        }
    }
    dst[n] = 0;
}

static proto_bool cid_eq(const uint8_t *a, uint8_t alen, const uint8_t *b, uint8_t blen)
{
    return alen == blen && memcmp(a, b, alen) == 0;
}

static void server_send(const char *ip, uint16_t port, const uint8_t *data, size_t len)
{
#if PROTOCORE_HOT
    pc_udp_listener_sendto(s_quic.port, ip, port, data, len);
#else
    if (s_quic.out_sink)
    {
        s_quic.out_sink(s_quic.out_ctx, data, len, ip, port);
    }
#endif
}

// --- ingest ring (SPSC: one producer fills, pc_quic_server_poll consumes) ------------------------
static proto_bool ring_push(const uint8_t *dg, size_t len, const char *ip, uint16_t port)
{
    if (len == 0 || len > PC_QUIC_MAX_DATAGRAM)
    {
        return PROTO_FALSE;
    }
    size_t head = s_quic.ring_head;
    size_t next = (head + 1) % PC_QUIC_INGEST_RING;
    if (next == (size_t)s_quic.ring_tail)
    {
        return PROTO_FALSE; // ring full: drop (QUIC recovers via retransmission)
    }
    QuicIngest *e = &s_qpool.ring[head];
    memcpy(e->data, dg, len);
    e->len = (uint16_t)len;
    copy_str(e->ip, sizeof e->ip, ip);
    e->port = port;
    s_quic.ring_head = next; // publish after the record is fully written
    return PROTO_TRUE;
}

static proto_bool ring_pop(QuicIngest *out)
{
    size_t tail = s_quic.ring_tail;
    if (tail == (size_t)s_quic.ring_head)
    {
        return PROTO_FALSE;
    }
    *out = s_qpool.ring[tail];
    s_quic.ring_tail = (tail + 1) % PC_QUIC_INGEST_RING;
    return PROTO_TRUE;
}

// --- slot pool --------------------------------------------------------------------------------
static QuicSlot *slot_by_id(uint32_t id)
{
    for (uint8_t i = 0; i < PC_QUIC_MAX_CONNS; i++)
    {
        if (s_qpool.pool[i].used && s_qpool.pool[i].id == id)
        {
            return &s_qpool.pool[i];
        }
    }
    return NULL;
}

static QuicSlot *alloc_slot()
{
    for (uint8_t i = 0; i < PC_QUIC_MAX_CONNS; i++)
    {
        if (!s_qpool.pool[i].used)
        {
            QuicSlot *s = &s_qpool.pool[i];
            memset(s, 0, sizeof *s);
            s->used = PROTO_TRUE;
            s->id = s_quic.next_id++;
            if (s_quic.next_id == 0)
            // true branch (wrap) is unreachable - it needs 2^32 allocations in
            // one process lifetime (next_id is never reset except by begin()),
            // which no host test can drive
            {
                s_quic.next_id = 1;
            }
            return s;
        }
    }
    return NULL;
}

// The HTTP/3 engine surfaces a completed request here; forward it to the application by conn id.
static void pc_h3_on_request(void *app, H3Conn * /*h3*/, uint64_t stream_id, const char *method, const char *path,
                             const char *authority, const uint8_t *body, size_t body_len)
{
    QuicSlot *s = (QuicSlot *)app;
    if (s_quic.on_request)
    {
        s_quic.on_request(s_quic.app, s->id, stream_id, method, path, authority, body, body_len);
    }
}

// Open a connection for a client's first Initial packet.
static QuicSlot *open_conn(const QuicLongHeader *lh, const char *ip, uint16_t port)
{
    QuicSlot *s = alloc_slot();
    if (!s)
    {
        return NULL;
    }

    QuicTlsConfig tc;
    memset(&tc, 0, sizeof tc);
    tc.cert_der = s_quic.cfg.cert_der;
    tc.cert_len = s_quic.cfg.cert_len;
    memcpy(tc.ed25519_seed, s_quic.cfg.ed25519_seed, sizeof tc.ed25519_seed);
    pc_quic_tp_defaults(&tc.params);
    // A real HTTP/3 endpoint must advertise flow-control room, or every request stream (and the
    // client's control / QPACK streams) is blocked - the RFC 9000 sec 18.2 defaults are all zero.
    tc.params.initial_max_data = 1048576;
    tc.params.initial_max_sd_bidi_remote = 262144; // client-initiated request streams
    tc.params.initial_max_sd_uni = 262144;         // client control / QPACK encoder+decoder streams
    tc.params.initial_max_streams_bidi = PC_H3_MAX_STREAMS;
    tc.params.initial_max_streams_uni = PC_H3_MAX_STREAMS;
    tc.params.max_idle_timeout = PC_QUIC_IDLE_MS; // both ends reclaim the connection after this idle
    s_quic.cfg.rng(tc.ephemeral_priv, sizeof tc.ephemeral_priv);
    s_quic.cfg.rng(tc.random, sizeof tc.random);
#if PC_ENABLE_PQC_KEX
    s_quic.cfg.rng(tc.mlkem_m, sizeof tc.mlkem_m); // fresh ML-KEM Encaps randomness per handshake
#endif

    uint8_t our_scid[PC_QUIC_SCID_LEN];
    s_quic.cfg.rng(our_scid, sizeof our_scid);

    QuicConnCallbacks cb;
    memset(&cb, 0, sizeof cb); // pc_h3_conn_init installs the real callbacks
    pc_quic_conn_init(&s->qc, &tc, lh->dcid, lh->dcid_len, lh->scid, lh->scid_len, our_scid, PC_QUIC_SCID_LEN, &cb);
    pc_h3_conn_init(&s->h3, &s->qc, pc_h3_on_request, s);

    copy_str(s->peer_ip, sizeof s->peer_ip, ip);
    s->peer_port = port;
    return s; // last_ms is set by the poll that received this datagram
}

// Route a datagram to its connection by Destination Connection ID. Sets *is_initial when it is an
// unmatched Initial (the caller opens a new connection) and copies the parsed long header out.
static QuicSlot *route(const uint8_t *dg, size_t len, proto_bool *is_initial, QuicLongHeader *lh_out)
{
    *is_initial = PROTO_FALSE;
    if (len < 1)
    // only the true branch is unreachable - route()'s sole call site is
    // pc_quic_server_poll, fed by ring_pop() from a ring that ring_push() (both
    // ingest paths) already refuses to fill with len==0, so len>=1 always holds here
    {
        return NULL;
    }
    if (pc_quic_is_long_header(dg[0]))
    {
        if (!pc_quic_parse_long_header(dg, len, lh_out))
        {
            return NULL;
        }
        for (uint8_t i = 0; i < PC_QUIC_MAX_CONNS; i++)
        {
            QuicSlot *s = &s_qpool.pool[i];
            if (!s->used)
            {
                continue;
            }
            if (cid_eq(lh_out->dcid, lh_out->dcid_len, s->qc.scid, s->qc.scid_len) ||
                cid_eq(lh_out->dcid, lh_out->dcid_len, s->qc.odcid, s->qc.odcid_len))
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
    if (len < (size_t)1 + PC_QUIC_SCID_LEN)
    {
        return NULL;
    }
    for (uint8_t i = 0; i < PC_QUIC_MAX_CONNS; i++)
    {
        QuicSlot *s = &s_qpool.pool[i];
        // scid_len is always PC_QUIC_SCID_LEN (only this server sets it, at conn init above), so its
        // != arm below is a defensive guard no host input can reach.
        if (s->used && s->qc.scid_len == PC_QUIC_SCID_LEN && memcmp(dg + 1, s->qc.scid, PC_QUIC_SCID_LEN) == 0)
        {
            return s;
        }
    }
    return NULL;
}

static void flush_and_reap(uint32_t now_ms)
{
    uint8_t out[PC_QUIC_MAX_DATAGRAM];
    for (uint8_t i = 0; i < PC_QUIC_MAX_CONNS; i++)
    {
        QuicSlot *s = &s_qpool.pool[i];
        if (!s->used)
        {
            continue;
        }
        pc_quic_conn_on_timeout(&s->qc, now_ms); // retransmit a lost handshake flight (PTO)
        size_t n;
        while ((n = pc_quic_conn_send(&s->qc, out, sizeof out)) > 0)
        {
            server_send(s->peer_ip, s->peer_port, out, n);
        }
        // Reap a closed connection, or one idle past the timeout (wrap-safe delta) so a client that
        // never closes cannot leak the fixed pool.
        if (pc_quic_conn_is_closed(&s->qc) || (uint32_t)(now_ms - s->last_ms) >= PC_QUIC_IDLE_MS)
        {
            s->used = PROTO_FALSE;
        }
    }
}

#if PROTOCORE_HOT
static void udp_ingest_cb(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)ctx;
    char ip[16];
    uint16_t port = 0;
    if (!pc_udp_peer_addr(peer, ip, sizeof ip, &port))
    {
        return;
    }
    ring_push(data, len, ip, port);
}
#endif

proto_bool pc_quic_server_begin(uint16_t port, const QuicServerConfig *cfg, QuicServerRequestFn on_request, void *app)
{
    if (!cfg || !cfg->rng)
    {
        return PROTO_FALSE;
    }
    s_quic.cfg = *cfg;
    s_quic.on_request = on_request;
    s_quic.app = app;
    s_quic.port = port ? port : PC_HTTP3_PORT;
    for (uint8_t i = 0; i < PC_QUIC_MAX_CONNS; i++)
    {
        s_qpool.pool[i].used = PROTO_FALSE;
    }
    s_quic.ring_head = 0;
    s_quic.ring_tail = 0;
    s_quic.next_id = 1;
    s_quic.running = PROTO_TRUE;
#if PROTOCORE_HOT
    return pc_udp_listen(s_quic.port, udp_ingest_cb, NULL);
#else
    return PROTO_TRUE; // host: fed through pc_quic_server_ingest()
#endif
}

void pc_quic_server_poll(uint32_t now_ms)
{
    if (!s_quic.running)
    {
        return;
    }
    QuicIngest ig;
    while (ring_pop(&ig))
    {
        proto_bool is_initial = PROTO_FALSE;
        QuicLongHeader lh;
        QuicSlot *s = route(ig.data, ig.len, &is_initial, &lh);
        if (!s && is_initial)
        {
            s = open_conn(&lh, ig.ip, ig.port);
        }
        if (!s)
        {
            continue;
        }
        s->last_ms = now_ms; // liveness for idle reaping
        pc_quic_conn_recv(&s->qc, ig.data, ig.len);
    }
    flush_and_reap(now_ms);
}

proto_bool pc_quic_server_respond(uint32_t conn_id, uint64_t stream_id, int status, const char *content_type,
                                  const uint8_t *body, size_t body_len)
{
    QuicSlot *s = slot_by_id(conn_id);
    if (!s)
    {
        return PROTO_FALSE;
    }
    return pc_h3_conn_respond(&s->h3, stream_id, status, content_type, body, body_len);
}

uint8_t pc_quic_server_active_conns(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < PC_QUIC_MAX_CONNS; i++)
    {
        if (s_qpool.pool[i].used)
        {
            n++;
        }
    }
    return n;
}

void pc_quic_server_stop(void)
{
    s_quic.running = PROTO_FALSE;
    for (uint8_t i = 0; i < PC_QUIC_MAX_CONNS; i++)
    {
        s_qpool.pool[i].used = PROTO_FALSE;
    }
    s_quic.ring_head = 0;
    s_quic.ring_tail = 0;
}

#if !PROTOCORE_HOT
void pc_quic_server_set_out_sink_cb(QuicServerOutFn fn, void *ctx)
{
    s_quic.out_sink = fn;
    s_quic.out_ctx = ctx;
}

proto_bool pc_quic_server_ingest(const uint8_t *datagram, size_t len, const char *ip, uint16_t port)
{
    return ring_push(datagram, len, ip, port);
}
#endif

#endif // PC_ENABLE_HTTP3
