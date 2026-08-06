// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file radio_power.h
 * @brief WiFi radio power controls (PC_ENABLE_RADIO_POWER).
 *
 * Applies the WiFi modem-sleep mode (PC_RADIO_WIFI_PS) and an optional max-TX cap
 * (PC_RADIO_MAX_TX_DBM) in one call - trade throughput/latency for lower average power on a battery
 * device. The mode names are pure/host-tested; the apply + readback use the L1 phy contract on ESP32
 * (no-ops on host).
 *
 * The module exports one symbol, @ref Radio. Everything in radio_power.c has internal linkage, so no
 * name from this module reaches the library-wide symbol space and none of them can collide.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RADIO_POWER_H
#define PROTOCORE_RADIO_POWER_H

#include "network_drivers/physical/physical.h" // pc_phy_ps, pc_phy_frame_fn: the L1 contract
#include "protocore_config.h"

PROTO_BEGIN_DECLS

/** @brief Modem-sleep modes (match PC_RADIO_WIFI_PS). Config/compare values, so integer constants in
 *  a namespacing struct (cast-free at ==/switch). The service maps these onto the L1 `pc_phy_ps`
 *  contract; what the radio backend calls them is its own business. */
#define PC_PS_NONE 0      ///< no modem sleep (max performance).
#define PC_PS_MIN_MODEM 1 ///< wake at every DTIM (balanced).
#define PC_PS_MAX_MODEM 2 ///< wake at a listen interval (lowest power, higher latency).

#if PC_ENABLE_RADIO_POWER
/** @brief The module's storage. Declared, never defined here: the layout stays in radio_power.c. */
typedef struct RadioCtx RadioCtx;
#endif

/**
 * @brief The radio-power module.
 *
 * @var RadioNs::ctx          the module's storage, opaque to every caller.
 * @var RadioNs::power        apply PC_RADIO_WIFI_PS (+ TX cap) to the radio. No-op on host.
 * @var RadioNs::ps_name      name for a modem-sleep mode ("none" / "min_modem" / "max_modem").
 * @var RadioNs::ps_get       modem-sleep mode read back from the radio (PC_PS_* ; 0 on host).
 * @var RadioNs::busy_hold    hold the radio awake for a bulk transfer (reference-counted).
 * @var RadioNs::busy_release release one bulk-transfer hold.
 * @var RadioNs::ps_set       set the modem-sleep mode on the radio.
 * @var RadioNs::ps_mode      the mode the radio reports, in L1's own pc_phy_ps terms.
 * @var RadioNs::tx_power_set cap transmit power, in dBm.
 * @var RadioNs::monitor_begin  start promiscuous capture on a channel.
 * @var RadioNs::monitor_set_channel  retune while capturing.
 * @var RadioNs::monitor_end  stop capturing.
 *
 * The first @ref RadioNs::busy_hold forces modem sleep off so a long transfer is not interrupted by
 * DTIM wakeups; the matching release, once the count returns to zero, restores the configured
 * PC_RADIO_WIFI_PS mode. Balance every hold with exactly one release. The relay/DNAT listener holds
 * one while any bridge is active; other bulk paths (large file serves, streaming PUT) can do the
 * same. Both are no-ops on host.
 */
typedef struct RadioNs
{
#if PC_ENABLE_RADIO_POWER
    RadioCtx *ctx;
    void (*power)(void);
    const char *(*ps_name)(uint8_t mode);
    uint8_t (*ps_get)(void);
    void (*busy_hold)(void);
    void (*busy_release)(void);
#endif
    proto_bool (*ps_set)(pc_phy_ps mode);
    pc_phy_ps (*ps_mode)(void);
    proto_bool (*tx_power_set)(int8_t dbm);
    proto_bool (*monitor_begin)(uint8_t channel, pc_phy_frame_fn cb);
    void (*monitor_set_channel)(uint8_t channel);
    void (*monitor_end)(void);
} RadioNs;

/** @brief The one symbol this module exports. */
extern const RadioNs Radio;

PROTO_END_DECLS

#endif // PROTOCORE_RADIO_POWER_H
