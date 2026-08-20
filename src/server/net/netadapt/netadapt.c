// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file netadapt.c
 * @brief Network adaptation decision core (see netadapt.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_NETADAPT

#include "server/net/netadapt/netadapt.h"

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_netadapt_window(uint8_t *restrict work)
{
    (void)work;
    uint32_t free_heap = NetadaptV.window_args.free_heap;
    uint32_t reserve = NetadaptV.window_args.reserve;
    uint32_t min_win = NetadaptV.window_args.min_win;
    uint32_t max_win = NetadaptV.window_args.max_win;

    uint32_t ceil_win = max_win < min_win ? min_win : max_win;
    if (free_heap <= reserve)
    {
        NetadaptV.u32 = min_win;
        return; // no spare heap: stay at the floor
    }

    // A quarter of the heap above the reserve - leaves headroom for TX buffers, TLS, app state.
    uint32_t win = (free_heap - reserve) / 4;
    if (win < min_win)
    {
        win = min_win;
    }
    if (win > ceil_win)
    {
        win = ceil_win;
    }
    NetadaptV.u32 = win;
}

void protocore_netadapt_dhcp_fallback(uint8_t *restrict work)
{
    (void)work;
    uint32_t elapsed_ms = NetadaptV.dhcp_fallback_args.elapsed_ms;
    uint32_t attempts = NetadaptV.dhcp_fallback_args.attempts;
    uint32_t timeout_ms = NetadaptV.dhcp_fallback_args.timeout_ms;
    uint32_t max_attempts = NetadaptV.dhcp_fallback_args.max_attempts;

    if (elapsed_ms >= timeout_ms)
    {
        NetadaptV.ok = PROTO_TRUE;
        return;
    }
    if (max_attempts > 0 && attempts >= max_attempts)
    {
        NetadaptV.ok = PROTO_TRUE;
        return;
    }
    NetadaptV.ok = PROTO_FALSE;
}

/** @brief The operands and the outcome. */
NetadaptVars NetadaptV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_NETADAPT
