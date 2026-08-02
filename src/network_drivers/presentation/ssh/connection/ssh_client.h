// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_client.h
 * @brief Outbound SSH client + reverse tunnel (PC_ENABLE_SSH_CLIENT).
 *
 * The SSH server terminates inbound connections; this is the mirror - the device is the SSH
 * *client*, dialing OUT to a relay and asking it to forward a port back. That is how a device behind
 * NAT / a firewall stays reachable: it holds a `tcpip-forward` (RFC 4254 §7.1, the `ssh -R` seam) to
 * a relay with a public address, and a connection to the relay's forwarded port is tunnelled back to
 * a service on the device - the standards-track "secure machine bridge over untrusted networks".
 *
 * ── What it negotiates ─────────────────────────────────────────────────────────────────────
 *   kex     : mlkem768x25519-sha256 (PQ/T hybrid, PC_ENABLE_PQC_KEX), curve25519-sha256,
 *             ecdh-sha2-nistp256, diffie-hellman-group14-sha256
 *   hostkey : ssh-ed25519, ecdsa-sha2-nistp256, rsa-sha2-512, rsa-sha2-256 - verified against a pin
 *   cipher  : chacha20-poly1305@openssh.com, aes256-gcm@openssh.com, aes256-ctr (+ hmac-sha2-256/512,
 *             ETM and E&M)
 *   auth    : publickey, ssh-ed25519 (RFC 4252 §7) - the device signs with its own key
 *
 * The client offers the full suite and negotiates whatever the relay supports (RFC 4253 §7.1 order),
 * so it interoperates with any modern SSH server without being kneecapped to one algorithm. It reuses
 * the transport primitives the server already ships (the curve/DH/ECDH KEX cores, ML-KEM-768, ed25519
 * / ECDSA / RSA verify, chacha-poly / AES-GCM / AES-CTR, the RFC 4253 §7.2 KDF, and the role-aware
 * binary packet layer), so this file is the client-role state machine only, not a second crypto stack.
 *
 * ── Security ───────────────────────────────────────────────────────────────────────────────
 * The relay's host key is **pinned** by fingerprint: the caller supplies the SHA-256 of the expected
 * host-key blob (K_S), and the handshake aborts if the relay presents anything else (no
 * trust-on-first-use, no accepting an unknown key). Pinning the fingerprint rather than a raw key is
 * host-key-type agnostic - it works whether the relay's key is ed25519, ECDSA or RSA. The device
 * authenticates with an ssh-ed25519 key of its own, whose public half the relay carries in
 * `authorized_keys`.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_CLIENT_H
#define PROTOCORE_SSH_CLIENT_H

#include "protocore_config.h"

#if PC_ENABLE_SSH_CLIENT

/** @brief How to reach the relay, who to log in as, and what to tunnel back. */
typedef struct
{
    const char *host;         ///< relay hostname or dotted-quad.
    uint16_t port;            ///< relay SSH port (0 => 22).
    const char *user;         ///< SSH username on the relay.
    const uint8_t *auth_seed; ///< 32-byte ssh-ed25519 private seed the device authenticates with.
    const uint8_t *host_pin;  ///< 32-byte SHA-256 of the relay's host-key blob (K_S); handshake aborts on mismatch.
    const char *bind_addr;    ///< address the relay binds the forward on ("" / null => "" = all, "localhost", ...).
    uint16_t bind_port;       ///< remote port the relay listens on (tcpip-forward); connections there tunnel back.
    uint16_t local_port;      ///< local TCP port a tunnelled connection is bridged to (e.g. 80).
} pc_ssh_tunnel_cfg;

/** @brief Lifecycle phase of the tunnel, for observability. */
typedef enum PROTO_ENUM_PACKED
{
    PC_TUN_IDLE = 0,   ///< not started.
    PC_TUN_CONNECTING, ///< TCP + SSH handshake + auth in progress.
    PC_TUN_UP,         ///< authenticated and the remote forward is established.
    PC_TUN_FAILED      ///< the last attempt failed (host-key mismatch, auth, or transport).
} pc_ssh_tunnel_state;

/**
 * @brief Start (or restart) the tunnel: connect to the relay, handshake, authenticate, and request
 *        the remote forward. Non-blocking after the initial connect; drive it with poll().
 * @return true if the connection and handshake started; false on bad args or immediate failure.
 *
 * @warning Call begin() and poll() from the SAME task, and give that task enough stack for the
 * negotiated KEX. The handshake's field arithmetic runs in the caller's task: curve25519/ed25519
 * peak ~10.5 KB, and the mlkem768x25519 hybrid (PC_ENABLE_PQC_KEX) adds ML-KEM-768 for ~16 KB total.
 * The Arduino loop() task's default 8 KB is NOT enough - run the tunnel from a dedicated task created
 * with a >= 20480-byte stack (see the example). begin() claims a private scratch arena for the calling
 * task, so poll() must run in that same task or the packet-decrypt tripwire fires.
 */
proto_bool pc_ssh_tunnel_begin(const pc_ssh_tunnel_cfg *cfg);

/**
 * @brief Pump the tunnel: advance the handshake, service the relay's keepalives, accept
 *        forwarded-tcpip channels and bridge their bytes to/from the local service. Call every loop,
 *        from the same (adequately-stacked) task that called begin() - see the begin() @warning.
 */
void pc_ssh_tunnel_poll(void);

/** @brief Tear the tunnel down and close the relay connection. */
void pc_ssh_tunnel_end(void);

/** @brief Current lifecycle state. */
pc_ssh_tunnel_state pc_ssh_tunnel_state_get(void);

/** @brief True once authenticated and the remote forward is live. */
proto_bool pc_ssh_tunnel_up(void);

/**
 * @brief Derive the ssh-ed25519 public key (32 bytes) from a private @p seed.
 *
 * Convenience for provisioning: print/serve this so it can be added to the relay's `authorized_keys`
 * (as `ssh-ed25519 <base64(0x0000000b "ssh-ed25519" 0x00000020 <pub>)>`).
 */
void pc_ssh_tunnel_pubkey(const uint8_t seed[32], uint8_t pub[32]);

#endif // PC_ENABLE_SSH_CLIENT
#endif // PROTOCORE_SSH_CLIENT_H
