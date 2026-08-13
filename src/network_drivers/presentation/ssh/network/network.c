// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
#include "network_drivers/session/proto_handler.h"
#include "network_drivers/session/worker.h" // Workers.wake(): the owner drains the flagged packet
#include "network_drivers/transport/tcp.h"
#include "server/clock/clock.h" // protocore_millis() for the re-key timer

// The network layer's state, owned by one instance (internal linkage): the SSH-slot -> socket-slot
// mapping (0xFF = free) and the one-time init flag. The bytes a connection uses are ssh.c's, and
// whether a connection must be torn down is the driving role's, not the stream's.
typedef struct
{
    uint8_t conn_for_ssh[MAX_SSH_CONNS];
    // Which pool conn_for_ssh[] indexes for this slot. An accepted socket and a dialled one are
    // numbered in separate arrays, so the handle alone does not identify the stream.
    SshStreamKind kind[MAX_SSH_CONNS];
    proto_bool init_done;
    // The socket each channel bridges. Every layer above deals in slots and channel ids; the handle
    // and the accessor it belongs to are this layer's alone.
    int chan_cid[MAX_SSH_CONNS][PROTOCORE_SSH_MAX_CHANNELS];
} SshNetworkCtx;
static SshNetworkCtx s_net;

// ---------------------------------------------------------------------------
// The stream a slot is bound to
// ---------------------------------------------------------------------------
// Everything outbound goes through these two, so the pool a handle belongs to is decided in one
// place instead of at each write site.

/** @brief True while slot @p i's stream can still carry bytes. */
static proto_bool stream_live(uint8_t i)
{
    if (i >= MAX_SSH_CONNS || s_net.conn_for_ssh[i] == 0xFF)
    {
        return PROTO_FALSE;
    }
    const uint8_t h = s_net.conn_for_ssh[i];
#if PROTOCORE_NEED_CLIENT
    if (s_net.kind[i] == SSH_STREAM_DIALED)
    {
        return Tcp.client->connected((int)h) && !Tcp.client->is_closed((int)h);
    }
#endif
    return protocore_conn_active(conn_pool[h].id);
}

/** @brief Put @p len bytes of framed packet on slot @p i's stream. */
static proto_bool stream_write(uint8_t i, const uint8_t *buf, size_t len)
{
    if (!stream_live(i))
    {
        return PROTO_FALSE;
    }
    const uint8_t h = s_net.conn_for_ssh[i];
#if PROTOCORE_NEED_CLIENT
    if (s_net.kind[i] == SSH_STREAM_DIALED)
    {
        return Tcp.client->send((int)h, buf, len);
    }
#endif
    Tcp.conn->send(conn_pool[h].id, buf, (proto_u16)len);
    Tcp.conn->flush(conn_pool[h].id);
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
        s_net.conn_for_ssh[j] = 0xFF;
        for (int k = 0; k < PROTOCORE_SSH_MAX_CHANNELS; k++)
        {
            s_net.chan_cid[j][k] = -1;
        }
    }
    s_net.init_done = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Outbound: frame + (encrypt/MAC) one SSH message and write it to the socket.
// ---------------------------------------------------------------------------

static void emit(uint8_t i, const uint8_t *payload, size_t len)
{
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
        if (s_net.kind[i] == SSH_STREAM_DIALED)
        {
            return;
        }
#endif
        Workers.wake(conn_pool[s_net.conn_for_ssh[i]].owner);
    }
}

// Put the packet the codec left flagged on the wire: as many bytes as the send window takes now,
// the rest on a later pass, and the release - which wipes the packet - once the last byte is out.
static void tx_drain(uint8_t conn_slot, uint8_t j)
{
    SshPacketState *pkt = &ssh_pkt[j];
    if (!pkt->tx_ready || !protocore_conn_active(conn_slot))
    {
        return;
    }
    size_t room = (size_t)Tcp.conn->sndbuf(conn_slot);
    size_t n = pkt->tx_len - pkt->tx_off;
    if (n > room)
    {
        n = room;
    }
    if (n > 0 && Tcp.conn->send(conn_slot, pkt->tx_wire + pkt->tx_off, (proto_u16)n))
    {
        Tcp.conn->flush(conn_slot);
        pkt->tx_off += n;
    }
    if (pkt->tx_off >= pkt->tx_len)
    {
        pkt->tx_ready = PROTO_FALSE; // the buffer stays borrowed for the next packet
    }
}

