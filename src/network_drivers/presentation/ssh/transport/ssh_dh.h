// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_dh.h
 * @brief DH-group14-SHA256 key exchange (RFC 4253 §8 + RFC 8268).
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * PROTOCOL FLOW (server side)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  Client                          Server
 *  ──────                          ──────
 *  SSH_MSG_KEXDH_INIT  ──e──►    SshDh.generate(slot):
 *                                   y  = random 2048-bit scalar
 *                                   f  = 2^y mod p        (server public)
 *                                   (y, f stored in ssh_dh[slot])
 *
 *                                 SshKex.dh_handle(slot, e):
 *                                   validate e: 1 < e < p-1
 *                                   K  = e^y mod p        (shared secret)
 *                                   H  = SHA256(V_C||V_S||I_C||I_S||K_S||e||f||K)
 *                                   sig = RSA-SHA2-256(host_key, H)
 *                          ◄──────  SSH_MSG_KEXDH_REPLY (K_S, f, sig)
 *                                   y zeroed; K → key derivation → K zeroed
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * SECURITY NOTES
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * 1. Private scalar y is generated fresh for each connection.  It is stored
 *    in ssh_dh[slot].y and zeroed by ssh_dh_wipe() (in ssh_keymat.h) as
 *    soon as K is derived.  NEVER reuse y.
 *
 * 2. The received value e is validated (1 < e < p-1) before any computation.
 *    A value of 1 or p-1 is a known small-subgroup attack; reject both
 *    (RFC 4253 §8 requires rejection of e outside [2, p-2]).
 *
 * 3. The exchange hash H is computed over all four handshake strings
 *    (V_C, V_S, I_C, I_S), the host key blob K_S, and the DH values.
 *    Omitting any field or reordering them breaks the binding between the
 *    cryptographic material and the identity of both sides.
 *
 * 4. Key material derivation follows RFC 4253 §7.2.  K and H are the only
 *    inputs; the session_id equals H from the first KEX.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_DH_H
#define PROTOCORE_SSH_DH_H

#include "crypto/asymmetric/bignum.h"
#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"
#include "protocore_config.h"

PROTO_BEGIN_DECLS

// ---------------------------------------------------------------------------
// DH key exchange
// ---------------------------------------------------------------------------

/** @brief Max bytes SshDh.kdf_derive() can produce (4 SHA-256 blocks). */
#define SSH_KDF_MAX (4 * PC_SHA256_DIGEST_LEN)

/**
 * @brief The Diffie-Hellman arithmetic and the key derivation that follows it (RFC 4253 sec 7.2).
 *
 * @var SshDhNs::generate         Generate the server ephemeral DH key pair for connection slot @p i
 * @var SshDhNs::derive_keys      Derive the six session keys from shared secret K and exchange hash H
 * @var SshDhNs::derive_keys_sid  Derive session keys with an explicit session id (RFC 4253 §7.2)
 * @var SshDhNs::kdf_derive       RFC 4253 §7.2 key derivation for any length up to @ref SSH_KDF_MAX
 */
typedef struct
{
    int (*generate)(uint8_t i);
    void (*derive_keys)(uint8_t i, const uint8_t K_be[256], const uint8_t H[PC_SHA256_DIGEST_LEN]);
    void (*derive_keys_sid)(uint8_t i, const uint8_t K_be[256], const uint8_t *H, const uint8_t *session_id,
                            uint8_t cipher_alg, uint8_t mac_alg, proto_bool k_is_string, size_t h_len, size_t sid_len,
                            proto_bool is512);
    void (*kdf_derive)(const uint8_t K_be[256], const uint8_t *H, const uint8_t *session_id, char label, uint8_t *out,
                       size_t out_len, proto_bool k_is_string, size_t h_len, size_t sid_len, proto_bool is512);
} SshDhNs;

/** @brief The one symbol this module exports. */
extern const SshDhNs SshDh;

PROTO_END_DECLS

#endif // PROTOCORE_SSH_DH_H
