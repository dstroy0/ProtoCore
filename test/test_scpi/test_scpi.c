// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SCPI / IEEE 488.2 instrument-control codec (services/instrumentation/scpi): the command
// builder, the response parsers (numeric / boolean / string / arbitrary block), the STB/ESR/ESE/SRE
// + error-queue status model, and the SCPI short/long-form header matcher. Pure host tests.

#include "services/instrumentation/scpi/scpi.h"
#include <math.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// ── common commands ──────────────────────────────────────────────────────────────────────────

void test_common_commands()
{
    TEST_ASSERT_EQUAL_STRING("*CLS", pc_scpi_common(SCPI_CLS));
    TEST_ASSERT_EQUAL_STRING("*IDN?", pc_scpi_common(SCPI_IDN_Q));
    TEST_ASSERT_EQUAL_STRING("*OPC?", pc_scpi_common(SCPI_OPC_Q));
    TEST_ASSERT_EQUAL_STRING("*RST", pc_scpi_common(SCPI_RST));
    TEST_ASSERT_EQUAL_STRING("*ESR?", pc_scpi_common(SCPI_ESR_Q));
    TEST_ASSERT_EQUAL_STRING("*STB?", pc_scpi_common(SCPI_STB_Q));
    TEST_ASSERT_EQUAL_STRING("*WAI", pc_scpi_common(SCPI_WAI));
}

// ── command builder ──────────────────────────────────────────────────────────────────────────

void test_build_no_args()
{
    char buf[32];
    size_t n = pc_scpi_build(buf, sizeof(buf), "*RST", NULL, 0);
    TEST_ASSERT_EQUAL_STRING("*RST\n", buf);
    TEST_ASSERT_EQUAL_size_t(5, n);
}

void test_build_one_arg()
{
    char buf[64];
    const char *args[] = {"1.5"};
    size_t n = pc_scpi_build(buf, sizeof(buf), "SOURce:VOLTage", args, 1);
    TEST_ASSERT_EQUAL_STRING("SOURce:VOLTage 1.5\n", buf);
    TEST_ASSERT_EQUAL_size_t(19, n);
}

void test_build_multi_arg()
{
    char buf[64];
    const char *args[] = {"1.5", "MAX"};
    size_t n = pc_scpi_build(buf, sizeof(buf), "SOUR:VOLT", args, 2);
    TEST_ASSERT_EQUAL_STRING("SOUR:VOLT 1.5,MAX\n", buf);
    TEST_ASSERT_EQUAL_size_t(18, n);
}

void test_build_overflow_and_guards()
{
    char small[8];
    // header alone longer than the buffer
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_build(small, sizeof(small), "SOURce:VOLTage", NULL, 0));
    char buf[16];
    const char *args[] = {"1234567890"};
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_build(buf, sizeof(buf), "VOLT", args, 1)); // arg pushes past cap
    // null guards
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_build(NULL, sizeof(buf), "X", NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_build(buf, sizeof(buf), NULL, NULL, 0));
    const char *bad[] = {NULL};
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_build(buf, sizeof(buf), "V", bad, 1)); // null arg element
}

void test_fmt_real()
{
    char buf[32];
    TEST_ASSERT_GREATER_THAN(0, (int)pc_scpi_fmt_real(buf, sizeof(buf), 1.5));
    TEST_ASSERT_EQUAL_STRING("1.5", buf);
    pc_scpi_fmt_real(buf, sizeof(buf), 0.0);
    TEST_ASSERT_EQUAL_STRING("0", buf);
    pc_scpi_fmt_real(buf, sizeof(buf), -12.25);
    TEST_ASSERT_EQUAL_STRING("-12.25", buf);
    // a tiny buffer fails closed
    char tiny[2];
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_fmt_real(tiny, sizeof(tiny), 123456.789));
}

// ── numeric response parser ──────────────────────────────────────────────────────────────────

