// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file coaps_server.c
 * @brief The CoAP-over-DTLS server: the pool, the ingest ring, and the poll. See coaps_server.h.
 */

#include "services/iot/coap/coaps_server/coaps_server.h"
#include "mmgr/secure/secure.h" // the persistent end this module's key material is taken from

static uint8_t dtls_server_work[16]; // the borrow an entry takes; DtlsServer never reads it

static uint8_t coaps_work[16]; // the borrow an entry takes; Coaps never reads it

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_COAPS_SERVER_BORROW persistent bytes
} CoapsServerOwnCtx;
static CoapsServerOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_coaps_server_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_COAPS_SERVER_BORROW).buf;
    }
    return s_own.span;
}

#if PROTOCORE_ENABLE_DTLS && PROTOCORE_ENABLE_COAP

#include "mmgr/protomem/protomem.h" // mem.cpy / mem.set / mem.cmp: the spans a slot and the ring move
#include "mmgr/protostr/protostr.h" // str.copy / str.eq: the peer address text
#include "mmgr/ring.h"              // the atomics the single-producer ingest ring's cursors are
#include "network_drivers/presentation/security/dtls/dtls_conn/dtls_conn.h"
#include "server/clock/clock.h"            // Clock.millis: the reclaim clock
#include "services/iot/coap/coaps/coaps.h" // Coaps.process: the handshake and the CoAP exchange

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

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define COAPS_SERVER_OFF_CTX 0u
static_assert(COAPS_SERVER_OFF_CTX + sizeof(struct CoapsServerStorage) <= PROTOCORE_COAPS_SERVER_BORROW,
              "PROTOCORE_COAPS_SERVER_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define COAPS_SERVER_CTX(w) ((struct CoapsServerStorage *)(void *)((w) + COAPS_SERVER_OFF_CTX))

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
static proto_bool ring_push(uint8_t *restrict work, const uint8_t *dg, size_t len, const char *ip, uint16_t port)
{
    if (!dg || !ip || len == 0 || len > PROTOCORE_COAPS_MAX_DATAGRAM)
    {
        return PROTO_FALSE;
    }
    size_t head = COAPS_SERVER_CTX(work)->ring_head;
    size_t next = (head + 1) % PROTOCORE_COAPS_INGEST_RING;
    if (next == (size_t)COAPS_SERVER_CTX(work)->ring_tail)
    {
        return PROTO_FALSE;
    }
    CoapsIngest *e = &COAPS_SERVER_CTX(work)->ring[head];
    mem.cpy(e->data, dg, len);
    e->len = (uint16_t)len;
    (void)str.copy(e->ip, ip, sizeof(e->ip));
    e->port = port;
    COAPS_SERVER_CTX(work)->ring_head = next; // published only once the entry is whole
    return PROTO_TRUE;
}

// Take the oldest queued datagram. False when the ring holds none.
static proto_bool ring_pop(uint8_t *restrict work, CoapsIngest *out)
{
    size_t tail = COAPS_SERVER_CTX(work)->ring_tail;
    if (tail == (size_t)COAPS_SERVER_CTX(work)->ring_head)
    {
        return PROTO_FALSE;
    }
    *out = COAPS_SERVER_CTX(work)->ring[tail];
    COAPS_SERVER_CTX(work)->ring_tail = (tail + 1) % PROTOCORE_COAPS_INGEST_RING;
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// The pool: keyed by peer address, or by Connection ID once one is negotiated
// ---------------------------------------------------------------------------

// The slot whose peer is ip:port, or NULL.
static CoapsSlot *slot_by_peer(uint8_t *restrict work, const char *ip, uint16_t port)
{
    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        CoapsSlot *s = &COAPS_SERVER_CTX(work)->pool[i];
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
static CoapsSlot *slot_by_cid(uint8_t *restrict work, const uint8_t *cid, size_t avail)
{
    uint8_t sc[PROTOCORE_DTLS_CID_MAX];
    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        CoapsSlot *s = &COAPS_SERVER_CTX(work)->pool[i];
        if (!s->used)
        {
            continue;
        }
        DtlsServerV.local_cid_args.c = &s->conn;
        DtlsServerV.local_cid_args.out = sc;
        DtlsServer.local_cid(dtls_server_work);
        size_t sl = DtlsServerV.n;
        if (sl && sl <= avail && mem.cmp(cid, sc, sl) == 0)
        {
            return s;
        }
    }
    return NULL;
}

// The first free slot, cleared and claimed, or NULL when the pool is full.
static CoapsSlot *alloc_slot(uint8_t *restrict work)
{
    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        if (!COAPS_SERVER_CTX(work)->pool[i].used)
        {
            CoapsSlot *s = &COAPS_SERVER_CTX(work)->pool[i];
            mem.set(s, 0, sizeof(*s));
            s->used = PROTO_TRUE;
            return s;
        }
    }
    return NULL;
}

// Open a connection for a peer that has no slot, drawing this handshake's ephemeral and random.
static CoapsSlot *open_conn(uint8_t *restrict work, const char *ip, uint16_t port)
{
    CoapsSlot *s = alloc_slot(work);
    if (!s)
    {
        return NULL;
    }
    COAPS_SERVER_CTX(work)->rng(s->eph, sizeof(s->eph));     // the X25519 ephemeral private key
    COAPS_SERVER_CTX(work)->rng(s->srand, sizeof(s->srand)); // the ServerHello random
    s->cfg.cert_der = COAPS_SERVER_CTX(work)->cert_der;
    s->cfg.cert_len = COAPS_SERVER_CTX(work)->cert_len;
    s->cfg.ed25519_seed = COAPS_SERVER_CTX(work)->ed25519_seed;
    s->cfg.ephemeral_priv = s->eph;
    s->cfg.server_random = s->srand;
    s->cfg.cookie_key = COAPS_SERVER_CTX(work)->cookie_key;
    uint8_t paddr[PROTOCORE_COAPS_PEER_SER];
    proto_bool bound = serialize_peer(ip, port, paddr);
    DtlsServerV.init_args.c = &s->conn;
    DtlsServerV.init_args.cfg = &s->cfg;
    DtlsServerV.init_args.peer_addr = bound ? paddr : NULL;
    DtlsServerV.init_args.peer_addr_len = bound ? sizeof(paddr) : 0;
    DtlsServer.init(dtls_server_work);
    (void)str.copy(s->peer_ip, ip, sizeof(s->peer_ip));
    s->peer_port = port;
    return s;
}

// ---------------------------------------------------------------------------
// Sending, and the two halves of one poll
// ---------------------------------------------------------------------------

// Put len octets on the wire to ip:port, from the bound port where there is one.
static void server_send(uint8_t *restrict work, const char *ip, uint16_t port, const uint8_t *data, size_t len)
{
#if PROTOCORE_HAS_NET_STACK
    protocore_ip dst = {PROTOCORE_IP_NONE, {0}};
    Ip.args.text = ip;
    Ip.args.out = &dst;
    Ip.parse(ip_work);
    if (!Ip.ok)
    {
        return;
    }
    UdpListenerV.port = COAPS_SERVER_CTX(work)->port;
    UdpListenerV.send_args.dst = &dst;
    UdpListenerV.send_args.dst_port = port;
    UdpListenerV.send_args.data = data;
    UdpListenerV.send_args.len = len;
    UdpListener.sendto(protocore_udp_listener_span());
#else
    if (COAPS_SERVER_CTX(work)->out_sink)
    {
        COAPS_SERVER_CTX(work)->out_sink(COAPS_SERVER_CTX(work)->out_ctx, data, len, ip, port);
    }
#endif
}

// Route one queued datagram to its connection, opening one for a new peer's ClientHello, and drive
// the handshake or the CoAP exchange through the bridge.
static void route_datagram(uint8_t *restrict work, const CoapsIngest *ig, uint32_t now, uint8_t *out, size_t out_cap)
{
    // A record carrying a Connection ID (RFC 9147 sec 4 Figure 3, the C bit) routes by that id, so a
    // peer that moved to a new address still reaches its connection (RFC 9146, RFC 9147 sec 9).
    // Everything else routes by source address, and a new peer's plaintext ClientHello opens a slot.
    proto_bool cid_rec =
        ig->len >= 1 && (ig->data[0] & COAPS_UHDR_FIXED_MASK) == COAPS_UHDR_FIXED && (ig->data[0] & COAPS_UHDR_CID);
    CoapsSlot *s = cid_rec ? slot_by_cid(work, ig->data + 1, ig->len - 1) : NULL;
    if (!s)
    {
        s = slot_by_peer(work, ig->ip, ig->port);
    }
    if (!s && !cid_rec)
    {
        s = open_conn(work, ig->ip, ig->port);
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
    CoapsV.conn = &s->conn;
    CoapsV.dgram.data = ig->data;
    CoapsV.dgram.len = ig->len;
    CoapsV.dgram.out = out;
    CoapsV.dgram.out_cap = out_cap;
    Coaps.process(coaps_work);
    if (CoapsV.i32 > 0)
    {
        server_send(work, s->peer_ip, s->peer_port, out, (size_t)CoapsV.i32);
    }
    else if (CoapsV.i32 < 0)
    {
        s->used = PROTO_FALSE; // the handshake failed, so the slot goes back
    }
}

// Fire the retransmission timer for a slot's outstanding flight (RFC 9147 sec 5.8), then reclaim the
// slot if the handshake gave up or the connection has gone quiet.
static void service_slot(uint8_t *restrict work, CoapsSlot *s, uint32_t now, uint8_t *out, size_t out_cap)
{
    if (!s->used)
    {
        return;
    }
    DtlsServerV.timeout_ms_args.c = &s->conn;
    DtlsServer.timeout_ms(dtls_server_work);
    if (DtlsServerV.n == 0) // 0 is due now, -1 is no timer, above 0 is still pending
    {
        DtlsServerV.on_timeout_args.c = &s->conn;
        DtlsServerV.on_timeout_args.out = out;
        DtlsServerV.on_timeout_args.out_cap = out_cap;
        DtlsServer.on_timeout(dtls_server_work);
        int n = DtlsServerV.n;
        if (n > 0)
        {
            server_send(work, s->peer_ip, s->peer_port, out, (size_t)n);
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
    UdpListenerV.peer_args.peer = peer;
    UdpListenerV.peer_args.ip_out = ip;
    UdpListenerV.peer_args.ip_cap = sizeof(ip);
    UdpListenerV.peer_args.port_out = &port;
    UdpListener.peer_addr(protocore_udp_listener_span());
    if (!UdpListenerV.ok)
    {
        return;
    }
    (void)ring_push(protocore_coaps_server_span(), data, len, ip, port);
}
#endif

// ---------------------------------------------------------------------------
// The bodies behind the table
// ---------------------------------------------------------------------------

// Install ns->identity, bind ns->bind.port, and route its datagrams into the pool.
void protocore_coaps_server_begin(uint8_t *restrict work)
{
    CoapsServerV.ok = PROTO_FALSE;
    if (!CoapsServerV.identity.rng || !CoapsServerV.identity.cert_der || CoapsServerV.identity.cert_len == 0)
    {
        return;
    }
    COAPS_SERVER_CTX(work)->cert_der = CoapsServerV.identity.cert_der;
    COAPS_SERVER_CTX(work)->cert_len = CoapsServerV.identity.cert_len;
    mem.cpy(COAPS_SERVER_CTX(work)->ed25519_seed, CoapsServerV.identity.ed25519_seed,
            sizeof(COAPS_SERVER_CTX(work)->ed25519_seed));
    mem.cpy(COAPS_SERVER_CTX(work)->cookie_key, CoapsServerV.identity.cookie_key,
            sizeof(COAPS_SERVER_CTX(work)->cookie_key));
    COAPS_SERVER_CTX(work)->rng = CoapsServerV.identity.rng;
    COAPS_SERVER_CTX(work)->port = CoapsServerV.bind.port ? CoapsServerV.bind.port : PROTOCORE_COAPS_PORT;
    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        COAPS_SERVER_CTX(work)->pool[i].used = PROTO_FALSE;
    }
    COAPS_SERVER_CTX(work)->ring_head = 0;
    COAPS_SERVER_CTX(work)->ring_tail = 0;
    COAPS_SERVER_CTX(work)->running = PROTO_TRUE;
#if PROTOCORE_HAS_NET_STACK
    UdpListenerV.port = COAPS_SERVER_CTX(work)->port;
    UdpListenerV.bind.handler = udp_ingest_cb;
    UdpListenerV.bind.handler_ctx = NULL;
    UdpListener.listen(protocore_udp_listener_span());
    CoapsServerV.ok = UdpListenerV.ok;
#else
    CoapsServerV.ok = PROTO_TRUE; // fed through ingest instead
#endif
}

// Drain the queued datagrams, then service every slot.
void protocore_coaps_server_poll(uint8_t *restrict work)
{
    CoapsServerV.ok = PROTO_FALSE;
    if (!COAPS_SERVER_CTX(work)->running)
    {
        return;
    }
    const uint32_t now = Clock.ms;
    uint8_t out[PROTOCORE_COAPS_OUT_CAP];

    CoapsIngest ig;
    while (ring_pop(work, &ig))
    {
        route_datagram(work, &ig, now, out, sizeof(out));
    }

    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        service_slot(work, &COAPS_SERVER_CTX(work)->pool[i], now, out, sizeof(out));
    }
    CoapsServerV.ok = PROTO_TRUE;
}

// The pool slots in use.
void protocore_coaps_server_active_conns(uint8_t *restrict work)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        if (COAPS_SERVER_CTX(work)->pool[i].used)
        {
            n++;
        }
    }
    CoapsServerV.u8 = n;
    CoapsServerV.ok = PROTO_TRUE;
}

