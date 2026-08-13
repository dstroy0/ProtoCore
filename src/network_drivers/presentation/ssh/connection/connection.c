// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file connection.c
 * @brief RFC 4254: the channel multiplexer, its window arithmetic, and the forwarding owners.
 */

#include "network_drivers/presentation/ssh/connection/connection.h"
#include "network_drivers/presentation/ssh/network/network.h" // SshNetwork: the socket seam
#include "network_drivers/presentation/ssh/ssh.h"
#include "network_drivers/presentation/ssh/transport/transport.h" // ssh_sess, ssh_pkt - session and packet state
#include "mmgr/bytes.h"      // protocore_rd_u32 / protocore_rd_str - the one length-prefixed reader
#include "mmgr/endian.h"     // protocore_wr32be - the one wire-integer writer
#include "mmgr/plaintext.h"  // protocore_plaintext_span / _mark / _release - the dispatch reply buffer
#include "mmgr/protoframe.h" // protocore_fval - the log field value
#include "mmgr/protomem.h"
#include "mmgr/protostr.h"        // str.len - the bounded string length
#include "shared_primitives/log.h" // PROTOCORE_LOGD


// ---------------------------------------------------------------------------
// RFC 4254 sec 5.2 - window arithmetic and channel signalling
// ---------------------------------------------------------------------------

void protocore_ssh_flow_init(SshFlow *f, uint32_t local_window, uint32_t peer_window, uint32_t peer_max_pkt)
{
    f->local_window = local_window;
    f->local_max = local_window;
    f->peer_window = peer_window;
    f->peer_max_pkt = peer_max_pkt;
}

proto_bool protocore_ssh_flow_recv_take(SshFlow *f, uint32_t n)
{
    if (n > f->local_window)
    {
        return PROTO_FALSE; // peer overran the advertised window (RFC 4254 sec 5.2)
    }
    f->local_window -= n;
    return PROTO_TRUE;
}

proto_bool protocore_ssh_flow_replenish_due(const SshFlow *f, uint32_t *add)
{
    if (f->local_window >= f->local_max / 2)
    {
        return PROTO_FALSE;
    }
    *add = f->local_max - f->local_window;
    return PROTO_TRUE;
}

void protocore_ssh_flow_local_credit(SshFlow *f, uint32_t add)
{
    f->local_window += add;
}

proto_bool protocore_ssh_flow_send_allows(const SshFlow *f, size_t len)
{
    return len <= f->peer_window && len <= f->peer_max_pkt;
}

uint32_t protocore_ssh_flow_send_cap(const SshFlow *f, uint32_t want)
{
    uint32_t cap = want;
    if (cap > f->peer_window)
    {
        cap = f->peer_window;
    }
    if (cap > f->peer_max_pkt)
    {
        cap = f->peer_max_pkt;
    }
    return cap;
}

void protocore_ssh_flow_send_take(SshFlow *f, uint32_t n)
{
    f->peer_window -= n;
}

void protocore_ssh_flow_peer_add(SshFlow *f, uint32_t add)
{
    uint32_t w = f->peer_window;
    f->peer_window = (w + add < w) ? 0xFFFFFFFFu : (w + add);
}

// ---------------------------------------------------------------------------
// Channel signaling (RFC 4254 sec 5)
// ---------------------------------------------------------------------------

int32_t protocore_ssh_sig_build_open_failure(uint8_t *out, size_t cap, uint32_t peer_id, uint32_t reason, size_t *out_len)
{
    if (cap < 17)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_OPEN_FAILURE;
    protocore_wr32be(out + 1, peer_id);
    protocore_wr32be(out + 5, reason);
    protocore_wr32be(out + 9, 0);  // empty description
    protocore_wr32be(out + 13, 0); // empty language
    *out_len = 17;
    return 0;
}

int32_t protocore_ssh_sig_build_open_confirm(const SshFlow *f, uint32_t peer_id, uint32_t local_id, uint8_t *out, size_t cap,
                                      size_t *out_len)
{
    if (cap < 17)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_OPEN_CONFIRMATION;
    protocore_wr32be(out + 1, peer_id);
    protocore_wr32be(out + 5, local_id);
    protocore_wr32be(out + 9, f->local_window);
    protocore_wr32be(out + 13, SSH_CHAN_MAX_PACKET);
    *out_len = 17;
    return 0;
}

int32_t protocore_ssh_sig_build_data(SshFlow *f, uint32_t peer_id, const uint8_t *data, size_t len, uint8_t *out, size_t cap,
                              size_t *out_len)
{
    if (!protocore_ssh_flow_send_allows(f, len))
    {
        return -1; // would exceed the peer's window or its maximum packet size
    }
    if (cap < 9 + len)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_DATA;
    protocore_wr32be(out + 1, peer_id);
    protocore_wr32be(out + 5, (uint32_t)len);
    mem.cpy(out + 9, data, len);
    *out_len = 9 + len;
    protocore_ssh_flow_send_take(f, (uint32_t)len);
    return 0;
}

int32_t protocore_ssh_sig_build_window_adjust(uint32_t peer_id, uint32_t add, uint8_t *out, size_t cap, size_t *out_len)
{
    if (cap < 9)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_WINDOW_ADJUST;
    protocore_wr32be(out + 1, peer_id);
    protocore_wr32be(out + 5, add);
    *out_len = 9;
    return 0;
}

int32_t protocore_ssh_sig_build_eof(uint32_t peer_id, uint8_t *out, size_t cap, size_t *out_len)
{
    if (cap < 5)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_EOF;
    protocore_wr32be(out + 1, peer_id);
    *out_len = 5;
    return 0;
}

int32_t protocore_ssh_sig_build_close(uint32_t peer_id, uint8_t *out, size_t cap, size_t *out_len)
{
    if (cap < 5)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_CLOSE;
    protocore_wr32be(out + 1, peer_id);
    *out_len = 5;
    return 0;
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 5 - channel multiplexing
// ---------------------------------------------------------------------------

SshChannel ssh_chan[MAX_SSH_CONNS][PROTOCORE_SSH_MAX_CHANNELS];

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
    SshPtyReqCb pty_req_cb;
    SshWindowChangeCb window_change_cb;
#if PROTOCORE_ENABLE_SSH_SFTP
    SshSftpOpenCb protocore_sftp_open_cb;
    SshSftpDataCb protocore_sftp_data_cb;
#endif
#if PROTOCORE_ENABLE_SSH_SCP
    SshScpOpenCb protocore_scp_open_cb;
    SshScpDataCb protocore_scp_data_cb;
#endif
} SshChannelCtx;
static SshChannelCtx s_chcb;

