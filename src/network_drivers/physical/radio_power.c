// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file radio_power.c
 * @brief Modem-sleep mode names (pure) + phy apply/readback (ESP32). See radio_power.h.
 *
 * Every function here has internal linkage. The module reaches callers through @ref Radio, which is
 * the only symbol it exports, so nothing in this file can collide with a name anywhere else.
 */

#include "network_drivers/physical/radio_power.h"
#include "network_drivers/physical/physical.h"

// The module's storage, whose layout radio_power.h only declares. Present on both arms so the
// module's shape does not change with the build.
struct RadioCtx
{
    int held; ///< bulk-transfer keep-awake refcount
};
static struct RadioCtx s_radio;

static const char *ps_name(pc_phy_ps mode)
{
    switch (mode)
    {
    case PC_PHY_PS_MIN_MODEM:
        return "min_modem";
    case PC_PHY_PS_MAX_MODEM:
        return "max_modem";
    case PC_PHY_PS_NONE:
        return "none";
    default:
        return "none";
    }
}

#if PROTOCORE_HOT

static void power(void)
{
    pc_phy_ps_set((pc_phy_ps)PC_RADIO_WIFI_PS);
#if PC_RADIO_MAX_TX_DBM > 0
    pc_phy_tx_power_set((int8_t)PC_RADIO_MAX_TX_DBM); // whole dBm; the backend owns the unit
#endif
}

static void busy_hold(void)
{
    if (s_radio.held == 0)
    {
        pc_phy_ps_set(PC_PHY_PS_NONE); // modem sleep off during a bulk transfer
    }
    s_radio.held++;
}

static void busy_release(void)
{
    if (s_radio.held > 0)
    {
        s_radio.held--;
        if (s_radio.held == 0)
        {
            power(); // last transfer done: restore the configured mode
        }
    }
}

#else  // no radio backend

static void power(void)
{
}
static void busy_hold(void)
{
    // No radio backend, so no modem sleep to hold off.
}
static void busy_release(void)
{
}
#endif // PROTOCORE_HOT

// Designated, so a member's position in the struct does not decide what it binds to. The table is
// split by a feature flag, where a positional list shifts every member below the arm at once.
const RadioNs Radio = {
#if PC_ENABLE_RADIO_POWER
    .ctx = &s_radio,
    .power = power,
    .ps_name = ps_name,
    .busy_hold = busy_hold,
    .busy_release = busy_release,
#endif
    .ps_set = pc_phy_ps_set,
    .ps_mode = pc_phy_ps_get,
    .tx_power_set = pc_phy_tx_power_set,
    .monitor_begin = pc_phy_monitor_begin,
    .monitor_set_channel = pc_phy_monitor_set_channel,
    .monitor_end = pc_phy_monitor_end};
