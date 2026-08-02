// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file netadapt.h
 * @brief Network adaptation decisions: TCP window sizing by free RAM + DHCP->static fallback
 *        (PC_ENABLE_NETADAPT).
 *
 * Two pure decisions a network manager needs on a memory-constrained, sometimes-headless device:
 *
 *  - `pc_netadapt_window()` - size the TCP receive window / RX buffer from the free heap, so a device
 *    with RAM to spare uses a bigger window for throughput while a low-memory one shrinks to stay alive.
 *    Keeps a reserve untouched and clamps to a sane [min, max].
 *
 *  - `pc_netadapt_dhcp_fallback()` - decide when to stop waiting on DHCP and configure a static IP, so
 *    a node on a network with no DHCP server still comes up. Triggers once the elapsed wait exceeds the
 *    timeout or the retry budget is spent.
 *
 * Pure, zero heap, no stdlib, host-testable; the app applies the results (lwIP window / netif config).
 */

#ifndef PROTOCORE_NETADAPT_H
#define PROTOCORE_NETADAPT_H

#include "protocore_config.h"

#if PC_ENABLE_NETADAPT

/**
 * @brief Recommend a TCP receive window / RX buffer size (bytes) from the free heap.
 * @param free_heap current free heap (bytes).
 * @param reserve   heap to leave untouched for everything else (bytes).
 * @param min_win   floor for the returned window (always usable).
 * @param max_win   ceiling for the returned window.
 * @return a window in [min_win, max_win]: min_win if the heap at/below the reserve, otherwise a quarter
 *         of the heap above the reserve (headroom for other buffers), clamped to the ceiling.
 *
 * If max_win < min_win the result is min_win.
 */
uint32_t pc_netadapt_window(uint32_t free_heap, uint32_t reserve, uint32_t min_win, uint32_t max_win);

/**
 * @brief Should we stop waiting on DHCP and switch to the configured static IP?
 * @param elapsed_ms   time since the DHCP attempt started (ms).
 * @param attempts     DHCP attempts made so far.
 * @param timeout_ms   per-run wait budget before falling back (ms).
 * @param max_attempts attempt budget before falling back (0 = ignore the attempt count).
 * @return true once the elapsed wait exceeds @p timeout_ms, or (when @p max_attempts > 0) the attempts
 *         reach @p max_attempts - i.e. DHCP has failed for long/often enough to fall back.
 */
proto_bool pc_netadapt_dhcp_fallback(uint32_t elapsed_ms, uint32_t attempts, uint32_t timeout_ms,
                                     uint32_t max_attempts);

#endif // PC_ENABLE_NETADAPT
#endif // PROTOCORE_NETADAPT_H
