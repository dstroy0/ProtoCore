// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SoC power governor (server/core/power_mgmt.h).
//
// No published standard governs a clock governor, so the decision cases take their expectations from
// exactly two places and nowhere else. The thresholds come from the published contract in
// power_mgmt.h - temp_hot_c "throttle at/above this die temperature", temp_cool_c "release the
// throttle at/below this one", busy_pct "load at/above which the ceiling is used", mhz_min "clock
// when idle, throttled, or recovering" - and from protocore_config.h, which enforces
// temp_cool_c < temp_hot_c, mhz_min <= mhz_max and busy_pct in 0..100 with #error at lines
// 3639-3649. Everything else is a property that holds whatever the governor does: the band between
// the two thresholds retains state, a feedback ramp crosses once per direction, decide is a pure
// function of its arguments, and a percentage saturates at 100.
//
// The report cases are anchored on RFC 8259. The document is walked with the sec 2 / 4 / 6 / 7
// grammar transcribed into json_ok() below rather than compared to a golden string, because the
// field names are the module's own and nothing publishes them; what RFC 8259 does publish is the
// literal spelling (sec 3 lines 301-314: false, null, true, "MUST be lowercase") and the number form
// (sec 6: no leading zeros, no plus sign), and those are what the cases assert.
//
// test_no_sensor_holds_a_throttle_already_held IS EXPECTED TO FAIL. power_mgmt.c line 81 publishes
// the rule for the sentinel - "INT16_MIN means 'no sensor on this part', which must not read as
// ice-cold and un-throttle" - and lines 87-90 do exactly that, clearing the throttle unconditionally
// when there is no sensor. The case asserts the documented rule, not the code.

#include "server/core/power_mgmt/power_mgmt.h"

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

// Run the decision over one set of arguments and hand back what it decided.
static PowerPlan decide_cfg(const PowerCfg *cfg, uint8_t load_pct, int16_t temp_c, proto_bool brownout,
                            uint32_t since_boot_ms, proto_bool was_throttled)
{
    Power.plan_args.cfg = cfg;
    Power.plan_args.load_pct = load_pct;
    Power.plan_args.temp_c = temp_c;
    Power.plan_args.brownout_boot = brownout;
    Power.plan_args.since_boot_ms = since_boot_ms;
    Power.plan_args.was_throttled = was_throttled;
    Power.decide(protocore_power_mgmt_span());
    return Power.plan;
}

static PowerPlan decide(uint8_t load_pct, int16_t temp_c, proto_bool brownout, uint32_t since_boot_ms,
                        proto_bool was_throttled)
{
    return decide_cfg(&CFG, load_pct, temp_c, brownout, since_boot_ms, was_throttled);
}

// ---------------------------------------------------------------------------
// The decision
// ---------------------------------------------------------------------------

// power_mgmt.h: temp_hot_c is "throttle at/above this die temperature", so hot-1 does not and hot
// does, from a cold start.
void test_the_throttle_engages_at_the_hot_threshold(void)
{
    TEST_ASSERT_FALSE(decide(100, (int16_t)(CFG.temp_hot_c - 1), PROTO_FALSE, 60000u, PROTO_FALSE).throttled);
    TEST_ASSERT_TRUE(decide(100, CFG.temp_hot_c, PROTO_FALSE, 60000u, PROTO_FALSE).throttled);
    TEST_ASSERT_TRUE(decide(100, (int16_t)(CFG.temp_hot_c + 50), PROTO_FALSE, 60000u, PROTO_FALSE).throttled);
}

// power_mgmt.h: temp_cool_c is "release the throttle at/below this one", so cool+1 holds and cool
// releases, from a throttled start.
void test_the_throttle_releases_at_the_cool_threshold(void)
{
    TEST_ASSERT_TRUE(decide(100, (int16_t)(CFG.temp_cool_c + 1), PROTO_FALSE, 60000u, PROTO_TRUE).throttled);
    TEST_ASSERT_FALSE(decide(100, CFG.temp_cool_c, PROTO_FALSE, 60000u, PROTO_TRUE).throttled);
    TEST_ASSERT_FALSE(decide(100, (int16_t)(CFG.temp_cool_c - 50), PROTO_FALSE, 60000u, PROTO_TRUE).throttled);
}

