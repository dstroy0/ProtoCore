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
 * per-message Finished MAC (sec 4.4.4).
 *
 * RFC 8446 sec 7.1 keys the whole schedule off the negotiated cipher suite's hash, so a schedule
 * binds SHA-256 or SHA-384 at @ref Tls13KsNs::early and every secret is 32 or 48 bytes from there on.
 * The term layout is stated at the wider of the two (@ref TLS13_SECRET_MAX) so a connection's storage
 * does not depend on what it negotiates, and the length actually in force is read back off @ref
 * Tls13KsNs::len rather than assumed.
 *
 * The schedule is transcript-hash-driven: each step takes a Transcript-Hash over the handshake
 * messages so far, so this module has no dependency on the message wire formats and is host-testable
 * in isolation against the RFC 8448 sec 3 worked trace (which lists every intermediate secret and the
 * (EC)DHE input directly). RFC 8446 sec 4.4.1 runs that hash under the same suite hash as the
 * schedule, so @ref Tls13KsNs::transcript_init and its two companions keep it here, in a borrow the
 * caller owns, and a driver never names a hash to keep a transcript. The QUIC packet-protection keys ({key, iv, hp})
 * are then derived from these traffic secrets by QuicCrypto.keys_from_secret (RFC 9001 sec 5.1).
 *
 * Pure, zero heap, host-tested against RFC 8448 sec 3.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TLS_KEY_SCHEDULE_H
#define PROTOCORE_TLS_KEY_SCHEDULE_H
#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if (PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_ENABLE_TLS)

PROTOCORE_BEGIN_DECLS

// Shared by the HTTP/3 (QUIC) handshake and the DTLS 1.3 handshake - both run the same TLS 1.3 key
// schedule (see protocore_tls13_msg.h for the matching guard on the message layer).

/** @brief Longest secret the two schedule hashes produce (SHA-384). The term layout is stated at this
 *  width; the length a schedule actually derives is @ref Tls13KsNs::len. */
#define TLS13_SECRET_MAX PROTOCORE_TLS13_SECRET_MAX

/** @brief SHA-256 secret length: what a TLS_AES_128_GCM_SHA256 schedule binds. */
#define TLS13_SECRET_SHA256 32

/** @brief SHA-384 secret length: what a TLS_AES_256_GCM_SHA384 schedule binds. */
#define TLS13_SECRET_SHA384 48

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
// Offsets into Tls13KeySchedule::s. Each term is one TLS13_SECRET_MAX slot, so a term sits at the
// same address whichever hash the schedule bound and only the bytes written inside it differ.
#define TLS13_KS_EARLY 0                             ///< HKDF-Extract(0, PSK|0) - no-PSK: Extract(0, 0^L)
#define TLS13_KS_HANDSHAKE TLS13_SECRET_MAX          ///< HKDF-Extract(Derive(early,"derived"), (EC)DHE)
#define TLS13_KS_MASTER (2 * TLS13_SECRET_MAX)       ///< HKDF-Extract(Derive(handshake,"derived"), 0^L)
#define TLS13_KS_CLIENT_HS (3 * TLS13_SECRET_MAX)    ///< Derive-Secret(handshake, "c hs traffic", CH..SH)
#define TLS13_KS_SERVER_HS (4 * TLS13_SECRET_MAX)    ///< Derive-Secret(handshake, "s hs traffic", CH..SH)
#define TLS13_KS_CLIENT_AP (5 * TLS13_SECRET_MAX)    ///< Derive-Secret(master, "c ap traffic", CH..SFIN)
#define TLS13_KS_SERVER_AP (6 * TLS13_SECRET_MAX)    ///< Derive-Secret(master, "s ap traffic", CH..SFIN)
#define TLS13_KS_EMPTY_HASH (7 * TLS13_SECRET_MAX)   ///< Transcript-Hash("")
#define TLS13_KS_DERIVED (8 * TLS13_SECRET_MAX)      ///< Derive-Secret(X, "derived", "")
#define TLS13_KS_FINISHED_KEY (9 * TLS13_SECRET_MAX) ///< HKDF-Expand-Label(traffic, "finished", "", L)
#define TLS13_KS_ZEROS (10 * TLS13_SECRET_MAX)       ///< 0^Hash.length, never written: the borrow arrives zeroed
#define TLS13_KS_VERIFY (11 * TLS13_SECRET_MAX)      ///< the Finished verify_data this end built or expects
#define PROTOCORE_TLS13_KS_CAP ((size_t)PROTOCORE_TLS13_KS_TERMS * TLS13_SECRET_MAX)
#define TLS13_KS_WORK PROTOCORE_TLS13_KS_CAP ///< past the terms: the bytes this schedule's HKDF works out of

