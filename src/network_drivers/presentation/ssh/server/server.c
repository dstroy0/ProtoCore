// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file server.c
 * @brief The server engine: accept a connection, drive it, tear it down.
 */

#include "network_drivers/presentation/ssh/server/server.h"
#include "mmgr/plaintext.h"
#include "mmgr/secure.h"
#include "network_drivers/presentation/ssh/auth/auth.h"
#include "network_drivers/presentation/ssh/connection/connection.h"
#include "network_drivers/presentation/ssh/network/network.h"
#include "network_drivers/presentation/ssh/ssh.h"
#include "network_drivers/presentation/ssh/transport/transport.h"
#include "network_drivers/transport/tcp/common.h"            // TcpConn, conn_pool: the slots a session runs on
#include "network_drivers/transport/tcp/protocol/protocol.h" // ConnPool: the slot a session closes
#include "network_drivers/transport/tcp/server/server.h"     // TcpListener: the port a forward binds
#include "network_drivers/transport/tcp/tcp.h"
#include "server/clock/clock.h"
#include "server/core/proto_handler.h"
#if PROTOCORE_ENABLE_SSH_ZLIB
#include "network_drivers/presentation/ssh/transport/comp.h"
#endif

// The listening role's own state (RFC 4253 sec 4.1): whether each slot's connection is to be torn
// down once the current pass finishes. The slot-to-socket binding is the network layer's.
struct SshServerStorage
{
    volatile proto_bool close[MAX_SSH_CONNS];
};

/**
 * @brief The listening role's state and the calls that reach it - what SshServerNs points at.
 *
 * @var SshServerInternal::store  the per-slot teardown flags
 * @var SshServerInternal::ns     the handle a caller sets a call's members on
 */
struct SshServerInternal
{
    struct SshServerStorage *store;
    SshServerNs *ns;
};

static struct SshServerStorage s_store;

static struct SshServerInternal s_srv = {.store = &s_store, .ns = &SshServer};

// ssh_pkt_recv handler: dispatch one decrypted message, remember fatal results.
static void msg_handler(uint8_t i, uint8_t msg_type, const uint8_t *payload, size_t len)
{
    if (ssh_transport_dispatch(i, msg_type, payload, len) < 0)
    {
        s_store.close[i] = PROTO_TRUE;
    }
}

static void net_accept(uint8_t conn_slot);
static void net_rx(uint8_t conn_slot);
static void net_close(uint8_t conn_slot);
static void net_poll(uint8_t conn_slot);

// The SSH connection ProtoHandler (Layer 5 dispatch seam), installed by
// Session.proto->register_builtins() through the accessor, so this module carries no dependency on
// the session layer. Designated, so a member's position does not decide what it binds to.
static const ProtoHandler s_ssh_handler = {
    .on_accept = net_accept, .on_data = net_rx, .on_close = net_close, .on_poll = net_poll};

