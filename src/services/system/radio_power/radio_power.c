// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file radio_power.c
 * @brief Modem-sleep mode names (pure) + esp_wifi apply/readback (ESP32).
 */

#include "services/system/radio_power/radio_power.h"
#include "network_drivers/physical/physical.h"

#if PC_ENABLE_RADIO_POWER

#if PROTOCORE_HOT
#include "esp_wifi.h"
#endif
const char *pc_radio_ps_name(uint8_t mode)
{
    switch (mode)
    {
    case pc_radio_ps::PC_PS_MIN_MODEM:
        return "min_modem";
    case pc_radio_ps::PC_PS_MAX_MODEM:
        return "max_modem";
    case pc_radio_ps::PC_PS_NONE:
        return "none";
    default:
        return "none";
    }
}

#if PROTOCORE_HOT

// Service vocabulary -> L1 vocabulary. Both are ours; neither is the vendor's.
pc_phy_ps to_phy_ps(uint8_t mode)
{
    if (mode == pc_radio_ps::PC_PS_MIN_MODEM)
    {
        return pc_phy_ps::PC_PHY_PS_MIN_MODEM;
    }
    if (mode == pc_radio_ps::PC_PS_MAX_MODEM)
    {
        return pc_phy_ps::PC_PHY_PS_MAX_MODEM;
    }
    return pc_phy_ps::PC_PHY_PS_NONE;
}

// Bulk-transfer keep-awake refcount, owned in one context (owner-context guard).
typedef struct
{
    int held;
} RadioBusyCtx;
static RadioBusyCtx s_busy;

void pc_radio_power_apply(void)
{
    pc_phy_ps_set(to_phy_ps(PC_RADIO_WIFI_PS));
#if PC_RADIO_MAX_TX_DBM > 0
    pc_phy_tx_power_set((int8_t)PC_RADIO_MAX_TX_DBM); // whole dBm; the backend owns the unit
#endif
}

uint8_t pc_radio_ps_get(void)
{
    const pc_phy_ps m = pc_phy_ps_get();
    if (m == pc_phy_ps::PC_PHY_PS_MIN_MODEM)
    {
        return pc_radio_ps::PC_PS_MIN_MODEM;
    }
    if (m == pc_phy_ps::PC_PHY_PS_MAX_MODEM)
    {
        return pc_radio_ps::PC_PS_MAX_MODEM;
    }
    return pc_radio_ps::PC_PS_NONE;
}

void pc_radio_busy_hold(void)
{
    if (s_busy.held++ == 0)
    {
        pc_phy_ps_set(pc_phy_ps::PC_PHY_PS_NONE); // modem sleep off during a bulk transfer
    }
}

void pc_radio_busy_release(void)
{
    if (s_busy.held > 0 && --s_busy.held == 0)
    {
        pc_radio_power_apply(); // last transfer done: restore the configured mode
    }
}

#else // host build - no radio

void pc_radio_power_apply(void)
{
}
uint8_t pc_radio_ps_get(void)
{
    return pc_radio_ps::PC_PS_NONE;
}
void pc_radio_busy_hold(void)
{
    // no-op on the host build: there is no radio to keep awake (the ESP32 branch above holds the
    // modem-sleep refcount).
}
void pc_radio_busy_release(void)
{
    // no-op on the host build (see pc_radio_busy_hold).
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_RADIO_POWER
