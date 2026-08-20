// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Protocol Buffers wire codec (services/iot/protobuf/protobuf.h).
//
// The governing document is Google's Protocol Buffers "Encoding" page
// (https://protobuf.dev/programming-guides/encoding/), not an IETF RFC. It prints the worked
// examples used here verbatim: Test1 with `a = 150` is `08 96 01`, Test2 with `b = "testing"` is
// `12 07 74 65 73 74 69 6e 67`, Test3 embedding that Test1 is `1a 03 08 96 01`, and its ZigZag table
// maps 0 -> 0, -1 -> 1, 1 -> 2, -2 -> 3, 0x7fffffff -> 0xfffffffe and -0x80000000 -> 0xffffffff.
//
// test_encoding_document_worked_examples is load-bearing: those three byte strings exercise the tag
// formula `(field_number << 3) | wire_type`, the Base 128 varint, the LEN length prefix and the
// nesting of one message inside another, which is every mechanism the format is made of.

#include "services/iot/protobuf/protobuf.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_buf[128];
static uint8_t g_inner[64];

static void writer_open(uint8_t slot, uint8_t *buf, size_t cap)
{
    Protobuf.slot = slot;
    Protobuf.writer.buf = buf;
    Protobuf.writer.cap = cap;
    Protobuf.writer_open(protocore_protobuf_span());
}

static size_t writer_finish(uint8_t slot)
{
    Protobuf.slot = slot;
    Protobuf.writer_finish(protocore_protobuf_span());
    return Protobuf.n;
}

static void reader_open(uint8_t slot, const uint8_t *buf, size_t len, size_t pos)
{
    Protobuf.slot = slot;
    Protobuf.source.buf = buf;
    Protobuf.source.len = len;
    Protobuf.source.pos = pos;
    Protobuf.reader_open(protocore_protobuf_span());
}

static proto_bool read_record(uint8_t slot)
{
    Protobuf.slot = slot;
    Protobuf.read_record(protocore_protobuf_span());
    return Protobuf.ok;
}

