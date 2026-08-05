// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SunSpec Modbus codec (services/energy/sunspec): the map writer, the
// marker check, the model-chain walker, and the typed point readers. Pure host tests.

#include "services/energy/sunspec/sunspec.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// Build a small map (marker + a tiny model + end model), then walk it back.
void test_build_and_walk()
{
    uint8_t buf[64];
    SunSpecWriter w;
    pc_sunspec_writer_init(&w, buf, sizeof(buf));
    pc_sunspec_write_marker(&w);
    // A made-up model id 99 with 4 body registers: u16=0x1234, i16=-2, u32=0x00BEEF01
    pc_sunspec_write_model_header(&w, 99, 4);
    pc_sunspec_write_u16(&w, 0x1234);
    pc_sunspec_write_i16(&w, -2);
    pc_sunspec_write_u32(&w, 0x00BEEF01);
    pc_sunspec_write_end_model(&w);
    size_t n = pc_sunspec_writer_finish(&w);
    // marker(4) + header(4) + body(8) + end(4) = 20
    TEST_ASSERT_EQUAL_size_t(20, n);

    TEST_ASSERT_TRUE(pc_sunspec_check_marker(buf, n));
    const uint8_t marker[] = {0x53, 0x75, 0x6E, 0x53}; // "SunS"
    TEST_ASSERT_EQUAL_HEX8_ARRAY(marker, buf, 4);

    size_t off;
    TEST_ASSERT_TRUE(pc_sunspec_begin(buf, n, &off));
    TEST_ASSERT_EQUAL_size_t(4, off);

    SunSpecModel m;
    TEST_ASSERT_TRUE(pc_sunspec_next_model(buf, n, &off, &m));
    TEST_ASSERT_EQUAL_UINT16(99, m.id);
    TEST_ASSERT_EQUAL_UINT16(4, m.length);
    TEST_ASSERT_EQUAL_size_t(8, m.body_len);
    TEST_ASSERT_EQUAL_HEX16(0x1234, pc_sunspec_u16(m.body, 0));
    TEST_ASSERT_EQUAL_INT16(-2, pc_sunspec_i16(m.body, 1));
    TEST_ASSERT_EQUAL_HEX32(0x00BEEF01, pc_sunspec_u32(m.body, 2));

    // The next walk hits the end model and stops.
    TEST_ASSERT_FALSE(pc_sunspec_next_model(buf, n, &off, &m));
}

// A two-model map walks in order.
void test_two_models()
{
    uint8_t buf[64];
    SunSpecWriter w;
    pc_sunspec_writer_init(&w, buf, sizeof(buf));
    pc_sunspec_write_marker(&w);
    pc_sunspec_write_model_header(&w, 1, 1); // common-ish, 1 body register
    pc_sunspec_write_u16(&w, 0xAAAA);
    pc_sunspec_write_model_header(&w, 103, 2); // inverter-ish, 2 body registers
    pc_sunspec_write_i16(&w, 100);
    pc_sunspec_write_i16(&w, -3); // a scale factor (sunssf)
    pc_sunspec_write_end_model(&w);
    size_t n = pc_sunspec_writer_finish(&w);

    size_t off;
    SunSpecModel m;
    TEST_ASSERT_TRUE(pc_sunspec_begin(buf, n, &off));
    TEST_ASSERT_TRUE(pc_sunspec_next_model(buf, n, &off, &m));
    TEST_ASSERT_EQUAL_UINT16(1, m.id);
    TEST_ASSERT_TRUE(pc_sunspec_next_model(buf, n, &off, &m));
    TEST_ASSERT_EQUAL_UINT16(103, m.id);
    TEST_ASSERT_EQUAL_INT16(100, pc_sunspec_i16(m.body, 0));
    TEST_ASSERT_EQUAL_INT16(-3, pc_sunspec_i16(m.body, 1));
    TEST_ASSERT_FALSE(pc_sunspec_next_model(buf, n, &off, &m));
}

