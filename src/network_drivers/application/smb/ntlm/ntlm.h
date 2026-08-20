// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntlm.h
 * @brief NTLMv2 response computation (MS-NLMP §3.3.2) for the SMB2 client (PROTOCORE_ENABLE_SMB).
 *
 * The auth core: from the user's password and the server's CHALLENGE (the 8-byte server challenge
 * + the target-info AV_PAIR blob), compute the NtChallengeResponse and the session base key that
 * seed SESSION_SETUP. Built on the KAT-verified MD4 / MD5 / HMAC-MD5 (crypto/hash/md.h). Pure, zero heap.
 *
 *   NThash        = MD4(UTF-16LE(password))
 *   NTOWFv2       = HMAC-MD5(NThash, UTF-16LE(Uppercase(user) + domain))
 *   temp          = 0x01 0x01 Z(6) Time(8) ClientChallenge(8) Z(4) TargetInfo Z(4)
 *   NTProofStr    = HMAC-MD5(NTOWFv2, ServerChallenge(8) + temp)
 *   NtChallengeResponse = NTProofStr(16) + temp
 *   SessionBaseKey = HMAC-MD5(NTOWFv2, NTProofStr)
 *
 * Verified against the MS-NLMP §4.2 worked example (test_ntlm).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NTLM_H
#define PROTOCORE_NTLM_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SMB

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief What nt_hash takes: password, nt_hash. */
typedef struct
{
    const char *password;
    uint8_t *nt_hash; ///< 16 bytes.
} NtlmNtHashArgs;
/** @brief What ntowfv2 takes: nt_hash, user, domain, owf. */
typedef struct
{
    const uint8_t *nt_hash; ///< 16 bytes.
    const char *user;
    const char *domain;
    uint8_t *owf; ///< 16 bytes.
} NtlmNtowfv2Args;
/** @brief What v2_response takes: owf, server_challenge, ... */
typedef struct
{
    const uint8_t *owf;              ///< NTOWFv2 (from protocore_ntlm_ntowfv2) 16 bytes.
    const uint8_t *server_challenge; ///< the 8-byte challenge from the server's CHALLENGE_MESSAGE 8 bytes.
    const uint8_t *client_challenge; ///< the 8-byte client-generated challenge 8 bytes.
    const uint8_t *timestamp;        ///< the 8-byte little-endian FILETIME (may be zero) 8 bytes.
    const uint8_t *target_info;      ///< the AV_PAIR blob from the CHALLENGE_MESSAGE
    size_t ti_len;
    uint8_t *out;
    size_t out_cap;
    uint8_t *session_key; ///< receives the 16-byte SessionBaseKey (may be null) 16 bytes.
} NtlmV2ResponseArgs;
/** @brief What set_mic_flag takes: target_info, ti_len, out, out_cap. */
typedef struct
{
    const uint8_t *target_info;
    size_t ti_len;
    uint8_t *out;
    size_t out_cap;
} NtlmSetMicFlagArgs;
/** @brief What mic takes: session_key, neg, neg_len, chal, chal_len, ... */
typedef struct
{
    const uint8_t *session_key; ///< 16 bytes.
    const uint8_t *neg;
    size_t neg_len;
    const uint8_t *chal;
    size_t chal_len;
    const uint8_t *auth;
    size_t auth_len;
    uint8_t *out; ///< 16 bytes.
} NtlmMicArgs;
/**
 * @brief NTLMv2 response computation (MS-NLMP §3.3.2) for the SMB2 client (PROTOCORE_ENABLE_SMB).
 *
 * A caller sets the members a call takes, invokes it through ::Ntlm with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Ntlm.nt_hash_args.password = ...;
 *   Ntlm.nt_hash_args.nt_hash = ...;
 *   Ntlm.nt_hash(work);
 *
 * @var NtlmNs::nt_hash_args  what nt_hash takes: password, nt_hash
 * @var NtlmNs::ntowfv2_args  what ntowfv2 takes: nt_hash, user, domain, owf
 * @var NtlmNs::v2_response_args  what v2_response takes: owf, server_challenge,
 * @var NtlmNs::set_mic_flag_args  what set_mic_flag takes: target_info, ti_len, out, out_cap
 * @var NtlmNs::mic_args  what mic takes: session_key, neg, neg_len, chal, chal_len,
 * @var NtlmNs::ok  true; false if user + domain exceed the internal 256-char scratch
 * @var NtlmNs::n  the NtChallengeResponse length written to out (48 + ti_len), or 0 ...
 * @var NtlmNs::nt_hash  the NT hash: MD4 of the UTF-16LE password (password is ASCII/UTF-8, ...
 * @var NtlmNs::ntowfv2  NTOWFv2 = HMAC-MD5(NThash, UTF-16LE(Uppercase(user) + domain)). ...
 * @var NtlmNs::v2_response  compute the NTLMv2 NtChallengeResponse (NTProofStr + temp) and the ...
 * @var NtlmNs::set_mic_flag  copy the CHALLENGE target-info AV_PAIR list into out, setting the ...
 * @var NtlmNs::mic  the NTLMSSP AUTHENTICATE MIC (MS-NLMP §3.1.5.1.2): HMAC-MD5 over ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    NtlmNtHashArgs nt_hash_args;
    NtlmNtowfv2Args ntowfv2_args;
    NtlmV2ResponseArgs v2_response_args;
    NtlmSetMicFlagArgs set_mic_flag_args;
    NtlmMicArgs mic_args;
    proto_bool ok;
    size_t n;
} NtlmVars;

/** @brief The operands and the outcome. */
extern NtlmVars NtlmV;

/** @brief The entries. */
typedef struct
{
    void (*const nt_hash)(uint8_t *restrict work);
    void (*const ntowfv2)(uint8_t *restrict work);
    void (*const v2_response)(uint8_t *restrict work);
    void (*const set_mic_flag)(uint8_t *restrict work);
    void (*const mic)(uint8_t *restrict work);
} NtlmNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in NtlmV or a region of the borrow at a fixed offset.
void protocore_ntlm_nt_hash(uint8_t *restrict work);
void protocore_ntlm_ntowfv2(uint8_t *restrict work);
void protocore_ntlm_v2_response(uint8_t *restrict work);
void protocore_ntlm_set_mic_flag(uint8_t *restrict work);
void protocore_ntlm_mic(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Ntlm.nt_hash(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const NtlmNs Ntlm __attribute__((unused)) = {
    .nt_hash = protocore_ntlm_nt_hash,
    .ntowfv2 = protocore_ntlm_ntowfv2,
    .v2_response = protocore_ntlm_v2_response,
    .set_mic_flag = protocore_ntlm_set_mic_flag,
    .mic = protocore_ntlm_mic,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SMB

#endif // PROTOCORE_NTLM_H