// The three worked examples the "Encoding" document prints, each built and then read back.
void test_encoding_document_worked_examples(void)
{
    // "Message Structure": `message Test1 { optional int32 a = 1; }` with a = 150 is `08 96 01`.
    static const uint8_t TEST1[3] = {0x08, 0x96, 0x01};
    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.tag.field_number = 1;
    Protobuf.value.u64 = 150;
    Protobuf.write_uint64(protocore_protobuf_span());
    TEST_ASSERT_TRUE(Protobuf.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(TEST1), writer_finish(0));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TEST1, g_buf, sizeof(TEST1));

    reader_open(0, TEST1, sizeof(TEST1), 0);
    TEST_ASSERT_TRUE(read_record(0));
    TEST_ASSERT_EQUAL_UINT32(1u, Protobuf.record.field_number);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PROTOBUF_WT_VARINT, Protobuf.record.wire_type);
    TEST_ASSERT_EQUAL_UINT64(150u, Protobuf.record.value);
    TEST_ASSERT_EQUAL_UINT(sizeof(TEST1), Protobuf.n);
    TEST_ASSERT_FALSE(read_record(0)); // the cursor is at the end

    // "Length-Delimited Records": `message Test2 { optional string b = 2; }` with b = "testing" is
    // `12 07 74 65 73 74 69 6e 67`.
    static const uint8_t TEST2[9] = {0x12, 0x07, 0x74, 0x65, 0x73, 0x74, 0x69, 0x6e, 0x67};
    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.tag.field_number = 2;
    Protobuf.value.text = "testing";
    Protobuf.write_string(protocore_protobuf_span());
    TEST_ASSERT_TRUE(Protobuf.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(TEST2), writer_finish(0));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TEST2, g_buf, sizeof(TEST2));

    reader_open(0, TEST2, sizeof(TEST2), 0);
    TEST_ASSERT_TRUE(read_record(0));
    TEST_ASSERT_EQUAL_UINT32(2u, Protobuf.record.field_number);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PROTOBUF_WT_LEN, Protobuf.record.wire_type);
    TEST_ASSERT_EQUAL_UINT(7u, Protobuf.record.len);
    TEST_ASSERT_EQUAL_MEMORY("testing", Protobuf.record.data, 7);
    // The payload is pointed at where it lies, not copied out.
    TEST_ASSERT_EQUAL_PTR(TEST2 + 2, Protobuf.record.data);

    // "Embedded Messages": `message Test3 { optional Test1 c = 3; }` with c.a = 150 is
    // `1a 03 08 96 01`. The inner message is encoded into its own row and added as a LEN payload.
    static const uint8_t TEST3[5] = {0x1a, 0x03, 0x08, 0x96, 0x01};
    writer_open(1, g_inner, sizeof(g_inner));
    Protobuf.slot = 1;
    Protobuf.tag.field_number = 1;
    Protobuf.value.u64 = 150;
    Protobuf.write_uint64(protocore_protobuf_span());
    const size_t inner_len = writer_finish(1);
    TEST_ASSERT_EQUAL_UINT(3u, inner_len);

    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.slot = 0;
    Protobuf.tag.field_number = 3;
    Protobuf.value.data = g_inner;
    Protobuf.value.len = inner_len;
    Protobuf.write_bytes(protocore_protobuf_span());
    TEST_ASSERT_TRUE(Protobuf.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(TEST3), writer_finish(0));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(TEST3, g_buf, sizeof(TEST3));

    // Reading it back nests two decoder rows: the outer LEN payload is the inner message.
    reader_open(0, TEST3, sizeof(TEST3), 0);
    TEST_ASSERT_TRUE(read_record(0));
    TEST_ASSERT_EQUAL_UINT32(3u, Protobuf.record.field_number);
    TEST_ASSERT_EQUAL_UINT(3u, Protobuf.record.len);
    const uint8_t *nested = Protobuf.record.data;
    reader_open(1, nested, 3, 0);
    TEST_ASSERT_TRUE(read_record(1));
    TEST_ASSERT_EQUAL_UINT32(1u, Protobuf.record.field_number);
    TEST_ASSERT_EQUAL_UINT64(150u, Protobuf.record.value);
}

// "Base 128 Varints": seven payload bits per octet, little-endian, the MSB the continuation bit. The
// document prints 1 as `01` and 150 as `96 01`; the rest here are that definition applied.
void test_base_128_varint(void)
{
    struct
    {
        uint64_t v;
        size_t len;
        uint8_t octets[10];
    } static const CASES[] = {
        {0u, 1, {0x00}},
        {1u, 1, {0x01}},
        {127u, 1, {0x7F}},                                                              // the widest one-octet value
        {128u, 2, {0x80, 0x01}},                                                        // 128 = 0*1 + 1*128
        {150u, 2, {0x96, 0x01}},                                                        // the document's own example
        {300u, 2, {0xAC, 0x02}},                                                        // 300 = 44 + 2*128
        {16383u, 2, {0xFF, 0x7F}},                                                      // the widest two-octet value
        {16384u, 3, {0x80, 0x80, 0x01}},                                                // the first three-octet value
        {UINT64_MAX, 10, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01}}, // ten octets
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        writer_open(0, g_buf, sizeof(g_buf));
        Protobuf.value.u64 = CASES[i].v;
        Protobuf.write_varint(protocore_protobuf_span());
        TEST_ASSERT_TRUE(Protobuf.ok);
        TEST_ASSERT_EQUAL_UINT(CASES[i].len, writer_finish(0));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(CASES[i].octets, g_buf, CASES[i].len);

        reader_open(0, g_buf, CASES[i].len, 0);
        Protobuf.slot = 0;
        Protobuf.read_varint(protocore_protobuf_span());
        TEST_ASSERT_TRUE(Protobuf.ok);
        TEST_ASSERT_EQUAL_UINT64(CASES[i].v, Protobuf.u64);
        TEST_ASSERT_EQUAL_UINT(CASES[i].len, Protobuf.n);
    }
    TEST_ASSERT_EQUAL_INT(10, PROTOCORE_PROTOBUF_VARINT_MAX);
}