// Stop polling, release every slot, and empty the ingest ring. The port stays bound.
void protocore_coaps_server_stop(uint8_t *restrict work)
{
    COAPS_SERVER_CTX(work)->running = PROTO_FALSE;
    for (uint8_t i = 0; i < PROTOCORE_COAPS_MAX_CONNS; i++)
    {
        COAPS_SERVER_CTX(work)->pool[i].used = PROTO_FALSE;
    }
    COAPS_SERVER_CTX(work)->ring_head = 0;
    COAPS_SERVER_CTX(work)->ring_tail = 0;
    CoapsServerV.ok = PROTO_TRUE;
}

#if !PROTOCORE_HAS_NET_STACK
// Install ns->sink as where every outbound datagram goes.
void protocore_coaps_server_set_out_sink(uint8_t *restrict work)
{
    COAPS_SERVER_CTX(work)->out_sink = CoapsServerV.sink.fn;
    COAPS_SERVER_CTX(work)->out_ctx = CoapsServerV.sink.ctx;
    CoapsServerV.ok = PROTO_TRUE;
}

// Queue ns->dgram as though it had been received, for the next poll to route.
void protocore_coaps_server_ingest(uint8_t *restrict work)
{
    CoapsServerV.ok = ring_push(work, CoapsServerV.dgram.data, CoapsServerV.dgram.len, CoapsServerV.dgram.ip,
                                CoapsServerV.dgram.port);
}
#endif

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
CoapsServerVars CoapsServerV;

#endif // PROTOCORE_ENABLE_DTLS && PROTOCORE_ENABLE_COAP
