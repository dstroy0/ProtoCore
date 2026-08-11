// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_channel.c
 * @brief SSH connection protocol - multiplexed session channels (RFC 4254).
 *
 * The channel table is owned here; inbound messages are routed to a channel by the
 * recipient channel id they carry, and the local channel id is the channel's slot
 * index in its connection's pool (unique per connection, which is all RFC 4254
 * requires). Other layers go through these functions, never the table.
 */

#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "mmgr/bytes.h"  // pc_rd_u32 / pc_rd_str - the one length-prefixed reader
#include "mmgr/endian.h" // pc_wr32be - the one wire-integer writer
#include "mmgr/protomem.h"

SshChannel ssh_chan[MAX_SSH_CONNS][PC_SSH_MAX_CHANNELS];

// All SSH channel-layer callbacks, owned by one instance (internal linkage): the channel-data
// sink and the local/remote port-forward hooks. Grouped so it is one named owner, unreachable
// from any other translation unit. (The ssh_chan[][] table is the shared cross-TU substrate.)
typedef struct
{
    SshChannelDataCb data_cb;
    SshForwardOpenCb forward_open_cb;
    SshForwardDataCb forward_data_cb;
    SshRemoteForwardOpenCb rfwd_open_cb;
    SshRemoteForwardCancelCb rfwd_cancel_cb;
    SshForwardConfirmCb forward_confirm_cb;
#if PC_ENABLE_SSH_SFTP
    SshSftpOpenCb pc_sftp_open_cb;
    SshSftpDataCb pc_sftp_data_cb;
#endif
#if PC_ENABLE_SSH_SCP
    SshScpOpenCb pc_scp_open_cb;
    SshScpDataCb pc_scp_data_cb;
#endif
} SshChannelCtx;
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
    mem.set(ssh_chan[i], 0, sizeof(ssh_chan[i])); // reset every channel for this connection
}

// ---------------------------------------------------------------------------
// Channel table (owned here)
// ---------------------------------------------------------------------------

SshChannel *pc_ssh_chan_by_id(uint8_t i, uint32_t id)
{
    if (i >= MAX_SSH_CONNS || id >= PC_SSH_MAX_CHANNELS || !ssh_chan[i][id].open)
    {
        return NULL;
    }
    return &ssh_chan[i][id];
}

// A pending server-initiated channel @p id on connection @p i (awaiting the client's
// CONFIRMATION / FAILURE), or nullptr.
static SshChannel *chan_pending_by_id(uint8_t i, uint32_t id)
{
    if (i >= MAX_SSH_CONNS || id >= PC_SSH_MAX_CHANNELS || !ssh_chan[i][id].pending)
    {
        return NULL;
    }
    return &ssh_chan[i][id];
}

