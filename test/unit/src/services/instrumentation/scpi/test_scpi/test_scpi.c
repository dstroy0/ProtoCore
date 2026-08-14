// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the SCPI / IEEE 488.2 codec (services/instrumentation/scpi/scpi.h).
//
// The load-bearing case is test_scpi99_exact_short_or_long_form_only. SCPI-99 Volume 1 sec 6.2.1
// states it in one sentence: "A SCPI instrument shall accept only the exact short and the exact
// long forms. Sending a header that is not the short form, nor the complete long form to a SCPI
// instrument shall cause it to generate an error." Every other truncation is a different command,
// and an instrument that accepts one has silently agreed to do something the controller did not
// ask for. The case walks a pattern's short form, its long form and every truncation between them.
//
// The status model is anchored on SCPI-99 Volume 2 sec 21.8: sec 21.8.1 gives the queue's overflow
// rule and its empty response verbatim, and sec 21.8.9 through 21.8.12 give the number ranges and
// the ESR bit each class sets. The special numeric values are Volume 1 sec 7.2.1.4 and 7.2.1.5.

#include "services/instrumentation/scpi/scpi.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// SCPI-99 Vol 1 sec 6.2.1. "SYSTem" has short form "SYST" and long form "SYSTEM"; nothing between
// them is accepted, and neither is anything past the long form.
void test_scpi99_exact_short_or_long_form_only(void)
{
    static const char *const ACCEPT[] = {"SYST:ERR?", "SYSTEM:ERROR?", "SYST:ERROR?", "SYSTEM:ERR?",
                                         "syst:err?", "SySt:ErR?"}; // sec 6.2.1: matching is case-insensitive
    for (size_t i = 0; i < sizeof(ACCEPT) / sizeof(ACCEPT[0]); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(protocore_scpi_match(ACCEPT[i], strlen(ACCEPT[i]), "SYSTem:ERRor?"), ACCEPT[i]);
    }

    static const char *const REJECT[] = {
        "SY:ERR?",       // shorter than the short form
        "SYS:ERR?",      // one character short of the short form
        "SYSTE:ERR?",    // between the short and the long form
        "SYSTEMS:ERR?",  // past the long form
        "SYST:ER?",      // the second node truncated below its short form
        "SYST:ERRO?",    // the second node between its forms
        "SYST:ERRORS?",  // past the second node's long form
    };
    for (size_t i = 0; i < sizeof(REJECT) / sizeof(REJECT[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(protocore_scpi_match(REJECT[i], strlen(REJECT[i]), "SYSTem:ERRor?"), REJECT[i]);
    }
}

// A query is a distinct command from the setting of the same name, so the '?' must agree on both
// sides, and the node depth must agree too.
void test_query_marker_and_depth_must_agree(void)
{
    TEST_ASSERT_FALSE(protocore_scpi_match("SYST:ERR", 8, "SYSTem:ERRor?"));
    TEST_ASSERT_FALSE(protocore_scpi_match("SYST:ERR?", 9, "SYSTem:ERRor"));
    TEST_ASSERT_TRUE(protocore_scpi_match("SOUR:VOLT", 9, "SOURce:VOLTage"));

    TEST_ASSERT_FALSE(protocore_scpi_match("SOUR", 4, "SOURce:VOLTage"));          // too shallow
    TEST_ASSERT_FALSE(protocore_scpi_match("SOUR:VOLT:LEV", 13, "SOURce:VOLTage")); // too deep
    TEST_ASSERT_TRUE(protocore_scpi_match("SOUR:VOLT:LEV:IMM:AMPL", 22, "SOURce:VOLTage:LEVel:IMMediate:AMPLitude"));
}

// SCPI-99 Vol 1 sec 6.2.5: an omitted numeric suffix means the same node as suffix 1, so OUTP and
// OUTPut1 are one command and OUTPut2 is another.
void test_numeric_suffix_defaults_to_one(void)
{
    TEST_ASSERT_TRUE(protocore_scpi_match("OUTP", 4, "OUTPut1"));
    TEST_ASSERT_TRUE(protocore_scpi_match("OUTP1", 5, "OUTPut"));
    TEST_ASSERT_TRUE(protocore_scpi_match("OUTPUT1", 7, "OUTPut1"));
    TEST_ASSERT_FALSE(protocore_scpi_match("OUTP2", 5, "OUTPut1"));
    TEST_ASSERT_FALSE(protocore_scpi_match("OUTP2", 5, "OUTPut"));
    TEST_ASSERT_TRUE(protocore_scpi_match("OUTP2", 5, "OUTPut2"));
    TEST_ASSERT_TRUE(protocore_scpi_match("SENS3:VOLT", 10, "SENSe3:VOLTage"));
    TEST_ASSERT_FALSE(protocore_scpi_match("SENS3:VOLT", 10, "SENSe4:VOLTage"));
}

// SCPI-99 Vol 1 sec 6.2.3: a leading colon anchors the header at the root of the tree, and the
// header ends at the first space, so the parameters that follow do not take part in the match.
void test_leading_colon_and_parameters_are_ignored(void)
{
    TEST_ASSERT_TRUE(protocore_scpi_match(":SYST:ERR?", 10, "SYSTem:ERRor?"));
    TEST_ASSERT_TRUE(protocore_scpi_match("SOUR:VOLT 1.5", 13, "SOURce:VOLTage"));
    TEST_ASSERT_TRUE(protocore_scpi_match(":SOUR:VOLT 1.5,2.5", 18, "SOURce:VOLTage"));
}

// IEEE 488.2 sec 10: the mandatory common commands are matched whole, and a common-command pattern
// never matches a hierarchy header.
void test_common_commands(void)
{
    TEST_ASSERT_EQUAL_STRING("*CLS", protocore_scpi_common(SCPI_CLS));
    TEST_ASSERT_EQUAL_STRING("*ESE", protocore_scpi_common(SCPI_ESE));
    TEST_ASSERT_EQUAL_STRING("*ESE?", protocore_scpi_common(SCPI_ESE_Q));
    TEST_ASSERT_EQUAL_STRING("*ESR?", protocore_scpi_common(SCPI_ESR_Q));
    TEST_ASSERT_EQUAL_STRING("*IDN?", protocore_scpi_common(SCPI_IDN_Q));
    TEST_ASSERT_EQUAL_STRING("*OPC", protocore_scpi_common(SCPI_OPC));
    TEST_ASSERT_EQUAL_STRING("*OPC?", protocore_scpi_common(SCPI_OPC_Q));
    TEST_ASSERT_EQUAL_STRING("*RST", protocore_scpi_common(SCPI_RST));
    TEST_ASSERT_EQUAL_STRING("*SRE", protocore_scpi_common(SCPI_SRE));
    TEST_ASSERT_EQUAL_STRING("*SRE?", protocore_scpi_common(SCPI_SRE_Q));
    TEST_ASSERT_EQUAL_STRING("*STB?", protocore_scpi_common(SCPI_STB_Q));
    TEST_ASSERT_EQUAL_STRING("*TST?", protocore_scpi_common(SCPI_TST_Q));
    TEST_ASSERT_EQUAL_STRING("*WAI", protocore_scpi_common(SCPI_WAI));

    TEST_ASSERT_TRUE(protocore_scpi_match("*IDN?", 5, "*IDN?"));
    TEST_ASSERT_TRUE(protocore_scpi_match("*idn?", 5, "*IDN?"));
    TEST_ASSERT_FALSE(protocore_scpi_match("*IDN", 4, "*IDN?"));
    TEST_ASSERT_FALSE(protocore_scpi_match("*ESR?", 5, "*ESE?"));
}

// A command line is the header, then a space and the comma-joined parameters, then the message
// terminator (IEEE 488.2 sec 7.7: <PROGRAM MESSAGE TERMINATOR>).
void test_command_line_form(void)
{
    char buf[64];
    TEST_ASSERT_EQUAL_UINT(5u, protocore_scpi_build(buf, sizeof(buf), "*RST", NULL, 0));
    TEST_ASSERT_EQUAL_STRING("*RST\n", buf);

    static const char *const ONE[] = {"1.5"};
    TEST_ASSERT_EQUAL_UINT(18u, protocore_scpi_build(buf, sizeof(buf), "SOUR:VOLT:LEV", ONE, 1));
    TEST_ASSERT_EQUAL_STRING("SOUR:VOLT:LEV 1.5\n", buf);

    static const char *const THREE[] = {"1.5", "MAX", "DEF"};
    TEST_ASSERT_TRUE(protocore_scpi_build(buf, sizeof(buf), "APPL", THREE, 3) > 0);
    TEST_ASSERT_EQUAL_STRING("APPL 1.5,MAX,DEF\n", buf);
}

// The builder refuses rather than truncating: a half-written command line is a different command.
void test_build_refuses_bad_arguments(void)
{
    static const char *const ONE[] = {"1.5"};
    static const char *const NULLARG[] = {NULL};
    char buf[64];

    TEST_ASSERT_EQUAL_UINT(5u, protocore_scpi_build(buf, 6, "*RST", NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_scpi_build(buf, 5, "*RST", NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_scpi_build(NULL, 64, "*RST", NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_scpi_build(buf, 64, NULL, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_scpi_build(buf, 64, "", NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_scpi_build(buf, 64, "*RST", NULL, 1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_scpi_build(buf, 64, "SOUR", NULLARG, 1));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_scpi_build(buf, 9, "SOUR:VOLT", ONE, 1));
}

// SCPI-99 Vol 1 sec 7.2.1: a response number may arrive in NR1, NR2 or NR3 form, and all three name
// the same value. Each expectation below is the decimal the digits themselves spell.
void test_numeric_response_forms(void)
{
    static const struct
    {
        const char *s;
        double want;
    } CASES[] = {
        {"0", 0.0},          {"1", 1.0},        {"-1", -1.0},       {"+1", 1.0},
        {"1234567", 1234567.0}, {"1.5", 1.5},   {"-1.5", -1.5},     {"0.001", 0.001},
        {".5", 0.5},         {"1.", 1.0},       {"1E3", 1000.0},    {"1e3", 1000.0},
        {"1.5E3", 1500.0},   {"1.5E+3", 1500.0}, {"1.5E-3", 0.0015}, {"-2.5E2", -250.0},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        double v = 12345.0;
        TEST_ASSERT_TRUE_MESSAGE(protocore_scpi_parse_number(CASES[i].s, strlen(CASES[i].s), &v), CASES[i].s);
        TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(1e-9, CASES[i].want, v, CASES[i].s);
    }
}

// SCPI-99 Vol 1 sec 7.2.1.4: "Positive infinity is represented as 9.9 E 37. Negative infinity is
// -9.9 E 37." Sec 7.2.1.5: "Not a number is represented as 9.91 E 37." Those three published
// constants must parse to their published magnitudes and stay distinguishable from each other.
void test_scpi99_special_numeric_values(void)
{
    double inf = 0.0;
    double ninf = 0.0;
    double nan = 0.0;
    TEST_ASSERT_TRUE(protocore_scpi_parse_number("9.9E37", 6, &inf));
    TEST_ASSERT_TRUE(protocore_scpi_parse_number("-9.9E37", 7, &ninf));
    TEST_ASSERT_TRUE(protocore_scpi_parse_number("9.91E37", 7, &nan));

    TEST_ASSERT_DOUBLE_WITHIN(1e30, 9.9e37, inf);
    TEST_ASSERT_DOUBLE_WITHIN(1e30, -9.9e37, ninf);
    TEST_ASSERT_DOUBLE_WITHIN(1e30, 9.91e37, nan);
    TEST_ASSERT_TRUE(nan > inf); // 9.91E37 is the larger, which is what tells NAN from INFinity
}

// A field that is not wholly a number is refused: a partial parse would silently accept a units
// suffix or a stray character as a reading.
void test_malformed_numbers_are_refused(void)
{
    static const char *const BAD[] = {"", "-", "+", ".", "E3", "1E", "1E+", "abc", "1.2.3", "1 2", "1V", "0x10", "--1"};
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        double v = 0.0;
        TEST_ASSERT_FALSE_MESSAGE(protocore_scpi_parse_number(BAD[i], strlen(BAD[i]), &v), BAD[i]);
    }
    double v = 0.0;
    TEST_ASSERT_FALSE(protocore_scpi_parse_number(NULL, 3, &v));
    TEST_ASSERT_FALSE(protocore_scpi_parse_number("1", 1, NULL));
}

// The formatter and the parser agree: a value formatted as a SCPI parameter parses back to itself.
void test_real_format_round_trips(void)
{
    static const double VALUES[] = {0.0, 1.0, -1.0, 1.5, -0.001, 12345.678, 1e-6, 1e12, -3.25e-9};
    for (size_t i = 0; i < sizeof(VALUES) / sizeof(VALUES[0]); i++)
    {
        char buf[32];
        double back = 0.0;
        size_t n = protocore_scpi_fmt_real(buf, sizeof(buf), VALUES[i]);
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_TRUE_MESSAGE(protocore_scpi_parse_number(buf, n, &back), buf);
        double tol = (VALUES[i] < 0 ? -VALUES[i] : VALUES[i]) * 1e-6;
        TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(tol + 1e-12, VALUES[i], back, buf);
    }
    char tiny[4];
    TEST_ASSERT_EQUAL_UINT(0u, protocore_scpi_fmt_real(NULL, 32, 1.0));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_scpi_fmt_real(tiny, 0, 1.0));
}

// SCPI-99 Vol 1 sec 7.3: a boolean response is 1 or 0, and ON / OFF are their character-data
// equivalents, matched case-insensitively like every other mnemonic.
void test_boolean_responses(void)
{
    proto_bool b = PROTO_FALSE;
    TEST_ASSERT_TRUE(protocore_scpi_parse_bool("1", 1, &b));
    TEST_ASSERT_TRUE(b);
    TEST_ASSERT_TRUE(protocore_scpi_parse_bool("0", 1, &b));
    TEST_ASSERT_FALSE(b);
    TEST_ASSERT_TRUE(protocore_scpi_parse_bool("ON", 2, &b));
    TEST_ASSERT_TRUE(b);
    TEST_ASSERT_TRUE(protocore_scpi_parse_bool("off", 3, &b));
    TEST_ASSERT_FALSE(b);
    TEST_ASSERT_TRUE(protocore_scpi_parse_bool("On", 2, &b));
    TEST_ASSERT_TRUE(b);

    static const char *const BAD[] = {"", "2", "01", "TRUE", "FALSE", "ONN", "O"};
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_FALSE_MESSAGE(protocore_scpi_parse_bool(BAD[i], strlen(BAD[i]), &b), BAD[i]);
    }
    TEST_ASSERT_FALSE(protocore_scpi_parse_bool(NULL, 1, &b));
    TEST_ASSERT_FALSE(protocore_scpi_parse_bool("1", 1, NULL));
}

// IEEE 488.2 sec 7.7.5 / SCPI-99 Vol 1 sec 7.7.1: a string response is quoted, and a quote inside
// it is doubled. Both the single and the double quote forms are legal.
void test_string_responses(void)
{
    char out[64];
    TEST_ASSERT_EQUAL_UINT(5u, protocore_scpi_parse_string("\"HELLO\"", 7, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("HELLO", out);
    TEST_ASSERT_EQUAL_UINT(5u, protocore_scpi_parse_string("'HELLO'", 7, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("HELLO", out);

    // a doubled quote inside collapses to one
    TEST_ASSERT_EQUAL_UINT(3u, protocore_scpi_parse_string("\"A\"\"B\"", 6, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("A\"B", out);

    // the other quote character is ordinary text inside
    TEST_ASSERT_EQUAL_UINT(3u, protocore_scpi_parse_string("\"A'B\"", 5, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("A'B", out);

    TEST_ASSERT_EQUAL_UINT(0u, protocore_scpi_parse_string("\"\"", 2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);

    static const char *const BAD[] = {
        "HELLO",     // unquoted
        "\"HELLO",   // unterminated
        "HELLO\"",   // no opening quote
        "\"HELLO'",  // mismatched quote characters
        "\"",        // one octet
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, protocore_scpi_parse_string(BAD[i], strlen(BAD[i]), out, sizeof(out)),
                                       BAD[i]);
    }
    // a destination too small refuses rather than truncating the reading
    char small[3];
    TEST_ASSERT_EQUAL_UINT(0u, protocore_scpi_parse_string("\"HELLO\"", 7, small, sizeof(small)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_scpi_parse_string(NULL, 7, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_scpi_parse_string("\"A\"", 3, NULL, 4));
}

// IEEE 488.2 sec 8.7.9: an arbitrary block is '#', one non-zero digit giving how many length digits
// follow, those digits, then exactly that many data octets. "#800000010" therefore introduces ten
// octets: 8 length digits spelling 00000010.
void test_ieee4882_definite_length_block(void)
{
    static const uint8_t BLOCK[] = "#210ABCDEFGHIJ";
    const uint8_t *data = NULL;
    size_t dlen = 0;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(protocore_scpi_parse_block(BLOCK, sizeof(BLOCK) - 1, &data, &dlen, &consumed));
    TEST_ASSERT_EQUAL_UINT(10u, dlen);
    TEST_ASSERT_EQUAL_UINT(14u, consumed); // '#' + '2' + "10" + 10 data octets
    TEST_ASSERT_EQUAL_MEMORY("ABCDEFGHIJ", data, 10);

    static const uint8_t WIDE[] = "#800000010ABCDEFGHIJ";
    TEST_ASSERT_TRUE(protocore_scpi_parse_block(WIDE, sizeof(WIDE) - 1, &data, &dlen, &consumed));
    TEST_ASSERT_EQUAL_UINT(10u, dlen);
    TEST_ASSERT_EQUAL_UINT(20u, consumed);
    TEST_ASSERT_EQUAL_MEMORY("ABCDEFGHIJ", data, 10);

    // a block carrying binary octets, which is the whole point of the form
    static const uint8_t BIN[] = {'#', '1', '4', 0x00, 0xFF, '\n', 0x1B};
    TEST_ASSERT_TRUE(protocore_scpi_parse_block(BIN, sizeof(BIN), &data, &dlen, &consumed));
    TEST_ASSERT_EQUAL_UINT(4u, dlen);
    TEST_ASSERT_EQUAL_HEX8(0x00, data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, data[1]);

    // trailing octets past the counted data are the next response, not part of this block
    static const uint8_t TRAIL[] = "#12AB\n";
    TEST_ASSERT_TRUE(protocore_scpi_parse_block(TRAIL, sizeof(TRAIL) - 1, &data, &dlen, &consumed));
    TEST_ASSERT_EQUAL_UINT(2u, dlen);
    TEST_ASSERT_EQUAL_UINT(5u, consumed);
}

// IEEE 488.2 sec 8.7.9: the indefinite form is "#0" followed by the data and terminated by
// NL with END.
void test_ieee4882_indefinite_length_block(void)
{
    static const uint8_t BLOCK[] = "#0ABCDE\n";
    const uint8_t *data = NULL;
    size_t dlen = 0;
    size_t consumed = 0;
    TEST_ASSERT_TRUE(protocore_scpi_parse_block(BLOCK, sizeof(BLOCK) - 1, &data, &dlen, &consumed));
    TEST_ASSERT_EQUAL_UINT(5u, dlen);
    TEST_ASSERT_EQUAL_UINT(8u, consumed);
    TEST_ASSERT_EQUAL_MEMORY("ABCDE", data, 5);

    // without the terminator the block is not yet complete
    TEST_ASSERT_FALSE(protocore_scpi_parse_block(BLOCK, sizeof(BLOCK) - 2, &data, &dlen, &consumed));
}

// A block whose counted data is not fully buffered, or whose header is malformed, is refused: a
// short read must not be handed up as a shorter waveform.
void test_block_refuses_malformed_and_truncated(void)
{
    const uint8_t *data = NULL;
    size_t dlen = 0;
    size_t consumed = 0;

    static const uint8_t GOOD[] = "#210ABCDEFGHIJ";
    for (size_t shorter = 0; shorter < sizeof(GOOD) - 1; shorter++)
    {
        TEST_ASSERT_FALSE(protocore_scpi_parse_block(GOOD, shorter, &data, &dlen, &consumed));
    }

    static const uint8_t *const BAD[] = {
        (const uint8_t *)"210ABCDEFGHIJ", // no '#'
        (const uint8_t *)"#X10AB",        // the width digit is not a digit
        (const uint8_t *)"#2XYABCDEFGHIJ", // the length digits are not digits
    };
    static const size_t BAD_LEN[] = {13, 6, 14};
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        TEST_ASSERT_FALSE(protocore_scpi_parse_block(BAD[i], BAD_LEN[i], &data, &dlen, &consumed));
    }
    TEST_ASSERT_FALSE(protocore_scpi_parse_block(NULL, 4, &data, &dlen, &consumed));
    TEST_ASSERT_FALSE(protocore_scpi_parse_block(GOOD, 14, NULL, &dlen, &consumed));
    TEST_ASSERT_FALSE(protocore_scpi_parse_block(GOOD, 14, &data, NULL, &consumed));
    TEST_ASSERT_FALSE(protocore_scpi_parse_block(GOOD, 14, &data, &dlen, NULL));
}

// IEEE 488.2 sec 11.5.1: the Standard Event Status Register bit positions.
// IEEE 488.2 sec 11.2: the Status Byte bit positions, with the SCPI EAV bit at 2.
void test_ieee4882_register_bit_positions(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01, SCPI_ESR_OPC); // bit 0 Operation Complete
    TEST_ASSERT_EQUAL_HEX8(0x02, SCPI_ESR_RQC); // bit 1 Request Control
    TEST_ASSERT_EQUAL_HEX8(0x04, SCPI_ESR_QYE); // bit 2 Query Error
    TEST_ASSERT_EQUAL_HEX8(0x08, SCPI_ESR_DDE); // bit 3 Device-Dependent Error
    TEST_ASSERT_EQUAL_HEX8(0x10, SCPI_ESR_EXE); // bit 4 Execution Error
    TEST_ASSERT_EQUAL_HEX8(0x20, SCPI_ESR_CME); // bit 5 Command Error
    TEST_ASSERT_EQUAL_HEX8(0x40, SCPI_ESR_URQ); // bit 6 User Request
    TEST_ASSERT_EQUAL_HEX8(0x80, SCPI_ESR_PON); // bit 7 Power On

    TEST_ASSERT_EQUAL_HEX8(0x04, SCPI_STB_EAV); // bit 2 error/event queue not empty
    TEST_ASSERT_EQUAL_HEX8(0x08, SCPI_STB_QSB); // bit 3 QUEStionable summary
    TEST_ASSERT_EQUAL_HEX8(0x10, SCPI_STB_MAV); // bit 4 Message Available
    TEST_ASSERT_EQUAL_HEX8(0x20, SCPI_STB_ESB); // bit 5 Standard Event summary
    TEST_ASSERT_EQUAL_HEX8(0x40, SCPI_STB_MSS); // bit 6 Master Summary Status
    TEST_ASSERT_EQUAL_HEX8(0x80, SCPI_STB_OSB); // bit 7 OPERation summary

    TEST_ASSERT_EQUAL_INT(5025, PROTOCORE_SCPI_PORT);
}

// SCPI-99 Vol 2 sec 21.8.9 through 21.8.12: [-199,-100] sets the command error bit (bit 5),
// [-299,-200] the execution error bit (bit 4), [-399,-300] and [1,32767] the device-specific error
// bit (bit 3), and [-499,-400] the query error bit (bit 2).
void test_scpi99_error_class_sets_its_esr_bit(void)
{
    static const struct
    {
        int16_t number;
        uint8_t bit;
    } CASES[] = {
        {-100, SCPI_ESR_CME}, {-113, SCPI_ESR_CME}, {-199, SCPI_ESR_CME},
        {-200, SCPI_ESR_EXE}, {-222, SCPI_ESR_EXE}, {-299, SCPI_ESR_EXE},
        {-300, SCPI_ESR_DDE}, {-350, SCPI_ESR_DDE}, {-399, SCPI_ESR_DDE},
        {-400, SCPI_ESR_QYE}, {-420, SCPI_ESR_QYE}, {-499, SCPI_ESR_QYE},
        {1, SCPI_ESR_DDE},    {32767, SCPI_ESR_DDE},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        ScpiStatus s;
        protocore_scpi_status_init(&s);
        protocore_scpi_push_error(&s, CASES[i].number, NULL);
        TEST_ASSERT_EQUAL_HEX8(CASES[i].bit, s.esr);
    }

    // sec 21.8: "The value, zero, is also reserved to indicate that no error or event has occurred"
    ScpiStatus s;
    protocore_scpi_status_init(&s);
    protocore_scpi_push_error(&s, 0, "ignored");
    TEST_ASSERT_EQUAL_HEX8(0, s.esr);
    TEST_ASSERT_EQUAL_UINT8(0, s.count);
}

// SCPI-99 Vol 2 sec 21.8.1: the queue "is first in, first out", and "When all errors/events have
// been read from the queue, further error/event queries shall return 0, "No error"".
void test_scpi99_error_queue_is_fifo_and_empties_to_no_error(void)
{
    ScpiStatus s;
    ScpiError e;
    protocore_scpi_status_init(&s);

    TEST_ASSERT_FALSE(protocore_scpi_pop_error(&s, &e));
    TEST_ASSERT_EQUAL_INT16(0, e.number);
    TEST_ASSERT_EQUAL_STRING("No error", e.msg);

    protocore_scpi_push_error(&s, -113, NULL);
    protocore_scpi_push_error(&s, -222, NULL);
    TEST_ASSERT_EQUAL_UINT8(2, s.count);

    TEST_ASSERT_TRUE(protocore_scpi_pop_error(&s, &e));
    TEST_ASSERT_EQUAL_INT16(-113, e.number);
    TEST_ASSERT_TRUE(protocore_scpi_pop_error(&s, &e));
    TEST_ASSERT_EQUAL_INT16(-222, e.number);
    TEST_ASSERT_FALSE(protocore_scpi_pop_error(&s, &e));
    TEST_ASSERT_EQUAL_INT16(0, e.number);
    TEST_ASSERT_EQUAL_STRING("No error", e.msg);
}

// SCPI-99 Vol 2 sec 21.8.1: "If the queue overflows, the last error/event in the queue is replaced
// with error -350,"Queue overflow" ... the least recent errors/events remain in the queue, and the
// most recent error/event is discarded."
void test_scpi99_queue_overflow_rule(void)
{
    ScpiStatus s;
    ScpiError e;
    protocore_scpi_status_init(&s);

    for (int i = 0; i < PROTOCORE_SCPI_ERR_QUEUE; i++)
    {
        protocore_scpi_push_error(&s, (int16_t)(-100 - i), NULL);
    }
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_SCPI_ERR_QUEUE, s.count);

    protocore_scpi_push_error(&s, -222, NULL); // one past the end
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_SCPI_ERR_QUEUE, s.count);

    // the least recent entries survive in order
    for (int i = 0; i < PROTOCORE_SCPI_ERR_QUEUE - 1; i++)
    {
        TEST_ASSERT_TRUE(protocore_scpi_pop_error(&s, &e));
        TEST_ASSERT_EQUAL_INT16((int16_t)(-100 - i), e.number);
    }
    // and the last one has become the overflow marker, the discarded -222 nowhere in the queue
    TEST_ASSERT_TRUE(protocore_scpi_pop_error(&s, &e));
    TEST_ASSERT_EQUAL_INT16(-350, e.number);
    TEST_ASSERT_EQUAL_STRING("Queue overflow", e.msg);
    TEST_ASSERT_FALSE(protocore_scpi_pop_error(&s, &e));
}

// SCPI-99 Vol 2 sec 21.8: "For standard defined error/event codes, the <Error/event_description>
// shall be sent exactly as indicated in this document including case."
void test_scpi99_standard_error_messages(void)
{
    TEST_ASSERT_EQUAL_STRING("No error", protocore_scpi_std_error(0));
    TEST_ASSERT_EQUAL_STRING("Command error", protocore_scpi_std_error(-100));
    TEST_ASSERT_EQUAL_STRING("Syntax error", protocore_scpi_std_error(-102));
    TEST_ASSERT_EQUAL_STRING("Undefined header", protocore_scpi_std_error(-113));
    TEST_ASSERT_EQUAL_STRING("Execution error", protocore_scpi_std_error(-200));
    TEST_ASSERT_EQUAL_STRING("Data out of range", protocore_scpi_std_error(-222));
    TEST_ASSERT_EQUAL_STRING("Device-specific error", protocore_scpi_std_error(-300));
    TEST_ASSERT_EQUAL_STRING("Queue overflow", protocore_scpi_std_error(-350));
    TEST_ASSERT_EQUAL_STRING("Query error", protocore_scpi_std_error(-400));
    TEST_ASSERT_EQUAL_STRING("Query UNTERMINATED", protocore_scpi_std_error(-420));

    // a device-specific (positive) number has no standard text
    TEST_ASSERT_EQUAL_STRING("", protocore_scpi_std_error(42));
    // and an unassigned number falls through to the same empty string, never to another's text
    TEST_ASSERT_EQUAL_STRING("", protocore_scpi_std_error(-101 - 1000));
}

// IEEE 488.2 sec 11.2: EAV reports a non-empty queue, ESB is the ESR masked by the ESE, and MSS is
// the OR of the remaining STB bits masked by the SRE. Bit 6 cannot summarize itself.
void test_status_byte_summary_bits(void)
{
    ScpiStatus s;
    ScpiError e;
    protocore_scpi_status_init(&s);
    TEST_ASSERT_EQUAL_HEX8(0, protocore_scpi_stb(&s));

    // a queued error raises EAV, and popping it lowers it again
    protocore_scpi_push_error(&s, -113, NULL);
    TEST_ASSERT_EQUAL_HEX8(SCPI_STB_EAV, protocore_scpi_stb(&s));
    protocore_scpi_pop_error(&s, &e);
    TEST_ASSERT_EQUAL_HEX8(0, protocore_scpi_stb(&s));

    // an ESR event with the matching ESE bit clear does not summarize
    protocore_scpi_status_init(&s);
    protocore_scpi_event(&s, SCPI_ESR_OPC);
    TEST_ASSERT_EQUAL_HEX8(0, protocore_scpi_stb(&s));
    s.ese = SCPI_ESR_OPC;
    TEST_ASSERT_EQUAL_HEX8(SCPI_STB_ESB, protocore_scpi_stb(&s));

    // with the SRE bit for ESB set, MSS follows
    s.sre = SCPI_STB_ESB;
    TEST_ASSERT_EQUAL_HEX8(SCPI_STB_ESB | SCPI_STB_MSS, protocore_scpi_stb(&s));

    // SRE bit 6 alone cannot raise MSS: the bit does not summarize itself
    protocore_scpi_status_init(&s);
    s.sre = SCPI_STB_MSS;
    s.summary = SCPI_STB_MAV;
    TEST_ASSERT_EQUAL_HEX8(SCPI_STB_MAV, protocore_scpi_stb(&s));

    // the app-set summary bits pass through, and nothing outside QSB/MAV/OSB does
    protocore_scpi_status_init(&s);
    s.summary = 0xFF;
    TEST_ASSERT_EQUAL_HEX8(SCPI_STB_QSB | SCPI_STB_MAV | SCPI_STB_OSB, protocore_scpi_stb(&s));

    TEST_ASSERT_EQUAL_HEX8(0, protocore_scpi_stb(NULL));
}

// IEEE 488.2 sec 11.4.3.4 / SCPI-99 Vol 2 sec 21.8.1: *CLS clears the ESR and empties the queue,
// and leaves the enable masks alone.
void test_cls_clears_events_not_enables(void)
{
    ScpiStatus s;
    ScpiError e;
    protocore_scpi_status_init(&s);
    s.ese = 0xFF;
    s.sre = 0xFF;
    protocore_scpi_event(&s, SCPI_ESR_PON);
    protocore_scpi_push_error(&s, -113, NULL);

    protocore_scpi_cls(&s);
    TEST_ASSERT_EQUAL_HEX8(0, s.esr);
    TEST_ASSERT_EQUAL_UINT8(0, s.count);
    TEST_ASSERT_EQUAL_HEX8(0xFF, s.ese);
    TEST_ASSERT_EQUAL_HEX8(0xFF, s.sre);
    TEST_ASSERT_FALSE(protocore_scpi_pop_error(&s, &e));

    // status_init is the power-on state: everything clear
    protocore_scpi_status_init(&s);
    TEST_ASSERT_EQUAL_HEX8(0, s.esr);
    TEST_ASSERT_EQUAL_HEX8(0, s.ese);
    TEST_ASSERT_EQUAL_HEX8(0, s.sre);
    TEST_ASSERT_EQUAL_UINT8(0, s.count);
}

// A caller-supplied message overrides the standard text, and null arguments are refused rather than
// written through.
void test_status_calls_tolerate_null(void)
{
    ScpiStatus s;
    ScpiError e;
    protocore_scpi_status_init(&s);
    protocore_scpi_push_error(&s, -113, "my own text");
    TEST_ASSERT_TRUE(protocore_scpi_pop_error(&s, &e));
    TEST_ASSERT_EQUAL_STRING("my own text", e.msg);

    protocore_scpi_status_init(NULL);
    protocore_scpi_event(NULL, SCPI_ESR_OPC);
    protocore_scpi_push_error(NULL, -100, NULL);
    protocore_scpi_cls(NULL);
    TEST_ASSERT_FALSE(protocore_scpi_pop_error(NULL, &e));
    TEST_ASSERT_EQUAL_INT16(0, e.number);
    TEST_ASSERT_FALSE(protocore_scpi_pop_error(&s, NULL));

    TEST_ASSERT_FALSE(protocore_scpi_match(NULL, 4, "*IDN?"));
    TEST_ASSERT_FALSE(protocore_scpi_match("*IDN?", 5, NULL));
}