// Between the two thresholds neither rule applies - the temperature is below hot and above cool - so
// the only answer the pair defines is the state that was already held.
void test_the_band_between_the_thresholds_retains_state(void)
{
    for (int16_t t = (int16_t)(CFG.temp_cool_c + 1); t < CFG.temp_hot_c; t++)
    {
        TEST_ASSERT_FALSE(decide(100, t, PROTO_FALSE, 60000u, PROTO_FALSE).throttled);
        TEST_ASSERT_TRUE(decide(100, t, PROTO_FALSE, 60000u, PROTO_TRUE).throttled);
    }
}

// Feed the decision its own previous output over a ramp up through hot and back down through cool.
// A single threshold flaps once per degree inside the band; two thresholds cross once per direction.
void test_a_feedback_ramp_crosses_once_per_direction(void)
{
    proto_bool held = PROTO_FALSE;
    int up = 0;
    for (int16_t t = (int16_t)(CFG.temp_cool_c - 10); t <= (int16_t)(CFG.temp_hot_c + 10); t++)
    {
        proto_bool now = decide(100, t, PROTO_FALSE, 60000u, held).throttled;
        if (now != held)
        {
            up++;
        }
        held = now;
    }
    TEST_ASSERT_TRUE(held);
    TEST_ASSERT_EQUAL_INT(1, up);

    int down = 0;
    for (int16_t t = (int16_t)(CFG.temp_hot_c + 10); t >= (int16_t)(CFG.temp_cool_c - 10); t--)
    {
        proto_bool now = decide(100, t, PROTO_FALSE, 60000u, held).throttled;
        if (now != held)
        {
            down++;
        }
        held = now;
    }
    TEST_ASSERT_FALSE(held);
    TEST_ASSERT_EQUAL_INT(1, down);
}

// power_mgmt.c: "INT16_MIN means 'no sensor on this part', which must not read as ice-cold and
// un-throttle." A part with no sensor therefore keeps a throttle it was already holding, which is
// what separates the sentinel from the coldest reading an int16 can carry - INT16_MIN+1 is a
// temperature, is at/below cool, and does release.
void test_no_sensor_holds_a_throttle_already_held(void)
{
    TEST_ASSERT_FALSE(decide(100, (int16_t)(INT16_MIN + 1), PROTO_FALSE, 60000u, PROTO_TRUE).throttled);
    TEST_ASSERT_TRUE(decide(100, INT16_MIN, PROTO_FALSE, 60000u, PROTO_TRUE).throttled);
}

// A threshold cannot be crossed by a reading that does not exist, so an absent sensor never starts a
// throttle on its own.
void test_no_sensor_never_engages_a_throttle(void)
{
    TEST_ASSERT_FALSE(decide(0, INT16_MIN, PROTO_FALSE, 60000u, PROTO_FALSE).throttled);
    TEST_ASSERT_FALSE(decide(100, INT16_MIN, PROTO_FALSE, 60000u, PROTO_FALSE).throttled);
    TEST_ASSERT_FALSE(decide(255, INT16_MIN, PROTO_TRUE, 0u, PROTO_FALSE).throttled);
}

// power_mgmt.h: busy_pct is "load at/above which the ceiling is used", mhz_max the "clock when there
// is work to do" and mhz_min the one "when idle". Swept over every busy_pct the config contract
// admits (protocore_config.h line 3647 rejects above 100) and every uint8_t a caller can report.
void test_the_load_picks_the_rail(void)
{
    static const uint8_t BUSY[] = {0u, 1u, 40u, 99u, 100u};
    for (size_t b = 0; b < sizeof(BUSY) / sizeof(BUSY[0]); b++)
    {
        PowerCfg cfg = CFG;
        cfg.busy_pct = BUSY[b];
        for (int load = 0; load <= 255; load++)
        {
            const uint8_t pct = (load > 100) ? 100u : (uint8_t)load; // a percentage saturates at 100
            const uint16_t want = (pct >= cfg.busy_pct) ? cfg.mhz_max : cfg.mhz_min;
            TEST_ASSERT_EQUAL_UINT16(want,
                                     decide_cfg(&cfg, (uint8_t)load, 25, PROTO_FALSE, 60000u, PROTO_FALSE).cpu_mhz);
        }
    }
}

