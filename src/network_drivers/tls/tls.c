// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls.c
 * @brief The per-slot TLS connections, and the slot-indexed surface over them. See tls.h.
 *
 * handshake/ drives one ::TlsConn and record/ frames for it; neither knows what a connection slot
 * is. This file is the table that joins the two, so a caller that has a slot index reaches the
 * connection standing on it.
 *
 * The joining is a BIO over the transport, the same shape the vendor arm uses: ciphertext is read
 * out of the slot's receive ring, and whatever the handshake owes goes back through the transport's
 * context-safe raw write. The pcb is captured when the connection begins, because a response sender
 * nulls TcpConn::pcb before it writes.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_TLS

#include "network_drivers/tls/tls.h"

#include "crypto/rng/rng.h"                                  // Rng.fill: the per-handshake ephemeral and random
#include "mmgr/secure/secure.h"                              // the persistent end this module's state is taken from
#include "network_drivers/tls/record/record.h"               // PROTOCORE_TLS_PLAINTEXT_HDR_LEN: the frame this reads
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the ring and the raw write

PROTOCORE_BEGIN_DECLS

/**
 * @brief The layer's compile-time storage: one connection per slot, and what a handshake needs.
 *
 * A TlsConn carries its traffic keys inline, and the credential this end signs with sits here too,
 * so these bytes are key material and take the end whose release wipes.
 */
struct TlsStorage
{
    TlsConn conn[MAX_CONNS];
    TlsConnConfig cfg[MAX_CONNS];            ///< per connection: the shared credential plus its own randomness
    protocore_pcb *pcb[MAX_CONNS];           ///< captured at begin; a response sender nulls TcpConn::pcb
    uint8_t eph[MAX_CONNS][32];              ///< X25519 ephemeral private key, fresh per handshake (RFC 8446 sec 4.2.8)
    uint8_t rnd[MAX_CONNS][32];              ///< Hello random, fresh per handshake (sec 4.1.2)
    uint8_t out[PROTOCORE_TLS_SEAM_OUT_CAP]; ///< what one pump owes the wire, built then sent
    uint8_t ed_seed[32];                     ///< the Ed25519 signing seed this end presents (RFC 7250)
    uint8_t ed_pub[32];                      ///< the raw public key that matches it
    proto_bool ready;                        ///< a credential is installed
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every region is
// that pointer plus a compile-time offset, so the assert below proves the span covers them before
// anything runs.
#define TLS_OFF_CTX 0u
static_assert(TLS_OFF_CTX + sizeof(struct TlsStorage) <= PROTOCORE_TLS_BORROW,
              "PROTOCORE_TLS_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define TLS_CTX(w) ((struct TlsStorage *)(void *)((w) + TLS_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_TLS_BORROW persistent bytes
} TlsOwnCtx;
static TlsOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_tls_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_TLS_BORROW).buf;
    }
    return s_own.span; // null while the pool was short, which every call refuses
}

TlsConn *protocore_tls_conn_at(uint8_t slot)
{
    uint8_t *work = protocore_tls_span();
    return (work && slot < MAX_CONNS) ? &TLS_CTX(work)->conn[slot] : NULL;
}

const char *protocore_tls_alpn(uint8_t slot)
{
    const TlsConn *c = protocore_tls_conn_at(slot);
    return c ? c->alpn : NULL;
}

// ---------------------------------------------------------------------------
// The BIO: ciphertext out of the slot's ring, and back through the raw write
// ---------------------------------------------------------------------------

// One whole TLS record out of the slot's receive ring into the connection's RX region. The header
// carries the fragment length (RFC 8446 sec 5.1), so a record that has not fully arrived is left
// where it is and read again on the next pump - the ring is not consumed until the whole record is
// there. Reports the record's total length, or 0 when it is not yet complete.
static size_t frame_one(uint8_t *restrict work, uint8_t slot, TlsConn *c)
{
    ConnPoolV.slot = slot;
    ConnPool.available(protocore_conn_pool_span());
    size_t have = ConnPoolV.n;
    if (have < PROTOCORE_TLS_PLAINTEXT_HDR_LEN)
    {
        return 0; // not even a header yet
    }

    uint8_t hdr[PROTOCORE_TLS_PLAINTEXT_HDR_LEN];
    ConnPoolV.slot = slot;
    ConnPoolV.io.off = 0;
    ConnPoolV.io.buf = hdr;
    ConnPoolV.io.count = sizeof(hdr);
    ConnPool.peek(protocore_conn_pool_span()); // peek: the ring keeps them until the whole record is in

    const size_t frag = ((size_t)hdr[3] << 8) | (size_t)hdr[4];
    const size_t total = PROTOCORE_TLS_PLAINTEXT_HDR_LEN + frag;
    if (frag > PROTOCORE_TLS_CONN_REC_CAP - PROTOCORE_TLS_PLAINTEXT_HDR_LEN)
    {
        return 0; // longer than sec 5.1 allows this build to hold; the caller's alert path ends it
    }
    if (have < total)
    {
        return 0; // the rest is still in flight
    }

    ConnPoolV.slot = slot;
    ConnPoolV.io.buf = c->rx;
    ConnPoolV.io.cap = total;
    ConnPool.read(protocore_conn_pool_span());
    (void)work;
    return ConnPoolV.n;
}

