// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file connection.h
 * @brief RFC 4254 connection protocol: channel multiplexing, windows, and port forwarding.
 */

#ifndef PROTOCORE_CONNECTION_CONNECTION_H
#define PROTOCORE_CONNECTION_CONNECTION_H

#include "network_drivers/presentation/ssh/common.h"

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// RFC 4254 sec 5.2 - the window pair
// ---------------------------------------------------------------------------

/** @brief One channel's flow-control state (RFC 4254 sec 5.2). */
typedef struct
{
    uint32_t local_window; ///< Bytes the peer may still send us before we WINDOW_ADJUST.
    uint32_t local_max;    ///< The window we advertised, and replenish back up to.
    uint32_t peer_window;  ///< Bytes we may still send the peer.
    uint32_t peer_max_pkt; ///< Peer's maximum packet size; caps a single send independently of the window.
} SshFlow;

/**
 * @brief Start a channel's windows: ours at @p local_window, the peer's at what it advertised.
 *
 * The local window is a parameter rather than a baked-in constant because the server and the
 * client advertise different sizes, and the value we replenish to must be the value we told the
 * peer about. Passing it here keeps the two from drifting apart.
 *
 * @param local_window  what we advertise in CHANNEL_OPEN / CONFIRMATION; also the replenish target.
 * @param peer_window   the peer's initial window from CHANNEL_OPEN / CONFIRMATION.
 * @param peer_max_pkt  the peer's maximum packet size from the same message.
 */

/**
 * @brief Account @p n inbound bytes against our window.
 *
 * @return false if @p n exceeds what we advertised - the peer overran the window (RFC 4254 sec 5.2)
 *         and the caller must fail the channel. The window is left untouched on failure.
 */

/**
 * @brief Decide whether a WINDOW_ADJUST is due, and for how much. Does not mutate.
 *
 * Replenishes once the window has drained past half, which keeps a bulk transfer from stalling
 * without emitting an adjust per packet.
 *
 * Pair it with protocore_ssh_flow_local_credit() only after the adjust has actually gone out. Deciding and
 * crediting are separate because on some paths the send can fail, and crediting first would leave us
 * believing we advertised bytes the peer never heard about - the peer then stops at its smaller
 * window while we wait for data, and the transfer deadlocks.
 *
 * @return true if a WINDOW_ADJUST is due; @p *add receives the delta to advertise.
 */

/** @brief Credit our window by @p add, once that WINDOW_ADJUST has actually been sent. */

/** @brief True if @p len bytes fit both the peer's remaining window and its maximum packet size. */

/**
 * @brief Clamp a would-be send to what the peer currently permits.
 *
 * A producer that pulls from a local source sizes its read to this, so it never reads bytes it
 * cannot legally forward. Returns 0 when the window is closed, which the caller treats as
 * "stop pumping until a WINDOW_ADJUST arrives".
 *
 * @return min(@p want, peer window, peer maximum packet size).
 */

/** @brief Account @p n outbound bytes against the peer's window (call only after send_allows()). */

/**
 * @brief Credit the peer's window from an inbound WINDOW_ADJUST.
 *
 * Saturates at UINT32_MAX rather than wrapping: a peer advertising a total past 2^32 is out of spec,
 * and wrapping would hand us a tiny window and stall the transfer.
 */

// ---------------------------------------------------------------------------
// Channel signaling (RFC 4254 sec 5)
//
// Every channel-related message is a transition on the window state above: OPEN / OPEN_CONFIRMATION
// establish it (they carry the initial window and maximum packet size), WINDOW_ADJUST increments it,
// DATA consumes it, EOF / CLOSE terminate it. Because the transitions and the state are the same
// concern, they live together - the RFC's rule that no data may be sent until the window allows it is
// only enforceable where the window is.
//
// These take the flow plus the ids the wire carries, never a channel struct: resolving a recipient
// channel number to a channel is multiplexing, and that stays in ssh_channel.
// ---------------------------------------------------------------------------

// The connection protocol's message numbers (RFC 4250 §4.1.1: 80 to 89 generic, 90 to 127 channel
// related). They sit with the builders below, which are what write them onto the wire.

