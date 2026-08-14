// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/machine_tool/haas_mdc/haas_mdc.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static void assert_field(const HaasMdcResp *r, size_t idx, const char *want)
{
    const char *p = NULL;
    size_t l = 0;
    TEST_ASSERT_TRUE_MESSAGE(protocore_haas_mdc_field(r, idx, &p, &l), want);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(want), l, want);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want, p, l, want);
}

static void assert_slice(const char *p, size_t l, const char *want)
{
    TEST_ASSERT_EQUAL_size_t_MESSAGE(strlen(want), l, want);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(want, p, l, want);
}

void test_numbered_query_is_the_documented_line(void)
{
    char buf[32];
    TEST_ASSERT_EQUAL_size_t(6u, protocore_haas_mdc_build_q(buf, sizeof(buf), HAAS_Q_SERIAL));
    TEST_ASSERT_EQUAL_STRING("?Q100\r", buf);

    TEST_ASSERT_EQUAL_size_t(6u, protocore_haas_mdc_build_q(buf, sizeof(buf), HAAS_Q_PROGRAM_STATUS));
    TEST_ASSERT_EQUAL_STRING("?Q500\r", buf);

    TEST_ASSERT_EQUAL_size_t(6u, protocore_haas_mdc_build_q(buf, sizeof(buf), HAAS_Q_MODE));
    TEST_ASSERT_EQUAL_STRING("?Q104\r", buf);
}

void test_macro_query_carries_the_variable_number(void)
{
    char buf[32];
    TEST_ASSERT_EQUAL_size_t(10u, protocore_haas_mdc_build_var(buf, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_STRING("?Q600 100\r", buf);

    TEST_ASSERT_EQUAL_size_t(8u, protocore_haas_mdc_build_var(buf, sizeof(buf), 1));
    TEST_ASSERT_EQUAL_STRING("?Q600 1\r", buf);
}

void test_builders_refuse_a_short_buffer(void)
{
    char buf[8];
    TEST_ASSERT_EQUAL_size_t(0u, protocore_haas_mdc_build_q(buf, 6, HAAS_Q_SERIAL));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_haas_mdc_build_var(buf, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_haas_mdc_build_q(NULL, sizeof(buf), HAAS_Q_SERIAL));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_haas_mdc_build_q(buf, 0, HAAS_Q_SERIAL));
}

void test_documented_frame_is_split_on_the_delimiters(void)
{
    static const char FRAME[] = "\x02"
                                "SERIAL NUMBER, 1234567"
                                "\x17\r\n>";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(FRAME, sizeof(FRAME) - 1, &r));
    TEST_ASSERT_EQUAL_UINT8(2u, r.n_fields);
    assert_field(&r, 0, "SERIAL NUMBER");
    assert_field(&r, 1, "1234567");

    const char *p = NULL;
    size_t l = 0;
    TEST_ASSERT_TRUE(protocore_haas_mdc_value(&r, &p, &l));
    assert_slice(p, l, "1234567");
}

void test_bytes_outside_the_frame_are_ignored(void)
{
    static const char FRAME[] = ">\r\n"
                                "\x02"
                                "MODE, MEM"
                                "\x17\r\n>";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(FRAME, sizeof(FRAME) - 1, &r));
    TEST_ASSERT_EQUAL_UINT8(2u, r.n_fields);
    assert_field(&r, 0, "MODE");
    assert_field(&r, 1, "MEM");
}

void test_fields_are_trimmed_of_surrounding_spaces(void)
{
    static const char FRAME[] = "\x02"
                                "  POWER ON TIME  ,     00012:34:56   "
                                "\x17";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(FRAME, sizeof(FRAME) - 1, &r));
    assert_field(&r, 0, "POWER ON TIME");
    assert_field(&r, 1, "00012:34:56");
}

void test_incomplete_frame_is_refused(void)
{
    HaasMdcResp r;
    static const char NO_ETB[] = "\x02"
                                 "SERIAL NUMBER, 12345";
    static const char NO_STX[] = "SERIAL NUMBER, 12345\x17\r\n>";
    TEST_ASSERT_FALSE(protocore_haas_mdc_parse(NO_ETB, sizeof(NO_ETB) - 1, &r));
    TEST_ASSERT_FALSE(protocore_haas_mdc_parse(NO_STX, sizeof(NO_STX) - 1, &r));
    TEST_ASSERT_FALSE(protocore_haas_mdc_parse(NULL, 4, &r));
}

void test_q500_program_status_and_parts(void)
{
    static const char FRAME[] = "\x02"
                                "PROGRAM, O00123, IDLE, PARTS, 42"
                                "\x17\r\n>";
    HaasMdcResp r;
    HaasMdcStatus st;
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(FRAME, sizeof(FRAME) - 1, &r));
    TEST_ASSERT_EQUAL_UINT8(5u, r.n_fields);
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse_status(&r, &st));
    TEST_ASSERT_FALSE(st.busy);
    assert_slice(st.program, st.program_len, "O00123");
    assert_slice(st.status, st.status_len, "IDLE");
    TEST_ASSERT_TRUE(st.parts_valid);
    TEST_ASSERT_EQUAL_UINT32(42u, st.parts);
}

