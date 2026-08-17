// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file key_schedule.h
 * @brief TLS 1.3 key schedule (RFC 8446 sec 7.1) for the QUIC handshake.
 *
 * QUIC runs TLS 1.3 as its handshake protocol (RFC 9001), and mbedTLS exposes no QUIC-TLS callback
 * API, so the handshake is hand-rolled here. This module is the key schedule: the chain of
 * HKDF-Extract and Derive-Secret steps (RFC 8446 sec 7.1) that turns the (EC)DHE shared secret and
 * the running handshake transcript hash into the traffic secrets for each encryption level, plus the
 * per-message Finished MAC (sec 4.4.4). It is cipher-suite TLS_AES_128_GCM_SHA256 only, so the hash
 * is SHA-256 throughout and every secret is 32 bytes.
 *
 * The schedule is transcript-hash-driven: each step takes a Transcript-Hash the caller computed over
 * the handshake messages so far, so this module has no dependency on the message wire formats and is
 * host-testable in isolation against the RFC 8448 sec 3 worked trace (which lists every intermediate
 * secret and the (EC)DHE input directly). The QUIC packet-protection keys ({key, iv, hp}) are then
 * derived from these traffic secrets by protocore_quic_keys_from_secret() (RFC 9001 sec 5.1).
 *
 * Pure, zero heap, host-tested against RFC 8448 sec 3.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TLS_KEY_SCHEDULE_H
#define PROTOCORE_TLS_KEY_SCHEDULE_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_TLS_SOFTWARE)

PROTOCORE_BEGIN_DECLS

// Shared by the HTTP/3 (QUIC) handshake and the DTLS 1.3 handshake - both run the same TLS 1.3 key
// schedule (see protocore_tls13_msg.h for the matching guard on the message layer).

/** @brief SHA-256 secret length; every TLS 1.3 secret here is 32 bytes. */
#define TLS13_SECRET_LEN PROTOCORE_TLS13_SECRET_LEN

/**
 * @brief The one thing that differs between the TLS 1.3 and DTLS 1.3 key schedules: the
 * HKDF-Expand-Label prefix ("tls13 " for TLS/QUIC per RFC 8446 sec 7.1, "dtls13" for DTLS 1.3 per
 * RFC 9147 sec 5.9). A caller picks the variant once (@ref TLS13_KDF or @ref DTLS13_KDF) and the
 * key schedule carries it, so no per-call flag is threaded through the derivation steps.
 */
typedef struct
{
    const char *label_prefix;
} Tls13Kdf;

/** @brief TLS 1.3 / QUIC variant ("tls13 " prefix, RFC 8446). */
extern const Tls13Kdf TLS13_KDF;
/** @brief DTLS 1.3 variant ("dtls13" prefix, RFC 9147 sec 5.9). */
extern const Tls13Kdf DTLS13_KDF;

/**
 * @brief The running key-schedule state for one handshake (server side).
 *
 * Filled in three steps as the handshake progresses: @ref Tls13KsNs::early (which also binds the @ref
 * Tls13Kdf variant) before any (EC)DHE, @ref Tls13KsNs::handshake once ClientHello..ServerHello is hashed
 * and the shared secret is known, and @ref Tls13KsNs::master once ClientHello..server Finished is hashed.
 * Each step also derives that level's client and server traffic secrets, from which the record/packet
 * keys are made.
 */
// Offsets into Tls13KeySchedule::s. Each term is one TLS13_SECRET_LEN value.
#define TLS13_KS_EARLY 0                             ///< HKDF-Extract(0, PSK|0) - no-PSK: Extract(0, 0^32)
#define TLS13_KS_HANDSHAKE TLS13_SECRET_LEN          ///< HKDF-Extract(Derive(early,"derived"), (EC)DHE)
#define TLS13_KS_MASTER (2 * TLS13_SECRET_LEN)       ///< HKDF-Extract(Derive(handshake,"derived"), 0^32)
#define TLS13_KS_CLIENT_HS (3 * TLS13_SECRET_LEN)    ///< Derive-Secret(handshake, "c hs traffic", CH..SH)
#define TLS13_KS_SERVER_HS (4 * TLS13_SECRET_LEN)    ///< Derive-Secret(handshake, "s hs traffic", CH..SH)
#define TLS13_KS_CLIENT_AP (5 * TLS13_SECRET_LEN)    ///< Derive-Secret(master, "c ap traffic", CH..SFIN)
#define TLS13_KS_SERVER_AP (6 * TLS13_SECRET_LEN)    ///< Derive-Secret(master, "s ap traffic", CH..SFIN)
#define TLS13_KS_EMPTY_HASH (7 * TLS13_SECRET_LEN)   ///< Transcript-Hash("")
#define TLS13_KS_DERIVED (8 * TLS13_SECRET_LEN)      ///< Derive-Secret(X, "derived", "")
#define TLS13_KS_FINISHED_KEY (9 * TLS13_SECRET_LEN) ///< HKDF-Expand-Label(traffic, "finished", "", L)
#define TLS13_KS_ZEROS (10 * TLS13_SECRET_LEN)       ///< 0^Hash.length, never written: the borrow arrives zeroed
#define TLS13_KS_VERIFY (11 * TLS13_SECRET_LEN)      ///< the Finished verify_data this end built or expects
#define PROTOCORE_TLS13_KS_CAP ((size_t)PROTOCORE_TLS13_KS_TERMS * TLS13_SECRET_LEN)
#define TLS13_KS_WORK PROTOCORE_TLS13_KS_CAP ///< past the terms: the bytes this schedule's HKDF works out of