/** @brief CHANNEL_OPEN_FAILURE. @p reason: 1 admin-prohibited, 2 connect-failed, 3 unknown-type, 4 resource. */
int32_t protocore_ssh_sig_build_open_failure(uint8_t *out, size_t cap, uint32_t peer_id, uint32_t reason,
                                             size_t *out_len);

/** @brief CHANNEL_OPEN_CONFIRMATION, advertising our current window and maximum packet size. */
int32_t protocore_ssh_sig_build_open_confirm(const SshFlow *f, uint32_t peer_id, uint32_t local_id, uint8_t *out,
                                             size_t cap, size_t *out_len);

/**
 * @brief CHANNEL_DATA carrying @p len bytes, and account them against the peer's window.
 *
 * Refuses when the send would exceed the peer's window or its maximum packet size, so the RFC 4254
 * sec 5.2 limit cannot be violated by any caller - the check and the debit are one step here.
 */
int32_t protocore_ssh_sig_build_data(SshFlow *f, uint32_t peer_id, const uint8_t *data, size_t len, uint8_t *out,
                                     size_t cap, size_t *out_len);

/** @brief CHANNEL_WINDOW_ADJUST granting @p add more bytes. Credit the window only once this is sent. */
int32_t protocore_ssh_sig_build_window_adjust(uint32_t peer_id, uint32_t add, uint8_t *out, size_t cap,
                                              size_t *out_len);

/** @brief CHANNEL_EOF, as one 5-byte message. */
int32_t protocore_ssh_sig_build_eof(uint32_t peer_id, uint8_t *out, size_t cap, size_t *out_len);

/** @brief CHANNEL_CLOSE, as one 5-byte message. */
int32_t protocore_ssh_sig_build_close(uint32_t peer_id, uint8_t *out, size_t cap, size_t *out_len);

// ---------------------------------------------------------------------------
// RFC 4254 sec 5 - channel multiplexing
// ---------------------------------------------------------------------------

/** @brief Channel type (RFC 4254). */
typedef enum PROTO_ENUM_PACKED
{
    SSH_CHAN_SESSION = 0,        ///< "session" - shell / exec / data
    SSH_CHAN_DIRECT_TCPIP = 1,   ///< "direct-tcpip" - client-initiated TCP forward (ssh -L)
    SSH_CHAN_FORWARDED_TCPIP = 2 ///< "forwarded-tcpip" - server-initiated TCP forward (ssh -R)
} SshChanType;

/**
 * @brief What a session channel's sec 6 request bound to it, if anything.
 *
 * These are not channel types: sec 5.1 names the type on the wire, and a file-transfer service is
 * a sec 6.5 "subsystem" or `exec` request arriving later on a channel already open as "session".
 * The type says how the channel was opened; this says what its data means.
 */
typedef enum PROTO_ENUM_PACKED
{
    SSH_CHAN_SERVICE_NONE = 0, ///< shell / exec output, handed to the channel-data callback
    SSH_CHAN_SERVICE_SFTP = 1, ///< subsystem "sftp" (PROTOCORE_ENABLE_SSH_SFTP)
    SSH_CHAN_SERVICE_SCP = 2   ///< exec "scp ..." (PROTOCORE_ENABLE_SSH_SCP)
} SshChanService;

/** @brief Per-connection channel state. */
typedef struct
{
    proto_bool open;        ///< True once the channel is confirmed open both ways.
    proto_bool pending;     ///< True for a server-initiated channel we opened, awaiting the client's confirmation.
    SshChanType type;       ///< session, direct-tcpip, or forwarded-tcpip.
    SshChanService service; ///< What a sec 6 request bound over a session channel, if anything.
    uint32_t local_id;      ///< Our channel id (== slot index).
    uint32_t peer_id;       ///< Client's channel id.
    SshFlow flow;           ///< RFC 4254 sec 5.2 window pair (owner: ssh_flow_control.*).
    proto_bool eof_sent;    ///< We sent CHANNEL_EOF (RFC 4254 sec 5.3).
    proto_bool relay_eof;   ///< The peer sent CHANNEL_EOF.
    // sec 5.3: "The channel is considered closed for a party when it has both sent and received
    // SSH_MSG_CHANNEL_CLOSE, and the party may then reuse the channel number." One latch each, so
    // the number is not handed out again while the peer's CLOSE is still in flight.
    proto_bool close_sent;     ///< We sent CHANNEL_CLOSE.
    proto_bool close_received; ///< The peer sent CHANNEL_CLOSE.
    // sec 6.2: a terminal allocated for this session, and the dimensions sec 6.7 updates. The
    // dimensions are "only informational", so they are carried, not acted on, by this layer.
    proto_bool pty;       ///< A "pty-req" was accepted on this channel.
    uint32_t width_chars; ///< terminal width, characters
    uint32_t height_rows; ///< terminal height, rows
    uint32_t width_px;    ///< terminal width, pixels
    uint32_t height_px;   ///< terminal height, pixels
} SshChannel;

