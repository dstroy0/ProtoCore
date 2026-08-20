// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file connection.c
 * @brief RFC 4254: the channel multiplexer, its window arithmetic, and the forwarding owners.
 */

#include "network_drivers/presentation/ssh/connection/connection.h"
#include "mmgr/bytes/bytes.h"           // bytes.rd_u32 / bytes.rd_str - the one length-prefixed reader
#include "mmgr/endian/endian.h"         // endian.wr32be - the one wire-integer writer
#include "mmgr/plaintext/plaintext.h"   // protocore_plaintext_span / _mark / _release - the dispatch reply buffer
#include "mmgr/protoframe/protoframe.h" // protocore_fval - the log field value
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h" // str.len - the bounded string length
#include "mmgr/secure/secure.h"     // the persistent end this module's key material is taken from
#include "network_drivers/presentation/ssh/network/network.h" // SshNetwork: the socket seam
#include "network_drivers/presentation/ssh/ssh.h"
#include "network_drivers/presentation/ssh/transport/transport/transport.h" // ssh_sess, ssh_pkt - session and packet state
#include "shared/log/log.h"                                                 // PROTOCORE_LOGD
static uint8_t ssh_app_server_work[16]; // the borrow an entry takes; SshAppServer never reads it

#if PROTOCORE_ENABLE_SSH_SFTP || PROTOCORE_ENABLE_SSH_SCP
#include "network_drivers/presentation/ssh/app/server/server.h" // SshAppServer: the service a channel request binds
#endif
#if PROTOCORE_SSH_PORT_FORWARD
#include "network_drivers/presentation/ssh/server/server.h" // SshServer: the listener a binding accepts on
#include "network_drivers/transport/tcp/tcp.h"              // Tcp: the socket a forwarded channel bridges
#include "shared/ip/ip.h"                                   // the address a binding accepts on
#endif

// ---------------------------------------------------------------------------
// RFC 4254 sec 5 - channel multiplexing
// ---------------------------------------------------------------------------

SshChannel ssh_chan[MAX_SSH_CONNS][PROTOCORE_SSH_MAX_CHANNELS];

// Defined below; the sec 5 and sec 6.2 handlers above them reach the table through these.
void protocore_ssh_chan_by_id(uint8_t *restrict work);
void ssh_pty_req_parse(uint8_t *restrict work);

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
} SshConnHandlers;

// A remote-forward binding: a listener this SSH connection asked us to open (RFC 4254 sec 7.1).
typedef struct
{
    proto_bool active;
    uint8_t ssh_slot;
    uint8_t listener_idx;                       // handle from ssh_rfwd_listener_open()
    uint16_t bind_port;                         // port bound on the device
    char bind_addr[PROTOCORE_SSH_FWD_HOST_MAX]; // address the client requested (echoed in CHANNEL_OPEN)
} SshRFwdBind;

/**
 * @brief The layer's compile-time storage: the remote-forward bindings an accepted request created.
 *
 * The ssh_chan[][] table above is the shared cross-TU substrate; a channel is its row. All BSS.
 */
struct SshConnectionStorage
{
    SshRFwdBind rbind[PROTOCORE_SSH_RFWD_MAX];
    SshConnHandlers handlers;  ///< the application hooks this layer calls back into
    SshForwardPolicyCb policy; ///< what a local forward (ssh -L) is admitted by
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SSH_CONNECTION_OFF_CTX 0u
static_assert(SSH_CONNECTION_OFF_CTX + sizeof(struct SshConnectionStorage) <= PROTOCORE_SSH_CONNECTION_BORROW,
              "PROTOCORE_SSH_CONNECTION_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define SSH_CONNECTION_CTX(w) ((struct SshConnectionStorage *)(void *)((w) + SSH_CONNECTION_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SSH_CONNECTION_BORROW persistent bytes
} SshConnectionOwnCtx;
static SshConnectionOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ssh_connection_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_SSH_CONNECTION_BORROW).buf;
    }
    return s_own.span;
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 5.2 - window arithmetic and channel signalling
// ---------------------------------------------------------------------------

void protocore_ssh_flow_init(uint8_t *restrict work)
{
    (void)work;
    SshFlow *f = SshConnectionV.flow.f;
    uint32_t local_window = SshConnectionV.flow.local_window;
    uint32_t peer_window = SshConnectionV.flow.peer_window;
    uint32_t peer_max_pkt = SshConnectionV.flow.peer_max_pkt;

    f->local_window = local_window;
    f->local_max = local_window;
    f->peer_window = peer_window;
    f->peer_max_pkt = peer_max_pkt;
}

void protocore_ssh_flow_recv_take(uint8_t *restrict work)
{
    (void)work;
    SshFlow *f = SshConnectionV.flow.f;
    uint32_t n = SshConnectionV.flow.n;

    if (n > f->local_window)
    {
        SshConnectionV.ok = PROTO_FALSE;
        return; // peer overran the advertised window (RFC 4254 sec 5.2)
    }
    f->local_window -= n;
    SshConnectionV.ok = PROTO_TRUE;
    return;
}

void protocore_ssh_flow_replenish_due(uint8_t *restrict work)
{
    (void)work;
    const SshFlow *f = SshConnectionV.flow.f;
    uint32_t *add = &SshConnectionV.flow.add;

    if (f->local_window >= f->local_max / 2)
    {
        SshConnectionV.ok = PROTO_FALSE;
        return;
    }
    *add = f->local_max - f->local_window;
    SshConnectionV.ok = PROTO_TRUE;
    return;
}

void protocore_ssh_flow_local_credit(uint8_t *restrict work)
{
    (void)work;
    SshFlow *f = SshConnectionV.flow.f;
    uint32_t add = SshConnectionV.flow.add;

    f->local_window += add;
}

void protocore_ssh_flow_send_allows(uint8_t *restrict work)
{
    (void)work;
    const SshFlow *f = SshConnectionV.flow.f;
    size_t len = SshConnectionV.chan.len;

    SshConnectionV.ok = len <= f->peer_window && len <= f->peer_max_pkt;

    return;
}

void protocore_ssh_flow_send_cap(uint8_t *restrict work)
{
    (void)work;
    const SshFlow *f = SshConnectionV.flow.f;
    uint32_t want = SshConnectionV.flow.want;

    uint32_t cap = want;
    if (cap > f->peer_window)
    {
        cap = f->peer_window;
    }
    if (cap > f->peer_max_pkt)
    {
        cap = f->peer_max_pkt;
    }
    SshConnectionV.u32 = cap;
    return;
}

void protocore_ssh_flow_send_take(uint8_t *restrict work)
{
    (void)work;
    SshFlow *f = SshConnectionV.flow.f;
    uint32_t n = SshConnectionV.flow.n;

    f->peer_window -= n;
}

void protocore_ssh_flow_peer_add(uint8_t *restrict work)
{
    (void)work;
    SshFlow *f = SshConnectionV.flow.f;
    uint32_t add = SshConnectionV.flow.add;

    uint32_t w = f->peer_window;
    f->peer_window = (w + add < w) ? 0xFFFFFFFFu : (w + add);
}

// ---------------------------------------------------------------------------
// Channel signaling (RFC 4254 sec 5)
// ---------------------------------------------------------------------------

int32_t protocore_ssh_sig_build_open_failure(uint8_t *out, size_t cap, uint32_t peer_id, uint32_t reason,
                                             size_t *out_len)
{
    if (cap < 17)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_OPEN_FAILURE;
    endian.wr32be(out + 1, peer_id);
    endian.wr32be(out + 5, reason);
    endian.wr32be(out + 9, 0);  // empty description
    endian.wr32be(out + 13, 0); // empty language
    *out_len = 17;
    return 0;
}

int32_t protocore_ssh_sig_build_open_confirm(const SshFlow *f, uint32_t peer_id, uint32_t local_id, uint8_t *out,
                                             size_t cap, size_t *out_len)
{
    if (cap < 17)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_OPEN_CONFIRMATION;
    endian.wr32be(out + 1, peer_id);
    endian.wr32be(out + 5, local_id);
    endian.wr32be(out + 9, f->local_window);
    endian.wr32be(out + 13, SSH_CHAN_MAX_PACKET);
    *out_len = 17;
    return 0;
}

int32_t protocore_ssh_sig_build_data(SshFlow *f, uint32_t peer_id, const uint8_t *data, size_t len, uint8_t *out,
                                     size_t cap, size_t *out_len)
{
    SshConnectionV.flow.f = f;
    SshConnectionV.flow.len = len;
    protocore_ssh_flow_send_allows(protocore_ssh_connection_span());
    if (!SshConnectionV.ok)
    {
        return -1; // would exceed the peer's window or its maximum packet size
    }
    if (cap < 9 + len)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_DATA;
    endian.wr32be(out + 1, peer_id);
    endian.wr32be(out + 5, (uint32_t)len);
    mem.cpy(out + 9, data, len);
    *out_len = 9 + len;
    SshConnectionV.flow.f = f;
    SshConnectionV.flow.n = (uint32_t)len;
    protocore_ssh_flow_send_take(protocore_ssh_connection_span());
    return 0;
}

int32_t protocore_ssh_sig_build_window_adjust(uint32_t peer_id, uint32_t add, uint8_t *out, size_t cap, size_t *out_len)
{
    if (cap < 9)
    {
        return -1;
    }
    out[0] = SSH_MSG_CHANNEL_WINDOW_ADJUST;
    endian.wr32be(out + 1, peer_id);
    endian.wr32be(out + 5, add);
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
    endian.wr32be(out + 1, peer_id);
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
    endian.wr32be(out + 1, peer_id);
    *out_len = 5;
    return 0;
}

