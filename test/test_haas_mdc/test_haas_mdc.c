// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Haas Machine Data Collection (MDC) Q-command codec (services/machine_tool/haas_mdc): the ?Q
// query builders, the STX/ETB frame + CSV parser, the typed Q500 (program/status/parts) and Q600
// (macro) decoders, the UNKNOWN error, and the DPRNT push de-multiplexer. Byte-exact response vectors
// per the Haas MDC framing (payload between STX 0x02 and ETB 0x17, then CR LF and a '>' prompt).

#include "services/machine_tool/haas_mdc/haas_mdc.h"
#include <string.h>
#include <unity.h>

// Frame delimiters as string-literal pieces (concatenation keeps the \x.. escapes from greedily
// swallowing the following payload character).
#define STX "\x02"
#define ETB "\x17"

void setUp()
{
}
void tearDown()
{
}

// ── query builders ───────────────────────────────────────────────────────────────────────────

void test_build_q()
{
    char buf[32];
    TEST_ASSERT_GREATER_THAN(0, (int)pc_haas_mdc_build_q(buf, sizeof(buf), HAAS_Q_SERIAL));
    TEST_ASSERT_EQUAL_STRING("?Q100\r", buf);
    pc_haas_mdc_build_q(buf, sizeof(buf), HAAS_Q_MODE);
    TEST_ASSERT_EQUAL_STRING("?Q104\r", buf);
    pc_haas_mdc_build_q(buf, sizeof(buf), HAAS_Q_PROGRAM_STATUS);
    TEST_ASSERT_EQUAL_STRING("?Q500\r", buf);
    // overflow fails closed
    char tiny[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_haas_mdc_build_q(tiny, sizeof(tiny), HAAS_Q_SERIAL));
}

void test_build_var()
{
    char buf[32];
    pc_haas_mdc_build_var(buf, sizeof(buf), 100);
    TEST_ASSERT_EQUAL_STRING("?Q600 100\r", buf);
    pc_haas_mdc_build_var(buf, sizeof(buf), 5021);
    TEST_ASSERT_EQUAL_STRING("?Q600 5021\r", buf);
    char tiny[6];
    TEST_ASSERT_EQUAL_size_t(0, pc_haas_mdc_build_var(tiny, sizeof(tiny), 5021));
}

// ── response parsing ─────────────────────────────────────────────────────────────────────────

void test_parse_simple_and_value()
{
    // Q100 -> serial number
    const char *frame = STX "SERIAL NUMBER, 1234567" ETB "\r\n>";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(frame, strlen(frame), &r));
    TEST_ASSERT_EQUAL_UINT8(2, r.n_fields);
    TEST_ASSERT_EQUAL_MEMORY("SERIAL NUMBER", r.field[0], 13);
    TEST_ASSERT_EQUAL_size_t(13, r.field_len[0]);
    const char *v = NULL;
    size_t vl = 0;
    TEST_ASSERT_TRUE(pc_haas_mdc_value(&r, &v, &vl)); // leading space trimmed
    TEST_ASSERT_EQUAL_size_t(7, vl);
    TEST_ASSERT_EQUAL_MEMORY("1234567", v, 7);

    // Q104 -> mode token
    const char *mode = STX "MODE, MDI" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(mode, strlen(mode), &r));
    TEST_ASSERT_TRUE(pc_haas_mdc_value(&r, &v, &vl));
    TEST_ASSERT_EQUAL_size_t(3, vl);
    TEST_ASSERT_EQUAL_MEMORY("MDI", v, 3);
    TEST_ASSERT_FALSE(pc_haas_mdc_is_error(&r));
}