void test_parse_number()
{
    double v = 0;
    TEST_ASSERT_TRUE(pc_scpi_parse_number("42", 2, &v)); // NR1
    TEST_ASSERT_EQUAL_DOUBLE(42.0, v);
    TEST_ASSERT_TRUE(pc_scpi_parse_number("-3.14", 5, &v)); // NR2, sign
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, -3.14, v);
    TEST_ASSERT_TRUE(pc_scpi_parse_number("1.5E3", 5, &v)); // NR3
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1500.0, v);
    TEST_ASSERT_TRUE(pc_scpi_parse_number("2.5e-2", 6, &v)); // negative exponent, lowercase e
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.025, v);
    TEST_ASSERT_TRUE(pc_scpi_parse_number("+.5", 3, &v)); // leading '+' and no integer digits
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.5, v);
    TEST_ASSERT_TRUE(pc_scpi_parse_number("9.9E37", 6, &v)); // SCPI +INFinity special value parses
    TEST_ASSERT_TRUE(v > 1e37);
}

void test_parse_number_rejects()
{
    double v = 0;
    TEST_ASSERT_FALSE(pc_scpi_parse_number("", 0, &v));
    TEST_ASSERT_FALSE(pc_scpi_parse_number("abc", 3, &v));
    TEST_ASSERT_FALSE(pc_scpi_parse_number("1.5V", 4, &v)); // trailing unit
    TEST_ASSERT_FALSE(pc_scpi_parse_number("1.5E", 4, &v)); // exponent with no digits
    TEST_ASSERT_FALSE(pc_scpi_parse_number("+", 1, &v));    // sign only
    TEST_ASSERT_FALSE(pc_scpi_parse_number("1 2", 3, &v));  // embedded space
}

// ── boolean response parser ──────────────────────────────────────────────────────────────────

void test_parse_bool()
{
    proto_bool b = PROTO_FALSE;
    TEST_ASSERT_TRUE(pc_scpi_parse_bool("1", 1, &b));
    TEST_ASSERT_TRUE(b);
    TEST_ASSERT_TRUE(pc_scpi_parse_bool("0", 1, &b));
    TEST_ASSERT_FALSE(b);
    TEST_ASSERT_TRUE(pc_scpi_parse_bool("ON", 2, &b));
    TEST_ASSERT_TRUE(b);
    TEST_ASSERT_TRUE(pc_scpi_parse_bool("off", 3, &b));
    TEST_ASSERT_FALSE(b);
    TEST_ASSERT_TRUE(pc_scpi_parse_bool("On", 2, &b));
    TEST_ASSERT_TRUE(b);
    TEST_ASSERT_FALSE(pc_scpi_parse_bool("YES", 3, &b));
    TEST_ASSERT_FALSE(pc_scpi_parse_bool("", 0, &b));
}

// ── string response parser ───────────────────────────────────────────────────────────────────

