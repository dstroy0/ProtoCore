// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for 802.11 power management on a build with no radio
// (network_drivers/physical/radio_power.h).
//
// IEEE Std 802.11-2020 governs every call here and no IETF RFC does. 11.2.3.2 names the two modes a
// non-AP STA runs in, active mode and PS mode, 6.3.2.2 (MLME-POWERMGT.request) is the primitive that
// selects one, and 11.7 governs transmit power in dBm. The standard publishes no C API, so the
// expectations are properties: a mode that is set must read back or the set must report failure, and
// the name of a mode is fixed text rather than whatever a vendor calls it.
//
// test_apply_refuses_without_a_radio is the load-bearing case. With no radio under the handle every
// apply must report failure rather than succeed silently: a caller that asks for monitor mode or a
// transmit cap on a part that has no radio has to be able to tell, and active mode is the only
// truthful answer when nothing can doze.

#include "network_drivers/physical/radio_power.h"

#include <unity.h>

void setUp(void)
{
}

void tearDown(void)
{
}

static const char *ps_name(protocore_phy_ps mode)
{
    Radio.ps.mode = mode;
    Radio.ps_name(Radio.internal);
    return Radio.text;
}

static protocore_phy_ps ps_mode(void)
{
    Radio.ps_mode(Radio.internal);
    return Radio.mode;
}

// The two 802.11-2020 11.2.3.2 modes plus the longer listen interval of 9.4.1.6, each rendered as
// this library's own name. A value outside the enum renders as active mode, never as a new name.
void test_ps_names_are_the_layers_own(void)
{
    TEST_ASSERT_EQUAL_STRING("none", ps_name(PROTOCORE_PHY_PS_NONE));
    TEST_ASSERT_EQUAL_STRING("min_modem", ps_name(PROTOCORE_PHY_PS_MIN_MODEM));
    TEST_ASSERT_EQUAL_STRING("max_modem", ps_name(PROTOCORE_PHY_PS_MAX_MODEM));
    TEST_ASSERT_EQUAL_STRING("none", ps_name((protocore_phy_ps)99));
}

// Rendering a name reads the argument only: naming a mode must not select it.
void test_ps_name_does_not_apply_the_mode(void)
{
    (void)ps_name(PROTOCORE_PHY_PS_MAX_MODEM);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode());
}

// Every apply reports failure with no radio behind it, and the readback stays at active mode.
void test_apply_refuses_without_a_radio(void)
{
    Radio.ps.mode = PROTOCORE_PHY_PS_MAX_MODEM;
    Radio.ps_set(Radio.internal);
    TEST_ASSERT_FALSE(Radio.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode());

    Radio.ps.mode = PROTOCORE_PHY_PS_MIN_MODEM;
    Radio.ps_set(Radio.internal);
    TEST_ASSERT_FALSE(Radio.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode());

    // 802.11-2020 11.7.6 selects a transmit power in dBm; with no radio there is nothing to cap.
    Radio.tx.dbm = 11;
    Radio.tx_power_set(Radio.internal);
    TEST_ASSERT_FALSE(Radio.ok);
    Radio.tx.dbm = -4;
    Radio.tx_power_set(Radio.internal);
    TEST_ASSERT_FALSE(Radio.ok);
}

static void on_frame(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel)
{
    (void)frame;
    (void)len;
    (void)rssi;
    (void)channel;
}

// Monitor capture needs a radio to tune, so beginning one is refused whether or not a sink is given,
// and the two calls that return nothing must still be safe to make.
void test_monitor_refuses_without_a_radio(void)
{
    Radio.monitor.channel = 6;
    Radio.monitor.on_frame = NULL;
    Radio.monitor_begin(Radio.internal);
    TEST_ASSERT_FALSE(Radio.ok);

    Radio.monitor.on_frame = on_frame;
    Radio.monitor_begin(Radio.internal);
    TEST_ASSERT_FALSE(Radio.ok);

    Radio.monitor.channel = 11;
    Radio.monitor_set_channel(Radio.internal);
    Radio.monitor_end(Radio.internal);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode());
}

// Applying the configured mode with no radio changes nothing and must not fault.
void test_power_is_a_no_op_without_a_radio(void)
{
    Radio.power(Radio.internal);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode());
}

// The keep-awake refcount has no mode to hold with no radio, so hold, release, and an unbalanced
// release all leave the readback where it was.
void test_busy_hold_release_is_a_no_op_without_a_radio(void)
{
    Radio.busy_hold(Radio.internal);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode());

    Radio.busy_release(Radio.internal);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode());

    Radio.busy_release(Radio.internal);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode());
}

// The handle carries the state its calls reach, so a missing binding faults at the first call rather
// than at the link.
void test_handle_is_bound(void)
{
    TEST_ASSERT_NOT_NULL(Radio.internal);
}
