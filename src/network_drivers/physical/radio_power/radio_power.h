// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file radio_power.h
 * @brief Layer 1 (Physical) - 802.11 power management, transmit power control, and monitor
 *        capture (PROTOCORE_ENABLE_RADIO_POWER).
 *
 * IEEE Std 802.11-2020 is the normative source for every call here; no IETF RFC governs radio
 * power management. 11.2.3.2 (Non-AP STA power management modes) names the two modes a non-AP
 * STA runs in, active mode and PS mode, and 6.3.2.2 (MLME-POWERMGT.request) is the primitive
 * that selects one. A STA in PS mode dozes and wakes for the DTIM of 11.2.3.4 (TIM types),
 * across at most the beacon count of 9.4.1.6 (Listen Interval field). 11.7 (TPC procedures)
 * governs transmit power in dBm, bounded by 11.7.5 (Specification of regulatory and local
 * maximum transmit power levels).
 *
 * PROTOCORE_RADIO_WIFI_PS picks the mode and PROTOCORE_RADIO_MAX_TX_DBM the cap; @ref RadioNs::power
 * applies both in one call, trading throughput and latency for lower average draw. The mode
 * names are pure and host-tested; the apply and the readback go through the L1 phy contract,
 * which reports failure when the part carries no radio backend.
 *
 * The module exports one symbol, @ref Radio. Everything in radio_power.c has internal linkage, so no
 * name from this module reaches the library-wide symbol space and none of them can collide.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RADIO_POWER_H
#define PROTOCORE_RADIO_POWER_H

#include "network_drivers/physical/physical/physical.h" // protocore_phy_ps, protocore_phy_frame_fn: the L1 contract

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/** @brief The power management mode a call applies or renders (802.11-2020 11.2.3.2). */
typedef struct
{
    protocore_phy_ps mode; ///< active mode or PS mode, in L1's own protocore_phy_ps terms
} RadioPsArgs;
/** @brief The transmit power a cap applies (802.11-2020 11.7.6). */
typedef struct
{
    int8_t dbm; ///< maximum transmit power in whole dBm; 802.11-2020 11.7.5 bounds it
} RadioTxArgs;
/** @brief What a monitor capture tunes to, and where its frames go. */
typedef struct
{
    uint8_t channel;                 ///< channel number in the radio's operating class (802.11-2020 Annex E)
    protocore_phy_frame_fn on_frame; ///< each captured frame, FCS stripped (802.11-2020 9.2.4.8)
} RadioMonitorArgs;
/** @brief The radio's own state and the calls that reach it, described only in radio_power.c. */
/**
 * @brief The radio: its power management mode, its transmit power cap, and monitor capture.
 *
 * A caller sets the members a call takes, invokes it through ::Radio, and reads the outcome off the
 * same handle. The keep-awake count is behind @ref internal.
 *
 * @var RadioNs::ps       the power management mode a call applies or renders (802.11-2020 11.2.3.2)
 * @var RadioNs::tx       the transmit power a cap applies (802.11-2020 11.7)
 * @var RadioNs::monitor  what a capture tunes to, and where its frames go
 * @var RadioNs::ok       a call's true/false outcome
 * @var RadioNs::mode     the mode the radio reports, in L1's own protocore_phy_ps terms
 * @var RadioNs::text     the name a render reports ("none" / "min_modem" / "max_modem")
 * @var RadioNs::power        apply PROTOCORE_RADIO_WIFI_PS, and PROTOCORE_RADIO_MAX_TX_DBM when nonzero
 * @var RadioNs::ps_name      render a power management mode as text
 * @var RadioNs::busy_hold    hold the radio in active mode for a bulk transfer (reference-counted)
 * @var RadioNs::busy_release release one bulk-transfer hold
 * @var RadioNs::ps_set       select active mode or PS mode (802.11-2020 6.3.2.2)
 * @var RadioNs::ps_mode      read the mode back into @ref RadioNs::mode
 * @var RadioNs::tx_power_set cap transmit power at @ref RadioTxArgs::dbm (802.11-2020 11.7.6)
 * @var RadioNs::monitor_begin        start capture on @ref RadioMonitorArgs::channel
 * @var RadioNs::monitor_set_channel  retune capture to @ref RadioMonitorArgs::channel
 * @var RadioNs::monitor_end          stop capture
 *
 * The first @ref RadioNs::busy_hold puts the radio in active mode so a long transfer crosses no
 * doze interval; the matching release, once the count returns to zero, applies the configured
 * PROTOCORE_RADIO_WIFI_PS mode again. Balance every hold with exactly one release. The relay/DNAT
 * listener holds one while any bridge is active; other bulk paths (large file serves, streaming
 * PUT) can do the same.
 */
typedef struct RadioNs
{
    RadioPsArgs ps;           ///< the power management mode a call applies or renders (802.11-2020 11.2.3.2)
    RadioTxArgs tx;           ///< the transmit power a cap applies (802.11-2020 11.7)
    RadioMonitorArgs monitor; ///< what a capture tunes to, and where its frames go
    proto_bool ok;
    protocore_phy_ps mode;
    const char *text;
#if PROTOCORE_ENABLE_RADIO_POWER
#endif
} RadioVars;

/** @brief The operands and the outcome. */
extern RadioVars RadioV;

/** @brief The entries. */
typedef struct
{
    void (*const power)(uint8_t *restrict work);
    void (*const ps_name)(uint8_t *restrict work);
    void (*const busy_hold)(uint8_t *restrict work);
    void (*const busy_release)(uint8_t *restrict work);
    void (*const ps_set)(uint8_t *restrict work);
    void (*const ps_mode)(uint8_t *restrict work);
    void (*const tx_power_set)(uint8_t *restrict work);
    void (*const monitor_begin)(uint8_t *restrict work);
    void (*const monitor_set_channel)(uint8_t *restrict work);
    void (*const monitor_end)(uint8_t *restrict work);
} RadioNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in RadioV or a region of the borrow at a fixed offset.
void protocore_radio_power_power(uint8_t *restrict work);
void protocore_radio_power_ps_name(uint8_t *restrict work);
void protocore_radio_power_busy_hold(uint8_t *restrict work);
void protocore_radio_power_busy_release(uint8_t *restrict work);
void protocore_radio_power_ps_set(uint8_t *restrict work);
void protocore_radio_power_ps_mode(uint8_t *restrict work);
void protocore_radio_power_tx_power_set(uint8_t *restrict work);
void protocore_radio_power_monitor_begin(uint8_t *restrict work);
void protocore_radio_power_monitor_set_channel(uint8_t *restrict work);
void protocore_radio_power_monitor_end(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Radio.power(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const RadioNs Radio __attribute__((unused)) = {
    .power = protocore_radio_power_power,
    .ps_name = protocore_radio_power_ps_name,
    .busy_hold = protocore_radio_power_busy_hold,
    .busy_release = protocore_radio_power_busy_release,
    .ps_set = protocore_radio_power_ps_set,
    .ps_mode = protocore_radio_power_ps_mode,
    .tx_power_set = protocore_radio_power_tx_power_set,
    .monitor_begin = protocore_radio_power_monitor_begin,
    .monitor_set_channel = protocore_radio_power_monitor_set_channel,
    .monitor_end = protocore_radio_power_monitor_end,
};

/**
 * @brief The PROTOCORE_RADIO_POWER_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where that
 * borrow comes from. Taken once from the end of the pool, so it lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_radio_power_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_RADIO_POWER_H