typedef struct
{
    const Tls13Kdf *kdf; ///< variant (label prefix) bound by @ref Tls13KsNs::early
    uint8_t *s;          ///< PROTOCORE_TLS13_KS_BORROW secure bytes: the terms, then the HKDF's own
} Tls13KeySchedule;

/** @brief The variant a call runs under, and the schedule it advances. */
typedef struct
{
    const Tls13Kdf *kdf;  ///< variant (label prefix) an early step binds
    Tls13KeySchedule *ks; ///< the schedule a step advances
    uint8_t *s;           ///< PROTOCORE_TLS13_KS_BORROW secure bytes the schedule runs out of
} Tls13KsBind;

/** @brief RFC 8446 sec 7.1 HKDF-Expand-Label / Derive-Secret: one derivation's terms. */
typedef struct
{
    uint8_t *work;                  ///< PROTOCORE_HKDF_BORROW bytes of caller storage
    const uint8_t *secret;          ///< a 32-byte PRK or traffic secret
    const char *label;              ///< short label without the prefix, e.g. "c hs traffic", "derived"
    const uint8_t *transcript_hash; ///< Transcript-Hash of the relevant messages; H("") for "derived"
    uint8_t *out;                   ///< where the derived bytes land
    size_t out_len;                 ///< how many
} Tls13KsDeriveArgs;

/** @brief RFC 8446 sec 7.1 steps 2 and 3: the (EC)DHE input, and the transcript each level is keyed off. */
typedef struct
{
    const uint8_t *ecdhe;        ///< the (EC)DHE shared secret
    size_t ecdhe_len;            ///< 32 for X25519, 64 for the X25519MLKEM768 hybrid
    const uint8_t *ch_sh_hash;   ///< Transcript-Hash of ClientHello..ServerHello
    const uint8_t *ch_sfin_hash; ///< Transcript-Hash of ClientHello..server Finished
} Tls13KsStepArgs;

/** @brief RFC 8446 sec 4.4.4: what the Finished verify_data is taken over, and where it lands. */
typedef struct
{
    const uint8_t *base_secret;     ///< the Finished sender's handshake traffic secret
    const uint8_t *transcript_hash; ///< the handshake up to but excluding this Finished
    uint8_t *out;                   ///< 32-byte verify_data
} Tls13FinishedArgs;

/**
 * @brief The TLS 1.3 key schedule (RFC 8446 sec 7.1).
 *
 * A caller sets the members a call takes, invokes it through ::Tls13Ks, and reads the outcome off the
 * same handle. The schedule itself is the caller's ::Tls13KeySchedule, named in @ref Tls13KsNs::bind.
 *
 * @var Tls13KsNs::bind            the variant a call runs under, and the schedule it advances
 * @var Tls13KsNs::derive_args     one derivation's terms
 * @var Tls13KsNs::step            the (EC)DHE input and the transcript each level is keyed off
 * @var Tls13KsNs::finished_args   what the Finished verify_data is taken over
 * @var Tls13KsNs::ok              a call's true/false outcome
 * @var Tls13KsNs::expand_label    HKDF-Expand-Label under the bound variant's prefix
 * @var Tls13KsNs::derive_secret   Derive-Secret: Expand-Label over a transcript hash, 32 bytes out
 * @var Tls13KsNs::early           step 1: bind the variant and the borrow, then early_secret
 * @var Tls13KsNs::handshake       step 2: handshake_secret and the handshake traffic secrets
 * @var Tls13KsNs::master          step 3: master_secret and the application traffic secrets
 * @var Tls13KsNs::finished_mac    the Finished verify_data (sec 4.4.4)
 *
 * @ref Tls13KsBind::s is bytes the CONNECTION owns and holds for exactly as long as it lives, so the
 * schedule dies with it. It must arrive zeroed: TLS13_KS_ZEROS is the first extract's IKM and nothing
 * ever writes it. A null @c s leaves @ref Tls13KsNs::early false and every later step a no-op.
 *
 * No storage member: the steps read their operands and the schedule's own borrow, and hold nothing.
 */
typedef struct
{
    Tls13KsBind bind;
    Tls13KsDeriveArgs derive_args;
    Tls13KsStepArgs step;
    Tls13FinishedArgs finished_args;

    proto_bool ok;

    void (*const expand_label)(uint8_t *restrict work);
    void (*const derive_secret)(uint8_t *restrict work);
    void (*const early)(uint8_t *restrict work);
    void (*const handshake)(uint8_t *restrict work);
    void (*const master)(uint8_t *restrict work);
    void (*const finished_mac)(uint8_t *restrict work);

} Tls13KsNs;

/** @brief The one symbol this module exports. */
extern Tls13KsNs Tls13Ks;

#endif // PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_TLS_SOFTWARE

PROTOCORE_END_DECLS

#endif // PROTOCORE_TLS_KEY_SCHEDULE_H