// Whatever the handshake owes, through the transport's context-safe raw write. The pump runs on a
// worker, not in the stack's thread, so this is the one write it may make.
static proto_bool emit(uint8_t *restrict work, uint8_t slot, size_t len)
{
    if (len == 0)
    {
        return PROTO_TRUE;
    }
    protocore_pcb *pcb = TLS_CTX(work)->pcb[slot];
    if (pcb == NULL)
    {
        return PROTO_FALSE;
    }
    ConnPoolV.pcb = pcb;
    ConnPoolV.io.data = TLS_CTX(work)->out;
    ConnPoolV.io.len = (proto_u16)len;
    ConnPool.raw_send(protocore_conn_pool_span());
    return ConnPoolV.ok;
}

// ---------------------------------------------------------------------------
// The slot-indexed surface
// ---------------------------------------------------------------------------

proto_bool protocore_tls_global_init(const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len)
{
    uint8_t *work = protocore_tls_span();
    // RFC 7250 raw public keys: this engine presents an Ed25519 key, not an X.509 chain, so the
    // credential is the 32-byte public key and the 32-byte signing seed that matches it.
    if (cert == NULL || key == NULL || cert_len != 32u || key_len != 32u)
    {
        return PROTO_FALSE;
    }
    raw.read(TLS_CTX(work)->ed_pub, cert, 32u);
    raw.read(TLS_CTX(work)->ed_seed, key, 32u);
    TLS_CTX(work)->ready = PROTO_TRUE;
    return PROTO_TRUE;
}

proto_bool protocore_tls_ready(void)
{
    uint8_t *work = protocore_tls_span();
    return work ? TLS_CTX(work)->ready : PROTO_FALSE;
}

proto_bool protocore_tls_conn_begin(uint8_t slot)
{
    uint8_t *work = protocore_tls_span();
    if (slot >= MAX_CONNS || !TLS_CTX(work)->ready)
    {
        return PROTO_FALSE;
    }

    // Fresh per handshake, from the one generator: the X25519 ephemeral private key (sec 4.2.8) and
    // the Hello random (sec 4.1.2). A repeat of either across connections would be a key reuse.
    RngV.fill_args.out = TLS_CTX(work)->eph[slot];
    RngV.fill_args.len = 32u;
    Rng.fill(protocore_rng_span());
    RngV.fill_args.out = TLS_CTX(work)->rnd[slot];
    RngV.fill_args.len = 32u;
    Rng.fill(protocore_rng_span());

    TlsConnConfig *cfg = &TLS_CTX(work)->cfg[slot];
    cfg->ed25519_seed = TLS_CTX(work)->ed_seed;
    cfg->ed25519_pub = TLS_CTX(work)->ed_pub;
    cfg->peer_pub = NULL; // a server verifies a client only under mTLS, which sets it
    cfg->ephemeral_priv = TLS_CTX(work)->eph[slot];
    cfg->random = TLS_CTX(work)->rnd[slot];
    cfg->hostname = NULL; // server: the SNI is the client's to offer

    ConnPoolV.slot = slot;
    ConnPool.pcb_of(protocore_conn_pool_span());
    TLS_CTX(work)->pcb[slot] = ConnPoolV.pcb;

    TlsConnectionV.conn = &TLS_CTX(work)->conn[slot];
    TlsConnectionV.init_args.role = TLS_ROLE_SERVER;
    TlsConnectionV.init_args.cfg = cfg;
    TlsConnection.init(protocore_tls_span());
    return TlsConnectionV.ok;
}

int protocore_tls_handshake(uint8_t slot)
{
    uint8_t *work = protocore_tls_span();
    if (slot >= MAX_CONNS)
    {
        return -1;
    }
    TlsConn *c = &TLS_CTX(work)->conn[slot];

    // One record per turn of this loop: the ring may hold a whole flight, and the handshake wants
    // them one at a time.
    for (;;)
    {
        size_t rec = frame_one(work, slot, c);
        if (rec == 0)
        {
            break; // nothing whole to feed; ask again when more ciphertext lands
        }

        TlsConnectionV.conn = c;
        TlsConnectionV.io.rx_len = rec;
        TlsConnectionV.out_args.out = TLS_CTX(work)->out;
        TlsConnectionV.out_args.out_cap = sizeof(TLS_CTX(work)->out);
        TlsConnection.process(protocore_tls_span());
        const int wrote = TlsConnectionV.i32;
        if (wrote < 0)
        {
            return -1; // the driver failed the connection and recorded its alert in TlsConn::alert
        }
        if (!emit(work, slot, (size_t)wrote))
        {
            return -1;
        }
    }

    TlsConnectionV.conn = c;
    TlsConnection.established(protocore_tls_span());
    return TlsConnectionV.ok ? 1 : 0;
}