void protocore_ssh_channel_set_data_cb(SshChannelDataCb cb)
{
    s_chcb.data_cb = cb;
}

void protocore_ssh_channel_set_pty_req_cb(SshPtyReqCb cb)
{
    s_chcb.pty_req_cb = cb;
}

void protocore_ssh_channel_set_window_change_cb(SshWindowChangeCb cb)
{
    s_chcb.window_change_cb = cb;
}

proto_bool protocore_ssh_channel_pty(uint8_t i, uint32_t channel, uint32_t *width_chars, uint32_t *height_rows,
                                     uint32_t *width_px, uint32_t *height_px)
{
    const SshChannel *c = protocore_ssh_chan_by_id(i, channel);
    if (c == NULL || !c->pty)
    {
        return PROTO_FALSE;
    }
    if (width_chars != NULL)
    {
        *width_chars = c->width_chars;
    }
    if (height_rows != NULL)
    {
        *height_rows = c->height_rows;
    }
    if (width_px != NULL)
    {
        *width_px = c->width_px;
    }
    if (height_px != NULL)
    {
        *height_px = c->height_px;
    }
    return PROTO_TRUE;
}

#if PROTOCORE_ENABLE_SSH_SFTP
void protocore_ssh_channel_set_sftp_open_cb(SshSftpOpenCb cb)
{
    s_chcb.protocore_sftp_open_cb = cb;
}
SshSftpOpenCb protocore_ssh_channel_sftp_open_cb(void)
{
    return s_chcb.protocore_sftp_open_cb;
}
void protocore_ssh_channel_set_sftp_data_cb(SshSftpDataCb cb)
{
    s_chcb.protocore_sftp_data_cb = cb;
}
#endif

#if PROTOCORE_ENABLE_SSH_SCP
void protocore_ssh_channel_set_scp_open_cb(SshScpOpenCb cb)
{
    s_chcb.protocore_scp_open_cb = cb;
}
SshScpOpenCb protocore_ssh_channel_scp_open_cb(void)
{
    return s_chcb.protocore_scp_open_cb;
}
void protocore_ssh_channel_set_scp_data_cb(SshScpDataCb cb)
{
    s_chcb.protocore_scp_data_cb = cb;
}
#endif

void protocore_ssh_channel_set_forward_open_cb(SshForwardOpenCb cb)
{
    s_chcb.forward_open_cb = cb;
}

void protocore_ssh_channel_set_forward_data_cb(SshForwardDataCb cb)
{
    s_chcb.forward_data_cb = cb;
}

void protocore_ssh_channel_set_rforward_open_cb(SshRemoteForwardOpenCb cb)
{
    s_chcb.rfwd_open_cb = cb;
}

void protocore_ssh_channel_set_rforward_cancel_cb(SshRemoteForwardCancelCb cb)
{
    s_chcb.rfwd_cancel_cb = cb;
}

void protocore_ssh_channel_set_forward_confirm_cb(SshForwardConfirmCb cb)
{
    s_chcb.forward_confirm_cb = cb;
}

void protocore_ssh_channel_init(uint8_t i)
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

int protocore_ssh_channel_bind_service(uint8_t i, uint32_t channel, SshChanService service)
{
    SshChannel *c = protocore_ssh_chan_by_id(i, channel);
    if (c == NULL)
    {
        return -1;
    }
    c->service = service;
    return 0;
}

SshChannel *protocore_ssh_chan_by_id(uint8_t i, uint32_t id)
{
    if (i >= MAX_SSH_CONNS || id >= PROTOCORE_SSH_MAX_CHANNELS || !ssh_chan[i][id].open)
    {
        return NULL;
    }
    return &ssh_chan[i][id];
}

// A pending server-initiated channel @p id on connection @p i (awaiting the client's
// CONFIRMATION / FAILURE), or nullptr.
static SshChannel *chan_pending_by_id(uint8_t i, uint32_t id)
{
    if (i >= MAX_SSH_CONNS || id >= PROTOCORE_SSH_MAX_CHANNELS || !ssh_chan[i][id].pending)
    {
        return NULL;
    }
    return &ssh_chan[i][id];
}

// Take slot @p slot for a new channel, clearing whatever the previous one left behind.
//
// RFC 4254 sec 5.3: "The channel is considered closed for a party when it has both sent and
// received SSH_MSG_CHANNEL_CLOSE, and the party may then reuse the channel number." Reuse is
// permitted, and chan_alloc only tests open/pending, so a recycled slot still carries the previous
// channel's EOF latches - the same pair the close path set on its way out. Every open goes through
// here so a new channel starts with none of it.
static SshChannel *chan_take(uint8_t i, int slot)
{
    SshChannel *c = &ssh_chan[i][slot];
    mem.set(c, 0, sizeof(*c));
    c->local_id = (uint32_t)slot;
    return c;
}

int protocore_ssh_chan_alloc(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    for (int c = 0; c < PROTOCORE_SSH_MAX_CHANNELS; c++)
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
    return protocore_ssh_sig_build_open_failure(out, cap, sender, reason, out_len);
}

