// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Protocol Buffers wire codec (services/iot/protobuf): the streaming
// writer and the cursor reader, anchored on the canonical spec vectors. Pure host tests.

#include "services/iot/protobuf/protobuf.h"

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// The spec's canonical example: field 1 = 150 encodes as 08 96 01.
void test_vector_field1_150()
{
    PbWriter w;
    uint8_t buf[16];
    pc_pb_writer_init(&w, buf, sizeof(buf));
    pc_pb_uint64(&w, 1, 150);
    size_t n = pc_pb_writer_finish(&w);
    const uint8_t expect[] = {0x08, 0x96, 0x01};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// The spec's string example: field 2 = "testing".
void test_vector_string_testing()
{
    PbWriter w;
    uint8_t buf[32];
    pc_pb_writer_init(&w, buf, sizeof(buf));
    pc_pb_string(&w, 2, "testing");
    size_t n = pc_pb_writer_finish(&w);
    const uint8_t expect[] = {0x12, 0x07, 0x74, 0x65, 0x73, 0x74, 0x69, 0x6E, 0x67};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

void test_zigzag_mapping()
{
    // Decode: encoded 1 -> -1, 2 -> 1, 3 -> -2.
    TEST_ASSERT_EQUAL_INT64(-1, pc_pb_zigzag64(1));
    TEST_ASSERT_EQUAL_INT64(1, pc_pb_zigzag64(2));
    TEST_ASSERT_EQUAL_INT64(-2, pc_pb_zigzag64(3));
    TEST_ASSERT_EQUAL_INT32(-2, pc_pb_zigzag32(3));

    // Encode sint64(-1) -> tag 08, varint 01.
    PbWriter w;
    uint8_t buf[16];
    pc_pb_writer_init(&w, buf, sizeof(buf));
    pc_pb_sint64(&w, 1, -1);
    size_t n = pc_pb_writer_finish(&w);
    const uint8_t expect[] = {0x08, 0x01};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

void test_fixed_and_float_bytes()
{
    PbWriter w;
    uint8_t buf[32];
    pc_pb_writer_init(&w, buf, sizeof(buf));
    pc_pb_fixed32(&w, 3, 0xDEADBEEF);
    pc_pb_float(&w, 4, 1.5f); // 1.5f bits == 0x3FC00000
    size_t n = pc_pb_writer_finish(&w);
    const uint8_t expect[] = {
        0x1D, 0xEF, 0xBE, 0xAD, 0xDE, // field 3 (3<<3|5), LE 0xDEADBEEF
        0x25, 0x00, 0x00, 0xC0, 0x3F  // field 4 (4<<3|5), LE 1.5f
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// Round-trip a mixed message through the writer and the cursor reader.
void test_round_trip_reader()
{
    PbWriter w;
    uint8_t buf[64];
    pc_pb_writer_init(&w, buf, sizeof(buf));
    pc_pb_uint64(&w, 1, 150);
    pc_pb_string(&w, 2, "hi");
    pc_pb_fixed32(&w, 3, 0x01020304);
    pc_pb_double(&w, 4, 2.5);
    pc_pb_sint64(&w, 5, -1234567);
    size_t total = pc_pb_writer_finish(&w);
    TEST_ASSERT_GREATER_THAN(0, (int)total);

    size_t pos = 0;
    PbField f;

    TEST_ASSERT_TRUE(pc_pb_read_field(buf, total, &pos, &f));
    TEST_ASSERT_EQUAL_UINT32(1, f.field_number);
    TEST_ASSERT_EQUAL_UINT8(PB_WT_VARINT, f.wire_type);
    TEST_ASSERT_EQUAL_UINT64(150, f.value);

    TEST_ASSERT_TRUE(pc_pb_read_field(buf, total, &pos, &f));
    TEST_ASSERT_EQUAL_UINT32(2, f.field_number);
    TEST_ASSERT_EQUAL_UINT8(PB_WT_LEN, f.wire_type);
    TEST_ASSERT_EQUAL_size_t(2, f.len);
    TEST_ASSERT_EQUAL_MEMORY("hi", f.data, 2);

    TEST_ASSERT_TRUE(pc_pb_read_field(buf, total, &pos, &f));
    TEST_ASSERT_EQUAL_UINT32(3, f.field_number);
    TEST_ASSERT_EQUAL_UINT8(PB_WT_I32, f.wire_type);
    TEST_ASSERT_EQUAL_HEX32(0x01020304, (uint32_t)f.value);

    TEST_ASSERT_TRUE(pc_pb_read_field(buf, total, &pos, &f));
    TEST_ASSERT_EQUAL_UINT32(4, f.field_number);
    TEST_ASSERT_EQUAL_UINT8(PB_WT_I64, f.wire_type);
    TEST_ASSERT_EQUAL_HEX64(0x4004000000000000ULL, f.value); // IEEE-754 bits of 2.5
    TEST_ASSERT_TRUE(pc_pb_double_bits(f.value) == 2.5);

    TEST_ASSERT_TRUE(pc_pb_read_field(buf, total, &pos, &f));
    TEST_ASSERT_EQUAL_UINT32(5, f.field_number);
    TEST_ASSERT_EQUAL_INT64(-1234567, pc_pb_zigzag64(f.value));

    TEST_ASSERT_FALSE(pc_pb_read_field(buf, total, &pos, &f)); // end of buffer
}

// int64 negative encodes as a 10-byte two's-complement varint.
void test_int64_negative()
{
    PbWriter w;
    uint8_t buf[16];
    pc_pb_writer_init(&w, buf, sizeof(buf));
    pc_pb_int64(&w, 1, -1);
    size_t total = pc_pb_writer_finish(&w);
    TEST_ASSERT_EQUAL_size_t(11, total); // 1 tag + 10 varint

    size_t pos = 0;
    PbField f;
    TEST_ASSERT_TRUE(pc_pb_read_field(buf, total, &pos, &f));
    TEST_ASSERT_EQUAL_HEX64(0xFFFFFFFFFFFFFFFFULL, f.value);
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)f.value);
}

void test_varint_and_overflow()
{
    // A multi-byte varint round-trips.
    PbWriter w;
    uint8_t buf[16];
    pc_pb_writer_init(&w, buf, sizeof(buf));
    pc_pb_write_varint(&w, 300); // 0xAC 0x02
    TEST_ASSERT_EQUAL_size_t(2, pc_pb_writer_finish(&w));
    TEST_ASSERT_EQUAL_HEX8(0xAC, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1]);
    size_t pos = 0;
    uint64_t v;
    TEST_ASSERT_TRUE(pc_pb_read_varint(buf, 2, &pos, &v));
    TEST_ASSERT_EQUAL_UINT64(300, v);

    // Overflow fails closed.
    PbWriter sw;
    uint8_t small[4];
    pc_pb_writer_init(&sw, small, sizeof(small));
    pc_pb_string(&sw, 1, "this is way too long");
    TEST_ASSERT_EQUAL_size_t(0, pc_pb_writer_finish(&sw));
}

void test_malformed_reads()
{
    size_t pos = 0;
    uint64_t v;
    const uint8_t trunc[] = {0x80}; // continuation bit set, no terminating byte
    TEST_ASSERT_FALSE(pc_pb_read_varint(trunc, sizeof(trunc), &pos, &v));

    PbField f;
    pos = 0;
    const uint8_t bad_len[] = {0x12, 0x05, 'h', 'i'}; // LEN says 5 but only 2 bytes follow
    TEST_ASSERT_FALSE(pc_pb_read_field(bad_len, sizeof(bad_len), &pos, &f));

    pos = 0;
    const uint8_t group[] = {0x0B}; // field 1 wire type 3 (SGROUP) - unsupported
    TEST_ASSERT_FALSE(pc_pb_read_field(group, sizeof(group), &pos, &f));
}

// A 64-bit varint is at most 10 bytes; the full-width value decodes, an 11th
// continuation byte does not (overlong varints are rejected, not wrapped).
void test_varint_width_boundary()
{
    // The maximum 64-bit varint: nine 0xFF groups then 0x01 -> all bits set.
    const uint8_t max64[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
    size_t pos = 0;
    uint64_t v = 0;
    TEST_ASSERT_TRUE(pc_pb_read_varint(max64, sizeof(max64), &pos, &v));
    TEST_ASSERT_EQUAL_HEX64(0xFFFFFFFFFFFFFFFFULL, v);
    TEST_ASSERT_EQUAL_size_t(10, pos);

    // Ten continuation bytes never terminate within the 10-byte budget -> reject.
    const uint8_t overlong[] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01};
    pos = 0;
    TEST_ASSERT_FALSE(pc_pb_read_varint(overlong, sizeof(overlong), &pos, &v));
}

// A zero-length LEN field is valid: it round-trips as an empty (non-null) payload.
void test_empty_length_field()
{
    PbWriter w;
    uint8_t buf[8];
    pc_pb_writer_init(&w, buf, sizeof(buf));
    pc_pb_string(&w, 1, ""); // empty string -> tag + length 0
    size_t total = pc_pb_writer_finish(&w);
    const uint8_t expect[] = {0x0A, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), total);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, total);

    size_t pos = 0;
    PbField f;
    TEST_ASSERT_TRUE(pc_pb_read_field(buf, total, &pos, &f));
    TEST_ASSERT_EQUAL_UINT32(1, f.field_number);
    TEST_ASSERT_EQUAL_UINT8(PB_WT_LEN, f.wire_type);
    TEST_ASSERT_EQUAL_size_t(0, f.len);
    TEST_ASSERT_NOT_NULL(f.data); // empty, but a valid in-buffer pointer
}

