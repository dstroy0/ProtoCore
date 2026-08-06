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

/**
 * @brief Application callback that decides whether a public key is authorized
 *        for @p user. @p blob is the "ssh-rsa" public-key blob.
 * @return true if the key may authenticate this user.
 */
typedef proto_bool (*SshPubkeyCb)(const char *user, const uint8_t *blob, size_t blob_len);

#if PC_ENABLE_SSH_KEYBOARD_INTERACTIVE
/**
 * @brief Userauth (RFC 4252): the two credential callbacks an application installs, and the arms
 * that turn one request.
 *
 * @var SshAuthNs::set_password_cb         Install the password-verification callback (nullptr → all fail)
 * @var SshAuthNs::set_pubkey_cb           Install the publickey-authorization callback (nullptr → all fail)
 * @var SshAuthNs::handle_service_request  Handle SSH_MSG_SERVICE_REQUEST; emit SERVICE_ACCEPT for ssh-userauth
 * @var SshAuthNs::parse_request           Parse an SSH_MSG_USERAUTH_REQUEST into @p req
 * @var SshAuthNs::build_failure           Build SSH_MSG_USERAUTH_FAILURE advertising "password"
 * @var SshAuthNs::build_success           Build SSH_MSG_USERAUTH_SUCCESS
 * @var SshAuthNs::handle_request          Handle a USERAUTH_REQUEST end-to-end for slot @p i
 * @var SshAuthNs::handle_info_response    Handle an SSH_MSG_USERAUTH_INFO_RESPONSE (RFC 4256 §3.4) for slot @p
 *                                         i
 */
typedef struct
{
    void (*set_password_cb)(SshPasswordCb cb);
    void (*set_pubkey_cb)(SshPubkeyCb cb);
    int (*handle_service_request)(const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap);
    int (*parse_request)(const uint8_t *payload, size_t len, SshAuthReq *req);
    int (*build_failure)(uint8_t *out, size_t *out_len, size_t cap, proto_bool partial);
    int (*build_success)(uint8_t *out, size_t *out_len, size_t cap);
    int (*handle_request)(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap);
    int (*handle_info_response)(uint8_t i, const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len,
                                size_t cap);
} SshAuthNs;

/** @brief The one symbol this module exports. */
extern const SshAuthNs SshAuth;

#endif

PROTO_END_DECLS

#endif // PROTOCORE_SSH_AUTH_H
