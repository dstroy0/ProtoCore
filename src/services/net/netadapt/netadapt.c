// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file netadapt.c
 * @brief Network adaptation decision core (see netadapt.h).
 */

#include "services/net/netadapt/netadapt.h"

#if PC_ENABLE_NETADAPT

uint32_t pc_netadapt_window(uint32_t free_heap, uint32_t reserve, uint32_t min_win, uint32_t max_win)
{
    uint32_t ceil_win = max_win < min_win ? min_win : max_win;
    if (free_heap <= reserve)
    {
        return min_win; // no spare heap: stay at the floor
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
    return win;
}

proto_bool pc_netadapt_dhcp_fallback(uint32_t elapsed_ms, uint32_t attempts, uint32_t timeout_ms, uint32_t max_attempts)
{
    if (elapsed_ms >= timeout_ms)
    {
        return PROTO_TRUE;
    }
    if (max_attempts > 0 && attempts >= max_attempts)
    {
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

#endif // PC_ENABLE_NETADAPT
