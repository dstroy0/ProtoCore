// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Haas Machine Data Collection Q-command codec
// (services/machine_tool/haas_mdc/haas_mdc.h).
//
// Three published Haas Automation documents, all fetched this session:
//
//  [NGC]  "Machine Data Collection - NGC", section "MDC - Ethernet Q Commands", haascnc.com. It
//         publishes the Query Format sentence, the Response Format sentence, and the "Data
//         Collection Queries and Commands Table" with an Example Response for every command.
//  [S143] Setting 143 "Machine Data Collect", Haas Mill / Lathe Operator's Manual. It publishes the
//         RS-232 output format "<STX><CSV response><ETB><CR/LF><0x3E>", "STX = 0x02 (ctrl-B);
//         ETB = 0x17 (ctrl-W)", the busy reply "STATUS, BUSY." and the unrecognized reply "UNKNOWN."
//  [DPR]  "Communication With External Device - DPRNT", haascnc.com: "A carriage return is sent out
//         after every DPRNT block" and "When an asterisk is output, it is converted to a space."
//
// Load-bearing: test_published_example_responses_split_on_the_commas. Every string in it is an
// Example Response cell copied out of [NGC]'s command table, so a splitter that lost a field or kept
// the separator space would be caught by the document rather than by this codec's own output.
//
// Failing by design, all three citing [NGC]:
//  - test_numbered_query_is_terminated_with_a_new_line and
//    test_macro_query_is_terminated_with_a_new_line. [NGC] Query Format: "The correct query format
//    is ?Q###, where ### is the query number, terminated with a new line." A new line is LF (0x0A);
//    the same section spells a CR when it means one, writing the response terminator as "/r/n".
//    haas_mdc.c:88 and :101 emit a bare CR (0x0D), which terminates no line under that sentence.
//  - test_ngc_ethernet_response_is_parsed. [NGC] Response Format: "Responses from the control begin
//    with > and end with /r/n" - no STX, no ETB. haas_mdc.h advertises "a raw TCP socket (Setting
//    143, default port 5051)", which is exactly the transport that sentence describes, but the
//    parser demands the [S143] RS-232 frame and rejects the NGC form outright.
//
// Properties, not published values: the DPRNT DC2 (0x12) / DC4 (0x14) stripping ([DPR] says POPEN and
// PCLOS "are not required on the Haas machine" and never names a control code), the eight-field cap,
// the buffer refusals, and the three-field "MACRO, <var>, <value>" decode - [NGC]'s Example Response
// for ?Q600 is the two-field "MACRO, 0.0", so no document supplies a row carrying a variable number.

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

// Frame a payload the way [S143] prints it and split it, so a case reads as one response.
static char g_frame[128];
static HaasMdcResp g_resp;

static const HaasMdcResp *framed(const char *payload)
{
    size_t n = 0;
    g_frame[n++] = (char)PROTOCORE_HAAS_MDC_STX;
    for (const char *p = payload; *p; p++)
    {
        g_frame[n++] = *p;
    }
    g_frame[n++] = (char)PROTOCORE_HAAS_MDC_ETB;
    g_frame[n++] = '\r';
    g_frame[n++] = '\n';
    g_frame[n++] = (char)PROTOCORE_HAAS_MDC_PROMPT;
    TEST_ASSERT_TRUE_MESSAGE(protocore_haas_mdc_parse(g_frame, n, &g_resp), payload);
    return &g_resp;
}

