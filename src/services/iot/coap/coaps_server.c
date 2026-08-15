// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file coaps_server.c
 * @brief The CoAP-over-DTLS server: the pool, the ingest ring, and the poll. See coaps_server.h.
 */

#include "services/iot/coap/coaps_server.h"

#if PROTOCORE_ENABLE_DTLS && PROTOCORE_ENABLE_COAP

#include "mmgr/protomem.h" // mem.cpy / mem.set / mem.cmp: the spans a slot and the ring move
#include "mmgr/protostr.h" // str.copy / str.eq: the peer address text
#include "mmgr/ring.h"     // the atomics the single-producer ingest ring's cursors are
#include "network_drivers/presentation/security/dtls/dtls_conn.h"
#include "server/clock/clock.h"      // Clock.millis: the reclaim clock
#include "services/iot/coap/coaps.h" // Coaps.process: the handshake and the CoAP exchange

#if PROTOCORE_HAS_NET_STACK
#include "network_drivers/transport/udp/server/server.h" // UdpListener: the bound port
#include "shared/ip/ip.h"                                // Ip.parse: a reply's destination
#endif

// The largest inbound datagram buffered: a ClientHello, a client Finished, or one CoAP application
// record (a CoAP message fits one datagram, RFC 7252 sec 4.6). An outbound flight is larger but goes
// straight out and is never buffered here.
#ifndef PROTOCORE_COAPS_MAX_DATAGRAM
#define PROTOCORE_COAPS_MAX_DATAGRAM 1500
#endif
// Scratch for one poll's outbound datagram: a whole server flight (ServerHello through Finished) or a
// sealed CoAP response. The certificate-dominated flight is the largest thing written into it.
#define PROTOCORE_COAPS_OUT_CAP 2048
// The HelloRetryRequest cookie binds the peer's IPv4 address (4) and port (2) (RFC 9147 sec 5.1).
#define PROTOCORE_COAPS_PEER_SER 6
// RFC 9147 sec 4 Figure 3, the DTLSCiphertext unified header's first byte.
#define COAPS_UHDR_FIXED_MASK 0xE0u ///< the three high bits
#define COAPS_UHDR_FIXED 0x20u      ///< which are set to 001
#define COAPS_UHDR_CID 0x10u        ///< the C bit: a Connection ID follows
#define COAPS_PEER_TEXT 16u         ///< bytes a peer's address text occupies, dotted-quad sized

/** @brief One buffered inbound datagram: its octets and the peer it arrived from. */
typedef struct
{
    uint8_t data[PROTOCORE_COAPS_MAX_DATAGRAM];
    uint16_t len;
    char ip[COAPS_PEER_TEXT];
    uint16_t port;
} CoapsIngest;

/**
 * @brief One pool slot: a DTLS connection, the key material its configuration points at, and the peer.
 *
 * The configuration's pointers reference the buffers beside it, so they outlive the connection.
 */
typedef struct
{
    proto_bool used;
    DtlsConn conn;
    DtlsServerConfig cfg; ///< this slot's configuration
    uint8_t eph[32];      ///< the X25519 ephemeral private key of this handshake
    uint8_t srand[32];    ///< the ServerHello random of this handshake
    char peer_ip[COAPS_PEER_TEXT];
    uint16_t peer_port;
    uint32_t last_ms; ///< the clock at the last inbound datagram, which is what reclaiming measures
} CoapsSlot;

/**
 * @brief The server's compile-time storage: the connection pool, the ingest ring, and the identity.
 *
 * All of it BSS, so a connection costs no heap.
 */
struct CoapsServerStorage
{
    CoapsSlot pool[PROTOCORE_COAPS_MAX_CONNS];     ///< the connections
    CoapsIngest ring[PROTOCORE_COAPS_INGEST_RING]; ///< the datagrams waiting for a poll

    _Atomic size_t ring_head; ///< the receive path advances it
    _Atomic size_t ring_tail; ///< the poll advances it

