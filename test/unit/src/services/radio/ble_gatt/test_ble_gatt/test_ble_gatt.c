// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t ble_gatt_work[16]; // the borrow an entry takes; BleGatt never reads it

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
    BleGattV.att_read_req_args.handle = 0x1234;
    BleGattV.att_read_req_args.out = out;
    BleGattV.att_read_req_args.cap = sizeof(out);
    BleGatt.att_read_req(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(sizeof(READ_REQ), BleGattV.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(READ_REQ, out, sizeof(READ_REQ));

    // Read Response: [0x0B][value...] - no handle, the request already named it
    static const uint8_t VAL[3] = {0xDE, 0xAD, 0xBE};
    static const uint8_t READ_RSP[] = {0x0B, 0xDE, 0xAD, 0xBE};
    BleGattV.att_read_rsp_args.val = VAL;
    BleGattV.att_read_rsp_args.vlen = sizeof(VAL);
    BleGattV.att_read_rsp_args.out = out;
    BleGattV.att_read_rsp_args.cap = sizeof(out);
    BleGatt.att_read_rsp(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(sizeof(READ_RSP), BleGattV.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(READ_RSP, out, sizeof(READ_RSP));

    // Write Request: [0x12][handle:2][value...]
    static const uint8_t WRITE_REQ[] = {0x12, 0x34, 0x12, 0xDE, 0xAD, 0xBE};
    BleGattV.att_write_req_args.handle = 0x1234;
    BleGattV.att_write_req_args.val = VAL;
    BleGattV.att_write_req_args.vlen = sizeof(VAL);
    BleGattV.att_write_req_args.out = out;
    BleGattV.att_write_req_args.cap = sizeof(out);
    BleGatt.att_write_req(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(sizeof(WRITE_REQ), BleGattV.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WRITE_REQ, out, sizeof(WRITE_REQ));

    // Handle Value Notification: [0x1B][handle:2][value...] - the same shape under another opcode
    static const uint8_t NOTIFY[] = {0x1B, 0x34, 0x12, 0xDE, 0xAD, 0xBE};
    BleGattV.att_notify_args.handle = 0x1234;
    BleGattV.att_notify_args.val = VAL;
    BleGattV.att_notify_args.vlen = sizeof(VAL);
    BleGattV.att_notify_args.out = out;
    BleGattV.att_notify_args.cap = sizeof(out);
    BleGatt.att_notify(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(sizeof(NOTIFY), BleGattV.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NOTIFY, out, sizeof(NOTIFY));

    // Error Response: [0x01][request opcode][handle:2][error code], five octets, always
    // (Vol 3 Part F sec 3.4.1.1). 0x0A is the Attribute Not Found error code.
    static const uint8_t ERROR_RSP[] = {0x01, 0x0A, 0x34, 0x12, 0x0A};
    BleGattV.att_error_rsp_args.req_op = ATT_OP_READ_REQ;
    BleGattV.att_error_rsp_args.handle = 0x1234;
    BleGattV.att_error_rsp_args.error = 0x0A;
    BleGattV.att_error_rsp_args.out = out;
    BleGattV.att_error_rsp_args.cap = sizeof(out);
    BleGatt.att_error_rsp(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(sizeof(ERROR_RSP), BleGattV.n);
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
    static const uint8_t OPS[] = {ATT_OP_ERROR_RSP, ATT_OP_READ_REQ,  ATT_OP_READ_RSP,
                                  ATT_OP_WRITE_REQ, ATT_OP_WRITE_RSP, ATT_OP_HANDLE_VALUE_NTF};
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

    BleGattV.att_write_req_args.handle = 0x0031;
    BleGattV.att_write_req_args.val = VAL;
    BleGattV.att_write_req_args.vlen = sizeof(VAL);
    BleGattV.att_write_req_args.out = pdu;
    BleGattV.att_write_req_args.cap = sizeof(pdu);
    BleGatt.att_write_req(ble_gatt_work);
    size_t n = BleGattV.n;
    BleGattV.att_parse_args.pdu = pdu;
    BleGattV.att_parse_args.len = n;
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_TRUE(BleGattV.ok);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_WRITE_REQ, p.opcode);
    TEST_ASSERT_EQUAL_HEX16(0x0031, p.handle);
    TEST_ASSERT_EQUAL_size_t(sizeof(VAL), p.value_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(VAL, p.value, sizeof(VAL));

    BleGattV.att_notify_args.handle = 0xABCD;
    BleGattV.att_notify_args.val = VAL;
    BleGattV.att_notify_args.vlen = sizeof(VAL);
    BleGattV.att_notify_args.out = pdu;
    BleGattV.att_notify_args.cap = sizeof(pdu);
    BleGatt.att_notify(ble_gatt_work);
    n = BleGattV.n;
    BleGattV.att_parse_args.pdu = pdu;
    BleGattV.att_parse_args.len = n;
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_TRUE(BleGattV.ok);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_HANDLE_VALUE_NTF, p.opcode);
    TEST_ASSERT_EQUAL_HEX16(0xABCD, p.handle);

    BleGattV.att_read_req_args.handle = 0xFF01;
    BleGattV.att_read_req_args.out = pdu;
    BleGattV.att_read_req_args.cap = sizeof(pdu);
    BleGatt.att_read_req(ble_gatt_work);
    n = BleGattV.n;
    BleGattV.att_parse_args.pdu = pdu;
    BleGattV.att_parse_args.len = n;
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_TRUE(BleGattV.ok);
    TEST_ASSERT_EQUAL_HEX16(0xFF01, p.handle);
    TEST_ASSERT_NULL(p.value);

    BleGattV.att_error_rsp_args.req_op = ATT_OP_WRITE_REQ;
    BleGattV.att_error_rsp_args.handle = 0x0025;
    BleGattV.att_error_rsp_args.error = 0x03;
    BleGattV.att_error_rsp_args.out = pdu;
    BleGattV.att_error_rsp_args.cap = sizeof(pdu);
    BleGatt.att_error_rsp(ble_gatt_work);
    n = BleGattV.n; // 0x03 = Write Not Permitted
    BleGattV.att_parse_args.pdu = pdu;
    BleGattV.att_parse_args.len = n;
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_TRUE(BleGattV.ok);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_ERROR_RSP, p.opcode);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_WRITE_REQ, p.req_op);
    TEST_ASSERT_EQUAL_HEX16(0x0025, p.handle);
    TEST_ASSERT_EQUAL_HEX8(0x03, p.error);

    BleGattV.att_read_rsp_args.val = VAL;
    BleGattV.att_read_rsp_args.vlen = sizeof(VAL);
    BleGattV.att_read_rsp_args.out = pdu;
    BleGattV.att_read_rsp_args.cap = sizeof(pdu);
    BleGatt.att_read_rsp(ble_gatt_work);
    n = BleGattV.n;
    BleGattV.att_parse_args.pdu = pdu;
    BleGattV.att_parse_args.len = n;
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_TRUE(BleGattV.ok);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_READ_RSP, p.opcode);
    TEST_ASSERT_EQUAL_size_t(sizeof(VAL), p.value_len);
}

// A PDU shorter than its opcode's fixed fields is not that PDU: the handle would be read out of
// whatever follows the buffer.
void test_parse_refuses_a_truncated_pdu(void)
{
    AttPdu p;
    static const uint8_t WRITE_NO_HANDLE[] = {ATT_OP_WRITE_REQ, 0x31};
    BleGattV.att_parse_args.pdu = WRITE_NO_HANDLE;
    BleGattV.att_parse_args.len = sizeof(WRITE_NO_HANDLE);
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_FALSE(BleGattV.ok);

    static const uint8_t READ_NO_HANDLE[] = {ATT_OP_READ_REQ, 0x25};
    BleGattV.att_parse_args.pdu = READ_NO_HANDLE;
    BleGattV.att_parse_args.len = sizeof(READ_NO_HANDLE);
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_FALSE(BleGattV.ok);

    static const uint8_t NTF_NO_HANDLE[] = {ATT_OP_HANDLE_VALUE_NTF, 0x25};
    BleGattV.att_parse_args.pdu = NTF_NO_HANDLE;
    BleGattV.att_parse_args.len = sizeof(NTF_NO_HANDLE);
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_FALSE(BleGattV.ok);

    static const uint8_t ERR_SHORT[] = {ATT_OP_ERROR_RSP, ATT_OP_READ_REQ, 0x25};
    BleGattV.att_parse_args.pdu = ERR_SHORT;
    BleGattV.att_parse_args.len = sizeof(ERR_SHORT);
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_FALSE(BleGattV.ok);

    static const uint8_t NOTHING[1] = {0};
    BleGattV.att_parse_args.pdu = NOTHING;
    BleGattV.att_parse_args.len = 0;
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_FALSE(BleGattV.ok);
    BleGattV.att_parse_args.pdu = NULL;
    BleGattV.att_parse_args.len = 5;
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_FALSE(BleGattV.ok);
    BleGattV.att_parse_args.pdu = NOTHING;
    BleGattV.att_parse_args.len = 1;
    BleGattV.att_parse_args.out = NULL;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_FALSE(BleGattV.ok);
}

// A PDU carrying only its fixed fields has no Attribute Value, and an opcode this codec does not
// know still reports its opcode with no fields read out of it.
void test_parse_value_absent_and_unknown_opcode(void)
{
    AttPdu p;
    static const uint8_t WRITE_RSP[] = {ATT_OP_WRITE_RSP};
    BleGattV.att_parse_args.pdu = WRITE_RSP;
    BleGattV.att_parse_args.len = sizeof(WRITE_RSP);
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_TRUE(BleGattV.ok);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_WRITE_RSP, p.opcode);
    TEST_ASSERT_NULL(p.value);
    TEST_ASSERT_EQUAL_size_t(0, p.value_len);

    static const uint8_t READ_RSP_EMPTY[] = {ATT_OP_READ_RSP};
    BleGattV.att_parse_args.pdu = READ_RSP_EMPTY;
    BleGattV.att_parse_args.len = sizeof(READ_RSP_EMPTY);
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_TRUE(BleGattV.ok);
    TEST_ASSERT_NULL(p.value);

    static const uint8_t WRITE_NO_VALUE[] = {ATT_OP_WRITE_REQ, 0x31, 0x00};
    BleGattV.att_parse_args.pdu = WRITE_NO_VALUE;
    BleGattV.att_parse_args.len = sizeof(WRITE_NO_VALUE);
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_TRUE(BleGattV.ok);
    TEST_ASSERT_EQUAL_HEX16(0x0031, p.handle);
    TEST_ASSERT_NULL(p.value);

    static const uint8_t UNKNOWN[] = {0xFF, 0x01};
    BleGattV.att_parse_args.pdu = UNKNOWN;
    BleGattV.att_parse_args.len = sizeof(UNKNOWN);
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_TRUE(BleGattV.ok);
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
    BleGattV.att_parse_args.pdu = PDU;
    BleGattV.att_parse_args.len = sizeof(PDU);
    BleGattV.att_parse_args.out = &p;
    BleGatt.att_parse(ble_gatt_work);
    TEST_ASSERT_TRUE(BleGattV.ok);
    TEST_ASSERT_EQUAL_PTR(PDU + 3, p.value);
    TEST_ASSERT_EQUAL_size_t(3, p.value_len);
}

// Every builder writes whole PDUs only: a region shorter than the PDU, or a missing region or
// value, yields nothing rather than a partial PDU the peer would misparse.
void test_builders_fail_closed(void)
{
    uint8_t out[16];
    static const uint8_t VAL[3] = {1, 2, 3};

    BleGattV.att_read_req_args.handle = 0x0025;
    BleGattV.att_read_req_args.out = out;
    BleGattV.att_read_req_args.cap = 2;
    BleGatt.att_read_req(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n);
    BleGattV.att_read_req_args.handle = 0x0025;
    BleGattV.att_read_req_args.out = NULL;
    BleGattV.att_read_req_args.cap = sizeof(out);
    BleGatt.att_read_req(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n);

    BleGattV.att_read_rsp_args.val = VAL;
    BleGattV.att_read_rsp_args.vlen = 3;
    BleGattV.att_read_rsp_args.out = out;
    BleGattV.att_read_rsp_args.cap = 3;
    BleGatt.att_read_rsp(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n); // needs 1 + 3
    BleGattV.att_read_rsp_args.val = VAL;
    BleGattV.att_read_rsp_args.vlen = 3;
    BleGattV.att_read_rsp_args.out = NULL;
    BleGattV.att_read_rsp_args.cap = sizeof(out);
    BleGatt.att_read_rsp(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n);
    BleGattV.att_read_rsp_args.val = NULL;
    BleGattV.att_read_rsp_args.vlen = 3;
    BleGattV.att_read_rsp_args.out = out;
    BleGattV.att_read_rsp_args.cap = sizeof(out);
    BleGatt.att_read_rsp(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n);

    BleGattV.att_write_req_args.handle = 0x0010;
    BleGattV.att_write_req_args.val = VAL;
    BleGattV.att_write_req_args.vlen = 3;
    BleGattV.att_write_req_args.out = out;
    BleGattV.att_write_req_args.cap = 5;
    BleGatt.att_write_req(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n); // needs 3 + 3
    BleGattV.att_write_req_args.handle = 0x0010;
    BleGattV.att_write_req_args.val = VAL;
    BleGattV.att_write_req_args.vlen = 3;
    BleGattV.att_write_req_args.out = NULL;
    BleGattV.att_write_req_args.cap = sizeof(out);
    BleGatt.att_write_req(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n);
    BleGattV.att_write_req_args.handle = 0x0010;
    BleGattV.att_write_req_args.val = NULL;
    BleGattV.att_write_req_args.vlen = 3;
    BleGattV.att_write_req_args.out = out;
    BleGattV.att_write_req_args.cap = sizeof(out);
    BleGatt.att_write_req(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n);

    BleGattV.att_notify_args.handle = 0x0010;
    BleGattV.att_notify_args.val = VAL;
    BleGattV.att_notify_args.vlen = 3;
    BleGattV.att_notify_args.out = out;
    BleGattV.att_notify_args.cap = 5;
    BleGatt.att_notify(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n);
    BleGattV.att_error_rsp_args.req_op = ATT_OP_READ_REQ;
    BleGattV.att_error_rsp_args.handle = 0x0010;
    BleGattV.att_error_rsp_args.error = 0x0A;
    BleGattV.att_error_rsp_args.out = out;
    BleGattV.att_error_rsp_args.cap = 4;
    BleGatt.att_error_rsp(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n);
    BleGattV.att_error_rsp_args.req_op = ATT_OP_READ_REQ;
    BleGattV.att_error_rsp_args.handle = 0x0010;
    BleGattV.att_error_rsp_args.error = 0x0A;
    BleGattV.att_error_rsp_args.out = NULL;
    BleGattV.att_error_rsp_args.cap = sizeof(out);
    BleGatt.att_error_rsp(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n);

    // A zero-length Attribute Value is legal: Vol 3 Part F sec 3.2.9 allows an attribute value of
    // length zero, so the PDU is just its fixed fields.
    BleGattV.att_read_rsp_args.val = NULL;
    BleGattV.att_read_rsp_args.vlen = 0;
    BleGattV.att_read_rsp_args.out = out;
    BleGattV.att_read_rsp_args.cap = sizeof(out);
    BleGatt.att_read_rsp(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(1, BleGattV.n);
    TEST_ASSERT_EQUAL_HEX8(ATT_OP_READ_RSP, out[0]);
    BleGattV.att_write_req_args.handle = 0x0010;
    BleGattV.att_write_req_args.val = NULL;
    BleGattV.att_write_req_args.vlen = 0;
    BleGattV.att_write_req_args.out = out;
    BleGattV.att_write_req_args.cap = sizeof(out);
    BleGatt.att_write_req(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(3, BleGattV.n);
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
    BleGattV.char_json_args.chars = CHARS;
    BleGattV.char_json_args.n = 2;
    BleGattV.char_json_args.out = out;
    BleGattV.char_json_args.cap = sizeof(out);
    BleGatt.char_json(ble_gatt_work);
    const size_t n = BleGattV.n;
    TEST_ASSERT_EQUAL_STRING(
        "[{\"handle\":37,\"uuid\":\"0x2a37\",\"props\":18},{\"handle\":49,\"uuid\":\"0x2a6e\",\"props\":2}]", out);
    TEST_ASSERT_EQUAL_size_t(strlen(out), n);

    // An empty table is the empty array, not an empty string.
    BleGattV.char_json_args.chars = NULL;
    BleGattV.char_json_args.n = 0;
    BleGattV.char_json_args.out = out;
    BleGattV.char_json_args.cap = sizeof(out);
    BleGatt.char_json(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(2, BleGattV.n);
    TEST_ASSERT_EQUAL_STRING("[]", out);

    // The UUID field is always four hex digits, so a low assigned number keeps its leading zeros.
    static const GattChar ONE = {0x0001, 0x002A, GATT_PROP_WRITE};
    BleGattV.char_json_args.chars = &ONE;
    BleGattV.char_json_args.n = 1;
    BleGattV.char_json_args.out = out;
    BleGattV.char_json_args.cap = sizeof(out);
    BleGatt.char_json(ble_gatt_work);
    TEST_ASSERT_TRUE(BleGattV.n > 0);
    TEST_ASSERT_EQUAL_STRING("[{\"handle\":1,\"uuid\":\"0x002a\",\"props\":8}]", out);
}

// The serializer writes the whole array or nothing: a truncated JSON array is not shorter data,
// it is unparseable.
void test_characteristic_table_json_fails_closed(void)
{
    static const GattChar CHARS[2] = {{0x0025, 0x2A37, GATT_PROP_READ | GATT_PROP_NOTIFY},
                                      {0x0031, 0x2A6E, GATT_PROP_READ}};
    char tiny[8];
    BleGattV.char_json_args.chars = CHARS;
    BleGattV.char_json_args.n = 2;
    BleGattV.char_json_args.out = tiny;
    BleGattV.char_json_args.cap = sizeof(tiny);
    BleGatt.char_json(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n);

    char out[64];
    BleGattV.char_json_args.chars = CHARS;
    BleGattV.char_json_args.n = 1;
    BleGattV.char_json_args.out = NULL;
    BleGattV.char_json_args.cap = sizeof(out);
    BleGatt.char_json(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n);
    BleGattV.char_json_args.chars = CHARS;
    BleGattV.char_json_args.n = 1;
    BleGattV.char_json_args.out = out;
    BleGattV.char_json_args.cap = 0;
    BleGatt.char_json(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n);
    BleGattV.char_json_args.chars = NULL;
    BleGattV.char_json_args.n = 1;
    BleGattV.char_json_args.out = out;
    BleGattV.char_json_args.cap = sizeof(out);
    BleGatt.char_json(ble_gatt_work);
    TEST_ASSERT_EQUAL_size_t(0, BleGattV.n);
}
