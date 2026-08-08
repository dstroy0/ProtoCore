// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the radio-power mode names (network_drivers/physical/radio_power). Applying the
// settings to the radio is ESP32-only (a no-op on host).

#include "network_drivers/physical/radio_power.h"
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_ps_names()
{
    TEST_ASSERT_EQUAL_STRING("none", Radio.ps_name(PC_PHY_PS_NONE));
    TEST_ASSERT_EQUAL_STRING("min_modem", Radio.ps_name(PC_PHY_PS_MIN_MODEM));
    TEST_ASSERT_EQUAL_STRING("max_modem", Radio.ps_name(PC_PHY_PS_MAX_MODEM));
    TEST_ASSERT_EQUAL_STRING("none", Radio.ps_name(99)); // unknown -> none
}

void test_apply_is_noop_on_host()
{
    Radio.power(); // must not crash
    TEST_ASSERT_EQUAL_UINT8(PC_PHY_PS_NONE, Radio.ps_mode());
}

void test_busy_hold_release_is_noop_on_host()
{
    // Bulk-transfer keep-awake refcount is ESP32-only; on host both calls are no-ops
    // that must not crash and must not perturb the readback.
    Radio.busy_hold();
    TEST_ASSERT_EQUAL_UINT8(PC_PHY_PS_NONE, Radio.ps_mode());
    Radio.busy_release();
    TEST_ASSERT_EQUAL_UINT8(PC_PHY_PS_NONE, Radio.ps_mode());
    // Unbalanced release (no matching hold) is still a no-op on host.
    Radio.busy_release();
    TEST_ASSERT_EQUAL_UINT8(PC_PHY_PS_NONE, Radio.ps_mode());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_ps_names);
    RUN_TEST(test_apply_is_noop_on_host);
    RUN_TEST(test_busy_hold_release_is_noop_on_host);
    return UNITY_END();
}
