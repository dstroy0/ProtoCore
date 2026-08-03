// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SoC power governor (server/power_mgmt): load-based scaling, the thermal
// hysteresis that stops a part at the limit oscillating, post-brownout recovery, the precedence
// between the three, and the no-sensor case.

#include "server/power_mgmt.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

static PowerCfg cfg;

void setUp()
{
    pc_power_cfg_defaults(&cfg);
    // Pin the values the tests reason about, so a change to the shipped defaults does not silently
    // rewrite what these assertions mean.
    cfg.mhz_max = 240;
    cfg.mhz_min = 80;
    cfg.busy_pct = 40;
    cfg.temp_hot_c = 80;
    cfg.temp_cool_c = 70;
    cfg.recover_ms = 10000;
}
void tearDown()
{
}

static PowerPlan plan(uint8_t load, int16_t temp, proto_bool bo, uint32_t up, proto_bool was)
{
    return pc_power_plan(&cfg, load, temp, bo, up, was);
}

// --- load scaling ---------------------------------------------------------

void test_idle_runs_at_the_floor()
{
    PowerPlan p = plan(0, 40, PROTO_FALSE, 60000, PROTO_FALSE);
    TEST_ASSERT_EQUAL_UINT16(80, p.cpu_mhz);
    TEST_ASSERT_FALSE(p.throttled);
    TEST_ASSERT_FALSE(p.recovering);
}

void test_busy_runs_at_the_ceiling()
{
    PowerPlan p = plan(90, 40, PROTO_FALSE, 60000, PROTO_FALSE);
    TEST_ASSERT_EQUAL_UINT16(240, p.cpu_mhz);
}

void test_busy_threshold_is_inclusive()
{
    TEST_ASSERT_EQUAL_UINT16(80, plan(39, 40, PROTO_FALSE, 60000, PROTO_FALSE).cpu_mhz);
    TEST_ASSERT_EQUAL_UINT16(240, plan(40, 40, PROTO_FALSE, 60000, PROTO_FALSE).cpu_mhz);
}

void test_load_above_100_is_clamped_not_wrapped()
{
    TEST_ASSERT_EQUAL_UINT16(240, plan(255, 40, PROTO_FALSE, 60000, PROTO_FALSE).cpu_mhz);
}

// --- thermal hysteresis ---------------------------------------------------

void test_hot_die_throttles_even_when_busy()
{
    PowerPlan p = plan(100, 85, PROTO_FALSE, 60000, PROTO_FALSE);
    TEST_ASSERT_TRUE(p.throttled);
    TEST_ASSERT_EQUAL_UINT16(80, p.cpu_mhz);
}

void test_throttle_threshold_is_inclusive()
{
    TEST_ASSERT_FALSE(plan(100, 79, PROTO_FALSE, 60000, PROTO_FALSE).throttled);
    TEST_ASSERT_TRUE(plan(100, 80, PROTO_FALSE, 60000, PROTO_FALSE).throttled);
}

void test_throttle_holds_between_the_two_thresholds()
{
    // 75 C is below the throttle point but above the restore point: once throttled it must stay
    // throttled, which is the whole reason the two thresholds differ.
    TEST_ASSERT_TRUE(plan(100, 75, PROTO_FALSE, 60000, PROTO_TRUE).throttled);
    TEST_ASSERT_FALSE(plan(100, 75, PROTO_FALSE, 60000, PROTO_FALSE).throttled);
}

void test_throttle_releases_at_the_cool_threshold()
{
    TEST_ASSERT_TRUE(plan(100, 71, PROTO_FALSE, 60000, PROTO_TRUE).throttled);
    TEST_ASSERT_FALSE(plan(100, 70, PROTO_FALSE, 60000, PROTO_TRUE).throttled);
}

void test_no_oscillation_when_parked_at_the_limit()
{
    // Feed the plan's own output back in, exactly as a caller does, while the die sits at the
    // throttle point. A single-threshold governor alternates here forever.
    proto_bool was = PROTO_FALSE;
    int changes = 0;
    for (int i = 0; i < 20; i++)
    {
        PowerPlan p = plan(100, 80, PROTO_FALSE, 60000, was);
        if (p.throttled != was)
        {
            changes++;
        }
        was = p.throttled;
    }
    TEST_ASSERT_EQUAL_INT(1, changes); // one entry into throttle, and then stable
}

