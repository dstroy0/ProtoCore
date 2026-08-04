// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file power_mgmt.h
 * @brief SoC power governor: frequency scaling, thermal throttle, brownout recovery, gating
 *        (PC_ENABLE_POWER_MGMT).
 *
 * network_drivers/physical/radio_power owns the radio and server/sleep_sched decides how *long* to sleep. Neither
 * owns the SoC itself, which is where the rest of the power budget goes: the CPU clock, the die
 * temperature, and the peripherals nobody is using.
 *
 * The governor answers one question - given the current load, die temperature, and how the board
 * last reset, what should the CPU clock be right now:
 *
 *  - **Scaling.** Busy work runs at the ceiling; an idle server drops to the floor. Running a
 *    240 MHz core to poll an idle socket is the single easiest power win on this part.
 *  - **Thermal throttle.** Hot parts clock down, and the restore threshold is *lower* than the
 *    throttle threshold. Without that gap a device sitting exactly at the limit oscillates between
 *    full speed and floor forever, which is worse than either.
 *  - **Brownout recovery.** A board that just browned out is on a supply that could not hold up the
 *    last load it saw, so slamming straight back to full speed invites the same collapse and a boot
 *    loop. After a brownout reset it comes up at the floor and stays there for a settle window.
 *  - **Gating.** Blocks the firmware never uses still burn current; Bluetooth is the big one, since
 *    the controller draws power whether or not anything is connected.
 *
 * The decision is pure and takes every input explicitly - load, temperature, the brownout flag, the
 * time since boot, and the previous throttle state for the hysteresis - so the whole governor is
 * host-testable with no hardware. The binding only reads the sensors and applies the result.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_POWER_MGMT_H
#define PROTOCORE_POWER_MGMT_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_POWER_MGMT

/** @brief Governor limits. Temperatures in whole degrees C; frequencies in MHz. */
typedef struct
{
    uint16_t mhz_max;    ///< clock when there is work to do.
    uint16_t mhz_min;    ///< clock when idle, throttled, or recovering.
    uint8_t busy_pct;    ///< load at/above which the ceiling is used.
    int16_t temp_hot_c;  ///< throttle at/above this die temperature.
    int16_t temp_cool_c; ///< release the throttle at/below this one (must be < temp_hot_c).
    uint32_t recover_ms; ///< how long to stay at the floor after a brownout reset.
} PowerCfg;

/** @brief What the governor decided this tick. */
typedef struct
{
    uint16_t cpu_mhz;      ///< clock to apply.
    proto_bool throttled;  ///< the thermal limit is holding the clock down.
    proto_bool recovering; ///< still inside the post-brownout settle window.
} PowerPlan;

/**
 * @brief Decide the clock for this tick.
 *
 * Precedence is deliberate: brownout recovery outranks thermal, which outranks load. A board that
 * cannot hold its supply must not be clocked up because it happens to be busy, and a hot board must
 * not be clocked up for the same reason.
 *
 * @param load_pct       0-100; work done in the last window (values above 100 are clamped).
 * @param temp_c         die temperature in whole degrees C.
 * @param brownout_boot  true if the last reset was a brownout.
 * @param since_boot_ms  milliseconds since boot, for the recovery window.
 * @param was_throttled  the previous tick's `throttled` - this is what gives the thermal decision
 *                       its hysteresis, so pass the plan's own output back in.
 */
PowerPlan pc_power_plan(const PowerCfg *cfg, uint8_t load_pct, int16_t temp_c, proto_bool brownout_boot,
                        uint32_t since_boot_ms, proto_bool was_throttled);

/** @brief Defaults from the PC_POWER_* build flags. */
void pc_power_cfg_defaults(PowerCfg *cfg);

/**
 * @brief Serialize a plan as `{"cpu_mhz":N,"throttled":bool,"recovering":bool,"temp_c":N}`.
 * @return length written (excl NUL), or 0 on overflow / bad args.
 */
size_t pc_power_json(const PowerPlan *plan, int16_t temp_c, char *out, size_t cap);

#if PROTOCORE_HOT
// --- device binding -----------------------------------------------------------------------

/** @brief True if the last reset was a brownout (esp_reset_reason). Latched, so it stays true. */
proto_bool pc_power_brownout_boot(void);

/** @brief Die temperature in whole degrees C, or INT16_MIN if this part has no sensor. */
int16_t pc_power_temp_c(void);

/** @brief Apply @p plan's clock (no-op if it already matches). @return true if the clock changed. */
proto_bool pc_power_apply(const PowerPlan *plan);

/** @brief Current CPU clock in MHz. */
uint16_t pc_power_cpu_mhz(void);

/**
 * @brief Release the Bluetooth controller's power domain when the firmware does not use BT.
 *
 * The controller draws current whether or not anything is connected, so on a build with no BLE this
 * is free power back. Safe to call when BT was never initialized, and safe to call twice.
 * @return true if a release actually happened.
 */
proto_bool pc_power_gate_bt(void);
#endif // PROTOCORE_HOT

#endif // PC_ENABLE_POWER_MGMT

PROTO_END_DECLS

#endif // PROTOCORE_POWER_MGMT_H
