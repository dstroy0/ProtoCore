// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file network.c
 * @brief Ring buffer in, framed bytes out; the socket slot and the SSH slot bound together.
 */

#include "network_drivers/presentation/ssh/network/network.h"
#include "network_drivers/presentation/ssh/auth/auth.h"
#include "network_drivers/presentation/ssh/connection/connection.h"
#include "network_drivers/presentation/ssh/ssh.h" // ssh_conn_slot() + the memory map
#include "network_drivers/presentation/ssh/transport/transport.h"
#if PROTOCORE_ENABLE_SSH_ZLIB
#include "network_drivers/presentation/ssh/transport/comp.h"
#endif
#include "network_drivers/transport/tcp/client/client.h"     // TcpClient: a dialled stream
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: an accepted stream
#include "server/clock/clock.h"                              // protocore_millis() for the re-key timer
#include "server/core/proto_handler.h"
#include "server/core/worker.h" // Workers.wake(): the owner drains the flagged packet

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
};

/**
 * @brief The bindings' state and the calls that reach them - what SshNetworkNs points at.
 *
 * @var SshNetworkInternal::store      the slot-to-stream map and the per-channel sockets
 * @var SshNetworkInternal::ns         the handle a caller sets a call's members on
 * @var SshNetworkInternal::init_done  the one-time init has run
 */
struct SshNetworkInternal
{
    struct SshNetworkStorage *store;
    SshNetworkNs *ns;
    proto_bool init_done;
};

static struct SshNetworkStorage s_store;

static struct SshNetworkInternal s_net = {.store = &s_store, .ns = &SshNetwork};

// ---------------------------------------------------------------------------
// The stream a slot is bound to
// ---------------------------------------------------------------------------
// Everything outbound goes through these two, so the pool a handle belongs to is decided in one
// place instead of at each write site.

/** @brief True while slot @p i's stream can still carry bytes. */
static proto_bool stream_live(uint8_t i)
{
    if (i >= MAX_SSH_CONNS || s_store.conn_for_ssh[i] == 0xFF)
    {
        return PROTO_FALSE;
    }
    const uint8_t h = s_store.conn_for_ssh[i];
#if PROTOCORE_NEED_CLIENT
    if (s_store.kind[i] == SSH_STREAM_DIALED)
    {
        TcpClient.cid = (int)h;
        TcpClient.connected(TcpClient.internal);
        const proto_bool up = TcpClient.ok;
        TcpClient.is_closed(TcpClient.internal);
        return up && !TcpClient.ok;
    }
#endif
    ConnPool.slot = h;
    ConnPool.active(ConnPool.internal);
    return ConnPool.ok;
}

/** @brief Put @p len bytes of framed packet on slot @p i's stream. */
static proto_bool stream_write(uint8_t i, const uint8_t *buf, size_t len)
{
    if (!stream_live(i))
    {
        return PROTO_FALSE;
    }
    const uint8_t h = s_store.conn_for_ssh[i];
#if PROTOCORE_NEED_CLIENT
    if (s_store.kind[i] == SSH_STREAM_DIALED)
    {
        TcpClient.cid = (int)h;
        TcpClient.io.data = buf;
        TcpClient.io.len = len;
        TcpClient.send(TcpClient.internal);
        return TcpClient.ok;
    }
#endif
    ConnPool.slot = h;
    ConnPool.io.data = buf;
    ConnPool.io.len = (proto_u16)len;
    ConnPool.send(ConnPool.internal);
    ConnPool.flush(ConnPool.internal);
    return PROTO_TRUE;
}

static void ensure_init(void)
{
    if (s_net.init_done)
    {
        return;
    }
    for (uint8_t j = 0; j < MAX_SSH_CONNS; j++)
    {
        s_store.conn_for_ssh[j] = 0xFF;
        for (int k = 0; k < PROTOCORE_SSH_MAX_CHANNELS; k++)
        {
            s_store.chan_cid[j][k] = -1;
        }
    }
    s_net.init_done = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Outbound: frame + (encrypt/MAC) one SSH message and write it to the socket.
// ---------------------------------------------------------------------------

static void emit(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t i = ctx->ns->ssh_slot;
    const uint8_t *payload = ctx->ns->msg.payload;
    const size_t len = ctx->ns->msg.len;
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
        if (s_store.kind[i] == SSH_STREAM_DIALED)
        {
            return;
        }
#endif
        ConnPool.slot = s_store.conn_for_ssh[i];
        ConnPool.owner(ConnPool.internal);
        Workers.worker_id = ConnPool.u8;
        Workers.wake(Workers.internal);
    }
}