void test_parse_status_idle()
{
    const char *frame = STX "PROGRAM, O00010, IDLE, PARTS, 42" ETB "\r\n>";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(frame, strlen(frame), &r));
    TEST_ASSERT_EQUAL_UINT8(5, r.n_fields);
    HaasMdcStatus s;
    TEST_ASSERT_TRUE(pc_haas_mdc_parse_status(&r, &s));
    TEST_ASSERT_FALSE(s.busy);
    TEST_ASSERT_EQUAL_MEMORY("O00010", s.program, 6);
    TEST_ASSERT_EQUAL_size_t(6, s.program_len);
    TEST_ASSERT_EQUAL_MEMORY("IDLE", s.status, 4);
    TEST_ASSERT_EQUAL_size_t(4, s.status_len);
    TEST_ASSERT_TRUE(s.parts_valid);
    TEST_ASSERT_EQUAL_UINT32(42, s.parts);
}

void test_parse_status_busy()
{
    const char *frame = STX "STATUS, BUSY" ETB "\r\n>";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(frame, strlen(frame), &r));
    HaasMdcStatus s;
    TEST_ASSERT_TRUE(pc_haas_mdc_parse_status(&r, &s));
    TEST_ASSERT_TRUE(s.busy);
    TEST_ASSERT_EQUAL_MEMORY("BUSY", s.status, 4);
    TEST_ASSERT_FALSE(s.parts_valid);
    TEST_ASSERT_NULL(s.program);
}

void test_parse_macro()
{
    // documented 6-decimal form
    const char *f1 = STX "MACRO, 100, 0.000000" ETB "\r\n>";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(f1, strlen(f1), &r));
    uint32_t var = 0;
    const char *val = NULL;
    size_t vl = 0;
    TEST_ASSERT_TRUE(pc_haas_mdc_parse_macro(&r, &var, &val, &vl));
    TEST_ASSERT_EQUAL_UINT32(100, var);
    TEST_ASSERT_EQUAL_MEMORY("0.000000", val, 8);
    TEST_ASSERT_EQUAL_size_t(8, vl);

    // space-padded, negative value (value field trimmed of leading padding)
    const char *f2 = STX "MACRO, 5021,       -234.567" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(f2, strlen(f2), &r));
    TEST_ASSERT_TRUE(pc_haas_mdc_parse_macro(&r, &var, &val, &vl));
    TEST_ASSERT_EQUAL_UINT32(5021, var);
    TEST_ASSERT_EQUAL_size_t(8, vl);
    TEST_ASSERT_EQUAL_MEMORY("-234.567", val, 8);
}

void test_error_and_no_frame()
{
    const char *unk = STX "UNKNOWN" ETB "\r\n>";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(unk, strlen(unk), &r));
    TEST_ASSERT_TRUE(pc_haas_mdc_is_error(&r));

    // no STX at all -> not a complete frame yet
    const char *partial = "PROGRAM, O00010";
    TEST_ASSERT_FALSE(pc_haas_mdc_parse(partial, strlen(partial), &r));
    // STX but no ETB -> incomplete
    const char *noetb = STX "MODE, MDI";
    TEST_ASSERT_FALSE(pc_haas_mdc_parse(noetb, strlen(noetb), &r));
}

void test_leading_prompt()
{
    // previous response's trailing '>' prompt precedes this frame in the stream
    const char *frame = ">" STX "MODE, JOG" ETB "\r\n";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(frame, strlen(frame), &r));
    const char *v = NULL;
    size_t vl = 0;
    TEST_ASSERT_TRUE(pc_haas_mdc_value(&r, &v, &vl));
    TEST_ASSERT_EQUAL_MEMORY("JOG", v, 3);
}

void test_field_access()
{
    const char *frame = STX "PROGRAM, O00010, FEED HOLD, PARTS, 7" ETB "\r\n>";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(frame, strlen(frame), &r));
    const char *p = NULL;
    size_t l = 0;
    TEST_ASSERT_TRUE(pc_haas_mdc_field(&r, 2, &p, &l));
    TEST_ASSERT_EQUAL_size_t(9, l); // "FEED HOLD" (interior space kept)
    TEST_ASSERT_EQUAL_MEMORY("FEED HOLD", p, 9);
    TEST_ASSERT_FALSE(pc_haas_mdc_field(&r, 99, &p, &l)); // out of range
}