proto_bool protocore_tls_established(uint8_t slot)
{
    uint8_t *work = protocore_tls_span();
    if (slot >= MAX_CONNS)
    {
        return PROTO_FALSE;
    }
    TlsConnectionV.conn = &TLS_CTX(work)->conn[slot];
    TlsConnection.established(protocore_tls_span());
    return TlsConnectionV.ok;
}

int protocore_tls_read(uint8_t slot, uint8_t *buf, size_t len)
{
    uint8_t *work = protocore_tls_span();
    if (slot >= MAX_CONNS || buf == NULL)
    {
        return -1;
    }
    TlsConn *c = &TLS_CTX(work)->conn[slot];
    size_t rec = frame_one(work, slot, c);
    if (rec == 0)
    {
        return 0; // no whole record yet
    }

    size_t out_len = 0;
    TlsConnectionV.conn = c;
    TlsConnectionV.io.rec = c->rx;
    TlsConnectionV.io.rec_len = rec;
    TlsConnectionV.out_args.out = buf;
    TlsConnectionV.out_args.out_cap = len;
    TlsConnectionV.out_args.out_len = &out_len;
    TlsConnection.open_app(protocore_tls_span());
    if (!TlsConnectionV.ok)
    {
        return -1; // an AEAD failure ends the connection (RFC 8446 sec 5.2)
    }
    return (int)out_len;
}

int protocore_tls_write(uint8_t slot, const void *data, size_t len)
{
    uint8_t *work = protocore_tls_span();
    if (slot >= MAX_CONNS || data == NULL)
    {
        return -1;
    }
    TlsConnectionV.conn = &TLS_CTX(work)->conn[slot];
    TlsConnectionV.io.data = (const uint8_t *)data;
    TlsConnectionV.io.len = len;
    TlsConnectionV.out_args.out = TLS_CTX(work)->out;
    TlsConnectionV.out_args.out_cap = sizeof(TLS_CTX(work)->out);
    TlsConnection.seal_app(protocore_tls_span());
    if (TlsConnectionV.n == 0)
    {
        return -1; // the fragment did not fit one record
    }
    if (!emit(work, slot, TlsConnectionV.n))
    {
        return -1;
    }
    return (int)len;
}

void protocore_tls_conn_end(uint8_t slot)
{
    uint8_t *work = protocore_tls_span();
    if (slot >= MAX_CONNS)
    {
        return;
    }
    // RFC 8446 sec 6.1 makes close_notify a SHOULD, and this engine has no call that emits one:
    // TlsConnNs::alert reports the alert that ended a connection, it does not send one. The
    // connection is ended by freeing its keys, and the peer sees the transport close.
    protocore_tls_conn_free(slot);
}

void protocore_tls_conn_free(uint8_t slot)
{
    uint8_t *work = protocore_tls_span();
    if (slot >= MAX_CONNS)
    {
        return;
    }
    TlsConn *c = &TLS_CTX(work)->conn[slot];

    // The four key generations this connection installed. The storage is this module's and is
    // reused by the next connection on the slot, so it is wiped rather than merely forgotten.
    TlsRecordV.key.keys = &c->hs_tx;
    TlsRecord.keys_wipe(protocore_tls_span());
    TlsRecordV.key.keys = &c->hs_rx;
    TlsRecord.keys_wipe(protocore_tls_span());
    TlsRecordV.key.keys = &c->ap_tx;
    TlsRecord.keys_wipe(protocore_tls_span());
    TlsRecordV.key.keys = &c->ap_rx;
    TlsRecord.keys_wipe(protocore_tls_span());

    mem.set(&TLS_CTX(work)->cfg[slot], 0, sizeof(TLS_CTX(work)->cfg[slot]));
    mem.set(TLS_CTX(work)->eph[slot], 0, 32u);
    mem.set(TLS_CTX(work)->rnd[slot], 0, 32u);
    TLS_CTX(work)->pcb[slot] = NULL;
    c->state = TLS_CONN_START;
    c->hs_keys_ready = PROTO_FALSE;
    c->ap_keys_ready = PROTO_FALSE;
}

size_t protocore_tls_arena_peak(void)
{
    return protocore_secure_high_water();
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_TLS
