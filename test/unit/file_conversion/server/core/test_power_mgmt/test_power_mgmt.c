// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/core/power_mgmt.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const PowerCfg CFG = {
    .mhz_max = 240,
    .mhz_min = 80,
    .busy_pct = 40,
    .temp_hot_c = 80,
    .temp_cool_c = 70,
    .recover_ms = 10000,
};

static PowerPlan decide(uint8_t load_pct, int16_t temp_c, proto_bool brownout, uint32_t since_boot_ms,
                        proto_bool was_throttled)
{
    Power.plan_args.cfg = &CFG;
    Power.plan_args.load_pct = load_pct;
    Power.plan_args.temp_c = temp_c;
    Power.plan_args.brownout_boot = brownout;
    Power.plan_args.since_boot_ms = since_boot_ms;
    Power.plan_args.was_throttled = was_throttled;
    Power.decide(Power.internal);
    return Power.plan;
}

static char g_json[128];

static const char *report(const PowerPlan *plan, int16_t temp_c, size_t cap)
{
    Power.out_args.plan = plan;
    Power.out_args.temp_c = temp_c;
    Power.out_args.out = g_json;
    Power.out_args.cap = cap;
    Power.json(Power.internal);
    return g_json;
}

void test_the_throttle_cannot_flap(void)
{
    for (int16_t t = (int16_t)(CFG.temp_cool_c + 1); t < CFG.temp_hot_c; t++)
    {
        PowerPlan cold = decide(100, t, PROTO_FALSE, 60000u, PROTO_FALSE);
        TEST_ASSERT_FALSE(cold.throttled);
        TEST_ASSERT_EQUAL_UINT16(CFG.mhz_max, cold.cpu_mhz);

        PowerPlan hot = decide(100, t, PROTO_FALSE, 60000u, PROTO_TRUE);
        TEST_ASSERT_TRUE(hot.throttled);
        TEST_ASSERT_EQUAL_UINT16(CFG.mhz_min, hot.cpu_mhz);

        TEST_ASSERT_FALSE(decide(100, t, PROTO_FALSE, 60000u, cold.throttled).throttled);
        TEST_ASSERT_TRUE(decide(100, t, PROTO_FALSE, 60000u, hot.throttled).throttled);
    }
}

void test_the_throttle_engages_at_the_hot_threshold(void)
{
    TEST_ASSERT_FALSE(decide(100, (int16_t)(CFG.temp_hot_c - 1), PROTO_FALSE, 60000u, PROTO_FALSE).throttled);
    TEST_ASSERT_TRUE(decide(100, CFG.temp_hot_c, PROTO_FALSE, 60000u, PROTO_FALSE).throttled);
    TEST_ASSERT_TRUE(decide(100, (int16_t)(CFG.temp_hot_c + 50), PROTO_FALSE, 60000u, PROTO_FALSE).throttled);
}

void test_the_throttle_releases_at_the_cool_threshold(void)
{
    TEST_ASSERT_TRUE(decide(100, (int16_t)(CFG.temp_cool_c + 1), PROTO_FALSE, 60000u, PROTO_TRUE).throttled);
    TEST_ASSERT_FALSE(decide(100, CFG.temp_cool_c, PROTO_FALSE, 60000u, PROTO_TRUE).throttled);
    TEST_ASSERT_FALSE(decide(100, (int16_t)(CFG.temp_cool_c - 50), PROTO_FALSE, 60000u, PROTO_TRUE).throttled);
}

void test_no_sensor_is_not_a_cold_reading(void)
{
    PowerPlan p = decide(100, INT16_MIN, PROTO_FALSE, 60000u, PROTO_TRUE);
    TEST_ASSERT_FALSE(p.throttled);
    TEST_ASSERT_EQUAL_UINT16(CFG.mhz_max, p.cpu_mhz);
    TEST_ASSERT_FALSE(decide(0, INT16_MIN, PROTO_FALSE, 60000u, PROTO_FALSE).throttled);
}

void test_the_load_picks_the_rail(void)
{
    TEST_ASSERT_EQUAL_UINT16(CFG.mhz_min, decide((uint8_t)(CFG.busy_pct - 1), 25, PROTO_FALSE, 60000u, PROTO_FALSE).cpu_mhz);
    TEST_ASSERT_EQUAL_UINT16(CFG.mhz_max, decide(CFG.busy_pct, 25, PROTO_FALSE, 60000u, PROTO_FALSE).cpu_mhz);
    TEST_ASSERT_EQUAL_UINT16(CFG.mhz_max, decide(100, 25, PROTO_FALSE, 60000u, PROTO_FALSE).cpu_mhz);
    TEST_ASSERT_EQUAL_UINT16(CFG.mhz_min, decide(0, 25, PROTO_FALSE, 60000u, PROTO_FALSE).cpu_mhz);
}

void test_a_load_above_a_hundred_is_clamped(void)
{
    TEST_ASSERT_EQUAL_UINT16(CFG.mhz_max, decide(255, 25, PROTO_FALSE, 60000u, PROTO_FALSE).cpu_mhz);
}

