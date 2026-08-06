// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file coaps_server.c
 * @brief CoAP-over-DTLS server front-end - implementation. See coaps_server.h.
 */

#include "services/iot/coap/coaps_server.h"

#if PC_ENABLE_DTLS && PC_ENABLE_COAP

#include "mmgr/ring.h" // pc_atomic (SPSC ingest-ring cursors)
#include "network_drivers/presentation/security/dtls/dtls_conn.h"
#include "server/clock/clock.h"      // pc_millis() - idle-reap clock (the DTLS PTO uses it internally too)
#include "services/iot/coap/coaps.h" // pc_coaps_process()

#if PROTOCORE_HOT
#include "network_drivers/transport/udp.h"
#endif

// Largest inbound datagram buffered: a ClientHello, a client Finished, or one CoAP application record
// (CoAP messages fit a single datagram, RFC 7252 §4.6). The server's outbound flight is larger but is
// sent straight out, never buffered here.
#ifndef PC_COAPS_MAX_DATAGRAM
#define PC_COAPS_MAX_DATAGRAM 1500
#endif
// Scratch for one poll's outbound datagram: a full server flight (ServerHello..Finished) or a sealed
// CoAP response. The Certificate-dominated flight is the largest thing written here.
#define PC_COAPS_OUT_CAP 2048
// The HelloRetryRequest cookie binds the peer's IPv4 address (4) + port (2); the transport is IPv4.
#define PC_COAPS_PEER_SER 6

// One buffered inbound datagram (payload + the peer it arrived from).
typedef struct
{
    uint8_t data[PC_COAPS_MAX_DATAGRAM];
    uint16_t len;
    char ip[16];
    uint16_t port;
} CoapsIngest;

// One pool slot: a DTLS connection plus the per-connection key material its config points at (so the
// config's pointers outlive the DtlsConn) and the peer to reply to.
typedef struct
{
    proto_bool used;
    DtlsConn conn;
    DtlsServerConfig cfg; ///< per-slot config; its ephemeral/random pointers reference the buffers below
    uint8_t eph[32];      ///< fresh X25519 ephemeral private key for this handshake
    uint8_t srand[32];    ///< fresh ServerHello random for this handshake
    char peer_ip[16];
    uint16_t peer_port;
    uint32_t last_ms; ///< pc_millis() of the last inbound datagram (idle-reaping clock)
} CoapsSlot;

// The DtlsConn pool + the ingest ring, owned by one instance (internal linkage). Kept separate from
// the DRAM control state below so the large buffers group together (and could move to PSRAM like the
// HTTP/3 pool if a large pool were ever wanted). One named owner, unreachable cross-TU.
typedef struct
{
    CoapsSlot pool[PC_COAPS_MAX_CONNS];
    CoapsIngest ring[PC_COAPS_INGEST_RING];
} CoapsServerPoolCtx;
static CoapsServerPoolCtx s_cpool;

// All CoAPs-server control state, owned by one instance (internal linkage): the ingest-ring cursors,
// the server identity + randomness source, the bound port / running flag, and (host) the outbound
// sink. One named owner, unreachable cross-TU.
typedef struct
{
    _Atomic size_t ring_head; ///< producer (udp / ingest) advances
    _Atomic size_t ring_tail; ///< consumer (poll) advances
    const uint8_t *cert_der;
    size_t cert_len;
    uint8_t ed25519_seed[32];
    uint8_t cookie_key[32];
    void (*rng)(uint8_t *out, size_t len);
    uint16_t port;
    proto_bool running;
#if !PROTOCORE_HOT
    CoapsServerOutFn out_sink;
    void *out_ctx;
#endif
} CoapsServerCtx;
static CoapsServerCtx s_coaps;

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

// Serialize an IPv4 dotted-quad @p ip + @p port into the address the HelloRetryRequest cookie binds, so
// a cookie minted for one peer is worthless to another (RFC 9147 §5.1). Returns false on a malformed
// address (then no address is bound and the peer simply cannot take the HRR path).
static proto_bool serialize_peer(const char *ip, uint16_t port, uint8_t out[PC_COAPS_PEER_SER])
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

static void server_send(const char *ip, uint16_t port, const uint8_t *data, size_t len)
{
#if PROTOCORE_HOT
    pc_ip dst = {PC_IP_NONE, {0}};
    if (Ip.parse(ip, &dst))
    {
        Udp.listener->sendto(s_coaps.port, &dst, port, data, len);
    }
#else
    if (s_coaps.out_sink)
    {
        s_coaps.out_sink(s_coaps.out_ctx, data, len, ip, port);
    }
#endif
}

