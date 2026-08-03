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
/** @brief Install the inbound-data callback (session channels). */
void pc_ssh_channel_set_data_cb(SshChannelDataCb cb);

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
/** @brief Install the direct-tcpip forward open-policy callback (opt-in). */
void pc_ssh_channel_set_forward_open_cb(SshForwardOpenCb cb);

/** @brief Install the direct-tcpip forward inbound-data callback. */
void pc_ssh_channel_set_forward_data_cb(SshForwardDataCb cb);

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
/** @brief Install the remote-forward (ssh -R) open-policy callback (opt-in). */
void pc_ssh_channel_set_rforward_open_cb(SshRemoteForwardOpenCb cb);

/** @brief Install the remote-forward (ssh -R) cancel callback (opt-in). */
void pc_ssh_channel_set_rforward_cancel_cb(SshRemoteForwardCancelCb cb);

/**
 * @brief Result of the client's reply to a server-initiated forwarded-tcpip channel:
 *        @p ok = true on CHANNEL_OPEN_CONFIRMATION (the bridge may start), false on
 *        CHANNEL_OPEN_FAILURE (the owner tears the bridge down). @p channel is the
 *        local id returned by pc_ssh_channel_open_forwarded().
 */
typedef void (*SshForwardConfirmCb)(uint8_t slot, uint32_t channel, proto_bool ok);
/** @brief Install the forwarded-tcpip open-confirmation callback (opt-in, ssh -R). */
void pc_ssh_channel_set_forward_confirm_cb(SshForwardConfirmCb cb);

#if PC_ENABLE_SSH_SFTP
/** @brief A `subsystem "sftp"` request was accepted on @p channel; the binding starts an SFTP session. */
typedef void (*SshSftpOpenCb)(uint8_t slot, uint32_t channel);
/** @brief Inbound bytes on an SFTP channel (the raw SSH_FXP_* stream) - kept out of the session data cb. */
typedef void (*SshSftpDataCb)(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len);
void pc_ssh_channel_set_sftp_open_cb(SshSftpOpenCb cb);
void pc_ssh_channel_set_sftp_data_cb(SshSftpDataCb cb);
#endif

#if PC_ENABLE_SSH_SCP
/** @brief An `exec "scp …"` request was accepted on @p channel (@p cmd is @p cmd_len bytes, not NUL-terminated). */
typedef void (*SshScpOpenCb)(uint8_t slot, uint32_t channel, const char *cmd, size_t cmd_len);
/** @brief Inbound bytes on an SCP channel (the RCP protocol stream). */
typedef void (*SshScpDataCb)(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len);
void pc_ssh_channel_set_scp_open_cb(SshScpOpenCb cb);
void pc_ssh_channel_set_scp_data_cb(SshScpDataCb cb);
#endif

/**
 * @brief Open a server-initiated "forwarded-tcpip" channel (RFC 4254 §7.2, ssh -R).
 *
 * Allocates a local channel on connection @p i (state = pending, awaiting the
 * client's confirmation) and builds the SSH_MSG_CHANNEL_OPEN in @p out.
 * @p conn_addr / @p conn_port are the forward's bound address/port (the "address
 * that was connected"); @p orig_addr / @p orig_port are the peer that connected.
 *
 * @return the local channel id (>= 0) on success, or -1 (pool full, or @p out too
 *         small). On success the caller emits @p out and, on the eventual confirm,
 *         bridges bytes on the returned channel.
 */
int pc_ssh_channel_open_forwarded(uint8_t i, const char *conn_addr, uint16_t conn_port, const char *orig_addr,
                                  uint16_t orig_port, uint8_t *out, size_t *out_len, size_t cap);

/**
 * @brief Handle SSH_MSG_CHANNEL_OPEN_CONFIRMATION for a channel we opened (ssh -R).
 *
 * Matches the pending channel by our recipient id, records the peer's channel id /
 * window / max-packet, and marks it open. Fires the confirm callback (@p ok = true).
 * @return 0 on success, -1 if malformed or no matching pending channel.
 */
int pc_ssh_channel_handle_open_confirm(uint8_t i, const uint8_t *payload, size_t len);

/**
 * @brief Handle SSH_MSG_CHANNEL_OPEN_FAILURE for a channel we opened (ssh -R).
 *
 * Frees the pending channel and fires the confirm callback (@p ok = false).
 * @return 0 on success, -1 if malformed or no matching pending channel.
 */