// --- brownout recovery ----------------------------------------------------

void test_brownout_boot_holds_the_floor_even_when_busy_and_cool()
{
    PowerPlan p = plan(100, 25, PROTO_TRUE, 0, PROTO_FALSE);
    TEST_ASSERT_TRUE(p.recovering);
    TEST_ASSERT_EQUAL_UINT16(80, p.cpu_mhz);
}

void test_recovery_window_ends()
{
    TEST_ASSERT_TRUE(plan(100, 25, PROTO_TRUE, 9999, PROTO_FALSE).recovering);
    PowerPlan p = plan(100, 25, PROTO_TRUE, 10000, PROTO_FALSE);
    TEST_ASSERT_FALSE(p.recovering);
    TEST_ASSERT_EQUAL_UINT16(240, p.cpu_mhz); // and full speed is allowed again
}

void test_normal_boot_never_recovers()
{
    TEST_ASSERT_FALSE(plan(100, 25, PROTO_FALSE, 0, PROTO_FALSE).recovering);
    TEST_ASSERT_EQUAL_UINT16(240, plan(100, 25, PROTO_FALSE, 0, PROTO_FALSE).cpu_mhz);
}

void test_brownout_and_hot_both_reported()
{
    // Precedence puts both at the floor, but the flags must still say why - a caller logging this
    // should be able to tell a thermal event from a supply event.
    PowerPlan p = plan(100, 90, PROTO_TRUE, 100, PROTO_TRUE);
    TEST_ASSERT_TRUE(p.recovering);
    TEST_ASSERT_TRUE(p.throttled);
    TEST_ASSERT_EQUAL_UINT16(80, p.cpu_mhz);
}

// --- no sensor ------------------------------------------------------------

void test_missing_sensor_does_not_read_as_ice_cold()
{
    // INT16_MIN means "this part has no sensor". Treating it as a temperature would both refuse to
    // ever throttle and silently release a throttle that was already on.
    PowerPlan p = plan(100, INT16_MIN, PROTO_FALSE, 60000, PROTO_TRUE);
    TEST_ASSERT_FALSE(p.throttled);
    TEST_ASSERT_EQUAL_UINT16(240, p.cpu_mhz);
}

// --- misc -----------------------------------------------------------------

void test_null_cfg_is_not_a_crash()
{
    PowerPlan p = pc_power_plan(NULL, 50, 40, PROTO_FALSE, 0, PROTO_FALSE);
    TEST_ASSERT_EQUAL_UINT16(0, p.cpu_mhz);
}

void test_null_cfg_defaults_is_not_a_crash()
{
    pc_power_cfg_defaults(NULL); // must not crash; there is no output to assert on
}

void test_defaults_are_self_consistent()
{
    PowerCfg d;
    pc_power_cfg_defaults(&d);
    TEST_ASSERT_TRUE(d.temp_cool_c < d.temp_hot_c); // hysteresis must have a gap
    TEST_ASSERT_TRUE(d.mhz_min <= d.mhz_max);
    TEST_ASSERT_TRUE(d.busy_pct <= 100);
}

