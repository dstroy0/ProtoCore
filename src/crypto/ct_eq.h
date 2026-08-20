// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ct_eq.h
 * @brief Constant-time comparison for secret-dependent checks.
 *
 * One implementation, so every MAC, tag, digest and signature check in the library compares the same
 * way. A second copy is a second chance to write the early-out version by accident, and an early-out
 * compare is a timing oracle no test catches.
 *
 * Zeroing storage is not here: protocore_secure_wipe() is a memory-manager operation and lives in
 * mmgr/secure.h, beside the pool that wipes on release.
 *
 * The compare is `static inline` here, so it inlines into the caller's loop. ::CtEq is the same
 * compare reached through the namespace, with the operands on the handle.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CT_EQ_H
#define PROTOCORE_CT_EQ_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_CT_EQ

#include "mmgr/protomem/protomem.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief Constant-time equality of two @p n-byte buffers: returns true iff every byte matches, in time
 *        independent of where (or whether) they first differ.
 *
 * Use this for every secret-dependent comparison - AEAD tags, MACs, digests, signature check values - so a
 * timing side channel cannot reveal how many leading bytes matched. Never use mem.cmp() for those (it returns
 * early on the first mismatch). The XOR-accumulate has no data-dependent branch; only the final all-zero test
 * (the intended result) is a comparison.
 */
static inline proto_bool protocore_ct_eq(const void *a, const void *b, size_t n)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++)
    {
        diff |= (uint8_t)(pa[i] ^ pb[i]);
    }
    return diff == 0;
}

/** @brief The two buffers a compare runs over. */
typedef struct
{
    const void *a; ///< the first buffer
    const void *b; ///< the second, holding n readable bytes like the first
    size_t n;      ///< how many bytes are compared
} CtEqEqArgs;

/**
 * @brief Constant-time equality.
 *
 * A caller sets the members the call takes, invokes it through ::CtEq, and reads the outcome off the
 * same handle.
 *
 *   CtEq.eq_args.a = computed_tag;
 *   CtEq.eq_args.b = received_tag;
 *   CtEq.eq_args.n = 16;
 *   CtEq.eq(work);
 *   if (CtEq.equal) { ... }
 *
 * @var CtEqNs::eq_args  the two buffers a compare runs over
 * @var CtEqNs::ok       a call's true/false outcome; false on a null pointer
 * @var CtEqNs::equal    true when all n bytes matched; false on a null pointer
 * @var CtEqNs::eq       compare the two buffers in time independent of where they first differ
 *
 * @ref CtEqNs::equal carries the answer and @ref CtEqNs::ok carries whether the call ran, so a null
 * pointer reads as not equal rather than as a match.
 *
 * @c work goes unread: the compare runs over the caller's two buffers and needs none of its own, so
 * this module takes no borrow, holds no context, and reaches the same answer whatever is passed.
 *
 * No storage member and no context: a caller sets operands and reads @ref CtEqNs::equal, and that is
 * all the surface there is.
 */
typedef struct
{
    CtEqEqArgs eq_args;

    proto_bool ok;
    proto_bool equal;

    void (*const eq)(uint8_t *restrict work);
} CtEqNs;

/** @brief The one symbol this module exports. */
extern CtEqNs CtEq;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CT_EQ

#endif // PROTOCORE_CT_EQ_H
