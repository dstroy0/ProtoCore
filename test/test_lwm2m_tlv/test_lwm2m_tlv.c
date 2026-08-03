// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the OMA LwM2M TLV codec (services/iot/lwm2m): the writer (raw + typed value
// helpers), the cursor reader, and integer value decoding. Byte vectors checked against
// the LwM2M TLV type-byte layout. Pure host tests.

#include "services/iot/lwm2m/lwm2m_tlv.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// A single-octet integer Resource: type 0xC1, id, value.
void test_write_int_1byte()
{
    uint8_t buf[16];
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    pc_lwm2m_tlv_write_int(&w, 3, 42);
    size_t n = pc_lwm2m_tlv_finish(&w);
    const uint8_t expect[] = {0xC1, 0x03, 0x2A};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// A value > 127 takes two octets (type length field = 2).
void test_write_int_2byte()
{
    uint8_t buf[16];
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    pc_lwm2m_tlv_write_int(&w, 1, 300);
    size_t n = pc_lwm2m_tlv_finish(&w);
    const uint8_t expect[] = {0xC2, 0x01, 0x01, 0x2C}; // 300 = 0x012C
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// The spec's manufacturer example: resource 0 = "Open Mobile Alliance" -> C8 00 14 ...
void test_write_string_8bit_length()
{
    uint8_t buf[64];
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    pc_lwm2m_tlv_write_string(&w, 0, "Open Mobile Alliance");
    size_t n = pc_lwm2m_tlv_finish(&w);
    TEST_ASSERT_EQUAL_HEX8(0xC8, buf[0]); // Resource, 8-bit id, 8-bit length
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]); // id 0
    TEST_ASSERT_EQUAL_HEX8(0x14, buf[2]); // length 20
    TEST_ASSERT_EQUAL_MEMORY("Open Mobile Alliance", buf + 3, 20);
    TEST_ASSERT_EQUAL_size_t(23, n);
}

// A 16-bit identifier sets type bit 5 and writes two id octets.
void test_write_16bit_id()
{
    uint8_t buf[16];
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    pc_lwm2m_tlv_write_int(&w, 0x0405, 1);
    size_t n = pc_lwm2m_tlv_finish(&w);
    const uint8_t expect[] = {0xE1, 0x04, 0x05, 0x01}; // 0xC0 | 0x20(id16) | len 1
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

void test_round_trip_and_value_int()
{
    uint8_t buf[64];
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    pc_lwm2m_tlv_write_int(&w, 0, 42);
    pc_lwm2m_tlv_write_int(&w, 1, 300);
    pc_lwm2m_tlv_write_int(&w, 2, -1);
    pc_lwm2m_tlv_write_bool(&w, 3, PROTO_TRUE);
    pc_lwm2m_tlv_write_string(&w, 4, "hi");
    size_t total = pc_lwm2m_tlv_finish(&w);
    TEST_ASSERT_GREATER_THAN(0, (int)total);

    size_t pos = 0;
    Lwm2mTlv t;
    int64_t v;

    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(buf, total, &pos, &t));
    TEST_ASSERT_EQUAL_UINT8(LWM2M_TLV_RESOURCE, t.id_type);
    TEST_ASSERT_EQUAL_UINT16(0, t.id);
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_value_int(t.value, t.value_len, &v));
    TEST_ASSERT_EQUAL_INT64(42, v);

    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(buf, total, &pos, &t));
    TEST_ASSERT_EQUAL_UINT16(1, t.id);
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_value_int(t.value, t.value_len, &v));
    TEST_ASSERT_EQUAL_INT64(300, v);

    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(buf, total, &pos, &t));
    TEST_ASSERT_EQUAL_UINT16(2, t.id);
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_value_int(t.value, t.value_len, &v));
    TEST_ASSERT_EQUAL_INT64(-1, v); // negative sign-extends

    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(buf, total, &pos, &t));
    TEST_ASSERT_EQUAL_UINT16(3, t.id);
    TEST_ASSERT_EQUAL_size_t(1, t.value_len);
    TEST_ASSERT_EQUAL_HEX8(0x01, t.value[0]);

    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(buf, total, &pos, &t));
    TEST_ASSERT_EQUAL_UINT16(4, t.id);
    TEST_ASSERT_EQUAL_MEMORY("hi", t.value, 2);

    TEST_ASSERT_FALSE(pc_lwm2m_tlv_read(buf, total, &pos, &t)); // end
}

