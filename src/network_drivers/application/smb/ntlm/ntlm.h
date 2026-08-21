// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_NTLM_H
#define PROTOCORE_NTLM_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file ntlm.h
 * @brief NTLMv2 response computation (MS-NLMP §3.3.2) for the SMB2 client (PROTOCORE_ENABLE_SMB).
 *
 * The auth core: from the user's password and the server's CHALLENGE (the 8-byte server challenge
 * + the target-info AV_PAIR blob), compute the NtChallengeResponse and the session base key that
 * seed SESSION_SETUP. Built on the KAT-verified MD4 / MD5 / HMAC-MD5 (crypto/hash/md.h). Pure, zero heap.
 *
 * NThash        = MD4(UTF-16LE(password))
 * NTOWFv2       = HMAC-MD5(NThash, UTF-16LE(Uppercase(user) + domain))
 * temp          = 0x01 0x01 Z(6) Time(8) ClientChallenge(8) Z(4) TargetInfo Z(4)
 * NTProofStr    = HMAC-MD5(NTOWFv2, ServerChallenge(8) + temp)
 * NtChallengeResponse = NTProofStr(16) + temp
 * SessionBaseKey = HMAC-MD5(NTOWFv2, NTProofStr)
 *
 * Verified against the MS-NLMP §4.2 worked example (test_ntlm).
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_NTLM_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    void (*nt_hash)(uint8_t *, const char *, uint8_t *);
    proto_bool (*ntowfv2)(uint8_t *, const uint8_t *, const char *, const char *, uint8_t *);
    size_t (*v2_response)(uint8_t *, const uint8_t *, const uint8_t *, const uint8_t *, const uint8_t *,
                          const uint8_t *, size_t, uint8_t *, size_t, uint8_t *);
    size_t (*set_mic_flag)(uint8_t *, const uint8_t *, size_t, uint8_t *, size_t);
    void (*mic)(uint8_t *, const uint8_t *, const uint8_t *, size_t, const uint8_t *, size_t, const uint8_t *, size_t,
                uint8_t *);
} NtlmNs;
PROTOCORE_NS_LAYOUT(NtlmNs, nt_hash, ntowfv2, v2_response, set_mic_flag, mic);

/**
 * @brief The NT hash: MD4 of the UTF-16LE password (password is ASCII/UTF-8, .
 * @param work PROTOCORE_NTLM_BORROW bytes the caller took. Not held past the call.
 * @param password Password
 * @param nt_hash 16 bytes
 */
void protocore_ntlm_nt_hash(uint8_t *work, const char *password, uint8_t *nt_hash);
/**
 * @brief NTOWFv2 = HMAC-MD5(NThash, UTF-16LE(Uppercase(user) + domain)). .
 * @param work PROTOCORE_NTLM_BORROW bytes the caller took. Not held past the call.
 * @param nt_hash 16 bytes
 * @param user User
 * @param domain Domain
 * @param owf 16 bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_ntlm_ntowfv2(uint8_t *work, const uint8_t *nt_hash, const char *user, const char *domain,
                                  uint8_t *owf);
/**
 * @brief Compute the NTLMv2 NtChallengeResponse (NTProofStr + temp) and the .
 * @param work PROTOCORE_NTLM_BORROW bytes the caller took. Not held past the call.
 * @param owf NTOWFv2 (from protocore_ntlm_ntowfv2) 16 bytes
 * @param server_challenge the 8-byte challenge from the server's CHALLENGE_MESSAGE 8 bytes
 * @param client_challenge the 8-byte client-generated challenge 8 bytes
 * @param timestamp the 8-byte little-endian FILETIME (may be zero) 8 bytes
 * @param target_info the AV_PAIR blob from the CHALLENGE_MESSAGE
 * @param ti_len Ti len
 * @param out Out
 * @param out_cap Out cap
 * @param session_key receives the 16-byte SessionBaseKey (may be null) 16 bytes
 * @return The size_t.
 */
size_t protocore_ntlm_v2_response(uint8_t *work, const uint8_t *owf, const uint8_t *server_challenge,
                                  const uint8_t *client_challenge, const uint8_t *timestamp, const uint8_t *target_info,
                                  size_t ti_len, uint8_t *out, size_t out_cap, uint8_t *session_key);
/**
 * @brief Copy the CHALLENGE target-info AV_PAIR list into out, setting the .
 * @param work PROTOCORE_NTLM_BORROW bytes the caller took. Not held past the call.
 * @param target_info Target info
 * @param ti_len Ti len
 * @param out Out
 * @param out_cap Out cap
 * @return The size_t.
 */
size_t protocore_ntlm_set_mic_flag(uint8_t *work, const uint8_t *target_info, size_t ti_len, uint8_t *out,
                                   size_t out_cap);
/**
 * @brief The NTLMSSP AUTHENTICATE MIC (MS-NLMP §3.1.5.1.2): HMAC-MD5 over .
 * @param work PROTOCORE_NTLM_BORROW bytes the caller took. Not held past the call.
 * @param session_key 16 bytes
 * @param neg Neg
 * @param neg_len Neg len
 * @param chal Chal
 * @param chal_len Chal len
 * @param auth Auth
 * @param auth_len Auth len
 * @param out 16 bytes
 */
void protocore_ntlm_mic(uint8_t *work, const uint8_t *session_key, const uint8_t *neg, size_t neg_len,
                        const uint8_t *chal, size_t chal_len, const uint8_t *auth, size_t auth_len, uint8_t *out);

/** @brief Module namespace. */
PROTOCORE_NS NtlmNs Ntlm PROTOCORE_UNUSED = {.nt_hash = protocore_ntlm_nt_hash,
                                             .ntowfv2 = protocore_ntlm_ntowfv2,
                                             .v2_response = protocore_ntlm_v2_response,
                                             .set_mic_flag = protocore_ntlm_set_mic_flag,
                                             .mic = protocore_ntlm_mic};

PROTOCORE_END_DECLS

#endif // PROTOCORE_NTLM_H
