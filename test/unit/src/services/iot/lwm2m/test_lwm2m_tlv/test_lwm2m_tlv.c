// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the OMA LwM2M TLV codec (services/iot/lwm2m/lwm2m_tlv.h).
//
// The governing standard is OMA-TS-LightweightM2M_Core-V1_2-20201110-A sec 7.4.5, not an IETF RFC.
// That section publishes both the Type byte layout (Table 7.4.5.-1) and complete worked payloads with
// their octets written out in hex, so nearly every expectation below is copied from the document.
//
// test_published_device_object_entries and test_published_access_control_payload are the load-bearing
// pair: the first reproduces entries of the sec 7.4.5.1 "Read /3/0" payload octet for octet, and the
// second parses the whole 38-octet sec 7.4.5.2 B) "Read /2" payload, including its 16-bit Identifier.
// Note the document's own sec 7.4.5.1 hex dump drops one octet of the Model Number string, so that
// entry is taken from Table 7.4.5.1.-1's Length column (0x16, 22 octets) rather than from the dump.

#include "services/iot/lwm2m/lwm2m_tlv.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_buf[512];

// One decoded entry, copied off the handle so a nested parse can rebind the reader cursor.
typedef struct
{
    Lwm2mTlvIdType id_type;
    uint16_t id;
    const uint8_t *value;
    size_t len;
} Entry;

static void open_sink(size_t cap)
{
    Lwm2mTlv.sink.buf = g_buf;
    Lwm2mTlv.sink.cap = cap;
    Lwm2mTlv.open(protocore_lwm2m_tlv_span());
}

static void write_opaque(Lwm2mTlvIdType id_type, uint16_t id, const void *value, size_t len)
{
    Lwm2mTlv.hdr.id_type = id_type;
    Lwm2mTlv.hdr.id = id;
    Lwm2mTlv.val.opaque = (const uint8_t *)value;
    Lwm2mTlv.val.len = len;
    Lwm2mTlv.write(protocore_lwm2m_tlv_span());
}

static void write_string(Lwm2mTlvIdType id_type, uint16_t id, const char *s)
{
    Lwm2mTlv.hdr.id_type = id_type;
    Lwm2mTlv.hdr.id = id;
    Lwm2mTlv.val.string_value = s;
    Lwm2mTlv.write_string(protocore_lwm2m_tlv_span());
}

static void write_integer(Lwm2mTlvIdType id_type, uint16_t id, int64_t v)
{
    Lwm2mTlv.hdr.id_type = id_type;
    Lwm2mTlv.hdr.id = id;
    Lwm2mTlv.val.integer_value = v;
    Lwm2mTlv.write_integer(protocore_lwm2m_tlv_span());
}

static size_t finish(void)
{
    Lwm2mTlv.finish(protocore_lwm2m_tlv_span());
    return Lwm2mTlv.n;
}

// Decode every entry of [buf, buf+len) into out, and report how many there were.
static size_t walk(const uint8_t *buf, size_t len, Entry *out, size_t max)
{
    Lwm2mTlv.source.buf = buf;
    Lwm2mTlv.source.len = len;
    Lwm2mTlv.parse(protocore_lwm2m_tlv_span());
    TEST_ASSERT_TRUE(Lwm2mTlv.ok);
    size_t n = 0;
    for (;;)
    {
        Lwm2mTlv.next(protocore_lwm2m_tlv_span());
        if (!Lwm2mTlv.ok)
        {
            break;
        }
        TEST_ASSERT_TRUE(n < max);
        out[n].id_type = Lwm2mTlv.hdr.id_type;
        out[n].id = Lwm2mTlv.hdr.id;
        out[n].value = Lwm2mTlv.val.opaque;
        out[n].len = Lwm2mTlv.val.len;
        n++;
    }
    return n;
}