// Frame one built SSH message into its own binary packet and put it on the slot's socket
// (RFC 4253 sec 6: one SSH message per packet). The wire is the slot's own span at SSH_OFF_WIRE.
static int net_write_msg(uint8_t ssh_slot, const uint8_t *msg, size_t len)
{
    if (!stream_live(ssh_slot))
    {
        return -1;
    }
    uint8_t *slot = ssh_conn_slot(ssh_slot);
    if (slot == NULL)
    {
        return -1;
    }

    uint8_t *wire = slot + SSH_OFF_WIRE;
    size_t wlen = 0;
    if (ssh_pkt_send(ssh_slot, msg, len, wire, &wlen, SSH_WIRE_CAP, &ssh_sess[ssh_slot].out) != 0)
    {
        return -1;
    }
    return stream_write(ssh_slot, wire, wlen) ? 0 : -1;
}

// The span a message may be built in so the framer wraps it without moving it. Null when the slot
// has no stream or no storage; @p cap takes what is left of the wire after the framing header.
static uint8_t *net_payload_region(uint8_t ssh_slot, size_t *cap)
{
    if (!stream_live(ssh_slot) || cap == NULL)
    {
        return NULL;
    }
    uint8_t *slot = ssh_conn_slot(ssh_slot);
    if (slot == NULL)
    {
        return NULL;
    }
    *cap = SSH_WIRE_CAP - SSH_WIRE_PAYLOAD_OFF;
    return slot + SSH_OFF_WIRE + SSH_WIRE_PAYLOAD_OFF;
}

// Frame the @p plen bytes already sitting in the region above and write them, no copy.
static int net_write_msg_at(uint8_t ssh_slot, size_t plen)
{
    if (!stream_live(ssh_slot))
    {
        return -1;
    }
    uint8_t *slot = ssh_conn_slot(ssh_slot);
    if (slot == NULL)
    {
        return -1;
    }
    uint8_t *wire = slot + SSH_OFF_WIRE;
    size_t wlen = 0;
    if (ssh_pkt_send_at(ssh_slot, wire, plen, &wlen, SSH_WIRE_CAP, &ssh_sess[ssh_slot].out) != 0)
    {
        return -1;
    }
    // ssh_pkt_send_at has already advanced the sequence number and the cipher for this packet.
    return stream_write(ssh_slot, wire, wlen) ? 0 : -1;
}


static int net_claim(uint8_t ssh_slot, int handle, SshStreamKind kind)
{
    ensure_init();
    // 0xFF is the free marker, so a handle that would collide with it cannot be bound.
    if (ssh_slot >= MAX_SSH_CONNS || handle < 0 || handle >= 0xFF || s_net.conn_for_ssh[ssh_slot] != 0xFF)
    {
        return -1;
    }
    s_net.conn_for_ssh[ssh_slot] = (uint8_t)handle;
    s_net.kind[ssh_slot] = kind;
    return 0;
}

static void net_release(uint8_t ssh_slot)
{
    ensure_init();
    if (ssh_slot >= MAX_SSH_CONNS)
    {
        return;
    }
    s_net.conn_for_ssh[ssh_slot] = 0xFF;
    s_net.kind[ssh_slot] = SSH_STREAM_ACCEPTED;
}

// The lowest SSH slot no socket owns, or 0xFF when every one is taken.
static uint8_t net_slot_free(void)
{
    ensure_init();
    for (uint8_t j = 0; j < MAX_SSH_CONNS; j++)
    {
        if (s_net.conn_for_ssh[j] == 0xFF)
        {
            return j;
        }
    }
    return 0xFF;
}