/** @brief Channel pool: PROTOCORE_SSH_MAX_CHANNELS channels per SSH connection (BSS).
 *  Owned by this layer; src/ code routes through the functions below, never the
 *  array (tests inspect it white-box). Index: [connection slot][channel slot]. */
extern SshChannel ssh_chan[MAX_SSH_CONNS][PROTOCORE_SSH_MAX_CHANNELS];

/** @brief Application callback for inbound channel data (raw bytes), tagged with
 *  the channel id it arrived on. */
typedef void (*SshChannelDataCb)(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len);
/** @brief Install the inbound-data callback (session channels). */

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

/** @brief Install the direct-tcpip forward inbound-data callback. */

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

/** @brief Install the remote-forward (ssh -R) cancel callback (opt-in). */

/**
 * @brief Result of the client's reply to a server-initiated forwarded-tcpip channel:
 *        @p ok = true on CHANNEL_OPEN_CONFIRMATION (the bridge may start), false on
 *        CHANNEL_OPEN_FAILURE (the owner tears the bridge down). @p channel is the
 *        local id returned by protocore_ssh_channel_open_forwarded().
 */
typedef void (*SshForwardConfirmCb)(uint8_t slot, uint32_t channel, proto_bool ok);
/** @brief Install the forwarded-tcpip open-confirmation callback (opt-in, ssh -R). */

/** @brief A `subsystem "sftp"` request was accepted on @p channel; the binding starts an SFTP session. */
typedef void (*SshSftpOpenCb)(uint8_t slot, uint32_t channel);
/** @brief Inbound bytes on an SFTP channel (the raw SSH_FXP_* stream) - kept out of the session data cb. */
typedef void (*SshSftpDataCb)(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len);

#if PROTOCORE_ENABLE_SSH_SFTP
/** @brief The registered sftp-open callback, or null; fired when sec 6.5 names the subsystem. */
SshSftpOpenCb protocore_ssh_channel_sftp_open_cb(void);
#endif

/** @brief An `exec "scp …"` request was accepted on @p channel (@p cmd is @p cmd_len bytes, not NUL-terminated). */
typedef void (*SshScpOpenCb)(uint8_t slot, uint32_t channel, const char *cmd, size_t cmd_len);
/** @brief Inbound bytes on an SCP channel (the RCP protocol stream). */
typedef void (*SshScpDataCb)(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len);

#if PROTOCORE_ENABLE_SSH_SCP
/** @brief The registered scp-open callback, or null; fired when sec 6.5 names an scp command. */
SshScpOpenCb protocore_ssh_channel_scp_open_cb(void);
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

/**
 * @brief Handle SSH_MSG_CHANNEL_OPEN_CONFIRMATION for a channel we opened (ssh -R).
 *
 * Matches the pending channel by our recipient id, records the peer's channel id /
 * window / max-packet, and marks it open. Fires the confirm callback (@p ok = true).
 * @return 0 on success, -1 if malformed or no matching pending channel.
 */

/**
 * @brief Handle SSH_MSG_CHANNEL_OPEN_FAILURE for a channel we opened (ssh -R).
 *
 * Frees the pending channel and fires the confirm callback (@p ok = false).
 * @return 0 on success, -1 if malformed or no matching pending channel.
 */

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

