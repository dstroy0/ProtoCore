// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Bluetooth ATT codec and the GATT characteristic bridge
// (services/radio/ble_gatt/ble_gatt.h).
//
// test_core_spec_att_pdu_layout is the load-bearing case. The Bluetooth Core Specification, Vol 3
// Part F, gives each ATT PDU a one-octet Attribute Opcode from the sec 3.4.8 opcode summary
// (Error Response 0x01, Read Request 0x0A, Read Response 0x0B, Write Request 0x12, Write Response
// 0x13, Handle Value Notification 0x1B) followed by fixed fields, and every multi-octet field
// including the Attribute Handle goes on the wire least significant octet first. Handle 0x0025
// written 0x25 0x00 and not 0x00 0x25 is what makes the difference between addressing the intended
// attribute and addressing an unrelated one.
//
// The Core Specification is not an IETF document and is not cached in this repository, so the
// opcode and error-code values were checked against the published spec's own numbering as it is
// reproduced in an independent implementation of it. The characteristic property bits are the
// Vol 3 Part G sec 3.3.1.1 Characteristic Properties field, and 0x2A37 / 0x2A6E are Bluetooth SIG
// assigned numbers (Heart Rate Measurement, Temperature).

#include "services/radio/ble_gatt/ble_gatt.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Vol 3 Part F sec 3.4.8: opcode first, then the PDU's fixed fields, the Attribute Handle
// little-endian. Handle 0x1234 makes both octets distinct, so an endianness slip cannot pass.
void test_core_spec_att_pdu_layout(void)
{
    uint8_t out[16];

    // Read Request: [0x0A][handle:2]
    static const uint8_t READ_REQ[] = {0x0A, 0x34, 0x12};
    TEST_ASSERT_EQUAL_size_t(sizeof(READ_REQ), att_read_req(0x1234, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(READ_REQ, out, sizeof(READ_REQ));

    // Read Response: [0x0B][value...] - no handle, the request already named it
    static const uint8_t VAL[3] = {0xDE, 0xAD, 0xBE};
    static const uint8_t READ_RSP[] = {0x0B, 0xDE, 0xAD, 0xBE};
    TEST_ASSERT_EQUAL_size_t(sizeof(READ_RSP), att_read_rsp(VAL, sizeof(VAL), out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(READ_RSP, out, sizeof(READ_RSP));

    // Write Request: [0x12][handle:2][value...]
    static const uint8_t WRITE_REQ[] = {0x12, 0x34, 0x12, 0xDE, 0xAD, 0xBE};
    TEST_ASSERT_EQUAL_size_t(sizeof(WRITE_REQ), att_write_req(0x1234, VAL, sizeof(VAL), out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WRITE_REQ, out, sizeof(WRITE_REQ));

    // Handle Value Notification: [0x1B][handle:2][value...] - the same shape under another opcode
    static const uint8_t NOTIFY[] = {0x1B, 0x34, 0x12, 0xDE, 0xAD, 0xBE};
    TEST_ASSERT_EQUAL_size_t(sizeof(NOTIFY), att_notify(0x1234, VAL, sizeof(VAL), out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NOTIFY, out, sizeof(NOTIFY));

    // Error Response: [0x01][request opcode][handle:2][error code], five octets, always
    // (Vol 3 Part F sec 3.4.1.1). 0x0A is the Attribute Not Found error code.
    static const uint8_t ERROR_RSP[] = {0x01, 0x0A, 0x34, 0x12, 0x0A};
    TEST_ASSERT_EQUAL_size_t(sizeof(ERROR_RSP), att_error_rsp(ATT_OP_READ_REQ, 0x1234, 0x0A, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ERROR_RSP, out, sizeof(ERROR_RSP));
}

// The opcode constants this codec emits are the spec's own numbering.
void test_core_spec_opcode_values(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01, ATT_OP_ERROR_RSP);
    TEST_ASSERT_EQUAL_HEX8(0x0A, ATT_OP_READ_REQ);
    TEST_ASSERT_EQUAL_HEX8(0x0B, ATT_OP_READ_RSP);
    TEST_ASSERT_EQUAL_HEX8(0x12, ATT_OP_WRITE_REQ);
    TEST_ASSERT_EQUAL_HEX8(0x13, ATT_OP_WRITE_RSP);
    TEST_ASSERT_EQUAL_HEX8(0x1B, ATT_OP_HANDLE_VALUE_NTF);

    // Vol 3 Part F sec 3.3.1 splits the opcode into a 6-bit Method with the Command Flag at bit 6
    // and the Authentication Signature Flag at bit 7, so every request and response above has both
    // high bits clear.
    static const uint8_t OPS[] = {ATT_OP_ERROR_RSP,  ATT_OP_READ_REQ,  ATT_OP_READ_RSP,
                                  ATT_OP_WRITE_REQ,  ATT_OP_WRITE_RSP, ATT_OP_HANDLE_VALUE_NTF};
    for (size_t i = 0; i < sizeof(OPS); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, OPS[i] & 0xC0);
    }
}

// Vol 3 Part G sec 3.3.1.1: the Characteristic Properties field is a bit field, one bit per
// permitted operation, so a characteristic that is both readable and notifiable carries both bits
// and no other.
void test_core_spec_characteristic_property_bits(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x02, GATT_PROP_READ);
    TEST_ASSERT_EQUAL_HEX8(0x04, GATT_PROP_WRITE_NR);
    TEST_ASSERT_EQUAL_HEX8(0x08, GATT_PROP_WRITE);
    TEST_ASSERT_EQUAL_HEX8(0x10, GATT_PROP_NOTIFY);
    TEST_ASSERT_EQUAL_HEX8(0x20, GATT_PROP_INDICATE);

    // Each names one distinct bit: no two overlap.
    static const uint8_t BITS[] = {GATT_PROP_READ, GATT_PROP_WRITE_NR, GATT_PROP_WRITE, GATT_PROP_NOTIFY,
                                   GATT_PROP_INDICATE};
    uint8_t seen = 0;
    for (size_t i = 0; i < sizeof(BITS); i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0, seen & BITS[i]);
        seen = (uint8_t)(seen | BITS[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0x3E, seen);
}

// A built PDU parses back to the fields it was built from: opcode, handle, and the value bounds.
void test_build_parse_round_trip(void)
{
    uint8_t pdu[16];
    static const uint8_t VAL[2] = {0x01, 0x02};
    AttPdu p;

    size_t n = att_write_req(0x0031, VAL, sizeof(VAL), pdu, sizeof(pdu));
    TEST_ASSERT_TRUE(att_parse(pdu, n, &p));
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_WRITE_REQ, p.opcode);
    TEST_ASSERT_EQUAL_HEX16(0x0031, p.handle);
    TEST_ASSERT_EQUAL_size_t(sizeof(VAL), p.value_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(VAL, p.value, sizeof(VAL));

    n = att_notify(0xABCD, VAL, sizeof(VAL), pdu, sizeof(pdu));
    TEST_ASSERT_TRUE(att_parse(pdu, n, &p));
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_HANDLE_VALUE_NTF, p.opcode);
    TEST_ASSERT_EQUAL_HEX16(0xABCD, p.handle);

    n = att_read_req(0xFF01, pdu, sizeof(pdu));
    TEST_ASSERT_TRUE(att_parse(pdu, n, &p));
    TEST_ASSERT_EQUAL_HEX16(0xFF01, p.handle);
    TEST_ASSERT_NULL(p.value);

    n = att_error_rsp(ATT_OP_WRITE_REQ, 0x0025, 0x03, pdu, sizeof(pdu)); // 0x03 = Write Not Permitted
    TEST_ASSERT_TRUE(att_parse(pdu, n, &p));
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_ERROR_RSP, p.opcode);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_WRITE_REQ, p.req_op);
    TEST_ASSERT_EQUAL_HEX16(0x0025, p.handle);
    TEST_ASSERT_EQUAL_HEX8(0x03, p.error);

    n = att_read_rsp(VAL, sizeof(VAL), pdu, sizeof(pdu));
    TEST_ASSERT_TRUE(att_parse(pdu, n, &p));
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_READ_RSP, p.opcode);
    TEST_ASSERT_EQUAL_size_t(sizeof(VAL), p.value_len);
}

// A PDU shorter than its opcode's fixed fields is not that PDU: the handle would be read out of
// whatever follows the buffer.
void test_parse_refuses_a_truncated_pdu(void)
{
    AttPdu p;
    static const uint8_t WRITE_NO_HANDLE[] = {ATT_OP_WRITE_REQ, 0x31};
    TEST_ASSERT_FALSE(att_parse(WRITE_NO_HANDLE, sizeof(WRITE_NO_HANDLE), &p));

    static const uint8_t READ_NO_HANDLE[] = {ATT_OP_READ_REQ, 0x25};
    TEST_ASSERT_FALSE(att_parse(READ_NO_HANDLE, sizeof(READ_NO_HANDLE), &p));

    static const uint8_t NTF_NO_HANDLE[] = {ATT_OP_HANDLE_VALUE_NTF, 0x25};
    TEST_ASSERT_FALSE(att_parse(NTF_NO_HANDLE, sizeof(NTF_NO_HANDLE), &p));

    static const uint8_t ERR_SHORT[] = {ATT_OP_ERROR_RSP, ATT_OP_READ_REQ, 0x25};
    TEST_ASSERT_FALSE(att_parse(ERR_SHORT, sizeof(ERR_SHORT), &p));

    static const uint8_t NOTHING[1] = {0};
    TEST_ASSERT_FALSE(att_parse(NOTHING, 0, &p));
    TEST_ASSERT_FALSE(att_parse(NULL, 5, &p));
    TEST_ASSERT_FALSE(att_parse(NOTHING, 1, NULL));
}

// A PDU carrying only its fixed fields has no Attribute Value, and an opcode this codec does not
// know still reports its opcode with no fields read out of it.
void test_parse_value_absent_and_unknown_opcode(void)
{
    AttPdu p;
    static const uint8_t WRITE_RSP[] = {ATT_OP_WRITE_RSP};
    TEST_ASSERT_TRUE(att_parse(WRITE_RSP, sizeof(WRITE_RSP), &p));
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_WRITE_RSP, p.opcode);
    TEST_ASSERT_NULL(p.value);
    TEST_ASSERT_EQUAL_size_t(0, p.value_len);

    static const uint8_t READ_RSP_EMPTY[] = {ATT_OP_READ_RSP};
    TEST_ASSERT_TRUE(att_parse(READ_RSP_EMPTY, sizeof(READ_RSP_EMPTY), &p));
    TEST_ASSERT_NULL(p.value);

    static const uint8_t WRITE_NO_VALUE[] = {ATT_OP_WRITE_REQ, 0x31, 0x00};
    TEST_ASSERT_TRUE(att_parse(WRITE_NO_VALUE, sizeof(WRITE_NO_VALUE), &p));
    TEST_ASSERT_EQUAL_HEX16(0x0031, p.handle);
    TEST_ASSERT_NULL(p.value);

    static const uint8_t UNKNOWN[] = {0xFF, 0x01};
    TEST_ASSERT_TRUE(att_parse(UNKNOWN, sizeof(UNKNOWN), &p));
    TEST_ASSERT_EQUAL_HEX8(0xFF, p.opcode);
    TEST_ASSERT_EQUAL_HEX16(0x0000, p.handle);
    TEST_ASSERT_NULL(p.value);
}

