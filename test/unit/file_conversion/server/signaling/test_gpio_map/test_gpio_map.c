// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/signaling/gpio_map.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_out[512];

static const char *render(const protocore_gpio_pin *pins, uint8_t count, uint32_t cap)
{
    memset(g_out, '#', sizeof(g_out));
    GpioMap.args.pins = pins;
    GpioMap.args.count = count;
    GpioMap.out_args.out = g_out;
    GpioMap.out_args.cap = cap;
    GpioMap.json(GpioMap.internal);
    return g_out;
}

static const char *dir_name(protocore_gpio_dir dir)
{
    GpioMap.args.dir = dir;
    GpioMap.dir_name(GpioMap.internal);
    return GpioMap.text;
}

static proto_bool is_output(const protocore_gpio_pin *pins, uint8_t count, uint8_t pin)
{
    GpioMap.args.pins = pins;
    GpioMap.args.count = count;
    GpioMap.args.pin = pin;
    GpioMap.is_output(GpioMap.internal);
    return GpioMap.ok;
}

static proto_bool parse_set(const char *body, size_t len, uint8_t *pin, uint8_t *level)
{
    GpioMap.parse_args.body = body;
    GpioMap.parse_args.len = len;
    GpioMap.parse_args.pin_out = pin;
    GpioMap.parse_args.level_out = level;
    GpioMap.parse_set(GpioMap.internal);
    return GpioMap.ok;
}

static const protocore_gpio_pin TABLE[3] = {
    {2, "led", PROTOCORE_GPIO_DIR_OUT, 1},
    {4, "button", PROTOCORE_GPIO_DIR_IN_PULLUP, 0},
    {5, "sense", PROTOCORE_GPIO_DIR_IN, 1},
};

void test_the_serialized_document(void)
{
    static const char WANT[] = "{\"pins\":["
                               "{\"pin\":2,\"label\":\"led\",\"dir\":\"out\",\"level\":1},"
                               "{\"pin\":4,\"label\":\"button\",\"dir\":\"in_pullup\",\"level\":0},"
                               "{\"pin\":5,\"label\":\"sense\",\"dir\":\"in\",\"level\":1}"
                               "]}";
    TEST_ASSERT_EQUAL_STRING(WANT, render(TABLE, 3, (uint32_t)sizeof(g_out)));
    TEST_ASSERT_EQUAL_INT((int32_t)(sizeof(WANT) - 1), GpioMap.n);
}

void test_an_empty_table_renders_an_empty_array(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"pins\":[]}", render(TABLE, 0, (uint32_t)sizeof(g_out)));
    TEST_ASSERT_EQUAL_INT(11, GpioMap.n);
}

void test_the_element_separator(void)
{
    const char *s = render(TABLE, 1, (uint32_t)sizeof(g_out));
    TEST_ASSERT_EQUAL_STRING("{\"pins\":[{\"pin\":2,\"label\":\"led\",\"dir\":\"out\",\"level\":1}]}", s);

    for (uint8_t n = 0; n <= 3u; n++)
    {
        s = render(TABLE, n, (uint32_t)sizeof(g_out));
        unsigned between = 0;
        for (const char *p = s; *p; p++)
        {
            between += (*p == '}' && p[1] == ',') ? 1u : 0u;
        }
        TEST_ASSERT_EQUAL_UINT(n ? (unsigned)(n - 1u) : 0u, between);
    }
}

void test_the_level_is_normalized(void)
{
    protocore_gpio_pin pins[1] = {{7, "x", PROTOCORE_GPIO_DIR_OUT, 0}};
    TEST_ASSERT_NOT_NULL(strstr(render(pins, 1, (uint32_t)sizeof(g_out)), "\"level\":0"));

    pins[0].level = 1;
    TEST_ASSERT_NOT_NULL(strstr(render(pins, 1, (uint32_t)sizeof(g_out)), "\"level\":1"));

    pins[0].level = 200;
    TEST_ASSERT_NOT_NULL(strstr(render(pins, 1, (uint32_t)sizeof(g_out)), "\"level\":1"));
    TEST_ASSERT_NULL(strstr(render(pins, 1, (uint32_t)sizeof(g_out)), "\"level\":200"));
}