/**
 * @brief Bind a sec 6.5 file-transfer service to an open channel.
 *
 * The channel was opened as "session" (sec 5.1); the request that names the service arrives later,
 * and this records which one so inbound CHANNEL_DATA routes to its binding rather than to the
 * channel-data callback. This layer owns the channel record, so the request handler above it asks
 * rather than writes.
 *
 * @return 0 on success, -1 when the channel is closed or unknown.
 */

/** @brief Reset channel state for slot @p i. */

/** @brief The open channel @p id on connection @p i, or null. Local id == slot index. */

/**
 * @brief First free channel slot on connection @p i, or -1 if the pool is full. A pending
 *        (opened-but-unconfirmed) channel is in use just like an open one.
 */

/**
 * @brief Handle SSH_MSG_CHANNEL_OPEN and emit CHANNEL_OPEN_CONFIRMATION.
 *
 * Accepts a "session" channel; any other type yields CHANNEL_OPEN_FAILURE.
 * @return 0 if a response was produced, -1 if malformed.
 */

// RFC 4250 sec 4.9.3 lists pty-req, env, shell, exec and subsystem under "Connection Protocol
// Channel Request Names", so what each carries after want_reply is this layer's to check. Both
// answer one question: are the fields the section names present and whole.

/** @brief Read @p n consecutive strings from @p off: true when every one is present and whole. */

/** @brief RFC 4254 sec 6.2: string TERM, four uint32 dimensions, string encoded terminal modes. */

// ---------------------------------------------------------------------------
// RFC 4254 sec 6.2 / sec 6.7 / sec 8 - the pseudo-terminal a session channel carries
// ---------------------------------------------------------------------------

/**
 * @brief The terminal a "pty-req" asked for, as sec 6.2 orders its fields.
 *
 * "Zero dimension parameters MUST be ignored. The character/row dimensions override the pixel
 * dimensions (when nonzero)." Both pairs are kept as they arrived; a consumer applies that rule.
 */
typedef struct
{
    char term[PROTOCORE_SSH_PTY_TERM_MAX]; ///< TERM value, null-terminated, truncated to fit.
    uint32_t width_chars;                  ///< terminal width, characters
    uint32_t height_rows;                  ///< terminal height, rows
    uint32_t width_px;                     ///< terminal width, pixels
    uint32_t height_px;                    ///< terminal height, pixels
    const uint8_t *modes;                  ///< encoded terminal modes, pointing into the request
    uint32_t modes_len;                    ///< length of modes
} SshPtyRequest;

/**
 * @brief Walk an encoded terminal-mode stream (RFC 4254 sec 8) and report whether it is well formed.
 *
 * "Opcodes 1 to 159 have a single uint32 argument. Opcodes 160 to 255 are not yet defined, and cause
 * parsing to stop... The stream is terminated by opcode TTY_OP_END (0x00)." Only a truncated
 * argument makes a stream malformed. A stream that runs out before its terminator is accepted for
 * what it did carry, and an empty one is well formed: it sets no modes.
 *
 * @param modes     Encoded stream.
 * @param len       Bytes in @p modes.
 * @param consumed  Set to the bytes parsed before TTY_OP_END or an undefined opcode. May be null.
 */

/**
 * @brief Parse a "pty-req" body (sec 6.2) starting at @p off.
 * @return true when every field is present and whole and the mode stream parses.
 */

/**
 * @brief Parse a "window-change" body (sec 6.7): four uint32 dimensions, no reply.
 * @return true when all four are present.
 */

/** @brief An accepted "pty-req" on channel @p channel. Return false to refuse the terminal. */
typedef proto_bool (*SshPtyReqCb)(uint8_t i, uint32_t channel, const SshPtyRequest *pty);

/** @brief A "window-change" (sec 6.7) on a channel that already has a terminal. */
typedef void (*SshWindowChangeCb)(uint8_t i, uint32_t channel, uint32_t width_chars, uint32_t height_rows,
                                  uint32_t width_px, uint32_t height_px);

/** @brief Install the sec 6.2 handler. Without one a "pty-req" is refused: no terminal exists. */

/** @brief Install the sec 6.7 handler. */

/** @brief The terminal dimensions channel @p channel carries, or false when it has no pty. */

// ---------------------------------------------------------------------------
// RFC 4254 sec 6.10 - returning exit status
// ---------------------------------------------------------------------------

