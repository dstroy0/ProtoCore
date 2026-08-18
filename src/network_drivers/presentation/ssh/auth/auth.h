// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file auth.h
 * @brief RFC 4252 user authentication.
 *
 * After NEWKEYS the client requests the "ssh-userauth" service; the server accepts it and then
 * drives SSH_MSG_USERAUTH_REQUEST exchanges until a method succeeds or the connection is dropped.
 * The "none" method is always answered with a failure that advertises what may continue
 * (RFC 4252 sec 5.2), which is how a client discovers the supported methods.
 */

#ifndef PROTOCORE_AUTH_AUTH_H
#define PROTOCORE_AUTH_AUTH_H

#include "network_drivers/presentation/ssh/common.h"


PROTOCORE_BEGIN_DECLS

/** @brief Parsed SSH_MSG_USERAUTH_REQUEST. */
typedef struct
{
    char user[SSH_AUTH_USER_MAX];         ///< User name, null-terminated.
    char service[32];                     ///< Requested service ("ssh-connection").
    char method[24];                      ///< Method name ("none", "password", "publickey", "keyboard-interactive").
    char password[SSH_AUTH_PASS_MAX];     ///< Password (method == "password"); the old one on a change.
    char new_password[SSH_AUTH_PASS_MAX]; ///< New password when is_pw_change (RFC 4252 sec 8).
    proto_bool is_password;               ///< True if a password method-request was parsed.
    proto_bool is_pw_change;              ///< True if the request set the change-password flag.
    proto_bool is_kbdint;                 ///< True if a keyboard-interactive method-request was parsed (RFC 4256).

    // publickey method (RFC 4252 §7)
    proto_bool is_pubkey;            ///< True if a publickey method-request was parsed.
    proto_bool has_signature;        ///< True if the request carried a signature.
    char pk_algo[SSH_AUTH_ALGO_MAX]; ///< Public-key algorithm name.
    const uint8_t *pk_blob;          ///< Public-key blob (points into the payload).
    uint32_t pk_blob_len;            ///< Length of pk_blob.
    const uint8_t *signature;        ///< Raw signature bytes (points into the payload).
    uint32_t signature_len;          ///< Length of signature.
    const uint8_t *signed_prefix;    ///< Bytes of the request that the signature covers.
    size_t signed_prefix_len;        ///< Length of signed_prefix (payload up to the signature).
} SshAuthReq;

/**
 * @brief Application callback that validates a username/password pair.
 * @return true to accept the credentials.
 */
typedef proto_bool (*SshPasswordCb)(const char *user, const char *password);

/** @brief Install the password-verification callback (nullptr → all fail). */

/**
 * @brief Application callback that STARTS a password change (RFC 4252 sec 8) for slot @p slot.
 *
 * The application owns the store and its encryption, and a store can be slow (flash). This callback
 * must not block: it copies what it needs, kicks off its own work, and returns. When the change has
 * finished (or failed to verify @p old_password) the application reports the outcome with
 * protocore_ssh_auth_pw_change_report(); the reply to the client is deferred until then, so the SSH worker
 * is never held on the store. @p old_password / @p new_password are wiped once this returns.
 */
typedef void (*SshPasswordChangeCb)(uint8_t slot, const char *user, const char *old_password, const char *new_password);

/** @brief A slot's password-change state: idle, handed to the application, or finished either way. */
typedef enum
{
    PROTOCORE_SSH_PW_CHANGE_NONE,
    PROTOCORE_SSH_PW_CHANGE_BUSY,
    PROTOCORE_SSH_PW_CHANGE_OK,
    PROTOCORE_SSH_PW_CHANGE_FAIL,
} SshPwChange;

/** @brief Install the password-change start callback (nullptr → change requests are refused busy). */

/**
 * @brief Report the outcome of the change the start callback began for slot @p slot.
 *
 * @p ok true when the old password verified and the new one was stored. Moves the slot to OK or
 * FAIL, which the next poll drains into the deferred reply. A no-op when no change is in flight
 * (a stale report after the connection left).
 */

/**
 * @brief Take slot @p i's finished change outcome, clearing it back to NONE.
 *
 * @return OK or FAIL once the application has reported, NONE while idle or still in flight.
 */
/**
 * @brief Write the "publickey" USERAUTH_REQUEST body that RFC 4252 sec 7 signs and sends.
 *
 * sec 7 gives one field order and uses it twice: the signature covers
 *
 *     string    session identifier
 *     byte      SSH_MSG_USERAUTH_REQUEST
 *     string    user name
 *     string    service name
 *     string    "publickey"
 *     boolean   TRUE
 *     string    public key algorithm name
 *     string    public key to be used for authentication
 *
 * and the request that carries the signature is the same bytes from SSH_MSG_USERAUTH_REQUEST on,
 * with the signature appended. Writing it in one place is what keeps the two identical - a
 * verifier hashes what arrived, so any divergence here is a signature that never validates.
 *
 * @param w        Span written into.
 * @param sid      Session identifier, or null to start at SSH_MSG_USERAUTH_REQUEST (the request form).
 * @param sid_len  Length of @p sid; ignored when @p sid is null.
 * @param user     User name.
 * @param service  Service name, normally "ssh-connection".
 * @param pk_algo  Public key algorithm name.
 * @param pk_blob  Public key blob.
 * @param pk_len   Length of @p pk_blob.
 */