// The tag "is encoded as a varint formed from the field number and the wire type via the formula
// (field_number << 3) | wire_type", and the wire type table gives VARINT 0, I64 1, LEN 2, SGROUP 3,
// EGROUP 4, I32 5.
void test_tag_formula_and_wire_type_ids(void)
{
    TEST_ASSERT_EQUAL_INT(0, PROTOCORE_PROTOBUF_WT_VARINT);
    TEST_ASSERT_EQUAL_INT(1, PROTOCORE_PROTOBUF_WT_I64);
    TEST_ASSERT_EQUAL_INT(2, PROTOCORE_PROTOBUF_WT_LEN);
    TEST_ASSERT_EQUAL_INT(3, PROTOCORE_PROTOBUF_WT_SGROUP);
    TEST_ASSERT_EQUAL_INT(4, PROTOCORE_PROTOBUF_WT_EGROUP);
    TEST_ASSERT_EQUAL_INT(5, PROTOCORE_PROTOBUF_WT_I32);

    struct
    {
        uint32_t field;
        uint8_t wt;
        size_t len;
        uint8_t octets[3];
    } static const CASES[] = {
        {1, PROTOCORE_PROTOBUF_WT_VARINT, 1, {0x08}},       // (1 << 3) | 0
        {2, PROTOCORE_PROTOBUF_WT_LEN, 1, {0x12}},          // (2 << 3) | 2
        {3, PROTOCORE_PROTOBUF_WT_LEN, 1, {0x1a}},          // (3 << 3) | 2
        {1, PROTOCORE_PROTOBUF_WT_I64, 1, {0x09}},          // (1 << 3) | 1
        {1, PROTOCORE_PROTOBUF_WT_I32, 1, {0x0d}},          // (1 << 3) | 5
        {15, PROTOCORE_PROTOBUF_WT_VARINT, 1, {0x78}},      // the widest one-octet tag
        {16, PROTOCORE_PROTOBUF_WT_VARINT, 2, {0x80, 0x01}} // (16 << 3) = 128, so two octets
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        writer_open(0, g_buf, sizeof(g_buf));
        Protobuf.tag.field_number = CASES[i].field;
        Protobuf.tag.wire_type = CASES[i].wt;
        Protobuf.write_tag(protocore_protobuf_span());
        TEST_ASSERT_TRUE(Protobuf.ok);
        TEST_ASSERT_EQUAL_UINT(CASES[i].len, writer_finish(0));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(CASES[i].octets, g_buf, CASES[i].len);
    }
}

// The document's ZigZag table: 0 -> 0, -1 -> 1, 1 -> 2, -2 -> 3, 0x7fffffff -> 0xfffffffe and
// -0x80000000 -> 0xffffffff, with sint64 following the same rule at 64 bits.
void test_zigzag_table(void)
{
    struct
    {
        int64_t signed_value;
        uint64_t encoded;
    } static const TABLE[] = {
        {0, 0u},
        {-1, 1u},
        {1, 2u},
        {-2, 3u},
        {2147483647LL, 4294967294u},  // 0x7fffffff -> 0xfffffffe
        {-2147483648LL, 4294967295u}, // -0x80000000 -> 0xffffffff
        {INT64_MAX, UINT64_MAX - 1u},
        {INT64_MIN, UINT64_MAX},
    };
    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++)
    {
        // The encoder writes the ZigZag varint behind a tag; the bare varint is what the table names.
        writer_open(0, g_buf, sizeof(g_buf));
        Protobuf.value.u64 = TABLE[i].encoded;
        Protobuf.write_varint(protocore_protobuf_span());
        const size_t bare = writer_finish(0);

        writer_open(0, g_inner, sizeof(g_inner));
        Protobuf.tag.field_number = 1;
        Protobuf.value.i64 = TABLE[i].signed_value;
        Protobuf.write_sint64(protocore_protobuf_span());
        TEST_ASSERT_TRUE(Protobuf.ok);
        TEST_ASSERT_EQUAL_UINT(1u + bare, writer_finish(0));
        TEST_ASSERT_EQUAL_HEX8(0x08, g_inner[0]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(g_buf, g_inner + 1, bare);

        // And the decode maps it back.
        Protobuf.value.u64 = TABLE[i].encoded;
        Protobuf.zigzag64(protocore_protobuf_span());
        TEST_ASSERT_TRUE(Protobuf.ok);
        TEST_ASSERT_EQUAL_INT64(TABLE[i].signed_value, Protobuf.i64);
    }

    // zigzag32 over the 32-bit rows of the same table.
    struct
    {
        int32_t signed_value;
        uint32_t encoded;
    } static const TABLE32[] = {
        {0, 0u}, {-1, 1u}, {1, 2u}, {-2, 3u}, {2147483647, 4294967294u}, {-2147483647 - 1, 4294967295u},
    };
    for (size_t i = 0; i < sizeof(TABLE32) / sizeof(TABLE32[0]); i++)
    {
        Protobuf.value.u32 = TABLE32[i].encoded;
        Protobuf.zigzag32(protocore_protobuf_span());
        TEST_ASSERT_TRUE(Protobuf.ok);
        TEST_ASSERT_EQUAL_INT32(TABLE32[i].signed_value, Protobuf.i32);
    }
}