/**
 * @brief Send "exit-status" for a command that has terminated (RFC 4254 sec 6.10).
 *
 * "When the command running at the other end terminates, the following message can be sent to
 * return the exit status of the command. Returning the status is RECOMMENDED. No acknowledgement is
 * sent for this message. The channel needs to be closed with SSH_MSG_CHANNEL_CLOSE after this
 * message." want_reply is FALSE, as the section fixes it.
 *
 * @return 0 on success, -1 when the channel is closed or the stream is gone.
 */

/**
 * @brief Send "exit-signal" for a command killed by a signal (RFC 4254 sec 6.10).
 *
 * @param signal_name  Signal name without the "SIG" prefix, as sec 6.10 lists them.
 * @param core_dumped  Whether the command dumped core.
 * @param err_msg      Error message in UTF-8; may be null for an empty one.
 * @return 0 on success, -1 when the channel is closed, the stream is gone, or the names do not fit.
 */

#if PROTOCORE_ENABLE_SSH_SFTP || PROTOCORE_ENABLE_SSH_SCP
/**
 * @brief Tag @p c and fire the open callback when a sec 6.5 request names a file-transfer service.
 *
 * Declared here and implemented above, in the layer that owns those services. RFC 4251 sec 1 puts
 * the connection protocol beneath them, so this layer reaches up through a hook it declares rather
 * than including a header from the layer above. @p off points at the request-specific argument and
 * may be advanced; *@p accept is raised for a subsystem the base set does not already admit.
 */
#endif

/**
 * @brief Handle SSH_MSG_CHANNEL_REQUEST.
 *
 * "shell", "exec", "pty-req", and "env" are accepted; anything else is refused - except that when
 * PROTOCORE_ENABLE_SSH_SFTP is set a `subsystem "sftp"` is accepted (the channel binds SSH_CHAN_SERVICE_SFTP and the
 * sftp-open callback fires), and when PROTOCORE_ENABLE_SSH_SCP is set an `exec "scp …"` is additionally tagged
 * SSH_CHAN_SERVICE_SCP (the scp-open callback fires with the command). When want_reply is set, CHANNEL_SUCCESS /
 * CHANNEL_FAILURE is written to @p out and *@p out_len > 0; otherwise *@p out_len is 0.
 * @return 0 on success, -1 if malformed.
 */

/**
 * @brief Handle SSH_MSG_CHANNEL_DATA: bounds-check, update the window, and
 *        invoke the data callback. If the local window is exhausted a
 *        CHANNEL_WINDOW_ADJUST is written to @p out (*@p out_len > 0).
 * @return 0 on success, -1 if malformed or channel not open.
 */

/**
 * @brief Handle SSH_MSG_CHANNEL_EXTENDED_DATA (RFC 4254 §5.2): bounds-check, update
 *        the window, and discard the payload. The data_type_code selects a stream
 *        this end does not surface, so the bytes are accounted for and dropped. If
 *        the local window is exhausted a CHANNEL_WINDOW_ADJUST is written to @p out
 *        (*@p out_len > 0).
 * @return 0 on success, -1 if malformed or channel not open.
 */

/**
 * @brief Build an SSH_MSG_CHANNEL_DATA message carrying @p data to the client on
 *        channel @p channel (a local channel id from a prior open).
 * @return 0 on success, -1 if the channel is closed/unknown, the peer window is
 *         too small, or @p out is too small.
 */

/**
 * @brief Handle SSH_MSG_CHANNEL_WINDOW_ADJUST (grows the peer window).
 * @return 0 on success, -1 if malformed.
 */

/**
 * @brief Build SSH_MSG_CHANNEL_EOF for channel @p channel and latch that we sent it.
 *        The channel stays open (RFC 4254 sec 5.3).
 * @return 0 on success, -1 if the channel is closed/unknown or @p out is too small.
 */

/**
 * @brief Build SSH_MSG_CHANNEL_CLOSE for channel @p channel and mark it closed.
 * @return 0 on success, -1 if the channel is closed/unknown or @p out is too small.
 */