// [NGC] "Data Collection Queries and Commands Table", the Command column:
//   ?Q100 ?Q101 ?Q102 ?Q104 ?Q200 ?Q201 ?Q300 ?Q301 ?Q303 ?Q304 ?Q402 ?Q403 ?Q500 ?Q600
void test_query_numbers_match_the_published_table(void)
{
    TEST_ASSERT_EQUAL_UINT16(100u, (uint16_t)HAAS_Q_SERIAL);
    TEST_ASSERT_EQUAL_UINT16(101u, (uint16_t)HAAS_Q_SOFTWARE);
    TEST_ASSERT_EQUAL_UINT16(102u, (uint16_t)HAAS_Q_MODEL);
    TEST_ASSERT_EQUAL_UINT16(104u, (uint16_t)HAAS_Q_MODE);
    TEST_ASSERT_EQUAL_UINT16(200u, (uint16_t)HAAS_Q_TOOL_CHANGES);
    TEST_ASSERT_EQUAL_UINT16(201u, (uint16_t)HAAS_Q_TOOL_IN_USE);
    TEST_ASSERT_EQUAL_UINT16(300u, (uint16_t)HAAS_Q_POWERON_TIME);
    TEST_ASSERT_EQUAL_UINT16(301u, (uint16_t)HAAS_Q_CUTTING_TIME);
    TEST_ASSERT_EQUAL_UINT16(303u, (uint16_t)HAAS_Q_LAST_CYCLE);
    TEST_ASSERT_EQUAL_UINT16(304u, (uint16_t)HAAS_Q_PREV_CYCLE);
    TEST_ASSERT_EQUAL_UINT16(402u, (uint16_t)HAAS_Q_M30_COUNTER_1);
    TEST_ASSERT_EQUAL_UINT16(403u, (uint16_t)HAAS_Q_M30_COUNTER_2);
    TEST_ASSERT_EQUAL_UINT16(500u, (uint16_t)HAAS_Q_PROGRAM_STATUS);
}

// [NGC] Query Format: "The correct query format is ?Q###, where ### is the query number", and the
// table spells the lines out: ?Q100, ?Q500, ?Q104. Five octets, then a terminator.
void test_query_line_spells_the_published_command(void)
{
    char buf[32];
    size_t n = protocore_haas_mdc_build_q(buf, sizeof(buf), HAAS_Q_SERIAL);
    TEST_ASSERT_TRUE(n >= 6u);
    TEST_ASSERT_EQUAL_MEMORY("?Q100", buf, 5u);

    n = protocore_haas_mdc_build_q(buf, sizeof(buf), HAAS_Q_PROGRAM_STATUS);
    TEST_ASSERT_TRUE(n >= 6u);
    TEST_ASSERT_EQUAL_MEMORY("?Q500", buf, 5u);

    n = protocore_haas_mdc_build_q(buf, sizeof(buf), HAAS_Q_MODE);
    TEST_ASSERT_TRUE(n >= 6u);
    TEST_ASSERT_EQUAL_MEMORY("?Q104", buf, 5u);
}

// [NGC] "Q600 Command (Read Variable)": "You can request the contents of any macro or system variable
// with the ?Q600 command; for example, ?Q600 xxxx." The variable number follows one space.
void test_macro_read_line_is_q600_space_variable(void)
{
    char buf[32];
    size_t n = protocore_haas_mdc_build_var(buf, sizeof(buf), 100);
    TEST_ASSERT_TRUE(n >= 10u);
    TEST_ASSERT_EQUAL_MEMORY("?Q600 100", buf, 9u);

    n = protocore_haas_mdc_build_var(buf, sizeof(buf), 1);
    TEST_ASSERT_TRUE(n >= 8u);
    TEST_ASSERT_EQUAL_MEMORY("?Q600 1", buf, 7u);
}

// [NGC], on typing the query: "Important: The letters must be uppercase." A lowercase octet anywhere
// in the line is a command the control does not answer.
void test_query_letters_are_uppercase(void)
{
    char buf[32];
    size_t n = protocore_haas_mdc_build_q(buf, sizeof(buf), HAAS_Q_SERIAL);
    for (size_t i = 0; i < n; i++)
    {
        TEST_ASSERT_FALSE(buf[i] >= 'a' && buf[i] <= 'z');
    }
    n = protocore_haas_mdc_build_var(buf, sizeof(buf), 100);
    for (size_t i = 0; i < n; i++)
    {
        TEST_ASSERT_FALSE(buf[i] >= 'a' && buf[i] <= 'z');
    }
}

// [NGC] Query Format: "... terminated with a new line." A new line is LF, 0x0A. The same section
// writes the response terminator as "/r/n", so the document names a CR when it wants one; the query
// sentence does not. The last octet of the line is therefore 0x0A, not 0x0D.
void test_numbered_query_is_terminated_with_a_new_line(void)
{
    char buf[32];
    size_t n = protocore_haas_mdc_build_q(buf, sizeof(buf), HAAS_Q_SERIAL);
    TEST_ASSERT_TRUE(n >= 6u);
    TEST_ASSERT_EQUAL_HEX8(0x0Au, (uint8_t)buf[n - 1u]);
}

