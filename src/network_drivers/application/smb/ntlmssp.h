// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntlmssp.h
 * @brief NTLMSSP message codec (MS-NLMP §2.2.1) for the SMB2 client (PC_ENABLE_SMB).
 *
 * The three-message NTLM handshake tokens that carry the NTLMv2 response (ntlm.h) inside SMB2
 * SESSION_SETUP: the client sends a NEGOTIATE (type 1), the server replies with a CHALLENGE
 * (type 2) carrying the 8-byte server challenge + the target-info AV_PAIRs, and the client sends
 * an AUTHENTICATE (type 3) carrying the NtChallengeResponse and the user/domain. All fields
 * little-endian; text is UTF-16LE. Pure, zero heap. This builds the raw NTLMSSP tokens; the
 * SPNEGO/GSS wrapping + the SESSION_SETUP framing are the next increment.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NTLMSSP_H
#define PROTOCORE_NTLMSSP_H

#include "protocore_config.h"

#if PC_ENABLE_SMB

/** @brief NTLMSSP NegotiateFlags (MS-NLMP §2.2.2.5), the subset a basic NTLMv2 client uses.
 *
 * A flags word is OR'd/AND'd, so these are integer constants in a namespacing struct, not an enum
 * class (which would force a cast at every | / &). */
typedef struct
{
#define NTLMSSP_NEGOTIATE_UNICODE 0x00000001
#define NTLMSSP_REQUEST_TARGET 0x00000004
#define NTLMSSP_NEGOTIATE_NTLM 0x00000200
#define NTLMSSP_NEGOTIATE_ALWAYS_SIGN 0x00008000
#define NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY 0x00080000
#define NTLMSSP_NEGOTIATE_TARGET_INFO 0x00800000
#define NTLMSSP_NEGOTIATE_VERSION 0x02000000
#define _128 0x20000000
#define _56 0x80000000
    // the default NEGOTIATE flag set for an NTLMv2 client
    static constexpr uint32_t NTLMSSP_CLIENT_DEFAULT_FLAGS = NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_REQUEST_TARGET |
                                                             NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_ALWAYS_SIGN |
                                                             NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY;
} NtlmsspFlags;

/** @brief Parsed CHALLENGE_MESSAGE (type 2). @ref target_info points INTO the source message. */
typedef struct
{
    uint32_t flags;
    uint8_t server_challenge[8];
    const uint8_t *target_info; ///< the AV_PAIR blob, or nullptr if absent
    uint16_t target_info_len;
} NtlmChallenge;

/**
 * @brief Build a NEGOTIATE_MESSAGE (type 1) with @p flags and empty domain/workstation.
 * @return message length (32), or 0 if @p cap < 32.
 */
size_t pc_ntlmssp_build_negotiate(uint8_t *buf, size_t cap, uint32_t flags);

/**
 * @brief Parse a CHALLENGE_MESSAGE (type 2): extract the flags, the 8-byte server challenge, and
 *        the target-info AV_PAIRs (bounds-checked into @p msg).
 * @return true on a valid CHALLENGE; false on a bad signature / type / truncation / out-of-bounds
 *         target info.
 */
proto_bool pc_ntlmssp_parse_challenge(const uint8_t *msg, size_t len, NtlmChallenge *out);

/**
 * @brief Offset of the 16-byte MIC field within an AUTHENTICATE_MESSAGE built @p with_mic (MS-NLMP
 *        §2.2.1.3): the 64-byte fixed header + the 8-byte Version field. The caller writes the computed
 *        MIC here after the message is built and the digest taken over it with these bytes zeroed.
 */
#define PC_NTLMSSP_MIC_OFFSET 72

/** @brief Length of the AUTHENTICATE MIC field (an HMAC-MD5 digest). */
#define PC_NTLMSSP_MIC_LEN 16

/**
 * @brief Build an AUTHENTICATE_MESSAGE (type 3) carrying the LM + NT responses and the identity.
 *
 * @param lm_resp / lm_len  the LM(v2) response (may be null/0).
 * @param nt_resp / nt_len  the NtChallengeResponse from pc_ntlm_v2_response.
 * @param domain / user / workstation  ASCII/UTF-8 identity strings (encoded UTF-16LE); user
 *        and domain are typically required, workstation is optional (may be null).
 * @param flags  the NegotiateFlags to echo (usually the server's from the CHALLENGE).
 * @param with_mic  when true, reserve the 8-byte Version + 16-byte MIC fields between the fixed header
 *        and the payload (set NTLMSSP_NEGOTIATE_VERSION, MIC zeroed at ::PC_NTLMSSP_MIC_OFFSET); the
 *        caller then computes the MIC and writes it there. When false the message has no Version/MIC.
 * @return total message length, or 0 on overflow. No session-key exchange (EncryptedRandomSessionKey empty).
 */
size_t pc_ntlmssp_build_authenticate(uint8_t *buf, size_t cap, const uint8_t *lm_resp, size_t lm_len,
                                     const uint8_t *nt_resp, size_t nt_len, const char *domain, const char *user,
                                     const char *workstation, uint32_t flags, proto_bool with_mic = PROTO_FALSE);

#endif // PC_ENABLE_SMB

#endif // PROTOCORE_NTLMSSP_H
