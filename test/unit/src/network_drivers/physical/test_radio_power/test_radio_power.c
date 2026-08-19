// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for 802.11 power management, driven against the radio the host build compiles
// (network_drivers/physical/radio_power.h).
//
// IEEE Std 802.11-2020 governs every call here and no IETF RFC does. 11.2.3.2 names the two modes a
// non-AP STA runs in, active mode and PS mode, 6.3.2.2 (MLME-POWERMGT.request) is the primitive that
// selects one, and 11.7 governs transmit power in dBm. The standard publishes no C API, so the
// expectations are properties: a mode that is set must read back or the set must report failure, and
// the name of a mode is fixed text rather than whatever a vendor calls it.
//
// test_busy_hold_forces_active_and_release_restores is the load-bearing case. There is a radio
// under the handle in every build, so a mode that is applied has to read back and the keep-awake
// refcount has to put the configured mode back when the last holder releases: a transfer that
// leaves the radio dozing, or one that leaves it awake, is the failure this catches.

#include "network_drivers/physical/radio_power/radio_power.h"

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
    Radio.ps_name(protocore_radio_power_span());
    return Radio.text;
}

static protocore_phy_ps ps_mode(void)
{
    Radio.ps_mode(protocore_radio_power_span());
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

// 802.11-2020 6.3.2.2: an applied mode is the mode the radio reports back.
void test_apply_sets_the_mode_and_reads_it_back(void)
{
    Radio.ps.mode = PROTOCORE_PHY_PS_MAX_MODEM;
    Radio.ps_set(protocore_radio_power_span());
    TEST_ASSERT_TRUE(Radio.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_MAX_MODEM, ps_mode());

    Radio.ps.mode = PROTOCORE_PHY_PS_MIN_MODEM;
    Radio.ps_set(protocore_radio_power_span());
    TEST_ASSERT_TRUE(Radio.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_MIN_MODEM, ps_mode());

    Radio.ps.mode = PROTOCORE_PHY_PS_NONE;
    Radio.ps_set(protocore_radio_power_span());
    TEST_ASSERT_TRUE(Radio.ok);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode());

    // 802.11-2020 11.7.6 selects a transmit power in dBm; the backend takes both signs.
    Radio.tx.dbm = 11;
    Radio.tx_power_set(protocore_radio_power_span());
    TEST_ASSERT_TRUE(Radio.ok);
    Radio.tx.dbm = -4;
    Radio.tx_power_set(protocore_radio_power_span());
    TEST_ASSERT_TRUE(Radio.ok);
}

static void on_frame(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel)
{
    (void)frame;
    (void)len;
    (void)rssi;
    (void)channel;
}

// Capture with nowhere to deliver is a caller bug, so it is refused; with a sink the radio arms,
// retunes, and stops.
void test_monitor_arms_and_refuses_a_null_sink(void)
{
    Radio.monitor.channel = 6;
    Radio.monitor.on_frame = NULL;
    Radio.monitor_begin(protocore_radio_power_span());
    TEST_ASSERT_FALSE(Radio.ok);

    Radio.monitor.on_frame = on_frame;
    Radio.monitor_begin(protocore_radio_power_span());
    TEST_ASSERT_TRUE(Radio.ok);

    Radio.monitor.channel = 11;
    Radio.monitor_set_channel(protocore_radio_power_span());
    Radio.monitor_end(protocore_radio_power_span());
}

// Applying the configured mode puts it on whatever the radio was left in.
void test_power_applies_the_configured_mode(void)
{
    Radio.ps.mode = PROTOCORE_PHY_PS_MAX_MODEM;
    Radio.ps_set(protocore_radio_power_span());
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_MAX_MODEM, ps_mode());

    Radio.power(protocore_radio_power_span());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)PROTOCORE_RADIO_WIFI_PS, ps_mode());
}

// The keep-awake refcount forces active mode for the length of a transfer and puts the configured
// mode back when the last holder releases. A release with nothing held changes nothing.
void test_busy_hold_forces_active_and_release_restores(void)
{
    Radio.ps.mode = PROTOCORE_PHY_PS_MAX_MODEM;
    Radio.ps_set(protocore_radio_power_span());

    Radio.busy_hold(protocore_radio_power_span());
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode()); // active for the transfer

    // Nested: the inner release is not the last one, so the mode stays active.
    Radio.busy_hold(protocore_radio_power_span());
    Radio.busy_release(protocore_radio_power_span());
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PHY_PS_NONE, ps_mode());

    Radio.busy_release(protocore_radio_power_span());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)PROTOCORE_RADIO_WIFI_PS, ps_mode()); // last holder: configured back

    // Unbalanced: nothing is held, so there is nothing to restore.
    Radio.busy_release(protocore_radio_power_span());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)PROTOCORE_RADIO_WIFI_PS, ps_mode());
}

// The borrow is where the state lives, so a pool too short to carve it is caught at the first call
// rather than by a read through a null.
void test_the_borrow_is_carved(void)
{
    TEST_ASSERT_NOT_NULL(protocore_radio_power_span());
}
