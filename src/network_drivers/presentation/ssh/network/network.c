// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file network.c
 * @brief Ring buffer in, framed bytes out; the socket slot and the SSH slot bound together.
 */

#include "network_drivers/presentation/ssh/network/network.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "network_drivers/presentation/ssh/auth/auth.h"
#include "network_drivers/presentation/ssh/common.h"
#include "network_drivers/presentation/ssh/connection/connection.h"
#include "network_drivers/presentation/ssh/ssh.h" // ssh_conn_slot() + the memory map
#include "network_drivers/presentation/ssh/transport/transport/transport.h"
#if PROTOCORE_ENABLE_SSH_ZLIB
#include "network_drivers/presentation/ssh/transport/comp/comp.h"
#endif
#include "network_drivers/transport/tcp/client/client.h"     // TcpClient: a dialled stream
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: an accepted stream
#include "server/clock/clock.h"                              // protocore_millis() for the re-key timer
#include "server/core/proto_handler.h"
#include "server/core/worker/worker.h" // Workers.wake(): the owner drains the flagged packet

// The network layer's state, owned by one instance (internal linkage): the SSH-slot -> socket-slot
// mapping (0xFF = free) and the one-time init flag. The bytes a connection uses are ssh.c's, and
// whether a connection must be torn down is the driving role's, not the stream's.
struct SshNetworkStorage
{
    uint8_t conn_for_ssh[MAX_SSH_CONNS];
    // Which pool conn_for_ssh[] indexes for this slot. An accepted socket and a dialled one are
    // numbered in separate arrays, so the handle alone does not identify the stream.
    SshStreamKind kind[MAX_SSH_CONNS];
    // The socket each channel bridges. Every layer above deals in slots and channel ids; the handle
    // and the accessor it belongs to are this layer's alone.
    int chan_cid[MAX_SSH_CONNS][PROTOCORE_SSH_MAX_CHANNELS];
    // Whether the one-time seeding of the two maps above has run.
    proto_bool init_done;
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SSH_NETWORK_OFF_CTX 0u
static_assert(SSH_NETWORK_OFF_CTX + sizeof(struct SshNetworkStorage) <= PROTOCORE_SSH_NETWORK_BORROW,
              "PROTOCORE_SSH_NETWORK_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define SSH_NETWORK_CTX(w) ((struct SshNetworkStorage *)(void *)((w) + SSH_NETWORK_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SSH_NETWORK_BORROW persistent bytes
} SshNetworkOwnCtx;
static SshNetworkOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ssh_network_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_SSH_NETWORK_BORROW).buf;
    }
    return s_own.span;
}

// ---------------------------------------------------------------------------
// The stream a slot is bound to
// ---------------------------------------------------------------------------
// Everything outbound goes through these two, so the pool a handle belongs to is decided in one
// place instead of at each write site.

/** @brief True while slot @p i's stream can still carry bytes. */
static proto_bool stream_live(uint8_t i)
{
    if (i >= MAX_SSH_CONNS || SSH_NETWORK_CTX(protocore_ssh_network_span())->conn_for_ssh[i] == 0xFF)
    {
        return PROTO_FALSE;
    }
    const uint8_t h = SSH_NETWORK_CTX(protocore_ssh_network_span())->conn_for_ssh[i];
#if PROTOCORE_NEED_CLIENT
    if (SSH_NETWORK_CTX(protocore_ssh_network_span())->kind[i] == SSH_STREAM_DIALED)
    {
        TcpClientV.cid = (int)h;
        TcpClient.connected(protocore_tcp_client_span());
        const proto_bool up = TcpClientV.ok;
        TcpClient.is_closed(protocore_tcp_client_span());
        return up && !TcpClientV.ok;
    }
#endif
    ConnPoolV.slot = h;
    ConnPool.active(protocore_conn_pool_span());
    return ConnPoolV.ok;
}

/** @brief Put @p len bytes of framed packet on slot @p i's stream. */
static proto_bool stream_write(uint8_t i, const uint8_t *buf, size_t len)
{
    if (!stream_live(i))
    {
        return PROTO_FALSE;
    }
    const uint8_t h = SSH_NETWORK_CTX(protocore_ssh_network_span())->conn_for_ssh[i];
#if PROTOCORE_NEED_CLIENT
    if (SSH_NETWORK_CTX(protocore_ssh_network_span())->kind[i] == SSH_STREAM_DIALED)
    {
        TcpClientV.cid = (int)h;
        TcpClientV.io.data = buf;
        TcpClientV.io.len = len;
        TcpClient.send(protocore_tcp_client_span());
        return TcpClientV.ok;
    }
#endif
    ConnPoolV.slot = h;
    ConnPoolV.io.data = buf;
    ConnPoolV.io.len = (proto_u16)len;
    ConnPool.send(protocore_conn_pool_span());
    ConnPool.flush(protocore_conn_pool_span());
    return PROTO_TRUE;
}

