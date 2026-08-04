// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the GPIO pin-mapper core (server/signaling/gpio_map): the direction
// names, the JSON serializer, the control-POST parser, and the output guard. The
// digital read / write are ESP32-only and no-ops on the host.

#include "server/signaling/gpio_map.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_dir_name()
{
    TEST_ASSERT_EQUAL_STRING("in", pc_gpio_dir_name(PC_GPIO_DIR_IN));
    TEST_ASSERT_EQUAL_STRING("in_pullup", pc_gpio_dir_name(PC_GPIO_DIR_IN_PULLUP));
    TEST_ASSERT_EQUAL_STRING("in_pulldown", pc_gpio_dir_name(PC_GPIO_DIR_IN_PULLDOWN));
    TEST_ASSERT_EQUAL_STRING("out", pc_gpio_dir_name(PC_GPIO_DIR_OUT));
    TEST_ASSERT_EQUAL_STRING("in", pc_gpio_dir_name((pc_gpio_dir)99)); // unknown -> in
}

void test_json()
{
    pc_gpio_pin pins[2] = {
        {2, "LED", PC_GPIO_DIR_OUT, 1},
        {0, "BOOT", PC_GPIO_DIR_IN_PULLUP, 0},
    };
    char buf[256];
    int n = pc_gpio_json(pins, 2, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"pins\":[{\"pin\":2,\"label\":\"LED\",\"dir\":\"out\",\"level\":1},"
                             "{\"pin\":0,\"label\":\"BOOT\",\"dir\":\"in_pullup\",\"level\":0}]}",
                             buf);
}

void test_json_empty()
{
    char buf[64];
    int n = pc_gpio_json(NULL, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, n); // null table -> empty string, 0
}

void test_json_small_buffer_fails_closed()
{
    pc_gpio_pin pins[1] = {{2, "LED", PC_GPIO_DIR_OUT, 1}};
    char buf[8];
    TEST_ASSERT_EQUAL_INT(0, pc_gpio_json(pins, 1, buf, sizeof(buf)));
}