// True when the pair is still the one claim() recorded: a slot whose socket has been recycled under
// it is not this connection's, and the caller must leave it alone.
static proto_bool net_owns(uint8_t ssh_slot, uint8_t conn_slot)
{
    ensure_init();
    return ssh_slot < MAX_SSH_CONNS && s_net.conn_for_ssh[ssh_slot] == conn_slot;
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
    TcpConn *conn = &conn_pool[conn_slot];
    uint8_t *ident = slot + SSH_OFF_WIRE;
    size_t ilen = 0;
    if (SSH_TRANSPORT->send_ident(i, ident, &ilen, SSH_WIRE_CAP) == 0 && protocore_conn_active(conn->id))
    {
        Tcp.conn->send(conn->id, ident, (proto_u16)ilen);
        Tcp.conn->flush(conn->id);
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
    return &s_net.chan_cid[ssh_slot][channel];
}

static int net_chan_open(uint8_t ssh_slot, uint32_t channel, const char *host, uint16_t port, uint32_t timeout_ms)
{
    ensure_init();
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL)
    {
        return -1;
    }
    int cid = Tcp.client->open(host, port, timeout_ms);
    if (cid < 0)
    {
        return -1;
    }
    *slot = cid;
    return cid;
}

// Record a socket the listener already accepted. The dial half is chan_open; this is its peer for
// the connections that arrive rather than being asked for.
static int net_chan_adopt(uint8_t ssh_slot, uint32_t channel, int cid)
{
    ensure_init();
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || cid < 0)
    {
        return -1;
    }
    *slot = cid;
    return 0;
}

// The channel a socket is bridged to. The transport's callbacks arrive keyed by socket, and this is
// the one place that mapping lives.
static proto_bool net_chan_by_cid(int cid, uint8_t *ssh_slot, uint32_t *channel)
{
    ensure_init();
    if (cid < 0)
    {
        return PROTO_FALSE;
    }
    for (uint8_t j = 0; j < MAX_SSH_CONNS; j++)
    {
        for (uint32_t k = 0; k < PROTOCORE_SSH_MAX_CHANNELS; k++)
        {
            if (s_net.chan_cid[j][k] == cid)
            {
                *ssh_slot = j;
                *channel = k;
                return PROTO_TRUE;
            }
        }
    }
    return PROTO_FALSE;
}

static int net_chan_write(uint8_t ssh_slot, uint32_t channel, const uint8_t *data, size_t len)
{
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        return -1;
    }
    Tcp.client->send(*slot, data, len);
    return (int)len;
}

static size_t net_chan_read(uint8_t ssh_slot, uint32_t channel, uint8_t *out, size_t cap)
{
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        return 0;
    }
    return Tcp.client->read(*slot, out, cap);
}

// True once the bridged socket has closed and every byte it held has been read.
static proto_bool net_chan_drained(uint8_t ssh_slot, uint32_t channel)
{
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        return PROTO_TRUE;
    }
    return Tcp.client->is_closed(*slot) && Tcp.client->available(*slot) == 0;
}

static size_t net_chan_avail(uint8_t ssh_slot, uint32_t channel)
{
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        return 0;
    }
    return Tcp.client->available(*slot);
}

static void net_chan_close(uint8_t ssh_slot, uint32_t channel)
{
    int *slot = chan_cid_of(ssh_slot, channel);
    if (slot == NULL || *slot < 0)
    {
        return;
    }
    Tcp.client->close(*slot);
    *slot = -1;
}

static void net_chan_close_all(uint8_t ssh_slot)
{
    if (ssh_slot >= MAX_SSH_CONNS)
    {
        return;
    }
    for (uint32_t k = 0; k < PROTOCORE_SSH_MAX_CHANNELS; k++)
    {
        net_chan_close(ssh_slot, k);
    }
}

#endif // PROTOCORE_NEED_CLIENT

const SshNetworkNs SshNetwork = {.claim = net_claim,
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
                                 .chan_close_all = net_chan_close_all
#endif // PROTOCORE_NEED_CLIENT
};
