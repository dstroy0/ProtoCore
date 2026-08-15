// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the panic decoder (server/core/exc_decoder.h).
//
// Two published formats govern this module. The input is the ESP-IDF panic dump, whose exact shape
// is printed in the Espressif "Fatal Errors" API guide: the "Guru Meditation Error: Core N
// panic'ed (Cause)." line, the register dump naming PC and EXCVADDR, and the "Backtrace:" line of
// pc:sp pairs. The output is JSON, so RFC 8259 sec 4 fixes the object form and sec 7 fixes which
// characters inside a string MUST be escaped.
//
// test_espressif_published_panic is the load-bearing case: the input is that guide's own example
// dump, character for character, so the fields this extracts are the fields a real part prints
// rather than the ones a hand-written sample happened to contain.

#include "server/core/exc_decoder.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The ESP-IDF "Fatal Errors" guide's own IllegalInstruction example, reproduced verbatim.
static const char *const PANIC =
    "Guru Meditation Error: Core 0 panic'ed (IllegalInstruction). Exception was unhandled.\n"
    "\n"
    "Core 0 register dump:\n"
    "PC      : 0x400e14ed  PS      : 0x00060030  A0      : 0x800d0805  A1      : 0x3ffb5030\n"
    "A2      : 0x00000000  A3      : 0x00000001  A4      : 0x00000001  A5      : 0x3ffb50dc\n"
    "A6      : 0x00000000  A7      : 0x00000001  A8      : 0x00000000  A9      : 0x3ffb5000\n"
    "A10     : 0x00000000  A11     : 0x3ffb2bac  A12     : 0x40082d1c  A13     : 0x06ff1ff8\n"
    "A14     : 0x3ffb7078  A15     : 0x00000000  SAR     : 0x00000014  EXCCAUSE: 0x0000001d\n"
    "EXCVADDR: 0x00000000  LBEG    : 0x4000c46c  LEND    : 0x4000c477  LCOUNT  : 0xffffffff\n"
    "\n"
    "Backtrace: 0x400e14ed:0x3ffb5030 0x400d0802:0x3ffb5050\n";

static ExcInfo g_info;
static char g_json[1024];

static proto_bool decode(const char *text)
{
    Exc.parse_args.text = text;
    Exc.parse_args.info = &g_info;
    Exc.parse(Exc.internal);
    return Exc.ok;
}

static const char *serialize(size_t cap)
{
    Exc.parse_args.info = &g_info;
    Exc.out_args.out = g_json;
    Exc.out_args.cap = cap;
    Exc.json(Exc.internal);
    return g_json;
}

void test_espressif_published_panic(void)
{
    TEST_ASSERT_TRUE(decode(PANIC));
    TEST_ASSERT_EQUAL_INT(0, g_info.core);
    TEST_ASSERT_EQUAL_STRING("IllegalInstruction", g_info.cause);
    TEST_ASSERT_EQUAL_HEX32(0x400e14edu, g_info.pc);
    TEST_ASSERT_TRUE(g_info.has_excvaddr); // the field is present, and its value is zero
    TEST_ASSERT_EQUAL_HEX32(0u, g_info.excvaddr);
    TEST_ASSERT_EQUAL_size_t(2u, g_info.frame_count);
    TEST_ASSERT_EQUAL_HEX32(0x400e14edu, g_info.frames[0].pc);
    TEST_ASSERT_EQUAL_HEX32(0x3ffb5030u, g_info.frames[0].sp);
    TEST_ASSERT_EQUAL_HEX32(0x400d0802u, g_info.frames[1].pc);
    TEST_ASSERT_EQUAL_HEX32(0x3ffb5050u, g_info.frames[1].sp);
}

// RFC 8259 sec 4: an object is members separated by ',', each a string name, ':', then a value.
// Every address is a string, so a 32-bit value above 2^31 needs no JSON number discussion at the
// panel that reads it, and each is written to a fixed eight hex digits.
void test_json_of_the_published_panic(void)
{
    TEST_ASSERT_TRUE(decode(PANIC));
    const char *s = serialize(sizeof(g_json));
    TEST_ASSERT_EQUAL_STRING("{\"core\":0,\"cause\":\"IllegalInstruction\",\"pc\":\"0x400e14ed\","
                             "\"excvaddr\":\"0x00000000\","
                             "\"backtrace\":[\"0x400e14ed\",\"0x400d0802\"]}",
                             s);
    TEST_ASSERT_EQUAL_size_t(strlen(s), Exc.n);
}