void test_parse_string()
{
    char out[32];
    TEST_ASSERT_EQUAL_size_t(5, pc_scpi_parse_string("\"hello\"", 7, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("hello", out);
    // single quotes
    TEST_ASSERT_EQUAL_size_t(3, pc_scpi_parse_string("'abc'", 5, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc", out);
    // a doubled quote collapses to one
    TEST_ASSERT_EQUAL_size_t(3, pc_scpi_parse_string("\"a\"\"b\"", 6, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a\"b", out);
    // empty string
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_parse_string("\"\"", 2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    // missing / mismatched quotes -> rejected
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_parse_string("hello", 5, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_parse_string("\"hello'", 7, out, sizeof(out)));
    // overflow fails closed
    char tiny[3];
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_parse_string("\"hello\"", 7, tiny, sizeof(tiny)));
}

// ── arbitrary block parser ───────────────────────────────────────────────────────────────────

void test_parse_block_definite()
{
    const uint8_t blk[] = "#14DATA"; // # 1 4 then 4 bytes "DATA"
    const uint8_t *data = NULL;
    size_t dlen = 0, consumed = 0;
    TEST_ASSERT_TRUE(pc_scpi_parse_block(blk, 7, &data, &dlen, &consumed));
    TEST_ASSERT_EQUAL_size_t(4, dlen);
    TEST_ASSERT_EQUAL_MEMORY("DATA", data, 4);
    TEST_ASSERT_EQUAL_size_t(7, consumed);

    // two length digits: #205HELLO -> length "05" = 5, "HELLO"
    const uint8_t blk2[] = "#205HELLO";
    TEST_ASSERT_TRUE(pc_scpi_parse_block(blk2, 9, &data, &dlen, &consumed));
    TEST_ASSERT_EQUAL_size_t(5, dlen);
    TEST_ASSERT_EQUAL_MEMORY("HELLO", data, 5);
    TEST_ASSERT_EQUAL_size_t(9, consumed);
}

void test_parse_block_indefinite()
{
    const uint8_t blk[] = "#0HELLO\n"; // #0 <data> NL
    const uint8_t *data = NULL;
    size_t dlen = 0, consumed = 0;
    TEST_ASSERT_TRUE(pc_scpi_parse_block(blk, 8, &data, &dlen, &consumed));
    TEST_ASSERT_EQUAL_size_t(5, dlen);
    TEST_ASSERT_EQUAL_MEMORY("HELLO", data, 5);
    TEST_ASSERT_EQUAL_size_t(8, consumed);
}

void test_parse_block_rejects()
{
    const uint8_t *data = NULL;
    size_t dlen = 0, consumed = 0;
    // truncated definite block (says 4 bytes, only 3 present)
    const uint8_t trunc[] = "#14DAT";
    TEST_ASSERT_FALSE(pc_scpi_parse_block(trunc, 6, &data, &dlen, &consumed));
    // not a block
    TEST_ASSERT_FALSE(pc_scpi_parse_block((const uint8_t *)"hello", 5, &data, &dlen, &consumed));
    // bad length-count digit
    const uint8_t bad[] = "#X4DATA";
    TEST_ASSERT_FALSE(pc_scpi_parse_block(bad, 7, &data, &dlen, &consumed));
    // indefinite with no terminating newline
    const uint8_t noeol[] = "#0HELLO";
    TEST_ASSERT_FALSE(pc_scpi_parse_block(noeol, 7, &data, &dlen, &consumed));
}

// ── status model ─────────────────────────────────────────────────────────────────────────────

void test_status_error_queue_fifo()
{
    ScpiStatus s;
    pc_scpi_status_init(&s);
    ScpiError e;
    // empty queue -> 0,"No error"
    TEST_ASSERT_FALSE(pc_scpi_pop_error(&s, &e));
    TEST_ASSERT_EQUAL_INT16(0, e.number);
    TEST_ASSERT_EQUAL_STRING("No error", e.msg);

    pc_scpi_push_error(&s, -113, NULL); // std text filled in
    pc_scpi_push_error(&s, -222, NULL);
    // FIFO order
    TEST_ASSERT_TRUE(pc_scpi_pop_error(&s, &e));
    TEST_ASSERT_EQUAL_INT16(-113, e.number);
    TEST_ASSERT_EQUAL_STRING("Undefined header", e.msg);
    TEST_ASSERT_TRUE(pc_scpi_pop_error(&s, &e));
    TEST_ASSERT_EQUAL_INT16(-222, e.number);
    TEST_ASSERT_EQUAL_STRING("Data out of range", e.msg);
    TEST_ASSERT_FALSE(pc_scpi_pop_error(&s, &e)); // drained
}

void test_status_esr_class_bits()
{
    ScpiStatus s;
    pc_scpi_status_init(&s);
    pc_scpi_push_error(&s, -100, NULL); // command error -> CME
    TEST_ASSERT_BITS_HIGH(SCPI_ESR_CME, s.esr);
    pc_scpi_push_error(&s, -200, NULL); // execution error -> EXE
    TEST_ASSERT_BITS_HIGH(SCPI_ESR_EXE, s.esr);
    pc_scpi_push_error(&s, -310, NULL); // device-specific -> DDE
    TEST_ASSERT_BITS_HIGH(SCPI_ESR_DDE, s.esr);
    pc_scpi_push_error(&s, -410, NULL); // query error -> QYE
    TEST_ASSERT_BITS_HIGH(SCPI_ESR_QYE, s.esr);
}

void test_status_stb_and_mss()
{
    ScpiStatus s;
    pc_scpi_status_init(&s);
    // an error in the queue raises EAV
    pc_scpi_push_error(&s, -100, NULL);
    TEST_ASSERT_BITS_HIGH(SCPI_STB_EAV, pc_scpi_stb(&s));

    // ESR & ESE -> ESB; ESB & SRE -> MSS
    pc_scpi_status_init(&s);
    pc_scpi_event(&s, SCPI_ESR_OPC);
    s.ese = SCPI_ESR_OPC; // enable OPC into the summary
    uint8_t stb = pc_scpi_stb(&s);
    TEST_ASSERT_BITS_HIGH(SCPI_STB_ESB, stb);
    TEST_ASSERT_BITS_LOW(SCPI_STB_MSS, stb); // not requested yet
    s.sre = SCPI_STB_ESB;                    // request service on the event summary
    stb = pc_scpi_stb(&s);
    TEST_ASSERT_BITS_HIGH(SCPI_STB_ESB | SCPI_STB_MSS, stb);

    // ESR event NOT enabled by ESE does not summarize
    pc_scpi_status_init(&s);
    pc_scpi_event(&s, SCPI_ESR_CME);
    TEST_ASSERT_BITS_LOW(SCPI_STB_ESB, pc_scpi_stb(&s));
}

void test_status_cls()
{
    ScpiStatus s;
    pc_scpi_status_init(&s);
    pc_scpi_push_error(&s, -222, NULL);
    pc_scpi_event(&s, SCPI_ESR_OPC);
    s.ese = SCPI_ESR_OPC; // enables survive *CLS
    s.sre = SCPI_STB_EAV;
    pc_scpi_cls(&s);
    TEST_ASSERT_EQUAL_UINT8(0, s.esr);
    TEST_ASSERT_EQUAL_UINT8(0, s.count);
    TEST_ASSERT_EQUAL_UINT8(SCPI_ESR_OPC, s.ese); // untouched
    TEST_ASSERT_EQUAL_UINT8(SCPI_STB_EAV, s.sre); // untouched
    ScpiError e;
    TEST_ASSERT_FALSE(pc_scpi_pop_error(&s, &e));
}

void test_status_queue_overflow()
{
    ScpiStatus s;
    pc_scpi_status_init(&s);
    for (int i = 0; i < PC_SCPI_ERR_QUEUE; i++)
    {
        pc_scpi_push_error(&s, (int16_t)(-100 - i), "e");
    }
    pc_scpi_push_error(&s, -222, "one too many"); // overflow -> tail becomes -350
    ScpiError e;
    int16_t last = 0;
    while (pc_scpi_pop_error(&s, &e))
    {
        last = e.number;
    }
    TEST_ASSERT_EQUAL_INT16(-350, last);
}

void test_std_error_lookup()
{
    TEST_ASSERT_EQUAL_STRING("No error", pc_scpi_std_error(0));
    TEST_ASSERT_EQUAL_STRING("Undefined header", pc_scpi_std_error(-113));
    TEST_ASSERT_EQUAL_STRING("Queue overflow", pc_scpi_std_error(-350));
    TEST_ASSERT_EQUAL_STRING("Query UNTERMINATED", pc_scpi_std_error(-420));
    TEST_ASSERT_EQUAL_STRING("", pc_scpi_std_error(-999)); // unknown
}

// ── header matcher ───────────────────────────────────────────────────────────────────────────

static proto_bool match(const char *in, const char *pat)
{
    return pc_scpi_match(in, strlen(in), pat);
}

void test_match_short_long_form()
{
    TEST_ASSERT_TRUE(match("SYST:ERR?", "SYSTem:ERRor?"));     // short form
    TEST_ASSERT_TRUE(match("system:error?", "SYSTem:ERRor?")); // long form, lowercase
    TEST_ASSERT_TRUE(match("SYSTEM:ERROR?", "SYSTem:ERRor?")); // long form, uppercase
    TEST_ASSERT_TRUE(match("MEAS:VOLT:DC?", "MEASure:VOLTage:DC?"));
    // a header may carry parameters after a space - only the header is matched
    TEST_ASSERT_TRUE(match("SOUR:VOLT 1.5", "SOURce:VOLTage"));
}

void test_match_query_suffix()
{
    TEST_ASSERT_FALSE(match("SYST:ERR", "SYSTem:ERRor?")); // pattern is a query, input is not
    TEST_ASSERT_FALSE(match("SYST:ERR?", "SYSTem:ERRor")); // input is a query, pattern is not
    TEST_ASSERT_TRUE(match("SYST:ERR", "SYSTem:ERRor"));   // both non-query
}

void test_match_numeric_suffix()
{
    TEST_ASSERT_TRUE(match("OUTP2:STAT", "OUTPut2:STATe"));  // suffix matches
    TEST_ASSERT_TRUE(match("OUTP:STAT", "OUTPut:STATe"));    // both default to 1
    TEST_ASSERT_FALSE(match("OUTP:STAT", "OUTPut2:STATe"));  // 1 vs 2
    TEST_ASSERT_FALSE(match("OUTP3:STAT", "OUTPut2:STATe")); // 3 vs 2
}

void test_match_common_and_root()
{
    TEST_ASSERT_TRUE(match("*IDN?", "*IDN?"));
    TEST_ASSERT_TRUE(match("*idn?", "*IDN?")); // case-insensitive
    TEST_ASSERT_FALSE(match("*RST", "*IDN?"));
    TEST_ASSERT_TRUE(match(":SYST:ERR?", "SYSTem:ERRor?")); // absolute-root leading ':'
}

void test_match_negatives()
{
    TEST_ASSERT_FALSE(match("MEAS:CURR?", "MEASure:VOLTage?"));  // different node
    TEST_ASSERT_FALSE(match("SYST:ERR:NEXT?", "SYSTem:ERRor?")); // extra depth
    TEST_ASSERT_FALSE(match("SYST?", "SYSTem:ERRor?"));          // too shallow
    TEST_ASSERT_FALSE(match("SYSTE:ERR?", "SYSTem:ERRor?"));     // partial (not short nor long form)
}

// ── guard / edge coverage ────────────────────────────────────────────────────────────────────

void test_common_commands_full_enum()
{
    TEST_ASSERT_EQUAL_STRING("*ESE", pc_scpi_common(SCPI_ESE));
    TEST_ASSERT_EQUAL_STRING("*ESE?", pc_scpi_common(SCPI_ESE_Q));
    TEST_ASSERT_EQUAL_STRING("*OPC", pc_scpi_common(SCPI_OPC));
    TEST_ASSERT_EQUAL_STRING("*SRE", pc_scpi_common(SCPI_SRE));
    TEST_ASSERT_EQUAL_STRING("*SRE?", pc_scpi_common(SCPI_SRE_Q));
    TEST_ASSERT_EQUAL_STRING("*TST?", pc_scpi_common(SCPI_TST_Q));
    // a value outside the enumeration falls past every case -> the empty string
    TEST_ASSERT_EQUAL_STRING("", pc_scpi_common((ScpiCommon)(200)));
}

void test_build_guard_edges()
{
    char buf[32];
    // a non-zero argc with no args vector at all
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_build(buf, sizeof(buf), "VOLT", NULL, 1));
    // an empty header is not a command
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_build(buf, sizeof(buf), "", NULL, 0));
    // the header fits but leaves no room for the terminating '\n' + NUL
    char exact[6];
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_build(exact, sizeof(exact), "ABCDE", NULL, 0));
    // one more byte of capacity is enough
    char room[7];
    TEST_ASSERT_EQUAL_size_t(6, pc_scpi_build(room, sizeof(room), "ABCDE", NULL, 0));
    TEST_ASSERT_EQUAL_STRING("ABCDE\n", room);
}

void test_fmt_real_guards()
{
    char buf[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_fmt_real(NULL, sizeof(buf), 1.0));
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_fmt_real(buf, 0, 1.0));
}

void test_parse_number_guards_and_exponent_forms()
{
    double v = 0;
    TEST_ASSERT_FALSE(pc_scpi_parse_number(NULL, 2, &v));
    TEST_ASSERT_FALSE(pc_scpi_parse_number("42", 2, NULL));
    // an explicit '+' exponent sign
    TEST_ASSERT_TRUE(pc_scpi_parse_number("1.5E+3", 6, &v));
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 1500.0, v);
    // junk after the exponent digits is trailing junk, not a unit-less number
    TEST_ASSERT_FALSE(pc_scpi_parse_number("1E5X", 4, &v));
}

