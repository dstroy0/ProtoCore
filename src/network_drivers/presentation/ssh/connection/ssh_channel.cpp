// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_channel.cpp
 * @brief SSH connection protocol - multiplexed session channels (RFC 4254).
 *
 * The channel table is owned here; inbound messages are routed to a channel by the
 * recipient channel id they carry, and the local channel id is the channel's slot
 * index in its connection's pool (unique per connection, which is all RFC 4254
 * requires). Other layers go through these functions, never the table.
 */

#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h" // SSH_MSG_CHANNEL_*
#include <string.h>

SshChannel ssh_chan[MAX_SSH_CONNS][PC_SSH_MAX_CHANNELS];

// All SSH channel-layer callbacks, owned by one instance (internal linkage): the channel-data
// sink and the local/remote port-forward hooks. Grouped so it is one named owner, unreachable
// from any other translation unit. (The ssh_chan[][] table is the shared cross-TU substrate.)
struct SshChannelCtx
{
    SshChannelDataCb data_cb = nullptr;
    SshForwardOpenCb forward_open_cb = nullptr;
    SshForwardDataCb forward_data_cb = nullptr;
    SshRemoteForwardOpenCb rfwd_open_cb = nullptr;
    SshRemoteForwardCancelCb rfwd_cancel_cb = nullptr;
    SshForwardConfirmCb forward_confirm_cb = nullptr;
#if PC_ENABLE_SSH_SFTP
    SshSftpOpenCb pc_sftp_open_cb = nullptr;
    SshSftpDataCb pc_sftp_data_cb = nullptr;
#endif
#if PC_ENABLE_SSH_SCP
    SshScpOpenCb pc_scp_open_cb = nullptr;
    SshScpDataCb pc_scp_data_cb = nullptr;
#endif
};
static SshChannelCtx s_chcb;

void pc_ssh_channel_set_data_cb(SshChannelDataCb cb)
{
    s_chcb.data_cb = cb;
}

#if PC_ENABLE_SSH_SFTP
void pc_ssh_channel_set_sftp_open_cb(SshSftpOpenCb cb)
{
    s_chcb.pc_sftp_open_cb = cb;
}
void pc_ssh_channel_set_sftp_data_cb(SshSftpDataCb cb)
{
    s_chcb.pc_sftp_data_cb = cb;
}
#endif

#if PC_ENABLE_SSH_SCP
void pc_ssh_channel_set_scp_open_cb(SshScpOpenCb cb)
{
    s_chcb.pc_scp_open_cb = cb;
}
void pc_ssh_channel_set_scp_data_cb(SshScpDataCb cb)
{
    s_chcb.pc_scp_data_cb = cb;
}
#endif

void pc_ssh_channel_set_forward_open_cb(SshForwardOpenCb cb)
{
    s_chcb.forward_open_cb = cb;
}

void pc_ssh_channel_set_forward_data_cb(SshForwardDataCb cb)
{
    s_chcb.forward_data_cb = cb;
}

void pc_ssh_channel_set_rforward_open_cb(SshRemoteForwardOpenCb cb)
{
    s_chcb.rfwd_open_cb = cb;
}

void pc_ssh_channel_set_rforward_cancel_cb(SshRemoteForwardCancelCb cb)
{
    s_chcb.rfwd_cancel_cb = cb;
}

void pc_ssh_channel_set_forward_confirm_cb(SshForwardConfirmCb cb)
{
    s_chcb.forward_confirm_cb = cb;
}

void pc_ssh_channel_init(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    memset(ssh_chan[i], 0, sizeof(ssh_chan[i])); // reset every channel for this connection
}

// ---------------------------------------------------------------------------
// Wire helpers
// ---------------------------------------------------------------------------

static uint32_t rd_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

// Read an SSH string: out points at the data, *slen its length. Advances *off.
static bool rd_string(const uint8_t *p, size_t len, size_t *off, const uint8_t **out, uint32_t *slen)
{
    if (*off + 4 > len)
    {
        return false;
    }
    uint32_t n = rd_u32(p + *off);
    *off += 4;
    if (*off + n > len)
    {
        return false;
    }
    *out = p + *off;
    *slen = n;
    *off += n;
    return true;
}

// ---------------------------------------------------------------------------
// Channel table (owned here)
// ---------------------------------------------------------------------------