static void ensure_init(void)
{
    if (SSH_NETWORK_CTX(protocore_ssh_network_span())->init_done)
    {
        return;
    }
    for (uint8_t j = 0; j < MAX_SSH_CONNS; j++)
    {
        SSH_NETWORK_CTX(protocore_ssh_network_span())->conn_for_ssh[j] = 0xFF;
        for (int k = 0; k < PROTOCORE_SSH_MAX_CHANNELS; k++)
        {
            SSH_NETWORK_CTX(protocore_ssh_network_span())->chan_cid[j][k] = -1;
        }
    }
    SSH_NETWORK_CTX(protocore_ssh_network_span())->init_done = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Outbound: frame + (encrypt/MAC) one SSH message and write it to the socket.
// ---------------------------------------------------------------------------

void protocore_ssh_network_emit(uint8_t *restrict work)
{
    (void)work;
    const uint8_t i = SshNetworkV.ssh_slot;
    const uint8_t *payload = SshNetworkV.msg.payload;
    const size_t len = SshNetworkV.msg.len;
    // The receive path checks the slot mapping, never liveness, so a stream that died between the
    // inbound read and this reply arrives here mapped but dead. Drop the reply rather than framing
    // it: ssh_pkt_emit advances the send sequence, and a counted packet that never reaches the peer
    // desynchronizes its MAC input for the rest of the connection (RFC 4253 sec 6.4).
    if (!stream_live(i))
    {
        return;
    }

    // Frame it into the secure pool and raise the flag, then wake the worker that owns this slot.
    // It owns the connection, the session and the pool the packet sits in, so it drains the packet
    // itself - woken when there is one rather than walking slots that have nothing. A dialled
    // stream has no accepted-side worker; its owner drains it from its own poll.
    if (ssh_pkt_emit(i, payload, len, &ssh_sess[i].out) == 0)
    {
#if PROTOCORE_NEED_CLIENT
        if (SSH_NETWORK_CTX(protocore_ssh_network_span())->kind[i] == SSH_STREAM_DIALED)
        {
            return;
        }
#endif
        ConnPoolV.slot = SSH_NETWORK_CTX(protocore_ssh_network_span())->conn_for_ssh[i];
        ConnPool.owner(protocore_conn_pool_span());
        WorkersV.worker_id = ConnPoolV.u8;
        Workers.wake(protocore_worker_span());
    }
}

// Put the packet the codec left flagged on the wire: as many bytes as the send window takes now,
// the rest on a later pass, and the release - which wipes the packet - once the last byte is out.
void protocore_ssh_network_tx_drain(uint8_t *restrict work)
{
    (void)work;
    const uint8_t conn_slot = SshNetworkV.conn_slot;
    const uint8_t j = SshNetworkV.ssh_slot;
    SshPacketState *pkt = &ssh_pkt[j];
    ConnPoolV.slot = conn_slot;
    ConnPool.active(protocore_conn_pool_span());
    if (!pkt->tx_ready || !ConnPoolV.ok)
    {
        return;
    }
    ConnPool.sndbuf(protocore_conn_pool_span());
    size_t room = (size_t)ConnPoolV.u16;
    size_t n = pkt->tx_len - pkt->tx_off;
    if (n > room)
    {
        n = room;
    }
    if (n > 0)
    {
        ConnPoolV.io.data = pkt->tx_wire + pkt->tx_off;
        ConnPoolV.io.len = (proto_u16)n;
        ConnPool.send(protocore_conn_pool_span());
        if (ConnPoolV.ok)
        {
            ConnPool.flush(protocore_conn_pool_span());
            pkt->tx_off += n;
        }
    }
    if (pkt->tx_off >= pkt->tx_len)
    {
        pkt->tx_ready = PROTO_FALSE; // the buffer stays borrowed for the next packet
    }
}

// Frame one built SSH message into its own binary packet and put it on the slot's socket
// (RFC 4253 sec 6: one SSH message per packet). The wire is the slot's own span at SSH_OFF_WIRE.
void protocore_ssh_network_write_msg(uint8_t *restrict work)
{
    (void)work;
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    const uint8_t *msg = SshNetworkV.msg.payload;
    const size_t len = SshNetworkV.msg.len;
    if (!stream_live(ssh_slot))
    {
        SshNetworkV.i32 = -1;
        return;
    }
    SshV.conn_slot_args.i = ssh_slot;
    Ssh.conn_slot(protocore_ssh_span());
    uint8_t *slot = SshV.ptr;
    if (slot == NULL)
    {
        SshNetworkV.i32 = -1;
        return;
    }

    uint8_t *wire = slot + SSH_OFF_WIRE;
    size_t wlen = 0;
    if (ssh_pkt_send(ssh_slot, msg, len, wire, &wlen, SSH_WIRE_CAP, &ssh_sess[ssh_slot].out) != 0)
    {
        SshNetworkV.i32 = -1;
        return;
    }
    SshNetworkV.i32 = stream_write(ssh_slot, wire, wlen) ? 0 : -1;
}

// The span a message may be built in so the framer wraps it without moving it. Null when the slot
// has no stream or no storage; @p cap takes what is left of the wire after the framing header.
void protocore_ssh_network_payload_region(uint8_t *restrict work)
{
    (void)work;
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    size_t *cap = &SshNetworkV.read_args.cap;
    if (!stream_live(ssh_slot) || cap == NULL)
    {
        SshNetworkV.region = NULL;
        return;
    }
    SshV.conn_slot_args.i = ssh_slot;
    Ssh.conn_slot(protocore_ssh_span());
    uint8_t *slot = SshV.ptr;
    if (slot == NULL)
    {
        SshNetworkV.region = NULL;
        return;
    }
    *cap = SSH_WIRE_CAP - SSH_WIRE_PAYLOAD_OFF;
    SshNetworkV.region = slot + SSH_OFF_WIRE + SSH_WIRE_PAYLOAD_OFF;
    return;
}

// Frame the @p plen bytes already sitting in the region above and write them, no copy.
void protocore_ssh_network_write_msg_at(uint8_t *restrict work)
{
    (void)work;
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    const size_t plen = SshNetworkV.msg.plen;
    if (!stream_live(ssh_slot))
    {
        SshNetworkV.i32 = -1;
        return;
    }
    SshV.conn_slot_args.i = ssh_slot;
    Ssh.conn_slot(protocore_ssh_span());
    uint8_t *slot = SshV.ptr;
    if (slot == NULL)
    {
        SshNetworkV.i32 = -1;
        return;
    }
    uint8_t *wire = slot + SSH_OFF_WIRE;
    size_t wlen = 0;
    if (ssh_pkt_send_at(ssh_slot, wire, plen, &wlen, SSH_WIRE_CAP, &ssh_sess[ssh_slot].out) != 0)
    {
        SshNetworkV.i32 = -1;
        return;
    }
    // ssh_pkt_send_at has already advanced the sequence number and the cipher for this packet.
    SshNetworkV.i32 = stream_write(ssh_slot, wire, wlen) ? 0 : -1;
}

void protocore_ssh_network_claim(uint8_t *restrict work)
{
    (void)work;
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    const int handle = SshNetworkV.handle;
    const SshStreamKind kind = SshNetworkV.stream.kind;
    ensure_init();
    // 0xFF is the free marker, so a handle that would collide with it cannot be bound.
    if (ssh_slot >= MAX_SSH_CONNS || handle < 0 || handle >= 0xFF ||
        SSH_NETWORK_CTX(protocore_ssh_network_span())->conn_for_ssh[ssh_slot] != 0xFF)
    {
        SshNetworkV.i32 = -1;
        return;
    }
    SSH_NETWORK_CTX(protocore_ssh_network_span())->conn_for_ssh[ssh_slot] = (uint8_t)handle;
    SSH_NETWORK_CTX(protocore_ssh_network_span())->kind[ssh_slot] = kind;
    SshNetworkV.i32 = 0;
    return;
}

void protocore_ssh_network_release(uint8_t *restrict work)
{
    (void)work;
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    ensure_init();
    if (ssh_slot >= MAX_SSH_CONNS)
    {
        return;
    }
    SSH_NETWORK_CTX(protocore_ssh_network_span())->conn_for_ssh[ssh_slot] = 0xFF;
    SSH_NETWORK_CTX(protocore_ssh_network_span())->kind[ssh_slot] = SSH_STREAM_ACCEPTED;
}

// The lowest SSH slot no socket owns, or 0xFF when every one is taken.
void protocore_ssh_network_slot_free(uint8_t *restrict work)
{
    (void)work;
    ensure_init();
    for (uint8_t j = 0; j < MAX_SSH_CONNS; j++)
    {
        if (SSH_NETWORK_CTX(protocore_ssh_network_span())->conn_for_ssh[j] == 0xFF)
        {
            SshNetworkV.u8 = j;
            return;
        }
    }
    SshNetworkV.u8 = 0xFF;
    return;
}

// True when the pair is still the one claim() recorded: a slot whose socket has been recycled under
// it is not this connection's, and the caller must leave it alone.
void protocore_ssh_network_owns(uint8_t *restrict work)
{
    (void)work;
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    const uint8_t conn_slot = SshNetworkV.conn_slot;
    ensure_init();
    SshNetworkV.ok =
        ssh_slot < MAX_SSH_CONNS && SSH_NETWORK_CTX(protocore_ssh_network_span())->conn_for_ssh[ssh_slot] == conn_slot;
}

// ---------------------------------------------------------------------------
// Protocol version exchange (RFC 4253 sec 4.2)
// ---------------------------------------------------------------------------

// Put the server identification string on the wire, raw, before any binary packet. RFC 4253 sec 4.2
// runs this exchange ahead of the binary packet protocol, so the slot's wire span is idle and stages
// it.
void ssh_net_version_exchange_send(uint8_t i, uint8_t conn_slot)
{
    SshV.conn_slot_args.i = i;
    Ssh.conn_slot(protocore_ssh_span());
    uint8_t *slot = SshV.ptr;
    if (slot == NULL)
    {
        return;
    }

    uint8_t *ident = slot + SSH_OFF_WIRE;
    size_t ilen = 0;
    ConnPoolV.slot = conn_slot;
    ConnPool.active(protocore_conn_pool_span());
    SshTransportV.slot = i;
    SshTransportV.out_args.out = ident;
    SshTransportV.out_args.cap = SSH_WIRE_CAP;
    SshTransport.send_ident(protocore_ssh_transport_span());
    ilen = SshTransportV.out_args.out_len;
    if (SshTransportV.i32 == 0 && ConnPoolV.ok)
    {
        ConnPoolV.io.data = ident;
        ConnPoolV.io.len = (proto_u16)ilen;
        ConnPool.send(protocore_conn_pool_span());
        ConnPool.flush(protocore_conn_pool_span());
    }
}

// ---------------------------------------------------------------------------
// Per-channel sockets
// ---------------------------------------------------------------------------

// The slot the handle for @p channel sits in, or null when the pair is out of range.
// A channel bridged to a socket of our own is an outbound connection, so this whole family needs the
// client half of the transport. TcpNs carries Tcp.client under the same condition.
#if PROTOCORE_NEED_CLIENT

static int *chan_cid_of(uint8_t ssh_slot, uint32_t channel)
{
    if (ssh_slot >= MAX_SSH_CONNS || channel >= PROTOCORE_SSH_MAX_CHANNELS)
    {
        return NULL;
    }
    return &SSH_NETWORK_CTX(protocore_ssh_network_span())->chan_cid[ssh_slot][channel];
}

void protocore_ssh_network_chan_open(uint8_t *restrict work)
{
    (void)work;
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    const uint32_t channel = SshNetworkV.stream.channel;
    const char *host = SshNetworkV.dial.host;
    const uint16_t port = SshNetworkV.dial.port;
    const uint32_t timeout_ms = SshNetworkV.dial.timeout_ms;
    ensure_init();
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL)
    {
        SshNetworkV.i32 = -1;
        return;
    }
    TcpClientV.dial.host = host;
    TcpClientV.dial.port = port;
    TcpClientV.dial.timeout_ms = timeout_ms;
    TcpClient.open(protocore_tcp_client_span());
    int cid = TcpClientV.i32;
    if (cid < 0)
    {
        SshNetworkV.i32 = -1;
        return;
    }
    *slot = cid;
    SshNetworkV.i32 = cid;
}

