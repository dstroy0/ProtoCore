// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the declarative frame builder (mmgr/protoframe.h).
//
// No standard governs the spec table itself, so it is pinned two ways. The field renderings are
// the printf conversions ISO C11 sec 7.21.6.1 defines - %0Nu, %0Nx, %0No, %.Ng, %.Nf - with each
// expected string derived from that clause rather than captured from a libc run. Everything else
// is the module's own stated contract, tested as a property.
//
// test_a_frame_that_does_not_fit_writes_an_empty_string is the load-bearing case. This engine is
// the single place ~160 call sites turn values into wire bytes, and its contract is that a frame is
// emitted whole or not at all: on overflow it returns 0 AND leaves out[0] = '\0'. A caller that
// ignores the return then sends nothing instead of half a header line, which is the difference
// between a dropped response and a desynchronized peer.

#include "mmgr/protoframe/protoframe.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}

void tearDown(void)
{
}

// The HTTP response header frame, declared the way a call site declares one.
static const protocore_field RESP[] = {{PROTOCORE_FK_LIT, 0, 9, "HTTP/1.1 "},
                                       PROTOCORE_U32,
                                       {PROTOCORE_FK_LIT, 0, 1, " "},
                                       PROTOCORE_STR,
                                       {PROTOCORE_FK_LIT, 0, 16, "\r\nContent-Type: "},
                                       PROTOCORE_STR,
                                       {PROTOCORE_FK_LIT, 0, 18, "\r\nContent-Length: "},
                                       PROTOCORE_U32,
                                       {PROTOCORE_FK_LIT, 0, 2, "\r\n"},
                                       PROTOCORE_END};

// A header line, for the append tests.
static const protocore_field HDR[] = {
    PROTOCORE_STR, {PROTOCORE_FK_LIT, 0, 2, ": "}, PROTOCORE_STR, {PROTOCORE_FK_LIT, 0, 2, "\r\n"}, PROTOCORE_END};

// ---- the literal-and-value walk --------------------------------------------

// Literals and values alternate in spec order; the return value is the byte count excluding the NUL.
void test_a_frame_interleaves_its_literals_and_values(void)
{
    static const char WANT[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 21\r\n";
    char out[128];
    size_t n = frame.build(out, sizeof(out), RESP,
                           (const protocore_fval[]){PROTOCORE_VU32(200u), PROTOCORE_VSTR("OK"),
                                                    PROTOCORE_VSTR("text/plain"), PROTOCORE_VU32(21u)},
                           4);
    TEST_ASSERT_EQUAL_STRING(WANT, out);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT) - 1u, n);
}

// A spec of literals alone takes no arguments at all.
void test_a_literal_only_frame_takes_no_arguments(void)
{
    static const protocore_field L[] = {{PROTOCORE_FK_LIT, 0, 12, "no args here"}, PROTOCORE_END};
    char out[32];
    TEST_ASSERT_EQUAL_size_t(12, frame.build(out, sizeof(out), L, NULL, 0));
    TEST_ASSERT_EQUAL_STRING("no args here", out);
}