void protocore_ssh_channel_set_data_cb(uint8_t *restrict work)
{
    (void)work;
    SshChannelDataCb cb = SshConnectionV.data_cb;

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.data_cb = cb;
}

void protocore_ssh_channel_set_pty_req_cb(uint8_t *restrict work)
{
    (void)work;
    SshPtyReqCb cb = SshConnectionV.pty_req_cb;

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.pty_req_cb = cb;
}

void protocore_ssh_channel_set_window_change_cb(uint8_t *restrict work)
{
    (void)work;
    SshWindowChangeCb cb = SshConnectionV.window_change_cb;

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.window_change_cb = cb;
}

void protocore_ssh_channel_pty(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    uint32_t channel = SshConnectionV.chan.channel;
    uint32_t *width_chars = &SshConnectionV.pty.width_chars;
    uint32_t *height_rows = &SshConnectionV.pty.height_rows;
    uint32_t *width_px = &SshConnectionV.pty.width_px;
    uint32_t *height_px = &SshConnectionV.pty.height_px;

    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.id = channel;
    protocore_ssh_chan_by_id(work);
    const SshChannel *c = SshConnectionV.found;
    if (c == NULL || !c->pty)
    {
        SshConnectionV.ok = PROTO_FALSE;
        return;
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
    SshConnectionV.ok = PROTO_TRUE;
    return;
}

#if PROTOCORE_ENABLE_SSH_SFTP
void protocore_ssh_channel_set_sftp_open_cb(uint8_t *restrict work)
{
    (void)work;
    SshSftpOpenCb cb = SshConnectionV.sftp_open_cb;

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.protocore_sftp_open_cb = cb;
}
SshSftpOpenCb protocore_ssh_channel_sftp_open_cb(void)
{
    return SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.protocore_sftp_open_cb;
}
void protocore_ssh_channel_set_sftp_data_cb(uint8_t *restrict work)
{
    (void)work;
    SshSftpDataCb cb = SshConnectionV.sftp_data_cb;

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.protocore_sftp_data_cb = cb;
}
#else

// PROTOCORE_ENABLE_SSH_SFTP is 0: the handle still carries the setters, so they exist and record nothing.
void protocore_ssh_channel_set_sftp_open_cb(uint8_t *restrict work)
{
    (void)work;
}
void protocore_ssh_channel_set_sftp_data_cb(uint8_t *restrict work)
{
    (void)work;
}
#endif

#if PROTOCORE_ENABLE_SSH_SCP
void protocore_ssh_channel_set_scp_open_cb(uint8_t *restrict work)
{
    (void)work;
    SshScpOpenCb cb = SshConnectionV.scp_open_cb;

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.protocore_scp_open_cb = cb;
}
SshScpOpenCb protocore_ssh_channel_scp_open_cb(void)
{
    return SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.protocore_scp_open_cb;
}
void protocore_ssh_channel_set_scp_data_cb(uint8_t *restrict work)
{
    (void)work;
    SshScpDataCb cb = SshConnectionV.scp_data_cb;

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.protocore_scp_data_cb = cb;
}
#else

// PROTOCORE_ENABLE_SSH_SCP is 0: the handle still carries the setters, so they exist and record nothing.
void protocore_ssh_channel_set_scp_open_cb(uint8_t *restrict work)
{
    (void)work;
}
void protocore_ssh_channel_set_scp_data_cb(uint8_t *restrict work)
{
    (void)work;
}
#endif

void protocore_ssh_channel_set_forward_open_cb(uint8_t *restrict work)
{
    (void)work;
    SshForwardOpenCb cb = SshConnectionV.forward_open_cb;

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.forward_open_cb = cb;
}

void protocore_ssh_channel_set_forward_data_cb(uint8_t *restrict work)
{
    (void)work;
    SshForwardDataCb cb = SshConnectionV.forward_data_cb;

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.forward_data_cb = cb;
}

void protocore_ssh_channel_set_rforward_open_cb(uint8_t *restrict work)
{
    (void)work;
    SshRemoteForwardOpenCb cb = SshConnectionV.rforward_open_cb;

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.rfwd_open_cb = cb;
}

void protocore_ssh_channel_set_rforward_cancel_cb(uint8_t *restrict work)
{
    (void)work;
    SshRemoteForwardCancelCb cb = SshConnectionV.rforward_cancel_cb;

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.rfwd_cancel_cb = cb;
}

void protocore_ssh_channel_set_forward_confirm_cb(uint8_t *restrict work)
{
    (void)work;
    SshForwardConfirmCb cb = SshConnectionV.forward_confirm_cb;

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.forward_confirm_cb = cb;
}

void protocore_ssh_channel_init(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = SshConnectionV.chan.slot;

    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    mem.set(ssh_chan[i], 0, sizeof(ssh_chan[i])); // reset every channel for this connection
}

// ---------------------------------------------------------------------------
// Channel table (owned here)
// ---------------------------------------------------------------------------

void protocore_ssh_channel_bind_service(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    uint32_t channel = SshConnectionV.chan.channel;
    SshChanService service = SshConnectionV.chan.service;

    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.id = channel;
    protocore_ssh_chan_by_id(work);
    SshChannel *c = SshConnectionV.found;
    if (c == NULL)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    c->service = service;
    SshConnectionV.i32 = 0;
    return;
}

void protocore_ssh_chan_by_id(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = SshConnectionV.chan.slot;
    uint32_t id = SshConnectionV.chan.id;

    if (i >= MAX_SSH_CONNS || id >= PROTOCORE_SSH_MAX_CHANNELS || !ssh_chan[i][id].open)
    {
        SshConnectionV.found = NULL;
        return;
    }
    SshConnectionV.found = &ssh_chan[i][id];
    return;
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

void protocore_ssh_chan_alloc(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = SshConnectionV.chan.slot;

    if (i >= MAX_SSH_CONNS)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    for (int c = 0; c < PROTOCORE_SSH_MAX_CHANNELS; c++)
    {
        if (!ssh_chan[i][c].open && !ssh_chan[i][c].pending)
        {
            SshConnectionV.i32 = c;
            return;
        }
    }
    SshConnectionV.i32 = -1;
    return;
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

void ssh_global_request_handle(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = SshConnectionV.chan.slot;
    const uint8_t *payload = SshConnectionV.chan.payload;
    size_t len = SshConnectionV.chan.len;
    uint8_t *out = SshConnectionV.chan.out;
    size_t *out_len = &SshConnectionV.chan.out_len;
    size_t cap = SshConnectionV.chan.cap;

    *out_len = 0;
    if (i >= MAX_SSH_CONNS || len < 1 || payload[0] != SSH_MSG_GLOBAL_REQUEST)
    {
        SshConnectionV.i32 = -1;
        return;
    }

    size_t off = 1;
    const uint8_t *name;
    uint32_t name_len;
    if (!bytes.rd_str(payload, len, &off, &name, &name_len))
    {
        SshConnectionV.i32 = -1;
        return;
    }
    if (off >= len)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    proto_bool want_reply = payload[off++] != 0;

    proto_bool is_fwd = (name_len == 13 && mem.cmp(name, "tcpip-forward", 13) == 0);
    proto_bool is_cancel = (name_len == 20 && mem.cmp(name, "cancel-tcpip-forward", 20) == 0);

    if (is_fwd || is_cancel)
    {
        // Request-specific data: bind address (string) followed by bind port (uint32).
        const uint8_t *addr;
        uint32_t addr_len;
        if (!bytes.rd_str(payload, len, &off, &addr, &addr_len) || off + 4 > len)
        {
            SshConnectionV.i32 = -1;
            return;
        }
        uint16_t bind_port = (uint16_t)endian.rd32be(payload + off);

        // The owner allocates (or cancels) the real listener; -1 means "refused".
        int bound = -1;
        if (is_fwd && SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.rfwd_open_cb)
        {
            bound = SSH_CONNECTION_CTX(protocore_ssh_connection_span())
                        ->handlers.rfwd_open_cb(i, (const char *)addr, addr_len, bind_port);
        }
        else if (is_cancel && SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.rfwd_cancel_cb)
        {
            bound = SSH_CONNECTION_CTX(protocore_ssh_connection_span())
                        ->handlers.rfwd_cancel_cb(i, (const char *)addr, addr_len, bind_port);
        }

        if (bound < 0)
        {
            if (want_reply) // refused: no owner, policy denied, or the table is full
            {
                if (cap < 1)
                {
                    SshConnectionV.i32 = -1;
                    return;
                }
                out[0] = SSH_MSG_REQUEST_FAILURE;
                *out_len = 1;
            }
            SshConnectionV.i32 = 0;
            return;
        }

        if (want_reply)
        {
            // A tcpip-forward that requested port 0 echoes the allocated port
            // (RFC 4254 §7.1); a specific port and cancel reply bare success.
            if (is_fwd && bind_port == 0)
            {
                if (cap < 5)
                {
                    SshConnectionV.i32 = -1;
                    return;
                }
                out[0] = SSH_MSG_REQUEST_SUCCESS;
                endian.wr32be(out + 1, (uint32_t)(uint16_t)bound);
                *out_len = 5;
            }
            else
            {
                if (cap < 1)
                {
                    SshConnectionV.i32 = -1;
                    return;
                }
                out[0] = SSH_MSG_REQUEST_SUCCESS;
                *out_len = 1;
            }
        }
        SshConnectionV.i32 = 0;
        return;
    }

    // Any other global request is unrecognized: RFC 4254 §4 -> REQUEST_FAILURE when the
    // client wants a reply, otherwise silently ignored. (Never UNIMPLEMENTED: the
    // GLOBAL_REQUEST message type is known; only this request name is not.)
    if (want_reply)
    {
        if (cap < 1)
        {
            SshConnectionV.i32 = -1;
            return;
        }
        out[0] = SSH_MSG_REQUEST_FAILURE;
        *out_len = 1;
    }
    SshConnectionV.i32 = 0;
    return;
}

// ---------------------------------------------------------------------------
// Server-initiated CHANNEL_OPEN (forwarded-tcpip, ssh -R) + its CONFIRM / FAILURE
// ---------------------------------------------------------------------------

void protocore_ssh_channel_open_forwarded(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    const char *conn_addr = SshConnectionV.fwd.conn_addr;
    uint16_t conn_port = SshConnectionV.fwd.conn_port;
    const char *orig_addr = SshConnectionV.fwd.orig_addr;
    uint16_t orig_port = SshConnectionV.fwd.orig_port;
    uint8_t *out = SshConnectionV.chan.out;
    size_t *out_len = &SshConnectionV.chan.out_len;
    size_t cap = SshConnectionV.chan.cap;

    *out_len = 0;
    if (i >= MAX_SSH_CONNS || !conn_addr || !orig_addr)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshConnectionV.chan.slot = i;
    protocore_ssh_chan_alloc(work);
    const int slot = SshConnectionV.i32;
    if (slot < 0)
    {
        SshConnectionV.i32 = -1;
        return; // channel pool full
    }

    const char *type = "forwarded-tcpip";
    size_t tl = 15, ca = str.len(conn_addr, cap), oa = str.len(orig_addr, cap);
    // byte || string(type) || u32 sender || u32 window || u32 maxpkt || string(conn_addr)
    //   || u32 conn_port || string(orig_addr) || u32 orig_port  (RFC 4254 §7.2)
    size_t need = 1 + (4 + tl) + 12 + (4 + ca) + 4 + (4 + oa) + 4;
    if (cap < need)
    {
        SshConnectionV.i32 = -1;
        return;
    }

    size_t off = 0;
    out[off++] = SSH_MSG_CHANNEL_OPEN;
    endian.wr32be(out + off, (uint32_t)tl);
    mem.cpy(out + off + 4, type, tl);
    off += 4 + tl;
    endian.wr32be(out + off, (uint32_t)slot); // our sender channel id
    endian.wr32be(out + off + 4, SSH_CHAN_WINDOW);
    endian.wr32be(out + off + 8, SSH_CHAN_MAX_PACKET);
    off += 12;
    endian.wr32be(out + off, (uint32_t)ca);
    mem.cpy(out + off + 4, conn_addr, ca);
    off += 4 + ca;
    endian.wr32be(out + off, conn_port);
    off += 4;
    endian.wr32be(out + off, (uint32_t)oa);
    mem.cpy(out + off + 4, orig_addr, oa);
    off += 4 + oa;
    endian.wr32be(out + off, orig_port);
    off += 4;

    SshChannel *c = chan_take(i, slot);
    c->pending = PROTO_TRUE; // awaiting the client's CHANNEL_OPEN_CONFIRMATION
    c->type = SSH_CHAN_FORWARDED_TCPIP;
    SshConnectionV.flow.f = &c->flow;
    SshConnectionV.flow.local_window = SSH_CHAN_WINDOW;
    SshConnectionV.flow.peer_window = 0;
    SshConnectionV.flow.peer_max_pkt = 0;
    protocore_ssh_flow_init(work);

    *out_len = off;
    SshConnectionV.i32 = slot;
    return;
}

void protocore_ssh_channel_handle_open_confirm(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    const uint8_t *payload = SshConnectionV.chan.payload;
    size_t len = SshConnectionV.chan.len;

    // byte || recipient(our local id) || sender(peer id) || window || max packet.
    if (i >= MAX_SSH_CONNS || len < 17 || payload[0] != SSH_MSG_CHANNEL_OPEN_CONFIRMATION)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshChannel *c = chan_pending_by_id(i, endian.rd32be(payload + 1));
    if (!c)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    c->peer_id = endian.rd32be(payload + 5);
    SshConnectionV.flow.f = &c->flow;
    SshConnectionV.flow.add = endian.rd32be(payload + 9);
    protocore_ssh_flow_peer_add(work);
    c->flow.peer_max_pkt = endian.rd32be(payload + 13);
    c->pending = PROTO_FALSE;
    c->open = PROTO_TRUE;
    if (SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.forward_confirm_cb)
    {
        SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.forward_confirm_cb(i, c->local_id, PROTO_TRUE);
    }
    SshConnectionV.i32 = 0;
    return;
}

void protocore_ssh_channel_handle_open_failure(uint8_t *restrict work)
{
    (void)work;
    uint8_t i = SshConnectionV.chan.slot;
    const uint8_t *payload = SshConnectionV.chan.payload;
    size_t len = SshConnectionV.chan.len;

    // byte || recipient(our local id) || reason || desc || lang.
    if (i >= MAX_SSH_CONNS || len < 5 || payload[0] != SSH_MSG_CHANNEL_OPEN_FAILURE)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshChannel *c = chan_pending_by_id(i, endian.rd32be(payload + 1));
    if (!c)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    uint32_t ch = c->local_id;
    c->pending = PROTO_FALSE;
    c->open = PROTO_FALSE; // free the slot; the client refused the forward
    if (SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.forward_confirm_cb)
    {
        SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.forward_confirm_cb(i, ch, PROTO_FALSE);
    }
    SshConnectionV.i32 = 0;
    return;
}

void protocore_ssh_channel_handle_open(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    const uint8_t *payload = SshConnectionV.chan.payload;
    size_t len = SshConnectionV.chan.len;
    uint8_t *out = SshConnectionV.chan.out;
    size_t *out_len = &SshConnectionV.chan.out_len;
    size_t cap = SshConnectionV.chan.cap;

    if (i >= MAX_SSH_CONNS || len < 1 || payload[0] != SSH_MSG_CHANNEL_OPEN)
    {
        SshConnectionV.i32 = -1;
        return;
    }

    size_t off = 1;
    const uint8_t *type;
    uint32_t type_len;
    if (!bytes.rd_str(payload, len, &off, &type, &type_len))
    {
        SshConnectionV.i32 = -1;
        return;
    }
    if (off + 12 > len)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    uint32_t sender = endian.rd32be(payload + off);
    uint32_t init_window = endian.rd32be(payload + off + 4);
    uint32_t max_pkt = endian.rd32be(payload + off + 8);
    off += 12;

    proto_bool is_session = (type_len == 7 && mem.cmp(type, "session", 7) == 0);
    proto_bool is_dtcpip = (type_len == 12 && mem.cmp(type, "direct-tcpip", 12) == 0);
    // RFC 4254 sec 7.2: "When a connection comes to a port for which remote forwarding has been
    // requested, a channel is opened to forward the port to the other side." It travels toward the
    // end that asked for the forward, so only that end accepts it; the listening end is the one that
    // sends them and treats an inbound one as a type it does not serve.
    proto_bool is_fwd_tcpip = (type_len == 15 && mem.cmp(type, "forwarded-tcpip", 15) == 0) && ssh_pkt[i].is_client;
    if (!is_session && !is_dtcpip && !is_fwd_tcpip)
    {
        SshConnectionV.i32 = build_open_failure(out, cap, sender, 3u, out_len);
        return; // unknown channel type
    }

    // direct-tcpip data: host(string) port(u32) orig_host(string) orig_port(u32).
    const uint8_t *fhost = NULL;
    uint32_t fhost_len = 0;
    uint16_t fport = 0;
    if (is_dtcpip)
    {
        if (!SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.forward_open_cb)
        {
            SshConnectionV.i32 = build_open_failure(out, cap, sender, 1u, out_len);
            return; // forwarding off: prohibited
        }
        if (!bytes.rd_str(payload, len, &off, &fhost, &fhost_len) || off + 4 > len)
        {
            SshConnectionV.i32 = -1;
            return;
        }
        fport = (uint16_t)endian.rd32be(payload + off); // orig host/port follow but are advisory
    }

    SshConnectionV.chan.slot = i;
    protocore_ssh_chan_alloc(work);
    const int slot = SshConnectionV.i32;
    if (slot < 0)
    {
        SshConnectionV.i32 = build_open_failure(out, cap, sender, 4u, out_len);
        return; // pool full
    }

    SshChannel *c = chan_take(i, slot);
    c->open = PROTO_TRUE;
    c->type = is_dtcpip ? SSH_CHAN_DIRECT_TCPIP : (is_fwd_tcpip ? SSH_CHAN_FORWARDED_TCPIP : SSH_CHAN_SESSION);
    c->peer_id = sender;
    SshConnectionV.flow.f = &c->flow;
    SshConnectionV.flow.local_window = SSH_CHAN_WINDOW;
    SshConnectionV.flow.peer_window = init_window;
    SshConnectionV.flow.peer_max_pkt = max_pkt;
    protocore_ssh_flow_init(work);

    if (is_dtcpip)
    {
        // The owner does the actual TCP connect (no I/O in this codec); on refusal
        // free the channel and fail closed.
        if (SSH_CONNECTION_CTX(protocore_ssh_connection_span())
                ->handlers.forward_open_cb(i, c->local_id, (const char *)fhost, fhost_len, fport) < 0)
        {
            c->open = PROTO_FALSE;
            SshConnectionV.i32 = build_open_failure(out, cap, sender, 2u, out_len);
            return; // connect failed
        }
    }
    SshConnectionV.i32 = build_open_confirm(c, out, cap, out_len);
    return;
}

// ---------------------------------------------------------------------------
// CHANNEL_REQUEST → SUCCESS / FAILURE
// ---------------------------------------------------------------------------

// Read @p n consecutive strings from @p off: true when every one of them is present and whole.
void ssh_req_strings_present(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *p = SshConnectionV.pty.p;
    size_t len = SshConnectionV.chan.len;
    size_t off = SshConnectionV.pty.off;
    uint8_t n = SshConnectionV.pty.n;

    const uint8_t *s = NULL;
    uint32_t slen = 0;
    for (uint8_t k = 0; k < n; k++)
    {
        if (!bytes.rd_str(p, len, &off, &s, &slen))
        {
            SshConnectionV.ok = PROTO_FALSE;
            return;
        }
    }
    SshConnectionV.ok = PROTO_TRUE;
    return;
}

// RFC 4254 sec 6.2: string TERM, four uint32 dimensions, string encoded terminal modes.
void ssh_pty_req_fields_present(uint8_t *restrict work)
{
    const uint8_t *p = SshConnectionV.pty.p;
    size_t len = SshConnectionV.chan.len;
    size_t off = SshConnectionV.pty.off;

    SshPtyRequest pty;
    SshConnectionV.pty.p = p;
    SshConnectionV.chan.len = len;
    SshConnectionV.pty.off = off;
    SshConnectionV.pty.req = &pty;
    ssh_pty_req_parse(work);
    return;
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 8 - encoding of terminal modes
// ---------------------------------------------------------------------------

// "Opcodes 1 to 159 have a single uint32 argument. Opcodes 160 to 255 are not yet defined, and
// cause parsing to stop (they should only be used after any other data). The stream is terminated
// by opcode TTY_OP_END (0x00)."
#define SSH_TTY_OP_END 0u
#define SSH_TTY_OP_ARG_LAST 159u

void ssh_pty_modes_valid(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *modes = SshConnectionV.pty.modes;
    const uint32_t len = SshConnectionV.pty.modes_len;
    uint32_t *consumed = &SshConnectionV.pty.consumed;

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
            SshConnectionV.ok = PROTO_FALSE;
            return; // an opcode whose uint32 argument is not there
        }
        k += 5u;
    }
    if (consumed != NULL)
    {
        *consumed = k;
    }
    SshConnectionV.ok = PROTO_TRUE;
    return;
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
    out->width_chars = endian.rd32be(p + *off);
    out->height_rows = endian.rd32be(p + *off + 4);
    out->width_px = endian.rd32be(p + *off + 8);
    out->height_px = endian.rd32be(p + *off + 12);
    *off += 16u;
    return PROTO_TRUE;
}

void ssh_pty_req_parse(uint8_t *restrict work)
{
    const uint8_t *p = SshConnectionV.pty.p;
    size_t len = SshConnectionV.chan.len;
    size_t off = SshConnectionV.pty.off;
    SshPtyRequest *out = SshConnectionV.pty.req;

    if (out == NULL)
    {
        SshConnectionV.ok = PROTO_FALSE;
        return;
    }
    mem.set(out, 0, sizeof(*out));

    const uint8_t *term = NULL;
    uint32_t term_len = 0;
    if (!bytes.rd_str(p, len, &off, &term, &term_len))
    {
        SshConnectionV.ok = PROTO_FALSE;
        return;
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
        SshConnectionV.ok = PROTO_FALSE;
        return;
    }
    if (!bytes.rd_str(p, len, &off, &out->modes, &out->modes_len))
    {
        SshConnectionV.ok = PROTO_FALSE;
        return;
    }
    SshConnectionV.pty.modes = out->modes;
    SshConnectionV.pty.modes_len = out->modes_len;
    ssh_pty_modes_valid(work);
    return;
}

void ssh_window_change_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *p = SshConnectionV.pty.p;
    size_t len = SshConnectionV.chan.len;
    size_t off = SshConnectionV.pty.off;
    SshPtyRequest *out = SshConnectionV.pty.req;

    if (out == NULL)
    {
        SshConnectionV.ok = PROTO_FALSE;
        return;
    }
    mem.set(out, 0, sizeof(*out));
    SshConnectionV.ok = read_dimensions(p, len, &off, out);
    return;
}

void protocore_ssh_channel_handle_request(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    const uint8_t *payload = SshConnectionV.chan.payload;
    size_t len = SshConnectionV.chan.len;
    uint8_t *out = SshConnectionV.chan.out;
    size_t *out_len = &SshConnectionV.chan.out_len;
    size_t cap = SshConnectionV.chan.cap;

    *out_len = 0;
    if (i >= MAX_SSH_CONNS || len < 1 || payload[0] != SSH_MSG_CHANNEL_REQUEST)
    {
        SshConnectionV.i32 = -1;
        return;
    }

    size_t off = 1;
    if (off + 4 > len)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    uint32_t recipient = endian.rd32be(payload + off); // our channel id
    off += 4;
    const uint8_t *rtype;
    uint32_t rtype_len;
    if (!bytes.rd_str(payload, len, &off, &rtype, &rtype_len))
    {
        SshConnectionV.i32 = -1;
        return;
    }
    if (off >= len)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    proto_bool want_reply = payload[off++] != 0;

    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.id = recipient;
    protocore_ssh_chan_by_id(work);
    SshChannel *c = SshConnectionV.found;
    if (!c)
    {
        SshConnectionV.i32 = -1;
        return;
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
        SshConnectionV.pty.p = payload;
        SshConnectionV.chan.len = len;
        SshConnectionV.pty.off = fields;
        SshConnectionV.pty.n = 1; // sec 6.5: string command
        ssh_req_strings_present(work);
        accept = SshConnectionV.ok;
    }
    else if (rtype_len == 3 && mem.cmp(rtype, "env", 3) == 0)
    {
        SshConnectionV.pty.p = payload;
        SshConnectionV.chan.len = len;
        SshConnectionV.pty.off = fields;
        SshConnectionV.pty.n = 2; // sec 6.4: string name, string value
        ssh_req_strings_present(work);
        accept = SshConnectionV.ok;
    }
    else if (rtype_len == 7 && mem.cmp(rtype, "pty-req", 7) == 0)
    {
        // sec 6.2: allocate a terminal for this session. The dimensions are "only informational",
        // so this layer records them and whoever runs the session decides whether it has a terminal
        // to give. No handler means no terminal, which is CHANNEL_FAILURE rather than a false yes.
        SshPtyRequest pty;
        SshConnectionV.pty.p = payload;
        SshConnectionV.chan.len = len;
        SshConnectionV.pty.off = fields;
        SshConnectionV.pty.req = &pty;
        ssh_pty_req_parse(work);
        if (SshConnectionV.ok && SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.pty_req_cb != NULL &&
            SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.pty_req_cb(i, c->local_id, &pty))
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
        SshConnectionV.pty.p = payload;
        SshConnectionV.chan.len = len;
        SshConnectionV.pty.off = fields;
        SshConnectionV.pty.req = &dim;
        ssh_window_change_parse(work);
        if (c->pty && SshConnectionV.ok)
        {
            c->width_chars = dim.width_chars;
            c->height_rows = dim.height_rows;
            c->width_px = dim.width_px;
            c->height_px = dim.height_px;
            if (SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.window_change_cb != NULL)
            {
                SSH_CONNECTION_CTX(protocore_ssh_connection_span())
                    ->handlers.window_change_cb(i, c->local_id, dim.width_chars, dim.height_rows, dim.width_px,
                                                dim.height_px);
            }
            accept = PROTO_TRUE;
        }
    }

#if PROTOCORE_ENABLE_SSH_SFTP || PROTOCORE_ENABLE_SSH_SCP
    SshAppServerV.slot = i;
    SshAppServerV.channel = c->local_id;
    SshAppServerV.accept = accept;
    SshAppServerV.req.rtype = rtype;
    SshAppServerV.req.rtype_len = rtype_len;
    SshAppServerV.req.payload = payload;
    SshAppServerV.req.len = len;
    SshAppServerV.req.off = off;
    SshAppServer.classify(ssh_app_server_work);
    off = SshAppServerV.req.off;
    accept = SshAppServerV.accept;
#endif

    if (!want_reply)
    {
        SshConnectionV.i32 = 0;
        return;
    }

    if (cap < 5)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    out[0] = accept ? SSH_MSG_CHANNEL_SUCCESS : SSH_MSG_CHANNEL_FAILURE;
    endian.wr32be(out + 1, c->peer_id);
    *out_len = 5;
    SshConnectionV.i32 = 0;
    return;
}

// ---------------------------------------------------------------------------
// CHANNEL_DATA (inbound) + flow control
// ---------------------------------------------------------------------------

void protocore_ssh_channel_handle_data(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    const uint8_t *payload = SshConnectionV.chan.payload;
    size_t len = SshConnectionV.chan.len;
    uint8_t *out = SshConnectionV.chan.out;
    size_t *out_len = &SshConnectionV.chan.out_len;
    size_t cap = SshConnectionV.chan.cap;

    *out_len = 0;
    if (i >= MAX_SSH_CONNS || len < 1 || payload[0] != SSH_MSG_CHANNEL_DATA)
    {
        SshConnectionV.i32 = -1;
        return;
    }

    size_t off = 1;
    if (off + 4 > len)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    uint32_t recipient = endian.rd32be(payload + off);
    off += 4;
    const uint8_t *data;
    uint32_t dlen;
    if (!bytes.rd_str(payload, len, &off, &data, &dlen))
    {
        SshConnectionV.i32 = -1;
        return;
    }

    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.id = recipient;
    protocore_ssh_chan_by_id(work);
    SshChannel *c = SshConnectionV.found;
    if (!c)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshConnectionV.flow.f = &c->flow;
    SshConnectionV.flow.n = dlen;
    protocore_ssh_flow_recv_take(work);
    if (!SshConnectionV.ok)
    {
        SshConnectionV.i32 = -1;
        return; // peer overran the advertised window (RFC 4254 §5.2)
    }

    if (dlen > 0)
    {
        // A forwarding channel's bytes are the forward's whatever else is bound; a session channel's
        // go to the sec 6.5 service that claimed it, or to the application when none did.
        switch (c->type)
        {
        case SSH_CHAN_DIRECT_TCPIP:
        case SSH_CHAN_FORWARDED_TCPIP: // forwarded TCP bytes (ssh -L / -R) -> the forward owner
            if (SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.forward_data_cb)
            {
                SSH_CONNECTION_CTX(protocore_ssh_connection_span())
                    ->handlers.forward_data_cb(i, c->local_id, data, dlen);
            }
            break;
        default:
            switch (c->service)
            {
#if PROTOCORE_ENABLE_SSH_SFTP
            case SSH_CHAN_SERVICE_SFTP: // SSH_FXP_* bytes -> the SFTP binding
                if (SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.protocore_sftp_data_cb)
                {
                    SSH_CONNECTION_CTX(protocore_ssh_connection_span())
                        ->handlers.protocore_sftp_data_cb(i, c->local_id, data, dlen);
                }
                break;
#endif
#if PROTOCORE_ENABLE_SSH_SCP
            case SSH_CHAN_SERVICE_SCP: // RCP protocol bytes -> the SCP binding
                if (SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.protocore_scp_data_cb)
                {
                    SSH_CONNECTION_CTX(protocore_ssh_connection_span())
                        ->handlers.protocore_scp_data_cb(i, c->local_id, data, dlen);
                }
                break;
#endif
            default: // shell/exec bytes -> the application
                if (SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.data_cb)
                {
                    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->handlers.data_cb(i, c->local_id, data, dlen);
                }
                break;
            }
            break;
        }
    }

    // Replenish the window once it drops below half.
    uint32_t add = 0;
    SshConnectionV.flow.f = &c->flow;
    protocore_ssh_flow_replenish_due(work);
    add = SshConnectionV.flow.add;
    if (SshConnectionV.ok && protocore_ssh_sig_build_window_adjust(c->peer_id, add, out, cap, out_len) == 0)
    {
        SshConnectionV.flow.f = &c->flow;
        SshConnectionV.flow.add = add;
        protocore_ssh_flow_local_credit(work); // the caller emits *out_len unconditionally
    }
    SshConnectionV.i32 = 0;
    return;
}

void protocore_ssh_channel_handle_extended_data(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    const uint8_t *payload = SshConnectionV.chan.payload;
    size_t len = SshConnectionV.chan.len;
    uint8_t *out = SshConnectionV.chan.out;
    size_t *out_len = &SshConnectionV.chan.out_len;
    size_t cap = SshConnectionV.chan.cap;

    *out_len = 0;
    if (i >= MAX_SSH_CONNS || len < 1 || payload[0] != SSH_MSG_CHANNEL_EXTENDED_DATA)
    {
        SshConnectionV.i32 = -1;
        return;
    }

    size_t off = 1;
    if (off + 8 > len)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    uint32_t recipient = endian.rd32be(payload + off);
    off += 8; // recipient channel, then the data_type_code this end does not separate
    const uint8_t *data;
    uint32_t dlen;
    if (!bytes.rd_str(payload, len, &off, &data, &dlen))
    {
        SshConnectionV.i32 = -1;
        return;
    }

    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.id = recipient;
    protocore_ssh_chan_by_id(work);
    SshChannel *c = SshConnectionV.found;
    if (!c)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshConnectionV.flow.f = &c->flow;
    SshConnectionV.flow.n = dlen;
    protocore_ssh_flow_recv_take(work);
    if (!SshConnectionV.ok)
    {
        SshConnectionV.i32 = -1;
        return; // peer overran the advertised window (RFC 4254 §5.2)
    }

    // Replenish the window once it drops below half.
    uint32_t add = 0;
    SshConnectionV.flow.f = &c->flow;
    protocore_ssh_flow_replenish_due(work);
    add = SshConnectionV.flow.add;
    if (SshConnectionV.ok && protocore_ssh_sig_build_window_adjust(c->peer_id, add, out, cap, out_len) == 0)
    {
        SshConnectionV.flow.f = &c->flow;
        SshConnectionV.flow.add = add;
        protocore_ssh_flow_local_credit(work); // the caller emits *out_len unconditionally
    }
    SshConnectionV.i32 = 0;
    return;
}

// ---------------------------------------------------------------------------
// CHANNEL_DATA (outbound)
// ---------------------------------------------------------------------------

void protocore_ssh_channel_build_data(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    uint32_t channel = SshConnectionV.chan.channel;
    const uint8_t *data = SshConnectionV.chan.data;
    size_t len = SshConnectionV.chan.len;
    uint8_t *out = SshConnectionV.chan.out;
    size_t *out_len = &SshConnectionV.chan.out_len;
    size_t cap = SshConnectionV.chan.cap;

    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.id = channel;
    protocore_ssh_chan_by_id(work);
    SshChannel *c = (i < MAX_SSH_CONNS) ? SshConnectionV.found : NULL;
    if (!c)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshConnectionV.i32 = protocore_ssh_sig_build_data(&c->flow, c->peer_id, data, len, out, cap, out_len);
    return;
}

// ---------------------------------------------------------------------------
// WINDOW_ADJUST (inbound)
// ---------------------------------------------------------------------------

void protocore_ssh_channel_handle_window_adjust(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    const uint8_t *payload = SshConnectionV.chan.payload;
    size_t len = SshConnectionV.chan.len;

    if (i >= MAX_SSH_CONNS || len < 9 || payload[0] != SSH_MSG_CHANNEL_WINDOW_ADJUST)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.id = endian.rd32be(payload + 1);
    protocore_ssh_chan_by_id(work);
    SshChannel *c = SshConnectionV.found;
    if (!c)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshConnectionV.flow.f = &c->flow;
    SshConnectionV.flow.add = endian.rd32be(payload + 5);
    protocore_ssh_flow_peer_add(work);
    SshConnectionV.i32 = 0;
    return;
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

void protocore_ssh_channel_build_eof(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    uint32_t channel = SshConnectionV.chan.channel;
    uint8_t *out = SshConnectionV.chan.out;
    size_t *out_len = &SshConnectionV.chan.out_len;
    size_t cap = SshConnectionV.chan.cap;

    if (i >= MAX_SSH_CONNS)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.id = channel;
    protocore_ssh_chan_by_id(work);
    SshConnectionV.i32 = build_eof_chan(SshConnectionV.found, out, out_len, cap);
    return;
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

void protocore_ssh_channel_build_close(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    uint32_t channel = SshConnectionV.chan.channel;
    uint8_t *out = SshConnectionV.chan.out;
    size_t *out_len = &SshConnectionV.chan.out_len;
    size_t cap = SshConnectionV.chan.cap;

    if (i >= MAX_SSH_CONNS)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.id = channel;
    protocore_ssh_chan_by_id(work);
    SshConnectionV.i32 = build_close_chan(SshConnectionV.found, out, out_len, cap);
    return;
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 5.2 / sec 5.3 / sec 7.2 - putting a channel message on the stream
// ---------------------------------------------------------------------------

// CHANNEL_DATA and forwarded-tcpip CHANNEL_OPEN are built straight into the framer's span, so a
// full-size payload is framed where it was written. EOF and CLOSE are five bytes and go by value.

void protocore_ssh_channel_send_data(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    uint32_t channel = SshConnectionV.chan.channel;
    const uint8_t *data = SshConnectionV.chan.data;
    size_t len = SshConnectionV.chan.len;

    size_t cap = 0;
    SshNetworkV.ssh_slot = i;
    SshNetwork.payload_region(protocore_ssh_network_span());
    uint8_t *region = SshNetworkV.region;
    cap = SshNetworkV.read_args.cap;
    if (region == NULL)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    size_t plen = 0;
    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.channel = channel;
    SshConnectionV.chan.data = data;
    SshConnectionV.chan.len = len;
    SshConnectionV.chan.out = region;
    SshConnectionV.chan.cap = cap;
    protocore_ssh_channel_build_data(work);
    plen = SshConnectionV.chan.out_len;
    if (SshConnectionV.i32 != 0)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshNetworkV.ssh_slot = i;
    SshNetworkV.msg.plen = plen;
    SshNetwork.write_msg_at(protocore_ssh_network_span());
    SshConnectionV.i32 = SshNetworkV.i32 == 0 ? (int)len : -1;
    return;
}

void protocore_ssh_channel_send_eof(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    uint32_t channel = SshConnectionV.chan.channel;

    uint8_t msg[SSH_CHANNEL_EOF_LEN];
    size_t n = 0;
    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.channel = channel;
    SshConnectionV.chan.out = msg;
    SshConnectionV.chan.cap = sizeof(msg);
    protocore_ssh_channel_build_eof(work);
    n = SshConnectionV.chan.out_len;
    if (SshConnectionV.i32 != 0 || n != sizeof(msg))
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshNetworkV.ssh_slot = i;
    SshNetworkV.msg.payload = msg;
    SshNetworkV.msg.len = n;
    SshNetwork.write_msg(protocore_ssh_network_span());
    SshConnectionV.i32 = SshNetworkV.i32;
    return;
}

void protocore_ssh_channel_send_close(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    uint32_t channel = SshConnectionV.chan.channel;

    uint8_t msg[SSH_CHANNEL_CLOSE_LEN];
    size_t n = 0;
    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.channel = channel;
    SshConnectionV.chan.out = msg;
    SshConnectionV.chan.cap = sizeof(msg);
    protocore_ssh_channel_build_close(work);
    n = SshConnectionV.chan.out_len;
    if (SshConnectionV.i32 != 0 || n != sizeof(msg))
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshNetworkV.ssh_slot = i;
    SshNetworkV.msg.payload = msg;
    SshNetworkV.msg.len = n;
    SshNetwork.write_msg(protocore_ssh_network_span());
    SshConnectionV.i32 = SshNetworkV.i32;
    return;
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 6.10 - returning exit status
// ---------------------------------------------------------------------------
// Both are CHANNEL_REQUEST with want_reply FALSE, so nothing is expected back and the channel is
// closed after. Built in the framer's span like any other message this layer sends.

void protocore_ssh_channel_send_exit_status(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    uint32_t channel = SshConnectionV.chan.channel;
    uint32_t exit_status = SshConnectionV.chan.exit_status;

    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.id = channel;
    protocore_ssh_chan_by_id(work);
    const SshChannel *c = SshConnectionV.found;
    size_t cap = 0;
    SshNetworkV.ssh_slot = i;
    SshNetwork.payload_region(protocore_ssh_network_span());
    uint8_t *region = SshNetworkV.region;
    cap = SshNetworkV.read_args.cap;
    if (c == NULL || region == NULL)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    protocore_span w = span.from(region, cap);
    bytes.put(&w, SSH_MSG_CHANNEL_REQUEST);
    bytes.put_be(&w, c->peer_id, 4);
    protocore_ssh_wr_cstr(&w, "exit-status");
    bytes.put(&w, 0); // want_reply FALSE
    bytes.put_be(&w, exit_status, 4);
    if (!span.ok(w))
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshNetworkV.ssh_slot = i;
    SshNetworkV.msg.plen = w.pos;
    SshNetwork.write_msg_at(protocore_ssh_network_span());
    SshConnectionV.i32 = SshNetworkV.i32;
    return;
}

void protocore_ssh_channel_send_exit_signal(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    uint32_t channel = SshConnectionV.chan.channel;
    const char *signal_name = SshConnectionV.chan.signal_name;
    proto_bool core_dumped = SshConnectionV.chan.core_dumped;
    const char *err_msg = SshConnectionV.chan.err_msg;

    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.id = channel;
    protocore_ssh_chan_by_id(work);
    const SshChannel *c = SshConnectionV.found;
    size_t cap = 0;
    SshNetworkV.ssh_slot = i;
    SshNetwork.payload_region(protocore_ssh_network_span());
    uint8_t *region = SshNetworkV.region;
    cap = SshNetworkV.read_args.cap;
    if (c == NULL || region == NULL || signal_name == NULL)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    protocore_span w = span.from(region, cap);
    bytes.put(&w, SSH_MSG_CHANNEL_REQUEST);
    bytes.put_be(&w, c->peer_id, 4);
    protocore_ssh_wr_cstr(&w, "exit-signal");
    bytes.put(&w, 0); // want_reply FALSE
    protocore_ssh_wr_cstr(&w, signal_name);
    bytes.put(&w, core_dumped ? 1 : 0);
    protocore_ssh_wr_cstr(&w, err_msg != NULL ? err_msg : "");
    protocore_ssh_wr_cstr(&w, ""); // language tag
    if (!span.ok(w))
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshNetworkV.ssh_slot = i;
    SshNetworkV.msg.plen = w.pos;
    SshNetwork.write_msg_at(protocore_ssh_network_span());
    SshConnectionV.i32 = SshNetworkV.i32;
    return;
}

void protocore_ssh_channel_send_open_forwarded(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    const char *conn_addr = SshConnectionV.fwd.conn_addr;
    uint16_t conn_port = SshConnectionV.fwd.conn_port;
    const char *orig_addr = SshConnectionV.fwd.orig_addr;
    uint16_t orig_port = SshConnectionV.fwd.orig_port;

    size_t cap = 0;
    SshNetworkV.ssh_slot = i;
    SshNetwork.payload_region(protocore_ssh_network_span());
    uint8_t *region = SshNetworkV.region;
    cap = SshNetworkV.read_args.cap;
    if (region == NULL)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    size_t plen = 0;
    const int ch = SshConnectionV.chan.slot = i;
    SshConnectionV.fwd.conn_addr = conn_addr;
    SshConnectionV.fwd.conn_port = conn_port;
    SshConnectionV.fwd.orig_addr = orig_addr;
    SshConnectionV.fwd.orig_port = orig_port;
    SshConnectionV.chan.out = region;
    SshConnectionV.chan.cap = cap;
    protocore_ssh_channel_open_forwarded(work);
    plen = SshConnectionV.chan.out_len;
    if (ch < 0)
    {
        SshConnectionV.i32 = -1;
        return; // channel pool full / build failed
    }
    SshNetworkV.ssh_slot = i;
    SshNetworkV.msg.plen = plen;
    SshNetwork.write_msg_at(protocore_ssh_network_span());
    SshConnectionV.i32 = SshNetworkV.i32 == 0 ? ch : -1;
    return;
}

void protocore_ssh_channel_handle_eof(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    const uint8_t *payload = SshConnectionV.chan.payload;
    size_t len = SshConnectionV.chan.len;

    if (i >= MAX_SSH_CONNS || len < 5 || payload[0] != SSH_MSG_CHANNEL_EOF)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.id = endian.rd32be(payload + 1);
    protocore_ssh_chan_by_id(work);
    SshChannel *c = SshConnectionV.found;
    if (c == NULL)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    c->relay_eof = PROTO_TRUE; // the peer sends no more; this direction still carries
    SshConnectionV.i32 = 0;
    return;
}

void protocore_ssh_channel_handle_close(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    const uint8_t *payload = SshConnectionV.chan.payload;
    size_t len = SshConnectionV.chan.len;
    uint8_t *out = SshConnectionV.chan.out;
    size_t *out_len = &SshConnectionV.chan.out_len;
    size_t cap = SshConnectionV.chan.cap;

    *out_len = 0;
    if (i >= MAX_SSH_CONNS || len < 5 || payload[0] != SSH_MSG_CHANNEL_CLOSE)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshConnectionV.chan.slot = i;
    SshConnectionV.chan.id = endian.rd32be(payload + 1);
    protocore_ssh_chan_by_id(work);
    SshChannel *c = SshConnectionV.found;
    if (c == NULL)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    c->close_received = PROTO_TRUE;
    SshConnectionV.i32 = build_close_chan(c, out, out_len, cap);
    return;
}

// ---------------------------------------------------------------------------
// RFC 4254 sec 7 - TCP/IP port forwarding
// ---------------------------------------------------------------------------

#if PROTOCORE_SSH_PORT_FORWARD

// Remote forwarding (ssh -R) allocates a real listener and bridges each accepted socket to a
// server-initiated forwarded-tcpip channel. sec 7.1 decides which bindings exist; the socket those
// bindings accept on is the listening role's, and its handler binds into the registry elsewhere.

// All SSH local-forward (ssh -L) state, owned by one instance (internal linkage): the policy
// callback. The channel is its ssh_chan row; the socket it bridges is the network layer's.

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
    if (SSH_CONNECTION_CTX(protocore_ssh_connection_span())->policy &&
        !SSH_CONNECTION_CTX(protocore_ssh_connection_span())->policy(hbuf, port))
    {
        return -1; // target administratively denied
    }
    // blocks on DNS + connect
    SshNetworkV.ssh_slot = ssh_slot;
    SshNetworkV.stream.channel = channel;
    SshNetworkV.dial.host = hbuf;
    SshNetworkV.dial.port = port;
    SshNetworkV.dial.timeout_ms = PROTOCORE_SSH_FWD_CONNECT_MS;
    SshNetwork.chan_open(protocore_ssh_network_span());
    if (SshNetworkV.i32 < 0)
    {
        return -1; // -> CHANNEL_OPEN_FAILURE (connect failed)
    }
    return 0;
}

// Inbound channel bytes. direct-tcpip (ssh -L): client -> outbound target socket.
// forwarded-tcpip (ssh -R): client -> the accepted socket we bridged back to it.
static void on_forward_data(uint8_t ssh_slot, uint32_t channel, const uint8_t *data, size_t len)
{
    SshConnectionV.chan.slot = ssh_slot;
    SshConnectionV.chan.id = channel;
    SshConnection.chan_by_id(protocore_ssh_connection_span());
    if (SshConnectionV.found)
    {
        SshNetworkV.ssh_slot = ssh_slot;
        SshNetworkV.stream.channel = channel;
        SshNetworkV.msg.payload = data;
        SshNetworkV.msg.len = len;
        SshNetwork.chan_write(protocore_ssh_network_span());
    }
}

// ---------------------------------------------------------------------------
// Remote-forward seam (GLOBAL_REQUEST tcpip-forward / cancel-tcpip-forward, ssh -R)
// ---------------------------------------------------------------------------

// All SSH remote-forward (ssh -R) state, owned by one instance (internal linkage): the listener
// bindings the accepted GLOBAL_REQUESTs created. One named owner, unreachable cross-TU.

static int rbind_find_free()
{
    for (int i = 0; i < PROTOCORE_SSH_RFWD_MAX; i++)
    {
        if (!SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].active)
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
        if (SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].active &&
            SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].ssh_slot == ssh_slot &&
            SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].bind_port == port)
        {
            return &SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i];
        }
    }
    return NULL;
}