// Record a socket the listener already accepted. The dial half is chan_open; this is its peer for
// the connections that arrive rather than being asked for.
void protocore_ssh_network_chan_adopt(uint8_t *restrict work)
{
    (void)work;
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    const uint32_t channel = SshNetworkV.stream.channel;
    const int cid = SshNetworkV.dial.cid;
    ensure_init();
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || cid < 0)
    {
        SshNetworkV.i32 = -1;
        return;
    }
    *slot = cid;
    SshNetworkV.i32 = 0;
    return;
}

// The channel a socket is bridged to. The transport's callbacks arrive keyed by socket, and this is
// the one place that mapping lives.
void protocore_ssh_network_chan_by_cid(uint8_t *restrict work)
{
    (void)work;
    const int cid = SshNetworkV.dial.cid;
    uint8_t *ssh_slot = &SshNetworkV.ssh_slot;
    uint32_t *channel = &SshNetworkV.stream.channel;
    ensure_init();
    if (cid < 0)
    {
        SshNetworkV.ok = PROTO_FALSE;
        return;
    }
    for (uint8_t j = 0; j < MAX_SSH_CONNS; j++)
    {
        for (uint32_t k = 0; k < PROTOCORE_SSH_MAX_CHANNELS; k++)
        {
            if (SSH_NETWORK_CTX(protocore_ssh_network_span())->chan_cid[j][k] == cid)
            {
                *ssh_slot = j;
                *channel = k;
                SshNetworkV.ok = PROTO_TRUE;
                return;
            }
        }
    }
    SshNetworkV.ok = PROTO_FALSE;
    return;
}

