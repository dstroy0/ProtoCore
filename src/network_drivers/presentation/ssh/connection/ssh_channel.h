// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_channel.h
 * @brief SSH connection protocol - multiplexed "session" channels (RFC 4254).
 *
 * After authentication the client opens one or more channels; this layer confirms
 * each, accepts a shell/exec/pty request, and exchanges SSH_MSG_CHANNEL_DATA.
 * Inbound channel data is surfaced to the application as a raw byte stream (no PTY
 * emulation), tagged with its channel id; outbound data is framed back to the
 * client on a given channel. Flow control follows RFC 4254 §5.2 (per-channel
 * window tracking + WINDOW_ADJUST).
 *
 * Up to PC_SSH_MAX_CHANNELS channels per connection are multiplexed, each with
 * its own id, peer id, and windows. The default (1) is the original single-channel
 * control/console link. Every inbound message is routed to its channel by the
 * recipient channel id it carries.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_CHANNEL_H
#define PROTOCORE_SSH_CHANNEL_H

#include "network_drivers/presentation/ssh/connection/ssh_flow_control.h"
#include "protocore_config.h"

PROTO_BEGIN_DECLS

/** @brief Channel type (RFC 4254). */
typedef enum PROTO_ENUM_PACKED
{
    SSH_CHAN_SESSION = 0,         ///< "session" - shell / exec / data
    SSH_CHAN_DIRECT_TCPIP = 1,    ///< "direct-tcpip" - client-initiated TCP forward (ssh -L)
    SSH_CHAN_FORWARDED_TCPIP = 2, ///< "forwarded-tcpip" - server-initiated TCP forward (ssh -R)
    SSH_CHAN_SFTP = 3,            ///< a "session" running the "sftp" subsystem (PC_ENABLE_SSH_SFTP)
    SSH_CHAN_SCP = 4              ///< a "session" running an `exec "scp …"` (PC_ENABLE_SSH_SCP)
} SshChanType;

/** @brief Per-connection channel state. */
typedef struct
{
    proto_bool open;    ///< True once the channel is confirmed open both ways.
    proto_bool pending; ///< True for a server-initiated channel we opened, awaiting the client's confirmation.
    SshChanType type;   ///< session, direct-tcpip, or forwarded-tcpip.
    uint32_t local_id;  ///< Our channel id (== slot index).
    uint32_t peer_id;   ///< Client's channel id.
    SshFlow flow;       ///< RFC 4254 sec 5.2 window pair (owner: ssh_flow_control.*).
} SshChannel;

/** @brief Channel pool: PC_SSH_MAX_CHANNELS channels per SSH connection (BSS).
 *  Owned by this layer; src/ code routes through the functions below, never the
 *  array (tests inspect it white-box). Index: [connection slot][channel slot]. */
extern SshChannel ssh_chan[MAX_SSH_CONNS][PC_SSH_MAX_CHANNELS];

/** @brief Application callback for inbound channel data (raw bytes), tagged with
 *  the channel id it arrived on. */