// load_pct is a percentage, so anything a caller reports above 100 decides exactly as 100 does.
void test_a_load_over_a_hundred_decides_as_a_hundred(void)
{
    const PowerPlan at_full = decide(100, 25, PROTO_FALSE, 60000u, PROTO_FALSE);
    for (int load = 101; load <= 255; load++)
    {
        const PowerPlan p = decide((uint8_t)load, 25, PROTO_FALSE, 60000u, PROTO_FALSE);
        TEST_ASSERT_EQUAL_UINT16(at_full.cpu_mhz, p.cpu_mhz);
        TEST_ASSERT_EQUAL_INT(at_full.throttled, p.throttled);
    }
}

// power_mgmt.h: recover_ms is "how long to stay at the floor after a brownout reset". A window of
// recover_ms milliseconds opened at since_boot_ms 0 covers 0 .. recover_ms-1, so recover_ms itself
// is the first instant outside it.
void test_a_brownout_boot_holds_the_floor_for_its_window(void)
{
    TEST_ASSERT_TRUE(decide(100, 25, PROTO_TRUE, 0u, PROTO_FALSE).recovering);
    TEST_ASSERT_TRUE(decide(100, 25, PROTO_TRUE, CFG.recover_ms - 1u, PROTO_FALSE).recovering);
    TEST_ASSERT_FALSE(decide(100, 25, PROTO_TRUE, CFG.recover_ms, PROTO_FALSE).recovering);
    TEST_ASSERT_FALSE(decide(100, 25, PROTO_TRUE, 0xFFFFFFFFu, PROTO_FALSE).recovering);
}

// The window only opens for a boot that followed a brownout.
void test_a_clean_boot_never_recovers(void)
{
    for (uint32_t ms = 0; ms < CFG.recover_ms; ms += CFG.recover_ms / 8u)
    {
        TEST_ASSERT_FALSE(decide(100, 25, PROTO_FALSE, ms, PROTO_FALSE).recovering);
    }
}

// power_mgmt.h: mhz_min is the "clock when idle, throttled, or recovering", so either hold takes the
// floor away from a load that would otherwise buy the ceiling, and both together do the same.
void test_either_hold_forces_the_floor(void)
{
    TEST_ASSERT_EQUAL_UINT16(CFG.mhz_max, decide(100, 25, PROTO_FALSE, 60000u, PROTO_FALSE).cpu_mhz);

    const PowerPlan hot = decide(100, (int16_t)(CFG.temp_hot_c + 5), PROTO_FALSE, 60000u, PROTO_FALSE);
    TEST_ASSERT_TRUE(hot.throttled);
    TEST_ASSERT_FALSE(hot.recovering);
    TEST_ASSERT_EQUAL_UINT16(CFG.mhz_min, hot.cpu_mhz);

    const PowerPlan brown = decide(100, 25, PROTO_TRUE, 0u, PROTO_FALSE);
    TEST_ASSERT_FALSE(brown.throttled);
    TEST_ASSERT_TRUE(brown.recovering);
    TEST_ASSERT_EQUAL_UINT16(CFG.mhz_min, brown.cpu_mhz);

    const PowerPlan both = decide(100, (int16_t)(CFG.temp_hot_c + 5), PROTO_TRUE, 0u, PROTO_FALSE);
    TEST_ASSERT_TRUE(both.throttled);
    TEST_ASSERT_TRUE(both.recovering);
    TEST_ASSERT_EQUAL_UINT16(CFG.mhz_min, both.cpu_mhz);
}

// power_mgmt.h: decide chooses "reading nothing outside plan_args", so the same arguments decide the
// same way however many other decisions ran in between.
void test_the_decision_is_a_pure_function_of_its_arguments(void)
{
    const PowerPlan first = decide(100, (int16_t)(CFG.temp_hot_c + 5), PROTO_TRUE, 0u, PROTO_TRUE);
    (void)decide(0, (int16_t)(CFG.temp_cool_c - 20), PROTO_FALSE, 0xFFFFFFFFu, PROTO_FALSE);
    (void)decide(50, INT16_MIN, PROTO_TRUE, 1u, PROTO_TRUE);
    const PowerPlan again = decide(100, (int16_t)(CFG.temp_hot_c + 5), PROTO_TRUE, 0u, PROTO_TRUE);

    TEST_ASSERT_EQUAL_UINT16(first.cpu_mhz, again.cpu_mhz);
    TEST_ASSERT_EQUAL_INT(first.throttled, again.throttled);
    TEST_ASSERT_EQUAL_INT(first.recovering, again.recovering);
}

