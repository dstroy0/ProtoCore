// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file client.h
 * @brief The client engine: drive the handshake, authenticate, and request the port forward.
 */

#ifndef PROTOCORE_CLIENT_CLIENT_H
#define PROTOCORE_CLIENT_CLIENT_H

#include "network_drivers/presentation/ssh/common.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_SSH_CLIENT

/** @brief How to reach the relay, who to log in as, and what to forward back. */
typedef struct
{
    const char *host;         ///< relay hostname or dotted-quad.
    uint16_t port;            ///< relay SSH port (0 => 22).
    const char *user;         ///< SSH username on the relay.
    const uint8_t *auth_seed; ///< 32-byte ssh-ed25519 private seed the device authenticates with.
    const uint8_t *host_pin;  ///< 32-byte SHA-256 of the relay's host-key blob (K_S); handshake aborts on mismatch.
    const char *bind_addr;    ///< address the relay binds the forward on ("" / null => "" = all, "localhost", ...).
    uint16_t
        bind_port; ///< remote port the relay listens on (tcpip-forward); connections accepted there are forwarded back.
    uint16_t local_port; ///< local TCP port a forwarded connection is bridged to (e.g. 80).
} protocore_ssh_client_cfg;

/** @brief Lifecycle phase of the forward, for observability. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_SSH_CLIENT_IDLE = 0,   ///< not started.
    PROTOCORE_SSH_CLIENT_CONNECTING, ///< TCP + SSH handshake + auth in progress.
    PROTOCORE_SSH_CLIENT_UP,         ///< authenticated and the remote forward is established.
    PROTOCORE_SSH_CLIENT_FAILED      ///< the last attempt failed (host-key mismatch, auth, or transport).
} protocore_ssh_client_state;

/**
 * @brief Start (or restart) the forward: connect to the relay, handshake, authenticate, and request
 *        the remote forward. Non-blocking after the initial connect; drive it with poll().
 * @return true if the connection and handshake started; false on bad args or immediate failure.
 *
 * @warning Call begin() and poll() from the SAME task, and give that task enough stack for the
 * negotiated KEX. The handshake's field arithmetic runs in the caller's task: curve25519/ed25519
 * peak ~10.5 KB, and the mlkem768x25519 hybrid (PROTOCORE_ENABLE_PQC_KEX) adds ML-KEM-768 for ~16 KB total.
 * The Arduino loop() task's default 8 KB is NOT enough - run the forward from a dedicated task created
 * with a >= 20480-byte stack (see the example). begin() claims a private scratch arena for the calling
 * task, so poll() must run in that same task or the packet-decrypt tripwire fires.
 */
proto_bool protocore_ssh_client_begin(const protocore_ssh_client_cfg *cfg);

/**
 * @brief Pump the forward: advance the handshake, service the relay's keepalives, accept
 *        forwarded-tcpip channels and bridge their bytes to/from the local service. Call every loop,
 *        from the same (adequately-stacked) task that called begin() - see the begin() @warning.
 */
void protocore_ssh_client_poll(void);

/** @brief Tear the forward down and close the relay connection. */
void protocore_ssh_client_end(void);

/**
 * @brief The client engine's operations, for the layers that frame messages on its connection.
 *
 * @var SshClientNs::send        frame one payload as a binary packet and write it to the relay
 * @var SshClientNs::crypto_work the client slot's handshake scratch, or null when the pool is short
 * @var SshClientNs::state       the forward's lifecycle phase
 */
typedef struct
{
    proto_bool (*send)(const uint8_t *payload, size_t len);
    uint8_t *(*crypto_work)(void);
    protocore_ssh_client_state (*state)(void);
} SshClientNs;

/** @brief The one instance, defined in client.c. */
extern const SshClientNs SshClient;

#endif // PROTOCORE_ENABLE_SSH_CLIENT

PROTOCORE_END_DECLS

#endif // PROTOCORE_CLIENT_CLIENT_H