int pc_ssh_chan_alloc(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
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

// Both message bodies are built by the signaling owner (ssh_flow_control); the mux only supplies the
// channel it resolved and the ids the wire carries.
static int build_open_failure(uint8_t *out, size_t cap, uint32_t sender, uint32_t reason, size_t *out_len)
{
    return pc_ssh_sig_build_open_failure(out, cap, sender, reason, out_len);
}

static int build_open_confirm(const SshChannel *c, uint8_t *out, size_t cap, size_t *out_len)
{
    return pc_ssh_sig_build_open_confirm(&c->flow, c->peer_id, c->local_id, out, cap, out_len);
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
    if (!pc_rd_str(payload, len, &off, &name, &name_len))
    {
        return -1;
    }
    if (off >= len)
    {
        return -1;
    }
    proto_bool want_reply = payload[off++] != 0;

    proto_bool is_fwd = (name_len == 13 && mem.cmp(name, "tcpip-forward", 13) == 0);
    proto_bool is_cancel = (name_len == 20 && mem.cmp(name, "cancel-tcpip-forward", 20) == 0);

    if (is_fwd || is_cancel)
    {
        // Request-specific data: bind address (string) followed by bind port (uint32).
        const uint8_t *addr;
        uint32_t addr_len;
        if (!pc_rd_str(payload, len, &off, &addr, &addr_len) || off + 4 > len)
        {
            return -1;
        }
        uint16_t bind_port = (uint16_t)pc_rd32be(payload + off);

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
                pc_wr32be(out + 1, (uint32_t)(uint16_t)bound);
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
    int slot = pc_ssh_chan_alloc(i);
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
    pc_wr32be(out + off, (uint32_t)tl);
    mem.cpy(out + off + 4, type, tl);
    off += 4 + tl;
    pc_wr32be(out + off, (uint32_t)slot); // our sender channel id
    pc_wr32be(out + off + 4, SSH_CHAN_WINDOW);
    pc_wr32be(out + off + 8, SSH_CHAN_MAX_PACKET);
    off += 12;
    pc_wr32be(out + off, (uint32_t)ca);
    mem.cpy(out + off + 4, conn_addr, ca);
    off += 4 + ca;
    pc_wr32be(out + off, conn_port);
    off += 4;
    pc_wr32be(out + off, (uint32_t)oa);
    mem.cpy(out + off + 4, orig_addr, oa);
    off += 4 + oa;
    pc_wr32be(out + off, orig_port);
    off += 4;

    SshChannel *c = &ssh_chan[i][slot];
    c->open = PROTO_FALSE;
    c->pending = PROTO_TRUE; // awaiting the client's CHANNEL_OPEN_CONFIRMATION
    c->type = SSH_CHAN_FORWARDED_TCPIP;
    c->local_id = (uint32_t)slot;
    c->peer_id = 0;
    pc_ssh_flow_init(&c->flow, SSH_CHAN_WINDOW, 0, 0);

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
    SshChannel *c = chan_pending_by_id(i, pc_rd32be(payload + 1));
    if (!c)
    {
        return -1;
    }
    c->peer_id = pc_rd32be(payload + 5);
    pc_ssh_flow_peer_add(&c->flow, pc_rd32be(payload + 9));
    c->flow.peer_max_pkt = pc_rd32be(payload + 13);
    c->pending = PROTO_FALSE;
    c->open = PROTO_TRUE;
    if (s_chcb.forward_confirm_cb)
    {
        s_chcb.forward_confirm_cb(i, c->local_id, PROTO_TRUE);
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
    SshChannel *c = chan_pending_by_id(i, pc_rd32be(payload + 1));
    if (!c)
    {
        return -1;
    }
    uint32_t ch = c->local_id;
    c->pending = PROTO_FALSE;
    c->open = PROTO_FALSE; // free the slot; the client refused the forward
    if (s_chcb.forward_confirm_cb)
    {
        s_chcb.forward_confirm_cb(i, ch, PROTO_FALSE);
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
    if (!pc_rd_str(payload, len, &off, &type, &type_len))
    {
        return -1;
    }
    if (off + 12 > len)
    {
        return -1;
    }
    uint32_t sender = pc_rd32be(payload + off);
    uint32_t init_window = pc_rd32be(payload + off + 4);
    uint32_t max_pkt = pc_rd32be(payload + off + 8);
    off += 12;

    proto_bool is_session = (type_len == 7 && mem.cmp(type, "session", 7) == 0);
    proto_bool is_dtcpip = (type_len == 12 && mem.cmp(type, "direct-tcpip", 12) == 0);
    if (!is_session && !is_dtcpip)
    {
        return build_open_failure(out, cap, sender, 3u, out_len); // unknown channel type
    }

    // direct-tcpip data: host(string) port(u32) orig_host(string) orig_port(u32).
    const uint8_t *fhost = NULL;
    uint32_t fhost_len = 0;
    uint16_t fport = 0;
    if (is_dtcpip)
    {
        if (!s_chcb.forward_open_cb)
        {
            return build_open_failure(out, cap, sender, 1u, out_len); // forwarding off: prohibited
        }
        if (!pc_rd_str(payload, len, &off, &fhost, &fhost_len) || off + 4 > len)
        {
            return -1;
        }
        fport = (uint16_t)pc_rd32be(payload + off); // orig host/port follow but are advisory
    }

    int slot = pc_ssh_chan_alloc(i);
    if (slot < 0)
    {
        return build_open_failure(out, cap, sender, 4u, out_len); // pool full
    }

    SshChannel *c = &ssh_chan[i][slot];
    c->open = PROTO_TRUE;
    c->type = is_dtcpip ? SSH_CHAN_DIRECT_TCPIP : SSH_CHAN_SESSION;
    c->local_id = (uint32_t)slot;
    c->peer_id = sender;
    pc_ssh_flow_init(&c->flow, SSH_CHAN_WINDOW, init_window, max_pkt);

    if (is_dtcpip)
    {
        // The owner does the actual TCP connect (no I/O in this codec); on refusal
        // free the channel and fail closed.
        if (s_chcb.forward_open_cb(i, c->local_id, (const char *)fhost, fhost_len, fport) < 0)
        {
            c->open = PROTO_FALSE;
            return build_open_failure(out, cap, sender, 2u, out_len); // connect failed
        }
    }
    return build_open_confirm(c, out, cap, out_len);
}

// ---------------------------------------------------------------------------
// CHANNEL_REQUEST → SUCCESS / FAILURE
// ---------------------------------------------------------------------------

#if PC_ENABLE_SSH_SFTP || PC_ENABLE_SSH_SCP
// A subsystem/exec CHANNEL_REQUEST may name a file-transfer service (SFTP or SCP). Tag @p c and fire the
// matching open callback when it does; @p off points at the request-specific arg and may be advanced. Flips
// *accept true for an accepted SFTP subsystem (exec is already in the base accept set).
static void classify_file_transfer_request(uint8_t i, SshChannel *c, const uint8_t *rtype, uint32_t rtype_len,
                                           const uint8_t *payload, size_t len, size_t *off, proto_bool *accept)
{
#if !PC_ENABLE_SSH_SFTP
    (void)accept; // only the SFTP subsystem path flips acceptance; scp exec is already accepted
#endif
#if PC_ENABLE_SSH_SFTP
    // subsystem "sftp": not in the base accept set, so accept it here and tag the channel for the SFTP binding.
    if (rtype_len == 9 && mem.cmp(rtype, "subsystem", 9) == 0)
    {
        const uint8_t *arg = NULL;
        uint32_t arg_len = 0;
        if (pc_rd_str(payload, len, off, &arg, &arg_len) && arg_len == 4 && mem.cmp(arg, "sftp", 4) == 0)
        {
            *accept = PROTO_TRUE;
            c->type = SSH_CHAN_SFTP;
            if (s_chcb.pc_sftp_open_cb)
            {
                s_chcb.pc_sftp_open_cb(i, c->local_id);
            }
        }
    }
#endif
#if PC_ENABLE_SSH_SCP
    // exec "scp …": already accepted (exec is in the base set); tag the channel + hand the command to the binding.
    if (rtype_len == 4 && mem.cmp(rtype, "exec", 4) == 0)
    {
        const uint8_t *arg = NULL;
        uint32_t arg_len = 0;
        if (pc_rd_str(payload, len, off, &arg, &arg_len) && arg_len >= 4 && mem.cmp(arg, "scp ", 4) == 0)
        {
            c->type = SSH_CHAN_SCP;
            if (s_chcb.pc_scp_open_cb)
            {
                s_chcb.pc_scp_open_cb(i, c->local_id, (const char *)arg, arg_len);
            }
        }
    }
#endif
}
#endif

// Read @p n consecutive strings from @p off: true when every one of them is present and whole.
static proto_bool req_strings_present(const uint8_t *p, size_t len, size_t off, uint8_t n)
{
    const uint8_t *s = NULL;
    uint32_t slen = 0;
    for (uint8_t k = 0; k < n; k++)
    {
        if (!pc_rd_str(p, len, &off, &s, &slen))
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

// RFC 4254 sec 6.2: string TERM, four uint32 dimensions, string encoded terminal modes.
static proto_bool pty_req_fields_present(const uint8_t *p, size_t len, size_t off)
{
    const uint8_t *s = NULL;
    uint32_t slen = 0;
    if (!pc_rd_str(p, len, &off, &s, &slen))
    {
        return PROTO_FALSE;
    }
    if (off + 16 > len)
    {
        return PROTO_FALSE;
    }
    off += 16;
    return pc_rd_str(p, len, &off, &s, &slen);
}

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
    uint32_t recipient = pc_rd32be(payload + off); // our channel id
    off += 4;
    const uint8_t *rtype;
    uint32_t rtype_len;
    if (!pc_rd_str(payload, len, &off, &rtype, &rtype_len))
    {
        return -1;
    }
    if (off >= len)
    {
        return -1;
    }
    proto_bool want_reply = payload[off++] != 0;

    SshChannel *c = pc_ssh_chan_by_id(i, recipient);
    if (!c)
    {
        return -1;
    }

    // RFC 4254: each request type carries its own mandatory fields after want_reply, and a request
    // truncated before them is not the request it names. The offset is read from a copy so the
    // file-transfer classifier below still sees the request-specific argument where it expects it.
    size_t fields = off;
    proto_bool accept = PROTO_FALSE;
    if (rtype_len == 5 && mem.cmp(rtype, "shell", 5) == 0)
    {
        accept = PROTO_TRUE; // sec 6.5: no request-specific data follows
    }
    else if (rtype_len == 4 && mem.cmp(rtype, "exec", 4) == 0)
    {
        accept = req_strings_present(payload, len, fields, 1); // sec 6.5: string command
    }
    else if (rtype_len == 3 && mem.cmp(rtype, "env", 3) == 0)
    {
        accept = req_strings_present(payload, len, fields, 2); // sec 6.4: string name, string value
    }
    else if (rtype_len == 7 && mem.cmp(rtype, "pty-req", 7) == 0)
    {
        accept = pty_req_fields_present(payload, len, fields); // sec 6.2
    }

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
    pc_wr32be(out + 1, c->peer_id);
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
    uint32_t recipient = pc_rd32be(payload + off);
    off += 4;
    const uint8_t *data;
    uint32_t dlen;
    if (!pc_rd_str(payload, len, &off, &data, &dlen))
    {
        return -1;
    }

    SshChannel *c = pc_ssh_chan_by_id(i, recipient);
    if (!c)
    {
        return -1;
    }
    if (!pc_ssh_flow_recv_take(&c->flow, dlen))
    {
        return -1; // peer overran the advertised window (RFC 4254 §5.2)
    }

    if (dlen > 0)
    {
        switch (c->type)
        {
        case SSH_CHAN_DIRECT_TCPIP:
        case SSH_CHAN_FORWARDED_TCPIP: // forwarded TCP bytes (ssh -L / -R) -> the forward owner
            if (s_chcb.forward_data_cb)
            {
                s_chcb.forward_data_cb(i, c->local_id, data, dlen);
            }
            break;
#if PC_ENABLE_SSH_SFTP
        case SSH_CHAN_SFTP: // SSH_FXP_* bytes -> the SFTP binding
            if (s_chcb.pc_sftp_data_cb)
            {
                s_chcb.pc_sftp_data_cb(i, c->local_id, data, dlen);
            }
            break;
#endif
#if PC_ENABLE_SSH_SCP
        case SSH_CHAN_SCP: // RCP protocol bytes -> the SCP binding
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
    uint32_t add = 0;
    if (cap >= 9 && pc_ssh_flow_replenish_due(&c->flow, &add))
    {
        out[0] = SSH_MSG_CHANNEL_WINDOW_ADJUST;
        pc_wr32be(out + 1, c->peer_id);
        pc_wr32be(out + 5, add);
        *out_len = 9;
        pc_ssh_flow_local_credit(&c->flow, add); // the caller emits *out_len unconditionally
    }
    return 0;
}

int pc_ssh_channel_handle_extended_data(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                                        size_t cap)
{
    *out_len = 0;
    if (i >= MAX_SSH_CONNS || len < 1 || payload[0] != SSH_MSG_CHANNEL_EXTENDED_DATA)
    {
        return -1;
    }

    size_t off = 1;
    if (off + 8 > len)
    {
        return -1;
    }
    uint32_t recipient = pc_rd32be(payload + off);
    off += 8; // recipient channel, then the data_type_code this end does not separate
    const uint8_t *data;
    uint32_t dlen;
    if (!pc_rd_str(payload, len, &off, &data, &dlen))
    {
        return -1;
    }

    SshChannel *c = pc_ssh_chan_by_id(i, recipient);
    if (!c)
    {
        return -1;
    }
    if (!pc_ssh_flow_recv_take(&c->flow, dlen))
    {
        return -1; // peer overran the advertised window (RFC 4254 §5.2)
    }

    // Replenish the window once it drops below half.
    uint32_t add = 0;
    if (cap >= 9 && pc_ssh_flow_replenish_due(&c->flow, &add))
    {
        out[0] = SSH_MSG_CHANNEL_WINDOW_ADJUST;
        pc_wr32be(out + 1, c->peer_id);
        pc_wr32be(out + 5, add);
        *out_len = 9;
        pc_ssh_flow_local_credit(&c->flow, add); // the caller emits *out_len unconditionally
    }
    return 0;
}

// ---------------------------------------------------------------------------
// CHANNEL_DATA (outbound)
// ---------------------------------------------------------------------------

int pc_ssh_channel_build_data(uint8_t i, uint32_t channel, const uint8_t *data, size_t len, uint8_t *out,
                              size_t *out_len, size_t cap)
{
    SshChannel *c = (i < MAX_SSH_CONNS) ? pc_ssh_chan_by_id(i, channel) : NULL;
    if (!c)
    {
        return -1;
    }
    return pc_ssh_sig_build_data(&c->flow, c->peer_id, data, len, out, cap, out_len);
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
    SshChannel *c = pc_ssh_chan_by_id(i, pc_rd32be(payload + 1));
    if (!c)
    {
        return -1;
    }
    pc_ssh_flow_peer_add(&c->flow, pc_rd32be(payload + 5));
    return 0;
}

// ---------------------------------------------------------------------------
// EOF + CLOSE
// ---------------------------------------------------------------------------

// Frame EOF + CLOSE for an open channel and mark it closed (shared by the inbound
// handler and the app/teardown path).
static int build_close_chan(SshChannel *c, uint8_t *out, size_t *out_len, size_t cap)
{
    if (!c || pc_ssh_sig_build_close(c->peer_id, out, cap, out_len) < 0)
    {
        return -1;
    }
    c->open = PROTO_FALSE; // freeing the slot is the mux's half; the message body is the signaling owner's
    return 0;
}

int pc_ssh_channel_build_close(uint8_t i, uint32_t channel, uint8_t *out, size_t *out_len, size_t cap)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    return build_close_chan(pc_ssh_chan_by_id(i, channel), out, out_len, cap);
}

int pc_ssh_channel_handle_close(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                                size_t cap)
{
    *out_len = 0;
    if (i >= MAX_SSH_CONNS || len < 5 || payload[0] != SSH_MSG_CHANNEL_CLOSE)
    {
        return -1;
    }
    return build_close_chan(pc_ssh_chan_by_id(i, pc_rd32be(payload + 1)), out, out_len, cap);
}