void test_parse_set()
{
    uint8_t pin = 0, level = 0;
    const char *b = "pin=2&level=1";
    TEST_ASSERT_TRUE(pc_gpio_parse_set(b, strlen(b), &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(2, pin);
    TEST_ASSERT_EQUAL_UINT8(1, level);

    const char *b2 = "level=0&pin=13"; // order independent
    TEST_ASSERT_TRUE(pc_gpio_parse_set(b2, strlen(b2), &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(13, pin);
    TEST_ASSERT_EQUAL_UINT8(0, level);

    const char *b3 = "pin=2&level=7"; // any nonzero level -> 1
    TEST_ASSERT_TRUE(pc_gpio_parse_set(b3, strlen(b3), &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(1, level);
}

void test_parse_set_rejects_partial()
{
    uint8_t pin = 0, level = 0;
    const char *b = "pin=2"; // missing level
    TEST_ASSERT_FALSE(pc_gpio_parse_set(b, strlen(b), &pin, &level));
    const char *b2 = "level=1"; // missing pin
    TEST_ASSERT_FALSE(pc_gpio_parse_set(b2, strlen(b2), &pin, &level));
    const char *b3 = "pin=&level=1"; // no digits for pin
    TEST_ASSERT_FALSE(pc_gpio_parse_set(b3, strlen(b3), &pin, &level));
}

void test_parse_set_no_prefix_match()
{
    // "spin=2" must not satisfy the "pin" field (field-boundary check).
    uint8_t pin = 0, level = 0;
    const char *b = "spin=2&level=1";
    TEST_ASSERT_FALSE(pc_gpio_parse_set(b, strlen(b), &pin, &level));
}

void test_is_output()
{
    pc_gpio_pin pins[2] = {
        {2, "LED", PC_GPIO_DIR_OUT, 0},
        {0, "BOOT", PC_GPIO_DIR_IN_PULLUP, 0},
    };
    TEST_ASSERT_TRUE(pc_gpio_is_output(pins, 2, 2));
    TEST_ASSERT_FALSE(pc_gpio_is_output(pins, 2, 0));  // input pin
    TEST_ASSERT_FALSE(pc_gpio_is_output(pins, 2, 99)); // not in table
}

void test_host_gpio_stubs()
{
    // Host build: the GPIO bind functions are no-ops (no digitalRead/Write).
    pc_gpio_pin pins[1] = {0};
    pc_gpio_begin_pins(pins, 1);
    pc_gpio_read(pins, 1);
    pc_gpio_write(0, 1);
    TEST_PASS();
}

// The serializer's own output guards: no buffer at all, and a zero capacity.
void test_json_null_and_zero_cap()
{
    pc_gpio_pin pins[1] = {{2, "LED", PC_GPIO_DIR_OUT, 1}};
    char buf[64];
    TEST_ASSERT_EQUAL_INT(0, pc_gpio_json(pins, 1, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(0, pc_gpio_json(pins, 1, buf, 0));
}

// A pin with no label serializes as an empty label rather than dereferencing null.
void test_json_null_label()
{
    pc_gpio_pin pins[1] = {{5, NULL, PC_GPIO_DIR_IN, 0}};
    char buf[128];
    int n = pc_gpio_json(pins, 1, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"pins\":[{\"pin\":5,\"label\":\"\",\"dir\":\"in\",\"level\":0}]}", buf);
}

// Every buffer smaller than the exact fit fails closed at whichever field runs out
// of room - the opening object, a pin record, or the closing bracket - and none of
// them writes past the capacity it was given.
void test_json_every_short_buffer_fails_closed()
{
    pc_gpio_pin pins[1] = {{2, "L", PC_GPIO_DIR_OUT, 1}};
    char full[128];
    int n = pc_gpio_json(pins, 1, full, sizeof(full));
    TEST_ASSERT_TRUE(n > 0);
    for (int cap = 1; cap <= n; cap++)
    {
        char buf[128];
        memset(buf, 0x5A, sizeof(buf));
        TEST_ASSERT_EQUAL_INT(0, pc_gpio_json(pins, 1, buf, (size_t)cap));
        TEST_ASSERT_EQUAL_HEX8(0x5A, (uint8_t)buf[cap]); // one past the cap is untouched
    }
    TEST_ASSERT_EQUAL_INT(n, pc_gpio_json(pins, 1, full, (size_t)n + 1)); // exact fit
}

// The parser rejects every null argument.
void test_parse_set_null_args()
{
    uint8_t pin = 0, level = 0;
    const char *b = "pin=2&level=1";
    TEST_ASSERT_FALSE(pc_gpio_parse_set(NULL, 13, &pin, &level));
    TEST_ASSERT_FALSE(pc_gpio_parse_set(b, strlen(b), NULL, &level));
    TEST_ASSERT_FALSE(pc_gpio_parse_set(b, strlen(b), &pin, NULL));
}

// A name that matches but is not followed by '=' is skipped, and the real field
// later in the body still wins.
void test_parse_set_name_without_equals()
{
    uint8_t pin = 0, level = 0;
    const char *b = "pinx=9&pin=4&level=1";
    TEST_ASSERT_TRUE(pc_gpio_parse_set(b, strlen(b), &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(4, pin);
    TEST_ASSERT_EQUAL_UINT8(1, level);
}

// A field whose value is missing entirely, or begins with a byte above '9', is
// rejected rather than read as zero.
void test_parse_set_non_digit_values()
{
    uint8_t pin = 0, level = 0;
    TEST_ASSERT_FALSE(pc_gpio_parse_set("pin=", 4, &pin, &level)); // value runs off the end
    const char *b = "pin=x&level=1";
    TEST_ASSERT_FALSE(pc_gpio_parse_set(b, strlen(b), &pin, &level)); // 'x' is above '9'
}

// A digit run ends at the first non-digit; the leading digits are still accepted.
void test_parse_set_value_stops_at_non_digit()
{
    uint8_t pin = 0, level = 0;
    const char *b = "pin=2x&level=1";
    TEST_ASSERT_TRUE(pc_gpio_parse_set(b, strlen(b), &pin, &level));
    TEST_ASSERT_EQUAL_UINT8(2, pin);
    TEST_ASSERT_EQUAL_UINT8(1, level);
}

// A null pin table has no outputs.
void test_is_output_null_table()
{
    TEST_ASSERT_FALSE(pc_gpio_is_output(NULL, 2, 2));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_dir_name);
    RUN_TEST(test_json);
    RUN_TEST(test_json_empty);
    RUN_TEST(test_json_small_buffer_fails_closed);
    RUN_TEST(test_parse_set);
    RUN_TEST(test_parse_set_rejects_partial);
    RUN_TEST(test_parse_set_no_prefix_match);
    RUN_TEST(test_is_output);
    RUN_TEST(test_host_gpio_stubs);
    RUN_TEST(test_json_null_and_zero_cap);
    RUN_TEST(test_json_null_label);
    RUN_TEST(test_json_every_short_buffer_fails_closed);
    RUN_TEST(test_parse_set_null_args);
    RUN_TEST(test_parse_set_name_without_equals);
    RUN_TEST(test_parse_set_non_digit_values);
    RUN_TEST(test_parse_set_value_stops_at_non_digit);
    RUN_TEST(test_is_output_null_table);
    return UNITY_END();
}