    const uint8_t *cert_der;               ///< the leaf certificate, referenced
    size_t cert_len;                       ///< its length
    uint8_t ed25519_seed[32];              ///< the signing seed, copied in
    uint8_t cookie_key[32];                ///< the cookie secret, copied in (RFC 9147 sec 5.1)
    void (*rng)(uint8_t *out, size_t len); ///< the CSPRNG each handshake draws from
    uint16_t port;                         ///< the bound port
    proto_bool running;                    ///< a begin succeeded and a stop has not run
#if !PROTOCORE_HAS_NET_STACK
    CoapsServerOutFn out_sink; ///< where an outbound datagram goes
    void *out_ctx;             ///< the context that sink is given back
#endif
};

/**
 * @brief The server's state and the calls that reach it - what CoapsServerNs points at.
 *
 * @var CoapsServerInternal::store  the pool, the ingest ring, and the identity
 * @var CoapsServerInternal::ns     the handle a caller sets a call's members on
 */
struct CoapsServerInternal
{
    struct CoapsServerStorage *store;
    CoapsServerNs *ns;
};

static struct CoapsServerStorage s_store;

static struct CoapsServerInternal s_coaps = {.store = &s_store, .ns = &CoapsServer};

// ---------------------------------------------------------------------------
// Pure helpers: they read what they are given and hold nothing
// ---------------------------------------------------------------------------

// Serialize a dotted-quad address and a port into the six octets the HelloRetryRequest cookie is
// bound to, so a cookie minted for one peer is worthless to another (RFC 9147 sec 5.1). False on a
// malformed address, which leaves no address bound and simply denies that peer the retry path.
static proto_bool serialize_peer(const char *ip, uint16_t port, uint8_t out[PROTOCORE_COAPS_PEER_SER])
{
    uint32_t oct = 0;
    int octets = 0;
    int ndig = 0;
    int idx = 0;
    for (const char *p = ip;; p++)
    {
        if (*p >= '0' && *p <= '9')
        {
            oct = oct * 10 + (uint32_t)(*p - '0');
            ndig++;
            if (oct > 255 || ndig > 3)
            {
                return PROTO_FALSE;
            }
        }
        else if (*p == '.' || *p == 0)
        {
            if (ndig == 0)
            {
                return PROTO_FALSE;
            }
            out[idx++] = (uint8_t)oct;
            octets++;
            oct = 0;
            ndig = 0;
            if (*p == 0)
            {
                break;
            }
        }
        else
        {
            return PROTO_FALSE;
        }
    }
    if (octets != 4)
    {
        return PROTO_FALSE;
    }
    out[4] = (uint8_t)(port >> 8);
    out[5] = (uint8_t)(port & 0xFF);
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// The ingest ring: one producer fills it, the poll drains it
// ---------------------------------------------------------------------------

// Take a copy of one datagram and its peer. False when it does not fit or the ring is full, and a
// dropped datagram is what the DTLS retransmission timer already recovers from.
static proto_bool ring_push(struct CoapsServerInternal *restrict ctx, const uint8_t *dg, size_t len, const char *ip,
                            uint16_t port)
{
    if (!dg || !ip || len == 0 || len > PROTOCORE_COAPS_MAX_DATAGRAM)
    {
        return PROTO_FALSE;
    }
    size_t head = ctx->store->ring_head;
    size_t next = (head + 1) % PROTOCORE_COAPS_INGEST_RING;
    if (next == (size_t)ctx->store->ring_tail)
    {
        return PROTO_FALSE;
    }
    CoapsIngest *e = &ctx->store->ring[head];
    mem.cpy(e->data, dg, len);
    e->len = (uint16_t)len;
    (void)str.copy(e->ip, ip, sizeof(e->ip));
    e->port = port;
    ctx->store->ring_head = next; // published only once the entry is whole
    return PROTO_TRUE;
}

// Take the oldest queued datagram. False when the ring holds none.
static proto_bool ring_pop(struct CoapsServerInternal *restrict ctx, CoapsIngest *out)
{
    size_t tail = ctx->store->ring_tail;
    if (tail == (size_t)ctx->store->ring_head)
    {
        return PROTO_FALSE;
    }
    *out = ctx->store->ring[tail];
    ctx->store->ring_tail = (tail + 1) % PROTOCORE_COAPS_INGEST_RING;
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// The pool: keyed by peer address, or by Connection ID once one is negotiated
// ---------------------------------------------------------------------------

// The slot whose peer is ip:port, or NULL.
static CoapsSlot *slot_by_peer(struct CoapsServerInternal *restrict ctx, const char *ip, uint16_t port)
{
    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        CoapsSlot *s = &ctx->store->pool[i];
        if (s->used && s->peer_port == port && str.eq(s->peer_ip, ip, sizeof(s->peer_ip), PROTO_FALSE))
        {
            return s;
        }
    }
    return NULL;
}

// The slot whose Connection ID (RFC 9146, RFC 9147 sec 9) the record carries, so a peer that moved to
// a new address still reaches its connection. @p cid points just past the unified header's first
// byte and @p avail is what is readable there.
static CoapsSlot *slot_by_cid(struct CoapsServerInternal *restrict ctx, const uint8_t *cid, size_t avail)
{
    uint8_t sc[PROTOCORE_DTLS_CID_MAX];
    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        CoapsSlot *s = &ctx->store->pool[i];
        if (!s->used)
        {
            continue;
        }
        size_t sl = DtlsServer.local_cid(&s->conn, sc);
        if (sl && sl <= avail && mem.cmp(cid, sc, sl) == 0)
        {
            return s;
        }
    }
    return NULL;
}