// A dump with no register section still names the faulting address: the outermost backtrace frame
// is the PC the panic was taken at, which is the first pair the guide prints.
void test_pc_falls_back_to_the_first_frame(void)
{
    TEST_ASSERT_TRUE(decode("Backtrace: 0x400e1111:0x3ffc0000 0x400e2222:0x3ffc0020 |<-CORRUPTED\n"));
    TEST_ASSERT_EQUAL_INT(-1, g_info.core);
    TEST_ASSERT_EQUAL_STRING("", g_info.cause);
    TEST_ASSERT_FALSE(g_info.has_excvaddr);
    TEST_ASSERT_EQUAL_size_t(2u, g_info.frame_count);
    TEST_ASSERT_EQUAL_HEX32(0x400e1111u, g_info.pc);
}

// A field that was not in the dump is not invented in the report: no core number and no faulting
// data address means neither member appears.
void test_absent_fields_are_omitted_from_the_report(void)
{
    TEST_ASSERT_TRUE(decode("Backtrace: 0x400e1111:0x3ffc0000\n"));
    const char *s = serialize(sizeof(g_json));
    TEST_ASSERT_EQUAL_STRING("{\"cause\":\"\",\"pc\":\"0x400e1111\",\"backtrace\":[\"0x400e1111\"]}", s);
}

// A dual-core part names the core that faulted, and the decimal emitter carries more than one digit.
void test_core_number_is_decimal(void)
{
    TEST_ASSERT_TRUE(decode("Guru Meditation Error: Core 1 panic'ed (LoadProhibited).\n"));
    TEST_ASSERT_EQUAL_INT(1, g_info.core);
    TEST_ASSERT_EQUAL_STRING("{\"core\":1,\"cause\":\"LoadProhibited\",\"pc\":\"0x00000000\",\"backtrace\":[]}",
                             serialize(sizeof(g_json)));

    TEST_ASSERT_TRUE(decode("Guru Meditation Error: Core 12 panic'ed (LoadProhibited).\n"));
    TEST_ASSERT_EQUAL_INT(12, g_info.core);
}

// The register values are C hex literals, so both spellings of the marker are read, a run stops at
// the first character that is not a hex digit, and 32 bits is where the value ends.
void test_hex_literals(void)
{
    TEST_ASSERT_TRUE(decode("PC      : 0X400dbeef\n")); // capital marker
    TEST_ASSERT_EQUAL_HEX32(0x400dbeefu, g_info.pc);

    TEST_ASSERT_TRUE(decode("PC      : 0x1z\n")); // 'z' is not a hex digit
    TEST_ASSERT_EQUAL_HEX32(0x1u, g_info.pc);

    TEST_ASSERT_TRUE(decode("PC      : 0x123456789\n")); // a ninth nibble is past 32 bits
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, g_info.pc);

    TEST_ASSERT_FALSE(decode("PC      : 0x\n"));    // the marker with no digits is not a literal
    TEST_ASSERT_FALSE(decode("PC      : 01234\n")); // decimal-looking, no marker
    TEST_ASSERT_FALSE(decode("PC      : zz\n"));
    TEST_ASSERT_FALSE(decode("PC 400d1234\n")); // no colon, so no value to read
    TEST_ASSERT_EQUAL_HEX32(0u, g_info.pc);
}

// A backtrace entry is a pc and its stack pointer joined by ':'. Anything that is not that pair
// ends the list rather than being read as a frame.
void test_a_frame_is_a_pc_colon_sp_pair(void)
{
    TEST_ASSERT_FALSE(decode("Backtrace: 0x400d1234 0x3ffb2200\n")); // a space where the ':' belongs
    TEST_ASSERT_EQUAL_size_t(0u, g_info.frame_count);

    TEST_ASSERT_TRUE(decode("Core 0 panic'ed (BadSP). Backtrace: 0x400d1000:0xZZ\n"));
    TEST_ASSERT_EQUAL_size_t(0u, g_info.frame_count);

    // the guide's corruption marker is not a pair, so the frames ahead of it are kept and it is not
    TEST_ASSERT_TRUE(decode("Backtrace: 0x400d1000:0x3ffb0000 |<-CORRUPTED\n"));
    TEST_ASSERT_EQUAL_size_t(1u, g_info.frame_count);
}

// Append " 0x400dXXXX:0x3ffbXXXX" for frame @p i, so the cap can be driven past without stdio.
static size_t put_frame(char *dst, size_t at, unsigned i)
{
    static const char HEX[] = "0123456789abcdef";
    const char *lead = " 0x400d";
    for (size_t k = 0; lead[k]; k++)
    {
        dst[at++] = lead[k];
    }
    dst[at++] = HEX[(i >> 12) & 0xF];
    dst[at++] = HEX[(i >> 8) & 0xF];
    dst[at++] = HEX[(i >> 4) & 0xF];
    dst[at++] = HEX[i & 0xF];
    const char *mid = ":0x3ffb";
    for (size_t k = 0; mid[k]; k++)
    {
        dst[at++] = mid[k];
    }
    dst[at++] = HEX[(i >> 12) & 0xF];
    dst[at++] = HEX[(i >> 8) & 0xF];
    dst[at++] = HEX[(i >> 4) & 0xF];
    dst[at++] = HEX[i & 0xF];
    dst[at] = '\0';
    return at;
}

