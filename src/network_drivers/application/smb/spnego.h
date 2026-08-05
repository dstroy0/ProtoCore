// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file spnego.h
 * @brief SPNEGO (RFC 4178) GSS-API wrapping of the NTLMSSP tokens for the SMB2 client
 *        (PC_ENABLE_SMB).
 *
 * SMB2 SESSION_SETUP carries the NTLM handshake tokens inside a SPNEGO negotiation token. This is
 * the minimal ASN.1 DER layer that a client needs:
 *  - the first client token is a GSS-API InitialContextToken: `[APPLICATION 0] { SPNEGO-OID,
 *    NegTokenInit [0] { mechTypes [0] { NTLM-OID }, mechToken [2] OCTET STRING(NTLMSSP NEGOTIATE) } }`;
 *  - the server replies with a bare NegTokenResp `[1] { ..., responseToken [2] OCTET STRING(NTLMSSP
 *    CHALLENGE) }`, from which the client extracts the CHALLENGE;
 *  - the client's second token is a NegTokenResp `[1] { responseToken [2] OCTET STRING(NTLMSSP
 *    AUTHENTICATE) }`.
 *
 * Pure DER, zero heap, definite-length only. The NTLM tokens come from ntlmssp.h.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SPNEGO_H
#define PROTOCORE_SPNEGO_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SMB

/**
 * @brief Wrap an NTLMSSP NEGOTIATE token in a SPNEGO GSS-API InitialContextToken (the first
 *        SESSION_SETUP security buffer).
 * @return the token length written to @p out, or 0 on overflow.
 */
size_t pc_spnego_wrap_negotiate(const uint8_t *ntlm, size_t pc_ntlm_len, uint8_t *out, size_t cap);

/**
 * @brief Extract the responseToken (the NTLMSSP CHALLENGE) from a server NegTokenResp.
 * @param pc_resp_token receives a pointer INTO @p blob; @p pc_resp_len its length.
 * @return true if a `[2]` responseToken OCTET STRING was found and is within bounds.
 */
proto_bool pc_spnego_parse_response(const uint8_t *blob, size_t len, const uint8_t **pc_resp_token,
                                    size_t *pc_resp_len);

/**
 * @brief Wrap an NTLMSSP AUTHENTICATE token in a SPNEGO NegTokenResp (the second SESSION_SETUP
 *        security buffer).
 * @return the token length written to @p out, or 0 on overflow.
 */
size_t pc_spnego_wrap_authenticate(const uint8_t *ntlm, size_t pc_ntlm_len, uint8_t *out, size_t cap);

#endif // PC_ENABLE_SMB

PROTO_END_DECLS

#endif // PROTOCORE_SPNEGO_H