// The first free slot, cleared and claimed, or NULL when the pool is full.
static CoapsSlot *alloc_slot(struct CoapsServerInternal *restrict ctx)
{
    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        if (!ctx->store->pool[i].used)
        {
            CoapsSlot *s = &ctx->store->pool[i];
            mem.set(s, 0, sizeof(*s));
            s->used = PROTO_TRUE;
            return s;
        }
    }
    return NULL;
}

// Open a connection for a peer that has no slot, drawing this handshake's ephemeral and random.
static CoapsSlot *open_conn(struct CoapsServerInternal *restrict ctx, const char *ip, uint16_t port)
{
    CoapsSlot *s = alloc_slot(ctx);
    if (!s)
    {
        return NULL;
    }
    ctx->store->rng(s->eph, sizeof(s->eph));     // the X25519 ephemeral private key
    ctx->store->rng(s->srand, sizeof(s->srand)); // the ServerHello random
    s->cfg.cert_der = ctx->store->cert_der;
    s->cfg.cert_len = ctx->store->cert_len;
    s->cfg.ed25519_seed = ctx->store->ed25519_seed;
    s->cfg.ephemeral_priv = s->eph;
    s->cfg.server_random = s->srand;
    s->cfg.cookie_key = ctx->store->cookie_key;
    uint8_t paddr[PROTOCORE_COAPS_PEER_SER];
    proto_bool bound = serialize_peer(ip, port, paddr);
    DtlsServer.init(&s->conn, &s->cfg, bound ? paddr : NULL, bound ? sizeof(paddr) : 0);
    (void)str.copy(s->peer_ip, ip, sizeof(s->peer_ip));
    s->peer_port = port;
    return s;
}

// ---------------------------------------------------------------------------
// Sending, and the two halves of one poll
// ---------------------------------------------------------------------------

// Put len octets on the wire to ip:port, from the bound port where there is one.
static void server_send(struct CoapsServerInternal *restrict ctx, const char *ip, uint16_t port, const uint8_t *data,
                        size_t len)
{
#if PROTOCORE_HAS_NET_STACK
    protocore_ip dst = {PROTOCORE_IP_NONE, {0}};
    Ip.args.text = ip;
    Ip.args.out = &dst;
    Ip.parse(Ip.internal);
    if (!Ip.ok)
    {
        return;
    }
    UdpListener.port = ctx->store->port;
    UdpListener.send_args.dst = &dst;
    UdpListener.send_args.dst_port = port;
    UdpListener.send_args.data = data;
    UdpListener.send_args.len = len;
    UdpListener.sendto(UdpListener.internal);
#else
    if (ctx->store->out_sink)
    {
        ctx->store->out_sink(ctx->store->out_ctx, data, len, ip, port);
    }
#endif
}