// RFC 4254 sec 7.2: a connection arriving on a forwarded listener names the binding that asked for
// it, and the forwarded-tcpip CHANNEL_OPEN echoes that binding's address and port.
void protocore_ssh_forward_binding(uint8_t *restrict work)
{
    (void)work;
    const uint8_t listener_idx = SshConnectionV.fwd.listener_idx;
    uint8_t *ssh_slot = &SshConnectionV.fwd.out_slot;
    uint16_t *bind_port = &SshConnectionV.fwd.bind_port;
    const char **bind_addr = &SshConnectionV.fwd.bind_addr;
    for (int i = 0; i < PROTOCORE_SSH_RFWD_MAX; i++)
    {
        if (SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].active &&
            SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].listener_idx == listener_idx)
        {
            *ssh_slot = SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].ssh_slot;
            *bind_port = SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].bind_port;
            *bind_addr = SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].bind_addr;
            SshConnectionV.ok = PROTO_TRUE;
            return;
        }
    }
    SshConnectionV.ok = PROTO_FALSE;
    return;
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
    SshServerV.bind_port = bind_port;
    SshServer.rfwd_listener_open(protocore_ssh_server_span());
    const int li = SshServerV.i32;
    if (li < 0)
    {
        return -1; // no listener capacity, or the port could not be bound
    }

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[bi].active = PROTO_TRUE;
    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[bi].ssh_slot = ssh_slot;
    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[bi].listener_idx = (uint8_t)li;
    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[bi].bind_port = bind_port;
    size_t al = addr_len < sizeof(SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[bi].bind_addr) - 1
                    ? addr_len
                    : sizeof(SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[bi].bind_addr) - 1;
    mem.cpy(SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[bi].bind_addr, addr, al);
    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[bi].bind_addr[al] = 0;
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
    SshServerV.handle = b->listener_idx;
    SshServer.rfwd_listener_close(protocore_ssh_server_span());
    b->active = PROTO_FALSE;
    return 0;
}

