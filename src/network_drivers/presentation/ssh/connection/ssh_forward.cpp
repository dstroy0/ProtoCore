// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_forward.cpp
 * @brief SSH direct-tcpip port-forwarding owner - outbound TCP + byte bridge.
 */

#include "network_drivers/presentation/ssh/connection/ssh_forward.h"

#if PC_SSH_PORT_FORWARD

#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "network_drivers/transport/client.h"

// Remote forwarding (ssh -R) uses the inbound transport + listener layer directly:
// it allocates a real listener and bridges each accepted socket to a server-initiated
// forwarded-tcpip channel.
#include "network_drivers/network/ip.h"
#include "network_drivers/session/proto_handler.h"
#include "network_drivers/transport/listener.h"
#include "network_drivers/transport/tcp.h"

#include <string.h>

// One forwarded TCP connection: an SSH channel bridged to a client-transport slot.
struct SshFwd
{
    bool active;
    uint8_t ssh_slot;
    uint32_t channel; // local channel id (== ssh_chan row index)
    int cid;          // pc_client connection id
};

// All SSH local-forward (ssh -L) state, owned by one instance (internal linkage): the active
// forward table and the policy callback. One named owner, unreachable cross-TU.
struct SshFwdCtx
{
    SshFwd fwd[PC_SSH_FWD_MAX];
    SshForwardPolicyCb policy = nullptr;
};
static SshFwdCtx s_fwd;

// Target -> client iterations per channel per poll: bounds the work each loop so
// one busy tunnel cannot starve the others (PC_SSH_FWD_CHUNK bytes each).
static const int kFwdBurst = 4;

static int fwd_find_free()
{
    for (int i = 0; i < PC_SSH_FWD_MAX; i++)
    {
        if (!s_fwd.fwd[i].active)
        {
            return i;
        }
    }
    return -1;
}

static SshFwd *fwd_lookup(uint8_t ssh_slot, uint32_t channel)
{
    for (int i = 0; i < PC_SSH_FWD_MAX; i++)
    {
        if (s_fwd.fwd[i].active && s_fwd.fwd[i].ssh_slot == ssh_slot && s_fwd.fwd[i].channel == channel)
        {
            return &s_fwd.fwd[i];
        }
    }
    return nullptr;
}

// ===========================================================================
// Remote forwarding (ssh -R): the client asks the server to LISTEN on a port and
// tunnel each accepted connection back over a server-initiated forwarded-tcpip
// channel. One bind == one real listener_pool[] slot; one bridge == one accepted
// conn_pool slot glued to one SSH channel. All storage static (no heap).
// ===========================================================================

// A remote-forward binding: a listener this SSH connection asked us to open.
struct SshRFwdBind
{
    bool active;
    uint8_t ssh_slot;
    uint8_t listener_idx;                // slot in listener_pool[]
    uint16_t bind_port;                  // port bound on the device
    char bind_addr[PC_SSH_FWD_HOST_MAX]; // address the client requested (echoed in CHANNEL_OPEN)
};

// One bridged connection: an accepted TCP socket glued to a forwarded-tcpip channel.
struct SshRFwdBridge
{
    bool active;
    bool confirmed;    // client CONFIRMED the channel: bytes may flow
    uint8_t ssh_slot;  // the owning SSH connection
    uint8_t conn_slot; // the accepted TCP conn_pool slot
    uint32_t channel;  // our local forwarded-tcpip channel id
};

// All SSH remote-forward (ssh -R) state, owned by one instance (internal linkage): the listener
// bindings and the accepted-connection bridges. One named owner, unreachable cross-TU.
struct SshRFwdCtx
{
    SshRFwdBind rbind[PC_SSH_RFWD_MAX];
    SshRFwdBridge rbridge[PC_SSH_RFWD_BRIDGE_MAX];
};
static SshRFwdCtx s_rfwd;

