// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file radio_power.h
 * @brief WiFi radio power controls (PC_ENABLE_RADIO_POWER).
 *
 * Applies the WiFi modem-sleep mode (PC_RADIO_WIFI_PS) and an optional max-TX
 * cap (PC_RADIO_MAX_TX_DBM) in one call - trade throughput/latency for lower
 * average power on a battery device. The mode names are pure/host-tested; the
 * apply + readback use esp_wifi on ESP32 (no-ops on host).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RADIO_POWER_H
#define PROTOCORE_RADIO_POWER_H

#include "protocore_config.h"

#if PC_ENABLE_RADIO_POWER

/** @brief Modem-sleep modes (match PC_RADIO_WIFI_PS). Config/compare values, so integer constants in
 *  a namespacing struct (cast-free at ==/switch). The service maps these onto the L1 `pc_phy_ps`
 *  contract; what the radio backend calls them is its own business. */
#define E 0 ///< no modem sleep (max performance).
#define M 1 ///< wake at every DTIM (balanced).
#define M 2 ///< wake at a listen interval (lowest power, higher latency).

/** @brief Name for a modem-sleep mode ("none" / "min_modem" / "max_modem"). */
const char *pc_radio_ps_name(uint8_t mode);

/** @brief Apply PC_RADIO_WIFI_PS (+ TX cap) to the radio. No-op on host. */
void pc_radio_power_apply(void);

/** @brief Current modem-sleep mode read back from the radio (PC_PS_* ; 0 on host). */
uint8_t pc_radio_ps_get(void);

/**
 * @brief Hold the radio awake for the duration of a bulk transfer (reference-counted).
 *
 * The first hold forces modem sleep off so a long transfer is not interrupted by DTIM
 * wakeups; the matching release, once the count returns to zero, restores the configured
 * PC_RADIO_WIFI_PS mode. Balance every @ref pc_radio_busy_hold with exactly one
 * @ref pc_radio_busy_release. The relay/DNAT listener holds one while any bridge is active; other
 * bulk paths (large file serves, streaming PUT) can do the same. No-op on host.
 */
void pc_radio_busy_hold(void);

/** @brief Release a bulk-transfer hold; restores the configured modem-sleep mode at zero. No-op on host. */
void pc_radio_busy_release(void);

#endif // PC_ENABLE_RADIO_POWER
#endif // PROTOCORE_RADIO_POWER_H