// The client's reply to a server-initiated forwarded-tcpip open.
static void on_forward_confirm(uint8_t ssh_slot, uint32_t channel, proto_bool ok)
{
    SshConnectionV.chan.slot = ssh_slot;
    SshConnectionV.chan.id = channel;
    SshConnection.chan_by_id(protocore_ssh_connection_span());
    SshChannel *c = SshConnectionV.found;
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
        SshNetworkV.ssh_slot = ssh_slot;
        SshNetworkV.stream.channel = channel;
        SshNetwork.chan_close(protocore_ssh_network_span()); // client refused the channel: drop the socket
        c->open = PROTO_FALSE;
    }
}

// ---------------------------------------------------------------------------
// ProtoConn::PROTO_SSH_RFWD handler: an inbound connection on a forwarded port.
// ---------------------------------------------------------------------------

void protocore_ssh_forward_set_policy_cb(uint8_t *restrict work)
{
    (void)work;
    SshForwardPolicyCb cb = SshConnectionV.forward_policy_cb;

    SSH_CONNECTION_CTX(protocore_ssh_connection_span())->policy = cb;
}

void protocore_ssh_forward_begin(uint8_t *restrict work)
{

    for (int i = 0; i < PROTOCORE_SSH_RFWD_MAX; i++)
    {
        SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].active = PROTO_FALSE;
    }
    SshConnectionV.forward_open_cb = on_forward_open;
    protocore_ssh_channel_set_forward_open_cb(work);
    SshConnectionV.forward_data_cb = on_forward_data;
    protocore_ssh_channel_set_forward_data_cb(work);
    // Remote forwarding (ssh -R): the request/cancel seam, the open-confirmation
    // callback, and the accept handler for connections on a forwarded port.
    SshConnectionV.rforward_open_cb = on_rforward_open;
    protocore_ssh_channel_set_rforward_open_cb(work);
    SshConnectionV.rforward_cancel_cb = on_rforward_cancel;
    protocore_ssh_channel_set_rforward_cancel_cb(work);
    SshConnectionV.forward_confirm_cb = on_forward_confirm;
    protocore_ssh_channel_set_forward_confirm_cb(work);
}

