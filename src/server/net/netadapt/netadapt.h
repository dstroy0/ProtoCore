// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file netadapt.h
 * @brief Network adaptation decisions: TCP window sizing by free RAM + DHCP->static fallback
 *        (PROTOCORE_ENABLE_NETADAPT).
 *
 * Two pure decisions a network manager needs on a memory-constrained, sometimes-headless device:
 *
 *  - `protocore_netadapt_window()` - size the TCP receive window / RX buffer from the free heap, so a device
 *    with RAM to spare uses a bigger window for throughput while a low-memory one shrinks to stay alive.
 *    Keeps a reserve untouched and clamps to a sane [min, max].
 *
 *  - `protocore_netadapt_dhcp_fallback()` - decide when to stop waiting on DHCP and configure a static IP, so
 *    a node on a network with no DHCP server still comes up. Triggers once the elapsed wait exceeds the
 *    timeout or the retry budget is spent.
 *
 * Pure, zero heap, no stdlib, host-testable; the app applies the results (lwIP window / netif config).
 */

#ifndef PROTOCORE_NETADAPT_H
#define PROTOCORE_NETADAPT_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_NETADAPT

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief What window takes: free_heap, reserve, min_win, max_win. */
typedef struct
{
    uint32_t free_heap; ///< current free heap (bytes)
    uint32_t reserve;   ///< heap to leave untouched for everything else (bytes)
    uint32_t min_win;   ///< floor for the returned window (always usable)
    uint32_t max_win;   ///< ceiling for the returned window
} NetadaptWindowArgs;
/** @brief What dhcp_fallback takes: elapsed_ms, attempts, timeout_ms, ... */
typedef struct
{
    uint32_t elapsed_ms;   ///< time since the DHCP attempt started (ms)
    uint32_t attempts;     ///< DHCP attempts made so far
    uint32_t timeout_ms;   ///< per-run wait budget before falling back (ms)
    uint32_t max_attempts; ///< attempt budget before falling back (0 = ignore the attempt count)
} NetadaptDhcpFallbackArgs;
/**
 * @brief Network adaptation decisions: TCP window sizing by free RAM + DHCP->static fallback ...
 *
 * A caller sets the members a call takes, invokes it through ::Netadapt with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Netadapt.window_args.free_heap = ...;
 *   Netadapt.window_args.reserve = ...;
 *   Netadapt.window_args.min_win = ...;
 *   Netadapt.window_args.max_win = ...;
 *   Netadapt.window(work);
 *   // Netadapt.u32 is what the call reports
 *
 * @var NetadaptNs::window_args  what window takes: free_heap, reserve, min_win, max_win
 * @var NetadaptNs::dhcp_fallback_args  what dhcp_fallback takes: elapsed_ms, attempts, timeout_ms,
 * @var NetadaptNs::ok  true once the elapsed wait exceeds timeout_ms, or (when ...
 * @var NetadaptNs::u32  a window in [min_win, max_win]: min_win if the heap at/below the ...
 * @var NetadaptNs::window  recommend a TCP receive window / RX buffer size (bytes) from the ...
 * @var NetadaptNs::dhcp_fallback  should we stop waiting on DHCP and switch to the configured static ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    NetadaptWindowArgs window_args;
    NetadaptDhcpFallbackArgs dhcp_fallback_args;
    proto_bool ok;
    uint32_t u32;
} NetadaptVars;

/** @brief The operands and the outcome. */
extern NetadaptVars NetadaptV;

/** @brief The entries. */
typedef struct
{
    void (*const window)(uint8_t *restrict work);
    void (*const dhcp_fallback)(uint8_t *restrict work);
} NetadaptNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in NetadaptV or a region of the borrow at a fixed offset.
void protocore_netadapt_window(uint8_t *restrict work);
void protocore_netadapt_dhcp_fallback(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Netadapt.window(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const NetadaptNs Netadapt __attribute__((unused)) = {
    .window = protocore_netadapt_window,
    .dhcp_fallback = protocore_netadapt_dhcp_fallback,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_NETADAPT

#endif // PROTOCORE_NETADAPT_H