static int rbind_find_free()
{
    for (int i = 0; i < PC_SSH_RFWD_MAX; i++)
    {
        if (!s_rfwd.rbind[i].active)
        {
            return i;
        }
    }
    return -1;
}
static SshRFwdBind *rbind_by_listener(uint8_t listener_idx)
{
    for (int i = 0; i < PC_SSH_RFWD_MAX; i++)
    {
        if (s_rfwd.rbind[i].active && s_rfwd.rbind[i].listener_idx == listener_idx)
        {
            return &s_rfwd.rbind[i];
        }
    }
    return nullptr;
}
static SshRFwdBind *rbind_find(uint8_t ssh_slot, uint16_t port)
{
    for (int i = 0; i < PC_SSH_RFWD_MAX; i++)
    {
        if (s_rfwd.rbind[i].active && s_rfwd.rbind[i].ssh_slot == ssh_slot && s_rfwd.rbind[i].bind_port == port)
        {
            return &s_rfwd.rbind[i];
        }
    }
    return nullptr;
}
static int rbridge_find_free()
{
    for (int i = 0; i < PC_SSH_RFWD_BRIDGE_MAX; i++)
    {
        if (!s_rfwd.rbridge[i].active)
        {
            return i;
        }
    }
    return -1;
}
static SshRFwdBridge *rbridge_by_conn(uint8_t conn_slot)
{
    for (int i = 0; i < PC_SSH_RFWD_BRIDGE_MAX; i++)
    {
        if (s_rfwd.rbridge[i].active && s_rfwd.rbridge[i].conn_slot == conn_slot)
        {
            return &s_rfwd.rbridge[i];
        }
    }
    return nullptr;
}
static SshRFwdBridge *rbridge_by_channel(uint8_t ssh_slot, uint32_t channel)
{
    for (int i = 0; i < PC_SSH_RFWD_BRIDGE_MAX; i++)
    {
        if (s_rfwd.rbridge[i].active && s_rfwd.rbridge[i].ssh_slot == ssh_slot && s_rfwd.rbridge[i].channel == channel)
        {
            return &s_rfwd.rbridge[i];
        }
    }
    return nullptr;
}

// Move accepted-socket bytes to the client over the SSH channel. Read only what the
// channel peer window (and max packet) allow so pc_ssh_conn_send never has to reject
// bytes already pulled from the ring; bounded per poll so one tunnel cannot starve
// the others. Leftover bytes stay in the rx ring (backpressure) for the next poll.
static void rbridge_pump_to_client(SshRFwdBridge *br)
{
    if (br->channel >= PC_SSH_MAX_CHANNELS)
    {
        return;
    }
    SshChannel *c = &ssh_chan[br->ssh_slot][br->channel];
    uint8_t buf[PC_SSH_FWD_CHUNK];
    for (int burst = 0; burst < 4; burst++)
    {
        size_t avail = pc_conn_available(br->conn_slot);
        uint32_t win = c->peer_window;
        if (avail == 0 || win == 0 || !c->open)
        {
            break;
        }
        size_t budget = avail;
        if (budget > win)
        {
            budget = win;
        }
        if (c->peer_max_pkt && budget > c->peer_max_pkt)
        {
            budget = c->peer_max_pkt;
        }
        if (budget > sizeof(buf))
        {
            budget = sizeof(buf);
        }
        size_t n = pc_conn_read(br->conn_slot, buf, budget);
        if (n == 0)
        {
            break;
        }
        if (pc_ssh_conn_send(br->ssh_slot, br->channel, buf, n) < 0)
        {
            break; // channel gone: retry next poll
        }
    }
}

// direct-tcpip open: check policy, connect to host:port (blocking), bind a slot.
// Returns 0 to confirm the channel, < 0 to refuse (denied / connect failed / full).
static int on_forward_open(uint8_t ssh_slot, uint32_t channel, const char *host, size_t host_len, uint16_t port)
{
    int idx = fwd_find_free();
    if (idx < 0)
    {
        return -1; // no forward capacity
    }
    char hbuf[PC_SSH_FWD_HOST_MAX];
    if (host_len == 0 || host_len >= sizeof(hbuf))
    {
        return -1;
    }
    memcpy(hbuf, host, host_len);
    hbuf[host_len] = 0;
    if (s_fwd.policy && !s_fwd.policy(hbuf, port))
    {
        return -1; // target administratively denied
    }
    int cid = pc_client_open(hbuf, port, PC_SSH_FWD_CONNECT_MS); // blocks on DNS + connect
    if (cid < 0)
    {
        return -1; // -> CHANNEL_OPEN_FAILURE (connect failed)
    }
    s_fwd.fwd[idx].active = true;
    s_fwd.fwd[idx].ssh_slot = ssh_slot;
    s_fwd.fwd[idx].channel = channel;
    s_fwd.fwd[idx].cid = cid;
    return 0;
}