void ssh_protocore_handler(struct SshServerInternal *restrict ctx)
{
    ctx->ns->handler = &s_ssh_handler;
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------

static void net_accept(uint8_t conn_slot)
{
    TcpConn *conn = &conn_pool[conn_slot];

    // Take a free SSH session slot and bind this socket to it.
    SshNetwork.slot_free(SshNetwork.internal);
    const uint8_t j = SshNetwork.u8;
    if (j != 0xFF)
    {
        SshNetwork.ssh_slot = j;
        SshNetwork.handle = (int)conn_slot;
        SshNetwork.stream.kind = SSH_STREAM_ACCEPTED;
        SshNetwork.claim(SshNetwork.internal);
    }
    if (j == 0xFF || SshNetwork.i32 != 0)
    {
        // No SSH capacity: drop the connection (transport owns the teardown).
        ConnPool.slot = conn->id;
        ConnPool.close(ConnPool.internal);
        return;
    }

    conn->proto_slot = j;
    s_store.close[j] = PROTO_FALSE;

    ssh_transport_init(j);
    ssh_pkt_init(j);
    // Role is a property of what was acquired: this slot was claimed by an inbound accept, so it is
    // the server end. It selects which direction's keys each cipher and MAC site reads, and
    // ssh_pkt_init has already set it - the accepting role is the codec's default.
    SshConnection.chan.slot = j;
    SshConnection.channel_init(SshConnection.internal);
#if PROTOCORE_ENABLE_SSH_ZLIB
    ssh_comp_reset(j); // clear compression state for the new connection (not run on a re-key)
#endif

    ssh_net_version_exchange_send(j, conn_slot);
}

static void close_conn(uint8_t conn_slot)
{
    ConnPool.slot = conn_slot;
    ConnPool.close(ConnPool.internal); // transport owns detach + slot reset + close
    net_close(conn_slot);
}

static void net_rx(uint8_t conn_slot)
{
    TcpConn *conn = &conn_pool[conn_slot];
    uint8_t j = conn->proto_slot;
    SshNetwork.ssh_slot = j;
    SshNetwork.conn_slot = conn_slot;
    SshNetwork.owns(SshNetwork.internal);
    if (!SshNetwork.ok)
    {
        return;
    }

    // Drain the ring into this slot's own read scratch via the transport read API.
    uint8_t *buf = ssh_conn_slot(j) + SSH_OFF_RX_READ;
    ConnPool.slot = conn_slot;
    ConnPool.io.buf = buf;
    ConnPool.io.cap = RX_BUF_SIZE;
    ConnPool.read(ConnPool.internal);
    size_t n = ConnPool.n;
    if (n == 0)
    {
        return;
    }

    size_t off = 0;
    int ident = ssh_transport_version_exchange_recv(j, buf, n, &off);
    if (ident < 0)
    {
        close_conn(conn_slot);
        return;
    }
    if (ident == 0)
    {
        return; // need more identification string bytes
    }

    if (off < n)
    {
        ssh_pkt_recv(j, buf + off, n - off, msg_handler, &ssh_sess[j].in);
    }

    protocore_secure_wipe(buf, n);

    SshNetwork.conn_slot = conn_slot;
    SshNetwork.ssh_slot = j;
    SshNetwork.tx_drain(SshNetwork.internal); // the reply the dispatch framed leaves on this pass

    if (s_store.close[j])
    {
        close_conn(conn_slot);
    }
}

static void net_close(uint8_t conn_slot)
{
    TcpConn *conn = &conn_pool[conn_slot];
    uint8_t j = conn->proto_slot;
    if (j < MAX_SSH_CONNS)
    {
#if PROTOCORE_SSH_PORT_FORWARD
        SshConnection.fwd.slot = j;
        SshConnection.forward_reset(SshConnection.internal); // close any forwarded TCP sockets this connection owned
#endif
        // Zero all key material and session state for this slot. The span holds every byte the
        // connection used, so wiping it covers all four regions in one pass.
        ssh_keymat_wipe(j);
        SshAuth.slot = j;
        SshAuth.reset(SshAuth.internal);
        protocore_secure_wipe(ssh_conn_slot(j), SSH_SLOT_BORROW);
        protocore_secure_wipe(&ssh_sess[j], sizeof(SshSession));
        SshNetwork.ssh_slot = j;
        SshNetwork.release(SshNetwork.internal);
    }
    conn->proto_slot = PROTOCORE_PROTO_SLOT_NONE;
}

static void net_poll(uint8_t conn_slot)
{
    TcpConn *conn = &conn_pool[conn_slot];
    ConnPool.slot = conn_slot;
    ConnPool.active(ConnPool.internal);
    if (!ConnPool.ok)
    {
        return;
    }
    uint8_t j = conn->proto_slot;
    SshNetwork.ssh_slot = j;
    SshNetwork.conn_slot = conn_slot;
    SshNetwork.owns(SshNetwork.internal);
    if (!SshNetwork.ok)
    {
        return;
    }

    // RFC 4252 sec 4: a connection that has not authenticated inside the timeout is disconnected.
    SshAuth.slot = j;
    SshAuth.timed_out(SshAuth.internal);
    if (SshAuth.ok)
    {
        static const char desc[] = "authentication timeout";
        uint8_t out[13 + sizeof(desc) - 1];
        size_t n = 0;
        if (ssh_pkt_build_disconnect(SSH_DISCONNECT_BY_APPLICATION, desc, sizeof(desc) - 1, out, &n, sizeof(out)) == 0)
        {
            SshNetwork.ssh_slot = j;
            SshNetwork.msg.payload = out;
            SshNetwork.msg.len = n;
            SshNetwork.emit(SshNetwork.internal);
            SshNetwork.conn_slot = conn_slot;
            SshNetwork.ssh_slot = j;
            SshNetwork.tx_drain(SshNetwork.internal); // the codec flagged it; put it out before the close
        }
        ConnPool.slot = conn_slot;
        ConnPool.close(ConnPool.internal);
        return;
    }

    ssh_transport_key_re_exchange(j);
    SshAuth.slot = j;
    SshAuth.passwd_change_reply(SshAuth.internal);

#if PROTOCORE_SSH_PORT_FORWARD
    SshConnection.fwd.slot = j;
    SshConnection.forward_pump(SshConnection.internal);
#endif

    SshNetwork.conn_slot = conn_slot;
    SshNetwork.ssh_slot = j;
    SshNetwork.tx_drain(SshNetwork.internal); // after the codec, so a packet framed on this pass leaves on it
}

// ---------------------------------------------------------------------------
// Forwarded-port connections (RFC 4254 sec 7.2, ProtoConn::PROTO_SSH_RFWD)
// ---------------------------------------------------------------------------

#if PROTOCORE_SSH_PORT_FORWARD

static void rfwd_on_accept(uint8_t conn_slot)
{
    uint8_t ssh_slot = 0;
    uint16_t bind_port = 0;
    const char *bind_addr = NULL;
    SshConnection.fwd.listener_idx = conn_pool[conn_slot].listener_id;
    SshConnection.forward_binding(SshConnection.internal);
    ssh_slot = SshConnection.fwd.out_slot;
    bind_port = SshConnection.fwd.bind_port;
    bind_addr = SshConnection.fwd.bind_addr;
    if (!SshConnection.ok)
    {
        ConnPool.slot = conn_slot;
        ConnPool.close(ConnPool.internal); // no binding owns this listener (stale): drop
        return;
    }
    // Originator address (advisory, RFC 4254 sec 7.2); the peer port is not exposed by the
    // transport, so it is reported as 0.
    char orig[PROTOCORE_IP_STR_MAX];
    orig[0] = 0;
    protocore_ip rip;
    ConnPool.slot = conn_slot;
    ConnPool.out = &rip;
    ConnPool.remote_addr(ConnPool.internal);
    if (ConnPool.ok)
    {
        Ip.args.ip = &rip;
        Ip.args.buf = orig;
        Ip.args.cap = sizeof(orig);
        Ip.format(Ip.internal);
    }
    // Open the forwarded-tcpip channel back to the client, echoing the requested bind
    // address as the "address that was connected".
    SshConnection.chan.slot = ssh_slot;
    SshConnection.fwd.conn_addr = bind_addr[0] ? bind_addr : "0.0.0.0";
    SshConnection.fwd.conn_port = bind_port;
    SshConnection.fwd.orig_addr = orig;
    SshConnection.fwd.orig_port = 0;
    SshConnection.channel_send_open_forwarded(SshConnection.internal);
    int ch = SshConnection.i32;
    if (ch < 0)
    {
        ConnPool.slot = conn_slot;
        ConnPool.close(ConnPool.internal); // SSH connection gone or channel pool full
        return;
    }
    SshNetwork.ssh_slot = ssh_slot;
    SshNetwork.stream.channel = (uint32_t)ch;
    SshNetwork.dial.cid = (int)conn_slot;
    SshNetwork.chan_adopt(SshNetwork.internal);
}

static void rfwd_on_data(uint8_t conn_slot)
{
    uint8_t slot = 0;
    uint32_t channel = 0;
    SshNetwork.dial.cid = (int)conn_slot;
    SshNetwork.chan_by_cid(SshNetwork.internal);
    if (SshNetwork.ok)
    {
        slot = SshNetwork.ssh_slot;
        channel = SshNetwork.stream.channel;
        SshConnection.fwd.slot = slot;
        SshConnection.forward_pump(SshConnection.internal); // the window gates it until the client confirms
    }
}

static void rfwd_on_close(uint8_t conn_slot)
{
    uint8_t slot = 0;
    uint32_t channel = 0;
    SshNetwork.dial.cid = (int)conn_slot;
    SshNetwork.chan_by_cid(SshNetwork.internal);
    if (!SshNetwork.ok)
    {
        return;
    }
    slot = SshNetwork.ssh_slot;
    channel = SshNetwork.stream.channel;
    SshConnection.chan.slot = slot;
    SshConnection.chan.channel = channel;
    SshConnection.channel_send_eof(SshConnection.internal); // the socket is gone, so this direction sends no more
    SshConnection.chan.slot = slot;
    SshConnection.chan.channel = channel;
    SshConnection.channel_send_close(SshConnection.internal); // then terminate the channel
    SshNetwork.ssh_slot = slot;
    SshNetwork.stream.channel = channel;
    SshNetwork.chan_close(SshNetwork.internal);
}

static void rfwd_on_poll(uint8_t conn_slot)
{
    // The dispatch loop now polls every slot uniformly; it used to poll only ACTIVE slots, so preserve
    // that gate here (a closing/free forward slot has nothing to pump).
    ConnPool.slot = conn_slot;
    ConnPool.active(ConnPool.internal);
    if (!ConnPool.ok)
    {
        return;
    }
    uint8_t slot = 0;
    uint32_t channel = 0;
    SshNetwork.dial.cid = (int)conn_slot;
    SshNetwork.chan_by_cid(SshNetwork.internal);
    if (!SshNetwork.ok)
    {
        return;
    }
    slot = SshNetwork.ssh_slot;
    channel = SshNetwork.stream.channel;
    // The client closed its side of the channel -> close the accepted socket.
    SshConnection.chan.slot = slot;
    SshConnection.chan.id = channel;
    SshConnection.chan_by_id(SshConnection.internal);
    if (SshConnection.found == NULL)
    {
        SshNetwork.ssh_slot = slot;
        SshNetwork.stream.channel = channel;
        SshNetwork.chan_close(SshNetwork.internal);
        return;
    }
    SshConnection.fwd.slot = slot;
    SshConnection.forward_pump(SshConnection.internal); // drain anything the window blocked earlier
}

static const ProtoHandler s_rfwd_handler = {
    .on_accept = rfwd_on_accept, .on_data = rfwd_on_data, .on_close = rfwd_on_close, .on_poll = rfwd_on_poll};

// RFC 4254 sec 7.1: the socket a binding accepts on. The pool and its dynamic create/stop are the
// listening role's, so the connection protocol names a port and gets a handle back.
void ssh_rfwd_listener_open(struct SshServerInternal *restrict ctx)
{
    const uint16_t bind_port = ctx->ns->bind_port;
    int li = -1;
    for (int k = 0; k < MAX_LISTENERS; k++)
    {
        if (!listener_pool[k].active) // the application's own listeners are already active
        {
            li = k;
            break;
        }
    }
    if (li < 0)
    {
        ctx->ns->i32 = -1;
        return; // no listener capacity
    }
    // Dynamic (tcpip_thread-marshaled) create: this runs in the SSH worker task.
    TcpListener.idx = (uint8_t)li;
    TcpListener.bind.port = bind_port;
    TcpListener.bind.proto = PROTO_SSH_RFWD;
    TcpListener.add_dynamic(TcpListener.internal);
    if (TcpListener.i32 != 1)
    {
        ctx->ns->i32 = -1;
        return; // bind failed (port already in use, etc.)
    }
    ctx->ns->i32 = li;
    return;
}

void ssh_rfwd_listener_close(struct SshServerInternal *restrict ctx)
{
    const int handle = ctx->ns->handle;
    if (handle >= 0 && handle < MAX_LISTENERS)
    {
        TcpListener.idx = (uint8_t)handle;
        TcpListener.stop_dynamic(TcpListener.internal);
    }
}

void ssh_protocore_rfwd_handler(struct SshServerInternal *restrict ctx)
{
    ctx->ns->handler = &s_rfwd_handler;
}

#else

// PROTOCORE_SSH_PORT_FORWARD is 0: the handle still carries the remote-forward calls, so they exist
// and report nothing bound.
void ssh_rfwd_listener_open(struct SshServerInternal *restrict ctx)
{
    ctx->ns->i32 = -1;
}
void ssh_rfwd_listener_close(struct SshServerInternal *restrict ctx)
{
    (void)ctx;
}
void ssh_protocore_rfwd_handler(struct SshServerInternal *restrict ctx)
{
    ctx->ns->handler = NULL;
}

#endif // PROTOCORE_SSH_PORT_FORWARD

// Designated, so a member's position in the struct does not decide what it binds to.
SshServerNs SshServer = {.rfwd_listener_open = ssh_rfwd_listener_open,
                         .rfwd_listener_close = ssh_rfwd_listener_close,
                         .proto_handler = ssh_protocore_handler,
                         .rfwd_proto_handler = ssh_protocore_rfwd_handler,
                         .internal = &s_srv};