// With no thresholds there is nothing to compare against, so the plan names no clock at all: 0 is
// neither rail, and a caller that applied it would be setting the part to no frequency.
void test_a_null_config_decides_nothing(void)
{
    const PowerPlan p = decide_cfg(NULL, 100, (int16_t)(CFG.temp_hot_c + 5), PROTO_TRUE, 0u, PROTO_TRUE);
    TEST_ASSERT_EQUAL_UINT16(0u, p.cpu_mhz);
    TEST_ASSERT_FALSE(p.throttled);
    TEST_ASSERT_FALSE(p.recovering);
}

// Each field takes its own build flag, and the three invariants protocore_config.h enforces at
// lines 3639-3649 (#error on cool >= hot, on min > max, on busy_pct > 100) hold in the result.
void test_the_defaults_carry_each_build_flag(void)
{
    PowerCfg cfg;
    memset(&cfg, 0xA5, sizeof(cfg));
    Power.cfg_out = &cfg;
    Power.defaults(protocore_power_mgmt_span());

    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_POWER_MHZ_MAX, cfg.mhz_max);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_POWER_MHZ_MIN, cfg.mhz_min);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_POWER_BUSY_PCT, cfg.busy_pct);
    TEST_ASSERT_EQUAL_INT16(PROTOCORE_POWER_TEMP_HOT_C, cfg.temp_hot_c);
    TEST_ASSERT_EQUAL_INT16(PROTOCORE_POWER_TEMP_COOL_C, cfg.temp_cool_c);
    TEST_ASSERT_EQUAL_UINT32(PROTOCORE_POWER_RECOVER_MS, cfg.recover_ms);

    TEST_ASSERT_TRUE(cfg.temp_cool_c < cfg.temp_hot_c);
    TEST_ASSERT_TRUE(cfg.mhz_min <= cfg.mhz_max);
    TEST_ASSERT_TRUE(cfg.busy_pct <= 100u);
}

// A null destination is refused rather than written through: the next fill still lands.
void test_defaults_refuse_a_null_destination(void)
{
    PowerCfg cfg;
    Power.cfg_out = NULL;
    Power.defaults(protocore_power_mgmt_span());

    memset(&cfg, 0xA5, sizeof(cfg));
    Power.cfg_out = &cfg;
    Power.defaults(protocore_power_mgmt_span());
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_POWER_MHZ_MAX, cfg.mhz_max);
}

// ---------------------------------------------------------------------------
// The report: RFC 8259 sec 2 / 3 / 4 / 6 / 7
// ---------------------------------------------------------------------------

static char g_json[128];

static const char *report(const PowerPlan *plan, int16_t temp_c, size_t cap)
{
    Power.out_args.plan = plan;
    Power.out_args.temp_c = temp_c;
    Power.out_args.out = g_json;
    Power.out_args.cap = cap;
    Power.json(protocore_power_mgmt_span());
    return g_json;
}

// Every value the walk accepted, in the order it met them.
#define MAX_TOK 16
static char g_tok[MAX_TOK][40];
static int g_ntok;

static int is_ws(char c)
{
    return c == 0x20 || c == 0x09 || c == 0x0A || c == 0x0D; // ws, sec 2
}

static int skip_ws(const char *s, int i)
{
    while (is_ws(s[i]))
    {
        i++;
    }
    return i;
}

static int parse_value(const char *s, int i);