// The parsed value points into the caller's own octets rather than into a copy, so the codec owns
// no storage for it.
void test_parsed_value_points_into_the_input(void)
{
    AttPdu p;
    static const uint8_t PDU[] = {ATT_OP_HANDLE_VALUE_NTF, 0x25, 0x00, 0xAA, 0xBB, 0xCC};
    TEST_ASSERT_TRUE(att_parse(PDU, sizeof(PDU), &p));
    TEST_ASSERT_EQUAL_PTR(PDU + 3, p.value);
    TEST_ASSERT_EQUAL_size_t(3, p.value_len);
}

// Every builder writes whole PDUs only: a region shorter than the PDU, or a missing region or
// value, yields nothing rather than a partial PDU the peer would misparse.
void test_builders_fail_closed(void)
{
    uint8_t out[16];
    static const uint8_t VAL[3] = {1, 2, 3};

    TEST_ASSERT_EQUAL_size_t(0, att_read_req(0x0025, out, 2));
    TEST_ASSERT_EQUAL_size_t(0, att_read_req(0x0025, NULL, sizeof(out)));

    TEST_ASSERT_EQUAL_size_t(0, att_read_rsp(VAL, 3, out, 3)); // needs 1 + 3
    TEST_ASSERT_EQUAL_size_t(0, att_read_rsp(VAL, 3, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, att_read_rsp(NULL, 3, out, sizeof(out)));

    TEST_ASSERT_EQUAL_size_t(0, att_write_req(0x0010, VAL, 3, out, 5)); // needs 3 + 3
    TEST_ASSERT_EQUAL_size_t(0, att_write_req(0x0010, VAL, 3, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, att_write_req(0x0010, NULL, 3, out, sizeof(out)));

    TEST_ASSERT_EQUAL_size_t(0, att_notify(0x0010, VAL, 3, out, 5));
    TEST_ASSERT_EQUAL_size_t(0, att_error_rsp(ATT_OP_READ_REQ, 0x0010, 0x0A, out, 4));
    TEST_ASSERT_EQUAL_size_t(0, att_error_rsp(ATT_OP_READ_REQ, 0x0010, 0x0A, NULL, sizeof(out)));

    // A zero-length Attribute Value is legal: Vol 3 Part F sec 3.2.9 allows an attribute value of
    // length zero, so the PDU is just its fixed fields.
    TEST_ASSERT_EQUAL_size_t(1, att_read_rsp(NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_READ_RSP, out[0]);
    TEST_ASSERT_EQUAL_size_t(3, att_write_req(0x0010, NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_WRITE_REQ, out[0]);
}

// The northbound view of a characteristic table: one JSON object per characteristic
// (RFC 8259 sec 4), the handle as a number, the 16-bit UUID as a lowercase 0x-prefixed string and
// the property bits as a number. 0x2A37 with Read|Notify is 0x02|0x10 = 0x12 = 18 decimal;
// 0x2A6E with Read alone is 2.
void test_characteristic_table_json(void)
{
    static const GattChar CHARS[2] = {{0x0025, 0x2A37, GATT_PROP_READ | GATT_PROP_NOTIFY},
                                      {0x0031, 0x2A6E, GATT_PROP_READ}};
    char out[160];
    const size_t n = protocore_gatt_char_json(CHARS, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(
        "[{\"handle\":37,\"uuid\":\"0x2a37\",\"props\":18},{\"handle\":49,\"uuid\":\"0x2a6e\",\"props\":2}]", out);
    TEST_ASSERT_EQUAL_size_t(strlen(out), n);

    // An empty table is the empty array, not an empty string.
    TEST_ASSERT_EQUAL_size_t(2, protocore_gatt_char_json(NULL, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("[]", out);

    // The UUID field is always four hex digits, so a low assigned number keeps its leading zeros.
    static const GattChar ONE = {0x0001, 0x002A, GATT_PROP_WRITE};
    TEST_ASSERT_TRUE(protocore_gatt_char_json(&ONE, 1, out, sizeof(out)) > 0);
    TEST_ASSERT_EQUAL_STRING("[{\"handle\":1,\"uuid\":\"0x002a\",\"props\":8}]", out);
}

// The serializer writes the whole array or nothing: a truncated JSON array is not shorter data,
// it is unparseable.
void test_characteristic_table_json_fails_closed(void)
{
    static const GattChar CHARS[2] = {{0x0025, 0x2A37, GATT_PROP_READ | GATT_PROP_NOTIFY},
                                      {0x0031, 0x2A6E, GATT_PROP_READ}};
    char tiny[8];
    TEST_ASSERT_EQUAL_size_t(0, protocore_gatt_char_json(CHARS, 2, tiny, sizeof(tiny)));

    char out[64];
    TEST_ASSERT_EQUAL_size_t(0, protocore_gatt_char_json(CHARS, 1, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_size_t(0, protocore_gatt_char_json(CHARS, 1, out, 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_gatt_char_json(NULL, 1, out, sizeof(out)));
}