// Route one queued datagram to its connection, opening one for a new peer's ClientHello, and drive
// the handshake or the CoAP exchange through the bridge.
static void route_datagram(struct CoapsServerInternal *restrict ctx, const CoapsIngest *ig, uint32_t now, uint8_t *out,
                           size_t out_cap)
{
    // A record carrying a Connection ID (RFC 9147 sec 4 Figure 3, the C bit) routes by that id, so a
    // peer that moved to a new address still reaches its connection (RFC 9146, RFC 9147 sec 9).
    // Everything else routes by source address, and a new peer's plaintext ClientHello opens a slot.
    proto_bool cid_rec =
        ig->len >= 1 && (ig->data[0] & COAPS_UHDR_FIXED_MASK) == COAPS_UHDR_FIXED && (ig->data[0] & COAPS_UHDR_CID);
    CoapsSlot *s = cid_rec ? slot_by_cid(ctx, ig->data + 1, ig->len - 1) : NULL;
    if (!s)
    {
        s = slot_by_peer(ctx, ig->ip, ig->port);
    }
    if (!s && !cid_rec)
    {
        s = open_conn(ctx, ig->ip, ig->port);
    }
    if (!s)
    {
        return; // an unknown Connection ID, or a full pool: the peer's retransmission recovers it
    }
    // A valid record from a new address moves where the replies go.
    if (cid_rec && (s->peer_port != ig->port || !str.eq(s->peer_ip, ig->ip, sizeof(s->peer_ip), PROTO_FALSE)))
    {
        (void)str.copy(s->peer_ip, ig->ip, sizeof(s->peer_ip));
        s->peer_port = ig->port;
    }
    s->last_ms = now;
    Coaps.conn = &s->conn;
    Coaps.dgram.data = ig->data;
    Coaps.dgram.len = ig->len;
    Coaps.dgram.out = out;
    Coaps.dgram.out_cap = out_cap;
    Coaps.process(Coaps.internal);
    if (Coaps.i32 > 0)
    {
        server_send(ctx, s->peer_ip, s->peer_port, out, (size_t)Coaps.i32);
    }
    else if (Coaps.i32 < 0)
    {
        s->used = PROTO_FALSE; // the handshake failed, so the slot goes back
    }
}

// Fire the retransmission timer for a slot's outstanding flight (RFC 9147 sec 5.8), then reclaim the
// slot if the handshake gave up or the connection has gone quiet.
static void service_slot(struct CoapsServerInternal *restrict ctx, CoapsSlot *s, uint32_t now, uint8_t *out,
                         size_t out_cap)
{
    if (!s->used)
    {
        return;
    }
    if (DtlsServer.timeout_ms(&s->conn) == 0) // 0 is due now, -1 is no timer, above 0 is still pending
    {
        int n = DtlsServer.on_timeout(&s->conn, out, out_cap);
        if (n > 0)
        {
            server_send(ctx, s->peer_ip, s->peer_port, out, (size_t)n);
        }
        else if (n < 0)
        {
            s->used = PROTO_FALSE; // the retransmission ceiling was reached
            return;
        }
    }
    if (now - s->last_ms >= PROTOCORE_COAPS_IDLE_MS) // unsigned, so the clock's wrap is safe
    {
        s->used = PROTO_FALSE;
    }
}

#if PROTOCORE_HAS_NET_STACK
static void udp_ingest_cb(const uint8_t *data, size_t len, const struct protocore_udp_peer *peer, void *user)
{
    (void)user;
    char ip[COAPS_PEER_TEXT] = {0};
    uint16_t port = 0;
    UdpListener.peer_args.peer = peer;
    UdpListener.peer_args.ip_out = ip;
    UdpListener.peer_args.ip_cap = sizeof(ip);
    UdpListener.peer_args.port_out = &port;
    UdpListener.peer_addr(UdpListener.internal);
    if (!UdpListener.ok)
    {
        return;
    }
    (void)ring_push(&s_coaps, data, len, ip, port);
}
#endif

// ---------------------------------------------------------------------------
// The bodies behind the table
// ---------------------------------------------------------------------------