// string = quotation-mark *char quotation-mark, unescaped = %x20-21 / %x23-5B / %x5D-10FFFF, sec 7.
static int parse_string(const char *s, int i)
{
    if (s[i] != '"')
    {
        return -1;
    }
    i++;
    for (;;)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c == '"')
        {
            return i + 1;
        }
        if (c == '\\')
        {
            const char e = s[i + 1];
            if (e == '"' || e == '\\' || e == '/' || e == 'b' || e == 'f' || e == 'n' || e == 'r' || e == 't')
            {
                i += 2;
                continue;
            }
            if (e == 'u')
            {
                for (int k = 0; k < 4; k++)
                {
                    const char h = s[i + 2 + k];
                    if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F')))
                    {
                        return -1;
                    }
                }
                i += 6;
                continue;
            }
            return -1;
        }
        if (c < 0x20)
        {
            return -1; // a control character, or the terminator: the string never closed
        }
        i++;
    }
}

// number = [ minus ] int [ frac ] [ exp ], int = zero / ( digit1-9 *DIGIT ), sec 6. A leading zero
// or a leading plus is refused here, which is what makes the reading cases mean something.
static int parse_number(const char *s, int i)
{
    if (s[i] == '-')
    {
        i++;
    }
    if (s[i] == '0')
    {
        i++;
    }
    else if (s[i] >= '1' && s[i] <= '9')
    {
        while (s[i] >= '0' && s[i] <= '9')
        {
            i++;
        }
    }
    else
    {
        return -1;
    }
    if (s[i] == '.')
    {
        i++;
        if (!(s[i] >= '0' && s[i] <= '9'))
        {
            return -1;
        }
        while (s[i] >= '0' && s[i] <= '9')
        {
            i++;
        }
    }
    if (s[i] == 'e' || s[i] == 'E')
    {
        i++;
        if (s[i] == '+' || s[i] == '-')
        {
            i++;
        }
        if (!(s[i] >= '0' && s[i] <= '9'))
        {
            return -1;
        }
        while (s[i] >= '0' && s[i] <= '9')
        {
            i++;
        }
    }
    return i;
}

// false = %x66.61.6c.73.65, null = %x6e.75.6c.6c, true = %x74.72.75.65, sec 3. Lowercase only.
static int parse_literal(const char *s, int i, const char *lit)
{
    const size_t n = strlen(lit);
    return strncmp(s + i, lit, n) == 0 ? i + (int)n : -1;
}

static int keep(const char *s, int from, int to)
{
    const int n = to - from;
    if (g_ntok < MAX_TOK && n < (int)sizeof(g_tok[0]))
    {
        memcpy(g_tok[g_ntok], s + from, (size_t)n);
        g_tok[g_ntok][n] = '\0';
        g_ntok++;
    }
    return to;
}

// object = begin-object [ member *( value-separator member ) ] end-object, member = string
// name-separator value, sec 4.
static int parse_object(const char *s, int i)
{
    i = skip_ws(s, i + 1);
    if (s[i] == '}')
    {
        return i + 1;
    }
    for (;;)
    {
        i = skip_ws(s, i);
        int j = parse_string(s, i);
        if (j < 0)
        {
            return -1;
        }
        i = skip_ws(s, j);
        if (s[i] != ':')
        {
            return -1;
        }
        j = parse_value(s, skip_ws(s, i + 1));
        if (j < 0)
        {
            return -1;
        }
        i = skip_ws(s, j);
        if (s[i] == ',')
        {
            i++;
            continue;
        }
        if (s[i] == '}')
        {
            return i + 1;
        }
        return -1;
    }
}

// array = begin-array [ value *( value-separator value ) ] end-array, sec 5.
static int parse_array(const char *s, int i)
{
    i = skip_ws(s, i + 1);
    if (s[i] == ']')
    {
        return i + 1;
    }
    for (;;)
    {
        int j = parse_value(s, skip_ws(s, i));
        if (j < 0)
        {
            return -1;
        }
        i = skip_ws(s, j);
        if (s[i] == ',')
        {
            i++;
            continue;
        }
        if (s[i] == ']')
        {
            return i + 1;
        }
        return -1;
    }
}

// value = false / null / true / object / array / number / string, sec 3.
static int parse_value(const char *s, int i)
{
    int j;
    if (s[i] == '{')
    {
        return parse_object(s, i);
    }
    if (s[i] == '[')
    {
        return parse_array(s, i);
    }
    if (s[i] == '"')
    {
        j = parse_string(s, i);
        return j < 0 ? -1 : keep(s, i, j);
    }
    if ((j = parse_literal(s, i, "false")) > 0)
    {
        return keep(s, i, j);
    }
    if ((j = parse_literal(s, i, "null")) > 0)
    {
        return keep(s, i, j);
    }
    if ((j = parse_literal(s, i, "true")) > 0)
    {
        return keep(s, i, j);
    }
    j = parse_number(s, i);
    return j < 0 ? -1 : keep(s, i, j);
}