// Inbound channel bytes. direct-tcpip (ssh -L): client -> outbound target socket.
// forwarded-tcpip (ssh -R): client -> the accepted socket we bridged back to it.
static void on_forward_data(uint8_t ssh_slot, uint32_t channel, const uint8_t *data, size_t len)
{
    SshFwd *f = fwd_lookup(ssh_slot, channel);
    if (f)
    {
        pc_client_send(f->cid, data, len);
        return;
    }
    SshRFwdBridge *br = rbridge_by_channel(ssh_slot, channel);
    if (br && br->confirmed)
    {
        pc_conn_send(br->conn_slot, data, (u16_t)len);
        pc_conn_flush(br->conn_slot);
    }
}

// ---------------------------------------------------------------------------
// Remote-forward seam (GLOBAL_REQUEST tcpip-forward / cancel-tcpip-forward, ssh -R)
// ---------------------------------------------------------------------------

// Open a listener bound to bind_port and remember it for this SSH connection.
// Returns the bound port (>= 0) on success, -1 to refuse.
static int on_rforward_open(uint8_t ssh_slot, const char *addr, size_t addr_len, uint16_t bind_port)
{
    if (bind_port == 0)
    {
        return -1; // ephemeral-port allocation is not supported: require an explicit port
    }
    if (rbind_find(ssh_slot, bind_port))
    {
        return -1; // already forwarding this port on this connection
    }
    int bi = rbind_find_free();
    if (bi < 0)
    {
        return -1; // remote-forward table full
    }
    // Find a free listener_pool slot (the app's own listeners are .active).
    int li = -1;
    for (int k = 0; k < MAX_LISTENERS; k++)
    {
        if (!listener_pool[k].active)
        {
            li = k;
            break;
        }
    }
    if (li < 0)
    {
        return -1; // no listener capacity
    }
    // Dynamic (tcpip_thread-marshaled) create: this runs in the SSH worker task.
    if (listener_add_dynamic((uint8_t)li, bind_port, ConnProto::PROTO_SSH_RFWD) != 1)
    {
        return -1; // bind failed (port already in use, etc.)
    }

    s_rfwd.rbind[bi].active = true;
    s_rfwd.rbind[bi].ssh_slot = ssh_slot;
    s_rfwd.rbind[bi].listener_idx = (uint8_t)li;
    s_rfwd.rbind[bi].bind_port = bind_port;
    size_t al = addr_len < sizeof(s_rfwd.rbind[bi].bind_addr) - 1 ? addr_len : sizeof(s_rfwd.rbind[bi].bind_addr) - 1;
    memcpy(s_rfwd.rbind[bi].bind_addr, addr, al);
    s_rfwd.rbind[bi].bind_addr[al] = 0;
    return bind_port;
}

// Cancel a remote forward: stop accepting new connections (existing bridges finish).
static int on_rforward_cancel(uint8_t ssh_slot, const char *addr, size_t addr_len, uint16_t bind_port)
{
    (void)addr;
    (void)addr_len;
    SshRFwdBind *b = rbind_find(ssh_slot, bind_port);
    if (!b)
    {
        return -1;
    }
    listener_stop_dynamic(b->listener_idx);
    b->active = false;
    return 0;
}

// The client's reply to a server-initiated forwarded-tcpip open.
static void on_forward_confirm(uint8_t ssh_slot, uint32_t channel, bool ok)
{
    SshRFwdBridge *br = rbridge_by_channel(ssh_slot, channel);
    if (!br)
    {
        return;
    }
    if (ok)
    {
        br->confirmed = true; // bytes may now flow (pumped on the next poll)
    }
    else
    {
        pc_conn_close(br->conn_slot); // client refused the tunnel: drop the accepted socket
        br->active = false;
    }
}