void test_parse_bool_guards()
{
    proto_bool b = PROTO_FALSE;
    TEST_ASSERT_FALSE(pc_scpi_parse_bool(NULL, 1, &b));
    TEST_ASSERT_FALSE(pc_scpi_parse_bool("1", 1, NULL));
    TEST_ASSERT_FALSE(pc_scpi_parse_bool("X", 1, &b)); // one character, neither '1' nor '0'
}

void test_parse_string_guards()
{
    char out[32];
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_parse_string(NULL, 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_parse_string("\"a\"", 3, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_parse_string("\"a\"", 3, out, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_parse_string("\"", 1, out, sizeof(out))); // shorter than a quote pair
}

void test_parse_string_malformed_interior_quote()
{
    char out[32];
    // an unpaired interior quote is a malformed close
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_parse_string("\"a\"b\"", 5, out, sizeof(out)));
    // a quote in the last scannable position has no room for its pair
    TEST_ASSERT_EQUAL_size_t(0, pc_scpi_parse_string("\"a\"\"", 4, out, sizeof(out)));
}

void test_parse_block_guards()
{
    const uint8_t blk[] = "#14DATA";
    const uint8_t *data = NULL;
    size_t dlen = 0, consumed = 0;
    TEST_ASSERT_FALSE(pc_scpi_parse_block(NULL, 7, &data, &dlen, &consumed));
    TEST_ASSERT_FALSE(pc_scpi_parse_block(blk, 7, NULL, &dlen, &consumed));
    TEST_ASSERT_FALSE(pc_scpi_parse_block(blk, 7, &data, NULL, &consumed));
    TEST_ASSERT_FALSE(pc_scpi_parse_block(blk, 7, &data, &dlen, NULL));
    TEST_ASSERT_FALSE(pc_scpi_parse_block(blk, 1, &data, &dlen, &consumed)); // shorter than "#n"
}

void test_parse_block_length_field_rejects()
{
    const uint8_t *data = NULL;
    size_t dlen = 0, consumed = 0;
    // an indefinite marker with nothing after it
    const uint8_t bare[] = "#0";
    TEST_ASSERT_FALSE(pc_scpi_parse_block(bare, 2, &data, &dlen, &consumed));
    // a width byte below '1'
    const uint8_t low[] = "#!4DATA";
    TEST_ASSERT_FALSE(pc_scpi_parse_block(low, 7, &data, &dlen, &consumed));
    // the declared width runs off the end of the buffer
    const uint8_t narrow[] = "#25";
    TEST_ASSERT_FALSE(pc_scpi_parse_block(narrow, 3, &data, &dlen, &consumed));
    // a non-digit inside the length field
    const uint8_t nondigit[] = "#1X";
    TEST_ASSERT_FALSE(pc_scpi_parse_block(nondigit, 3, &data, &dlen, &consumed));
}

void test_status_null_guards()
{
    // every status entry point tolerates a missing status block
    pc_scpi_status_init(NULL);
    pc_scpi_event(NULL, SCPI_ESR_OPC);
    pc_scpi_cls(NULL);
    TEST_ASSERT_EQUAL_UINT8(0, pc_scpi_stb(NULL));

    ScpiStatus s;
    pc_scpi_status_init(&s);
    pc_scpi_push_error(NULL, -100, "x");
    pc_scpi_push_error(&s, 0, "zero is not an error"); // 0 never queues and latches nothing
    TEST_ASSERT_EQUAL_UINT8(0, s.count);
    TEST_ASSERT_EQUAL_UINT8(0, s.esr);

    ScpiError e;
    TEST_ASSERT_FALSE(pc_scpi_pop_error(&s, NULL)); // nowhere to write the entry
    e.number = -1;
    e.msg = NULL;
    TEST_ASSERT_FALSE(pc_scpi_pop_error(NULL, &e)); // no queue -> the no-error entry
    TEST_ASSERT_EQUAL_INT16(0, e.number);
    TEST_ASSERT_EQUAL_STRING("No error", e.msg);
}

void test_status_esr_class_bits_full_range()
{
    ScpiStatus s;
    // a positive number is device-specific -> DDE
    pc_scpi_status_init(&s);
    pc_scpi_push_error(&s, 42, "vendor fault");
    TEST_ASSERT_EQUAL_UINT8(SCPI_ESR_DDE, s.esr);

    // the -5xx .. -8xx class boundaries (each range's first number falls into the NEXT test)
    const int16_t numbers[] = {-500, -600, -700, -800};
    const uint8_t bits[] = {SCPI_ESR_PON, SCPI_ESR_URQ, SCPI_ESR_RQC, SCPI_ESR_OPC};
    for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++)
    {
        pc_scpi_status_init(&s);
        pc_scpi_push_error(&s, numbers[i], "x");
        TEST_ASSERT_EQUAL_UINT8(bits[i], s.esr);
    }

    // -900 and below latch no class bit, yet the entry still queues
    pc_scpi_status_init(&s);
    pc_scpi_push_error(&s, -900, "x");
    TEST_ASSERT_EQUAL_UINT8(0, s.esr);
    TEST_ASSERT_EQUAL_UINT8(1, s.count);
}