// Put the packet the codec left flagged on the wire: as many bytes as the send window takes now,
// the rest on a later pass, and the release - which wipes the packet - once the last byte is out.
static void tx_drain(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t conn_slot = ctx->ns->conn_slot;
    const uint8_t j = ctx->ns->ssh_slot;
    SshPacketState *pkt = &ssh_pkt[j];
    ConnPool.slot = conn_slot;
    ConnPool.active(ConnPool.internal);
    if (!pkt->tx_ready || !ConnPool.ok)
    {
        return;
    }
    ConnPool.sndbuf(ConnPool.internal);
    size_t room = (size_t)ConnPool.u16;
    size_t n = pkt->tx_len - pkt->tx_off;
    if (n > room)
    {
        n = room;
    }
    if (n > 0)
    {
        ConnPool.io.data = pkt->tx_wire + pkt->tx_off;
        ConnPool.io.len = (proto_u16)n;
        ConnPool.send(ConnPool.internal);
        if (ConnPool.ok)
        {
            ConnPool.flush(ConnPool.internal);
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
static void net_write_msg(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    const uint8_t *msg = ctx->ns->msg.payload;
    const size_t len = ctx->ns->msg.len;
    if (!stream_live(ssh_slot))
    {
        ctx->ns->i32 = -1;
        return;
    }
    uint8_t *slot = ssh_conn_slot(ssh_slot);
    if (slot == NULL)
    {
        ctx->ns->i32 = -1;
        return;
    }

    uint8_t *wire = slot + SSH_OFF_WIRE;
    size_t wlen = 0;
    if (ssh_pkt_send(ssh_slot, msg, len, wire, &wlen, SSH_WIRE_CAP, &ssh_sess[ssh_slot].out) != 0)
    {
        ctx->ns->i32 = -1;
        return;
    }
    ctx->ns->i32 = stream_write(ssh_slot, wire, wlen) ? 0 : -1;
}

// The span a message may be built in so the framer wraps it without moving it. Null when the slot
// has no stream or no storage; @p cap takes what is left of the wire after the framing header.
static void net_payload_region(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    size_t *cap = &ctx->ns->read_args.cap;
    if (!stream_live(ssh_slot) || cap == NULL)
    {
        ctx->ns->region = NULL;
        return;
    }
    uint8_t *slot = ssh_conn_slot(ssh_slot);
    if (slot == NULL)
    {
        ctx->ns->region = NULL;
        return;
    }
    *cap = SSH_WIRE_CAP - SSH_WIRE_PAYLOAD_OFF;
    ctx->ns->region = slot + SSH_OFF_WIRE + SSH_WIRE_PAYLOAD_OFF;
    return;
}

// Frame the @p plen bytes already sitting in the region above and write them, no copy.
static void net_write_msg_at(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    const size_t plen = ctx->ns->msg.plen;
    if (!stream_live(ssh_slot))
    {
        ctx->ns->i32 = -1;
        return;
    }
    uint8_t *slot = ssh_conn_slot(ssh_slot);
    if (slot == NULL)
    {
        ctx->ns->i32 = -1;
        return;
    }
    uint8_t *wire = slot + SSH_OFF_WIRE;
    size_t wlen = 0;
    if (ssh_pkt_send_at(ssh_slot, wire, plen, &wlen, SSH_WIRE_CAP, &ssh_sess[ssh_slot].out) != 0)
    {
        ctx->ns->i32 = -1;
        return;
    }
    // ssh_pkt_send_at has already advanced the sequence number and the cipher for this packet.
    ctx->ns->i32 = stream_write(ssh_slot, wire, wlen) ? 0 : -1;
}

static void net_claim(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    const int handle = ctx->ns->handle;
    const SshStreamKind kind = ctx->ns->stream.kind;
    ensure_init();
    // 0xFF is the free marker, so a handle that would collide with it cannot be bound.
    if (ssh_slot >= MAX_SSH_CONNS || handle < 0 || handle >= 0xFF || s_store.conn_for_ssh[ssh_slot] != 0xFF)
    {
        ctx->ns->i32 = -1;
        return;
    }
    s_store.conn_for_ssh[ssh_slot] = (uint8_t)handle;
    s_store.kind[ssh_slot] = kind;
    ctx->ns->i32 = 0;
    return;
}

static void net_release(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    ensure_init();
    if (ssh_slot >= MAX_SSH_CONNS)
    {
        return;
    }
    s_store.conn_for_ssh[ssh_slot] = 0xFF;
    s_store.kind[ssh_slot] = SSH_STREAM_ACCEPTED;
}

// The lowest SSH slot no socket owns, or 0xFF when every one is taken.
static void net_slot_free(struct SshNetworkInternal *restrict ctx)
{
    ensure_init();
    for (uint8_t j = 0; j < MAX_SSH_CONNS; j++)
    {
        if (s_store.conn_for_ssh[j] == 0xFF)
        {
            ctx->ns->u8 = j;
            return;
        }
    }
    ctx->ns->u8 = 0xFF;
    return;
}

// True when the pair is still the one claim() recorded: a slot whose socket has been recycled under
// it is not this connection's, and the caller must leave it alone.
static void net_owns(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    const uint8_t conn_slot = ctx->ns->conn_slot;
    ensure_init();
    ctx->ns->ok = ssh_slot < MAX_SSH_CONNS && s_store.conn_for_ssh[ssh_slot] == conn_slot;
}

// ---------------------------------------------------------------------------
// Protocol version exchange (RFC 4253 sec 4.2)
// ---------------------------------------------------------------------------

// Put the server identification string on the wire, raw, before any binary packet. RFC 4253 sec 4.2
// runs this exchange ahead of the binary packet protocol, so the slot's wire span is idle and stages
// it.
void ssh_net_version_exchange_send(uint8_t i, uint8_t conn_slot)
{
    uint8_t *slot = ssh_conn_slot(i);
    if (slot == NULL)
    {
        return;
    }

    uint8_t *ident = slot + SSH_OFF_WIRE;
    size_t ilen = 0;
    ConnPool.slot = conn_slot;
    ConnPool.active(ConnPool.internal);
    SshTransport.slot = i;
    SshTransport.out_args.out = ident;
    SshTransport.out_args.cap = SSH_WIRE_CAP;
    SshTransport.send_ident(SshTransport.internal);
    ilen = SshTransport.out_args.out_len;
    if (SshTransport.i32 == 0 && ConnPool.ok)
    {
        ConnPool.io.data = ident;
        ConnPool.io.len = (proto_u16)ilen;
        ConnPool.send(ConnPool.internal);
        ConnPool.flush(ConnPool.internal);
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
    return &s_store.chan_cid[ssh_slot][channel];
}

static void net_chan_open(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    const uint32_t channel = ctx->ns->stream.channel;
    const char *host = ctx->ns->dial.host;
    const uint16_t port = ctx->ns->dial.port;
    const uint32_t timeout_ms = ctx->ns->dial.timeout_ms;
    ensure_init();
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL)
    {
        ctx->ns->i32 = -1;
        return;
    }
    TcpClient.dial.host = host;
    TcpClient.dial.port = port;
    TcpClient.dial.timeout_ms = timeout_ms;
    TcpClient.open(TcpClient.internal);
    int cid = TcpClient.i32;
    if (cid < 0)
    {
        ctx->ns->i32 = -1;
        return;
    }
    *slot = cid;
    return cid;
}

// Record a socket the listener already accepted. The dial half is chan_open; this is its peer for
// the connections that arrive rather than being asked for.
static void net_chan_adopt(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    const uint32_t channel = ctx->ns->stream.channel;
    const int cid = ctx->ns->dial.cid;
    ensure_init();
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || cid < 0)
    {
        ctx->ns->i32 = -1;
        return;
    }
    *slot = cid;
    ctx->ns->i32 = 0;
    return;
}

// The channel a socket is bridged to. The transport's callbacks arrive keyed by socket, and this is
// the one place that mapping lives.
static void net_chan_by_cid(struct SshNetworkInternal *restrict ctx)
{
    const int cid = ctx->ns->dial.cid;
    uint8_t *ssh_slot = &ctx->ns->ssh_slot;
    uint32_t *channel = &ctx->ns->stream.channel;
    ensure_init();
    if (cid < 0)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    for (uint8_t j = 0; j < MAX_SSH_CONNS; j++)
    {
        for (uint32_t k = 0; k < PROTOCORE_SSH_MAX_CHANNELS; k++)
        {
            if (s_store.chan_cid[j][k] == cid)
            {
                *ssh_slot = j;
                *channel = k;
                ctx->ns->ok = PROTO_TRUE;
                return;
            }
        }
    }
    ctx->ns->ok = PROTO_FALSE;
    return;
}

static void net_chan_write(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    const uint32_t channel = ctx->ns->stream.channel;
    const uint8_t *data = ctx->ns->msg.payload;
    const size_t len = ctx->ns->msg.len;
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        ctx->ns->i32 = -1;
        return;
    }
    TcpClient.cid = *slot;
    TcpClient.io.data = data;
    TcpClient.io.len = len;
    TcpClient.send(TcpClient.internal);
    return (int)len;
}

static void net_chan_read(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    const uint32_t channel = ctx->ns->stream.channel;
    uint8_t *out = ctx->ns->read_args.out;
    const size_t cap = ctx->ns->read_args.cap;
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        ctx->ns->n = 0;
        return;
    }
    TcpClient.cid = *slot;
    TcpClient.io.buf = out;
    TcpClient.io.cap = cap;
    TcpClient.read(TcpClient.internal);
    ctx->ns->n = TcpClient.n;
    return;
}

