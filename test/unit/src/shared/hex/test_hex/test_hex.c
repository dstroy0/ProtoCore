// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for base-16 conversion (shared/hex/hex.h).
//
// The digit tables are the load-bearing part: PROTOCORE_HEX.lower and .upper are indexed directly by
// four call sites across the library (membuild, json, exc_decoder, ble_gatt, ip), so a wrong octet
// in either would corrupt every hex field the library emits. test_digit_tables_are_ascii asserts
// them against the ASCII code points themselves rather than against the module's own lookup.
//
// The decoders report failure through a negative return rather than a sentinel digit, so the
// refusal cases matter as much as the conversions: a malformed byte that came back as 0 would be
// indistinguishable from a valid zero.

#include "shared/hex/hex.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// ASCII: '0'..'9' are 0x30..0x39, 'a'..'f' are 0x61..0x66, 'A'..'F' are 0x41..0x46. Asserted from
// the code points, so the table cannot agree with a typo in itself.
void test_digit_tables_are_ascii(void)
{
    for (int v = 0; v <= 9; v++)
    {
        TEST_ASSERT_EQUAL_CHAR((char)(0x30 + v), PROTOCORE_HEX.lower[v]);
        TEST_ASSERT_EQUAL_CHAR((char)(0x30 + v), PROTOCORE_HEX.upper[v]);
    }
    for (int v = 10; v <= 15; v++)
    {
        TEST_ASSERT_EQUAL_CHAR((char)(0x61 + (v - 10)), PROTOCORE_HEX.lower[v]);
        TEST_ASSERT_EQUAL_CHAR((char)(0x41 + (v - 10)), PROTOCORE_HEX.upper[v]);
    }
}

// One nibble out, both cases.
void test_digit_of_nibble(void)
{
    Hex.args.upper = PROTO_FALSE;
    Hex.args.nibble = 0x0Au;
    Hex.digit(Hex.internal);
    TEST_ASSERT_EQUAL_CHAR('a', Hex.ch);

    Hex.args.upper = PROTO_TRUE;
    Hex.args.nibble = 0x0Fu;
    Hex.digit(Hex.internal);
    TEST_ASSERT_EQUAL_CHAR('F', Hex.ch);
}

// Only the low four bits select the digit, so a caller cannot index past the table.
void test_digit_masks_to_four_bits(void)
{
    Hex.args.upper = PROTO_FALSE;
    Hex.args.nibble = 0xF3u; // high nibble must be ignored
    Hex.digit(Hex.internal);
    TEST_ASSERT_EQUAL_CHAR('3', Hex.ch);
}

// One character in, either case; anything else is -1 rather than a digit value.
void test_val_of_character(void)
{
    static const char OK[] = "0123456789abcdefABCDEF";
    static const int8_t WANT[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 10, 11, 12, 13, 14, 15};
    for (size_t i = 0; i < sizeof(OK) - 1; i++)
    {
        Hex.args.ch = OK[i];
        Hex.val(Hex.internal);
        TEST_ASSERT_EQUAL_INT8(WANT[i], Hex.i8);
    }
}

// A non-digit is refused, not folded to zero. 'g' and '/' and ':' sit just outside the ranges.
void test_val_refuses_non_digits(void)
{
    static const char BAD[] = {'g', 'G', '/', ':', '@', 'z', ' ', '\0'};
    for (size_t i = 0; i < sizeof(BAD); i++)
    {
        Hex.args.ch = BAD[i];
        Hex.val(Hex.internal);
        TEST_ASSERT_EQUAL_INT8(-1, Hex.i8);
    }
}

// A byte run out and back is the identity - the property every caller relies on.
void test_encode_decode_round_trip(void)
{
    static const uint8_t IN[8] = {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF, 0xA5, 0x5A};
    char text[2 * sizeof(IN) + 1];
    uint8_t back[sizeof(IN)];

    Hex.args.upper = PROTO_FALSE;
    Hex.io.in = IN;
    Hex.io.n = (uint32_t)sizeof(IN);
    Hex.io.out = text;
    Hex.encode(Hex.internal);
    TEST_ASSERT_EQUAL_STRING("00017f80feffa55a", text);

    Hex.io.text = text;
    Hex.io.n = (uint32_t)(2 * sizeof(IN));
    Hex.io.bytes = back;
    Hex.io.cap = (uint32_t)sizeof(back);
    Hex.decode(Hex.internal);
    TEST_ASSERT_EQUAL_INT32((int32_t)sizeof(IN), Hex.i32);
    TEST_ASSERT_EQUAL_MEMORY(IN, back, sizeof(IN));
}

// An odd-length run cannot be whole bytes, so it is refused before anything is written.
void test_decode_refuses_odd_length(void)
{
    uint8_t back[4] = {0xEE, 0xEE, 0xEE, 0xEE};
    Hex.io.text = "abc";
    Hex.io.n = 3;
    Hex.io.bytes = back;
    Hex.io.cap = (uint32_t)sizeof(back);
    Hex.decode(Hex.internal);
    TEST_ASSERT_EQUAL_INT32(-1, Hex.i32);
    TEST_ASSERT_EQUAL_UINT8(0xEEu, back[0]); // untouched
}

// A result larger than the destination is refused without writing anything.
void test_decode_refuses_overflow(void)
{
    uint8_t back[2] = {0xEE, 0xEE};
    Hex.io.text = "00112233";
    Hex.io.n = 8;
    Hex.io.bytes = back;
    Hex.io.cap = (uint32_t)sizeof(back);
    Hex.decode(Hex.internal);
    TEST_ASSERT_EQUAL_INT32(-1, Hex.i32);
    TEST_ASSERT_EQUAL_UINT8(0xEEu, back[0]);
}

// u32 writes the form an HTTP/1.1 chunk size line takes (RFC 9112 sec 7.1): lowercase, most
// significant digit first, no 0x prefix, no leading zeros - and zero is a single "0", not empty.
void test_u32_is_the_chunk_size_form(void)
{
    char out[8];

    Hex.args.v = 0u;
    Hex.io.out = out;
    Hex.u32(Hex.internal);
    TEST_ASSERT_EQUAL_UINT8(1u, Hex.u8);
    TEST_ASSERT_EQUAL_MEMORY("0", out, 1);

    Hex.args.v = 0x1Au;
    Hex.io.out = out;
    Hex.u32(Hex.internal);
    TEST_ASSERT_EQUAL_UINT8(2u, Hex.u8);
    TEST_ASSERT_EQUAL_MEMORY("1a", out, 2);

    Hex.args.v = 0xFFFFFFFFu;
    Hex.io.out = out;
    Hex.u32(Hex.internal);
    TEST_ASSERT_EQUAL_UINT8(8u, Hex.u8);
    TEST_ASSERT_EQUAL_MEMORY("ffffffff", out, 8);
}
