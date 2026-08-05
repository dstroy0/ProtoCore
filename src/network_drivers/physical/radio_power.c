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

#if PC_ENABLE_RADIO_POWER

// The module's storage, whose layout radio_power.h only declares. Present on both arms so the
// module's shape does not change with the build.
struct RadioCtx
{
    int held; ///< bulk-transfer keep-awake refcount
};
static struct RadioCtx s_radio;

static const char *ps_name(uint8_t mode)
{
    switch (mode)
    {
    case PC_PS_MIN_MODEM:
        return "min_modem";
    case PC_PS_MAX_MODEM:
        return "max_modem";
    case PC_PS_NONE:
        return "none";
    default:
        return "none";
    }
}

#if PROTOCORE_HOT

// Service vocabulary -> L1 vocabulary. Both are ours; neither is the vendor's.
static pc_phy_ps to_phy_ps(uint8_t mode)
{
    if (mode == PC_PS_MIN_MODEM)
    {
        return PC_PHY_PS_MIN_MODEM;
    }
    if (mode == PC_PS_MAX_MODEM)
    {
        return PC_PHY_PS_MAX_MODEM;
    }
    return PC_PHY_PS_NONE;
}

static void power(void)
{
    pc_phy_ps_set(to_phy_ps(PC_RADIO_WIFI_PS));
#if PC_RADIO_MAX_TX_DBM > 0
    pc_phy_tx_power_set((int8_t)PC_RADIO_MAX_TX_DBM); // whole dBm; the backend owns the unit
#endif
}

static uint8_t ps_get(void)
{
    const pc_phy_ps m = pc_phy_ps_get();
    if (m == PC_PHY_PS_MIN_MODEM)
    {
        return PC_PS_MIN_MODEM;
    }
    if (m == PC_PHY_PS_MAX_MODEM)
    {
        return PC_PS_MAX_MODEM;
    }
    return PC_PS_NONE;
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

#else // host build - no radio

static void power(void)
{
}
static uint8_t ps_get(void)
{
    return PC_PS_NONE;
}
static void busy_hold(void)
{
    // no-op on the host build: there is no radio to keep awake (the ESP32 branch above holds the
    // modem-sleep refcount).
}
static void busy_release(void)
{
    // no-op on the host build (see busy_hold).
}

#endif // PROTOCORE_HOT

const RadioNs Radio = {&s_radio, power, ps_name, ps_get, busy_hold, busy_release};

#endif // PC_ENABLE_RADIO_POWER