// The open channel @p id on connection @p i, or nullptr. local id == slot index.
static SshChannel *chan_by_id(uint8_t i, uint32_t id)
{
    // GCOVR_EXCL_LINE below: i is always in range - every caller rejects i >= MAX_SSH_CONNS before
    // looking a channel up, so only the id / open halves of this guard are exercisable.
    if (i >= MAX_SSH_CONNS || id >= PC_SSH_MAX_CHANNELS || !ssh_chan[i][id].open) // GCOVR_EXCL_LINE
    {
        return nullptr;
    }
    return &ssh_chan[i][id];
}

// A pending server-initiated channel @p id on connection @p i (awaiting the client's
// CONFIRMATION / FAILURE), or nullptr.
static SshChannel *chan_pending_by_id(uint8_t i, uint32_t id)
{
    // GCOVR_EXCL_LINE below: i is always in range (same reason as chan_by_id above).
    if (i >= MAX_SSH_CONNS || id >= PC_SSH_MAX_CHANNELS || !ssh_chan[i][id].pending) // GCOVR_EXCL_LINE
    {
        return nullptr;
    }
    return &ssh_chan[i][id];
}

// First free channel slot on connection @p i, or -1 if the pool is full. A pending
// (opened-but-unconfirmed) channel is in use just like an open one.
static int chan_alloc(uint8_t i)
{
    for (int c = 0; c < PC_SSH_MAX_CHANNELS; c++)
    {
        if (!ssh_chan[i][c].open && !ssh_chan[i][c].pending)
        {
            return c;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// CHANNEL_OPEN → CONFIRMATION / FAILURE
// ---------------------------------------------------------------------------

// CHANNEL_OPEN_FAILURE: byte || recipient(sender) || reason || desc || lang.
// reason: 1 admin-prohibited, 2 connect-failed, 3 unknown-type, 4 resource-shortage.
static int build_open_failure(uint8_t *out, size_t cap, uint32_t sender, uint32_t reason, size_t *out_len)
{
    if (cap < 17)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_OPEN_FAILURE;
    wr_u32(out + 1, sender);
    wr_u32(out + 5, reason);
    wr_u32(out + 9, 0);  // empty description
    wr_u32(out + 13, 0); // empty language
    *out_len = 17;
    return 0;
}

// CHANNEL_OPEN_CONFIRMATION: byte || recipient(peer) || sender(local) || window || max.
static int build_open_confirm(const SshChannel *c, uint8_t *out, size_t cap, size_t *out_len)
{
    if (cap < 17)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_OPEN_CONFIRM;
    wr_u32(out + 1, c->peer_id);
    wr_u32(out + 5, c->local_id);
    wr_u32(out + 9, c->local_window);
    wr_u32(out + 13, SSH_CHAN_MAX_PACKET);
    *out_len = 17;
    return 0;
}

// ---------------------------------------------------------------------------
// GLOBAL_REQUEST (RFC 4254 §4; §7.1 tcpip-forward / cancel-tcpip-forward)
// ---------------------------------------------------------------------------

int ssh_global_request_handle(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap)
{
    *out_len = 0;
    if (i >= MAX_SSH_CONNS || len < 1 || payload[0] != SSH_MSG_GLOBAL_REQUEST)
    {
        return -1;
    }

    size_t off = 1;
    const uint8_t *name;
    uint32_t name_len;
    if (!rd_string(payload, len, &off, &name, &name_len))
    {
        return -1;
    }
    if (off >= len)
    {
        return -1;
    }
    bool want_reply = payload[off++] != 0;

    bool is_fwd = (name_len == 13 && memcmp(name, "tcpip-forward", 13) == 0);
    bool is_cancel = (name_len == 20 && memcmp(name, "cancel-tcpip-forward", 20) == 0);

    if (is_fwd || is_cancel)
    {
        // Request-specific data: bind address (string) followed by bind port (uint32).
        const uint8_t *addr;
        uint32_t addr_len;
        if (!rd_string(payload, len, &off, &addr, &addr_len) || off + 4 > len)
        {
            return -1;
        }
        uint16_t bind_port = (uint16_t)rd_u32(payload + off);

        // The owner allocates (or cancels) the real listener; -1 means "refused".
        int bound = -1;
        if (is_fwd && s_chcb.rfwd_open_cb)
        {
            bound = s_chcb.rfwd_open_cb(i, (const char *)addr, addr_len, bind_port);
        }
        else if (is_cancel && s_chcb.rfwd_cancel_cb)
        {
            bound = s_chcb.rfwd_cancel_cb(i, (const char *)addr, addr_len, bind_port);
        }

        if (bound < 0)
        {
            if (want_reply) // refused: no owner, policy denied, or the table is full
            {
                if (cap < 1)
                {
                    return -1;
                }
                out[0] = SSH_MSG_REQUEST_FAILURE;
                *out_len = 1;
            }
            return 0;
        }

        if (want_reply)
        {
            // A tcpip-forward that requested port 0 echoes the allocated port
            // (RFC 4254 §7.1); a specific port and cancel reply bare success.
            if (is_fwd && bind_port == 0)
            {
                if (cap < 5)
                {
                    return -1;
                }
                out[0] = SSH_MSG_REQUEST_SUCCESS;
                wr_u32(out + 1, (uint32_t)(uint16_t)bound);
                *out_len = 5;
            }
            else
            {
                if (cap < 1)
                {
                    return -1;
                }
                out[0] = SSH_MSG_REQUEST_SUCCESS;
                *out_len = 1;
            }
        }
        return 0;
    }

    // Any other global request is unrecognized: RFC 4254 §4 -> REQUEST_FAILURE when the
    // client wants a reply, otherwise silently ignored. (Never UNIMPLEMENTED: the
    // GLOBAL_REQUEST message type is known; only this request name is not.)
    if (want_reply)
    {
        if (cap < 1)
        {
            return -1;
        }
        out[0] = SSH_MSG_REQUEST_FAILURE;
        *out_len = 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Server-initiated CHANNEL_OPEN (forwarded-tcpip, ssh -R) + its CONFIRM / FAILURE
// ---------------------------------------------------------------------------

int pc_ssh_channel_open_forwarded(uint8_t i, const char *conn_addr, uint16_t conn_port, const char *orig_addr,
                                  uint16_t orig_port, uint8_t *out, size_t *out_len, size_t cap)
{
    *out_len = 0;
    if (i >= MAX_SSH_CONNS || !conn_addr || !orig_addr)
    {
        return -1;
    }
    int slot = chan_alloc(i);
    if (slot < 0)
    {
        return -1; // channel pool full
    }

    const char *type = "forwarded-tcpip";
    size_t tl = 15, ca = strnlen(conn_addr, cap), oa = strnlen(orig_addr, cap);
    // byte || string(type) || u32 sender || u32 window || u32 maxpkt || string(conn_addr)
    //   || u32 conn_port || string(orig_addr) || u32 orig_port  (RFC 4254 §7.2)
    size_t need = 1 + (4 + tl) + 12 + (4 + ca) + 4 + (4 + oa) + 4;
    if (cap < need)
    {
        return -1;
    }

    size_t off = 0;
    out[off++] = SSH_MSG_CHANNEL_OPEN;
    wr_u32(out + off, (uint32_t)tl);
    memcpy(out + off + 4, type, tl);
    off += 4 + tl;
    wr_u32(out + off, (uint32_t)slot); // our sender channel id
    wr_u32(out + off + 4, SSH_CHAN_WINDOW);
    wr_u32(out + off + 8, SSH_CHAN_MAX_PACKET);
    off += 12;
    wr_u32(out + off, (uint32_t)ca);
    memcpy(out + off + 4, conn_addr, ca);
    off += 4 + ca;
    wr_u32(out + off, conn_port);
    off += 4;
    wr_u32(out + off, (uint32_t)oa);
    memcpy(out + off + 4, orig_addr, oa);
    off += 4 + oa;
    wr_u32(out + off, orig_port);
    off += 4;

    SshChannel *c = &ssh_chan[i][slot];
    c->open = false;
    c->pending = true; // awaiting the client's CHANNEL_OPEN_CONFIRMATION
    c->type = SshChanType::SSH_CHAN_FORWARDED_TCPIP;
    c->local_id = (uint32_t)slot;
    c->peer_id = 0;
    c->local_window = SSH_CHAN_WINDOW;
    c->peer_window = 0;
    c->peer_max_pkt = 0;

    *out_len = off;
    return slot;
}

int pc_ssh_channel_handle_open_confirm(uint8_t i, const uint8_t *payload, size_t len)
{
    // byte || recipient(our local id) || sender(peer id) || window || max packet.
    if (i >= MAX_SSH_CONNS || len < 17 || payload[0] != SSH_MSG_CHANNEL_OPEN_CONFIRM)
    {
        return -1;
    }
    SshChannel *c = chan_pending_by_id(i, rd_u32(payload + 1));
    if (!c)
    {
        return -1;
    }
    c->peer_id = rd_u32(payload + 5);
    c->peer_window = rd_u32(payload + 9);
    c->peer_max_pkt = rd_u32(payload + 13);
    c->pending = false;
    c->open = true;
    if (s_chcb.forward_confirm_cb)
    {
        s_chcb.forward_confirm_cb(i, c->local_id, true);
    }
    return 0;
}

int pc_ssh_channel_handle_open_failure(uint8_t i, const uint8_t *payload, size_t len)
{
    // byte || recipient(our local id) || reason || desc || lang.
    if (i >= MAX_SSH_CONNS || len < 5 || payload[0] != SSH_MSG_CHANNEL_OPEN_FAILURE)
    {
        return -1;
    }
    SshChannel *c = chan_pending_by_id(i, rd_u32(payload + 1));
    if (!c)
    {
        return -1;
    }
    uint32_t ch = c->local_id;
    c->pending = false;
    c->open = false; // free the slot; the client refused the forward
    if (s_chcb.forward_confirm_cb)
    {
        s_chcb.forward_confirm_cb(i, ch, false);
    }
    return 0;
}

int pc_ssh_channel_handle_open(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap)
{
    if (i >= MAX_SSH_CONNS || len < 1 || payload[0] != SSH_MSG_CHANNEL_OPEN)
    {
        return -1;
    }

    size_t off = 1;
    const uint8_t *type;
    uint32_t type_len;
    if (!rd_string(payload, len, &off, &type, &type_len))
    {
        return -1;
    }
    if (off + 12 > len)
    {
        return -1;
    }
    uint32_t sender = rd_u32(payload + off);
    uint32_t init_window = rd_u32(payload + off + 4);
    uint32_t max_pkt = rd_u32(payload + off + 8);
    off += 12;

    bool is_session = (type_len == 7 && memcmp(type, "session", 7) == 0);
    bool is_dtcpip = (type_len == 12 && memcmp(type, "direct-tcpip", 12) == 0);
    if (!is_session && !is_dtcpip)
    {
        return build_open_failure(out, cap, sender, 3u, out_len); // unknown channel type
    }

    // direct-tcpip data: host(string) port(u32) orig_host(string) orig_port(u32).
    const uint8_t *fhost = nullptr;
    uint32_t fhost_len = 0;
    uint16_t fport = 0;
    if (is_dtcpip)
    {
        if (!s_chcb.forward_open_cb)
        {
            return build_open_failure(out, cap, sender, 1u, out_len); // forwarding off: prohibited
        }
        if (!rd_string(payload, len, &off, &fhost, &fhost_len) || off + 4 > len)
        {
            return -1;
        }
        fport = (uint16_t)rd_u32(payload + off); // orig host/port follow but are advisory
    }

    int slot = chan_alloc(i);
    if (slot < 0)
    {
        return build_open_failure(out, cap, sender, 4u, out_len); // pool full
    }

    SshChannel *c = &ssh_chan[i][slot];
    c->open = true;
    c->type = is_dtcpip ? SshChanType::SSH_CHAN_DIRECT_TCPIP : SshChanType::SSH_CHAN_SESSION;
    c->local_id = (uint32_t)slot;
    c->peer_id = sender;
    c->local_window = SSH_CHAN_WINDOW;
    c->peer_window = init_window;
    c->peer_max_pkt = max_pkt;

    if (is_dtcpip)
    {
        // The owner does the actual TCP connect (no I/O in this codec); on refusal
        // free the channel and fail closed.
        if (s_chcb.forward_open_cb(i, c->local_id, (const char *)fhost, fhost_len, fport) < 0)
        {
            c->open = false;
            return build_open_failure(out, cap, sender, 2u, out_len); // connect failed
        }
    }
    return build_open_confirm(c, out, cap, out_len);
}

// ---------------------------------------------------------------------------
// CHANNEL_REQUEST → SUCCESS / FAILURE
// ---------------------------------------------------------------------------

#if PC_ENABLE_SSH_SFTP || PC_ENABLE_SSH_SCP
namespace
{
// A subsystem/exec CHANNEL_REQUEST may name a file-transfer service (SFTP or SCP). Tag @p c and fire the
// matching open callback when it does; @p off points at the request-specific arg and may be advanced. Flips
// *accept true for an accepted SFTP subsystem (exec is already in the base accept set).
void classify_file_transfer_request(uint8_t i, SshChannel *c, const uint8_t *rtype, uint32_t rtype_len,
                                    const uint8_t *payload, size_t len, size_t *off, bool *accept)
{
#if !PC_ENABLE_SSH_SFTP
    (void)accept; // only the SFTP subsystem path flips acceptance; scp exec is already accepted
#endif
#if PC_ENABLE_SSH_SFTP
    // subsystem "sftp": not in the base accept set, so accept it here and tag the channel for the SFTP binding.
    if (rtype_len == 9 && memcmp(rtype, "subsystem", 9) == 0)
    {
        const uint8_t *arg = nullptr;
        uint32_t arg_len = 0;
        if (rd_string(payload, len, off, &arg, &arg_len) && arg_len == 4 && memcmp(arg, "sftp", 4) == 0)
        {
            *accept = true;
            c->type = SshChanType::SSH_CHAN_SFTP;
            if (s_chcb.pc_sftp_open_cb)
            {
                s_chcb.pc_sftp_open_cb(i, c->local_id);
            }
        }
    }
#endif
#if PC_ENABLE_SSH_SCP
    // exec "scp …": already accepted (exec is in the base set); tag the channel + hand the command to the binding.
    if (rtype_len == 4 && memcmp(rtype, "exec", 4) == 0)
    {
        const uint8_t *arg = nullptr;
        uint32_t arg_len = 0;
        if (rd_string(payload, len, off, &arg, &arg_len) && arg_len >= 4 && memcmp(arg, "scp ", 4) == 0)
        {
            c->type = SshChanType::SSH_CHAN_SCP;
            if (s_chcb.pc_scp_open_cb)
            {
                s_chcb.pc_scp_open_cb(i, c->local_id, (const char *)arg, arg_len);
            }
        }
    }
#endif
}
} // namespace
#endif

int pc_ssh_channel_handle_request(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                                  size_t cap)
{
    *out_len = 0;
    if (i >= MAX_SSH_CONNS || len < 1 || payload[0] != SSH_MSG_CHANNEL_REQUEST)
    {
        return -1;
    }

    size_t off = 1;
    if (off + 4 > len)
    {
        return -1;
    }
    uint32_t recipient = rd_u32(payload + off); // our channel id
    off += 4;
    const uint8_t *rtype;
    uint32_t rtype_len;
    if (!rd_string(payload, len, &off, &rtype, &rtype_len))
    {
        return -1;
    }
    if (off >= len)
    {
        return -1;
    }
    bool want_reply = payload[off++] != 0;

    SshChannel *c = chan_by_id(i, recipient);
    if (!c)
    {
        return -1;
    }

    bool accept =
        (rtype_len == 5 && memcmp(rtype, "shell", 5) == 0) || (rtype_len == 4 && memcmp(rtype, "exec", 4) == 0) ||
        (rtype_len == 7 && memcmp(rtype, "pty-req", 7) == 0) || (rtype_len == 3 && memcmp(rtype, "env", 3) == 0);

#if PC_ENABLE_SSH_SFTP || PC_ENABLE_SSH_SCP
    classify_file_transfer_request(i, c, rtype, rtype_len, payload, len, &off, &accept);
#endif

    if (!want_reply)
    {
        return 0;
    }

    if (cap < 5)
    {
        return -1;
    }
    out[0] = accept ? SSH_MSG_CHANNEL_SUCCESS : SSH_MSG_CHANNEL_FAILURE;
    wr_u32(out + 1, c->peer_id);
    *out_len = 5;
    return 0;
}

// ---------------------------------------------------------------------------
// CHANNEL_DATA (inbound) + flow control
// ---------------------------------------------------------------------------

int pc_ssh_channel_handle_data(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap)
{
    *out_len = 0;
    if (i >= MAX_SSH_CONNS || len < 1 || payload[0] != SSH_MSG_CHANNEL_DATA)
    {
        return -1;
    }

    size_t off = 1;
    if (off + 4 > len)
    {
        return -1;
    }
    uint32_t recipient = rd_u32(payload + off);
    off += 4;
    const uint8_t *data;
    uint32_t dlen;
    if (!rd_string(payload, len, &off, &data, &dlen))
    {
        return -1;
    }

    SshChannel *c = chan_by_id(i, recipient);
    if (!c)
    {
        return -1;
    }
    if (dlen > c->local_window)
    {
        return -1; // peer overran the advertised window (RFC 4254 §5.2)
    }
    c->local_window -= dlen;

    if (dlen > 0)
    {
        switch (c->type)
        {
        case SshChanType::SSH_CHAN_DIRECT_TCPIP:
        case SshChanType::SSH_CHAN_FORWARDED_TCPIP: // forwarded TCP bytes (ssh -L / -R) -> the forward owner
            if (s_chcb.forward_data_cb)
            {
                s_chcb.forward_data_cb(i, c->local_id, data, dlen);
            }
            break;
#if PC_ENABLE_SSH_SFTP
        case SshChanType::SSH_CHAN_SFTP: // SSH_FXP_* bytes -> the SFTP binding
            if (s_chcb.pc_sftp_data_cb)
            {
                s_chcb.pc_sftp_data_cb(i, c->local_id, data, dlen);
            }
            break;
#endif
#if PC_ENABLE_SSH_SCP
        case SshChanType::SSH_CHAN_SCP: // RCP protocol bytes -> the SCP binding
            if (s_chcb.pc_scp_data_cb)
            {
                s_chcb.pc_scp_data_cb(i, c->local_id, data, dlen);
            }
            break;
#endif
        default: // SSH_CHAN_SESSION: shell/exec bytes -> the application
            if (s_chcb.data_cb)
            {
                s_chcb.data_cb(i, c->local_id, data, dlen);
            }
            break;
        }
    }

    // Replenish the window once it drops below half.
    if (c->local_window < SSH_CHAN_WINDOW / 2)
    {
        uint32_t add = SSH_CHAN_WINDOW - c->local_window;
        if (cap >= 9)
        {
            out[0] = SSH_MSG_CHANNEL_WINDOW_ADJUST;
            wr_u32(out + 1, c->peer_id);
            wr_u32(out + 5, add);
            *out_len = 9;
            c->local_window += add;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// CHANNEL_DATA (outbound)
// ---------------------------------------------------------------------------

int pc_ssh_channel_build_data(uint8_t i, uint32_t channel, const uint8_t *data, size_t len, uint8_t *out,
                              size_t *out_len, size_t cap)
{
    SshChannel *c = (i < MAX_SSH_CONNS) ? chan_by_id(i, channel) : nullptr;
    if (!c)
    {
        return -1;
    }
    if (len > c->peer_window || len > c->peer_max_pkt)
    {
        return -1; // would exceed the client's window / packet size
    }
    if (cap < 1 + 4 + 4 + len)
    {
        return -1;
    }

    out[0] = SSH_MSG_CHANNEL_DATA;
    wr_u32(out + 1, c->peer_id);
    wr_u32(out + 5, (uint32_t)len);
    memcpy(out + 9, data, len);
    *out_len = 9 + len;
    c->peer_window -= (uint32_t)len;
    return 0;
}

// ---------------------------------------------------------------------------
// WINDOW_ADJUST (inbound)
// ---------------------------------------------------------------------------

int pc_ssh_channel_handle_window_adjust(uint8_t i, const uint8_t *payload, size_t len)
{
    if (i >= MAX_SSH_CONNS || len < 9 || payload[0] != SSH_MSG_CHANNEL_WINDOW_ADJUST)
    {
        return -1;
    }
    SshChannel *c = chan_by_id(i, rd_u32(payload + 1));
    if (!c)
    {
        return -1;
    }
    uint32_t add = rd_u32(payload + 5);
    // Saturate rather than overflow the 32-bit window.
    uint32_t w = c->peer_window;
    c->peer_window = (w + add < w) ? 0xFFFFFFFFu : (w + add);
    return 0;
}

// ---------------------------------------------------------------------------
// EOF + CLOSE
// ---------------------------------------------------------------------------

// Frame EOF + CLOSE for an open channel and mark it closed (shared by the inbound
// handler and the app/teardown path).
static int build_close_chan(SshChannel *c, uint8_t *out, size_t *out_len, size_t cap)
{
    if (!c || cap < 10)
    {
        return -1;
    }
    uint32_t peer = c->peer_id;
    out[0] = SSH_MSG_CHANNEL_EOF;
    wr_u32(out + 1, peer);
    out[5] = SSH_MSG_CHANNEL_CLOSE;
    wr_u32(out + 6, peer);
    *out_len = 10;
    c->open = false;
    return 0;
}

int pc_ssh_channel_build_close(uint8_t i, uint32_t channel, uint8_t *out, size_t *out_len, size_t cap)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    return build_close_chan(chan_by_id(i, channel), out, out_len, cap);
}

int pc_ssh_channel_handle_close(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                                size_t cap)
{
    *out_len = 0;
    if (i >= MAX_SSH_CONNS || len < 5 || payload[0] != SSH_MSG_CHANNEL_CLOSE)
    {
        return -1;
    }
    return build_close_chan(chan_by_id(i, rd_u32(payload + 1)), out, out_len, cap);
}