void protocore_ssh_network_chan_write(uint8_t *restrict work)
{
    (void)work;
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    const uint32_t channel = SshNetworkV.stream.channel;
    const uint8_t *data = SshNetworkV.msg.payload;
    const size_t len = SshNetworkV.msg.len;
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        SshNetworkV.i32 = -1;
        return;
    }
    TcpClientV.cid = *slot;
    TcpClientV.io.data = data;
    TcpClientV.io.len = len;
    TcpClient.send(protocore_tcp_client_span());
    SshNetworkV.i32 = (int)len;
}

void protocore_ssh_network_chan_read(uint8_t *restrict work)
{
    (void)work;
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    const uint32_t channel = SshNetworkV.stream.channel;
    uint8_t *out = SshNetworkV.read_args.out;
    const size_t cap = SshNetworkV.read_args.cap;
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        SshNetworkV.n = 0;
        return;
    }
    TcpClientV.cid = *slot;
    TcpClientV.io.buf = out;
    TcpClientV.io.cap = cap;
    TcpClient.read(protocore_tcp_client_span());
    SshNetworkV.n = TcpClientV.n;
    return;
}

// True once the bridged socket has closed and every byte it held has been read.
void protocore_ssh_network_chan_drained(uint8_t *restrict work)
{
    (void)work;
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    const uint32_t channel = SshNetworkV.stream.channel;
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        SshNetworkV.ok = PROTO_TRUE;
        return;
    }
    TcpClientV.cid = *slot;
    TcpClient.is_closed(protocore_tcp_client_span());
    const proto_bool gone = TcpClientV.ok;
    TcpClient.available(protocore_tcp_client_span());
    SshNetworkV.ok = gone && TcpClientV.n == 0;
}

void protocore_ssh_network_chan_avail(uint8_t *restrict work)
{
    (void)work;
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    const uint32_t channel = SshNetworkV.stream.channel;
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        SshNetworkV.n = 0;
        return;
    }
    TcpClientV.cid = *slot;
    TcpClient.available(protocore_tcp_client_span());
    SshNetworkV.n = TcpClientV.n;
    return;
}

void protocore_ssh_network_chan_close(uint8_t *restrict work)
{
    (void)work;
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    const uint32_t channel = SshNetworkV.stream.channel;
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        return;
    }
    TcpClientV.cid = *slot;
    TcpClient.close(protocore_tcp_client_span());
    *slot = -1;
}

void protocore_ssh_network_chan_close_all(uint8_t *restrict work)
{
    const uint8_t ssh_slot = SshNetworkV.ssh_slot;
    if (ssh_slot >= MAX_SSH_CONNS)
    {
        return;
    }
    for (uint32_t k = 0; k < PROTOCORE_SSH_MAX_CHANNELS; k++)
    {
        SshNetworkV.stream.channel = k;
        protocore_ssh_network_chan_close(work);
    }
}

#endif // PROTOCORE_NEED_CLIENT

/** @brief The operands and the outcome. */
SshNetworkVars SshNetworkV;