// "int32, int64 ... use variable-length encoding" and a negative int64 is its two's complement, which
// always sets the top bit, so it takes the full ten octets. This is the difference from sint64.
void test_int64_is_two_s_complement_and_sint64_is_zigzag(void)
{
    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.tag.field_number = 1;
    Protobuf.value.i64 = -1;
    Protobuf.write_int64(protocore_protobuf_span());
    // -1 as a uint64 is 0xFFFFFFFFFFFFFFFF, ten varint octets behind a one-octet tag.
    TEST_ASSERT_EQUAL_UINT(11u, writer_finish(0));
    TEST_ASSERT_EQUAL_HEX8(0x08, g_buf[0]);
    static const uint8_t MINUS_ONE[10] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MINUS_ONE, g_buf + 1, 10);

    // The same -1 as sint64 is the ZigZag 1, so two octets whole.
    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.value.i64 = -1;
    Protobuf.write_sint64(protocore_protobuf_span());
    TEST_ASSERT_EQUAL_UINT(2u, writer_finish(0));
    TEST_ASSERT_EQUAL_HEX8(0x08, g_buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, g_buf[1]);
}

// "bool" is a VARINT of 0 or 1; I32 and I64 payloads are little-endian fixed-width.
void test_fixed_width_and_bool_payloads(void)
{
    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.tag.field_number = 1;
    Protobuf.value.flag = PROTO_TRUE;
    Protobuf.write_bool(protocore_protobuf_span());
    Protobuf.value.flag = PROTO_FALSE;
    Protobuf.write_bool(protocore_protobuf_span());
    TEST_ASSERT_EQUAL_UINT(4u, writer_finish(0));
    static const uint8_t BOOLS[4] = {0x08, 0x01, 0x08, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BOOLS, g_buf, 4);

    // fixed32 of 0x01020304 is `0d 04 03 02 01`: tag (1<<3)|5, then four little-endian octets.
    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.value.u32 = 0x01020304u;
    Protobuf.write_fixed32(protocore_protobuf_span());
    TEST_ASSERT_EQUAL_UINT(5u, writer_finish(0));
    static const uint8_t F32[5] = {0x0d, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(F32, g_buf, 5);
    reader_open(0, g_buf, 5, 0);
    TEST_ASSERT_TRUE(read_record(0));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PROTOBUF_WT_I32, Protobuf.record.wire_type);
    TEST_ASSERT_EQUAL_UINT64(0x01020304u, Protobuf.record.value);

    // fixed64 of 0x0102030405060708 is `09` then eight little-endian octets.
    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.value.u64 = 0x0102030405060708ull;
    Protobuf.write_fixed64(protocore_protobuf_span());
    TEST_ASSERT_EQUAL_UINT(9u, writer_finish(0));
    static const uint8_t F64[9] = {0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(F64, g_buf, 9);
    reader_open(0, g_buf, 9, 0);
    TEST_ASSERT_TRUE(read_record(0));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PROTOBUF_WT_I64, Protobuf.record.wire_type);
    TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ull, Protobuf.record.value);
}