void test_string_point()
{
    uint8_t buf[64];
    SunSpecWriter w;
    pc_sunspec_writer_init(&w, buf, sizeof(buf));
    pc_sunspec_write_marker(&w);
    pc_sunspec_write_model_header(&w, 1, 8);     // 8 registers = 16 chars of body
    pc_sunspec_write_string(&w, "Acme Corp", 8); // "Acme Corp" + NUL padding to 16 bytes
    pc_sunspec_write_end_model(&w);
    size_t n = pc_sunspec_writer_finish(&w);

    size_t off;
    SunSpecModel m;
    pc_sunspec_begin(buf, n, &off);
    TEST_ASSERT_TRUE(pc_sunspec_next_model(buf, n, &off, &m));
    char mfg[32];
    TEST_ASSERT_TRUE(pc_sunspec_string(m.body, 0, 8, mfg, sizeof(mfg)));
    TEST_ASSERT_EQUAL_STRING("Acme Corp", mfg);
}

void test_marker_and_truncation()
{
    const uint8_t no_marker[] = {0x00, 0x01, 0x02, 0x03};
    TEST_ASSERT_FALSE(pc_sunspec_check_marker(no_marker, sizeof(no_marker)));
    size_t off;
    TEST_ASSERT_FALSE(pc_sunspec_begin(no_marker, sizeof(no_marker), &off));

    // Marker + a header that claims more body than is present -> truncation.
    const uint8_t trunc[] = {0x53, 0x75, 0x6E, 0x53, 0x00, 0x63, 0x00, 0x04, 0x12, 0x34};
    SunSpecModel m;
    TEST_ASSERT_TRUE(pc_sunspec_begin(trunc, sizeof(trunc), &off));
    TEST_ASSERT_FALSE(pc_sunspec_next_model(trunc, sizeof(trunc), &off, &m));
}

void test_writer_overflow_fails_closed()
{
    uint8_t small[6];
    SunSpecWriter w;
    pc_sunspec_writer_init(&w, small, sizeof(small));
    pc_sunspec_write_marker(&w);             // 4 bytes ok
    pc_sunspec_write_model_header(&w, 1, 1); // would need 4 more -> overflow
    TEST_ASSERT_EQUAL_size_t(0, pc_sunspec_writer_finish(&w));
}

// Reader guards (next_model null args + no-room-for-header), the i32 point reader, and
// the string reader's argument guards.
void test_reader_guards_and_i32()
{
    uint8_t buf[16] = {0};
    size_t off = 0;
    SunSpecModel m;
    TEST_ASSERT_FALSE(pc_sunspec_next_model(NULL, 16, &off, &m));  // null regs
    TEST_ASSERT_FALSE(pc_sunspec_next_model(buf, 16, NULL, &m));   // null offset
    TEST_ASSERT_FALSE(pc_sunspec_next_model(buf, 16, &off, NULL)); // null out
    off = 14;
    TEST_ASSERT_FALSE(pc_sunspec_next_model(buf, 16, &off, &m)); // no room for the [id][length] header

    const uint8_t body[4] = {0xFF, 0xFF, 0xFF, 0xFE}; // big-endian -2
    TEST_ASSERT_EQUAL_INT32(-2, pc_sunspec_i32(body, 0));

    char out[8];
    TEST_ASSERT_FALSE(pc_sunspec_string(NULL, 0, 1, out, sizeof(out)));  // null body
    TEST_ASSERT_FALSE(pc_sunspec_string(body, 0, 1, NULL, sizeof(out))); // null out
    TEST_ASSERT_FALSE(pc_sunspec_string(body, 0, 1, out, 0));            // zero out_cap
}

// The i32 writer, ss_put's error-flag short-circuit, and every pc_sunspec_write_string
// reject (null string, already-errored writer, and a field that overflows the buffer).
void test_writer_error_and_string_paths()
{
    uint8_t buf[16];
    SunSpecWriter w;
    pc_sunspec_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(pc_sunspec_write_i32(&w, -123456));
    TEST_ASSERT_EQUAL_size_t(4, pc_sunspec_writer_finish(&w));

    // Once a write overflows, the next ss_put bails on the sticky error flag.
    uint8_t two[2];
    SunSpecWriter e;
    pc_sunspec_writer_init(&e, two, sizeof(two));
    TEST_ASSERT_FALSE(pc_sunspec_write_u32(&e, 0)); // needs 4 > cap 2 -> sets error
    TEST_ASSERT_FALSE(pc_sunspec_write_u16(&e, 0)); // ss_put sees the error flag

    pc_sunspec_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_sunspec_write_string(&w, NULL, 1)); // null string

    SunSpecWriter serr;
    pc_sunspec_writer_init(&serr, two, sizeof(two));
    TEST_ASSERT_FALSE(pc_sunspec_write_u32(&serr, 0));         // set the error flag
    TEST_ASSERT_FALSE(pc_sunspec_write_string(&serr, "x", 1)); // write_string sees it

    uint8_t four[4];
    SunSpecWriter sof;
    pc_sunspec_writer_init(&sof, four, sizeof(four));
    TEST_ASSERT_FALSE(pc_sunspec_write_string(&sof, "abcd", 4)); // field 8 > cap 4
}