void test_match_null_and_empty()
{
    TEST_ASSERT_FALSE(pc_scpi_match(NULL, 4, "SYSTem"));
    TEST_ASSERT_FALSE(pc_scpi_match("SYST", 4, NULL));
    TEST_ASSERT_FALSE(pc_scpi_match("", 0, "SYSTem"));  // no input node at all
    TEST_ASSERT_FALSE(pc_scpi_match(":", 1, "SYSTem")); // nothing after the root anchor
    TEST_ASSERT_FALSE(pc_scpi_match("SYST", 4, ""));    // no pattern node at all
}

void test_match_bad_numeric_suffix()
{
    // a non-digit in the input's numeric suffix
    TEST_ASSERT_FALSE(pc_scpi_match("SYST1X", 6, "SYSTem1"));
    // a non-digit in the pattern's numeric suffix
    TEST_ASSERT_FALSE(pc_scpi_match("SYST1", 5, "SYSTem1X"));
}

void test_match_non_alpha_header_bytes()
{
    // bytes between 'Z' and 'a', and above 'z', are not alpha - they land in the suffix field
    TEST_ASSERT_FALSE(pc_scpi_match("SYST_", 5, "SYSTem"));
    TEST_ASSERT_FALSE(pc_scpi_match("SYST~", 5, "SYSTem"));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_common_commands);
    RUN_TEST(test_build_no_args);
    RUN_TEST(test_build_one_arg);
    RUN_TEST(test_build_multi_arg);
    RUN_TEST(test_build_overflow_and_guards);
    RUN_TEST(test_fmt_real);
    RUN_TEST(test_parse_number);
    RUN_TEST(test_parse_number_rejects);
    RUN_TEST(test_parse_bool);
    RUN_TEST(test_parse_string);
    RUN_TEST(test_parse_block_definite);
    RUN_TEST(test_parse_block_indefinite);
    RUN_TEST(test_parse_block_rejects);
    RUN_TEST(test_status_error_queue_fifo);
    RUN_TEST(test_status_esr_class_bits);
    RUN_TEST(test_status_stb_and_mss);
    RUN_TEST(test_status_cls);
    RUN_TEST(test_status_queue_overflow);
    RUN_TEST(test_std_error_lookup);
    RUN_TEST(test_match_short_long_form);
    RUN_TEST(test_match_query_suffix);
    RUN_TEST(test_match_numeric_suffix);
    RUN_TEST(test_match_common_and_root);
    RUN_TEST(test_match_negatives);
    RUN_TEST(test_common_commands_full_enum);
    RUN_TEST(test_build_guard_edges);
    RUN_TEST(test_fmt_real_guards);
    RUN_TEST(test_parse_number_guards_and_exponent_forms);
    RUN_TEST(test_parse_bool_guards);
    RUN_TEST(test_parse_string_guards);
    RUN_TEST(test_parse_string_malformed_interior_quote);
    RUN_TEST(test_parse_block_guards);
    RUN_TEST(test_parse_block_length_field_rejects);
    RUN_TEST(test_status_null_guards);
    RUN_TEST(test_status_esr_class_bits_full_range);
    RUN_TEST(test_match_null_and_empty);
    RUN_TEST(test_match_bad_numeric_suffix);
    RUN_TEST(test_match_non_alpha_header_bytes);
    return UNITY_END();
}