// JSON-text = ws value ws, sec 2, with nothing left over.
static int json_ok(const char *s)
{
    g_ntok = 0;
    int i = parse_value(s, skip_ws(s, 0));
    if (i < 0)
    {
        return 0;
    }
    return s[skip_ws(s, i)] == '\0';
}

static int token_int(const char *t, long *out)
{
    int i = 0;
    long sign = 1;
    long v = 0;
    if (t[i] == '-')
    {
        sign = -1;
        i++;
    }
    if (!(t[i] >= '0' && t[i] <= '9'))
    {
        return 0;
    }
    for (; t[i]; i++)
    {
        if (t[i] < '0' || t[i] > '9')
        {
            return 0;
        }
        v = (v * 10) + (t[i] - '0');
    }
    *out = sign * v;
    return 1;
}

static int has_number(long want)
{
    for (int i = 0; i < g_ntok; i++)
    {
        long v;
        if (token_int(g_tok[i], &v) && v == want)
        {
            return 1;
        }
    }
    return 0;
}

static int has_literal(const char *lit)
{
    for (int i = 0; i < g_ntok; i++)
    {
        if (strcmp(g_tok[i], lit) == 0)
        {
            return 1;
        }
    }
    return 0;
}

// Every report is a JSON-text by the sec 2 grammar, across the whole space of plans the decision can
// produce and the range of readings an int16 can carry.
void test_every_report_is_a_json_text(void)
{
    static const int16_t TEMP[] = {INT16_MIN, (int16_t)(INT16_MIN + 1), -100, -40, -1, 0, 1, 25, 85, 32767};
    for (size_t t = 0; t < sizeof(TEMP) / sizeof(TEMP[0]); t++)
    {
        for (int thr = 0; thr < 2; thr++)
        {
            for (int rec = 0; rec < 2; rec++)
            {
                PowerPlan p;
                p.cpu_mhz = (thr || rec) ? CFG.mhz_min : CFG.mhz_max;
                p.throttled = thr ? PROTO_TRUE : PROTO_FALSE;
                p.recovering = rec ? PROTO_TRUE : PROTO_FALSE;
                TEST_ASSERT_TRUE_MESSAGE(json_ok(report(&p, TEMP[t], sizeof(g_json))), g_json);
                TEST_ASSERT_EQUAL_UINT((unsigned)strlen(g_json), (unsigned)Power.n);
            }
        }
    }
}

// RFC 8259 sec 3: the three literal names are false, null and true, and "MUST be lowercase. No other
// literal names are allowed" - so a report carrying both flags spells them exactly those ways.
void test_the_flags_use_the_literal_names_the_rfc_publishes(void)
{
    PowerPlan p;
    p.cpu_mhz = CFG.mhz_min;
    p.throttled = PROTO_TRUE;
    p.recovering = PROTO_FALSE;
    TEST_ASSERT_TRUE(json_ok(report(&p, 85, sizeof(g_json))));
    TEST_ASSERT_TRUE(has_literal("true"));
    TEST_ASSERT_TRUE(has_literal("false"));

    p.throttled = PROTO_FALSE;
    p.recovering = PROTO_TRUE;
    TEST_ASSERT_TRUE(json_ok(report(&p, 85, sizeof(g_json))));
    TEST_ASSERT_TRUE(has_literal("true"));
    TEST_ASSERT_TRUE(has_literal("false"));
}

// A reading crosses the report unchanged and in the sec 6 number form. The grammar walker refuses a
// leading zero and a leading plus, so recovering the same integer out of the document is what shows
// the sign and the digits both survived.
void test_a_reading_survives_the_report(void)
{
    static const int16_t TEMP[] = {(int16_t)(INT16_MIN + 1), -1000, -100, -40, -1, 0, 1, 25, 85, 1000, 32767};
    for (size_t t = 0; t < sizeof(TEMP) / sizeof(TEMP[0]); t++)
    {
        PowerPlan p;
        p.cpu_mhz = CFG.mhz_max;
        p.throttled = PROTO_FALSE;
        p.recovering = PROTO_FALSE;
        TEST_ASSERT_TRUE(json_ok(report(&p, TEMP[t], sizeof(g_json))));
        TEST_ASSERT_TRUE_MESSAGE(has_number((long)TEMP[t]), g_json);
    }
}