// An Object Instance wrapping nested Resource TLVs (the writer emits raw nested bytes).
void test_object_instance_nested()
{
    uint8_t inner[16];
    Lwm2mTlvWriter iw;
    pc_lwm2m_tlv_init(&iw, inner, sizeof(inner));
    pc_lwm2m_tlv_write_int(&iw, 0, 1);
    pc_lwm2m_tlv_write_int(&iw, 1, 2);
    size_t inner_len = pc_lwm2m_tlv_finish(&iw);

    uint8_t buf[32];
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    pc_lwm2m_tlv_write(&w, LWM2M_TLV_OBJECT_INSTANCE, 0, inner, inner_len);
    size_t total = pc_lwm2m_tlv_finish(&w);

    size_t pos = 0;
    Lwm2mTlv t;
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(buf, total, &pos, &t));
    TEST_ASSERT_EQUAL_UINT8(LWM2M_TLV_OBJECT_INSTANCE, t.id_type);
    TEST_ASSERT_EQUAL_size_t(inner_len, t.value_len);
    // The Object Instance value is itself a TLV stream.
    size_t ipos = 0;
    Lwm2mTlv it;
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(t.value, t.value_len, &ipos, &it));
    TEST_ASSERT_EQUAL_UINT16(0, it.id);
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(t.value, t.value_len, &ipos, &it));
    TEST_ASSERT_EQUAL_UINT16(1, it.id);
}

// A value past 255 octets uses the 16-bit length field (length-type 2); round-trips.
void test_write_16bit_length()
{
    uint8_t val[300];
    for (size_t i = 0; i < sizeof(val); i++)
    {
        val[i] = (uint8_t)i;
    }

    uint8_t buf[320];
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_write(&w, LWM2M_TLV_RESOURCE, 5, val, sizeof(val)));
    size_t total = pc_lwm2m_tlv_finish(&w);

    TEST_ASSERT_EQUAL_HEX8(0xD0, buf[0]); // Resource, 8-bit id, 16-bit length (2 << 3)
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[1]); // id 5
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[2]); // 300 = 0x012C, high octet
    TEST_ASSERT_EQUAL_HEX8(0x2C, buf[3]); // low octet

    size_t pos = 0;
    Lwm2mTlv t;
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(buf, total, &pos, &t));
    TEST_ASSERT_EQUAL_UINT16(5, t.id);
    TEST_ASSERT_EQUAL_size_t(300, t.value_len);
    TEST_ASSERT_EQUAL_MEMORY(val, t.value, sizeof(val));
}

// The reader decodes the 24-bit length field (length-type 3). A real 24-bit length
// implies a >64KB value, so the form is exercised here with a short hand-built vector.
void test_read_24bit_length()
{
    const uint8_t tlv[] = {0xD8, 0x07, 0x00, 0x00, 0x03, 0xAA, 0xBB, 0xCC}; // lentype 3, len 3
    size_t pos = 0;
    Lwm2mTlv t;
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(tlv, sizeof(tlv), &pos, &t));
    TEST_ASSERT_EQUAL_UINT8(LWM2M_TLV_RESOURCE, t.id_type);
    TEST_ASSERT_EQUAL_UINT16(7, t.id);
    TEST_ASSERT_EQUAL_size_t(3, t.value_len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, t.value[0]);
    TEST_ASSERT_EQUAL_size_t(sizeof(tlv), pos);

    // A 24-bit length that overruns the buffer is rejected.
    const uint8_t trunc[] = {0xD8, 0x07, 0x00, 0x00, 0x10, 0xAA}; // says 16 octets, only 1 follows
    pos = 0;
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_read(trunc, sizeof(trunc), &pos, &t));
}