// Writer overflow / null-argument paths all fail closed.
void test_writer_error_paths()
{
    uint8_t buf[4];
    PbWriter w;

    // A 5-byte varint does not fit a 4-byte buffer.
    pc_pb_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_pb_write_varint(&w, 0xFFFFFFFFull));
    TEST_ASSERT_TRUE(w.error);

    // fixed32: the tag fits the 2-byte buffer but the 4-byte value does not.
    uint8_t b2[2];
    pc_pb_writer_init(&w, b2, sizeof(b2));
    TEST_ASSERT_FALSE(pc_pb_fixed32(&w, 1, 0x12345678u));
    TEST_ASSERT_TRUE(w.error);

    // bytes: not even the tag fits a zero-length buffer.
    pc_pb_writer_init(&w, buf, 0);
    TEST_ASSERT_FALSE(pc_pb_bytes(&w, 1, (const uint8_t *)"x", 1));

    // string: a null pointer is rejected.
    pc_pb_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_pb_string(&w, 1, NULL));
    TEST_ASSERT_TRUE(w.error);
}

// Reader truncation / null-argument paths all fail closed.
void test_reader_error_paths()
{
    size_t pos = 0;
    uint64_t v;
    TEST_ASSERT_FALSE(pc_pb_read_varint(NULL, 4, &pos, &v));

    PbField f;
    uint8_t lone_cont[1] = {0x80}; // tag varint is a lone continuation byte
    pos = 0;
    TEST_ASSERT_FALSE(pc_pb_read_field(lone_cont, 1, &pos, &f));

    uint8_t i64[3] = {0x09, 0x01, 0x02}; // wire type 1 (I64) with < 8 payload bytes
    pos = 0;
    TEST_ASSERT_FALSE(pc_pb_read_field(i64, sizeof(i64), &pos, &f));

    uint8_t i32[2] = {0x0D, 0x01}; // wire type 5 (I32) with < 4 payload bytes
    pos = 0;
    TEST_ASSERT_FALSE(pc_pb_read_field(i32, sizeof(i32), &pos, &f));

    uint8_t len_trunc[2] = {0x0A, 0x80}; // wire type 2 (LEN) with a truncated length varint
    pos = 0;
    TEST_ASSERT_FALSE(pc_pb_read_field(len_trunc, sizeof(len_trunc), &pos, &f));
}

