// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

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
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NTLMSSP_H
#define PROTOCORE_NTLMSSP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SMB

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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

/** @brief What build_negotiate takes: buf, cap, flags. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint32_t flags;
} NtlmsspBuildNegotiateArgs;

/** @brief What parse_challenge takes: msg, len, out. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
    NtlmChallenge *out;
} NtlmsspParseChallengeArgs;

/** @brief What build_authenticate takes: buf, cap, lm_resp, lm_len, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const uint8_t *lm_resp; ///< / lm_len the LM(v2) response (may be null/0)
    size_t lm_len;
    const uint8_t *nt_resp; ///< / nt_len the NtChallengeResponse from protocore_ntlm_v2_response
    size_t nt_len;
    const char
        *domain; ///< / user / workstation ASCII/UTF-8 identity strings (encoded UTF-16LE); user and domain are ...
    const char *user;
    const char *workstation;
    uint32_t flags; ///< the NegotiateFlags to echo (usually the server's from the CHALLENGE)
    proto_bool
        with_mic; ///< when true, reserve the 8-byte Version + 16-byte MIC fields between the fixed header and the ...
} NtlmsspBuildAuthenticateArgs;

/**
 * @brief NTLMSSP message codec (MS-NLMP §2.2.1) for the SMB2 client (PROTOCORE_ENABLE_SMB).
 *
 * A caller sets the members a call takes, invokes it through ::Ntlmssp with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Ntlmssp.build_negotiate_args.buf = ...;
 *   Ntlmssp.build_negotiate_args.cap = ...;
 *   Ntlmssp.build_negotiate_args.flags = ...;
 *   Ntlmssp.build_negotiate(work);
 *   // Ntlmssp.n is what the call reports
 *
 * @var NtlmsspNs::build_negotiate_args  what build_negotiate takes: buf, cap, flags
 * @var NtlmsspNs::parse_challenge_args  what parse_challenge takes: msg, len, out
 * @var NtlmsspNs::build_authenticate_args  what build_authenticate takes: buf, cap, lm_resp, lm_len,
 * @var NtlmsspNs::ok  true on a valid CHALLENGE; false on a bad signature / type / ...
 * @var NtlmsspNs::n  message length (32), or 0 if cap < 32
 * @var NtlmsspNs::build_negotiate  build a NEGOTIATE_MESSAGE (type 1) with flags and empty ...
 * @var NtlmsspNs::parse_challenge  parse a CHALLENGE_MESSAGE (type 2): extract the flags, the 8-byte ...
 * @var NtlmsspNs::build_authenticate  build an AUTHENTICATE_MESSAGE (type 3) carrying the LM + NT ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    NtlmsspBuildNegotiateArgs build_negotiate_args;
    NtlmsspParseChallengeArgs parse_challenge_args;
    NtlmsspBuildAuthenticateArgs build_authenticate_args;
    proto_bool ok;
    size_t n;
} NtlmsspVars;

/** @brief The operands and the outcome. */
extern NtlmsspVars NtlmsspV;

/** @brief The entries. */
typedef struct
{
    void (*const build_negotiate)(uint8_t *restrict work);
    void (*const parse_challenge)(uint8_t *restrict work);
    void (*const build_authenticate)(uint8_t *restrict work);
} NtlmsspNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in NtlmsspV or a region of the borrow at a fixed offset.
void protocore_ntlmssp_build_negotiate(uint8_t *restrict work);
void protocore_ntlmssp_parse_challenge(uint8_t *restrict work);
void protocore_ntlmssp_build_authenticate(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Ntlmssp.build_negotiate(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const NtlmsspNs Ntlmssp __attribute__((unused)) = {
    .build_negotiate = protocore_ntlmssp_build_negotiate,
    .parse_challenge = protocore_ntlmssp_parse_challenge,
    .build_authenticate = protocore_ntlmssp_build_authenticate,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SMB

#endif // PROTOCORE_NTLMSSP_H