// float is an I32 record of the IEEE 754 binary32 bits and double an I64 record of the binary64 bits.
// 1.0f is sign 0, exponent 127 = 0x7F, mantissa 0, so 0x3F800000; 1.0 is 0x3FF0000000000000.
void test_float_and_double_bit_patterns(void)
{
    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.tag.field_number = 1;
    Protobuf.value.f32 = 1.0f;
    Protobuf.write_float(protocore_protobuf_span());
    TEST_ASSERT_EQUAL_UINT(5u, writer_finish(0));
    static const uint8_t ONE_F[5] = {0x0d, 0x00, 0x00, 0x80, 0x3F};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ONE_F, g_buf, 5);

    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.value.f64 = 1.0;
    Protobuf.write_double(protocore_protobuf_span());
    TEST_ASSERT_EQUAL_UINT(9u, writer_finish(0));
    static const uint8_t ONE_D[9] = {0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ONE_D, g_buf, 9);

    // The two bit readers name the same values back, compared as bit patterns so the check is exact.
    Protobuf.value.u32 = 0x3F800000u;
    Protobuf.float_bits(protocore_protobuf_span());
    TEST_ASSERT_TRUE(Protobuf.ok);
    uint32_t f32_bits = 0;
    const float got_f = Protobuf.f32;
    memcpy(&f32_bits, &got_f, sizeof(f32_bits));
    TEST_ASSERT_EQUAL_HEX32(0x3F800000u, f32_bits);

    Protobuf.value.u64 = 0x3FF0000000000000ull;
    Protobuf.double_bits(protocore_protobuf_span());
    TEST_ASSERT_TRUE(Protobuf.ok);
    uint64_t f64_bits = 0;
    const double got_d = Protobuf.f64;
    memcpy(&f64_bits, &got_d, sizeof(f64_bits));
    TEST_ASSERT_EQUAL_HEX64(0x3FF0000000000000ull, f64_bits);
}

// "Packed repeated fields ... are encoded as a single LEN record that contains each element
// concatenated": three int32 values 3, 270 and 86942 pack into `22 06 03 8E 02 9E A7 05`, where 0x22
// is (4 << 3) | 2 and 6 is the length of the concatenated varints.
void test_packed_repeated_field(void)
{
    // Each element's varint, from the Base 128 definition:
    //   3     = 0x03
    //   270   = 14 + 2*128           -> 0x8E 0x02
    //   86942 = 30 + 39*128 + 5*16384 -> 0x9E 0xA7 0x05
    writer_open(1, g_inner, sizeof(g_inner));
    Protobuf.slot = 1;
    Protobuf.value.u64 = 3;
    Protobuf.write_varint(protocore_protobuf_span());
    Protobuf.value.u64 = 270;
    Protobuf.write_varint(protocore_protobuf_span());
    Protobuf.value.u64 = 86942;
    Protobuf.write_varint(protocore_protobuf_span());
    const size_t packed = writer_finish(1);
    TEST_ASSERT_EQUAL_UINT(6u, packed);
    static const uint8_t ELEMENTS[6] = {0x03, 0x8E, 0x02, 0x9E, 0xA7, 0x05};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ELEMENTS, g_inner, 6);

    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.slot = 0;
    Protobuf.tag.field_number = 4;
    Protobuf.value.data = g_inner;
    Protobuf.value.len = packed;
    Protobuf.write_bytes(protocore_protobuf_span());
    TEST_ASSERT_EQUAL_UINT(8u, writer_finish(0));
    static const uint8_t PACKED[8] = {0x22, 0x06, 0x03, 0x8E, 0x02, 0x9E, 0xA7, 0x05};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(PACKED, g_buf, 8);

    // Unpacking walks the LEN payload as bare varints on a second decoder row.
    reader_open(0, PACKED, sizeof(PACKED), 0);
    TEST_ASSERT_TRUE(read_record(0));
    TEST_ASSERT_EQUAL_UINT32(4u, Protobuf.record.field_number);
    TEST_ASSERT_EQUAL_UINT(6u, Protobuf.record.len);
    reader_open(1, Protobuf.record.data, 6, 0);
    static const uint64_t WANT[3] = {3u, 270u, 86942u};
    for (size_t i = 0; i < 3; i++)
    {
        Protobuf.slot = 1;
        Protobuf.read_varint(protocore_protobuf_span());
        TEST_ASSERT_TRUE(Protobuf.ok);
        TEST_ASSERT_EQUAL_UINT64(WANT[i], Protobuf.u64);
    }
}

