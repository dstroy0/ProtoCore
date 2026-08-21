// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_SPNEGO_H
#define PROTOCORE_SPNEGO_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file spnego.h
 * @brief SPNEGO (RFC 4178) GSS-API wrapping of the NTLMSSP tokens for the SMB2 client
(PROTOCORE_ENABLE_SMB).
 *
 * SMB2 SESSION_SETUP carries the NTLM handshake tokens inside a SPNEGO negotiation token. This is
 * the minimal ASN.1 DER layer that a client needs:
 * - the first client token is a GSS-API InitialContextToken: `[APPLICATION 0] { SPNEGO-OID,
 * NegTokenInit [0] { mechTypes [0] { NTLM-OID }, mechToken [2] OCTET STRING(NTLMSSP NEGOTIATE) } }`;
 * - the server replies with a bare NegTokenResp `[1] { ..., responseToken [2] OCTET STRING(NTLMSSP
 * CHALLENGE) }`, from which the client extracts the CHALLENGE;
 * - the client's second token is a NegTokenResp `[1] { responseToken [2] OCTET STRING(NTLMSSP
 * AUTHENTICATE) }`.
 *
 * Pure DER, zero heap, definite-length only. The NTLM tokens come from ntlmssp.h.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_SPNEGO_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*wrap_negotiate)(uint8_t *restrict, const uint8_t *, size_t, uint8_t *, size_t);
    proto_bool (*parse_response)(uint8_t *restrict, const uint8_t *, size_t, const uint8_t **, size_t *);
    size_t (*wrap_authenticate)(uint8_t *restrict, const uint8_t *, size_t, uint8_t *, size_t);
} SpnegoNs;
PROTOCORE_NS_LAYOUT(SpnegoNs, wrap_negotiate, parse_response, wrap_authenticate);

/**
 * @brief Wrap an NTLMSSP NEGOTIATE token in a SPNEGO GSS-API .
 * @param work PROTOCORE_SPNEGO_BORROW bytes the caller took. Not held past the call.
 * @param ntlm Ntlm
 * @param protocore_ntlm_len Protocore ntlm len
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_spnego_wrap_negotiate(uint8_t *restrict work, const uint8_t *ntlm, size_t protocore_ntlm_len,
                                       uint8_t *out, size_t cap);
/**
 * @brief Extract the responseToken (the NTLMSSP CHALLENGE) from a server .
 * @param work PROTOCORE_SPNEGO_BORROW bytes the caller took. Not held past the call.
 * @param blob Blob
 * @param len Len
 * @param protocore_resp_token receives a pointer INTO blob; protocore_resp_len its length
 * @param protocore_resp_len Protocore resp len
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_spnego_parse_response(uint8_t *restrict work, const uint8_t *blob, size_t len,
                                           const uint8_t **protocore_resp_token, size_t *protocore_resp_len);
/**
 * @brief Wrap an NTLMSSP AUTHENTICATE token in a SPNEGO NegTokenResp (the .
 * @param work PROTOCORE_SPNEGO_BORROW bytes the caller took. Not held past the call.
 * @param ntlm Ntlm
 * @param protocore_ntlm_len Protocore ntlm len
 * @param out Out
 * @param cap Cap
 * @return The size_t.
 */
size_t protocore_spnego_wrap_authenticate(uint8_t *restrict work, const uint8_t *ntlm, size_t protocore_ntlm_len,
                                          uint8_t *out, size_t cap);

/** @brief Module namespace. */
PROTOCORE_NS SpnegoNs Spnego PROTOCORE_UNUSED = {.wrap_negotiate = protocore_spnego_wrap_negotiate,
                                                 .parse_response = protocore_spnego_parse_response,
                                                 .wrap_authenticate = protocore_spnego_wrap_authenticate};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SPNEGO_H