static int build_open_confirm(const SshChannel *c, uint8_t *out, size_t cap, size_t *out_len)
{
    return protocore_ssh_sig_build_open_confirm(&c->flow, c->peer_id, c->local_id, out, cap, out_len);
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
    if (!protocore_rd_str(payload, len, &off, &name, &name_len))
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
        if (!protocore_rd_str(payload, len, &off, &addr, &addr_len) || off + 4 > len)
        {
            return -1;
        }
        uint16_t bind_port = (uint16_t)protocore_rd32be(payload + off);

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
                protocore_wr32be(out + 1, (uint32_t)(uint16_t)bound);
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

int protocore_ssh_channel_open_forwarded(uint8_t i, const char *conn_addr, uint16_t conn_port, const char *orig_addr,
                                  uint16_t orig_port, uint8_t *out, size_t *out_len, size_t cap)
{
    *out_len = 0;
    if (i >= MAX_SSH_CONNS || !conn_addr || !orig_addr)
    {
        return -1;
    }
    int slot = protocore_ssh_chan_alloc(i);
    if (slot < 0)
    {
        return -1; // channel pool full
    }

    const char *type = "forwarded-tcpip";
    size_t tl = 15, ca = str.len(conn_addr, cap), oa = str.len(orig_addr, cap);
    // byte || string(type) || u32 sender || u32 window || u32 maxpkt || string(conn_addr)
    //   || u32 conn_port || string(orig_addr) || u32 orig_port  (RFC 4254 §7.2)
    size_t need = 1 + (4 + tl) + 12 + (4 + ca) + 4 + (4 + oa) + 4;
    if (cap < need)
    {
        return -1;
    }

    size_t off = 0;
    out[off++] = SSH_MSG_CHANNEL_OPEN;
    protocore_wr32be(out + off, (uint32_t)tl);
    mem.cpy(out + off + 4, type, tl);
    off += 4 + tl;
    protocore_wr32be(out + off, (uint32_t)slot); // our sender channel id
    protocore_wr32be(out + off + 4, SSH_CHAN_WINDOW);
    protocore_wr32be(out + off + 8, SSH_CHAN_MAX_PACKET);
    off += 12;
    protocore_wr32be(out + off, (uint32_t)ca);
    mem.cpy(out + off + 4, conn_addr, ca);
    off += 4 + ca;
    protocore_wr32be(out + off, conn_port);
    off += 4;
    protocore_wr32be(out + off, (uint32_t)oa);
    mem.cpy(out + off + 4, orig_addr, oa);
    off += 4 + oa;
    protocore_wr32be(out + off, orig_port);
    off += 4;

    SshChannel *c = chan_take(i, slot);
    c->pending = PROTO_TRUE; // awaiting the client's CHANNEL_OPEN_CONFIRMATION
    c->type = SSH_CHAN_FORWARDED_TCPIP;
    protocore_ssh_flow_init(&c->flow, SSH_CHAN_WINDOW, 0, 0);

    *out_len = off;
    return slot;
}

int protocore_ssh_channel_handle_open_confirm(uint8_t i, const uint8_t *payload, size_t len)
{
    // byte || recipient(our local id) || sender(peer id) || window || max packet.
    if (i >= MAX_SSH_CONNS || len < 17 || payload[0] != SSH_MSG_CHANNEL_OPEN_CONFIRMATION)
    {
        return -1;
    }
    SshChannel *c = chan_pending_by_id(i, protocore_rd32be(payload + 1));
    if (!c)
    {
        return -1;
    }
    c->peer_id = protocore_rd32be(payload + 5);
    protocore_ssh_flow_peer_add(&c->flow, protocore_rd32be(payload + 9));
    c->flow.peer_max_pkt = protocore_rd32be(payload + 13);
    c->pending = PROTO_FALSE;
    c->open = PROTO_TRUE;
    if (s_chcb.forward_confirm_cb)
    {
        s_chcb.forward_confirm_cb(i, c->local_id, PROTO_TRUE);
    }
    return 0;
}

int protocore_ssh_channel_handle_open_failure(uint8_t i, const uint8_t *payload, size_t len)
{
    // byte || recipient(our local id) || reason || desc || lang.
    if (i >= MAX_SSH_CONNS || len < 5 || payload[0] != SSH_MSG_CHANNEL_OPEN_FAILURE)
    {
        return -1;
    }
    SshChannel *c = chan_pending_by_id(i, protocore_rd32be(payload + 1));
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

int protocore_ssh_channel_handle_open(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap)
{
    if (i >= MAX_SSH_CONNS || len < 1 || payload[0] != SSH_MSG_CHANNEL_OPEN)
    {
        return -1;
    }

    size_t off = 1;
    const uint8_t *type;
    uint32_t type_len;
    if (!protocore_rd_str(payload, len, &off, &type, &type_len))
    {
        return -1;
    }
    if (off + 12 > len)
    {
        return -1;
    }
    uint32_t sender = protocore_rd32be(payload + off);
    uint32_t init_window = protocore_rd32be(payload + off + 4);
    uint32_t max_pkt = protocore_rd32be(payload + off + 8);
    off += 12;

    proto_bool is_session = (type_len == 7 && mem.cmp(type, "session", 7) == 0);
    proto_bool is_dtcpip = (type_len == 12 && mem.cmp(type, "direct-tcpip", 12) == 0);
    // RFC 4254 sec 7.2: "When a connection comes to a port for which remote forwarding has been
    // requested, a channel is opened to forward the port to the other side." It travels toward the
    // end that asked for the forward, so only that end accepts it; the listening end is the one that
    // sends them and treats an inbound one as a type it does not serve.
    proto_bool is_fwd_tcpip =
        (type_len == 15 && mem.cmp(type, "forwarded-tcpip", 15) == 0) && ssh_pkt[i].is_client;
    if (!is_session && !is_dtcpip && !is_fwd_tcpip)
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
        if (!protocore_rd_str(payload, len, &off, &fhost, &fhost_len) || off + 4 > len)
        {
            return -1;
        }
        fport = (uint16_t)protocore_rd32be(payload + off); // orig host/port follow but are advisory
    }

    int slot = protocore_ssh_chan_alloc(i);
    if (slot < 0)
    {
        return build_open_failure(out, cap, sender, 4u, out_len); // pool full
    }

    SshChannel *c = chan_take(i, slot);
    c->open = PROTO_TRUE;
    c->type = is_dtcpip ? SSH_CHAN_DIRECT_TCPIP : (is_fwd_tcpip ? SSH_CHAN_FORWARDED_TCPIP : SSH_CHAN_SESSION);
    c->peer_id = sender;
    protocore_ssh_flow_init(&c->flow, SSH_CHAN_WINDOW, init_window, max_pkt);

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

// Read @p n consecutive strings from @p off: true when every one of them is present and whole.
proto_bool ssh_req_strings_present(const uint8_t *p, size_t len, size_t off, uint8_t n)
{
    const uint8_t *s = NULL;
    uint32_t slen = 0;
    for (uint8_t k = 0; k < n; k++)
    {
        if (!protocore_rd_str(p, len, &off, &s, &slen))
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

// RFC 4254 sec 6.2: string TERM, four uint32 dimensions, string encoded terminal modes.
proto_bool ssh_pty_req_fields_present(const uint8_t *p, size_t len, size_t off)
{
    SshPtyRequest pty;
    return ssh_pty_req_parse(p, len, off, &pty);
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 8 - encoding of terminal modes
// ---------------------------------------------------------------------------

// "Opcodes 1 to 159 have a single uint32 argument. Opcodes 160 to 255 are not yet defined, and
// cause parsing to stop (they should only be used after any other data). The stream is terminated
// by opcode TTY_OP_END (0x00)."
#define SSH_TTY_OP_END 0u
#define SSH_TTY_OP_ARG_LAST 159u

proto_bool ssh_pty_modes_valid(const uint8_t *modes, uint32_t len, uint32_t *consumed)
{
    uint32_t k = 0;
    while (k < len)
    {
        const uint8_t op = modes[k];
        if (op == SSH_TTY_OP_END || op > SSH_TTY_OP_ARG_LAST)
        {
            k++; // the terminator, or the first undefined opcode: nothing past it is read
            break;
        }
        if (k + 5u > len)
        {
            return PROTO_FALSE; // an opcode whose uint32 argument is not there
        }
        k += 5u;
    }
    if (consumed != NULL)
    {
        *consumed = k;
    }
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 6.2 / sec 6.7 - the pty request and its dimension updates
// ---------------------------------------------------------------------------

// The four dimensions both requests carry, in the order sec 6.2 and sec 6.7 give them.
static proto_bool read_dimensions(const uint8_t *p, size_t len, size_t *off, SshPtyRequest *out)
{
    if (*off + 16u > len)
    {
        return PROTO_FALSE;
    }
    out->width_chars = protocore_rd32be(p + *off);
    out->height_rows = protocore_rd32be(p + *off + 4);
    out->width_px = protocore_rd32be(p + *off + 8);
    out->height_px = protocore_rd32be(p + *off + 12);
    *off += 16u;
    return PROTO_TRUE;
}

proto_bool ssh_pty_req_parse(const uint8_t *p, size_t len, size_t off, SshPtyRequest *out)
{
    if (out == NULL)
    {
        return PROTO_FALSE;
    }
    mem.set(out, 0, sizeof(*out));

    const uint8_t *term = NULL;
    uint32_t term_len = 0;
    if (!protocore_rd_str(p, len, &off, &term, &term_len))
    {
        return PROTO_FALSE;
    }
    // TERM is an environment variable value; anything past what this build carries is dropped
    // rather than refused, since the value is informational.
    uint32_t n = term_len;
    if (n > sizeof(out->term) - 1u)
    {
        n = (uint32_t)(sizeof(out->term) - 1u);
    }
    mem.cpy(out->term, term, n);
    out->term[n] = '\0';

    if (!read_dimensions(p, len, &off, out))
    {
        return PROTO_FALSE;
    }
    if (!protocore_rd_str(p, len, &off, &out->modes, &out->modes_len))
    {
        return PROTO_FALSE;
    }
    return ssh_pty_modes_valid(out->modes, out->modes_len, NULL);
}

proto_bool ssh_window_change_parse(const uint8_t *p, size_t len, size_t off, SshPtyRequest *out)
{
    if (out == NULL)
    {
        return PROTO_FALSE;
    }
    mem.set(out, 0, sizeof(*out));
    return read_dimensions(p, len, &off, out);
}


int protocore_ssh_channel_handle_request(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
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
    uint32_t recipient = protocore_rd32be(payload + off); // our channel id
    off += 4;
    const uint8_t *rtype;
    uint32_t rtype_len;
    if (!protocore_rd_str(payload, len, &off, &rtype, &rtype_len))
    {
        return -1;
    }
    if (off >= len)
    {
        return -1;
    }
    proto_bool want_reply = payload[off++] != 0;

    SshChannel *c = protocore_ssh_chan_by_id(i, recipient);
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
        accept = ssh_req_strings_present(payload, len, fields, 1); // sec 6.5: string command
    }
    else if (rtype_len == 3 && mem.cmp(rtype, "env", 3) == 0)
    {
        accept = ssh_req_strings_present(payload, len, fields, 2); // sec 6.4: string name, string value
    }
    else if (rtype_len == 7 && mem.cmp(rtype, "pty-req", 7) == 0)
    {
        // sec 6.2: allocate a terminal for this session. The dimensions are "only informational",
        // so this layer records them and whoever runs the session decides whether it has a terminal
        // to give. No handler means no terminal, which is CHANNEL_FAILURE rather than a false yes.
        SshPtyRequest pty;
        if (ssh_pty_req_parse(payload, len, fields, &pty) && s_chcb.pty_req_cb != NULL &&
            s_chcb.pty_req_cb(i, c->local_id, &pty))
        {
            c->pty = PROTO_TRUE;
            c->width_chars = pty.width_chars;
            c->height_rows = pty.height_rows;
            c->width_px = pty.width_px;
            c->height_px = pty.height_px;
            accept = PROTO_TRUE;
        }
    }
    else if (rtype_len == 13 && mem.cmp(rtype, "window-change", 13) == 0)
    {
        // sec 6.7: "A response SHOULD NOT be sent to this message." The reply below is gated on
        // want_reply, which sec 6.7 fixes FALSE, so a conforming peer is answered nothing.
        SshPtyRequest dim;
        if (c->pty && ssh_window_change_parse(payload, len, fields, &dim))
        {
            c->width_chars = dim.width_chars;
            c->height_rows = dim.height_rows;
            c->width_px = dim.width_px;
            c->height_px = dim.height_px;
            if (s_chcb.window_change_cb != NULL)
            {
                s_chcb.window_change_cb(i, c->local_id, dim.width_chars, dim.height_rows, dim.width_px,
                                        dim.height_px);
            }
            accept = PROTO_TRUE;
        }
    }

#if PROTOCORE_ENABLE_SSH_SFTP || PROTOCORE_ENABLE_SSH_SCP
    ssh_classify_file_transfer_request(i, c->local_id, rtype, rtype_len, payload, len, &off, &accept);
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
    protocore_wr32be(out + 1, c->peer_id);
    *out_len = 5;
    return 0;
}

// ---------------------------------------------------------------------------
// CHANNEL_DATA (inbound) + flow control
// ---------------------------------------------------------------------------

int protocore_ssh_channel_handle_data(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap)
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
    uint32_t recipient = protocore_rd32be(payload + off);
    off += 4;
    const uint8_t *data;
    uint32_t dlen;
    if (!protocore_rd_str(payload, len, &off, &data, &dlen))
    {
        return -1;
    }

    SshChannel *c = protocore_ssh_chan_by_id(i, recipient);
    if (!c)
    {
        return -1;
    }
    if (!protocore_ssh_flow_recv_take(&c->flow, dlen))
    {
        return -1; // peer overran the advertised window (RFC 4254 §5.2)
    }

    if (dlen > 0)
    {
        // A forwarding channel's bytes are the forward's whatever else is bound; a session channel's
        // go to the sec 6.5 service that claimed it, or to the application when none did.
        switch (c->type)
        {
        case SSH_CHAN_DIRECT_TCPIP:
        case SSH_CHAN_FORWARDED_TCPIP: // forwarded TCP bytes (ssh -L / -R) -> the forward owner
            if (s_chcb.forward_data_cb)
            {
                s_chcb.forward_data_cb(i, c->local_id, data, dlen);
            }
            break;
        default:
            switch (c->service)
            {
#if PROTOCORE_ENABLE_SSH_SFTP
            case SSH_CHAN_SERVICE_SFTP: // SSH_FXP_* bytes -> the SFTP binding
                if (s_chcb.protocore_sftp_data_cb)
                {
                    s_chcb.protocore_sftp_data_cb(i, c->local_id, data, dlen);
                }
                break;
#endif
#if PROTOCORE_ENABLE_SSH_SCP
            case SSH_CHAN_SERVICE_SCP: // RCP protocol bytes -> the SCP binding
                if (s_chcb.protocore_scp_data_cb)
                {
                    s_chcb.protocore_scp_data_cb(i, c->local_id, data, dlen);
                }
                break;
#endif
            default: // shell/exec bytes -> the application
                if (s_chcb.data_cb)
                {
                    s_chcb.data_cb(i, c->local_id, data, dlen);
                }
                break;
            }
            break;
        }
    }

    // Replenish the window once it drops below half.
    uint32_t add = 0;
    if (protocore_ssh_flow_replenish_due(&c->flow, &add) &&
        protocore_ssh_sig_build_window_adjust(c->peer_id, add, out, cap, out_len) == 0)
    {
        protocore_ssh_flow_local_credit(&c->flow, add); // the caller emits *out_len unconditionally
    }
    return 0;
}

int protocore_ssh_channel_handle_extended_data(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
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
    uint32_t recipient = protocore_rd32be(payload + off);
    off += 8; // recipient channel, then the data_type_code this end does not separate
    const uint8_t *data;
    uint32_t dlen;
    if (!protocore_rd_str(payload, len, &off, &data, &dlen))
    {
        return -1;
    }

    SshChannel *c = protocore_ssh_chan_by_id(i, recipient);
    if (!c)
    {
        return -1;
    }
    if (!protocore_ssh_flow_recv_take(&c->flow, dlen))
    {
        return -1; // peer overran the advertised window (RFC 4254 §5.2)
    }

    // Replenish the window once it drops below half.
    uint32_t add = 0;
    if (protocore_ssh_flow_replenish_due(&c->flow, &add) &&
        protocore_ssh_sig_build_window_adjust(c->peer_id, add, out, cap, out_len) == 0)
    {
        protocore_ssh_flow_local_credit(&c->flow, add); // the caller emits *out_len unconditionally
    }
    return 0;
}

// ---------------------------------------------------------------------------
// CHANNEL_DATA (outbound)
// ---------------------------------------------------------------------------

int protocore_ssh_channel_build_data(uint8_t i, uint32_t channel, const uint8_t *data, size_t len, uint8_t *out,
                              size_t *out_len, size_t cap)
{
    SshChannel *c = (i < MAX_SSH_CONNS) ? protocore_ssh_chan_by_id(i, channel) : NULL;
    if (!c)
    {
        return -1;
    }
    return protocore_ssh_sig_build_data(&c->flow, c->peer_id, data, len, out, cap, out_len);
}

// ---------------------------------------------------------------------------
// WINDOW_ADJUST (inbound)
// ---------------------------------------------------------------------------

int protocore_ssh_channel_handle_window_adjust(uint8_t i, const uint8_t *payload, size_t len)
{
    if (i >= MAX_SSH_CONNS || len < 9 || payload[0] != SSH_MSG_CHANNEL_WINDOW_ADJUST)
    {
        return -1;
    }
    SshChannel *c = protocore_ssh_chan_by_id(i, protocore_rd32be(payload + 1));
    if (!c)
    {
        return -1;
    }
    protocore_ssh_flow_peer_add(&c->flow, protocore_rd32be(payload + 5));
    return 0;
}

// ---------------------------------------------------------------------------
// EOF + CLOSE
// ---------------------------------------------------------------------------

// Frame EOF for an open channel and latch that we sent it. The channel stays open, so the peer's
// direction keeps carrying (RFC 4254 sec 5.3).
static int build_eof_chan(SshChannel *c, uint8_t *out, size_t *out_len, size_t cap)
{
    if (!c || protocore_ssh_sig_build_eof(c->peer_id, out, cap, out_len) < 0)
    {
        return -1;
    }
    c->eof_sent = PROTO_TRUE;
    return 0;
}

int protocore_ssh_channel_build_eof(uint8_t i, uint32_t channel, uint8_t *out, size_t *out_len, size_t cap)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    return build_eof_chan(protocore_ssh_chan_by_id(i, channel), out, out_len, cap);
}

// Frame CLOSE for an open channel and mark it closed (shared by the inbound
// handler and the app/teardown path).
// Send CHANNEL_CLOSE once, and free the channel number only when both directions have closed.
// Freeing the slot is the mux's half; the message body is the signaling owner's.
static int build_close_chan(SshChannel *c, uint8_t *out, size_t *out_len, size_t cap)
{
    if (!c)
    {
        return -1;
    }
    // sec 5.3: "a party MUST send back an SSH_MSG_CHANNEL_CLOSE unless it has already sent this
    // message for the channel."
    if (!c->close_sent)
    {
        if (protocore_ssh_sig_build_close(c->peer_id, out, cap, out_len) < 0)
        {
            return -1;
        }
        c->close_sent = PROTO_TRUE;
    }
    else
    {
        *out_len = 0;
    }
    if (c->close_sent && c->close_received)
    {
        c->open = PROTO_FALSE; // both halves are in: the channel number may be reused
    }
    return 0;
}

int protocore_ssh_channel_build_close(uint8_t i, uint32_t channel, uint8_t *out, size_t *out_len, size_t cap)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    return build_close_chan(protocore_ssh_chan_by_id(i, channel), out, out_len, cap);
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 5.2 / sec 5.3 / sec 7.2 - putting a channel message on the stream
// ---------------------------------------------------------------------------

// CHANNEL_DATA and forwarded-tcpip CHANNEL_OPEN are built straight into the framer's span, so a
// full-size payload is framed where it was written. EOF and CLOSE are five bytes and go by value.

int protocore_ssh_channel_send_data(uint8_t i, uint32_t channel, const uint8_t *data, size_t len)
{
    size_t cap = 0;
    uint8_t *region = SshNetwork.payload_region(i, &cap);
    if (region == NULL)
    {
        return -1;
    }
    size_t plen = 0;
    if (protocore_ssh_channel_build_data(i, channel, data, len, region, &plen, cap) != 0)
    {
        return -1;
    }
    return SshNetwork.write_msg_at(i, plen) == 0 ? (int)len : -1;
}

int protocore_ssh_channel_send_eof(uint8_t i, uint32_t channel)
{
    uint8_t msg[SSH_CHANNEL_EOF_LEN];
    size_t n = 0;
    if (protocore_ssh_channel_build_eof(i, channel, msg, &n, sizeof(msg)) != 0 || n != sizeof(msg))
    {
        return -1;
    }
    return SshNetwork.write_msg(i, msg, n);
}

int protocore_ssh_channel_send_close(uint8_t i, uint32_t channel)
{
    uint8_t msg[SSH_CHANNEL_CLOSE_LEN];
    size_t n = 0;
    if (protocore_ssh_channel_build_close(i, channel, msg, &n, sizeof(msg)) != 0 || n != sizeof(msg))
    {
        return -1;
    }
    return SshNetwork.write_msg(i, msg, n);
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 6.10 - returning exit status
// ---------------------------------------------------------------------------
// Both are CHANNEL_REQUEST with want_reply FALSE, so nothing is expected back and the channel is
// closed after. Built in the framer's span like any other message this layer sends.

int protocore_ssh_channel_send_exit_status(uint8_t i, uint32_t channel, uint32_t exit_status)
{
    const SshChannel *c = protocore_ssh_chan_by_id(i, channel);
    size_t cap = 0;
    uint8_t *region = SshNetwork.payload_region(i, &cap);
    if (c == NULL || region == NULL)
    {
        return -1;
    }
    protocore_span w = protocore_span_from(region, cap);
    protocore_bw_put(&w, SSH_MSG_CHANNEL_REQUEST);
    protocore_bw_put_be(&w, c->peer_id, 4);
    protocore_ssh_wr_cstr(&w, "exit-status");
    protocore_bw_put(&w, 0); // want_reply FALSE
    protocore_bw_put_be(&w, exit_status, 4);
    if (!protocore_span_ok(w))
    {
        return -1;
    }
    return SshNetwork.write_msg_at(i, w.pos);
}

int protocore_ssh_channel_send_exit_signal(uint8_t i, uint32_t channel, const char *signal_name,
                                           proto_bool core_dumped, const char *err_msg)
{
    const SshChannel *c = protocore_ssh_chan_by_id(i, channel);
    size_t cap = 0;
    uint8_t *region = SshNetwork.payload_region(i, &cap);
    if (c == NULL || region == NULL || signal_name == NULL)
    {
        return -1;
    }
    protocore_span w = protocore_span_from(region, cap);
    protocore_bw_put(&w, SSH_MSG_CHANNEL_REQUEST);
    protocore_bw_put_be(&w, c->peer_id, 4);
    protocore_ssh_wr_cstr(&w, "exit-signal");
    protocore_bw_put(&w, 0); // want_reply FALSE
    protocore_ssh_wr_cstr(&w, signal_name);
    protocore_bw_put(&w, core_dumped ? 1 : 0);
    protocore_ssh_wr_cstr(&w, err_msg != NULL ? err_msg : "");
    protocore_ssh_wr_cstr(&w, ""); // language tag
    if (!protocore_span_ok(w))
    {
        return -1;
    }
    return SshNetwork.write_msg_at(i, w.pos);
}

int protocore_ssh_channel_send_open_forwarded(uint8_t i, const char *conn_addr, uint16_t conn_port,
                                              const char *orig_addr, uint16_t orig_port)
{
    size_t cap = 0;
    uint8_t *region = SshNetwork.payload_region(i, &cap);
    if (region == NULL)
    {
        return -1;
    }
    size_t plen = 0;
    const int ch =
        protocore_ssh_channel_open_forwarded(i, conn_addr, conn_port, orig_addr, orig_port, region, &plen, cap);
    if (ch < 0)
    {
        return -1; // channel pool full / build failed
    }
    return SshNetwork.write_msg_at(i, plen) == 0 ? ch : -1;
}

int protocore_ssh_channel_handle_eof(uint8_t i, const uint8_t *payload, size_t len)
{
    if (i >= MAX_SSH_CONNS || len < 5 || payload[0] != SSH_MSG_CHANNEL_EOF)
    {
        return -1;
    }
    SshChannel *c = protocore_ssh_chan_by_id(i, protocore_rd32be(payload + 1));
    if (c == NULL)
    {
        return -1;
    }
    c->relay_eof = PROTO_TRUE; // the peer sends no more; this direction still carries
    return 0;
}

int protocore_ssh_channel_handle_close(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                                size_t cap)
{
    *out_len = 0;
    if (i >= MAX_SSH_CONNS || len < 5 || payload[0] != SSH_MSG_CHANNEL_CLOSE)
    {
        return -1;
    }
    SshChannel *c = protocore_ssh_chan_by_id(i, protocore_rd32be(payload + 1));
    if (c == NULL)
    {
        return -1;
    }
    c->close_received = PROTO_TRUE;
    return build_close_chan(c, out, out_len, cap);
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 7 - TCP/IP port forwarding
// ---------------------------------------------------------------------------

#if PROTOCORE_SSH_PORT_FORWARD

#include "network_drivers/presentation/ssh/ssh.h"
#include "network_drivers/transport/tcp.h"

// Remote forwarding (ssh -R) allocates a real listener and bridges each accepted socket to a
// server-initiated forwarded-tcpip channel. sec 7.1 decides which bindings exist; the socket those
// bindings accept on is the listening role's, and its handler binds into the registry elsewhere.
#include "shared_primitives/ip.h"

// All SSH local-forward (ssh -L) state, owned by one instance (internal linkage): the policy
// callback. The channel is its ssh_chan row; the socket it bridges is the network layer's.
typedef struct
{
    SshForwardPolicyCb policy;
} SshFwdCtx;
static SshFwdCtx s_fwd;

// Target -> client iterations per channel per poll: bounds the work each loop so
// one busy forward cannot starve the others (PROTOCORE_SSH_FWD_CHUNK bytes each).
static const int kFwdBurst = 4;

// ===========================================================================
// Remote forwarding (ssh -R): the client asks the server to LISTEN on a port and
// carry each accepted connection back over a server-initiated forwarded-tcpip
// channel. One bind == one listener the sec 4.1 role opened; one bridge == one accepted
// conn_pool slot glued to one SSH channel. All storage static (no heap).
// ===========================================================================





// direct-tcpip open: check policy, connect to host:port (blocking), bind a slot.
// Returns 0 to confirm the channel, < 0 to refuse (denied / connect failed / full).
static int on_forward_open(uint8_t ssh_slot, uint32_t channel, const char *host, size_t host_len, uint16_t port)
{
    char hbuf[PROTOCORE_SSH_FWD_HOST_MAX];
    if (host_len == 0 || host_len >= sizeof(hbuf))
    {
        return -1;
    }
    mem.cpy(hbuf, host, host_len);
    hbuf[host_len] = 0;
    if (s_fwd.policy && !s_fwd.policy(hbuf, port))
    {
        return -1; // target administratively denied
    }
    // blocks on DNS + connect
    if (SshNetwork.chan_open(ssh_slot, channel, hbuf, port, PROTOCORE_SSH_FWD_CONNECT_MS) < 0)
    {
        return -1; // -> CHANNEL_OPEN_FAILURE (connect failed)
    }
    return 0;
}

// Inbound channel bytes. direct-tcpip (ssh -L): client -> outbound target socket.
// forwarded-tcpip (ssh -R): client -> the accepted socket we bridged back to it.
static void on_forward_data(uint8_t ssh_slot, uint32_t channel, const uint8_t *data, size_t len)
{
    if (protocore_ssh_chan_by_id(ssh_slot, channel))
    {
        SshNetwork.chan_write(ssh_slot, channel, data, len);
    }
}

// ---------------------------------------------------------------------------
// Remote-forward seam (GLOBAL_REQUEST tcpip-forward / cancel-tcpip-forward, ssh -R)
// ---------------------------------------------------------------------------

// A remote-forward binding: a listener this SSH connection asked us to open (RFC 4254 sec 7.1).
typedef struct
{
    proto_bool active;
    uint8_t ssh_slot;
    uint8_t listener_idx;                       // handle from ssh_rfwd_listener_open()
    uint16_t bind_port;                         // port bound on the device
    char bind_addr[PROTOCORE_SSH_FWD_HOST_MAX]; // address the client requested (echoed in CHANNEL_OPEN)
} SshRFwdBind;

// All SSH remote-forward (ssh -R) state, owned by one instance (internal linkage): the listener
// bindings the accepted GLOBAL_REQUESTs created. One named owner, unreachable cross-TU.
typedef struct
{
    SshRFwdBind rbind[PROTOCORE_SSH_RFWD_MAX];
} SshRFwdCtx;

static SshRFwdCtx s_rfwd;

static int rbind_find_free()
{
    for (int i = 0; i < PROTOCORE_SSH_RFWD_MAX; i++)
    {
        if (!s_rfwd.rbind[i].active)
        {
            return i;
        }
    }
    return -1;
}

static SshRFwdBind *rbind_find(uint8_t ssh_slot, uint16_t port)
{
    for (int i = 0; i < PROTOCORE_SSH_RFWD_MAX; i++)
    {
        if (s_rfwd.rbind[i].active && s_rfwd.rbind[i].ssh_slot == ssh_slot && s_rfwd.rbind[i].bind_port == port)
        {
            return &s_rfwd.rbind[i];
        }
    }
    return NULL;
}

// RFC 4254 sec 7.2: a connection arriving on a forwarded listener names the binding that asked for
// it, and the forwarded-tcpip CHANNEL_OPEN echoes that binding's address and port.
proto_bool protocore_ssh_forward_binding(uint8_t listener_idx, uint8_t *ssh_slot, uint16_t *bind_port,
                                         const char **bind_addr)
{
    for (int i = 0; i < PROTOCORE_SSH_RFWD_MAX; i++)
    {
        if (s_rfwd.rbind[i].active && s_rfwd.rbind[i].listener_idx == listener_idx)
        {
            *ssh_slot = s_rfwd.rbind[i].ssh_slot;
            *bind_port = s_rfwd.rbind[i].bind_port;
            *bind_addr = s_rfwd.rbind[i].bind_addr;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

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
    // sec 7.1 decides that this binding exists; the socket it accepts on is the listening role's.
    const int li = ssh_rfwd_listener_open(bind_port);
    if (li < 0)
    {
        return -1; // no listener capacity, or the port could not be bound
    }

    s_rfwd.rbind[bi].active = PROTO_TRUE;
    s_rfwd.rbind[bi].ssh_slot = ssh_slot;
    s_rfwd.rbind[bi].listener_idx = (uint8_t)li;
    s_rfwd.rbind[bi].bind_port = bind_port;
    size_t al = addr_len < sizeof(s_rfwd.rbind[bi].bind_addr) - 1 ? addr_len : sizeof(s_rfwd.rbind[bi].bind_addr) - 1;
    mem.cpy(s_rfwd.rbind[bi].bind_addr, addr, al);
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
    ssh_rfwd_listener_close(b->listener_idx);
    b->active = PROTO_FALSE;
    return 0;
}

// The client's reply to a server-initiated forwarded-tcpip open.
static void on_forward_confirm(uint8_t ssh_slot, uint32_t channel, proto_bool ok)
{
    SshChannel *c = protocore_ssh_chan_by_id(ssh_slot, channel);
    if (!c)
    {
        return;
    }
    if (ok)
    {
        c->pending = PROTO_FALSE; // bytes may now flow (pumped on the next poll)
    }
    else
    {
        SshNetwork.chan_close(ssh_slot, channel); // client refused the channel: drop the socket
        c->open = PROTO_FALSE;
    }
}

// ---------------------------------------------------------------------------
// ProtoConn::PROTO_SSH_RFWD handler: an inbound connection on a forwarded port.
// ---------------------------------------------------------------------------






void protocore_ssh_forward_set_policy_cb(SshForwardPolicyCb cb)
{
    s_fwd.policy = cb;
}

void protocore_ssh_forward_begin()
{
    for (int i = 0; i < PROTOCORE_SSH_RFWD_MAX; i++)
    {
        s_rfwd.rbind[i].active = PROTO_FALSE;
    }
    protocore_ssh_channel_set_forward_open_cb(on_forward_open);
    protocore_ssh_channel_set_forward_data_cb(on_forward_data);
    // Remote forwarding (ssh -R): the request/cancel seam, the open-confirmation
    // callback, and the accept handler for connections on a forwarded port.
    protocore_ssh_channel_set_rforward_open_cb(on_rforward_open);
    protocore_ssh_channel_set_rforward_cancel_cb(on_rforward_cancel);
    protocore_ssh_channel_set_forward_confirm_cb(on_forward_confirm);
}

void protocore_ssh_forward_pump(uint8_t ssh_slot)
{
    uint8_t buf[PROTOCORE_SSH_FWD_CHUNK];
    for (uint32_t ch = 0; ch < PROTOCORE_SSH_MAX_CHANNELS; ch++)
    {
        SshChannel *c = &ssh_chan[ssh_slot][ch];
        if (c->type != SSH_CHAN_DIRECT_TCPIP && c->type != SSH_CHAN_FORWARDED_TCPIP)
        {
            continue;
        }

        // Client closed its side of the channel: drop the target socket.
        if (!c->open)
        {
            SshNetwork.chan_close(ssh_slot, ch);
            continue;
        }

        // Target -> client: forward what the peer window allows, bounded per poll.
        for (int burst = 0; burst < kFwdBurst; burst++)
        {
            size_t avail = SshNetwork.chan_avail(ssh_slot, ch);
            if (avail > sizeof(buf))
            {
                avail = sizeof(buf);
            }
            uint32_t budget = protocore_ssh_flow_send_cap(&c->flow, (uint32_t)avail);
            if (budget == 0)
            {
                break;
            }
            size_t n = SshNetwork.chan_read(ssh_slot, ch, buf, budget);
            if (n == 0)
            {
                break;
            }
            if (protocore_ssh_channel_send_data(ssh_slot, ch, buf, n) < 0)
            {
                break; // sized to the window, so this should send; retry next poll
            }
        }

        // Target closed and fully drained: this direction sends no more, so half-close it with
        // CHANNEL_EOF, once. The channel stays open and the peer keeps sending (RFC 4254 sec 5.3).
        if (SshNetwork.chan_drained(ssh_slot, ch) && !c->eof_sent)
        {
            protocore_ssh_channel_send_eof(ssh_slot, ch);
        }

        // Both directions have signaled EOF: terminate the channel and drop the socket, once.
        if (c->eof_sent && c->relay_eof && !c->close_sent)
        {
            // The socket goes now; the channel number stays taken until the peer's CLOSE answers
            // ours (sec 5.3), which build_close_chan settles.
            protocore_ssh_channel_send_close(ssh_slot, ch);
            SshNetwork.chan_close(ssh_slot, ch);
        }
    }
}

void protocore_ssh_forward_reset(uint8_t ssh_slot)
{
    // every socket this connection's channels bridged, forwarded or direct.
    SshNetwork.chan_close_all(ssh_slot);
    // remote (ssh -R): stop this connection's forwarded listeners and drop every
    // accepted socket it had bridged (the SSH channels go away with the connection).
    for (int i = 0; i < PROTOCORE_SSH_RFWD_MAX; i++)
    {
        if (s_rfwd.rbind[i].active && s_rfwd.rbind[i].ssh_slot == ssh_slot)
        {
            ssh_rfwd_listener_close(s_rfwd.rbind[i].listener_idx);
            s_rfwd.rbind[i].active = PROTO_FALSE;
        }
    }
}

#endif // PROTOCORE_SSH_PORT_FORWARD

// ---------------------------------------------------------------------------
// RFC 4254 - message numbers 80 to 127, reached once authentication has passed
// ---------------------------------------------------------------------------

int ssh_connection_dispatch(uint8_t i, uint8_t msg_type, const uint8_t *payload, size_t len)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshSession *s = &ssh_sess[i];

    // The reply buffer is borrowed for this dispatch, not carried on the worker stack: it is the
    // single largest frame on the SSH path and the handshake below it is the deepest call chain in
    // the library. protocore_plaintext_span binds the capacity to the allocation.
    size_t mark = protocore_plaintext_mark();
    protocore_span reply = protocore_plaintext_span(SSH_PKT_BUF_SIZE, 16);
    if (!protocore_span_ok(reply))
    {
        protocore_plaintext_release(mark);
        return -1; // arena exhausted: fail closed, the caller drops the connection
    }
    size_t n = 0;

    switch (msg_type)
    {
    case SSH_MSG_GLOBAL_REQUEST:
        // RFC 4254 §4: connection-wide request (e.g. tcpip-forward for ssh -R). Only
        // meaningful post-auth; reply REQUEST_SUCCESS/FAILURE when want_reply is set.
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        if (ssh_global_request_handle(i, payload, len, reply.buf, &n, reply.cap) != 0)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        if (n > 0)
        {
            SshNetwork.emit(i, reply.buf, n);
        }
        protocore_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_OPEN:
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        if (protocore_ssh_channel_handle_open(i, payload, len, reply.buf, &n, reply.cap) != 0)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        SshNetwork.emit(i, reply.buf, n);
        protocore_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_OPEN_CONFIRMATION:
        // The client's reply to a server-initiated forwarded-tcpip open (ssh -R): record
        // the peer window and start the bridge. A stray confirm is ignored (not fatal).
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        protocore_ssh_channel_handle_open_confirm(i, payload, len);
        protocore_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_OPEN_FAILURE:
        // The client refused a server-initiated forwarded-tcpip open: tear the bridge down.
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        protocore_ssh_channel_handle_open_failure(i, payload, len);
        protocore_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_REQUEST:
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        if (protocore_ssh_channel_handle_request(i, payload, len, reply.buf, &n, reply.cap) != 0)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        SshNetwork.emit(i, reply.buf, n); // SUCCESS/FAILURE only when want_reply was set
        protocore_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_DATA:
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        if (protocore_ssh_channel_handle_data(i, payload, len, reply.buf, &n, reply.cap) != 0)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        SshNetwork.emit(i, reply.buf, n); // WINDOW_ADJUST when the receive window is replenished
        protocore_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_EXTENDED_DATA:
        // RFC 4254 sec 5.2: either side may send it, so it is accounted against the window rather
        // than answered with UNIMPLEMENTED.
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        if (protocore_ssh_channel_handle_extended_data(i, payload, len, reply.buf, &n, reply.cap) != 0)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        SshNetwork.emit(i, reply.buf, n); // WINDOW_ADJUST when the receive window is replenished
        protocore_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_WINDOW_ADJUST:
        // RFC 4254 sec 5: the connection protocol runs on top of userauth, so every channel message
        // is refused before authentication, these two included.
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        protocore_ssh_channel_handle_window_adjust(i, payload, len);
        protocore_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_EOF:
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        protocore_ssh_channel_handle_eof(i, payload, len);
        protocore_plaintext_release(mark);
        return 0;

    case SSH_MSG_CHANNEL_CLOSE:
        // RFC 4254 sec 5.3: answer an inbound CLOSE with CHANNEL_CLOSE, in its own binary packet.
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            return -1;
        }
        if (protocore_ssh_channel_handle_close(i, payload, len, reply.buf, &n, reply.cap) == 0 && n == 5)
        {
            SshNetwork.emit(i, reply.buf, 5);
        }
        protocore_plaintext_release(mark);
        return 0;

    default: {
        // RFC 4253 sec 11.4: an unrecognized message is answered with SSH_MSG_UNIMPLEMENTED, which
        // the transport builds - message 3 and the receive counter are both its.
        size_t un = 0;
        if (ssh_pkt_unimplemented(i, reply.buf, &un, sizeof(reply.buf)) == 0)
        {
            SshNetwork.emit(i, reply.buf, un);
        }
        protocore_plaintext_release(mark);
        return 0;
    }
    }
}