/**
 * @brief True once slot @p i has gone too long without authenticating (RFC 4252 sec 4).
 *
 * "The server SHOULD have a timeout for authentication and disconnect if the authentication has not
 * been accepted within the timeout period. The RECOMMENDED timeout period is 10 minutes." The clock
 * starts on the first call for a slot and stops once authentication completes; SSH_AUTH_TIMEOUT_MS
 * of 0 disables it. Poll it, and disconnect when it answers true.
 */

SshPwChange protocore_ssh_auth_pw_change_take(uint8_t i);

/**
 * @brief Drop a parked password change without replying to it.
 *
 * RFC 4252 sec 5.1 sends SSH_MSG_USERAUTH_SUCCESS once, so a change whose answer arrives after some
 * other method already succeeded is discarded rather than answered.
 */

/**
 * @brief Drop slot @p i's half-finished authentication state and wipe its username.
 *
 * A keyboard-interactive exchange is armed by the USERAUTH_REQUEST and consumed by the matching
 * INFO_RESPONSE. A connection that leaves between the two ends here.
 */

/**
 * @brief Application callback that decides whether a public key is authorized
 *        for @p user. @p blob is the "ssh-rsa" public-key blob.
 * @return true if the key may authenticate this user.
 */
typedef proto_bool (*SshPubkeyCb)(const char *user, const uint8_t *blob, size_t blob_len);

/** @brief Install the publickey-authorization callback (nullptr → all fail). */

/**
 * @brief Parse an SSH_MSG_USERAUTH_REQUEST into @p req.
 * @return 0 on success, -1 if malformed.
 */

/** @brief Build SSH_MSG_USERAUTH_FAILURE advertising "password". */

/** @brief Build SSH_MSG_USERAUTH_SUCCESS. */

/**
 * @brief Handle a USERAUTH_REQUEST end-to-end for slot @p i.
 *
 * Parses the request, checks "password" credentials via the installed callback,
 * and writes either USERAUTH_SUCCESS or USERAUTH_FAILURE to @p out. On success
 * the session is marked authenticated and advanced to the connection phase.
 *
 * @return 0 if a response was produced (check the message type), -1 on parse
 *         error.
 */

#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
/**
 * @brief Handle an SSH_MSG_USERAUTH_INFO_RESPONSE (RFC 4256 §3.4) for slot @p i.
 *
 * The response to the single "Password:" prompt this server sends is verified through the installed
 * password callback (keyboard-interactive is the challenge-response face of password auth here). Writes
 * USERAUTH_SUCCESS or USERAUTH_FAILURE to @p out. Only valid while a keyboard-interactive exchange is
 * pending for the slot (a prior USERAUTH_REQUEST selected it); otherwise fails.
 *
 * @return 0 if a response was produced (check the message type), -1 on parse error / no exchange pending.
 */
#endif

/** @brief Dispatch messages 50 to 79; 80 and above need authentication (RFC 4252 sec 6). */

/** @brief Send the reply a finished password change (RFC 4252 sec 8) deferred on slot @p i. */

/** @brief RFC 4252 sec 5: the body of one userauth message. */
typedef struct
{
    const uint8_t *payload; ///< the message body
    size_t len;             ///< how many bytes it has
} SshAuthMsgArgs;

/** @brief Where a reply is written, either as a buffer or as a span. */
typedef struct
{
    uint8_t *out;      ///< where a reply is written
    size_t out_len;    ///< what was written
    size_t cap;        ///< how much room it has
    protocore_span *w; ///< the span a publickey request is built in
} SshAuthOutArgs;

/** @brief RFC 4252 sec 5 / sec 7: the names one USERAUTH_REQUEST carries, and the key it offers. */
typedef struct
{
    const uint8_t *sid;     ///< the session identifier it signs over, or NULL for the request form
    size_t sid_len;         ///< its length; ignored when sid is NULL
    const char *user;       ///< the user name
    const char *service;    ///< the service name, normally "ssh-connection"
    const char *pk_algo;    ///< the public key algorithm name
    const uint8_t *pk_blob; ///< the public key blob
    size_t pk_len;          ///< its length
} SshUserauthArgs;

