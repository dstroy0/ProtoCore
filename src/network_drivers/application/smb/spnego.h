// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file spnego.h
 * @brief SPNEGO (RFC 4178) GSS-API wrapping of the NTLMSSP tokens for the SMB2 client
 *        (PROTOCORE_ENABLE_SMB).
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SMB

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief What wrap_negotiate takes: ntlm, protocore_ntlm_len, out, ... */
typedef struct
{
    const uint8_t *ntlm;
    size_t protocore_ntlm_len;
    uint8_t *out;
    size_t cap;
} SpnegoWrapNegotiateArgs;

/** @brief What parse_response takes: blob, len, protocore_resp_token, ... */
typedef struct
{
    const uint8_t *blob;
    size_t len;
    const uint8_t **protocore_resp_token; ///< receives a pointer INTO blob; protocore_resp_len its length
    size_t *protocore_resp_len;
} SpnegoParseResponseArgs;

/** @brief What wrap_authenticate takes: ntlm, protocore_ntlm_len, ... */
typedef struct
{
    const uint8_t *ntlm;
    size_t protocore_ntlm_len;
    uint8_t *out;
    size_t cap;
} SpnegoWrapAuthenticateArgs;

/**
 * @brief SPNEGO (RFC 4178) GSS-API wrapping of the NTLMSSP tokens for the SMB2 client (PROTOCORE_ENABLE_SMB).
 *
 * A caller sets the members a call takes, invokes it through ::Spnego with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Spnego.wrap_negotiate_args.ntlm = ...;
 *   Spnego.wrap_negotiate_args.protocore_ntlm_len = ...;
 *   Spnego.wrap_negotiate_args.out = ...;
 *   Spnego.wrap_negotiate_args.cap = ...;
 *   Spnego.wrap_negotiate(work);
 *   // Spnego.n is what the call reports
 *
 * @var SpnegoNs::wrap_negotiate_args  what wrap_negotiate takes: ntlm, protocore_ntlm_len, out,
 * @var SpnegoNs::parse_response_args  what parse_response takes: blob, len, protocore_resp_token,
 * @var SpnegoNs::wrap_authenticate_args  what wrap_authenticate takes: ntlm, protocore_ntlm_len,
 * @var SpnegoNs::ok  true if a `[2]` responseToken OCTET STRING was found and is within ...
 * @var SpnegoNs::n  the token length written to out, or 0 on overflow
 * @var SpnegoNs::wrap_negotiate  wrap an NTLMSSP NEGOTIATE token in a SPNEGO GSS-API ...
 * @var SpnegoNs::parse_response  extract the responseToken (the NTLMSSP CHALLENGE) from a server ...
 * @var SpnegoNs::wrap_authenticate  wrap an NTLMSSP AUTHENTICATE token in a SPNEGO NegTokenResp (the ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    SpnegoWrapNegotiateArgs wrap_negotiate_args;
    SpnegoParseResponseArgs parse_response_args;
    SpnegoWrapAuthenticateArgs wrap_authenticate_args;

    proto_bool ok;
    size_t n;

    void (*const wrap_negotiate)(uint8_t *restrict work);
    void (*const parse_response)(uint8_t *restrict work);
    void (*const wrap_authenticate)(uint8_t *restrict work);
} SpnegoNs;

/** @brief The one symbol this module exports. */
extern SpnegoNs Spnego;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SMB

#endif // PROTOCORE_SPNEGO_H