int pc_ssh_channel_handle_open_failure(uint8_t i, const uint8_t *payload, size_t len);

/**
 * @brief Handle SSH_MSG_GLOBAL_REQUEST (RFC 4254 §4).
 *
 * Parses the request name and want_reply flag. "tcpip-forward" /
 * "cancel-tcpip-forward" are routed to the remote-forward seam above (accepted only
 * when a callback is installed); a "tcpip-forward" that bound port 0 gets its
 * allocated port echoed in the reply (RFC 4254 §7.1). Any other request name is
 * unrecognized: per §4 it is answered with SSH_MSG_REQUEST_FAILURE when want_reply is
 * set, and silently ignored otherwise (never SSH_MSG_UNIMPLEMENTED - GLOBAL_REQUEST is
 * a known message type; only the request name is unknown).
 *
 * @return 0 on success (a reply is in @p out with *@p out_len bytes, or *@p out_len is
 *         0 when no reply is due), -1 if the message is malformed.
 */
int ssh_global_request_handle(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap);

/** @brief Reset channel state for slot @p i. */
void pc_ssh_channel_init(uint8_t i);

/**
 * @brief Handle SSH_MSG_CHANNEL_OPEN and emit CHANNEL_OPEN_CONFIRMATION.
 *
 * Accepts a "session" channel; any other type yields CHANNEL_OPEN_FAILURE.
 * @return 0 if a response was produced, -1 if malformed.
 */
int pc_ssh_channel_handle_open(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                               size_t cap);

/**
 * @brief Handle SSH_MSG_CHANNEL_REQUEST.
 *
 * "shell", "exec", "pty-req", and "env" are accepted; anything else is refused - except that when
 * PC_ENABLE_SSH_SFTP is set a `subsystem "sftp"` is accepted (the channel is tagged SSH_CHAN_SFTP and the
 * sftp-open callback fires), and when PC_ENABLE_SSH_SCP is set an `exec "scp …"` is additionally tagged
 * SSH_CHAN_SCP (the scp-open callback fires with the command). When want_reply is set, CHANNEL_SUCCESS /
 * CHANNEL_FAILURE is written to @p out and *@p out_len > 0; otherwise *@p out_len is 0.
 * @return 0 on success, -1 if malformed.
 */
int pc_ssh_channel_handle_request(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                                  size_t cap);

/**
 * @brief Handle SSH_MSG_CHANNEL_DATA: bounds-check, update the window, and
 *        invoke the data callback. If the local window is exhausted a
 *        CHANNEL_WINDOW_ADJUST is written to @p out (*@p out_len > 0).
 * @return 0 on success, -1 if malformed or channel not open.
 */
int pc_ssh_channel_handle_data(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                               size_t cap);

/**
 * @brief Build an SSH_MSG_CHANNEL_DATA message carrying @p data to the client on
 *        channel @p channel (a local channel id from a prior open).
 * @return 0 on success, -1 if the channel is closed/unknown, the peer window is
 *         too small, or @p out is too small.
 */
int pc_ssh_channel_build_data(uint8_t i, uint32_t channel, const uint8_t *data, size_t len, uint8_t *out,
                              size_t *out_len, size_t cap);

/**
 * @brief Handle SSH_MSG_CHANNEL_WINDOW_ADJUST (grows the peer window).
 * @return 0 on success, -1 if malformed.
 */
int pc_ssh_channel_handle_window_adjust(uint8_t i, const uint8_t *payload, size_t len);

/**
 * @brief Build SSH_MSG_CHANNEL_EOF + SSH_MSG_CHANNEL_CLOSE for channel @p channel
 *        and mark it closed.
 * @return 0 on success, -1 if the channel is closed/unknown or @p out is too small.
 */
int pc_ssh_channel_build_close(uint8_t i, uint32_t channel, uint8_t *out, size_t *out_len, size_t cap);

/**
 * @brief Handle an inbound SSH_MSG_CHANNEL_CLOSE: route to the recipient channel,
 *        reply with EOF + CLOSE, and mark it closed.
 * @return 0 if a response was produced, -1 if malformed or the channel is unknown.
 */
int pc_ssh_channel_handle_close(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                                size_t cap);

#endif // PROTOCORE_SSH_CHANNEL_H