void test_q500_busy_branch_reports_no_counts(void)
{
    static const char FRAME[] = "\x02"
                                "STATUS, BUSY"
                                "\x17\r\n>";
    HaasMdcResp r;
    HaasMdcStatus st;
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(FRAME, sizeof(FRAME) - 1, &r));
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse_status(&r, &st));
    TEST_ASSERT_TRUE(st.busy);
    assert_slice(st.status, st.status_len, "BUSY");
    TEST_ASSERT_NULL(st.program);
    TEST_ASSERT_FALSE(st.parts_valid);
}

void test_q500_rejects_a_non_numeric_parts_field(void)
{
    HaasMdcResp r;
    HaasMdcStatus st;
    static const char BAD_PARTS[] = "\x02"
                                    "PROGRAM, O00123, IDLE, PARTS, ----"
                                    "\x17";
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(BAD_PARTS, sizeof(BAD_PARTS) - 1, &r));
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse_status(&r, &st));
    TEST_ASSERT_FALSE(st.parts_valid);
    TEST_ASSERT_EQUAL_UINT32(0u, st.parts);

    static const char OTHER[] = "\x02"
                                "SERIAL NUMBER, 1234567"
                                "\x17";
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(OTHER, sizeof(OTHER) - 1, &r));
    TEST_ASSERT_FALSE(protocore_haas_mdc_parse_status(&r, &st));
}

void test_q600_macro_response(void)
{
    static const char FRAME[] = "\x02"
                                "MACRO, 100, 1.000000"
                                "\x17\r\n>";
    HaasMdcResp r;
    uint32_t var = 0;
    const char *value = NULL;
    size_t value_len = 0;
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(FRAME, sizeof(FRAME) - 1, &r));
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse_macro(&r, &var, &value, &value_len));
    TEST_ASSERT_EQUAL_UINT32(100u, var);
    assert_slice(value, value_len, "1.000000");

    static const char BAD[] = "\x02"
                              "MACRO, ABC, 1.0"
                              "\x17";
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(BAD, sizeof(BAD) - 1, &r));
    TEST_ASSERT_FALSE(protocore_haas_mdc_parse_macro(&r, &var, &value, &value_len));
}

void test_unknown_is_the_error_response(void)
{
    static const char FRAME[] = "\x02"
                                "UNKNOWN"
                                "\x17\r\n>";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(FRAME, sizeof(FRAME) - 1, &r));
    TEST_ASSERT_TRUE(protocore_haas_mdc_is_error(&r));
    TEST_ASSERT_EQUAL_UINT8(1u, r.n_fields);

    TEST_ASSERT_FALSE(protocore_haas_mdc_value(&r, NULL, NULL));

    static const char OK[] = "\x02"
                             "SERIAL NUMBER, 1"
                             "\x17";
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(OK, sizeof(OK) - 1, &r));
    TEST_ASSERT_FALSE(protocore_haas_mdc_is_error(&r));

    static const char PREFIX[] = "\x02"
                                 "UNKNOWNS, 1"
                                 "\x17";
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(PREFIX, sizeof(PREFIX) - 1, &r));
    TEST_ASSERT_FALSE(protocore_haas_mdc_is_error(&r));
}

void test_dprnt_line_is_unframed_text(void)
{
    static const char LINE[] = "\x12"
                               "TOOL 5 LIFE 87"
                               "\r\n\x14";
    const char *text = NULL;
    size_t len = 0;
    TEST_ASSERT_TRUE(protocore_haas_mdc_dprnt_line(LINE, sizeof(LINE) - 1, &text, &len));
    assert_slice(text, len, "TOOL 5 LIFE 87");

    static const char FRAMED[] = "\x02"
                                 "SERIAL NUMBER, 1"
                                 "\x17";
    TEST_ASSERT_FALSE(protocore_haas_mdc_dprnt_line(FRAMED, sizeof(FRAMED) - 1, &text, &len));

    static const char EMPTY[] = ">\r\n";
    TEST_ASSERT_FALSE(protocore_haas_mdc_dprnt_line(EMPTY, sizeof(EMPTY) - 1, &text, &len));
    TEST_ASSERT_FALSE(protocore_haas_mdc_dprnt_line(LINE, 0, &text, &len));
}

void test_dprnt_keeps_interior_spaces(void)
{
    static const char LINE[] = "X 12.5  Y 3.0\r\n";
    const char *text = NULL;
    size_t len = 0;
    TEST_ASSERT_TRUE(protocore_haas_mdc_dprnt_line(LINE, sizeof(LINE) - 1, &text, &len));
    assert_slice(text, len, "X 12.5  Y 3.0");
}

void test_field_table_is_bounded(void)
{
    static const char FRAME[] = "\x02"
                                "1,2,3,4,5,6,7,8,9,10"
                                "\x17";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(FRAME, sizeof(FRAME) - 1, &r));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)PROTOCORE_HAAS_MDC_MAX_FIELDS, r.n_fields);
    assert_field(&r, 0, "1");
    assert_field(&r, PROTOCORE_HAAS_MDC_MAX_FIELDS - 1, "8");
    TEST_ASSERT_FALSE(protocore_haas_mdc_field(&r, PROTOCORE_HAAS_MDC_MAX_FIELDS, NULL, NULL));
}