typedef void (*SshChannelDataCb)(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len);
/**
 * @brief The channel layer (RFC 4254): the callbacks each channel kind is delivered through, and the
 * arms that turn one peer message. SshChannel is one channel; this drives them.
 *
 * @var SshChannelNs::set_data_cb             Install the inbound-data callback (session channels)
 * @var SshChannelNs::set_forward_open_cb     Install the direct-tcpip forward open-policy callback (opt-in)
 * @var SshChannelNs::set_forward_data_cb     Install the direct-tcpip forward inbound-data callback
 * @var SshChannelNs::set_rforward_open_cb    Install the remote-forward (ssh -R) open-policy callback (opt-in)
 * @var SshChannelNs::set_rforward_cancel_cb  Install the remote-forward (ssh -R) cancel callback (opt-in)
 * @var SshChannelNs::set_forward_confirm_cb  Install the forwarded-tcpip open-confirmation callback (opt-in,
 *                                            ssh -R)
 * @var SshChannelNs::set_sftp_open_cb        (see the implementation)
 * @var SshChannelNs::set_sftp_data_cb        (see the implementation)
 * @var SshChannelNs::set_scp_open_cb         (see the implementation)
 * @var SshChannelNs::set_scp_data_cb         (see the implementation)
 * @var SshChannelNs::init                    Reset channel state for slot @p i
 * @var SshChannelNs::open_forwarded          Open a server-initiated "forwarded-tcpip" channel (RFC 4254 §7.2,
 *                                            ssh -R)
 * @var SshChannelNs::handle_open             Handle SSH_MSG_CHANNEL_OPEN and emit CHANNEL_OPEN_CONFIRMATION
 * @var SshChannelNs::handle_open_confirm     Handle SSH_MSG_CHANNEL_OPEN_CONFIRMATION for a channel we opened
 *                                            (ssh -R)
 * @var SshChannelNs::handle_open_failure     Handle SSH_MSG_CHANNEL_OPEN_FAILURE for a channel we opened (ssh
 *                                            -R)
 * @var SshChannelNs::handle_request          Handle SSH_MSG_CHANNEL_REQUEST
 * @var SshChannelNs::handle_data             Handle SSH_MSG_CHANNEL_DATA: bounds-check, update the window, and
 *                                            invoke the data callback. If the local window is exhausted a
 *                                            CHANNEL_WINDOW_ADJUST is written to @p out (*@p out_len > 0)
 * @var SshChannelNs::build_data              Build an SSH_MSG_CHANNEL_DATA message carrying @p data to the
 *                                            client on channel @p channel (a local channel id from a prior
 *                                            open)
 * @var SshChannelNs::handle_window_adjust    Handle SSH_MSG_CHANNEL_WINDOW_ADJUST (grows the peer window)
 * @var SshChannelNs::build_close             Build SSH_MSG_CHANNEL_EOF + SSH_MSG_CHANNEL_CLOSE for channel @p
 *                                            channel and mark it closed
 * @var SshChannelNs::handle_close            Handle an inbound SSH_MSG_CHANNEL_CLOSE: route to the recipient
 *                                            channel, reply with EOF + CLOSE, and mark it closed
 * @var SshChannelNs::global_request          Handle SSH_MSG_GLOBAL_REQUEST (RFC 4254 §4)
 */
typedef struct
{
    void (*set_data_cb)(SshChannelDataCb cb);
    void (*set_forward_open_cb)(SshForwardOpenCb cb);
    void (*set_forward_data_cb)(SshForwardDataCb cb);
    void (*set_rforward_open_cb)(SshRemoteForwardOpenCb cb);
    void (*set_rforward_cancel_cb)(SshRemoteForwardCancelCb cb);
    void (*set_forward_confirm_cb)(SshForwardConfirmCb cb);
    void (*set_sftp_open_cb)(SshSftpOpenCb cb);
    void (*set_sftp_data_cb)(SshSftpDataCb cb);
    void (*set_scp_open_cb)(SshScpOpenCb cb);
    void (*set_scp_data_cb)(SshScpDataCb cb);
    void (*init)(uint8_t i);
    int (*open_forwarded)(uint8_t i, const char *conn_addr, uint16_t conn_port, const char *orig_addr,
                          uint16_t orig_port, uint8_t *out, size_t *out_len, size_t cap);
    int (*handle_open)(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap);
    int (*handle_open_confirm)(uint8_t i, const uint8_t *payload, size_t len);
    int (*handle_open_failure)(uint8_t i, const uint8_t *payload, size_t len);
    int (*handle_request)(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap);
    int (*handle_data)(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap);
    int (*build_data)(uint8_t i, uint32_t channel, const uint8_t *data, size_t len, uint8_t *out, size_t *out_len,
                      size_t cap);
    int (*handle_window_adjust)(uint8_t i, const uint8_t *payload, size_t len);
    int (*build_close)(uint8_t i, uint32_t channel, uint8_t *out, size_t *out_len, size_t cap);
    int (*handle_close)(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap);
    int (*global_request)(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap);
} SshChannelNs;