void protocore_ssh_forward_pump(uint8_t *restrict work)
{
    uint8_t ssh_slot = SshConnectionV.fwd.slot;

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
            SshNetworkV.ssh_slot = ssh_slot;
            SshNetworkV.stream.channel = ch;
            SshNetwork.chan_close(protocore_ssh_network_span());
            continue;
        }

        // Target -> client: forward what the peer window allows, bounded per poll.
        for (int burst = 0; burst < kFwdBurst; burst++)
        {
            SshNetworkV.ssh_slot = ssh_slot;
            SshNetworkV.stream.channel = ch;
            SshNetwork.chan_avail(protocore_ssh_network_span());
            size_t avail = SshNetworkV.n;
            if (avail > sizeof(buf))
            {
                avail = sizeof(buf);
            }
            SshConnectionV.flow.f = &c->flow;
            SshConnectionV.flow.want = (uint32_t)avail;
            protocore_ssh_flow_send_cap(work);
            uint32_t budget = SshConnectionV.u32;
            if (budget == 0)
            {
                break;
            }
            SshNetworkV.ssh_slot = ssh_slot;
            SshNetworkV.stream.channel = ch;
            SshNetworkV.read_args.out = buf;
            SshNetworkV.read_args.cap = budget;
            SshNetwork.chan_read(protocore_ssh_network_span());
            size_t n = SshNetworkV.n;
            if (n == 0)
            {
                break;
            }
            SshConnectionV.chan.slot = ssh_slot;
            SshConnectionV.chan.channel = ch;
            SshConnectionV.chan.data = buf;
            SshConnectionV.chan.len = n;
            protocore_ssh_channel_send_data(work);
            if (SshConnectionV.i32 < 0)
            {
                break; // sized to the window, so this should send; retry next poll
            }
        }

        // Target closed and fully drained: this direction sends no more, so half-close it with
        // CHANNEL_EOF, once. The channel stays open and the peer keeps sending (RFC 4254 sec 5.3).
        SshNetworkV.ssh_slot = ssh_slot;
        SshNetworkV.stream.channel = ch;
        SshNetwork.chan_drained(protocore_ssh_network_span());
        if (SshNetworkV.ok && !c->eof_sent)
        {
            SshConnectionV.chan.slot = ssh_slot;
            SshConnectionV.chan.channel = ch;
            protocore_ssh_channel_send_eof(work);
        }

        // Both directions have signaled EOF: terminate the channel and drop the socket, once.
        if (c->eof_sent && c->relay_eof && !c->close_sent)
        {
            // The socket goes now; the channel number stays taken until the peer's CLOSE answers
            // ours (sec 5.3), which build_close_chan settles.
            SshConnectionV.chan.slot = ssh_slot;
            SshConnectionV.chan.channel = ch;
            protocore_ssh_channel_send_close(work);
            SshNetworkV.ssh_slot = ssh_slot;
            SshNetworkV.stream.channel = ch;
            SshNetwork.chan_close(protocore_ssh_network_span());
        }
    }
}