// Integers wider than two octets decode at the 4- and 8-byte widths, signed.
void test_value_int_4_and_8_byte()
{
    uint8_t buf[64];
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    pc_lwm2m_tlv_write_int(&w, 0, 70000);         // > int16 -> 4 octets
    pc_lwm2m_tlv_write_int(&w, 1, 5000000000LL);  // > int32 -> 8 octets
    pc_lwm2m_tlv_write_int(&w, 2, -5000000000LL); // 8 octets, negative sign-extends
    size_t total = pc_lwm2m_tlv_finish(&w);

    size_t pos = 0;
    Lwm2mTlv t;
    int64_t v;

    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(buf, total, &pos, &t));
    TEST_ASSERT_EQUAL_size_t(4, t.value_len);
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_value_int(t.value, t.value_len, &v));
    TEST_ASSERT_EQUAL_INT64(70000, v);

    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(buf, total, &pos, &t));
    TEST_ASSERT_EQUAL_size_t(8, t.value_len);
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_value_int(t.value, t.value_len, &v));
    TEST_ASSERT_EQUAL_INT64(5000000000LL, v);

    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(buf, total, &pos, &t));
    TEST_ASSERT_EQUAL_size_t(8, t.value_len);
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_value_int(t.value, t.value_len, &v));
    TEST_ASSERT_EQUAL_INT64(-5000000000LL, v);
}

// A zero-length value is the inline length-type with length 0 and round-trips empty.
void test_zero_length_value()
{
    uint8_t buf[8];
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_write(&w, LWM2M_TLV_RESOURCE, 9, NULL, 0));
    size_t total = pc_lwm2m_tlv_finish(&w);
    const uint8_t expect[] = {0xC0, 0x09}; // Resource, 8-bit id, inline length 0
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), total);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, total);

    size_t pos = 0;
    Lwm2mTlv t;
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(buf, total, &pos, &t));
    TEST_ASSERT_EQUAL_UINT16(9, t.id);
    TEST_ASSERT_EQUAL_size_t(0, t.value_len);
}

void test_overflow_and_malformed()
{
    uint8_t small[2];
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, small, sizeof(small));
    pc_lwm2m_tlv_write_string(&w, 0, "too big for two bytes");
    TEST_ASSERT_EQUAL_size_t(0, pc_lwm2m_tlv_finish(&w));

    // A TLV declaring more value than is buffered.
    const uint8_t trunc[] = {0xC8, 0x00, 0x14, 'a', 'b'}; // says length 20, only 2 follow
    size_t pos = 0;
    Lwm2mTlv t;
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_read(trunc, sizeof(trunc), &pos, &t));

    int64_t v;
    const uint8_t three[] = {1, 2, 3};
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_value_int(three, 3, &v)); // 3 octets is not a valid int width
}

// Writer argument/length rejections: null writer, a length with a null value, the
// 24-bit length-field branch, a value too large for any length field, and a null string.
void test_write_error_paths()
{
    uint8_t buf[16];
    Lwm2mTlvWriter w;
    uint8_t dummy[1] = {0};

    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_write(NULL, LWM2M_TLV_RESOURCE, 0, dummy, 1)); // null writer
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_write(&w, LWM2M_TLV_RESOURCE, 0, NULL, 4));    // len but null value

    // A value_len that needs the 24-bit length field: the type byte is built (length-type 3),
    // then the capacity check rejects it, so the (huge) value is never read.
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_write(&w, LWM2M_TLV_RESOURCE, 0, dummy, 0x10000));
    // A value_len too large for even the 24-bit length field.
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_write(&w, LWM2M_TLV_RESOURCE, 0, dummy, 0x1000000));

    TEST_ASSERT_FALSE(pc_lwm2m_tlv_write_string(&w, 0, NULL)); // null string
}

// The IEEE-754 float writer (big-endian 8 octets) round-trips through the reader.
void test_write_float_roundtrip()
{
    uint8_t buf[16];
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_write_float(&w, 6, 3.5));
    size_t total = pc_lwm2m_tlv_finish(&w);

    size_t pos = 0;
    Lwm2mTlv t;
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(buf, total, &pos, &t));
    TEST_ASSERT_EQUAL_UINT16(6, t.id);
    TEST_ASSERT_EQUAL_size_t(8, t.value_len);
    uint64_t bits = 0;
    for (int i = 0; i < 8; i++)
    {
        bits = (bits << 8) | t.value[i];
    }
    double back;
    memcpy(&back, &bits, 8);
    TEST_ASSERT_TRUE(back == 3.5); // 3.5 is exactly representable
}