/** @brief The one symbol this module exports. */
extern const SshChannelNs SshChannels;

/**
 * @brief "direct-tcpip" forward request: a client asked the server to open a TCP
 *        connection to @p host : @p port (ssh -L). The forwarding owner (which
 *        does the actual TCP I/O - this codec does not) decides whether to allow
 *        it; @p host is not NUL-terminated (@p host_len bytes).
 * @return 0 to accept (the channel is opened and confirmed), < 0 to refuse
 *         (CHANNEL_OPEN_FAILURE, administratively prohibited / connect failed).
 *
 * If no callback is installed, all forward requests are refused - so forwarding is
 * opt-in (no open relay by default).
 */
typedef int (*SshForwardOpenCb)(uint8_t slot, uint32_t channel, const char *host, size_t host_len, uint16_t port);
/** @brief Inbound data on a direct-tcpip channel (the owner writes it to the
 *  forwarded TCP socket). Kept separate from the session data callback. */
typedef void (*SshForwardDataCb)(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len);

/**
 * @brief "tcpip-forward" remote-forward request (ssh -R): the client asks the server
 *        to listen on @p bind_addr : @p bind_port and open a channel back for each
 *        accepted connection (RFC 4254 §7.1). @p bind_addr is @p addr_len bytes (not
 *        NUL-terminated). The forwarding owner (which allocates the real listener -
 *        this codec does no I/O) decides.
 * @return the bound port on success (echo @p bind_port, or the port the owner picked
 *         when @p bind_port == 0), or < 0 to refuse. If no callback is installed every
 *         request is refused, so remote forwarding is opt-in (no listener is opened).
 */
typedef int (*SshRemoteForwardOpenCb)(uint8_t slot, const char *bind_addr, size_t addr_len, uint16_t bind_port);
/** @brief "cancel-tcpip-forward" request (RFC 4254 §7.1): drop a remote forward.
 *  @return 0 if a matching forward was cancelled, < 0 if none / unsupported. */
typedef int (*SshRemoteForwardCancelCb)(uint8_t slot, const char *bind_addr, size_t addr_len, uint16_t bind_port);

/**
 * @brief Result of the client's reply to a server-initiated forwarded-tcpip channel:
 *        @p ok = true on CHANNEL_OPEN_CONFIRMATION (the bridge may start), false on
 *        CHANNEL_OPEN_FAILURE (the owner tears the bridge down). @p channel is the
 *        local id returned by SshChannels.open_forwarded().
 */
typedef void (*SshForwardConfirmCb)(uint8_t slot, uint32_t channel, proto_bool ok);

#if PC_ENABLE_SSH_SFTP
/** @brief A `subsystem "sftp"` request was accepted on @p channel; the binding starts an SFTP session. */
typedef void (*SshSftpOpenCb)(uint8_t slot, uint32_t channel);
/** @brief Inbound bytes on an SFTP channel (the raw SSH_FXP_* stream) - kept out of the session data cb. */
typedef void (*SshSftpDataCb)(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len);
#endif

#if PC_ENABLE_SSH_SCP
/** @brief An `exec "scp …"` request was accepted on @p channel (@p cmd is @p cmd_len bytes, not NUL-terminated). */
typedef void (*SshScpOpenCb)(uint8_t slot, uint32_t channel, const char *cmd, size_t cmd_len);
/** @brief Inbound bytes on an SCP channel (the RCP protocol stream). */
typedef void (*SshScpDataCb)(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len);
#endif

PROTO_END_DECLS

#endif // PROTOCORE_SSH_CHANNEL_H