void test_dprnt()
{
    // a pushed DPRNT line: raw text + CRLF, no STX/ETB
    const char *line = "X-12.3456\r\n";
    const char *t = NULL;
    size_t tl = 0;
    TEST_ASSERT_TRUE(pc_haas_mdc_dprnt_line(line, strlen(line), &t, &tl));
    TEST_ASSERT_EQUAL_size_t(9, tl);
    TEST_ASSERT_EQUAL_MEMORY("X-12.3456", t, 9);

    // bracketed by POPEN (DC2 0x12) / PCLOS (DC4 0x14)
    const char *bracket = "\x12"
                          "COUNT 5\r\n"
                          "\x14";
    TEST_ASSERT_TRUE(pc_haas_mdc_dprnt_line(bracket, strlen(bracket), &t, &tl));
    TEST_ASSERT_EQUAL_size_t(7, tl); // interior space (a DPRNT '*') preserved
    TEST_ASSERT_EQUAL_MEMORY("COUNT 5", t, 7);

    // a framed Q response is NOT a DPRNT push (contains STX)
    const char *framed = STX "MODE, MDI" ETB "\r\n>";
    TEST_ASSERT_FALSE(pc_haas_mdc_dprnt_line(framed, strlen(framed), &t, &tl));
}

// ── guard / edge coverage ────────────────────────────────────────────────────────────────────

void test_build_guards()
{
    char buf[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_haas_mdc_build_q(NULL, sizeof(buf), HAAS_Q_SERIAL));
    TEST_ASSERT_EQUAL_size_t(0, pc_haas_mdc_build_q(buf, 0, HAAS_Q_SERIAL));
    TEST_ASSERT_EQUAL_size_t(0, pc_haas_mdc_build_var(NULL, sizeof(buf), 100));
    TEST_ASSERT_EQUAL_size_t(0, pc_haas_mdc_build_var(buf, 0, 100));
}

void test_parse_guards()
{
    const char *frame = STX "MODE, MDI" ETB "\r\n>";
    HaasMdcResp r;
    TEST_ASSERT_FALSE(pc_haas_mdc_parse(NULL, 4, &r));
    TEST_ASSERT_FALSE(pc_haas_mdc_parse(frame, strlen(frame), NULL));
}

void test_field_trimming_edges()
{
    HaasMdcResp r;
    // trailing spaces before the comma are trimmed off the field
    const char *trail = STX "MODE   , MDI" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(trail, strlen(trail), &r));
    TEST_ASSERT_EQUAL_size_t(4, r.field_len[0]);
    TEST_ASSERT_EQUAL_MEMORY("MODE", r.field[0], 4);

    // an all-space field collapses to empty, and so does a zero-length one
    const char *blank = STX "MODE,   ," ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(blank, strlen(blank), &r));
    TEST_ASSERT_EQUAL_UINT8(3, r.n_fields);
    TEST_ASSERT_EQUAL_size_t(0, r.field_len[1]); // "   "
    TEST_ASSERT_EQUAL_size_t(0, r.field_len[2]); // ""
}

void test_max_fields_cap()
{
    // more comma-separated fields than the struct holds: the extras are dropped, not written
    const char *many = STX "1,2,3,4,5,6,7,8,9,10" ETB "\r\n>";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(many, strlen(many), &r));
    TEST_ASSERT_EQUAL_UINT8(PC_HAAS_MDC_MAX_FIELDS, r.n_fields);
    TEST_ASSERT_EQUAL_MEMORY("8", r.field[PC_HAAS_MDC_MAX_FIELDS - 1], 1);
}

