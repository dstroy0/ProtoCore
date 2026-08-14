// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the LSB-first bit writer (mmgr/bitio.h).
//
// RFC 1951 sec 3.1.1 states the packing order this writer implements: elements enter a byte "in
// order of increasing bit number within the byte", a non-Huffman element low bit first and a
// Huffman code high bit first.
//
// test_rfc1951_empty_fixed_block is the load-bearing case. The final fixed-Huffman block carrying
// nothing is the three header bits of sec 3.2.3 followed by the seven-bit end-of-block code of
// sec 3.2.6, and under sec 3.1.1 that is the two octets 03 00. A writer that packs high bit first,
// or that pads a partial byte with anything but zero, emits different octets there.

#include "mmgr/bitio.h"
#include <string.h>

#include <unity.h>

#define OUT_CAP 64u

static uint8_t s_out[OUT_CAP];
static protocore_bit_writer w;

// Bind the writer to a fresh, zeroed buffer of @p cap bytes.
static void writer_reset(size_t cap)
{
    memset(s_out, 0, sizeof(s_out));
    w.out = s_out;
    w.cap = cap;
    w.cnt = 0;
    w.acc = 0;
    w.nbits = 0;
    w.overflow = PROTO_FALSE;
}

void setUp(void)
{
    writer_reset(OUT_CAP);
}

void tearDown(void)
{
}

// ---- the RFC 1951 bitstream -----------------------------------------------

// RFC 1951 sec 3.2.3: the first bit is BFINAL, the next two are BTYPE. sec 3.2.6 fixes BTYPE=01
// for the fixed codes and gives the end-of-block symbol 256 the seven-bit code 0000000. sec 3.1.1
// packs the header bits low bit first and the Huffman code high bit first.
//
//   bit 0     BFINAL = 1
//   bits 1,2  BTYPE  = 01, low bit of the element first -> bit1 = 1, bit2 = 0
//   bits 3..9 end-of-block code 0000000
//
//   byte 0 = 0b00000011 = 0x03, byte 1 holds two code bits and six zero pad bits = 0x00
void test_rfc1951_empty_fixed_block(void)
{
    bitw.put(&w, 1u, 1);
    bitw.put(&w, 1u, 2);
    for (int i = 0; i < 7; i++)
    {
        bitw.put(&w, 0u, 1); // the code's bits, most significant first
    }
    bitw.align(&w);

    TEST_ASSERT_FALSE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(2, w.cnt);
    TEST_ASSERT_EQUAL_HEX8(0x03u, s_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, s_out[1]);
}

// RFC 1951 sec 3.2.4: a stored block is BFINAL, BTYPE=00, the remaining bits of the byte ignored,
// then LEN and NLEN, NLEN being the one's complement of LEN. sec 3.1.1 packs each of those 16-bit
// elements low bit first.
//
//   byte 0 = BFINAL 1 at bit 0, BTYPE 00 at bits 1,2 = 0b00000001 = 0x01
//   LEN  = 5      -> 05 00
//   NLEN = ~5     -> 0xFFFA -> FA FF
void test_rfc1951_stored_block_header(void)
{
    bitw.put(&w, 1u, 1);
    bitw.put(&w, 0u, 2);
    bitw.align(&w);
    bitw.put(&w, 5u, 16);
    bitw.put(&w, 0xFFFAu, 16);

    static const uint8_t WANT[] = {0x01, 0x05, 0x00, 0xFA, 0xFF};
    TEST_ASSERT_FALSE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), w.cnt);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, s_out, sizeof(WANT));
}

// ---- the packing rule itself ----------------------------------------------

// sec 3.1.1, applied twice: 0b101 in three bits fills bits 0..2, then 0b10 in two bits enters low
// bit first, so bit 3 takes the element's bit 0 and bit 4 its bit 1.
//   0b1_0101 = 0x15
void test_elements_enter_low_bit_first(void)
{
    bitw.put(&w, 0x5u, 3);
    bitw.put(&w, 0x2u, 2);
    bitw.align(&w);
    TEST_ASSERT_EQUAL_size_t(1, w.cnt);
    TEST_ASSERT_EQUAL_HEX8(0x15u, s_out[0]);
}