/** @brief Bytes in SSH_MSG_CHANNEL_EOF: the message number and the recipient channel. */
#define SSH_CHANNEL_EOF_LEN 5u

/** @brief Bytes in SSH_MSG_CHANNEL_CLOSE: the message number and the recipient channel. */
#define SSH_CHANNEL_CLOSE_LEN 5u

/**
 * @brief Build SSH_MSG_CHANNEL_DATA for @p channel and put it on the slot's stream (sec 5.2).
 * @return the bytes accepted from @p data, -1 when the stream is gone or the build failed.
 */

/**
 * @brief Build SSH_MSG_CHANNEL_EOF for @p channel and put it on the slot's stream (sec 5.3).
 * @return 0 on success, -1 otherwise.
 */

/**
 * @brief Build SSH_MSG_CHANNEL_CLOSE for @p channel and put it on the slot's stream (sec 5.3).
 * @return 0 on success, -1 otherwise.
 */

/**
 * @brief Open a "forwarded-tcpip" channel to the peer and put it on the slot's stream (sec 7.2).
 * @return the local channel number on success, -1 when the pool is full or the stream is gone.
 */

/**
 * @brief Handle an inbound SSH_MSG_CHANNEL_EOF: mark the peer done sending on the
 *        recipient channel. Sends nothing and leaves the channel open, so the other
 *        direction keeps carrying data (RFC 4254 sec 5.3).
 * @return 0 on success, -1 if malformed or the channel is unknown.
 */

/**
 * @brief Handle an inbound SSH_MSG_CHANNEL_CLOSE: route to the recipient channel,
 *        reply with EOF + CLOSE, and mark it closed.
 * @return 0 if a response was produced, -1 if malformed or the channel is unknown.
 */

// ---------------------------------------------------------------------------
// RFC 4254 sec 7 - TCP/IP port forwarding
// ---------------------------------------------------------------------------

/**
 * @brief Allow/deny policy for a forward target. Return true to permit the connect.
 *
 * @p host is NUL-terminated. If no policy is installed every post-authentication
 * forward is permitted (an open proxy for authenticated users) - install one to
 * restrict the reachable host:port set.
 */
typedef proto_bool (*SshForwardPolicyCb)(const char *host, uint16_t port);

#if PROTOCORE_SSH_PORT_FORWARD

/** @brief Install the forward-target policy (optional; default permits all). */

/**
 * @brief Enable direct-tcpip forwarding: install the channel forward callbacks.
 *
 * Call once after protocore_ssh_conn_setup(). Until then (or if PROTOCORE_SSH_PORT_FORWARD is 0)
 * the channel codec refuses every direct-tcpip open, so there is no open relay.
 */

/**
 * @brief Pump every forward on SSH connection @p ssh_slot: move buffered target
 *        bytes to the client (bounded by the channel's peer window) and propagate
 *        a close from either side. Called from the SSH connection poll each loop.
 */

/**
 * @brief RFC 4254 sec 7.2: the sec 7.1 binding that owns @p listener_idx.
 *
 * A connection accepted on a forwarded listener is answered with a "forwarded-tcpip" CHANNEL_OPEN
 * carrying "the address that was connected" and "port that was connected", which are the ones the
 * binding requested. False when no active binding owns the listener.
 */

/** @brief Tear down all forwards on @p ssh_slot (its SSH connection is closing). */

#endif // PROTOCORE_SSH_PORT_FORWARD

/** @brief Dispatch messages 80 to 127 (RFC 4254). */

/** @brief RFC 4254 sec 5.2 window: what a flow-control call reads and writes. */
typedef struct
{
    SshFlow *f;            ///< the window a call acts on
    uint32_t local_window; ///< our initial window
    uint32_t peer_window;  ///< the peer's
    uint32_t peer_max_pkt; ///< the largest packet it will take
    uint32_t n;            ///< bytes a take accounts for
    uint32_t add;          ///< in: credit to add; out: what a replenish owes
    uint32_t want;         ///< bytes a send would like to put out
    size_t len;            ///< the length a send test measures
} SshFlowArgs;