void test_accessor_guards()
{
    const char *frame = STX "MODE, MDI" ETB "\r\n>";
    HaasMdcResp r;
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(frame, strlen(frame), &r));
    const char *p = NULL;
    size_t l = 0;
    TEST_ASSERT_FALSE(pc_haas_mdc_field(NULL, 0, &p, &l));
    TEST_ASSERT_TRUE(pc_haas_mdc_field(&r, 0, NULL, NULL)); // both outputs optional
    TEST_ASSERT_FALSE(pc_haas_mdc_is_error(NULL));

    // a response the parser rejected is left with no fields; every accessor must stay closed
    HaasMdcResp empty;
    TEST_ASSERT_FALSE(pc_haas_mdc_parse("no frame here", 13, &empty));
    TEST_ASSERT_EQUAL_UINT8(0, empty.n_fields);
    TEST_ASSERT_FALSE(pc_haas_mdc_is_error(&empty)); // field index past the end
    TEST_ASSERT_FALSE(pc_haas_mdc_value(&empty, &p, &l));
}

void test_field_is_prefix_mismatch()
{
    HaasMdcResp r;
    HaasMdcStatus s;
    // the field runs past the literal: "STATUSX" is not "STATUS"
    const char *longer = STX "STATUSX, BUSY" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(longer, strlen(longer), &r));
    TEST_ASSERT_FALSE(pc_haas_mdc_parse_status(&r, &s));

    // the field stops short of the literal: "STAT" is not "STATUS" either
    const char *shorter = STX "STAT, BUSY" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(shorter, strlen(shorter), &r));
    TEST_ASSERT_FALSE(pc_haas_mdc_parse_status(&r, &s));
}

void test_parse_status_guards_and_branches()
{
    HaasMdcResp r;
    HaasMdcStatus s;
    const char *busy = STX "STATUS, BUSY" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(busy, strlen(busy), &r));
    TEST_ASSERT_FALSE(pc_haas_mdc_parse_status(NULL, &s));
    TEST_ASSERT_FALSE(pc_haas_mdc_parse_status(&r, NULL));

    // STATUS with no second field: still busy, but there is no status token to report
    const char *bare = STX "STATUS" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(bare, strlen(bare), &r));
    TEST_ASSERT_TRUE(pc_haas_mdc_parse_status(&r, &s));
    TEST_ASSERT_TRUE(s.busy);
    TEST_ASSERT_NULL(s.status);
    TEST_ASSERT_EQUAL_size_t(0, s.status_len);

    // PROGRAM with fewer than three fields is not a recognizable Q500 form
    const char *shortprog = STX "PROGRAM, O00010" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(shortprog, strlen(shortprog), &r));
    TEST_ASSERT_FALSE(pc_haas_mdc_parse_status(&r, &s));

    // exactly three fields: no PARTS pair, so the counter stays invalid
    const char *noparts = STX "PROGRAM, O00010, IDLE" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(noparts, strlen(noparts), &r));
    TEST_ASSERT_TRUE(pc_haas_mdc_parse_status(&r, &s));
    TEST_ASSERT_EQUAL_MEMORY("IDLE", s.status, 4);
    TEST_ASSERT_FALSE(s.parts_valid);
    TEST_ASSERT_EQUAL_UINT32(0, s.parts);

    // a non-numeric parts field leaves the counter invalid
    const char *badparts = STX "PROGRAM, O00010, IDLE, PARTS, 4X" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(badparts, strlen(badparts), &r));
    TEST_ASSERT_TRUE(pc_haas_mdc_parse_status(&r, &s));
    TEST_ASSERT_FALSE(s.parts_valid);

    // an empty parts field is rejected by the length guard
    const char *emptyparts = STX "PROGRAM, O00010, IDLE, PARTS," ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(emptyparts, strlen(emptyparts), &r));
    TEST_ASSERT_TRUE(pc_haas_mdc_parse_status(&r, &s));
    TEST_ASSERT_FALSE(s.parts_valid);
}