// An empty spec writes nothing and still leaves a valid C string.
void test_an_empty_spec_yields_an_empty_string(void)
{
    static const protocore_field E[] = {PROTOCORE_END};
    char out[8];
    out[0] = 'x';
    TEST_ASSERT_EQUAL_size_t(0, frame.build(out, sizeof(out), E, NULL, 0));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// "A NULL PROTOCORE_FK_STR argument renders as empty, never as a crash or (null)."
void test_a_null_string_argument_renders_as_empty(void)
{
    static const protocore_field S[] = {
        {PROTOCORE_FK_LIT, 0, 1, "["}, PROTOCORE_STR, {PROTOCORE_FK_LIT, 0, 1, "]"}, PROTOCORE_END};
    char out[32];
    TEST_ASSERT_EQUAL_size_t(2, frame.build(out, sizeof(out), S, (const protocore_fval[]){PROTOCORE_VSTR(NULL)}, 1));
    TEST_ASSERT_EQUAL_STRING("[]", out);
}

// ---- the field kinds -------------------------------------------------------

// Every valued kind, once, at a stated width. Each rendering is the C11 sec 7.21.6.1 conversion the
// kind names:
//   HEX width 8  = %08x  -> 0xbeef            -> "0000beef"
//   DEC width 3  = %03u  -> 7                 -> "007"
//   I64          = %lld  -> -5                -> "-5"
//   U64          = %llu  -> 12                -> "12"
//   G width 6    = %.6g  -> X = 0, so style f precision 5 -> "3.14159"
//   FIX width 2  = %.2f  -> 2.5 is exact      -> "2.50"
//   CH           = %c    -> 'x'
//   JSON                 -> RFC 8259 sec 7: the quotation mark is escaped -> "\"a\\\"b\""
//   XML                  -> XML 1.0 sec 4.6 predefined entity            -> "a&lt;b"
//   OCT width 4  = %04o  -> 8                 -> "0010"
void test_every_field_kind_renders_its_conversion(void)
{
    static const protocore_field ALL[] = {{PROTOCORE_FK_HEX, 8, 0, NULL},
                                          {PROTOCORE_FK_LIT, 0, 1, "|"},
                                          {PROTOCORE_FK_DEC, 3, 0, NULL},
                                          {PROTOCORE_FK_LIT, 0, 1, "|"},
                                          PROTOCORE_I64,
                                          {PROTOCORE_FK_LIT, 0, 1, "|"},
                                          PROTOCORE_U64,
                                          {PROTOCORE_FK_LIT, 0, 1, "|"},
                                          {PROTOCORE_FK_G, 6, 0, NULL},
                                          {PROTOCORE_FK_LIT, 0, 1, "|"},
                                          {PROTOCORE_FK_FIX, 2, 0, NULL},
                                          {PROTOCORE_FK_LIT, 0, 1, "|"},
                                          PROTOCORE_CH,
                                          {PROTOCORE_FK_LIT, 0, 1, "|"},
                                          PROTOCORE_JSON,
                                          {PROTOCORE_FK_LIT, 0, 1, "|"},
                                          PROTOCORE_XML,
                                          {PROTOCORE_FK_LIT, 0, 1, "|"},
                                          {PROTOCORE_FK_OCT, 4, 0, NULL},
                                          PROTOCORE_END};
    char out[160];
    frame.build(out, sizeof(out), ALL,
                (const protocore_fval[]){PROTOCORE_VHEX(0xbeefu), PROTOCORE_VDEC(7u), PROTOCORE_VI64(-5),
                                         PROTOCORE_VU64(12u), PROTOCORE_VG(3.14159265), PROTOCORE_VFIX(2.5),
                                         PROTOCORE_VCH('x'), PROTOCORE_VJSON("a\"b"), PROTOCORE_VXML("a<b"),
                                         PROTOCORE_VOCT(8u)},
                10);
    TEST_ASSERT_EQUAL_STRING("0000beef|007|-5|12|3.14159|2.50|x|\"a\\\"b\"|a&lt;b|0010", out);
}

// A width of 0 means no padding, not zero digits, and a value wider than the width keeps every
// digit: C11 sec 7.21.6.1 pads to the field width and never truncates the conversion.
void test_a_width_pads_but_never_truncates(void)
{
    static const protocore_field W[] = {{PROTOCORE_FK_HEX, 0, 0, NULL}, {PROTOCORE_FK_LIT, 0, 1, ","},
                                        {PROTOCORE_FK_HEX, 4, 0, NULL}, {PROTOCORE_FK_LIT, 0, 1, ","},
                                        {PROTOCORE_FK_DEC, 5, 0, NULL}, {PROTOCORE_FK_LIT, 0, 1, ","},
                                        {PROTOCORE_FK_OCT, 0, 0, NULL}, {PROTOCORE_FK_LIT, 0, 1, ","},
                                        {PROTOCORE_FK_DEC, 2, 0, NULL}, PROTOCORE_END};
    char out[64];
    frame.build(out, sizeof(out), W,
                (const protocore_fval[]){PROTOCORE_VHEX(0xabu), PROTOCORE_VHEX(0xabu), PROTOCORE_VDEC(42u),
                                         PROTOCORE_VOCT(64u), PROTOCORE_VDEC(123456u)},
                5);
    TEST_ASSERT_EQUAL_STRING("ab,00ab,00042,100,123456", out);
}

// The %g and %f field kinds, at the two thresholds C11 sec 7.21.6.1 names for the style choice.
//
//   G width 6, X =  6 (1e6)     : 6 > 6 false -> style e -> "1e+06"
//   G width 6, X =  5 (123456)  : 6 > 5 >= -4 -> style f precision 0 -> "123456"
//   G width 6, X = -5 (1e-5)    : -5 >= -4 false -> style e -> "1e-05"
//   G width 10, X = 0 (pi)      : style f precision 9 -> "3.141592654"
//   FIX width 2, 2.675          : the stored double is 2.6749999... -> "2.67"
void test_the_float_kinds_follow_the_printf_style_rule(void)
{
    static const protocore_field G6[] = {{PROTOCORE_FK_G, 6, 0, NULL}, PROTOCORE_END};
    static const protocore_field G10[] = {{PROTOCORE_FK_G, 10, 0, NULL}, PROTOCORE_END};
    static const protocore_field F2[] = {{PROTOCORE_FK_FIX, 2, 0, NULL}, PROTOCORE_END};
    char out[64];

    frame.build(out, sizeof(out), G6, (const protocore_fval[]){PROTOCORE_VG(1e6)}, 1);
    TEST_ASSERT_EQUAL_STRING("1e+06", out);
    frame.build(out, sizeof(out), G6, (const protocore_fval[]){PROTOCORE_VG(123456.0)}, 1);
    TEST_ASSERT_EQUAL_STRING("123456", out);
    frame.build(out, sizeof(out), G6, (const protocore_fval[]){PROTOCORE_VG(1e-5)}, 1);
    TEST_ASSERT_EQUAL_STRING("1e-05", out);
    frame.build(out, sizeof(out), G6, (const protocore_fval[]){PROTOCORE_VG(-0.0)}, 1);
    TEST_ASSERT_EQUAL_STRING("-0", out);

    frame.build(out, sizeof(out), G10, (const protocore_fval[]){PROTOCORE_VG(3.14159265358979)}, 1);
    TEST_ASSERT_EQUAL_STRING("3.141592654", out);

    frame.build(out, sizeof(out), F2, (const protocore_fval[]){PROTOCORE_VFIX(2.675)}, 1);
    TEST_ASSERT_EQUAL_STRING("2.67", out);
    frame.build(out, sizeof(out), F2, (const protocore_fval[]){PROTOCORE_VFIX(0.0)}, 1);
    TEST_ASSERT_EQUAL_STRING("0.00", out);
}

// Past 2^64 the integer part cannot go through a uint64, and the documented fallback is the
// significant-digit form. Pinned so the wrapped cast that rendered 1e20 as "0.00" cannot return.
void test_a_fixed_field_above_the_64_bit_range_falls_back(void)
{
    static const protocore_field F2[] = {{PROTOCORE_FK_FIX, 2, 0, NULL}, PROTOCORE_END};
    char out[64];
    frame.build(out, sizeof(out), F2, (const protocore_fval[]){PROTOCORE_VFIX(1e20)}, 1);
    TEST_ASSERT_EQUAL_STRING("1e+20", out);
    frame.build(out, sizeof(out), F2, (const protocore_fval[]){PROTOCORE_VFIX(1e300)}, 1);
    TEST_ASSERT_EQUAL_STRING("1e+300", out);
}

// ---- the fail-closed contract ----------------------------------------------

// "Returns 0 if the frame does not fit, and writes out[0] = '\0'. There is no truncation."
void test_a_frame_that_does_not_fit_writes_an_empty_string(void)
{
    char tiny[8];
    memset(tiny, 'Z', sizeof(tiny));
    TEST_ASSERT_EQUAL_size_t(0, frame.build(tiny, sizeof(tiny), RESP,
                                            (const protocore_fval[]){PROTOCORE_VU32(200u), PROTOCORE_VSTR("OK"),
                                                                     PROTOCORE_VSTR("text/plain"), PROTOCORE_VU32(21u)},
                                            4));
    TEST_ASSERT_EQUAL_STRING("", tiny);
}

// The boundary is exact: n bytes plus the NUL fit a capacity of n + 1, and one byte less refuses.
void test_the_capacity_boundary_is_exact(void)
{
    static const protocore_field F[] = {{PROTOCORE_FK_LIT, 0, 4, "abcd"}, PROTOCORE_END};
    char five[5];
    TEST_ASSERT_EQUAL_size_t(4, frame.build(five, sizeof(five), F, NULL, 0));
    TEST_ASSERT_EQUAL_STRING("abcd", five);

    char four[4];
    TEST_ASSERT_EQUAL_size_t(0, frame.build(four, sizeof(four), F, NULL, 0));
    TEST_ASSERT_EQUAL_STRING("", four);
}

// A null destination and a null spec are refusals, not writes through a null.
void test_null_arguments_are_refused(void)
{
    char out[8];
    TEST_ASSERT_EQUAL_size_t(0, frame.build(NULL, 8, RESP,
                                            (const protocore_fval[]){PROTOCORE_VU32(1u), PROTOCORE_VSTR(""),
                                                                     PROTOCORE_VSTR(""), PROTOCORE_VU32(0u)},
                                            4));
    TEST_ASSERT_EQUAL_size_t(0, frame.build(out, sizeof(out), NULL, NULL, 0));
}

// A zero-capacity buffer owns no bytes, so not even the terminator may be written into it.
void test_a_zero_capacity_buffer_is_never_written(void)
{
    char sentinel[2] = {0x7F, 0x7F};
    TEST_ASSERT_EQUAL_size_t(0, frame.build(sentinel, 0, RESP,
                                            (const protocore_fval[]){PROTOCORE_VU32(200u), PROTOCORE_VSTR("OK"),
                                                                     PROTOCORE_VSTR("t"), PROTOCORE_VU32(0u)},
                                            4));
    TEST_ASSERT_EQUAL_CHAR(0x7F, sentinel[0]);

    static const protocore_field L[] = {{PROTOCORE_FK_LIT, 0, 1, "x"}, PROTOCORE_END};
    TEST_ASSERT_EQUAL_size_t(0, frame.build(sentinel, 0, L, NULL, 0));
    TEST_ASSERT_EQUAL_CHAR(0x7F, sentinel[0]);
}

// ---- the argument check ----------------------------------------------------

// "Each value carries the protocore_fk it was written as, so the engine compares it against the
// spec's and refuses a frame whose values do not match." A string where the spec wants a number
// produces nothing rather than a frame built from the wrong union member.
void test_a_value_whose_kind_disagrees_with_the_spec_is_refused(void)
{
    static const protocore_field N[] = {PROTOCORE_U32, PROTOCORE_END};
    char out[32];
    out[0] = 'x';
    TEST_ASSERT_EQUAL_size_t(0, frame.build(out, sizeof(out), N, (const protocore_fval[]){PROTOCORE_VSTR("7")}, 1));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// Arity is checked in both directions, so a miscount is a refusal rather than a read past the end
// of the value array or a silently dropped field.
void test_the_argument_count_must_match_the_spec(void)
{
    static const protocore_field TWO[] = {PROTOCORE_U32, {PROTOCORE_FK_LIT, 0, 1, "-"}, PROTOCORE_U32, PROTOCORE_END};
    char out[32];

    TEST_ASSERT_EQUAL_size_t(0, frame.build(out, sizeof(out), TWO, (const protocore_fval[]){PROTOCORE_VU32(1u)}, 1));
    TEST_ASSERT_EQUAL_STRING("", out);

    TEST_ASSERT_EQUAL_size_t(
        0, frame.build(out, sizeof(out), TWO,
                       (const protocore_fval[]){PROTOCORE_VU32(1u), PROTOCORE_VU32(2u), PROTOCORE_VU32(3u)}, 3));
    TEST_ASSERT_EQUAL_STRING("", out);

    TEST_ASSERT_EQUAL_size_t(
        3, frame.build(out, sizeof(out), TWO, (const protocore_fval[]){PROTOCORE_VU32(1u), PROTOCORE_VU32(2u)}, 2));
    TEST_ASSERT_EQUAL_STRING("1-2", out);
}

// An opcode this engine does not know is a refusal: a spec built against a newer engine must not
// silently emit a frame that is missing a field.
void test_an_unknown_opcode_is_refused(void)
{
    static const protocore_field BAD[] = {{PROTOCORE_FK_LIT, 0, 1, "a"}, {200, 0, 0, NULL}, PROTOCORE_END};
    char out[32];
    TEST_ASSERT_EQUAL_size_t(0, frame.build(out, sizeof(out), BAD, (const protocore_fval[]){{200, {.u32 = 0}}}, 1));
    TEST_ASSERT_EQUAL_STRING("", out);
}

// ---- the append form -------------------------------------------------------

// append adds to what the buffer already holds and returns the new total length.
void test_append_accumulates_onto_the_existing_contents(void)
{
    char acc[64];
    acc[0] = '\0';
    TEST_ASSERT_EQUAL_size_t(8, frame.append(acc, sizeof(acc), HDR,
                                             (const protocore_fval[]){PROTOCORE_VSTR("X-A"), PROTOCORE_VSTR("1")}, 2));
    TEST_ASSERT_EQUAL_size_t(16, frame.append(acc, sizeof(acc), HDR,
                                              (const protocore_fval[]){PROTOCORE_VSTR("X-B"), PROTOCORE_VSTR("2")}, 2));
    TEST_ASSERT_EQUAL_STRING("X-A: 1\r\nX-B: 2\r\n", acc);
}

// "on overflow the buffer is rewound to its previous length, so a frame is added whole or not at
// all" - a half-written header line is a protocol violation, not a truncation.
void test_append_rewinds_the_whole_frame_on_overflow(void)
{
    char small[12];
    small[0] = '\0';
    frame.append(small, sizeof(small), HDR, (const protocore_fval[]){PROTOCORE_VSTR("X-A"), PROTOCORE_VSTR("1")}, 2);
    TEST_ASSERT_EQUAL_size_t(
        0, frame.append(small, sizeof(small), HDR,
                        (const protocore_fval[]){PROTOCORE_VSTR("X-VeryLong"), PROTOCORE_VSTR("2")}, 2));
    TEST_ASSERT_EQUAL_STRING("X-A: 1\r\n", small);
}

// A buffer with no room left is untouched.
void test_append_to_a_full_buffer_changes_nothing(void)
{
    char full[8];
    memset(full, 'a', sizeof(full) - 1u);
    full[sizeof(full) - 1u] = '\0';
    TEST_ASSERT_EQUAL_size_t(0, frame.append(full, sizeof(full), HDR,
                                             (const protocore_fval[]){PROTOCORE_VSTR("X"), PROTOCORE_VSTR("1")}, 2));
    TEST_ASSERT_EQUAL_STRING("aaaaaaa", full);
}