// A put takes the low @p n bits of the value and nothing above them.
void test_put_takes_only_the_low_n_bits(void)
{
    bitw.put(&w, 0xFFFFFFFFu, 3);
    bitw.align(&w);
    TEST_ASSERT_EQUAL_size_t(1, w.cnt);
    TEST_ASSERT_EQUAL_HEX8(0x07u, s_out[0]);
}

// Eight bits complete a byte, which spills without an align.
void test_eight_bits_is_exactly_one_byte(void)
{
    bitw.put(&w, 0xA5u, 8);
    TEST_ASSERT_EQUAL_size_t(1, w.cnt);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, s_out[0]);
    TEST_ASSERT_EQUAL_INT(0, w.nbits);
}

// One put wider than a byte spills every byte it completes, low byte of the element first.
void test_a_wide_put_spills_every_completed_byte(void)
{
    bitw.put(&w, 0x00ABCDEFu, 24);
    static const uint8_t WANT[] = {0xEF, 0xCD, 0xAB};
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), w.cnt);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, s_out, sizeof(WANT));
    TEST_ASSERT_EQUAL_INT(0, w.nbits);
}

// A partial byte is finished with zero in the bits above it, not with whatever the accumulator
// carried before.
void test_align_pads_the_partial_byte_with_zero(void)
{
    bitw.put(&w, 0xFFu, 8);
    bitw.put(&w, 0x5u, 3);
    bitw.align(&w);
    TEST_ASSERT_EQUAL_size_t(2, w.cnt);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, s_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x05u, s_out[1]);
    TEST_ASSERT_EQUAL_INT(0, w.nbits);
}

// An align on a byte boundary has nothing buffered, so it writes nothing.
void test_align_on_a_boundary_writes_nothing(void)
{
    bitw.put(&w, 0xA5u, 8);
    bitw.align(&w);
    TEST_ASSERT_EQUAL_size_t(1, w.cnt);
    bitw.align(&w);
    TEST_ASSERT_EQUAL_size_t(1, w.cnt);
    TEST_ASSERT_FALSE(w.overflow);
}

// ---- the capacity edge -----------------------------------------------------

// Filling the buffer to the last byte is not an overflow.
void test_exact_fill_is_not_an_overflow(void)
{
    writer_reset(4);
    for (int i = 0; i < 4; i++)
    {
        bitw.put(&w, (uint32_t)(0x10u + (unsigned)i), 8);
    }
    TEST_ASSERT_FALSE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(4, w.cnt);
}

// A spill with the cursor at the capacity latches overflow and leaves the byte past cap alone.
void test_a_byte_past_cap_latches_and_stores_nothing(void)
{
    writer_reset(4);
    for (int i = 0; i < 5; i++)
    {
        bitw.put(&w, 0xFFu, 8);
    }
    TEST_ASSERT_TRUE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(4, w.cnt);
    TEST_ASSERT_EQUAL_HEX8(0x00u, s_out[4]);
}

// An align needing a byte the buffer does not have latches instead of writing.
void test_align_with_no_room_latches(void)
{
    writer_reset(1);
    bitw.put(&w, 0xFFu, 8);
    TEST_ASSERT_FALSE(w.overflow);
    bitw.put(&w, 0x1u, 3);
    bitw.align(&w);
    TEST_ASSERT_TRUE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(1, w.cnt);
}

// Once latched the flag stays set, whatever a later call would have fit.
void test_overflow_stays_latched(void)
{
    writer_reset(2);
    for (int i = 0; i < 4; i++)
    {
        bitw.put(&w, 0xFFu, 8);
    }
    TEST_ASSERT_TRUE(w.overflow);
    bitw.put(&w, 0x1u, 1);
    bitw.align(&w);
    TEST_ASSERT_TRUE(w.overflow);
}