void protocore_ssh_forward_reset(uint8_t *restrict work)
{
    (void)work;
    uint8_t ssh_slot = SshConnectionV.fwd.slot;

    // every socket this connection's channels bridged, forwarded or direct.
    SshNetworkV.ssh_slot = ssh_slot;
    SshNetwork.chan_close_all(protocore_ssh_network_span());
    // remote (ssh -R): stop this connection's forwarded listeners and drop every
    // accepted socket it had bridged (the SSH channels go away with the connection).
    for (int i = 0; i < PROTOCORE_SSH_RFWD_MAX; i++)
    {
        if (SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].active &&
            SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].ssh_slot == ssh_slot)
        {
            SshServerV.handle = SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].listener_idx;
            SshServer.rfwd_listener_close(protocore_ssh_server_span());
            SSH_CONNECTION_CTX(protocore_ssh_connection_span())->rbind[i].active = PROTO_FALSE;
        }
    }
}

#else

// PROTOCORE_SSH_PORT_FORWARD is 0: the handle still carries the forward calls, so they exist and
// report nothing bound.
void protocore_ssh_forward_binding(uint8_t *restrict work)
{
    (void)work;
    SshConnectionV.i32 = -1;
}
void protocore_ssh_forward_set_policy_cb(uint8_t *restrict work)
{
    (void)work;
}
void protocore_ssh_forward_begin(uint8_t *restrict work)
{
    (void)work;
    SshConnectionV.ok = PROTO_FALSE;
}
void protocore_ssh_forward_pump(uint8_t *restrict work)
{
    (void)work;
}
void protocore_ssh_forward_reset(uint8_t *restrict work)
{
    (void)work;
}
#endif // PROTOCORE_SSH_PORT_FORWARD