void test_json()
{
    char buf[128];
    PowerPlan p = plan(0, 41, PROTO_FALSE, 60000, PROTO_FALSE);
    TEST_ASSERT_TRUE(pc_power_json(&p, 41, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"cpu_mhz\":80,\"throttled\":false,\"recovering\":false,\"temp_c\":41}", buf);

    PowerPlan h = plan(100, 85, PROTO_FALSE, 60000, PROTO_FALSE);
    TEST_ASSERT_TRUE(pc_power_json(&h, 85, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"cpu_mhz\":80,\"throttled\":true,\"recovering\":false,\"temp_c\":85}", buf);
}

void test_json_reports_a_missing_sensor_as_null()
{
    char buf[128];
    PowerPlan p = plan(0, INT16_MIN, PROTO_FALSE, 60000, PROTO_FALSE);
    TEST_ASSERT_TRUE(pc_power_json(&p, INT16_MIN, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"cpu_mhz\":80,\"throttled\":false,\"recovering\":false,\"temp_c\":null}", buf);
}

void test_json_missing_sensor_reports_throttled_and_recovering_true()
{
    // The no-sensor branch has its own throttled/recovering ternaries; exercise both true arms,
    // which test_json_reports_a_missing_sensor_as_null (both false) does not reach.
    char buf[128];
    PowerPlan p;
    p.cpu_mhz = 80;
    p.throttled = PROTO_TRUE;
    p.recovering = PROTO_TRUE;
    TEST_ASSERT_TRUE(pc_power_json(&p, INT16_MIN, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"cpu_mhz\":80,\"throttled\":true,\"recovering\":true,\"temp_c\":null}", buf);
}

void test_json_with_a_sensor_reading_reports_recovering_true()
{
    // test_json only ever sees recovering=false; cover the recovering-true arm of the
    // has-a-sensor branch's ternary too.
    char buf[128];
    PowerPlan p;
    p.cpu_mhz = 80;
    p.throttled = PROTO_FALSE;
    p.recovering = PROTO_TRUE;
    TEST_ASSERT_TRUE(pc_power_json(&p, 41, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"cpu_mhz\":80,\"throttled\":false,\"recovering\":true,\"temp_c\":41}", buf);
}

void test_json_overflow_is_fail_closed()
{
    char tiny[12];
    PowerPlan p = plan(0, 41, PROTO_FALSE, 60000, PROTO_FALSE);
    TEST_ASSERT_EQUAL_UINT32(0, pc_power_json(&p, 41, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("", tiny);
    TEST_ASSERT_EQUAL_UINT32(0, pc_power_json(NULL, 41, tiny, sizeof(tiny)));
}

void test_json_null_out_is_rejected()
{
    PowerPlan p = plan(0, 41, PROTO_FALSE, 60000, PROTO_FALSE);
    TEST_ASSERT_EQUAL_UINT32(0, pc_power_json(&p, 41, NULL, 128));
}

void test_json_zero_cap_is_rejected()
{
    char buf[128];
    PowerPlan p = plan(0, 41, PROTO_FALSE, 60000, PROTO_FALSE);
    TEST_ASSERT_EQUAL_UINT32(0, pc_power_json(&p, 41, buf, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_idle_runs_at_the_floor);
    RUN_TEST(test_busy_runs_at_the_ceiling);
    RUN_TEST(test_busy_threshold_is_inclusive);
    RUN_TEST(test_load_above_100_is_clamped_not_wrapped);
    RUN_TEST(test_hot_die_throttles_even_when_busy);
    RUN_TEST(test_throttle_threshold_is_inclusive);
    RUN_TEST(test_throttle_holds_between_the_two_thresholds);
    RUN_TEST(test_throttle_releases_at_the_cool_threshold);
    RUN_TEST(test_no_oscillation_when_parked_at_the_limit);
    RUN_TEST(test_brownout_boot_holds_the_floor_even_when_busy_and_cool);
    RUN_TEST(test_recovery_window_ends);
    RUN_TEST(test_normal_boot_never_recovers);
    RUN_TEST(test_brownout_and_hot_both_reported);
    RUN_TEST(test_missing_sensor_does_not_read_as_ice_cold);
    RUN_TEST(test_null_cfg_is_not_a_crash);
    RUN_TEST(test_null_cfg_defaults_is_not_a_crash);
    RUN_TEST(test_defaults_are_self_consistent);
    RUN_TEST(test_json);
    RUN_TEST(test_json_reports_a_missing_sensor_as_null);
    RUN_TEST(test_json_missing_sensor_reports_throttled_and_recovering_true);
    RUN_TEST(test_json_with_a_sensor_reading_reports_recovering_true);
    RUN_TEST(test_json_overflow_is_fail_closed);
    RUN_TEST(test_json_null_out_is_rejected);
    RUN_TEST(test_json_zero_cap_is_rejected);
    return UNITY_END();
}