/** @brief RFC 4252 sec 7 / sec 8: what an attempt of each method is checked against. */
typedef struct
{
    SshPasswordCb password_cb;              ///< what a password attempt is checked against
    SshPasswordChangeCb password_change_cb; ///< what a change request is handed to
    SshPubkeyCb pubkey_cb;                  ///< what a public key is checked against
} SshAuthCbs;

/** @brief The authentication layer's own state and the calls that reach it, described only in auth.c. */
struct SshAuthInternal;

/**
 * @brief The SSH authentication protocol (RFC 4252): what a slot must satisfy before a service runs.
 *
 * A caller sets the members a call takes, invokes it through ::SshAuth, and reads the outcome off
 * the same handle.
 *
 * @var SshAuthNs::slot        the SSH slot a call acts on
 * @var SshAuthNs::msg_type    the message a dispatch routes (sec 6: 50-79 here, 80+ need auth)
 * @var SshAuthNs::msg         sec 5 the message body a dispatch is given
 * @var SshAuthNs::out_args    where a reply is written
 * @var SshAuthNs::req         where a parse lands the request (sec 5)
 * @var SshAuthNs::partial     the failure is a partial success (sec 5.1)
 * @var SshAuthNs::userauth    sec 5 / sec 7 the fields one request names
 * @var SshAuthNs::cbs         what an attempt of each method is checked against
 * @var SshAuthNs::ok          a call's true/false outcome
 * @var SshAuthNs::i32         a call's signed outcome
 * @var SshAuthNs::set_password_cb         install the password check
 * @var SshAuthNs::set_password_change_cb  install the change handler
 * @var SshAuthNs::set_pubkey_cb           install the public key check
 * @var SshAuthNs::pw_change_report        report a finished change back to the slot
 * @var SshAuthNs::pw_change_clear         drop a pending change on the slot
 * @var SshAuthNs::passwd_change_reply     send the reply a finished change deferred
 * @var SshAuthNs::write_publickey_request build a publickey request into out_args.w (sec 7)
 * @var SshAuthNs::timed_out               the slot has gone too long unauthenticated (sec 4)
 * @var SshAuthNs::reset                   clear the slot's authentication state
 * @var SshAuthNs::parse_request           parse a USERAUTH_REQUEST into req (sec 5)
 * @var SshAuthNs::build_failure           write USERAUTH_FAILURE (sec 5.1)
 * @var SshAuthNs::build_success           write USERAUTH_SUCCESS (sec 5.1)
 * @var SshAuthNs::handle_request          answer a USERAUTH_REQUEST
 * @var SshAuthNs::handle_info_response    answer a USERAUTH_INFO_RESPONSE (RFC 4256 sec 3.4)
 * @var SshAuthNs::dispatch                route messages 50 to 79
 * @var SshAuthNs::internal    the layer's state and the calls that reach it
 */
typedef struct
{
    uint8_t slot;       ///< the SSH slot a call acts on
    uint8_t msg_type;   ///< the message a dispatch routes
    SshAuthReq *req;    ///< where a parse lands the request (sec 5)
    proto_bool partial; ///< the failure is a partial success (sec 5.1)

    SshAuthMsgArgs msg;       ///< sec 5 the message body a dispatch is given
    SshAuthOutArgs out_args;  ///< where a reply is written
    SshUserauthArgs userauth; ///< sec 5 / sec 7 the fields one request names
    SshAuthCbs cbs;           ///< what an attempt is checked against

    proto_bool ok;
    int i32;

    void (*set_password_cb)(struct SshAuthInternal *ctx);
    void (*set_password_change_cb)(struct SshAuthInternal *ctx);
    void (*set_pubkey_cb)(struct SshAuthInternal *ctx);
    void (*pw_change_report)(struct SshAuthInternal *ctx);
    void (*pw_change_clear)(struct SshAuthInternal *ctx);
    void (*passwd_change_reply)(struct SshAuthInternal *ctx);
    void (*write_publickey_request)(struct SshAuthInternal *ctx);
    void (*timed_out)(struct SshAuthInternal *ctx);
    void (*reset)(struct SshAuthInternal *ctx);
    void (*parse_request)(struct SshAuthInternal *ctx);
    void (*build_failure)(struct SshAuthInternal *ctx);
    void (*build_success)(struct SshAuthInternal *ctx);
    void (*handle_request)(struct SshAuthInternal *ctx);
#if PROTOCORE_ENABLE_SSH_KEYBOARD_INTERACTIVE
    void (*handle_info_response)(struct SshAuthInternal *ctx);
#endif
    void (*dispatch)(struct SshAuthInternal *ctx);

    struct SshAuthInternal *internal;
} SshAuthNs;

/** @brief The one symbol this module exports. */
extern SshAuthNs SshAuth;

PROTOCORE_END_DECLS

#endif // PROTOCORE_AUTH_AUTH_H