// ---------------------------------------------------------------------------
// ConnProto::PROTO_SSH_RFWD handler: an inbound connection on a forwarded port.
// ---------------------------------------------------------------------------

static void rfwd_on_accept(uint8_t conn_slot)
{
    SshRFwdBind *b = rbind_by_listener(conn_pool[conn_slot].listener_id);
    if (!b)
    {
        pc_conn_close(conn_slot); // no binding owns this listener (stale): drop
        return;
    }
    int idx = rbridge_find_free();
    if (idx < 0)
    {
        pc_conn_close(conn_slot); // bridge table full
        return;
    }
    // Originator address (advisory, RFC 4254 §7.2); the peer port is not exposed by the
    // transport, so it is reported as 0.
    char orig[PC_IP_STR_MAX];
    orig[0] = 0;
    pc_ip rip;
    if (pc_conn_remote_addr(conn_slot, &rip))
    {
        pc_ip_format(&rip, orig, sizeof(orig));
    }
    // Open the forwarded-tcpip channel back to the client, echoing the requested bind
    // address as the "address that was connected".
    int ch = pc_ssh_conn_open_forwarded(b->ssh_slot, b->bind_addr[0] ? b->bind_addr : "0.0.0.0", b->bind_port, orig, 0);
    if (ch < 0)
    {
        pc_conn_close(conn_slot); // SSH connection gone or channel pool full
        return;
    }
    s_rfwd.rbridge[idx].active = true;
    s_rfwd.rbridge[idx].confirmed = false;
    s_rfwd.rbridge[idx].ssh_slot = b->ssh_slot;
    s_rfwd.rbridge[idx].conn_slot = conn_slot;
    s_rfwd.rbridge[idx].channel = (uint32_t)ch;
}

static void rfwd_on_data(uint8_t conn_slot)
{
    SshRFwdBridge *br = rbridge_by_conn(conn_slot);
    if (br && br->confirmed)
    {
        rbridge_pump_to_client(br); // otherwise the bytes wait in the ring until confirmed
    }
}

static void rfwd_on_close(uint8_t conn_slot)
{
    SshRFwdBridge *br = rbridge_by_conn(conn_slot);
    if (!br)
    {
        return;
    }
    pc_ssh_conn_close_channel(br->ssh_slot, br->channel); // tell the client EOF + CLOSE
    br->active = false;
}

static void rfwd_on_poll(uint8_t conn_slot)
{
    // The dispatch loop now polls every slot uniformly; it used to poll only ACTIVE slots, so preserve
    // that gate here (a closing/free forward slot has nothing to pump).
    if (!pc_conn_active(conn_slot))
    {
        return;
    }
    SshRFwdBridge *br = rbridge_by_conn(conn_slot);
    if (!br || !br->confirmed)
    {
        return;
    }
    // The client closed its side of the channel -> close the accepted socket.
    if (br->channel < PC_SSH_MAX_CHANNELS && !ssh_chan[br->ssh_slot][br->channel].open)
    {
        pc_conn_close(conn_slot);
        br->active = false;
        return;
    }
    rbridge_pump_to_client(br); // drain anything the window blocked earlier
}

static const ProtoHandler s_rfwd_handler = {rfwd_on_accept, rfwd_on_data, rfwd_on_close, rfwd_on_poll};

void pc_ssh_forward_set_policy_cb(SshForwardPolicyCb cb)
{
    s_fwd.policy = cb;
}