// "Groups are a deprecated feature that should not be used", and the wire type table names no 6 or 7,
// so a record carrying any of the four is refused rather than skipped by guesswork.
void test_group_and_unassigned_wire_types_are_refused(void)
{
    static const uint8_t WT[] = {PROTOCORE_PROTOBUF_WT_SGROUP, PROTOCORE_PROTOBUF_WT_EGROUP, 6, 7};
    for (size_t i = 0; i < sizeof(WT) / sizeof(WT[0]); i++)
    {
        const uint8_t rec[3] = {(uint8_t)((1u << 3) | WT[i]), 0x00, 0x00};
        reader_open(0, rec, sizeof(rec), 0);
        TEST_ASSERT_FALSE(read_record(0));
    }
}

// A payload the buffer does not carry to the end is refused, so a decode never points past the source.
void test_truncated_records_are_refused(void)
{
    // A LEN record claiming five octets with three behind it.
    static const uint8_t SHORT_LEN[5] = {0x12, 0x05, 'a', 'b', 'c'};
    reader_open(0, SHORT_LEN, sizeof(SHORT_LEN), 0);
    TEST_ASSERT_FALSE(read_record(0));

    // An I64 record with four octets behind it.
    static const uint8_t SHORT_I64[5] = {0x09, 0x01, 0x02, 0x03, 0x04};
    reader_open(0, SHORT_I64, sizeof(SHORT_I64), 0);
    TEST_ASSERT_FALSE(read_record(0));

    // An I32 record with three.
    static const uint8_t SHORT_I32[4] = {0x0d, 0x01, 0x02, 0x03};
    reader_open(0, SHORT_I32, sizeof(SHORT_I32), 0);
    TEST_ASSERT_FALSE(read_record(0));

    // A varint whose continuation bit is set on the last buffered octet.
    static const uint8_t OPEN_VARINT[3] = {0x08, 0x96, 0x80};
    reader_open(0, OPEN_VARINT, sizeof(OPEN_VARINT), 0);
    TEST_ASSERT_FALSE(read_record(0));

    // A varint that runs past the ten-octet maximum.
    static const uint8_t ELEVEN[11] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01};
    reader_open(0, ELEVEN, sizeof(ELEVEN), 0);
    Protobuf.slot = 0;
    Protobuf.read_varint(protocore_protobuf_span());
    TEST_ASSERT_FALSE(Protobuf.ok);

    // A source with no octets behind it.
    reader_open(0, NULL, 8, 0);
    TEST_ASSERT_FALSE(Protobuf.ok);
    TEST_ASSERT_FALSE(read_record(0));
}

