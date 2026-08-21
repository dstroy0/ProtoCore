// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_NTLMSSP_H
#define PROTOCORE_NTLMSSP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file ntlmssp.h
 * @brief NTLMSSP message codec (MS-NLMP §2.2.1) for the SMB2 client (PROTOCORE_ENABLE_SMB).
 *
 * The three-message NTLM handshake tokens that carry the NTLMv2 response (ntlm.h) inside SMB2
 * SESSION_SETUP: the client sends a NEGOTIATE (type 1), the server replies with a CHALLENGE
 * (type 2) carrying the 8-byte server challenge + the target-info AV_PAIRs, and the client sends
 * an AUTHENTICATE (type 3) carrying the NtChallengeResponse and the user/domain. All fields
 * little-endian; text is UTF-16LE. Pure, zero heap. This builds the raw NTLMSSP tokens; the
 * SPNEGO/GSS wrapping + the SESSION_SETUP framing are the next increment.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_NTLMSSP_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief NTLMSSP NegotiateFlags (MS-NLMP §2.2.2.5), the subset a basic NTLMv2 client uses. */
#define NTLMSSP_NEGOTIATE_UNICODE 0x00000001
#define NTLMSSP_REQUEST_TARGET 0x00000004
#define NTLMSSP_NEGOTIATE_NTLM 0x00000200
#define NTLMSSP_NEGOTIATE_ALWAYS_SIGN 0x00008000
#define NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY 0x00080000
#define NTLMSSP_NEGOTIATE_TARGET_INFO 0x00800000
#define NTLMSSP_NEGOTIATE_VERSION 0x02000000
#define NTLMSSP_NEGOTIATE_128 0x20000000
#define NTLMSSP_NEGOTIATE_56 0x80000000

/** @brief The NEGOTIATE flag set an NTLMv2 client sends. */
#define NTLMSSP_CLIENT_DEFAULT_FLAGS                                                                                   \
    (NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_REQUEST_TARGET | NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_ALWAYS_SIGN |     \
     NTLMSSP_NEGOTIATE_EXTENDED_SESSIONSECURITY)

/**
 * @brief Offset of the 16-byte MIC field within an AUTHENTICATE_MESSAGE built @p with_mic (MS-NLMP
 *        §2.2.1.3): the 64-byte fixed header + the 8-byte Version field. The caller writes the computed
 *        MIC here after the message is built and the digest taken over it with these bytes zeroed.
 */
#define PROTOCORE_NTLMSSP_MIC_OFFSET 72

/** @brief Length of the AUTHENTICATE MIC field (an HMAC-MD5 digest). */
#define PROTOCORE_NTLMSSP_MIC_LEN 16

/** @brief Parsed CHALLENGE_MESSAGE (type 2). @ref target_info points INTO the source message. */
typedef struct
{
    uint32_t flags;
    uint8_t server_challenge[8];
    const uint8_t *target_info; ///< the AV_PAIR blob, or nullptr if absent
    uint16_t target_info_len;
} NtlmChallenge;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*build_negotiate)(uint8_t *, uint8_t *, size_t, uint32_t);
    proto_bool (*parse_challenge)(uint8_t *, const uint8_t *, size_t, NtlmChallenge *);
    size_t (*build_authenticate)(uint8_t *, uint8_t *, size_t, const uint8_t *, size_t, const uint8_t *, size_t,
                                 const char *, const char *, const char *, uint32_t, proto_bool);
} NtlmsspNs;
PROTOCORE_NS_LAYOUT(NtlmsspNs, build_negotiate, parse_challenge, build_authenticate);

/**
 * @brief Build a NEGOTIATE_MESSAGE (type 1) with flags and empty .
 * @param work PROTOCORE_NTLMSSP_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param flags Flags
 * @return The size_t.
 */
size_t protocore_ntlmssp_build_negotiate(uint8_t *work, uint8_t *buf, size_t cap, uint32_t flags);
/**
 * @brief Parse a CHALLENGE_MESSAGE (type 2): extract the flags, the 8-byte .
 * @param work PROTOCORE_NTLMSSP_BORROW bytes the caller took. Not held past the call.
 * @param msg Msg
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_ntlmssp_parse_challenge(uint8_t *work, const uint8_t *msg, size_t len, NtlmChallenge *out);
/**
 * @brief Build an AUTHENTICATE_MESSAGE (type 3) carrying the LM + NT .
 * @param work PROTOCORE_NTLMSSP_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param cap Cap
 * @param lm_resp / lm_len the LM(v2) response (may be null/0)
 * @param lm_len Lm len
 * @param nt_resp / nt_len the NtChallengeResponse from protocore_ntlm_v2_response
 * @param nt_len Nt len
 * @param domain / user / workstation ASCII/UTF-8 identity strings (encoded UTF-16LE); user and domain are
 * @param user User
 * @param workstation Workstation
 * @param flags the NegotiateFlags to echo (usually the server's from the CHALLENGE)
 * @param with_mic when true, reserve the 8-byte Version + 16-byte MIC fields between the fixed header and the
 * @return The size_t.
 */
size_t protocore_ntlmssp_build_authenticate(uint8_t *work, uint8_t *buf, size_t cap, const uint8_t *lm_resp,
                                            size_t lm_len, const uint8_t *nt_resp, size_t nt_len, const char *domain,
                                            const char *user, const char *workstation, uint32_t flags,
                                            proto_bool with_mic);

/** @brief Module namespace. */
PROTOCORE_NS NtlmsspNs Ntlmssp PROTOCORE_UNUSED = {.build_negotiate = protocore_ntlmssp_build_negotiate,
                                                   .parse_challenge = protocore_ntlmssp_parse_challenge,
                                                   .build_authenticate = protocore_ntlmssp_build_authenticate};

PROTOCORE_END_DECLS

#endif // PROTOCORE_NTLMSSP_H