// The reader's 16-bit-id decode plus its truncation rejects.
void test_read_id16_and_truncation()
{
    // 16-bit-id resource: type 0xE1 (id16 flag + inline len 1), id 0x0405, value 0x07.
    const uint8_t tlv16[] = {0xE1, 0x04, 0x05, 0x07};
    size_t pos = 0;
    Lwm2mTlv t;
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_read(tlv16, sizeof(tlv16), &pos, &t));
    TEST_ASSERT_EQUAL_UINT16(0x0405, t.id);
    TEST_ASSERT_EQUAL_size_t(1, t.value_len);
    TEST_ASSERT_EQUAL_HEX8(0x07, t.value[0]);

    // id16 flag set but only the type byte is present -> not enough for the id.
    const uint8_t just_type[] = {0xE1};
    pos = 0;
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_read(just_type, sizeof(just_type), &pos, &t));

    // An 8-bit length-type header whose length octet is missing.
    const uint8_t trunc_len[] = {0xC8, 0x00}; // lentype 1, id 0x00, then no length octet
    pos = 0;
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_read(trunc_len, sizeof(trunc_len), &pos, &t));
}

// write_bool with v == false takes the ternary's else branch (byte 0x00).
void test_write_bool_false()
{
    uint8_t buf[16];
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_write_bool(&w, 5, PROTO_FALSE));
    size_t total = pc_lwm2m_tlv_finish(&w);
    const uint8_t expect[] = {0xC1, 0x05, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), total);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, total);
}

// Once a write overflows and latches w->error, a later write on the same writer
// short-circuits on the already-set error flag rather than re-checking capacity.
void test_write_after_error_latched()
{
    uint8_t buf[2];
    Lwm2mTlvWriter w;
    pc_lwm2m_tlv_init(&w, buf, sizeof(buf));
    uint8_t dummy[4] = {0, 0, 0, 0};
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_write(&w, LWM2M_TLV_RESOURCE, 0, dummy, 4)); // overflows, latches error
    // This write would fit in the 2-byte buffer on its own, but the writer is
    // already latched into the error state, so it is rejected without another
    // capacity check.
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_write(&w, LWM2M_TLV_RESOURCE, 1, NULL, 0));
}

// pc_lwm2m_tlv_read rejects a null buffer, null pos, or null out independently.
void test_read_null_args()
{
    const uint8_t buf[] = {0xC1, 0x00, 0x01};
    size_t pos = 0;
    Lwm2mTlv t;
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_read(NULL, sizeof(buf), &pos, &t));
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_read(buf, sizeof(buf), NULL, &t));
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_read(buf, sizeof(buf), &pos, NULL));
}

// pc_lwm2m_tlv_value_int rejects a null value pointer, and tolerates a null
// out pointer on an otherwise-valid decode (result computed but not stored).
void test_value_int_null_args()
{
    int64_t out;
    TEST_ASSERT_FALSE(pc_lwm2m_tlv_value_int(NULL, 1, &out));

    const uint8_t v1[] = {0x2A};
    TEST_ASSERT_TRUE(pc_lwm2m_tlv_value_int(v1, 1, NULL));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_write_int_1byte);
    RUN_TEST(test_write_int_2byte);
    RUN_TEST(test_write_string_8bit_length);
    RUN_TEST(test_write_16bit_id);
    RUN_TEST(test_round_trip_and_value_int);
    RUN_TEST(test_object_instance_nested);
    RUN_TEST(test_write_16bit_length);
    RUN_TEST(test_read_24bit_length);
    RUN_TEST(test_value_int_4_and_8_byte);
    RUN_TEST(test_zero_length_value);
    RUN_TEST(test_overflow_and_malformed);
    RUN_TEST(test_write_error_paths);
    RUN_TEST(test_write_float_roundtrip);
    RUN_TEST(test_read_id16_and_truncation);
    RUN_TEST(test_write_bool_false);
    RUN_TEST(test_write_after_error_latched);
    RUN_TEST(test_read_null_args);
    RUN_TEST(test_value_int_null_args);
    return UNITY_END();
}