// The clock the plan names crosses the report unchanged too.
void test_the_clock_survives_the_report(void)
{
    static const uint16_t MHZ[] = {0u, 1u, 80u, 240u, 65535u};
    for (size_t m = 0; m < sizeof(MHZ) / sizeof(MHZ[0]); m++)
    {
        PowerPlan p;
        p.cpu_mhz = MHZ[m];
        p.throttled = PROTO_FALSE;
        p.recovering = PROTO_FALSE;
        TEST_ASSERT_TRUE(json_ok(report(&p, 25, sizeof(g_json))));
        TEST_ASSERT_TRUE_MESSAGE(has_number((long)MHZ[m]), g_json);
    }
}

// power_mgmt.h: temp_c is "the die temperature, or INT16_MIN when there is no sensor". A sentinel is
// not a reading, so it must not appear as a number - RFC 8259 sec 3 publishes null for exactly this,
// and a consumer that saw -32768 would take it for the coldest reading in the range.
void test_the_sentinel_is_never_reported_as_a_reading(void)
{
    PowerPlan p;
    p.cpu_mhz = CFG.mhz_max;
    p.throttled = PROTO_FALSE;
    p.recovering = PROTO_FALSE;
    TEST_ASSERT_TRUE(json_ok(report(&p, INT16_MIN, sizeof(g_json))));
    TEST_ASSERT_TRUE(has_literal("null"));
    TEST_ASSERT_FALSE(has_number(-32768L));

    TEST_ASSERT_TRUE(json_ok(report(&p, (int16_t)(INT16_MIN + 1), sizeof(g_json))));
    TEST_ASSERT_FALSE(has_literal("null"));
    TEST_ASSERT_TRUE(has_number(-32767L));
}

// Half a JSON-text is not a JSON-text. At every capacity below the one the document needs, the
// result is the empty string with n == 0 - never a prefix a consumer would try to parse.
void test_a_short_buffer_yields_nothing_not_a_prefix(void)
{
    PowerPlan p;
    p.cpu_mhz = CFG.mhz_min;
    p.throttled = PROTO_TRUE;
    p.recovering = PROTO_TRUE;
    (void)report(&p, -40, sizeof(g_json));
    const size_t full = Power.n;
    TEST_ASSERT_TRUE(full > 0u);

    for (size_t cap = 1; cap <= full; cap++)
    {
        (void)report(&p, -40, cap);
        TEST_ASSERT_EQUAL_UINT(0u, (unsigned)Power.n);
        TEST_ASSERT_EQUAL_CHAR('\0', g_json[0]);
    }

    (void)report(&p, -40, full + 1u);
    TEST_ASSERT_EQUAL_UINT((unsigned)full, (unsigned)Power.n);
    TEST_ASSERT_TRUE(json_ok(g_json));
}

// Nothing to describe, nowhere to write it, or no room at all: each is reported as zero bytes.
void test_the_report_refuses_what_it_cannot_write(void)
{
    PowerPlan p;
    p.cpu_mhz = CFG.mhz_max;
    p.throttled = PROTO_FALSE;
    p.recovering = PROTO_FALSE;

    Power.out_args.plan = NULL;
    Power.out_args.temp_c = 25;
    Power.out_args.out = g_json;
    Power.out_args.cap = sizeof(g_json);
    Power.json(protocore_power_mgmt_span());
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)Power.n);

    Power.out_args.plan = &p;
    Power.out_args.out = NULL;
    Power.json(protocore_power_mgmt_span());
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)Power.n);

    Power.out_args.out = g_json;
    Power.out_args.cap = 0;
    g_json[0] = 'x';
    Power.json(protocore_power_mgmt_span());
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)Power.n);
    TEST_ASSERT_EQUAL_CHAR('x', g_json[0]);
}