void pc_ssh_forward_begin()
{
    for (int i = 0; i < PC_SSH_FWD_MAX; i++)
    {
        s_fwd.fwd[i].active = false;
    }
    for (int i = 0; i < PC_SSH_RFWD_MAX; i++)
    {
        s_rfwd.rbind[i].active = false;
    }
    for (int i = 0; i < PC_SSH_RFWD_BRIDGE_MAX; i++)
    {
        s_rfwd.rbridge[i].active = false;
    }
    pc_ssh_channel_set_forward_open_cb(on_forward_open);
    pc_ssh_channel_set_forward_data_cb(on_forward_data);
    // Remote forwarding (ssh -R): the request/cancel seam, the open-confirmation
    // callback, and the accept handler for connections on a forwarded port.
    pc_ssh_channel_set_rforward_open_cb(on_rforward_open);
    pc_ssh_channel_set_rforward_cancel_cb(on_rforward_cancel);
    pc_ssh_channel_set_forward_confirm_cb(on_forward_confirm);
    proto_register(ConnProto::PROTO_SSH_RFWD, &s_rfwd_handler);
}

void pc_ssh_forward_pump(uint8_t ssh_slot)
{
    uint8_t buf[PC_SSH_FWD_CHUNK];
    for (int i = 0; i < PC_SSH_FWD_MAX; i++)
    {
        SshFwd *f = &s_fwd.fwd[i];
        if (!f->active || f->ssh_slot != ssh_slot)
        {
            continue;
        }
        if (f->channel >= PC_SSH_MAX_CHANNELS) // defensive: stale binding
        {
            pc_client_close(f->cid);
            f->active = false;
            continue;
        }
        SshChannel *c = &ssh_chan[ssh_slot][f->channel];

        // Client closed its side of the channel: drop the target socket.
        if (!c->open)
        {
            pc_client_close(f->cid);
            f->active = false;
            continue;
        }

        // Target -> client: forward what the peer window allows, bounded per poll.
        for (int burst = 0; burst < kFwdBurst; burst++)
        {
            size_t avail = pc_client_available(f->cid);
            uint32_t win = c->peer_window;
            if (avail == 0 || win == 0)
            {
                break;
            }
            size_t budget = avail;
            if (budget > win)
            {
                budget = win;
            }
            if (c->peer_max_pkt && budget > c->peer_max_pkt)
            {
                budget = c->peer_max_pkt;
            }
            if (budget > sizeof(buf))
            {
                budget = sizeof(buf);
            }
            size_t n = pc_client_read(f->cid, buf, budget);
            if (n == 0)
            {
                break;
            }
            if (pc_ssh_conn_send(ssh_slot, f->channel, buf, n) < 0)
            {
                break; // sized to the window, so this should send; retry next poll
            }
        }

        // Target closed (FIN) and fully drained: EOF + CLOSE to the client, free.
        if (pc_client_is_closed(f->cid) && pc_client_available(f->cid) == 0)
        {
            pc_ssh_conn_close_channel(ssh_slot, f->channel);
            pc_client_close(f->cid);
            f->active = false;
        }
    }
}

void pc_ssh_forward_reset(uint8_t ssh_slot)
{
    // direct-tcpip (ssh -L): close outbound target sockets this connection owned.
    for (int i = 0; i < PC_SSH_FWD_MAX; i++)
    {
        if (s_fwd.fwd[i].active && s_fwd.fwd[i].ssh_slot == ssh_slot)
        {
            pc_client_close(s_fwd.fwd[i].cid);
            s_fwd.fwd[i].active = false;
        }
    }
    // remote (ssh -R): stop this connection's forwarded listeners and drop every
    // accepted socket it had bridged (the SSH channels go away with the connection).
    for (int i = 0; i < PC_SSH_RFWD_MAX; i++)
    {
        if (s_rfwd.rbind[i].active && s_rfwd.rbind[i].ssh_slot == ssh_slot)
        {
            listener_stop_dynamic(s_rfwd.rbind[i].listener_idx);
            s_rfwd.rbind[i].active = false;
        }
    }
    for (int i = 0; i < PC_SSH_RFWD_BRIDGE_MAX; i++)
    {
        if (s_rfwd.rbridge[i].active && s_rfwd.rbridge[i].ssh_slot == ssh_slot)
        {
            pc_conn_close(s_rfwd.rbridge[i].conn_slot);
            s_rfwd.rbridge[i].active = false;
        }
    }
}

#endif // PC_SSH_PORT_FORWARD
