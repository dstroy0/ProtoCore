// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file totp.h
 * @brief Time-based one-time passwords, RFC 6238 over RFC 4226 HOTP (PROTOCORE_ENABLE_TOTP).
 *
 * RFC 6238 sec 4.2: "TOTP = HOTP(K, T), where T is an integer and represents the number of time
 * steps between the initial counter time T0 and the current Unix time", with
 * "T = (Current Unix time - T0) / X" under the floor function. X is the time step in seconds and T0
 * the Unix time step counting starts at, both system parameters of RFC 6238 sec 4.1.
 *
 * The value under it is RFC 4226 sec 5.2 "HOTP(K,C) = Truncate(HMAC-SHA-1(K,C))": the counter C is
 * hashed high-order byte first, the DT function of RFC 4226 sec 5.3 takes a 31-bit window out of
 * the 20-byte MAC, and that number is reduced mod 10^Digit. The MAC is RFC 2104 sec 2
 * H(K XOR opad, H(K XOR ipad, C)) over the platform's SHA-1.
 *
 * The verifier walks the drift window of RFC 6238 sec 6: an OTP is accepted when it matches the
 * receipt time step or any step within a set number of steps forward or backward of it. The
 * provisioning secret arrives as base32 text (RFC 4648 sec 6) and decodes into the K every other
 * call is keyed by.
 *
 * A caller sets the members a call takes, invokes it through ::Totp, and reads the outcome off the
 * same handle. The module exports one symbol, @ref Totp; everything in totp.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_TOTP_H
#define PROTOCORE_TOTP_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_TOTP

PROTOCORE_BEGIN_DECLS

/** @brief X: the time step in seconds a zero @c step.x takes (RFC 6238 sec 4.1). */
#define PROTOCORE_TOTP_X_DEFAULT 30u

/** @brief Digit: the shortest OTP the algorithm extracts, and what a zero @c digit takes (RFC 4226 sec 5.3). */
#define PROTOCORE_TOTP_DIGIT_MIN 6u

/** @brief RFC 4226 sec 5.1 C and RFC 6238 sec 4.1 T0/X: the moving factor one OTP is computed for. */
typedef struct
{
    uint64_t counter;   ///< C: the counter HOTP hashes, high-order byte first (RFC 4226 sec 5.2)
    uint64_t unix_time; ///< the current Unix time T is taken from (RFC 6238 sec 4.2)
    uint64_t t0;        ///< T0: the Unix time step counting starts at; 0 is the epoch (RFC 6238 sec 4.1)
    uint32_t x;         ///< X: the time step in seconds; 0 takes ::PROTOCORE_TOTP_X_DEFAULT (RFC 6238 sec 4.1)
} TotpStepArgs;

/** @brief RFC 6238 sec 6: the OTP a validation judges and the clock drift it accepts around it. */
typedef struct
{
    uint32_t otp;  ///< the OTP value submitted for validation
    int32_t drift; ///< time steps of out-of-synch taken forward and backward; negative matches nothing
} TotpValidateArgs;

/** @brief RFC 4648 sec 6: the base32 provisioning secret and the buffer a decode fills with K. */
typedef struct
{
    const char *b32; ///< the base32 text a decode reads
    uint8_t *out;    ///< where the decoded K bytes land
    size_t cap;      ///< how many bytes that buffer holds
} TotpSecretArgs;

/** @brief The module's handle onto its own calls, described only in totp.c. */
struct TotpInternal;

/**
 * @brief One-time passwords: HOTP over a counter, TOTP over a time step, and the base32 secret.
 *
 * No storage member: every call reads only the members set on this handle and writes only its
 * results back. The throttling counter of RFC 4226 sec 7.3 and the "MUST NOT accept the second
 * attempt of the OTP after the successful validation" record of RFC 6238 sec 5.2 are the caller's
 * to keep, so there is no table, window or counter here to hold them.
 *
 * @var TotpNs::k       K: the shared secret every OTP is keyed by (RFC 4226 sec 5.1)
 * @var TotpNs::keylen  how many bytes K holds; over 64 it is replaced by H(K) (RFC 2104 sec 2)
 * @var TotpNs::digit   Digit: how many digits the OTP carries; 0 takes ::PROTOCORE_TOTP_DIGIT_MIN
 * @var TotpNs::step    C, and the T0/X/Unix time that fix T: set @c step.counter for a
 *                      counter-based code, @c step.unix_time / @c step.t0 / @c step.x for a
 *                      time-based one
 * @var TotpNs::check   the submitted OTP and the drift steps a validation walks around T
 * @var TotpNs::secret  the base32 text a decode reads and the buffer it writes K into
 * @var TotpNs::ok      a validation's true/false verdict
 * @var TotpNs::u32     D: the OTP a generate produced, in 0...10^Digit-1 (RFC 4226 sec 5.3)
 * @var TotpNs::i32     the number of K bytes a decode wrote, or -1 on a rejected character or a
 *                      buffer too small
 * @var TotpNs::hotp           HOTP(K,C) for @c step.counter (RFC 4226 sec 5.3)
 * @var TotpNs::totp           HOTP(K,T) for the time step T names (RFC 6238 sec 4.2)
 * @var TotpNs::verify         match @c check.otp against T and the steps around it (RFC 6238 sec 6)
 * @var TotpNs::base32_decode  base32 text to the K bytes it stands for (RFC 4648 sec 6)
 * @var TotpNs::internal       the module's handle onto the calls above
 */
typedef struct
{
    const uint8_t *k; ///< K: the shared secret every OTP is keyed by (RFC 4226 sec 5.1)
    size_t keylen;    ///< how many bytes K holds
    uint8_t digit;    ///< Digit: how many digits the OTP carries (RFC 4226 sec 5.1)

    TotpStepArgs step;      ///< the moving factor an OTP is computed for
    TotpValidateArgs check; ///< what a validation judges, and the drift around it
    TotpSecretArgs secret;  ///< the base32 secret and where a decode writes K

    proto_bool ok;
    uint32_t u32;
    int32_t i32;

    void (*hotp)(struct TotpInternal *ctx);
    void (*totp)(struct TotpInternal *ctx);
    void (*verify)(struct TotpInternal *ctx);
    void (*base32_decode)(struct TotpInternal *ctx);

    struct TotpInternal *internal;
} TotpNs;

/** @brief The one symbol this module exports. */
extern TotpNs Totp;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_TOTP

#endif // PROTOCORE_TOTP_H
