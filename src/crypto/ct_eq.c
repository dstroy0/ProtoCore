// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ct_eq.c
 * @brief The ::CtEq entry over the constant-time compare in ct_eq.h.
 *
 * The compare itself stays `static inline` in the header for the crypto files that run it inside a
 * loop. This file is the namespace over it: one entry that takes its operands off the handle and
 * leaves the answer on the same handle.
 *
 * Nothing here carves the borrow, holds state between calls or touches the pool. The two buffers are
 * the caller's, so the module needs no bytes of its own and defines no context.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_CT_EQ

#include "crypto/ct_eq.h"
#include "crypto/crypto_opt.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// XOR-accumulate over the caller's two buffers; the borrow goes unread.
static void ct_eq_eq(uint8_t *restrict work)
{
    (void)work;
    CtEq.ok = PROTO_FALSE;
    CtEq.equal = PROTO_FALSE;
    if (!CtEq.eq_args.a || !CtEq.eq_args.b)
    {
        return;
    }
    CtEq.equal = protocore_ct_eq(CtEq.eq_args.a, CtEq.eq_args.b, CtEq.eq_args.n);
    CtEq.ok = PROTO_TRUE;
}

CtEqNs CtEq = {.eq = ct_eq_eq};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CT_EQ
