// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file diffserv.c
 * @brief DiffServ QoS marking (RFC 2474) - the two server-wide DSCP defaults.
 *
 * Owns the default DSCP applied to outbound TCP connections and the default for UDP datagrams. A
 * listener's override and a connection's override are set through those objects, each next to the
 * pcb pool it touches, and both read these defaults.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DIFFSERV

#include "diffserv.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from

/**
 * @brief The two code points this server marks with, held where only this file describes them.
 *
 * RFC 2474 sec 3 names the six-bit selector in the DS field the DSCP; these are the defaults each
 * outbound object starts from. Both start at 0: RFC 2474 sec 4.1 makes the all-zero DSCP the
 * default PHB, best effort.
 */
struct DiffServStorage
{
    uint8_t tcp_dscp; ///< server-wide default DSCP for outbound TCP connections (0 = best-effort)
    uint8_t udp_dscp; ///< default DSCP for outbound UDP datagrams (0 = best-effort)
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define DIFFSERV_OFF_CTX 0u
static_assert(DIFFSERV_OFF_CTX + sizeof(struct DiffServStorage) <= PROTOCORE_DIFFSERV_BORROW,
              "PROTOCORE_DIFFSERV_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define DIFFSERV_CTX(w) ((struct DiffServStorage *)(void *)((w) + DIFFSERV_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_DIFFSERV_BORROW persistent bytes
} DiffServOwnCtx;
static DiffServOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_diffserv_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_DIFFSERV_BORROW).buf;
    }
    return s_own.span;
}

// Masked to six bits on write, so a caller cannot spill into the two currently-unused bits.
static void set_default(uint8_t *restrict work)
{
    DIFFSERV_CTX(work)->tcp_dscp = (uint8_t)(DiffServ.dscp & 0x3F);
}

static void default_dscp(uint8_t *restrict work)
{
    DiffServ.u8 = DIFFSERV_CTX(work)->tcp_dscp;
}

static void set_udp(uint8_t *restrict work)
{
    DIFFSERV_CTX(work)->udp_dscp = (uint8_t)(DiffServ.dscp & 0x3F);
}

static void udp_dscp(uint8_t *restrict work)
{
    DiffServ.u8 = DIFFSERV_CTX(work)->udp_dscp;
}

// Not entries: these take no borrow, so they read the module's own span. A null span is the pool
// coming up short, and reports the all-zero DSCP, which RFC 2474 sec 4.1 makes the default PHB.
uint8_t protocore_diffserv_default_dscp(void)
{
    uint8_t *work = protocore_diffserv_span();
    return work ? DIFFSERV_CTX(work)->tcp_dscp : 0u;
}

uint8_t protocore_diffserv_udp_dscp(void)
{
    uint8_t *work = protocore_diffserv_span();
    return work ? DIFFSERV_CTX(work)->udp_dscp : 0u;
}

// Designated, so a member's position in the struct does not decide what it binds to.
DiffServNs DiffServ = {
    .set_default = set_default, .default_dscp = default_dscp, .set_udp = set_udp, .udp_dscp = udp_dscp};

#endif // PROTOCORE_ENABLE_DIFFSERV