// --- ingest ring (SPSC: one producer fills, pc_coaps_server_poll consumes) ------------------------
static proto_bool ring_push(const uint8_t *dg, size_t len, const char *ip, uint16_t port)
{
    if (len == 0 || len > PC_COAPS_MAX_DATAGRAM)
    {
        return PROTO_FALSE;
    }
    size_t head = s_coaps.ring_head;
    size_t next = (head + 1) % PC_COAPS_INGEST_RING;
    if (next == (size_t)s_coaps.ring_tail)
    {
        return PROTO_FALSE; // ring full: drop (DTLS recovers via retransmission)
    }
    CoapsIngest *e = &s_cpool.ring[head];
    memcpy(e->data, dg, len);
    e->len = (uint16_t)len;
    copy_str(e->ip, sizeof e->ip, ip);
    e->port = port;
    s_coaps.ring_head = next; // publish after the record is fully written
    return PROTO_TRUE;
}

static proto_bool ring_pop(CoapsIngest *out)
{
    size_t tail = s_coaps.ring_tail;
    if (tail == (size_t)s_coaps.ring_head)
    {
        return PROTO_FALSE;
    }
    *out = s_cpool.ring[tail];
    s_coaps.ring_tail = (tail + 1) % PC_COAPS_INGEST_RING;
    return PROTO_TRUE;
}

// --- slot pool (keyed by peer address, or by connection id once one is negotiated) -------------
static CoapsSlot *slot_by_peer(const char *ip, uint16_t port)
{
    for (uint8_t i = 0; i < PC_COAPS_MAX_CONNS; i++)
    {
        CoapsSlot *s = &s_cpool.pool[i];
        if (s->used && s->peer_port == port && strcmp(s->peer_ip, ip) == 0)
        {
            return s;
        }
    }
    return NULL;
}

// Find the connection whose negotiated connection id (RFC 9146 / RFC 9147 §9) matches the id carried in
// an inbound CID record, so a peer that has roamed to a new address is still routed to its connection.
// @p cid points just past the record's first byte; @p avail is the bytes available there.
static CoapsSlot *slot_by_cid(const uint8_t *cid, size_t avail)
{
    uint8_t sc[PC_DTLS_CID_MAX];
    for (uint8_t i = 0; i < PC_COAPS_MAX_CONNS; i++)
    {
        CoapsSlot *s = &s_cpool.pool[i];
        if (!s->used)
        {
            continue;
        }
        size_t sl = DtlsServer.local_cid(&s->conn, sc);
        if (sl && sl <= avail && memcmp(cid, sc, sl) == 0)
        {
            return s;
        }
    }
    return NULL;
}

static CoapsSlot *alloc_slot()
{
    for (uint8_t i = 0; i < PC_COAPS_MAX_CONNS; i++)
    {
        if (!s_cpool.pool[i].used)
        {
            CoapsSlot *s = &s_cpool.pool[i];
            memset(s, 0, sizeof *s);
            s->used = PROTO_TRUE;
            return s;
        }
    }
    return NULL;
}

// Open a connection for a datagram from a peer with no existing slot.
static CoapsSlot *open_conn(const char *ip, uint16_t port)
{
    CoapsSlot *s = alloc_slot();
    if (!s)
    {
        return NULL;
    }
    s_coaps.rng(s->eph, sizeof s->eph);     // fresh X25519 ephemeral private key
    s_coaps.rng(s->srand, sizeof s->srand); // fresh ServerHello random
    s->cfg.cert_der = s_coaps.cert_der;
    s->cfg.cert_len = s_coaps.cert_len;
    s->cfg.ed25519_seed = s_coaps.ed25519_seed;
    s->cfg.ephemeral_priv = s->eph;
    s->cfg.server_random = s->srand;
    s->cfg.cookie_key = s_coaps.cookie_key;
    uint8_t paddr[PC_COAPS_PEER_SER];
    proto_bool ok = serialize_peer(ip, port, paddr);
    DtlsServer.init(&s->conn, &s->cfg, ok ? paddr : NULL, ok ? sizeof paddr : 0);
    copy_str(s->peer_ip, sizeof s->peer_ip, ip);
    s->peer_port = port;
    return s;
}

#if PROTOCORE_HOT
static void udp_ingest_cb(const uint8_t *data, size_t len, const struct pc_udp_peer *peer, void *ctx)
{
    (void)ctx;
    char ip[16];
    uint16_t port = 0;
    if (!Udp.listener->peer_addr(peer, ip, sizeof ip, &port))
    {
        return;
    }
    ring_push(data, len, ip, port);
}
#endif