// Install ns->identity, bind ns->bind.port, and route its datagrams into the pool.
static void coaps_server_begin(struct CoapsServerInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->identity.rng || !ctx->ns->identity.cert_der || ctx->ns->identity.cert_len == 0)
    {
        return;
    }
    ctx->store->cert_der = ctx->ns->identity.cert_der;
    ctx->store->cert_len = ctx->ns->identity.cert_len;
    mem.cpy(ctx->store->ed25519_seed, ctx->ns->identity.ed25519_seed, sizeof(ctx->store->ed25519_seed));
    mem.cpy(ctx->store->cookie_key, ctx->ns->identity.cookie_key, sizeof(ctx->store->cookie_key));
    ctx->store->rng = ctx->ns->identity.rng;
    ctx->store->port = ctx->ns->bind.port ? ctx->ns->bind.port : PROTOCORE_COAPS_PORT;
    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        ctx->store->pool[i].used = PROTO_FALSE;
    }
    ctx->store->ring_head = 0;
    ctx->store->ring_tail = 0;
    ctx->store->running = PROTO_TRUE;
#if PROTOCORE_HAS_NET_STACK
    UdpListener.port = ctx->store->port;
    UdpListener.bind.handler = udp_ingest_cb;
    UdpListener.bind.handler_ctx = NULL;
    UdpListener.listen(UdpListener.internal);
    ctx->ns->ok = UdpListener.ok;
#else
    ctx->ns->ok = PROTO_TRUE; // fed through ingest instead
#endif
}

// Drain the queued datagrams, then service every slot.
static void coaps_server_poll(struct CoapsServerInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->store->running)
    {
        return;
    }
    const uint32_t now = Clock.ms;
    uint8_t out[PROTOCORE_COAPS_OUT_CAP];

    CoapsIngest ig;
    while (ring_pop(ctx, &ig))
    {
        route_datagram(ctx, &ig, now, out, sizeof(out));
    }

    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        service_slot(ctx, &ctx->store->pool[i], now, out, sizeof(out));
    }
    ctx->ns->ok = PROTO_TRUE;
}

// The pool slots in use.
static void coaps_server_active_conns(struct CoapsServerInternal *restrict ctx)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        if (ctx->store->pool[i].used)
        {
            n++;
        }
    }
    ctx->ns->u8 = n;
    ctx->ns->ok = PROTO_TRUE;
}

// Stop polling, release every slot, and empty the ingest ring. The port stays bound.
static void coaps_server_stop(struct CoapsServerInternal *restrict ctx)
{
    ctx->store->running = PROTO_FALSE;
    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        ctx->store->pool[i].used = PROTO_FALSE;
    }
    ctx->store->ring_head = 0;
    ctx->store->ring_tail = 0;
    ctx->ns->ok = PROTO_TRUE;
}

#if !PROTOCORE_HAS_NET_STACK
// Install ns->sink as where every outbound datagram goes.
static void coaps_server_set_out_sink(struct CoapsServerInternal *restrict ctx)
{
    ctx->store->out_sink = ctx->ns->sink.fn;
    ctx->store->out_ctx = ctx->ns->sink.ctx;
    ctx->ns->ok = PROTO_TRUE;
}

// Queue ns->dgram as though it had been received, for the next poll to route.
static void coaps_server_ingest(struct CoapsServerInternal *restrict ctx)
{
    ctx->ns->ok = ring_push(ctx, ctx->ns->dgram.data, ctx->ns->dgram.len, ctx->ns->dgram.ip, ctx->ns->dgram.port);
}
#endif

// Designated, so a member's position in the struct does not decide what it binds to.
CoapsServerNs CoapsServer = {
    .begin = coaps_server_begin,
    .poll = coaps_server_poll,
    .active_conns = coaps_server_active_conns,
    .stop = coaps_server_stop,
#if !PROTOCORE_HAS_NET_STACK
    .set_out_sink = coaps_server_set_out_sink,
    .ingest = coaps_server_ingest,
#endif
    .internal = &s_coaps,
};

#endif // PROTOCORE_ENABLE_DTLS && PROTOCORE_ENABLE_COAP