/** @brief RFC 4254 sec 5 channels: what a channel call acts on. */
typedef struct
{
    uint8_t slot;            ///< the SSH slot
    uint32_t channel;        ///< the channel number on it
    uint32_t id;             ///< the channel a lookup names
    SshChanService service;  ///< the service a bind attaches
    const uint8_t *payload;  ///< the message body
    size_t len;              ///< how many bytes it has
    const uint8_t *data;     ///< payload bytes a send carries
    uint8_t *out;            ///< where a reply is written
    size_t out_len;          ///< what was written
    size_t cap;              ///< how much room it has
    uint32_t exit_status;    ///< sec 6.10 exit-status
    const char *signal_name; ///< sec 6.10 exit-signal
    proto_bool core_dumped;  ///< whether it dumped core
    const char *err_msg;     ///< the message that accompanies it
    const uint8_t *rtype;    ///< sec 5.4 request type
    uint32_t rtype_len;      ///< its length
} SshChanArgs;

/** @brief RFC 4254 sec 6.2 pty-req and sec 6.7 window-change: what a terminal call parses. */
typedef struct
{
    const uint8_t *p;     ///< the request bytes
    size_t len;           ///< how many
    size_t off;           ///< where the fields start
    uint8_t n;            ///< strings a presence check counts
    const uint8_t *modes; ///< the encoded terminal modes
    uint32_t modes_len;   ///< their length
    uint32_t consumed;    ///< what a mode walk consumed
    SshPtyRequest *req;   ///< where a parse lands
    uint32_t width_chars; ///< the reported terminal width
    uint32_t height_rows; ///< its height
    uint32_t width_px;    ///< the same in pixels
    uint32_t height_px;   ///<
} SshPtyArgs;

/** @brief RFC 4254 sec 7 TCP/IP forwarding: what a forwarding call names. */
typedef struct
{
    uint8_t slot;          ///< the SSH slot a forward belongs to
    const char *conn_addr; ///< the address connected to
    uint16_t conn_port;    ///< its port
    const char *orig_addr; ///< where the connection came from
    uint16_t orig_port;    ///< its port
    uint8_t listener_idx;  ///< the listener row a binding lookup names
    uint8_t out_slot;      ///< the slot that binding belongs to
    uint16_t bind_port;    ///< the port it bound
    const char *bind_addr; ///< the address it bound
} SshFwdArgs;

/**
 * @brief The SSH connection protocol (RFC 4254): channels, their windows, and what runs on them.
 *
 * A caller fills the group its call belongs to, invokes the call through ::SshConnection, and reads
 * the outcome off the same handle. The groups are separate because a window has nothing to do with
 * a forwarding binding, and neither has anything to do with a pty request.
 *
 * @var SshConnectionNs::flow      sec 5.2 window arguments
 * @var SshConnectionNs::chan      sec 5 channel arguments
 * @var SshConnectionNs::pty       sec 6.2 / 6.7 terminal arguments
 * @var SshConnectionNs::fwd       sec 7 forwarding arguments
 * @var SshConnectionNs::msg_type  the message a dispatch routes
 * @var SshConnectionNs::ok        a call's true/false outcome
 * @var SshConnectionNs::i32       a call's signed outcome
 * @var SshConnectionNs::u32       a call's 32-bit outcome
 * @var SshConnectionNs::found     the channel a lookup reports, or NULL
 */
