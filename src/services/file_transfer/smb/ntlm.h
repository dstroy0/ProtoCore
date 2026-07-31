// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntlm.h
 * @brief NTLMv2 response computation (MS-NLMP §3.3.2) for the SMB2 client (PC_ENABLE_SMB).
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

#include "protocore_config.h"

#if PC_ENABLE_SMB

#include <stddef.h>
#include <stdint.h>

/** @brief The NT hash: MD4 of the UTF-16LE password (@p password is ASCII/UTF-8, NUL-terminated). */
void pc_ntlm_nt_hash(const char *password, uint8_t nt_hash[16]);

/**
 * @brief NTOWFv2 = HMAC-MD5(NThash, UTF-16LE(Uppercase(user) + domain)).
 *
 * Only the @p user is uppercased (ASCII), not the @p domain (MS-NLMP). Both are NUL-terminated.
 * @return true; false if user + domain exceed the internal 256-char scratch.
 */
bool pc_ntlm_ntowfv2(const uint8_t nt_hash[16], const char *user, const char *domain, uint8_t owf[16]);

/**
 * @brief Compute the NTLMv2 NtChallengeResponse (NTProofStr + temp) and the session base key.
 *
 * @param owf              NTOWFv2 (from pc_ntlm_ntowfv2).
 * @param server_challenge the 8-byte challenge from the server's CHALLENGE_MESSAGE.
 * @param client_challenge the 8-byte client-generated challenge.
 * @param timestamp        the 8-byte little-endian FILETIME (may be zero).
 * @param target_info      the AV_PAIR blob from the CHALLENGE_MESSAGE.
 * @param session_key      receives the 16-byte SessionBaseKey (may be null).
 * @return the NtChallengeResponse length written to @p out (48 + @p ti_len), or 0 on overflow.
 */
size_t pc_ntlm_v2_response(const uint8_t owf[16], const uint8_t server_challenge[8], const uint8_t client_challenge[8],
                           const uint8_t timestamp[8], const uint8_t *target_info, size_t ti_len, uint8_t *out,
                           size_t out_cap, uint8_t session_key[16]);

/**
 * @brief Copy the CHALLENGE target-info AV_PAIR list into @p out, setting the MsvAvFlags (AvId 6)
 *        bit 0x00000002 ("the client provides a MIC in the AUTHENTICATE", MS-NLMP §2.2.2.10 / §3.1.5.1.2).
 *
 * If an MsvAvFlags pair is already present its value is OR'd with 0x2; otherwise a new
 * `AvId=6, AvLen=4, value=0x00000002` pair is inserted immediately before the MsvAvEOL (AvId 0)
 * terminator. The resulting blob is what the NTLMv2 response is computed over, so the server sees the
 * flag and verifies the MIC. Call before ::pc_ntlm_v2_response and feed it the returned blob.
 *
 * @return the new target-info length in @p out (@p ti_len, or @p ti_len + 8 when a pair was inserted),
 *         or 0 on a null pointer / overflow / a malformed (unterminated) list.
 */
size_t pc_ntlm_set_mic_flag(const uint8_t *target_info, size_t ti_len, uint8_t *out, size_t out_cap);

/**
 * @brief The NTLMSSP AUTHENTICATE MIC (MS-NLMP §3.1.5.1.2): HMAC-MD5 over the concatenation of the
 *        three NTLM messages, keyed by the ExportedSessionKey (= the NTLMv2 SessionBaseKey when no key
 *        exchange is negotiated).
 *
 *   MIC = HMAC-MD5(session_key, NEGOTIATE_MESSAGE || CHALLENGE_MESSAGE || AUTHENTICATE_MESSAGE)
 *
 * The three are the raw NTLMSSP token bytes (not SPNEGO-wrapped); @p auth must already have its 16-byte
 * MIC field zeroed (as ::pc_ntlmssp_build_authenticate leaves it). Streams the parts, so no scratch
 * concatenation buffer is needed.
 */
void pc_ntlm_mic(const uint8_t session_key[16], const uint8_t *neg, size_t neg_len, const uint8_t *chal,
                 size_t chal_len, const uint8_t *auth, size_t auth_len, uint8_t out[16]);

#endif // PC_ENABLE_SMB

#endif // PROTOCORE_NTLM_H
