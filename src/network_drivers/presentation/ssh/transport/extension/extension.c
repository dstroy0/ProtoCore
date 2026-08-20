// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file extension.c
 * @brief RFC 8308 extension negotiation.
 */

#include "network_drivers/presentation/ssh/transport/extension/extension.h"
#include "network_drivers/presentation/ssh/common.h"
#include "network_drivers/presentation/ssh/transport/transport/transport.h" // ssh_kex_prefer_rsa()

// sec 2.2: the indicator each role offers. A client sends the first, a server the second, and each
// looks for the other's.
static const char EXT_INFO_C[] = "ext-info-c";
static const char EXT_INFO_S[] = "ext-info-s";

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_extension_info_indicator(uint8_t *restrict work)
{
    (void)work;
    proto_bool client_role = ExtensionV.info_indicator_args.client_role;

    ExtensionV.text = client_role ? EXT_INFO_C : EXT_INFO_S;
}

void protocore_extension_build(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = ExtensionV.build_args.out;
    size_t *len = ExtensionV.build_args.len;
    size_t cap = ExtensionV.build_args.cap;

    // byte SSH_MSG_EXT_INFO || uint32 nr-extensions || (string name, string value)*
    protocore_span w = span.from(out, cap);
    bytes.put(&w, SSH_MSG_EXT_INFO);
    bytes.put_be(&w, 1, 4);                       // one extension
    protocore_ssh_wr_cstr(&w, "server-sig-algs"); // extension name
    // Accepted client public-key signature algorithms for userauth. All are always verifiable
    // (independent of which host key we hold); ordered by our preference so a modern client picks
    // the steered-to type. Both RSA hashes are offered (rsa-sha2-512 first, RFC 8332); the verifier
    // picks the hash from the client's chosen algorithm name.
    const char *siglist = ssh_kex_prefer_rsa() ? "rsa-sha2-512,rsa-sha2-256,ecdsa-sha2-nistp256,ssh-ed25519"
                                               : "ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-512,rsa-sha2-256";
    protocore_ssh_wr_cstr(&w, siglist); // value: accepted client-sig algorithms
    if (!span.ok(w))
    {
        ExtensionV.n = -1;
        return;
    }
    *len = w.pos;
    ExtensionV.n = 0;
}
/** @brief The operands and the outcome. */
ExtensionVars ExtensionV;

PROTOCORE_END_DECLS