void test_a_label_cannot_close_its_string(void)
{
    protocore_gpio_pin pins[1] = {{9, "a\"b\\c", PROTOCORE_GPIO_DIR_IN, 0}};
    const char *s = render(pins, 1, (uint32_t)sizeof(g_out));
    TEST_ASSERT_NOT_NULL(strstr(s, "\\\""));
    TEST_ASSERT_NOT_NULL(strstr(s, "\\\\"));

    TEST_ASSERT_EQUAL_STRING("]}", &s[strlen(s) - 2]);
    TEST_ASSERT_NOT_NULL(strstr(s, "\"dir\":\"in\""));
}

void test_a_short_capacity_yields_no_document(void)
{
    const int32_t need = (int32_t)strlen(render(TABLE, 3, (uint32_t)sizeof(g_out)));
    TEST_ASSERT_TRUE(need > 0);

    for (uint32_t cap = 1; cap <= (uint32_t)need; cap++)
    {
        (void)render(TABLE, 3, cap);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, GpioMap.n, "a short capacity must report nothing written");
    }

    (void)render(TABLE, 3, (uint32_t)need + 1u);
    TEST_ASSERT_EQUAL_INT(need, GpioMap.n);
}

void test_the_serializer_refuses_missing_arguments(void)
{
    GpioMap.args.pins = TABLE;
    GpioMap.args.count = 3;
    GpioMap.out_args.out = NULL;
    GpioMap.out_args.cap = (uint32_t)sizeof(g_out);
    GpioMap.json(GpioMap.internal);
    TEST_ASSERT_EQUAL_INT(0, GpioMap.n);

    GpioMap.out_args.out = g_out;
    GpioMap.out_args.cap = 0;
    GpioMap.json(GpioMap.internal);
    TEST_ASSERT_EQUAL_INT(0, GpioMap.n);

    g_out[0] = 'x';
    GpioMap.args.pins = NULL;
    GpioMap.out_args.cap = (uint32_t)sizeof(g_out);
    GpioMap.json(GpioMap.internal);
    TEST_ASSERT_EQUAL_INT(0, GpioMap.n);
    TEST_ASSERT_EQUAL_STRING("", g_out);
}

void test_direction_names(void)
{
    TEST_ASSERT_EQUAL_STRING("in", dir_name(PROTOCORE_GPIO_DIR_IN));
    TEST_ASSERT_EQUAL_STRING("in_pullup", dir_name(PROTOCORE_GPIO_DIR_IN_PULLUP));
    TEST_ASSERT_EQUAL_STRING("in_pulldown", dir_name(PROTOCORE_GPIO_DIR_IN_PULLDOWN));
    TEST_ASSERT_EQUAL_STRING("out", dir_name(PROTOCORE_GPIO_DIR_OUT));
    TEST_ASSERT_EQUAL_STRING("in", dir_name((protocore_gpio_dir)99));
}

void test_only_a_declared_output_may_be_driven(void)
{
    TEST_ASSERT_TRUE(is_output(TABLE, 3, 2));
    TEST_ASSERT_FALSE(is_output(TABLE, 3, 4));
    TEST_ASSERT_FALSE(is_output(TABLE, 3, 5));
    TEST_ASSERT_FALSE(is_output(TABLE, 3, 99));
    TEST_ASSERT_FALSE(is_output(TABLE, 3, 0));

    TEST_ASSERT_FALSE(is_output(TABLE, 0, 2));
    TEST_ASSERT_TRUE(is_output(TABLE, 1, 2));

    TEST_ASSERT_FALSE(is_output(NULL, 3, 2));
}

