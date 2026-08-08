// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_auth.h
 * @brief SSH user-authentication layer (RFC 4252).
 *
 * After NEWKEYS the client requests the "ssh-userauth" service; the server
 * accepts it and then drives SSH_MSG_USERAUTH_REQUEST exchanges until a method
 * succeeds (SSH_MSG_USERAUTH_SUCCESS) or the connection is dropped.
 *
 * This implementation supports the "password" method (RFC 4252 §8): the
 * password travels inside the encrypted transport and is checked against an
 * application-supplied callback. The "none" method is always answered with a
 * failure that advertises "password" (RFC 4252 §5.2), which is how a client
 * discovers the supported methods.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_AUTH_H
#define PROTOCORE_SSH_AUTH_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Message type constants (RFC 4250 §4.1.1: 50 to 59 generic, 60 to 79 method specific)
// ---------------------------------------------------------------------------

#define SSH_MSG_USERAUTH_REQUEST 50
#define SSH_MSG_USERAUTH_FAILURE 51
#define SSH_MSG_USERAUTH_SUCCESS 52
#define SSH_MSG_USERAUTH_PK_OK 60
// 60 is method-specific: it is PK_OK for publickey and INFO_REQUEST for keyboard-interactive
// (RFC 4256 §3.2). The current auth phase/state disambiguates which handler owns an inbound 60.
#define SSH_MSG_USERAUTH_INFO_REQUEST 60  // RFC 4256 §3.2 (keyboard-interactive, server->client)
#define SSH_MSG_USERAUTH_INFO_RESPONSE 61 // RFC 4256 §3.4 (keyboard-interactive, client->server)

/** @brief Parsed SSH_MSG_USERAUTH_REQUEST. */
typedef struct
{
    char user[SSH_AUTH_USER_MAX];     ///< User name, null-terminated.
    char service[32];                 ///< Requested service ("ssh-connection").
    char method[24];                  ///< Method name ("none", "password", "publickey", "keyboard-interactive").
    char password[SSH_AUTH_PASS_MAX]; ///< Password (method == "password").
    proto_bool is_password;           ///< True if a password method-request was parsed.
    proto_bool is_kbdint;             ///< True if a keyboard-interactive method-request was parsed (RFC 4256).

    // publickey method (RFC 4252 §7)
    proto_bool is_pubkey;         ///< True if a publickey method-request was parsed.
    proto_bool has_signature;     ///< True if the request carried a signature.
    char pk_algo[20];             ///< Public-key algorithm name.
    const uint8_t *pk_blob;       ///< Public-key blob (points into the payload).
    uint32_t pk_blob_len;         ///< Length of pk_blob.
    const uint8_t *signature;     ///< Raw signature bytes (points into the payload).
    uint32_t signature_len;       ///< Length of signature.
    const uint8_t *signed_prefix; ///< Bytes of the request that the signature covers.
    size_t signed_prefix_len;     ///< Length of signed_prefix (payload up to the signature).
} SshAuthReq;

/**
 * @brief Application callback that validates a username/password pair.
 * @return true to accept the credentials.
 */
typedef proto_bool (*SshPasswordCb)(const char *user, const char *password);

/** @brief Install the password-verification callback (nullptr → all fail). */
void pc_ssh_auth_set_password_cb(SshPasswordCb cb);

/**
 * @brief Application callback that decides whether a public key is authorized
 *        for @p user. @p blob is the "ssh-rsa" public-key blob.
 * @return true if the key may authenticate this user.
 */
typedef proto_bool (*SshPubkeyCb)(const char *user, const uint8_t *blob, size_t blob_len);

/** @brief Install the publickey-authorization callback (nullptr → all fail). */
void pc_ssh_auth_set_pubkey_cb(SshPubkeyCb cb);

/**
 * @brief Handle SSH_MSG_SERVICE_REQUEST; emit SERVICE_ACCEPT for ssh-userauth.
 * @return 0 and writes SERVICE_ACCEPT to @p out, or -1 if the service is not
 *         "ssh-userauth" or the message is malformed.
 */
int pc_ssh_auth_handle_service_request(const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap);

/**
 * @brief Parse an SSH_MSG_USERAUTH_REQUEST into @p req.
 * @return 0 on success, -1 if malformed.
 */
int pc_ssh_auth_parse_request(const uint8_t *payload, size_t len, SshAuthReq *req);

/** @brief Build SSH_MSG_USERAUTH_FAILURE advertising "password". */
int pc_ssh_auth_build_failure(uint8_t *out, size_t *out_len, size_t cap, proto_bool partial);

/** @brief Build SSH_MSG_USERAUTH_SUCCESS. */
int pc_ssh_auth_build_success(uint8_t *out, size_t *out_len, size_t cap);

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
int pc_ssh_auth_handle_request(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                               size_t cap);

#if PC_ENABLE_SSH_KEYBOARD_INTERACTIVE
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
int pc_ssh_auth_handle_info_response(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                                     size_t cap);
#endif

PROTO_END_DECLS

#endif // PROTOCORE_SSH_AUTH_H