void test_parse_macro_guards_and_rejects()
{
    HaasMdcResp r;
    uint32_t var = 0;
    const char *val = NULL;
    size_t vl = 0;

    TEST_ASSERT_FALSE(pc_haas_mdc_parse_macro(NULL, &var, &val, &vl));

    // fewer than three fields
    const char *shortm = STX "MACRO, 100" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(shortm, strlen(shortm), &r));
    TEST_ASSERT_FALSE(pc_haas_mdc_parse_macro(&r, &var, &val, &vl));

    // three fields, but not a MACRO response
    const char *notmacro = STX "PROGRAM, O00010, IDLE" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(notmacro, strlen(notmacro), &r));
    TEST_ASSERT_FALSE(pc_haas_mdc_parse_macro(&r, &var, &val, &vl));

    // a signed variable number is not an unsigned decimal ('-' sorts below '0')
    const char *neg = STX "MACRO, -1, 0.000000" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(neg, strlen(neg), &r));
    TEST_ASSERT_FALSE(pc_haas_mdc_parse_macro(&r, &var, &val, &vl));

    // a trailing non-digit in the variable number ('A' sorts above '9')
    const char *alpha = STX "MACRO, 10A, 0.000000" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(alpha, strlen(alpha), &r));
    TEST_ASSERT_FALSE(pc_haas_mdc_parse_macro(&r, &var, &val, &vl));

    // an empty variable field
    const char *blank = STX "MACRO, , 0.000000" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(blank, strlen(blank), &r));
    TEST_ASSERT_FALSE(pc_haas_mdc_parse_macro(&r, &var, &val, &vl));

    // every output is optional
    const char *good = STX "MACRO, 100, 0.000000" ETB "\r\n>";
    TEST_ASSERT_TRUE(pc_haas_mdc_parse(good, strlen(good), &r));
    TEST_ASSERT_TRUE(pc_haas_mdc_parse_macro(&r, NULL, NULL, NULL));
}

void test_dprnt_guards_and_strip_edges()
{
    const char *t = NULL;
    size_t tl = 0;
    TEST_ASSERT_FALSE(pc_haas_mdc_dprnt_line(NULL, 4, &t, &tl));
    TEST_ASSERT_FALSE(pc_haas_mdc_dprnt_line("X", 0, &t, &tl));

    // a leading prompt byte and a leading CR LF are all stripped
    const char *lead = ">\r\nHELLO\r\n";
    TEST_ASSERT_TRUE(pc_haas_mdc_dprnt_line(lead, strlen(lead), &t, &tl));
    TEST_ASSERT_EQUAL_size_t(5, tl);
    TEST_ASSERT_EQUAL_MEMORY("HELLO", t, 5);

    // nothing but line terminators strips down to empty
    TEST_ASSERT_FALSE(pc_haas_mdc_dprnt_line("\r\n", 2, &t, &tl));
    // a bare PCLOS (DC4) strips off the tail, leaving nothing
    TEST_ASSERT_FALSE(pc_haas_mdc_dprnt_line("\x14", 1, &t, &tl));

    // both outputs are optional
    TEST_ASSERT_TRUE(pc_haas_mdc_dprnt_line("OK\r\n", 4, NULL, NULL));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_q);
    RUN_TEST(test_build_var);
    RUN_TEST(test_parse_simple_and_value);
    RUN_TEST(test_parse_status_idle);
    RUN_TEST(test_parse_status_busy);
    RUN_TEST(test_parse_macro);
    RUN_TEST(test_error_and_no_frame);
    RUN_TEST(test_leading_prompt);
    RUN_TEST(test_field_access);
    RUN_TEST(test_dprnt);
    RUN_TEST(test_build_guards);
    RUN_TEST(test_parse_guards);
    RUN_TEST(test_field_trimming_edges);
    RUN_TEST(test_max_fields_cap);
    RUN_TEST(test_accessor_guards);
    RUN_TEST(test_field_is_prefix_mismatch);
    RUN_TEST(test_parse_status_guards_and_branches);
    RUN_TEST(test_parse_macro_guards_and_rejects);
    RUN_TEST(test_dprnt_guards_and_strip_edges);
    return UNITY_END();
}