// HttpRoute one ingested datagram to its peer slot (opening one for a fresh peer's ClientHello) and drive
// the DTLS handshake / CoAP exchange through the bridge.
static void coaps_route_datagram(const CoapsIngest *ig, uint32_t now, uint8_t *out, size_t out_cap)
{
    // A DTLSCiphertext with the C bit (0b001C....) carries a connection id: route by it so a peer that
    // has roamed to a new address still reaches its connection (RFC 9146 / RFC 9147 §9). Otherwise route
    // by source address; a fresh peer's (plaintext) ClientHello opens a slot.
    proto_bool cid_rec = ig->len >= 1 && (ig->data[0] & 0xE0) == 0x20 && (ig->data[0] & 0x10);
    CoapsSlot *s = cid_rec ? slot_by_cid(ig->data + 1, ig->len - 1) : NULL;
    if (!s)
    {
        s = slot_by_peer(ig->ip, ig->port);
    }
    if (!s && !cid_rec)
    {
        s = open_conn(ig->ip, ig->port);
    }
    if (!s)
    {
        return; // unknown connection id, or pool full: drop (the peer retransmits / DTLS PTO recovers)
    }
    // Address migration: a valid CID record from a new address updates where we send replies.
    if (cid_rec && (s->peer_port != ig->port || strcmp(s->peer_ip, ig->ip) != 0))
    {
        copy_str(s->peer_ip, sizeof s->peer_ip, ig->ip);
        s->peer_port = ig->port;
    }
    s->last_ms = now;
    int n = pc_coaps_process(&s->conn, ig->data, ig->len, out, out_cap);
    if (n > 0)
    {
        server_send(s->peer_ip, s->peer_port, out, (size_t)n);
    }
    else if (n < 0)
    {
        s->used = PROTO_FALSE; // fatal handshake error: free the slot
    }
}

// Fire the retransmission timer for a slot's outstanding flight, then reap it if the handshake failed or
// the connection has gone idle.
static void coaps_service_slot(CoapsSlot *s, uint32_t now, uint8_t *out, size_t out_cap)
{
    if (!s->used)
    {
        return;
    }
    if (DtlsServer.timeout_ms(&s->conn) == 0) // 0 == due now (-1 == no timer, >0 == still pending)
    {
        int n = DtlsServer.on_timeout(&s->conn, out, out_cap);
        if (n > 0)
        {
            server_send(s->peer_ip, s->peer_port, out, (size_t)n);
        }
        else if (n < 0)
        {
            s->used = PROTO_FALSE; // retransmission ceiling hit: abandon the handshake
            return;
        }
    }
    if (now - s->last_ms >= PC_COAPS_IDLE_MS) // wrap-safe idle delta (uint32_t arithmetic)
    {
        s->used = PROTO_FALSE;
    }
}

proto_bool pc_coaps_server_begin(uint16_t port, const CoapsServerConfig *cfg)
{
    if (!cfg || !cfg->rng || !cfg->cert_der || cfg->cert_len == 0)
    {
        return PROTO_FALSE;
    }
    s_coaps.cert_der = cfg->cert_der;
    s_coaps.cert_len = cfg->cert_len;
    memcpy(s_coaps.ed25519_seed, cfg->ed25519_seed, sizeof s_coaps.ed25519_seed);
    memcpy(s_coaps.cookie_key, cfg->cookie_key, sizeof s_coaps.cookie_key);
    s_coaps.rng = cfg->rng;
    s_coaps.port = port ? port : PC_COAPS_PORT;
    for (uint8_t i = 0; i < PC_COAPS_MAX_CONNS; i++)
    {
        s_cpool.pool[i].used = PROTO_FALSE;
    }
    s_coaps.ring_head = 0;
    s_coaps.ring_tail = 0;
    s_coaps.running = PROTO_TRUE;
#if PROTOCORE_HOT
    return Udp.listener->listen(s_coaps.port, udp_ingest_cb, NULL);
#else
    return PROTO_TRUE; // host: fed through pc_coaps_server_ingest()
#endif
}

void pc_coaps_server_poll()
{
    if (!s_coaps.running)
    {
        return;
    }
    uint32_t now = pc_millis();
    uint8_t out[PC_COAPS_OUT_CAP];

    // Drain queued datagrams: route each to its peer's slot (opening one for a new peer) and drive the
    // handshake / CoAP exchange through the bridge.
    CoapsIngest ig;
    while (ring_pop(&ig))
    {
        coaps_route_datagram(&ig, now, out, sizeof out);
    }

    // Fire the retransmission timer for any outstanding flight, then reap closed / idle connections.
    for (uint8_t i = 0; i < PC_COAPS_MAX_CONNS; i++)
    {
        coaps_service_slot(&s_cpool.pool[i], now, out, sizeof out);
    }
}

uint8_t pc_coaps_server_active_conns()
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < PC_COAPS_MAX_CONNS; i++)
    {
        if (s_cpool.pool[i].used)
        {
            n++;
        }
    }
    return n;
}

void pc_coaps_server_stop()
{
    s_coaps.running = PROTO_FALSE;
    for (uint8_t i = 0; i < PC_COAPS_MAX_CONNS; i++)
    {
        s_cpool.pool[i].used = PROTO_FALSE;
    }
    s_coaps.ring_head = 0;
    s_coaps.ring_tail = 0;
}

#if !PROTOCORE_HOT
void pc_coaps_server_set_out_sink_cb(CoapsServerOutFn fn, void *ctx)
{
    s_coaps.out_sink = fn;
    s_coaps.out_ctx = ctx;
}

proto_bool pc_coaps_server_ingest(const uint8_t *datagram, size_t len, const char *ip, uint16_t port)
{
    return ring_push(datagram, len, ip, port);
}
#endif

#endif // PC_ENABLE_DTLS && PC_ENABLE_COAP