// The same sentence governs the ?Q600 line: it is a query in the ?Q### family.
void test_macro_query_is_terminated_with_a_new_line(void)
{
    char buf[32];
    size_t n = protocore_haas_mdc_build_var(buf, sizeof(buf), 100);
    TEST_ASSERT_TRUE(n >= 10u);
    TEST_ASSERT_EQUAL_HEX8(0x0Au, (uint8_t)buf[n - 1u]);
}

// [S143] output format: "<STX><CSV response><ETB><CR/LF><0x3E>", with "STX = 0x02 (ctrl-B);
// ETB = 0x17 (ctrl-W)" and 0x3E written as the trailing octet.
void test_setting143_frame_bytes(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x02u, (uint8_t)PROTOCORE_HAAS_MDC_STX);
    TEST_ASSERT_EQUAL_HEX8(0x17u, (uint8_t)PROTOCORE_HAAS_MDC_ETB);
    TEST_ASSERT_EQUAL_HEX8(0x3Eu, (uint8_t)PROTOCORE_HAAS_MDC_PROMPT);
}

// [NGC] "Data Collection Queries and Commands Table", the Example Response column, verbatim. [NGC]
// Response Format: "Successful queries return the name of the query, then the requested information,
// separated by commas." So field 0 is the name and field 1 is the value for every row, and
// protocore_haas_mdc_value is field 1. The separator space is stripped per haas_mdc.h, which
// documents each field as "trimmed of surrounding spaces".
void test_published_example_responses_split_on_the_commas(void)
{
    struct
    {
        const char *payload;
        const char *name;
        const char *value;
    } static const ROW[] = {
        {"SERIAL NUMBER, 1234567", "SERIAL NUMBER", "1234567"},
        {"SOFTWARE VERSION, 100.17.000.2037", "SOFTWARE VERSION", "100.17.000.2037"},
        {"MODEL, CSMD-G2", "MODEL", "CSMD-G2"},
        {"MODE, ZERO", "MODE", "ZERO"},
        {"TOOL CHANGES, 35", "TOOL CHANGES", "35"},
        {"USING TOOL, 4", "USING TOOL", "4"},
        {"P.O. TIME, 06282:17:13", "P.O. TIME", "06282:17:13"},
        {"C.S. TIME, 00098:18:29", "C.S. TIME", "00098:18:29"},
        {"LAST CYCLE, 00000:00:13", "LAST CYCLE", "00000:00:13"},
        {"PREV CYCLE, 00000:00:01", "PREV CYCLE", "00000:00:01"},
        {"M30 #1, 380", "M30 #1", "380"},
        {"M30 #2, 380", "M30 #2", "380"},
        {"MACRO, 0.0", "MACRO", "0.0"},
    };
    for (size_t i = 0; i < sizeof(ROW) / sizeof(ROW[0]); i++)
    {
        const HaasMdcResp *r = framed(ROW[i].payload);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(2u, r->n_fields, ROW[i].payload);
        assert_field(r, 0, ROW[i].name);
        assert_field(r, 1, ROW[i].value);

        const char *p = NULL;
        size_t l = 0;
        TEST_ASSERT_TRUE_MESSAGE(protocore_haas_mdc_value(r, &p, &l), ROW[i].payload);
        assert_slice(p, l, ROW[i].value);
    }
}

// [NGC] table, ?Q500: definition "Three-in-one (PROGRAM, Oxxxxx, STATUS, PARTS, xxxxx", example
// response "PROGRAM, MDI, IDLE, PARTS, 380". Five fields, the program in field 1, the run status in
// field 2 and the counter in field 4, both spellings of the program field.
void test_q500_published_example(void)
{
    HaasMdcStatus st;
    const HaasMdcResp *r = framed("PROGRAM, MDI, IDLE, PARTS, 380");
    TEST_ASSERT_EQUAL_UINT8(5u, r->n_fields);
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse_status(r, &st));
    TEST_ASSERT_FALSE(st.busy);
    assert_slice(st.program, st.program_len, "MDI");
    assert_slice(st.status, st.status_len, "IDLE");
    TEST_ASSERT_TRUE(st.parts_valid);
    TEST_ASSERT_EQUAL_UINT32(380u, st.parts);

    r = framed("PROGRAM, O00123, IDLE, PARTS, 380");
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse_status(r, &st));
    assert_slice(st.program, st.program_len, "O00123");
    TEST_ASSERT_EQUAL_UINT32(380u, st.parts);
}