static int64_t as_integer(const Entry *e)
{
    Lwm2mTlv.val.opaque = e->value;
    Lwm2mTlv.val.len = e->len;
    Lwm2mTlv.value_integer(protocore_lwm2m_tlv_span());
    TEST_ASSERT_TRUE(Lwm2mTlv.ok);
    return Lwm2mTlv.val.integer_value;
}

// LwM2M Core sec 7.4.5.1, the "Read /3/0" Device Object payload. Each expected octet string below is
// the row's Type Byte, ID Byte(s), Length Byte(s) and Value as Table 7.4.5.1.-1 prints them.
void test_published_device_object_entries(void)
{
    // Manufacturer: 0b11 0 01 000 = 0xC8, ID 0x00, Length 0x14, "Open Mobile Alliance".
    open_sink(sizeof(g_buf));
    write_string(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x00, "Open Mobile Alliance");
    TEST_ASSERT_TRUE(Lwm2mTlv.ok);
    TEST_ASSERT_EQUAL_UINT(23u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\xC8\x00\x14"
                             "Open Mobile Alliance",
                             g_buf, 23);

    // Model Number: 0xC8, ID 0x01, Length 0x16 (22 octets).
    open_sink(sizeof(g_buf));
    write_string(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x01, "Lightweight M2M Client");
    TEST_ASSERT_EQUAL_UINT(25u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\xC8\x01\x16"
                             "Lightweight M2M Client",
                             g_buf, 25);

    // Serial Number: 0xC8, ID 0x02, Length 0x09, "345000123".
    open_sink(sizeof(g_buf));
    write_string(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x02, "345000123");
    TEST_ASSERT_EQUAL_UINT(12u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\xC8\x02\x09"
                             "345000123",
                             g_buf, 12);

    // Firmware Version: 0b11 0 00 011 = 0xC3, ID 0x03, no Length field, "1.0".
    open_sink(sizeof(g_buf));
    write_string(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x03, "1.0");
    TEST_ASSERT_EQUAL_UINT(5u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\xC3\x03"
                             "1.0",
                             g_buf, 5);

    // Available Power Sources, a multiple Resource: 0b10 0 00 110 = 0x86, ID 0x06, whose Value is the
    // two Resource Instances 0b01 0 00 001 = 0x41 carrying 0x01 and 0x05.
    open_sink(sizeof(g_buf));
    write_integer(LWM2M_TLV_RESOURCE_INSTANCE, 0x00, 0x01);
    write_integer(LWM2M_TLV_RESOURCE_INSTANCE, 0x01, 0x05);
    TEST_ASSERT_EQUAL_UINT(6u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\x41\x00\x01\x41\x01\x05", g_buf, 6);
    uint8_t inner[6];
    memcpy(inner, g_buf, 6);
    open_sink(sizeof(g_buf));
    write_opaque(LWM2M_TLV_MULTIPLE_RESOURCE, 0x06, inner, 6);
    TEST_ASSERT_EQUAL_UINT(8u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\x86\x06\x41\x00\x01\x41\x01\x05", g_buf, 8);

    // Power Source Voltage: 0b10 0 01 000 = 0x88, ID 0x07, Length 0x08, over the two 16-bit Resource
    // Instances 0b01 0 00 010 = 0x42 carrying 0x0ED8 and 0x1388.
    open_sink(sizeof(g_buf));
    write_integer(LWM2M_TLV_RESOURCE_INSTANCE, 0x00, 0x0ED8);
    write_integer(LWM2M_TLV_RESOURCE_INSTANCE, 0x01, 0x1388);
    TEST_ASSERT_EQUAL_UINT(8u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\x42\x00\x0E\xD8\x42\x01\x13\x88", g_buf, 8);
    uint8_t volts[8];
    memcpy(volts, g_buf, 8);
    open_sink(sizeof(g_buf));
    write_opaque(LWM2M_TLV_MULTIPLE_RESOURCE, 0x07, volts, 8);
    TEST_ASSERT_EQUAL_UINT(11u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\x88\x07\x08\x42\x00\x0E\xD8\x42\x01\x13\x88", g_buf, 11);

    // Battery Level: 0b11 0 00 001 = 0xC1, ID 0x09, 0x64. Memory Free: 0xC1, ID 0x0A, 0x0F.
    open_sink(sizeof(g_buf));
    write_integer(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x09, 0x64);
    write_integer(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x0A, 0x0F);
    TEST_ASSERT_EQUAL_UINT(6u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\xC1\x09\x64\xC1\x0A\x0F", g_buf, 6);

    // Current Time: 0b11 0 00 100 = 0xC4, ID 0x0D, the 32-bit Integer 0x5182428F.
    open_sink(sizeof(g_buf));
    write_integer(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x0D, 0x5182428F);
    TEST_ASSERT_EQUAL_UINT(6u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\xC4\x0D\x51\x82\x42\x8F", g_buf, 6);

    // UTC Offset: 0b11 0 00 110 = 0xC6, ID 0x0E, "+02:00". Supported Binding: 0xC1, ID 0x10, "U".
    open_sink(sizeof(g_buf));
    write_string(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x0E, "+02:00");
    write_string(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x10, "U");
    TEST_ASSERT_EQUAL_UINT(11u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\xC6\x0E"
                             "+02:00"
                             "\xC1\x10"
                             "U",
                             g_buf, 11);
}

// LwM2M Core sec 7.4.5.2 B), the 38-octet "Read /2" Access Control payload, verbatim.
static const uint8_t ACCESS_CONTROL[38] = {0x08, 0x00, 0x0E, 0xC1, 0x00, 0x01, 0xC1, 0x01, 0x00, 0x83, 0x02, 0x41, 0x7F,
                                           0x07, 0xC1, 0x03, 0x7F, 0x08, 0x02, 0x12, 0xC1, 0x00, 0x03, 0xC1, 0x01, 0x00,
                                           0x87, 0x02, 0x41, 0x7F, 0x07, 0x61, 0x01, 0x36, 0x01, 0xC1, 0x03, 0x7F};

// Every row of Table 7.4.5.2.-2 read back out of that payload, three levels deep.
void test_published_access_control_payload(void)
{
    Entry top[4];
    TEST_ASSERT_EQUAL_UINT(2u, walk(ACCESS_CONTROL, sizeof(ACCESS_CONTROL), top, 4));

    // Access Control Object Instance 0: 0b00 0 01 000, ID 0x00, Length 0x0E.
    TEST_ASSERT_EQUAL_INT(LWM2M_TLV_OBJECT_INSTANCE, top[0].id_type);
    TEST_ASSERT_EQUAL_UINT16(0x00, top[0].id);
    TEST_ASSERT_EQUAL_UINT(0x0Eu, top[0].len);
    // Access Control Object Instance 2: ID 0x02, Length 0x12.
    TEST_ASSERT_EQUAL_INT(LWM2M_TLV_OBJECT_INSTANCE, top[1].id_type);
    TEST_ASSERT_EQUAL_UINT16(0x02, top[1].id);
    TEST_ASSERT_EQUAL_UINT(0x12u, top[1].len);

    uint8_t inst0[0x0E];
    uint8_t inst2[0x12];
    memcpy(inst0, top[0].value, sizeof(inst0));
    memcpy(inst2, top[1].value, sizeof(inst2));

    // Instance 0: Object ID 0x01, Object Instance ID 0x00, an ACL multiple Resource, then Owner 0x7F.
    Entry r[8];
    TEST_ASSERT_EQUAL_UINT(4u, walk(inst0, sizeof(inst0), r, 8));
    TEST_ASSERT_EQUAL_INT(LWM2M_TLV_RESOURCE_WITH_VALUE, r[0].id_type);
    TEST_ASSERT_EQUAL_UINT16(0x00, r[0].id);
    TEST_ASSERT_EQUAL_INT64(0x01, as_integer(&r[0]));
    TEST_ASSERT_EQUAL_UINT16(0x01, r[1].id);
    TEST_ASSERT_EQUAL_INT64(0x00, as_integer(&r[1]));
    TEST_ASSERT_EQUAL_INT(LWM2M_TLV_MULTIPLE_RESOURCE, r[2].id_type);
    TEST_ASSERT_EQUAL_UINT16(0x02, r[2].id);
    TEST_ASSERT_EQUAL_UINT(3u, r[2].len);
    TEST_ASSERT_EQUAL_INT(LWM2M_TLV_RESOURCE_WITH_VALUE, r[3].id_type);
    TEST_ASSERT_EQUAL_UINT16(0x03, r[3].id);
    TEST_ASSERT_EQUAL_INT64(0x7F, as_integer(&r[3]));

    uint8_t acl0[3];
    memcpy(acl0, r[2].value, sizeof(acl0));
    Entry ri[4];
    TEST_ASSERT_EQUAL_UINT(1u, walk(acl0, sizeof(acl0), ri, 4));
    TEST_ASSERT_EQUAL_INT(LWM2M_TLV_RESOURCE_INSTANCE, ri[0].id_type);
    TEST_ASSERT_EQUAL_UINT16(0x7F, ri[0].id); // ACL [127]
    TEST_ASSERT_EQUAL_INT64(0x07, as_integer(&ri[0]));

    // Instance 2: the same shape, with an ACL carrying two Resource Instances.
    TEST_ASSERT_EQUAL_UINT(4u, walk(inst2, sizeof(inst2), r, 8));
    TEST_ASSERT_EQUAL_INT64(0x03, as_integer(&r[0]));
    TEST_ASSERT_EQUAL_INT64(0x00, as_integer(&r[1]));
    TEST_ASSERT_EQUAL_INT(LWM2M_TLV_MULTIPLE_RESOURCE, r[2].id_type);
    TEST_ASSERT_EQUAL_UINT(7u, r[2].len);
    TEST_ASSERT_EQUAL_INT64(0x7F, as_integer(&r[3]));

    uint8_t acl2[7];
    memcpy(acl2, r[2].value, sizeof(acl2));
    TEST_ASSERT_EQUAL_UINT(2u, walk(acl2, sizeof(acl2), ri, 4));
    TEST_ASSERT_EQUAL_UINT16(0x7F, ri[0].id); // ACL [127]
    TEST_ASSERT_EQUAL_INT64(0x07, as_integer(&ri[0]));
    TEST_ASSERT_EQUAL_UINT16(0x0136, ri[1].id); // ACL [310], the 16-bit Identifier
    TEST_ASSERT_EQUAL_INT64(0x01, as_integer(&ri[1]));
}

// Table 7.4.5.-1 bit 5: an Identifier past 255 needs the 16-bit field. The published ACL [310] row is
// 0b01 1 00 001 = 0x61 with the Identifier 0x0136 in network byte order.
void test_sixteen_bit_identifier(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x20, LWM2M_TLV_ID16_FLAG);
    open_sink(sizeof(g_buf));
    write_integer(LWM2M_TLV_RESOURCE_INSTANCE, 310, 0x01);
    TEST_ASSERT_EQUAL_UINT(4u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\x61\x01\x36\x01", g_buf, 4);

    // 255 still rides in the 8-bit field; 256 is the first that does not.
    open_sink(sizeof(g_buf));
    write_integer(LWM2M_TLV_RESOURCE_INSTANCE, 255, 0x01);
    TEST_ASSERT_EQUAL_UINT(3u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\x41\xFF\x01", g_buf, 3);
    open_sink(sizeof(g_buf));
    write_integer(LWM2M_TLV_RESOURCE_INSTANCE, 256, 0x01);
    TEST_ASSERT_EQUAL_UINT(4u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\x61\x01\x00\x01", g_buf, 4);
}

// Table 7.4.5.-1 bits 4-3: 00 no Length field with the Length in bits 2-0, 01 an 8-bit Length field,
// 10 a 16-bit one, 11 a 24-bit one. The boundaries are 7/8, 255/256 and 65535/65536.
static uint8_t g_big[65536 + 8];
static uint8_t g_value[65536];

void test_length_field_widths(void)
{
    struct
    {
        size_t len;
        uint8_t type;
        size_t header; // Type + Identifier + Length field
    } static const CASES[] = {
        {0, 0xC0, 2},     // 0b11 0 00 000
        {7, 0xC7, 2},     // 0b11 0 00 111, the widest inline Length
        {8, 0xC8, 3},     // 0b11 0 01 000, the first 8-bit Length field
        {255, 0xC8, 3},   // the widest 8-bit Length
        {256, 0xD0, 4},   // 0b11 0 10 000, the first 16-bit Length field
        {65535, 0xD0, 4}, // the widest 16-bit Length
        {65536, 0xD8, 5}, // 0b11 0 11 000, the first 24-bit Length field
    };
    TEST_ASSERT_EQUAL_HEX8(0x07, LWM2M_TLV_INLINE_LEN_MASK);
    TEST_ASSERT_EQUAL_INT(3, LWM2M_TLV_LENTYPE_SHIFT);
    TEST_ASSERT_EQUAL_HEX8(0x03, LWM2M_TLV_LENTYPE_MASK);

    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        Lwm2mTlv.sink.buf = g_big;
        Lwm2mTlv.sink.cap = sizeof(g_big);
        Lwm2mTlv.open(protocore_lwm2m_tlv_span());
        write_opaque(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x05, g_value, CASES[i].len);
        TEST_ASSERT_TRUE(Lwm2mTlv.ok);
        TEST_ASSERT_EQUAL_UINT(CASES[i].header + CASES[i].len, finish());
        TEST_ASSERT_EQUAL_HEX8(CASES[i].type, g_big[0]);
        TEST_ASSERT_EQUAL_HEX8(0x05, g_big[1]);

        Entry e[2];
        TEST_ASSERT_EQUAL_UINT(1u, walk(g_big, CASES[i].header + CASES[i].len, e, 2));
        TEST_ASSERT_EQUAL_UINT(CASES[i].len, e[0].len);
    }

    // The Length field is an unsigned integer in network byte order: 256 is 0x01 0x00 over two
    // octets and 65536 is 0x01 0x00 0x00 over three.
    Lwm2mTlv.sink.buf = g_big;
    Lwm2mTlv.sink.cap = sizeof(g_big);
    Lwm2mTlv.open(protocore_lwm2m_tlv_span());
    write_opaque(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x05, g_value, 256);
    TEST_ASSERT_EQUAL_UINT(4u + 256u, finish());
    TEST_ASSERT_EQUAL_HEX8(0x01, g_big[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_big[3]);

    Lwm2mTlv.sink.buf = g_big;
    Lwm2mTlv.sink.cap = sizeof(g_big);
    Lwm2mTlv.open(protocore_lwm2m_tlv_span());
    write_opaque(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x05, g_value, 65536);
    TEST_ASSERT_EQUAL_UINT(5u + 65536u, finish());
    TEST_ASSERT_EQUAL_HEX8(0x01, g_big[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_big[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_big[4]);
}

// LwM2M Core Appendix C Table C.-2: Integer is a binary signed integer in network byte order and
// two's complement, 1, 2, 4 or 8 octets. The boundaries are the signed limits of each width.
void test_integer_takes_the_shortest_signed_width(void)
{
    // A Value of 1, 2 or 4 octets rides in the inline Length (bits 2-0); 8 passes the inline maximum
    // of 7, so it takes an 8-bit Length field and the Type byte becomes 0xC8.
    struct
    {
        int64_t v;
        size_t len;
        uint8_t type;
        size_t header;
    } static const CASES[] = {
        {0, 1, 0xC1, 2},
        {127, 1, 0xC1, 2},
        {-128, 1, 0xC1, 2},
        {128, 2, 0xC2, 2},
        {-129, 2, 0xC2, 2},
        {32767, 2, 0xC2, 2},
        {-32768, 2, 0xC2, 2},
        {32768, 4, 0xC4, 2},
        {-32769, 4, 0xC4, 2},
        {2147483647, 4, 0xC4, 2},
        {-2147483648LL, 4, 0xC4, 2},
        {2147483648LL, 8, 0xC8, 3},
        {-2147483649LL, 8, 0xC8, 3},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        open_sink(sizeof(g_buf));
        write_integer(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x01, CASES[i].v);
        TEST_ASSERT_EQUAL_UINT(CASES[i].header + CASES[i].len, finish());
        TEST_ASSERT_EQUAL_HEX8(CASES[i].type, g_buf[0]);

        Entry e[2];
        TEST_ASSERT_EQUAL_UINT(1u, walk(g_buf, CASES[i].header + CASES[i].len, e, 2));
        TEST_ASSERT_EQUAL_UINT(CASES[i].len, e[0].len);
        TEST_ASSERT_EQUAL_INT64(CASES[i].v, as_integer(&e[0]));
    }

    // The published two's complement patterns for -1 at every width.
    static const uint8_t MINUS_ONE_1[3] = {0xC1, 0x01, 0xFF};
    static const uint8_t MINUS_ONE_2[4] = {0xC2, 0x01, 0xFF, 0xFF};
    static const uint8_t MINUS_ONE_4[6] = {0xC4, 0x01, 0xFF, 0xFF, 0xFF, 0xFF};
    Entry e[2];
    TEST_ASSERT_EQUAL_UINT(1u, walk(MINUS_ONE_1, sizeof(MINUS_ONE_1), e, 2));
    TEST_ASSERT_EQUAL_INT64(-1, as_integer(&e[0]));
    TEST_ASSERT_EQUAL_UINT(1u, walk(MINUS_ONE_2, sizeof(MINUS_ONE_2), e, 2));
    TEST_ASSERT_EQUAL_INT64(-1, as_integer(&e[0]));
    TEST_ASSERT_EQUAL_UINT(1u, walk(MINUS_ONE_4, sizeof(MINUS_ONE_4), e, 2));
    TEST_ASSERT_EQUAL_INT64(-1, as_integer(&e[0]));
}

// Appendix C Table C.-2: "the Length of a Boolean value MUST always be 1 byte", 0 for False and 1
// for True.
void test_boolean_is_one_octet(void)
{
    open_sink(sizeof(g_buf));
    Lwm2mTlv.hdr.id_type = LWM2M_TLV_RESOURCE_WITH_VALUE;
    Lwm2mTlv.hdr.id = 0x0B;
    Lwm2mTlv.val.boolean_value = PROTO_TRUE;
    Lwm2mTlv.write_boolean(protocore_lwm2m_tlv_span());
    Lwm2mTlv.hdr.id = 0x0C;
    Lwm2mTlv.val.boolean_value = PROTO_FALSE;
    Lwm2mTlv.write_boolean(protocore_lwm2m_tlv_span());
    TEST_ASSERT_EQUAL_UINT(6u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\xC1\x0B\x01\xC1\x0C\x00", g_buf, 6);
}

// Appendix C Table C.-2: Float is binary32 or binary64, and this writer emits binary64. The IEEE 754
// binary64 encoding of 1.0 is sign 0, exponent 1023 = 0x3FF, mantissa 0, so 0x3FF0000000000000; -2.0
// is sign 1, exponent 1024 = 0x400, mantissa 0, so 0xC000000000000000.
void test_float_is_binary64_in_network_byte_order(void)
{
    open_sink(sizeof(g_buf));
    Lwm2mTlv.hdr.id_type = LWM2M_TLV_RESOURCE_WITH_VALUE;
    Lwm2mTlv.hdr.id = 0x01;
    Lwm2mTlv.val.float_value = 1.0;
    Lwm2mTlv.write_float(protocore_lwm2m_tlv_span());
    TEST_ASSERT_EQUAL_UINT(11u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\xC8\x01\x08\x3F\xF0\x00\x00\x00\x00\x00\x00", g_buf, 11);

    open_sink(sizeof(g_buf));
    Lwm2mTlv.hdr.id = 0x02;
    Lwm2mTlv.val.float_value = -2.0;
    Lwm2mTlv.write_float(protocore_lwm2m_tlv_span());
    TEST_ASSERT_EQUAL_UINT(11u, finish());
    TEST_ASSERT_EQUAL_MEMORY("\xC8\x02\x08\xC0\x00\x00\x00\x00\x00\x00\x00", g_buf, 11);
}

// The first entry that does not fit poisons the cursor, so the finish reports 0 rather than a
// truncated payload a peer would parse as a shorter but valid one.
void test_writer_fails_closed(void)
{
    open_sink(4); // room for one 3-octet entry and nothing more
    write_integer(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x01, 0x11);
    TEST_ASSERT_TRUE(Lwm2mTlv.ok);
    write_integer(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x02, 0x22);
    TEST_ASSERT_FALSE(Lwm2mTlv.ok);
    // A later write that would have fit is refused too: the cursor stays poisoned.
    write_opaque(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x03, "", 0);
    TEST_ASSERT_FALSE(Lwm2mTlv.ok);
    Lwm2mTlv.finish(protocore_lwm2m_tlv_span());
    TEST_ASSERT_FALSE(Lwm2mTlv.ok);
    TEST_ASSERT_EQUAL_UINT(0u, Lwm2mTlv.n);

    // A sink with no buffer starts poisoned.
    Lwm2mTlv.sink.buf = NULL;
    Lwm2mTlv.sink.cap = 64;
    Lwm2mTlv.open(protocore_lwm2m_tlv_span());
    TEST_ASSERT_FALSE(Lwm2mTlv.ok);
    write_integer(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x01, 1);
    TEST_ASSERT_FALSE(Lwm2mTlv.ok);
    TEST_ASSERT_EQUAL_UINT(0u, finish());

    // A write with a Value length but no Value is refused rather than read through.
    open_sink(sizeof(g_buf));
    write_opaque(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x01, NULL, 4);
    TEST_ASSERT_FALSE(Lwm2mTlv.ok);
    // A string write with no string is refused as well.
    write_string(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x01, NULL);
    TEST_ASSERT_FALSE(Lwm2mTlv.ok);
}

// An entry the source cuts short is refused instead of pointing a Value past the end of the buffer.
void test_reader_refuses_a_truncated_entry(void)
{
    static const uint8_t GOOD[6] = {0xC4, 0x0D, 0x51, 0x82, 0x42, 0x8F};
    Entry e[2];
    TEST_ASSERT_EQUAL_UINT(1u, walk(GOOD, sizeof(GOOD), e, 2));
    for (size_t len = 1; len < sizeof(GOOD); len++)
    {
        Lwm2mTlv.source.buf = GOOD;
        Lwm2mTlv.source.len = len;
        Lwm2mTlv.parse(protocore_lwm2m_tlv_span());
        TEST_ASSERT_TRUE(Lwm2mTlv.ok);
        Lwm2mTlv.next(protocore_lwm2m_tlv_span());
        TEST_ASSERT_FALSE_MESSAGE(Lwm2mTlv.ok, "a cut-short entry must not decode");
    }

    // A 16-bit Identifier with only one octet behind it.
    static const uint8_t SHORT_ID[2] = {0x61, 0x01};
    Lwm2mTlv.source.buf = SHORT_ID;
    Lwm2mTlv.source.len = sizeof(SHORT_ID);
    Lwm2mTlv.parse(protocore_lwm2m_tlv_span());
    Lwm2mTlv.next(protocore_lwm2m_tlv_span());
    TEST_ASSERT_FALSE(Lwm2mTlv.ok);

    // An 8-bit Length field with no octet behind it.
    static const uint8_t SHORT_LEN[2] = {0xC8, 0x01};
    Lwm2mTlv.source.buf = SHORT_LEN;
    Lwm2mTlv.source.len = sizeof(SHORT_LEN);
    Lwm2mTlv.parse(protocore_lwm2m_tlv_span());
    Lwm2mTlv.next(protocore_lwm2m_tlv_span());
    TEST_ASSERT_FALSE(Lwm2mTlv.ok);

    // A source with no buffer decodes nothing.
    Lwm2mTlv.source.buf = NULL;
    Lwm2mTlv.source.len = 8;
    Lwm2mTlv.parse(protocore_lwm2m_tlv_span());
    TEST_ASSERT_FALSE(Lwm2mTlv.ok);
    Lwm2mTlv.next(protocore_lwm2m_tlv_span());
    TEST_ASSERT_FALSE(Lwm2mTlv.ok);
}

// value_integer only accepts the 1, 2, 4 and 8 octet widths Appendix C Table C.-2 lists.
void test_value_integer_refuses_other_widths(void)
{
    static const uint8_t V[8] = {0};
    static const size_t GOOD[] = {1, 2, 4, 8};
    static const size_t BAD[] = {0, 3, 5, 6, 7};
    for (size_t i = 0; i < sizeof(GOOD) / sizeof(GOOD[0]); i++)
    {
        Lwm2mTlv.val.opaque = V;
        Lwm2mTlv.val.len = GOOD[i];
        Lwm2mTlv.value_integer(protocore_lwm2m_tlv_span());
        TEST_ASSERT_TRUE(Lwm2mTlv.ok);
    }
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        Lwm2mTlv.val.opaque = V;
        Lwm2mTlv.val.len = BAD[i];
        Lwm2mTlv.value_integer(protocore_lwm2m_tlv_span());
        TEST_ASSERT_FALSE(Lwm2mTlv.ok);
    }
    Lwm2mTlv.val.opaque = NULL;
    Lwm2mTlv.val.len = 4;
    Lwm2mTlv.value_integer(protocore_lwm2m_tlv_span());
    TEST_ASSERT_FALSE(Lwm2mTlv.ok);
}

// A written payload reads back as the entries that went in, and the reader stops at the end.
void test_write_then_read_round_trip(void)
{
    open_sink(sizeof(g_buf));
    write_integer(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x00, -12345);
    write_string(LWM2M_TLV_RESOURCE_WITH_VALUE, 0x01, "protocore");
    write_opaque(LWM2M_TLV_OBJECT_INSTANCE, 0x0201, "\x01\x02\x03", 3);
    const size_t n = finish();
    TEST_ASSERT_TRUE(n > 0);

    Entry e[4];
    TEST_ASSERT_EQUAL_UINT(3u, walk(g_buf, n, e, 4));
    TEST_ASSERT_EQUAL_INT(LWM2M_TLV_RESOURCE_WITH_VALUE, e[0].id_type);
    TEST_ASSERT_EQUAL_UINT16(0x00, e[0].id);
    TEST_ASSERT_EQUAL_INT64(-12345, as_integer(&e[0]));
    TEST_ASSERT_EQUAL_UINT16(0x01, e[1].id);
    TEST_ASSERT_EQUAL_UINT(9u, e[1].len);
    TEST_ASSERT_EQUAL_MEMORY("protocore", e[1].value, 9);
    TEST_ASSERT_EQUAL_INT(LWM2M_TLV_OBJECT_INSTANCE, e[2].id_type);
    TEST_ASSERT_EQUAL_UINT16(0x0201, e[2].id);
    TEST_ASSERT_EQUAL_UINT(3u, e[2].len);
}