// The row's error flag is sticky: the first append that will not fit poisons it, and the finish
// reports 0 rather than a message a peer would parse as a shorter but valid one.
void test_writer_fails_closed(void)
{
    uint8_t small[4];
    writer_open(0, small, sizeof(small));
    TEST_ASSERT_TRUE(Protobuf.ok);
    Protobuf.tag.field_number = 1;
    Protobuf.value.u64 = 150;
    Protobuf.write_uint64(protocore_protobuf_span()); // 3 octets, fits
    TEST_ASSERT_TRUE(Protobuf.ok);
    Protobuf.write_uint64(protocore_protobuf_span()); // another 3, does not
    TEST_ASSERT_FALSE(Protobuf.ok);
    Protobuf.value.u64 = 0;
    Protobuf.write_varint(protocore_protobuf_span()); // one octet would fit, but the row is poisoned
    TEST_ASSERT_FALSE(Protobuf.ok);
    TEST_ASSERT_EQUAL_UINT(0u, writer_finish(0));

    // A row with no buffer starts poisoned.
    writer_open(0, NULL, 64);
    TEST_ASSERT_FALSE(Protobuf.ok);
    TEST_ASSERT_EQUAL_UINT(0u, writer_finish(0));

    // A LEN record with a length but no octets behind it, and a string with no text.
    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.value.data = NULL;
    Protobuf.value.len = 4;
    Protobuf.write_bytes(protocore_protobuf_span());
    TEST_ASSERT_FALSE(Protobuf.ok);
    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.value.text = NULL;
    Protobuf.write_string(protocore_protobuf_span());
    TEST_ASSERT_FALSE(Protobuf.ok);
    TEST_ASSERT_EQUAL_UINT(0u, writer_finish(0));

    // A slot past the pool touches nothing.
    Protobuf.slot = PROTOCORE_PROTOBUF_SLOTS;
    Protobuf.writer.buf = g_buf;
    Protobuf.writer.cap = sizeof(g_buf);
    Protobuf.writer_open(protocore_protobuf_span());
    TEST_ASSERT_FALSE(Protobuf.ok);
    Protobuf.reader_open(protocore_protobuf_span());
    TEST_ASSERT_FALSE(Protobuf.ok);
    Protobuf.slot = 0;
}

// A message of several records walks record by record, each leaving the cursor where the next begins.
void test_a_message_walks_record_by_record(void)
{
    writer_open(0, g_buf, sizeof(g_buf));
    Protobuf.tag.field_number = 1;
    Protobuf.value.u64 = 150;
    Protobuf.write_uint64(protocore_protobuf_span());
    Protobuf.tag.field_number = 2;
    Protobuf.value.text = "testing";
    Protobuf.write_string(protocore_protobuf_span());
    Protobuf.tag.field_number = 3;
    Protobuf.value.flag = PROTO_TRUE;
    Protobuf.write_bool(protocore_protobuf_span());
    const size_t total = writer_finish(0);
    TEST_ASSERT_EQUAL_UINT(3u + 9u + 2u, total);

    reader_open(0, g_buf, total, 0);
    TEST_ASSERT_TRUE(read_record(0));
    TEST_ASSERT_EQUAL_UINT32(1u, Protobuf.record.field_number);
    TEST_ASSERT_EQUAL_UINT64(150u, Protobuf.record.value);
    TEST_ASSERT_EQUAL_UINT(3u, Protobuf.n);
    TEST_ASSERT_TRUE(read_record(0));
    TEST_ASSERT_EQUAL_UINT32(2u, Protobuf.record.field_number);
    TEST_ASSERT_EQUAL_MEMORY("testing", Protobuf.record.data, 7);
    TEST_ASSERT_EQUAL_UINT(12u, Protobuf.n);
    TEST_ASSERT_TRUE(read_record(0));
    TEST_ASSERT_EQUAL_UINT32(3u, Protobuf.record.field_number);
    TEST_ASSERT_EQUAL_UINT64(1u, Protobuf.record.value);
    TEST_ASSERT_EQUAL_UINT(total, Protobuf.n);
    TEST_ASSERT_FALSE(read_record(0));

    // An open seated past the start begins at that offset, clamped to the length.
    reader_open(0, g_buf, total, 3);
    TEST_ASSERT_TRUE(Protobuf.ok);
    TEST_ASSERT_EQUAL_UINT(3u, Protobuf.n);
    TEST_ASSERT_TRUE(read_record(0));
    TEST_ASSERT_EQUAL_UINT32(2u, Protobuf.record.field_number);
    reader_open(0, g_buf, total, total + 99);
    TEST_ASSERT_EQUAL_UINT(total, Protobuf.n);
    TEST_ASSERT_FALSE(read_record(0));
}