void test_a_set_request_parses_both_fields(void)
{
    uint8_t pin = 0xFF;
    uint8_t level = 0xFF;

    TEST_ASSERT_TRUE(parse_set("pin=2&level=1", 13, &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(2u, pin);
    TEST_ASSERT_EQUAL_UINT8(1u, level);

    TEST_ASSERT_TRUE(parse_set("level=0&pin=13", 14, &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(13u, pin);
    TEST_ASSERT_EQUAL_UINT8(0u, level);

    TEST_ASSERT_TRUE(parse_set("x=9&pin=7&y=1&level=1", 21, &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(7u, pin);
    TEST_ASSERT_EQUAL_UINT8(1u, level);
}

void test_the_parsed_level_is_a_flag(void)
{
    uint8_t pin = 0;
    uint8_t level = 0xFF;
    TEST_ASSERT_TRUE(parse_set("pin=1&level=200", 15, &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(1u, level);

    TEST_ASSERT_TRUE(parse_set("pin=1&level=0", 13, &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(0u, level);
}

void test_an_incomplete_set_request_is_refused(void)
{
    uint8_t pin = 0xAA;
    uint8_t level = 0xAA;

    static const char *const BAD[] = {
        "",
        "pin=2",
        "level=1",
        "pin=&level=1",
        "pin=2&level=",
        "pin=x&level=1",
        "pin=2&level=x",
        "spin=2&level=1",
        "pin2&level=1",
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(parse_set(BAD[i], strlen(BAD[i]), &pin, &level), BAD[i]);
    }
    TEST_ASSERT_EQUAL_UINT8(0xAAu, pin);
    TEST_ASSERT_EQUAL_UINT8(0xAAu, level);

    TEST_ASSERT_FALSE(parse_set(NULL, 13, &pin, &level));
    TEST_ASSERT_FALSE(parse_set("pin=2&level=1", 13, NULL, &level));
    TEST_ASSERT_FALSE(parse_set("pin=2&level=1", 13, &pin, NULL));
}

void test_the_body_length_bounds_the_parse(void)
{
    uint8_t pin = 0;
    uint8_t level = 0;
    TEST_ASSERT_FALSE(parse_set("pin=2&level=1", 5, &pin, &level));
    TEST_ASSERT_FALSE(parse_set("pin=2&level=1", 12, &pin, &level));
    TEST_ASSERT_TRUE(parse_set("pin=2&level=1", 13, &pin, &level));
}

void test_arming_and_sampling_the_table(void)
{
    protocore_gpio_pin pins[3] = {
        {12, "led", PROTOCORE_GPIO_DIR_OUT, 0},
        {13, "button", PROTOCORE_GPIO_DIR_IN_PULLUP, 0},
        {14, "sense", PROTOCORE_GPIO_DIR_IN_PULLDOWN, 1},
    };

    GpioMap.args.pins = pins;
    GpioMap.args.count = 3;
    GpioMap.begin_pins(GpioMap.internal);

    GpioMap.args.pins_rw = pins;
    GpioMap.args.count = 3;
    GpioMap.sample(GpioMap.internal);
    TEST_ASSERT_EQUAL_UINT8(1u, pins[1].level);
    TEST_ASSERT_EQUAL_UINT8(0u, pins[2].level);

    GpioMap.args.pin = 12;
    GpioMap.args.level = 1;
    GpioMap.write(GpioMap.internal);
    GpioMap.args.pins_rw = pins;
    GpioMap.sample(GpioMap.internal);
    TEST_ASSERT_EQUAL_UINT8(1u, pins[0].level);

    GpioMap.args.pin = 12;
    GpioMap.args.level = 0;
    GpioMap.write(GpioMap.internal);
    GpioMap.args.pins_rw = pins;
    GpioMap.sample(GpioMap.internal);
    TEST_ASSERT_EQUAL_UINT8(0u, pins[0].level);

    GpioMap.args.pins = NULL;
    GpioMap.begin_pins(GpioMap.internal);
    GpioMap.args.pins_rw = NULL;
    GpioMap.sample(GpioMap.internal);
}

void test_a_write_drives_a_flag(void)
{
    protocore_gpio_pin pins[1] = {{15, "out", PROTOCORE_GPIO_DIR_OUT, 0}};
    GpioMap.args.pins = pins;
    GpioMap.args.count = 1;
    GpioMap.begin_pins(GpioMap.internal);

    GpioMap.args.pin = 15;
    GpioMap.args.level = 200;
    GpioMap.write(GpioMap.internal);
    GpioMap.args.pins_rw = pins;
    GpioMap.args.count = 1;
    GpioMap.sample(GpioMap.internal);
    TEST_ASSERT_EQUAL_UINT8(1u, pins[0].level);
}