typedef struct
{
    const Tls13Kdf *kdf; ///< variant (label prefix) bound by @ref Tls13KsNs::early
    uint8_t *s;          ///< PROTOCORE_TLS13_KS_BORROW secure bytes: the terms, then the HKDF's own
    size_t len;          ///< the bound hash's secret length, 32 or 48
    proto_bool is384;    ///< true when the suite's hash is SHA-384
} Tls13KeySchedule;

/** @brief The variant a call runs under, and the schedule it advances. */
typedef struct
{
    const Tls13Kdf *kdf;  ///< variant (label prefix) an early step binds
    Tls13KeySchedule *ks; ///< the schedule a step advances
    uint8_t *s;           ///< PROTOCORE_TLS13_KS_BORROW secure bytes the schedule runs out of
    proto_bool is384;     ///< the negotiated suite's hash: true for SHA-384, false for SHA-256
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
    uint8_t *out;                   ///< @ref Tls13KsNs::len bytes of verify_data
} Tls13FinishedArgs;

/** @brief RFC 8446 sec 4.4.1 Transcript-Hash: the bytes one update absorbs, and where a peek lands. */
typedef struct
{
    const uint8_t *data; ///< the handshake message bytes absorbed, header included
    size_t len;          ///< how many
    uint8_t *out;        ///< where a peek writes @ref Tls13KsNs::len octets of digest
} Tls13TranscriptArgs;

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
 * @var Tls13KsNs::len             the bound hash's secret length, 32 or 48, from @ref Tls13KsNs::early
 * @var Tls13KsNs::expand_label    HKDF-Expand-Label under the bound variant's prefix and hash
 * @var Tls13KsNs::derive_secret   Derive-Secret: Expand-Label over a transcript hash, @c len bytes out
 * @var Tls13KsNs::early           step 1: bind the variant, the hash and the borrow, then early_secret
 * @var Tls13KsNs::handshake       step 2: handshake_secret and the handshake traffic secrets
 * @var Tls13KsNs::master          step 3: master_secret and the application traffic secrets
 * @var Tls13KsNs::finished_mac    the Finished verify_data (sec 4.4.4)
 * @var Tls13KsNs::transcript_args  the bytes one update absorbs, and where a peek lands
 * @var Tls13KsNs::transcript_init  start a Transcript-Hash under the bound hash, in @c work
 * @var Tls13KsNs::transcript_update  absorb one handshake message into it
 * @var Tls13KsNs::transcript_peek  the digest so far, without disturbing the running context
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
    Tls13TranscriptArgs transcript_args;
    proto_bool ok;
    size_t len;
} Tls13KsVars;

/** @brief The operands and the outcome. */
extern Tls13KsVars Tls13KsV;

/** @brief The entries. */
typedef struct
{
    void (*const expand_label)(uint8_t *restrict work);
    void (*const derive_secret)(uint8_t *restrict work);
    void (*const early)(uint8_t *restrict work);
    void (*const handshake)(uint8_t *restrict work);
    void (*const master)(uint8_t *restrict work);
    void (*const finished_mac)(uint8_t *restrict work);
    void (*const transcript_init)(uint8_t *restrict work);
    void (*const transcript_update)(uint8_t *restrict work);
    void (*const transcript_peek)(uint8_t *restrict work);
} Tls13KsNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Tls13KsV or a region of the borrow at a fixed offset.
void protocore_tls13_ks_expand_label(uint8_t *restrict work);
void protocore_tls13_ks_derive_secret(uint8_t *restrict work);
void protocore_tls13_ks_early(uint8_t *restrict work);
void protocore_tls13_ks_handshake(uint8_t *restrict work);
void protocore_tls13_ks_master(uint8_t *restrict work);
void protocore_tls13_ks_finished_mac(uint8_t *restrict work);
void protocore_tls13_ks_transcript_init(uint8_t *restrict work);
void protocore_tls13_ks_transcript_update(uint8_t *restrict work);
void protocore_tls13_ks_transcript_peek(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Tls13Ks.expand_label(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Tls13KsNs Tls13Ks __attribute__((unused)) = {
    .expand_label = protocore_tls13_ks_expand_label,
    .derive_secret = protocore_tls13_ks_derive_secret,
    .early = protocore_tls13_ks_early,
    .handshake = protocore_tls13_ks_handshake,
    .master = protocore_tls13_ks_master,
    .finished_mac = protocore_tls13_ks_finished_mac,
    .transcript_init = protocore_tls13_ks_transcript_init,
    .transcript_update = protocore_tls13_ks_transcript_update,
    .transcript_peek = protocore_tls13_ks_transcript_peek,
};

#endif // PROTOCORE_ENABLE_HTTP3 || PROTOCORE_ENABLE_DTLS || PROTOCORE_ENABLE_TLS

PROTOCORE_END_DECLS

#endif // PROTOCORE_TLS_KEY_SCHEDULE_H