// A backtrace longer than the retained depth stops at that depth instead of writing past the array.
void test_the_frame_list_stops_at_its_capacity(void)
{
    char text[2048];
    memcpy(text, "Backtrace:", 11);
    size_t at = 10;
    for (unsigned i = 0; i < PROTOCORE_EXC_MAX_FRAMES + 3u; i++)
    {
        at = put_frame(text, at, i);
    }
    TEST_ASSERT_TRUE(decode(text));
    TEST_ASSERT_EQUAL_size_t((size_t)PROTOCORE_EXC_MAX_FRAMES, g_info.frame_count);
    TEST_ASSERT_EQUAL_HEX32(0x400d0000u, g_info.frames[0].pc);
    TEST_ASSERT_EQUAL_HEX32(0x400d0000u + PROTOCORE_EXC_MAX_FRAMES - 1u,
                            g_info.frames[PROTOCORE_EXC_MAX_FRAMES - 1].pc);
}

// The cause is copied into a fixed field, so a long one is truncated and terminated, and one whose
// closing parenthesis never arrives stops at the end of the text.
void test_the_cause_is_bounded(void)
{
    TEST_ASSERT_TRUE(decode("panic'ed (ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789).\n"));
    TEST_ASSERT_EQUAL_size_t(sizeof(g_info.cause) - 1u, strlen(g_info.cause));
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLMNOPQRSTUVWXYZ01234", g_info.cause);

    TEST_ASSERT_TRUE(decode("Core 0 panic'ed (Trunc"));
    TEST_ASSERT_EQUAL_STRING("Trunc", g_info.cause);
}

// RFC 8259 sec 7: a string MUST escape the quotation mark and the reverse solidus.
void test_json_escapes_the_two_characters_a_string_must(void)
{
    TEST_ASSERT_TRUE(decode("Guru Meditation Error: Core 0 panic'ed (a\"b\\c).\n"
                            "Backtrace: 0x400d1234:0x3ffb0000\n"));
    TEST_ASSERT_EQUAL_STRING("a\"b\\c", g_info.cause);
    TEST_ASSERT_EQUAL_STRING("{\"core\":0,\"cause\":\"a\\\"b\\\\c\",\"pc\":\"0x400d1234\","
                             "\"backtrace\":[\"0x400d1234\"]}",
                             serialize(sizeof(g_json)));
}

// Text carrying none of the three markers is not a panic dump, and neither is an empty one.
void test_text_that_is_not_a_dump_is_reported_as_such(void)
{
    TEST_ASSERT_FALSE(decode("nothing to see here\n"));
    TEST_ASSERT_FALSE(decode(""));
}

// A report that does not fit is not written at all: a truncated JSON object is not a document the
// panel can read, and a partial backtrace names the wrong frames.
void test_json_fails_closed_on_a_short_buffer(void)
{
    TEST_ASSERT_TRUE(decode(PANIC));
    (void)serialize(8);
    TEST_ASSERT_EQUAL_size_t(0u, Exc.n);
    TEST_ASSERT_EQUAL_CHAR('\0', g_json[0]);
}

void test_null_arguments_are_refused(void)
{
    Exc.parse_args.text = NULL;
    Exc.parse_args.info = &g_info;
    Exc.parse(Exc.internal);
    TEST_ASSERT_FALSE(Exc.ok);

    Exc.parse_args.text = "Backtrace: 0x400d1000:0x3ffb0000\n";
    Exc.parse_args.info = NULL;
    Exc.parse(Exc.internal);
    TEST_ASSERT_FALSE(Exc.ok);

    Exc.parse_args.info = NULL;
    Exc.out_args.out = g_json;
    Exc.out_args.cap = sizeof(g_json);
    Exc.json(Exc.internal);
    TEST_ASSERT_EQUAL_size_t(0u, Exc.n);

    Exc.parse_args.info = &g_info;
    Exc.out_args.out = NULL;
    Exc.out_args.cap = sizeof(g_json);
    Exc.json(Exc.internal);
    TEST_ASSERT_EQUAL_size_t(0u, Exc.n);

    Exc.out_args.out = g_json;
    Exc.out_args.cap = 0;
    Exc.json(Exc.internal);
    TEST_ASSERT_EQUAL_size_t(0u, Exc.n);
}