// ---------------------------------------------------------------------------
// RFC 4254 - message numbers 80 to 127, reached once authentication has passed
// ---------------------------------------------------------------------------

void ssh_connection_dispatch(uint8_t *restrict work)
{
    uint8_t i = SshConnectionV.chan.slot;
    uint8_t msg_type = SshConnectionV.msg_type;
    const uint8_t *payload = SshConnectionV.chan.payload;
    size_t len = SshConnectionV.chan.len;

    if (i >= MAX_SSH_CONNS)
    {
        SshConnectionV.i32 = -1;
        return;
    }
    SshSession *s = &ssh_sess[i];

    // The reply buffer is borrowed for this dispatch, not carried on the worker stack: it is the
    // single largest frame on the SSH path and the handshake below it is the deepest call chain in
    // the library. protocore_plaintext_span binds the capacity to the allocation.
    size_t mark = protocore_plaintext_mark();
    protocore_span reply = protocore_plaintext_span(SSH_PKT_BUF_SIZE, 16);
    if (!span.ok(reply))
    {
        protocore_plaintext_release(mark);
        SshConnectionV.i32 = -1;
        return; // arena exhausted: fail closed, the caller drops the connection
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
            SshConnectionV.i32 = -1;
            return;
        }
        SshConnectionV.chan.slot = i;
        SshConnectionV.chan.payload = payload;
        SshConnectionV.chan.len = len;
        SshConnectionV.chan.out = reply.buf;
        SshConnectionV.chan.cap = reply.cap;
        ssh_global_request_handle(work);
        n = SshConnectionV.chan.out_len;
        if (SshConnectionV.i32 != 0)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        if (n > 0)
        {
            SshNetworkV.ssh_slot = i;
            SshNetworkV.msg.payload = reply.buf;
            SshNetworkV.msg.len = n;
            SshNetwork.emit(protocore_ssh_network_span());
        }
        protocore_plaintext_release(mark);
        SshConnectionV.i32 = 0;
        return;

    case SSH_MSG_CHANNEL_OPEN:
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        SshConnectionV.chan.slot = i;
        SshConnectionV.chan.payload = payload;
        SshConnectionV.chan.len = len;
        SshConnectionV.chan.out = reply.buf;
        SshConnectionV.chan.cap = reply.cap;
        protocore_ssh_channel_handle_open(work);
        n = SshConnectionV.chan.out_len;
        if (SshConnectionV.i32 != 0)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        SshNetworkV.ssh_slot = i;
        SshNetworkV.msg.payload = reply.buf;
        SshNetworkV.msg.len = n;
        SshNetwork.emit(protocore_ssh_network_span());
        protocore_plaintext_release(mark);
        SshConnectionV.i32 = 0;
        return;

    case SSH_MSG_CHANNEL_OPEN_CONFIRMATION:
        // The client's reply to a server-initiated forwarded-tcpip open (ssh -R): record
        // the peer window and start the bridge. A stray confirm is ignored (not fatal).
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        SshConnectionV.chan.slot = i;
        SshConnectionV.chan.payload = payload;
        SshConnectionV.chan.len = len;
        protocore_ssh_channel_handle_open_confirm(work);
        protocore_plaintext_release(mark);
        SshConnectionV.i32 = 0;
        return;

    case SSH_MSG_CHANNEL_OPEN_FAILURE:
        // The client refused a server-initiated forwarded-tcpip open: tear the bridge down.
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        SshConnectionV.chan.slot = i;
        SshConnectionV.chan.payload = payload;
        SshConnectionV.chan.len = len;
        protocore_ssh_channel_handle_open_failure(work);
        protocore_plaintext_release(mark);
        SshConnectionV.i32 = 0;
        return;

    case SSH_MSG_CHANNEL_REQUEST:
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        SshConnectionV.chan.slot = i;
        SshConnectionV.chan.payload = payload;
        SshConnectionV.chan.len = len;
        SshConnectionV.chan.out = reply.buf;
        SshConnectionV.chan.cap = reply.cap;
        protocore_ssh_channel_handle_request(work);
        n = SshConnectionV.chan.out_len;
        if (SshConnectionV.i32 != 0)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        SshNetworkV.ssh_slot = i;
        SshNetworkV.msg.payload = reply.buf;
        SshNetworkV.msg.len = n;
        SshNetwork.emit(protocore_ssh_network_span()); // SUCCESS/FAILURE only when want_reply was set
        protocore_plaintext_release(mark);
        SshConnectionV.i32 = 0;
        return;

    case SSH_MSG_CHANNEL_DATA:
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        SshConnectionV.chan.slot = i;
        SshConnectionV.chan.payload = payload;
        SshConnectionV.chan.len = len;
        SshConnectionV.chan.out = reply.buf;
        SshConnectionV.chan.cap = reply.cap;
        protocore_ssh_channel_handle_data(work);
        n = SshConnectionV.chan.out_len;
        if (SshConnectionV.i32 != 0)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        SshNetworkV.ssh_slot = i;
        SshNetworkV.msg.payload = reply.buf;
        SshNetworkV.msg.len = n;
        SshNetwork.emit(protocore_ssh_network_span()); // WINDOW_ADJUST when the receive window is replenished
        protocore_plaintext_release(mark);
        SshConnectionV.i32 = 0;
        return;

    case SSH_MSG_CHANNEL_EXTENDED_DATA:
        // RFC 4254 sec 5.2: either side may send it, so it is accounted against the window rather
        // than answered with UNIMPLEMENTED.
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        SshConnectionV.chan.slot = i;
        SshConnectionV.chan.payload = payload;
        SshConnectionV.chan.len = len;
        SshConnectionV.chan.out = reply.buf;
        SshConnectionV.chan.cap = reply.cap;
        protocore_ssh_channel_handle_extended_data(work);
        n = SshConnectionV.chan.out_len;
        if (SshConnectionV.i32 != 0)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        SshNetworkV.ssh_slot = i;
        SshNetworkV.msg.payload = reply.buf;
        SshNetworkV.msg.len = n;
        SshNetwork.emit(protocore_ssh_network_span()); // WINDOW_ADJUST when the receive window is replenished
        protocore_plaintext_release(mark);
        SshConnectionV.i32 = 0;
        return;

    case SSH_MSG_CHANNEL_WINDOW_ADJUST:
        // RFC 4254 sec 5: the connection protocol runs on top of userauth, so every channel message
        // is refused before authentication, these two included.
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        SshConnectionV.chan.slot = i;
        SshConnectionV.chan.payload = payload;
        SshConnectionV.chan.len = len;
        protocore_ssh_channel_handle_window_adjust(work);
        protocore_plaintext_release(mark);
        SshConnectionV.i32 = 0;
        return;

    case SSH_MSG_CHANNEL_EOF:
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        SshConnectionV.chan.slot = i;
        SshConnectionV.chan.payload = payload;
        SshConnectionV.chan.len = len;
        protocore_ssh_channel_handle_eof(work);
        protocore_plaintext_release(mark);
        SshConnectionV.i32 = 0;
        return;

    case SSH_MSG_CHANNEL_CLOSE:
        // RFC 4254 sec 5.3: answer an inbound CLOSE with CHANNEL_CLOSE, in its own binary packet.
        if (!s->authed)
        {
            protocore_plaintext_release(mark);
            SshConnectionV.i32 = -1;
            return;
        }
        SshConnectionV.chan.slot = i;
        SshConnectionV.chan.payload = payload;
        SshConnectionV.chan.len = len;
        SshConnectionV.chan.out = reply.buf;
        SshConnectionV.chan.cap = reply.cap;
        protocore_ssh_channel_handle_close(work);
        n = SshConnectionV.chan.out_len;
        if (SshConnectionV.i32 == 0 && n == 5)
        {
            SshNetworkV.ssh_slot = i;
            SshNetworkV.msg.payload = reply.buf;
            SshNetworkV.msg.len = 5;
            SshNetwork.emit(protocore_ssh_network_span());
        }
        protocore_plaintext_release(mark);
        SshConnectionV.i32 = 0;
        return;

    default: {
        // RFC 4253 sec 11.4: an unrecognized message is answered with SSH_MSG_UNIMPLEMENTED, which
        // the transport builds - message 3 and the receive counter are both its.
        size_t un = 0;
        if (ssh_pkt_unimplemented(i, reply.buf, &un, sizeof(reply.buf)) == 0)
        {
            SshNetworkV.ssh_slot = i;
            SshNetworkV.msg.payload = reply.buf;
            SshNetworkV.msg.len = un;
            SshNetwork.emit(protocore_ssh_network_span());
        }
        protocore_plaintext_release(mark);
        SshConnectionV.i32 = 0;
        return;
    }
    }
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
SshConnectionVars SshConnectionV;