// [S143]: "If the control is busy, the control will output 'STATUS, BUSY.'" That reply carries no
// program and no counter, so neither may be reported as read.
void test_busy_is_status_busy(void)
{
    HaasMdcStatus st;
    const HaasMdcResp *r = framed("STATUS, BUSY");
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse_status(r, &st));
    TEST_ASSERT_TRUE(st.busy);
    assert_slice(st.status, st.status_len, "BUSY");
    TEST_ASSERT_NULL(st.program);
    TEST_ASSERT_FALSE(st.parts_valid);
}

// [S143]: "If a request is not recognized, the control will output 'UNKNOWN.'" The whole first field
// is that word, so a name that merely starts with it is a different reply.
void test_unknown_is_the_unrecognized_request_reply(void)
{
    const HaasMdcResp *r = framed("UNKNOWN");
    TEST_ASSERT_TRUE(protocore_haas_mdc_is_error(r));
    TEST_ASSERT_EQUAL_UINT8(1u, r->n_fields);
    TEST_ASSERT_FALSE(protocore_haas_mdc_value(r, NULL, NULL));

    TEST_ASSERT_FALSE(protocore_haas_mdc_is_error(framed("SERIAL NUMBER, 1234567")));
    TEST_ASSERT_FALSE(protocore_haas_mdc_is_error(framed("UNKNOWNS, 1")));
}

// [NGC] Response Format: "Responses from the control begin with > and end with /r/n." That is the
// Ethernet transport haas_mdc.h claims ("a raw TCP socket (Setting 143, default port 5051)"), and
// the frame carries no STX and no ETB. The payload is the ?Q100 row's example response.
void test_ngc_ethernet_response_is_parsed(void)
{
    static const char FRAME[] = ">SERIAL NUMBER, 1234567\r\n";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(FRAME, sizeof(FRAME) - 1u, &r));
    assert_field(&r, 0, "SERIAL NUMBER");
    assert_field(&r, 1, "1234567");
}

// [S143] puts the payload between STX and ETB and the prompt after it, so a prompt and a CR/LF left
// over from the previous reply are outside the window and belong to no field.
void test_bytes_outside_the_frame_are_ignored(void)
{
    static const char FRAME[] = ">\r\n"
                                "\x02"
                                "MODE, ZERO"
                                "\x17\r\n>";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(FRAME, sizeof(FRAME) - 1u, &r));
    TEST_ASSERT_EQUAL_UINT8(2u, r.n_fields);
    assert_field(&r, 0, "MODE");
    assert_field(&r, 1, "ZERO");
}

// haas_mdc.h documents each field as "trimmed of surrounding spaces". Every [NGC] example response
// puts one space after the comma, and P.O. TIME's value is space padded on the wire, so the padding
// is not part of the value.
void test_fields_are_trimmed_of_surrounding_spaces(void)
{
    static const char FRAME[] = "\x02"
                                "  P.O. TIME  ,     06282:17:13   "
                                "\x17";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(FRAME, sizeof(FRAME) - 1u, &r));
    assert_field(&r, 0, "P.O. TIME");
    assert_field(&r, 1, "06282:17:13");
}

// Both delimiters bound the payload, so a stream missing either one carries no complete reply yet
// and must not be reported as one.
void test_incomplete_frame_is_refused(void)
{
    HaasMdcResp r;
    static const char NO_ETB[] = "\x02"
                                 "SERIAL NUMBER, 1234567";
    static const char NO_STX[] = "SERIAL NUMBER, 1234567\x17\r\n>";
    TEST_ASSERT_FALSE(protocore_haas_mdc_parse(NO_ETB, sizeof(NO_ETB) - 1u, &r));
    TEST_ASSERT_FALSE(protocore_haas_mdc_parse(NO_STX, sizeof(NO_STX) - 1u, &r));
    TEST_ASSERT_FALSE(protocore_haas_mdc_parse(NULL, 4u, &r));
}

// Property: a parts field that is not a decimal number is reported as absent rather than as zero
// parts, and a reply that is not one of the two Q500 forms is not decoded as one.
void test_q500_refuses_a_non_numeric_parts_field(void)
{
    HaasMdcStatus st;
    const HaasMdcResp *r = framed("PROGRAM, O00123, IDLE, PARTS, ----");
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse_status(r, &st));
    TEST_ASSERT_FALSE(st.parts_valid);
    TEST_ASSERT_EQUAL_UINT32(0u, st.parts);

    TEST_ASSERT_FALSE(protocore_haas_mdc_parse_status(framed("SERIAL NUMBER, 1234567"), &st));
}