// True once the bridged socket has closed and every byte it held has been read.
static void net_chan_drained(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    const uint32_t channel = ctx->ns->stream.channel;
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        ctx->ns->ok = PROTO_TRUE;
        return;
    }
    TcpClient.cid = *slot;
    TcpClient.is_closed(TcpClient.internal);
    const proto_bool gone = TcpClient.ok;
    TcpClient.available(TcpClient.internal);
    return gone && TcpClient.n == 0;
}

static void net_chan_avail(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    const uint32_t channel = ctx->ns->stream.channel;
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        ctx->ns->n = 0;
        return;
    }
    TcpClient.cid = *slot;
    TcpClient.available(TcpClient.internal);
    ctx->ns->n = TcpClient.n;
    return;
}

static void net_chan_close(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    const uint32_t channel = ctx->ns->stream.channel;
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        return;
    }
    TcpClient.cid = *slot;
    TcpClient.close(TcpClient.internal);
    *slot = -1;
}

static void net_chan_close_all(struct SshNetworkInternal *restrict ctx)
{
    const uint8_t ssh_slot = ctx->ns->ssh_slot;
    if (ssh_slot >= MAX_SSH_CONNS)
    {
        return;
    }
    for (uint32_t k = 0; k < PROTOCORE_SSH_MAX_CHANNELS; k++)
    {
        ctx->ns->stream.channel = k;
        net_chan_close(ctx);
    }
}

#endif // PROTOCORE_NEED_CLIENT

SshNetworkNs SshNetwork = {.claim = net_claim,
                           .release = net_release,
                           .slot_free = net_slot_free,
                           .owns = net_owns,
                           .tx_drain = tx_drain,
                           .emit = emit,
                           .write_msg = net_write_msg,
                           .payload_region = net_payload_region,
                           .write_msg_at = net_write_msg_at,
#if PROTOCORE_NEED_CLIENT
                           .chan_open = net_chan_open,
                           .chan_adopt = net_chan_adopt,
                           .chan_by_cid = net_chan_by_cid,
                           .chan_write = net_chan_write,
                           .chan_read = net_chan_read,
                           .chan_avail = net_chan_avail,
                           .chan_drained = net_chan_drained,
                           .chan_close = net_chan_close,
                           .chan_close_all = net_chan_close_all,
#endif // PROTOCORE_NEED_CLIENT
                           .internal = &s_net};