// pc_sunspec_check_marker's short-circuit arms (null regs, and a non-null buffer that's
// too short for the marker) plus pc_sunspec_begin's null-offset guard.
void test_check_marker_null_and_short_and_begin_null_offset()
{
    TEST_ASSERT_FALSE(pc_sunspec_check_marker(NULL, 10)); // regs == NULL short-circuits

    const uint8_t shortbuf[4] = {0x53, 0x75, 0x6E, 0x53};    // "SunS", but len is reported short
    TEST_ASSERT_FALSE(pc_sunspec_check_marker(shortbuf, 2)); // len < 4

    const uint8_t marker[4] = {0x53, 0x75, 0x6E, 0x53};
    size_t off;
    TEST_ASSERT_FALSE(pc_sunspec_begin(marker, sizeof(marker), NULL)); // offset == NULL
    (void)off;
}

// pc_sunspec_string's loop-exit conditions: ending because avail (nregs*2) is exhausted with
// no NUL padding, and ending because out_cap truncates before avail or a NUL is reached.
void test_string_loop_boundary_exits()
{
    // No NUL anywhere in the field; the loop runs until i == avail (out_cap is not the limit).
    const uint8_t full[2] = {'A', 'B'};
    char out1[8];
    TEST_ASSERT_TRUE(pc_sunspec_string(full, 0, 1, out1, sizeof(out1)));
    TEST_ASSERT_EQUAL_STRING("AB", out1);

    // No NUL within the truncated window either; out_cap - 1 is smaller than avail, so the
    // loop is cut short by the destination capacity, not by content or by avail.
    const uint8_t longer[4] = {'W', 'X', 'Y', 'Z'};
    char out2[3];
    TEST_ASSERT_TRUE(pc_sunspec_string(longer, 0, 2, out2, sizeof(out2)));
    TEST_ASSERT_EQUAL_STRING("WX", out2);
}

// The && short-circuit failure arms in pc_sunspec_write_model_header and
// pc_sunspec_write_end_model: each one's *first* write_u16 failing (so the second is never
// attempted), and write_end_model's *second* write_u16 failing after the first succeeded.
void test_writer_two_step_short_circuit_failures()
{
    uint8_t one[1];
    SunSpecWriter w1;
    pc_sunspec_writer_init(&w1, one, sizeof(one)); // cap 1: even the first write_u16 can't fit
    TEST_ASSERT_FALSE(pc_sunspec_write_model_header(&w1, 5, 5));
    TEST_ASSERT_EQUAL_size_t(0, pc_sunspec_writer_finish(&w1));

    SunSpecWriter w2;
    pc_sunspec_writer_init(&w2, one, sizeof(one)); // same: id write itself overflows
    TEST_ASSERT_FALSE(pc_sunspec_write_end_model(&w2));
    TEST_ASSERT_EQUAL_size_t(0, pc_sunspec_writer_finish(&w2));

    uint8_t three[3];
    SunSpecWriter w3;
    pc_sunspec_writer_init(&w3, three, sizeof(three)); // id write (2B) fits, length write (2B) doesn't
    TEST_ASSERT_FALSE(pc_sunspec_write_end_model(&w3));
    TEST_ASSERT_EQUAL_size_t(0, pc_sunspec_writer_finish(&w3));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_and_walk);
    RUN_TEST(test_two_models);
    RUN_TEST(test_string_point);
    RUN_TEST(test_marker_and_truncation);
    RUN_TEST(test_writer_overflow_fails_closed);
    RUN_TEST(test_reader_guards_and_i32);
    RUN_TEST(test_writer_error_and_string_paths);
    RUN_TEST(test_check_marker_null_and_short_and_begin_null_offset);
    RUN_TEST(test_string_loop_boundary_exits);
    RUN_TEST(test_writer_two_step_short_circuit_failures);
    return UNITY_END();
}