// The float-from-bits helper round-trips an IEEE-754 value.
void test_float_bits_helper()
{
    uint32_t bits;
    float in = 3.5f;
    memcpy(&bits, &in, 4);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, pc_pb_float_bits(bits));
}

// Once the sticky error flag is set (by an earlier overflow), pc_pb_write_varint
// short-circuits at entry and leaves the writer state untouched - the "already in
// error" path, distinct from the "this call is the one that overflows" path.
void test_writer_error_is_sticky()
{
    PbWriter w;
    uint8_t buf[4];
    pc_pb_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_pb_write_varint(&w, 0xFFFFFFFFull)); // overflow: sets w->error
    TEST_ASSERT_TRUE(w.error);
    size_t pos_before = w.pos;

    TEST_ASSERT_FALSE(pc_pb_write_varint(&w, 1)); // short-circuits on the sticky flag
    TEST_ASSERT_EQUAL_size_t(pos_before, w.pos);  // untouched: never reached the payload loop
}

// pc_pb_bool encodes true/false as the varint 1/0 (previously untested entirely).
void test_bool_true_and_false()
{
    PbWriter w;
    uint8_t buf[8];
    pc_pb_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(pc_pb_bool(&w, 1, PROTO_TRUE));
    TEST_ASSERT_TRUE(pc_pb_bool(&w, 2, PROTO_FALSE));
    size_t n = pc_pb_writer_finish(&w);
    const uint8_t expect[] = {0x08, 0x01, 0x10, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// pc_pb_uint64 (the `write_tag(...) && write_varint(...)` combinator) has two distinct
// overflow points: the tag itself not fitting, versus the tag fitting but the value's
// varint not fitting behind it.
void test_uint64_tag_and_value_overflow()
{
    PbWriter w;

    uint8_t zero_cap[1];
    pc_pb_writer_init(&w, zero_cap, 0);
    TEST_ASSERT_FALSE(pc_pb_uint64(&w, 1, 150)); // tag alone does not fit

    uint8_t one_byte[1];
    pc_pb_writer_init(&w, one_byte, sizeof(one_byte));
    TEST_ASSERT_FALSE(pc_pb_uint64(&w, 1, 300)); // tag fits; the 2-byte varint value does not
}

// pc_pb_fixed32 / pc_pb_fixed64: the tag write itself can fail on a zero-capacity
// buffer (not just the fixed-width payload behind it, already covered elsewhere).
void test_fixed32_fixed64_tag_overflow()
{
    PbWriter w;

    uint8_t zero_cap[1];
    pc_pb_writer_init(&w, zero_cap, 0);
    TEST_ASSERT_FALSE(pc_pb_fixed32(&w, 1, 0x11111111u));

    pc_pb_writer_init(&w, zero_cap, 0);
    TEST_ASSERT_FALSE(pc_pb_fixed64(&w, 1, 0x1122334455667788ULL));

    uint8_t one_byte[1];
    pc_pb_writer_init(&w, one_byte, sizeof(one_byte));
    TEST_ASSERT_FALSE(pc_pb_fixed64(&w, 1, 0x1122334455667788ULL)); // tag fits, 8-byte payload does not
}

// pc_pb_bytes: the length varint can fail to fit even when the tag just did, and a
// null data pointer with a non-zero length is rejected once the header wrote fine.
void test_bytes_header_overflow_and_null_data()
{
    PbWriter w;

    uint8_t one_byte[1];
    pc_pb_writer_init(&w, one_byte, sizeof(one_byte));
    TEST_ASSERT_FALSE(pc_pb_bytes(&w, 1, NULL, 0)); // tag fits, the length varint does not

    uint8_t buf[8];
    pc_pb_writer_init(&w, buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_pb_bytes(&w, 1, NULL, 5)); // header writes fine; null data is rejected
    TEST_ASSERT_TRUE(w.error);
}

// Reader null-argument combinations not already exercised by test_reader_error_paths
// (which only covers pc_pb_read_varint(NULL, ...) and one pc_pb_read_field case).
void test_reader_additional_null_arg_paths()
{
    uint8_t buf[4] = {0x08, 0x01, 0x00, 0x00};
    size_t pos = 0;
    uint64_t v;
    PbField f;

    TEST_ASSERT_FALSE(pc_pb_read_varint(buf, sizeof(buf), NULL, &v));   // pos == NULL
    TEST_ASSERT_FALSE(pc_pb_read_varint(buf, sizeof(buf), &pos, NULL)); // out == NULL

    TEST_ASSERT_FALSE(pc_pb_read_field(NULL, sizeof(buf), &pos, &f));  // buf == NULL
    TEST_ASSERT_FALSE(pc_pb_read_field(buf, sizeof(buf), NULL, &f));   // pos == NULL
    TEST_ASSERT_FALSE(pc_pb_read_field(buf, sizeof(buf), &pos, NULL)); // out == NULL
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_writer_error_paths);
    RUN_TEST(test_reader_error_paths);
    RUN_TEST(test_float_bits_helper);
    RUN_TEST(test_vector_field1_150);
    RUN_TEST(test_vector_string_testing);
    RUN_TEST(test_zigzag_mapping);
    RUN_TEST(test_fixed_and_float_bytes);
    RUN_TEST(test_round_trip_reader);
    RUN_TEST(test_int64_negative);
    RUN_TEST(test_varint_and_overflow);
    RUN_TEST(test_malformed_reads);
    RUN_TEST(test_varint_width_boundary);
    RUN_TEST(test_empty_length_field);
    RUN_TEST(test_writer_error_is_sticky);
    RUN_TEST(test_bool_true_and_false);
    RUN_TEST(test_uint64_tag_and_value_overflow);
    RUN_TEST(test_fixed32_fixed64_tag_overflow);
    RUN_TEST(test_bytes_header_overflow_and_null_data);
    RUN_TEST(test_reader_additional_null_arg_paths);
    return UNITY_END();
}