// Property: [NGC]'s ?Q600 example response is the two-field "MACRO, 0.0", so the variable number in
// the three-field form has no published example. What holds either way is that the value is handed
// back as text, and that a variable field which is not a decimal number is refused.
void test_macro_row_with_a_variable_number_decodes(void)
{
    uint32_t var = 0;
    const char *value = NULL;
    size_t value_len = 0;
    const HaasMdcResp *r = framed("MACRO, 100, 1.000000");
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse_macro(r, &var, &value, &value_len));
    TEST_ASSERT_EQUAL_UINT32(100u, var);
    assert_slice(value, value_len, "1.000000");

    TEST_ASSERT_FALSE(protocore_haas_mdc_parse_macro(framed("MACRO, ABC, 1.0"), &var, &value, &value_len));
}

// [DPR]: "A carriage return is sent out after every DPRNT block", and the block is pushed by the
// running program with no reply framing around it. A buffer carrying an STX is a Q reply instead.
// Property: the DC2 (0x12) / DC4 (0x14) bracketing stripped here is not named anywhere in [DPR],
// which says only that POPEN and PCLOS "are not required on the Haas machine".
void test_dprnt_line_is_unframed_text(void)
{
    static const char LINE[] = "\x12"
                               "TOOL 5 LIFE 87"
                               "\r\n\x14";
    const char *text = NULL;
    size_t len = 0;
    TEST_ASSERT_TRUE(protocore_haas_mdc_dprnt_line(LINE, sizeof(LINE) - 1u, &text, &len));
    assert_slice(text, len, "TOOL 5 LIFE 87");

    static const char FRAMED_REPLY[] = "\x02"
                                       "SERIAL NUMBER, 1234567"
                                       "\x17";
    TEST_ASSERT_FALSE(protocore_haas_mdc_dprnt_line(FRAMED_REPLY, sizeof(FRAMED_REPLY) - 1u, &text, &len));

    static const char EMPTY[] = ">\r\n";
    TEST_ASSERT_FALSE(protocore_haas_mdc_dprnt_line(EMPTY, sizeof(EMPTY) - 1u, &text, &len));
    TEST_ASSERT_FALSE(protocore_haas_mdc_dprnt_line(LINE, 0u, &text, &len));
}

// [DPR]: "When an asterisk is output, it is converted to a space." The spaces a DPRNT block carries
// are therefore program output, and collapsing or dropping an interior run of them loses characters
// the part program printed on purpose.
void test_dprnt_keeps_interior_spaces(void)
{
    static const char LINE[] = "X 12.5  Y 3.0\r\n";
    const char *text = NULL;
    size_t len = 0;
    TEST_ASSERT_TRUE(protocore_haas_mdc_dprnt_line(LINE, sizeof(LINE) - 1u, &text, &len));
    assert_slice(text, len, "X 12.5  Y 3.0");
}

// Property: the field table is fixed at PROTOCORE_HAAS_MDC_MAX_FIELDS entries, so a longer reply
// fills it and stops rather than writing past it, and the entries kept are the leading ones.
void test_field_table_is_bounded(void)
{
    static const char FRAME[] = "\x02"
                                "1,2,3,4,5,6,7,8,9,10"
                                "\x17";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(protocore_haas_mdc_parse(FRAME, sizeof(FRAME) - 1u, &r));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)PROTOCORE_HAAS_MDC_MAX_FIELDS, r.n_fields);
    assert_field(&r, 0, "1");
    assert_field(&r, PROTOCORE_HAAS_MDC_MAX_FIELDS - 1u, "8");
    TEST_ASSERT_FALSE(protocore_haas_mdc_field(&r, PROTOCORE_HAAS_MDC_MAX_FIELDS, NULL, NULL));
}

// Property: a builder that cannot write the whole line writes none of it. Half a query is a
// different query number.
void test_builders_refuse_a_short_buffer(void)
{
    char buf[8];
    TEST_ASSERT_EQUAL_size_t(0u, protocore_haas_mdc_build_q(buf, 6, HAAS_Q_SERIAL));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_haas_mdc_build_var(buf, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_haas_mdc_build_q(NULL, sizeof(buf), HAAS_Q_SERIAL));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_haas_mdc_build_q(buf, 0, HAAS_Q_SERIAL));
}