void test_a_brownout_boot_holds_the_floor_for_its_window(void)
{
    PowerPlan inside = decide(100, 25, PROTO_TRUE, CFG.recover_ms - 1u, PROTO_FALSE);
    TEST_ASSERT_TRUE(inside.recovering);
    TEST_ASSERT_EQUAL_UINT16(CFG.mhz_min, inside.cpu_mhz);

    PowerPlan at_the_edge = decide(100, 25, PROTO_TRUE, CFG.recover_ms, PROTO_FALSE);
    TEST_ASSERT_FALSE(at_the_edge.recovering);
    TEST_ASSERT_EQUAL_UINT16(CFG.mhz_max, at_the_edge.cpu_mhz);

    TEST_ASSERT_FALSE(decide(100, 25, PROTO_FALSE, 0u, PROTO_FALSE).recovering);
}

void test_either_hold_forces_the_floor(void)
{
    PowerPlan both = decide(100, (int16_t)(CFG.temp_hot_c + 5), PROTO_TRUE, 0u, PROTO_FALSE);
    TEST_ASSERT_TRUE(both.throttled);
    TEST_ASSERT_TRUE(both.recovering);
    TEST_ASSERT_EQUAL_UINT16(CFG.mhz_min, both.cpu_mhz);
}

void test_a_null_config_decides_nothing(void)
{
    Power.plan_args.cfg = NULL;
    Power.plan_args.load_pct = 100;
    Power.plan_args.temp_c = 25;
    Power.plan_args.brownout_boot = PROTO_TRUE;
    Power.plan_args.since_boot_ms = 0;
    Power.plan_args.was_throttled = PROTO_TRUE;
    Power.decide(Power.internal);
    TEST_ASSERT_EQUAL_UINT16(0, Power.plan.cpu_mhz);
    TEST_ASSERT_FALSE(Power.plan.throttled);
    TEST_ASSERT_FALSE(Power.plan.recovering);
}

void test_the_defaults_carry_a_hysteresis_gap(void)
{
    PowerCfg cfg;
    memset(&cfg, 0xFF, sizeof(cfg));
    Power.cfg_out = &cfg;
    Power.defaults(Power.internal);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_POWER_MHZ_MAX, cfg.mhz_max);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_POWER_MHZ_MIN, cfg.mhz_min);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_POWER_BUSY_PCT, cfg.busy_pct);
    TEST_ASSERT_EQUAL_INT16(PROTOCORE_POWER_TEMP_HOT_C, cfg.temp_hot_c);
    TEST_ASSERT_EQUAL_INT16(PROTOCORE_POWER_TEMP_COOL_C, cfg.temp_cool_c);
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_POWER_RECOVER_MS, cfg.recover_ms);
    TEST_ASSERT_TRUE(cfg.temp_cool_c < cfg.temp_hot_c);
    TEST_ASSERT_TRUE(cfg.mhz_min < cfg.mhz_max);

    Power.cfg_out = NULL;
    Power.defaults(Power.internal);
}

void test_the_report_is_an_rfc8259_object(void)
{
    PowerPlan p = decide(100, 25, PROTO_FALSE, 60000u, PROTO_FALSE);
    TEST_ASSERT_EQUAL_STRING("{\"cpu_mhz\":240,\"throttled\":false,\"recovering\":false,\"temp_c\":25}",
                             report(&p, 25, sizeof(g_json)));
    TEST_ASSERT_EQUAL_size_t(strlen(g_json), Power.n);

    PowerPlan hot = decide(100, (int16_t)(CFG.temp_hot_c + 5), PROTO_TRUE, 0u, PROTO_FALSE);
    TEST_ASSERT_EQUAL_STRING("{\"cpu_mhz\":80,\"throttled\":true,\"recovering\":true,\"temp_c\":85}",
                             report(&hot, (int16_t)(CFG.temp_hot_c + 5), sizeof(g_json)));
}

void test_no_sensor_is_reported_as_null(void)
{
    PowerPlan p = decide(100, INT16_MIN, PROTO_FALSE, 60000u, PROTO_FALSE);
    TEST_ASSERT_EQUAL_STRING("{\"cpu_mhz\":240,\"throttled\":false,\"recovering\":false,\"temp_c\":null}",
                             report(&p, INT16_MIN, sizeof(g_json)));
}

void test_a_below_zero_reading_keeps_its_sign(void)
{
    PowerPlan p = decide(0, -40, PROTO_FALSE, 60000u, PROTO_FALSE);
    TEST_ASSERT_EQUAL_STRING("{\"cpu_mhz\":80,\"throttled\":false,\"recovering\":false,\"temp_c\":-40}",
                             report(&p, -40, sizeof(g_json)));
}

void test_the_report_fails_closed(void)
{
    PowerPlan p = decide(100, 25, PROTO_FALSE, 60000u, PROTO_FALSE);
    TEST_ASSERT_EQUAL_STRING("", report(&p, 25, 8));
    TEST_ASSERT_EQUAL_size_t(0u, Power.n);

    Power.out_args.plan = NULL;
    Power.out_args.out = g_json;
    Power.out_args.cap = sizeof(g_json);
    Power.json(Power.internal);
    TEST_ASSERT_EQUAL_size_t(0u, Power.n);

    Power.out_args.plan = &p;
    Power.out_args.out = NULL;
    Power.json(Power.internal);
    TEST_ASSERT_EQUAL_size_t(0u, Power.n);

    Power.out_args.out = g_json;
    Power.out_args.cap = 0;
    Power.json(Power.internal);
    TEST_ASSERT_EQUAL_size_t(0u, Power.n);
}