typedef struct
{
    SshFlowArgs flow;
    SshChanArgs chan;
    SshPtyArgs pty;
    SshFwdArgs fwd;
    uint8_t msg_type;

    proto_bool ok;
    int i32;
    uint32_t u32;
    SshChannel *found;

    // sec 5.2 window
    void (*const flow_init)(uint8_t *restrict work);
    void (*const flow_recv_take)(uint8_t *restrict work);
    void (*const flow_replenish_due)(uint8_t *restrict work);
    void (*const flow_local_credit)(uint8_t *restrict work);
    void (*const flow_send_allows)(uint8_t *restrict work);
    void (*const flow_send_cap)(uint8_t *restrict work);
    void (*const flow_send_take)(uint8_t *restrict work);
    void (*const flow_peer_add)(uint8_t *restrict work);

    // sec 5 channels
    void (*const channel_init)(uint8_t *restrict work);
    void (*const chan_alloc)(uint8_t *restrict work);
    void (*const chan_by_id)(uint8_t *restrict work);
    void (*const channel_bind_service)(uint8_t *restrict work);
    void (*const channel_handle_open)(uint8_t *restrict work);
    void (*const channel_handle_open_confirm)(uint8_t *restrict work);
    void (*const channel_handle_open_failure)(uint8_t *restrict work);
    void (*const channel_handle_request)(uint8_t *restrict work);
    void (*const channel_handle_data)(uint8_t *restrict work);
    void (*const channel_handle_extended_data)(uint8_t *restrict work);
    void (*const channel_handle_window_adjust)(uint8_t *restrict work);
    void (*const channel_handle_eof)(uint8_t *restrict work);
    void (*const channel_handle_close)(uint8_t *restrict work);
    void (*const channel_build_data)(uint8_t *restrict work);
    void (*const channel_build_eof)(uint8_t *restrict work);
    void (*const channel_build_close)(uint8_t *restrict work);
    void (*const channel_send_data)(uint8_t *restrict work);
    void (*const channel_send_eof)(uint8_t *restrict work);
    void (*const channel_send_close)(uint8_t *restrict work);
    void (*const channel_send_exit_status)(uint8_t *restrict work);
    void (*const channel_send_exit_signal)(uint8_t *restrict work);
    void (*const channel_open_forwarded)(uint8_t *restrict work);
    void (*const channel_send_open_forwarded)(uint8_t *restrict work);
    void (*const channel_pty)(uint8_t *restrict work);

    // sec 6.2 / 6.7 terminal
    void (*const req_strings_present)(uint8_t *restrict work);
    void (*const pty_req_fields_present)(uint8_t *restrict work);
    void (*const pty_modes_valid)(uint8_t *restrict work);
    void (*const pty_req_parse)(uint8_t *restrict work);
    void (*const window_change_parse)(uint8_t *restrict work);

    // sec 7 forwarding
    void (*const forward_begin)(uint8_t *restrict work);
    void (*const forward_pump)(uint8_t *restrict work);
    void (*const forward_binding)(uint8_t *restrict work);
    void (*const forward_reset)(uint8_t *restrict work);
    void (*const global_request_handle)(uint8_t *restrict work);
    void (*const dispatch)(uint8_t *restrict work);

    // the application handlers this layer calls back into
    void (*const set_data_cb)(uint8_t *restrict work);
    void (*const set_pty_req_cb)(uint8_t *restrict work);
    void (*const set_window_change_cb)(uint8_t *restrict work);
    void (*const set_forward_open_cb)(uint8_t *restrict work);
    void (*const set_forward_data_cb)(uint8_t *restrict work);
    void (*const set_forward_confirm_cb)(uint8_t *restrict work);
    void (*const set_forward_policy_cb)(uint8_t *restrict work);
    void (*const set_rforward_open_cb)(uint8_t *restrict work);
    void (*const set_rforward_cancel_cb)(uint8_t *restrict work);
    void (*const set_sftp_open_cb)(uint8_t *restrict work);
    void (*const set_sftp_data_cb)(uint8_t *restrict work);
    void (*const set_scp_open_cb)(uint8_t *restrict work);
    void (*const set_scp_data_cb)(uint8_t *restrict work);

    // the handler a setter installs, read by the setter it belongs to
    SshChannelDataCb data_cb;
    SshPtyReqCb pty_req_cb;
    SshWindowChangeCb window_change_cb;
    SshForwardOpenCb forward_open_cb;
    SshForwardDataCb forward_data_cb;
    SshForwardConfirmCb forward_confirm_cb;
    SshForwardPolicyCb forward_policy_cb;
    SshRemoteForwardOpenCb rforward_open_cb;
    SshRemoteForwardCancelCb rforward_cancel_cb;
    SshSftpOpenCb sftp_open_cb;
    SshSftpDataCb sftp_data_cb;
    SshScpOpenCb scp_open_cb;
    SshScpDataCb scp_data_cb;
} SshConnectionNs;

/** @brief The one symbol this module exports. */
extern SshConnectionNs SshConnection;

/**
 * @brief The PROTOCORE_SSH_CONNECTION_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_ssh_connection_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_CONNECTION_CONNECTION_H
