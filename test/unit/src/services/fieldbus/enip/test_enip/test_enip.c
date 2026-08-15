// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the EtherNet/IP encapsulation codec (services/fieldbus/enip/enip.h).
//
// The governing text is the ODVA EtherNet/IP specification, Volume 2 chapter 2: a 24-octet
// encapsulation header whose fields are all little-endian, a published command-code registry, and
// the Common Packet Format item-type registry. test_register_session_octets is the load-bearing
// case: RegisterSession is the first message every originator sends, its 28 octets are fully
// determined by that layout, and each expected octet below is placed from the field table rather
// than copied out of the builder.

#include "services/fieldbus/enip/enip.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t CTX[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

// Volume 2 chapter 2 command-code and CPF item-type registries, and the fixed header width.
void test_published_registry_values(void)
{
    TEST_ASSERT_EQUAL_size_t(24u, (size_t)EIP_HEADER_SIZE);

    TEST_ASSERT_EQUAL_HEX16(0x0004u, EIP_CMD_LIST_SERVICES);
    TEST_ASSERT_EQUAL_HEX16(0x0063u, EIP_CMD_LIST_IDENTITY);
    TEST_ASSERT_EQUAL_HEX16(0x0064u, EIP_CMD_LIST_INTERFACES);
    TEST_ASSERT_EQUAL_HEX16(0x0065u, EIP_CMD_REGISTER_SESSION);
    TEST_ASSERT_EQUAL_HEX16(0x0066u, EIP_CMD_UNREGISTER_SESSION);
    TEST_ASSERT_EQUAL_HEX16(0x006Fu, EIP_CMD_SEND_RR_DATA);
    TEST_ASSERT_EQUAL_HEX16(0x0070u, EIP_CMD_SEND_UNIT_DATA);
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, EIP_STATUS_SUCCESS);

    TEST_ASSERT_EQUAL_HEX16(0x0000u, EIP_CPF_NULL);
    TEST_ASSERT_EQUAL_HEX16(0x000Cu, EIP_CPF_LIST_IDENTITY);
    TEST_ASSERT_EQUAL_HEX16(0x00A1u, EIP_CPF_CONNECTED_ADDRESS);
    TEST_ASSERT_EQUAL_HEX16(0x00B1u, EIP_CPF_CONNECTED_DATA);
    TEST_ASSERT_EQUAL_HEX16(0x00B2u, EIP_CPF_UNCONNECTED_DATA);
}

// RegisterSession, laid out from the header field table:
//   [0..1]   command        0x0065 little-endian -> 65 00
//   [2..3]   length         4 octets of command data -> 04 00
//   [4..7]   session handle 0 (the target assigns it in the reply)
//   [8..11]  status         0
//   [12..19] sender context echoed verbatim by the target
//   [20..23] options        0
//   [24..25] protocol version 1 -> 01 00
//   [26..27] options flags    0 -> 00 00
void test_register_session_octets(void)
{
    static const uint8_t WANT[28] = {
        0x65, 0x00,                                     // command
        0x04, 0x00,                                     // length
        0x00, 0x00, 0x00, 0x00,                         // session handle
        0x00, 0x00, 0x00, 0x00,                         // status
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // sender context
        0x00, 0x00, 0x00, 0x00,                         // options
        0x01, 0x00,                                     // protocol version
        0x00, 0x00,                                     // options flags
    };
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), protocore_eip_build_register_session(buf, sizeof(buf), CTX));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));

    // A null context is the same request with eight zero octets.
    static const uint8_t ZEROS[8] = {0};
    TEST_ASSERT_EQUAL_size_t(28u, protocore_eip_build_register_session(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ZEROS, buf + 12, 8);
}

// UnRegisterSession and ListIdentity carry no command-specific data, so both are the bare header.
// UnRegisterSession names the session it closes; ListIdentity is sent before any session exists.
void test_headers_without_command_data(void)
{
    uint8_t buf[64];

    TEST_ASSERT_EQUAL_size_t(24u, protocore_eip_build_unregister_session(buf, sizeof(buf), 0x11223344u, CTX));
    static const uint8_t UNREG[8] = {0x66, 0x00, 0x00, 0x00, 0x44, 0x33, 0x22, 0x11};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(UNREG, buf, sizeof(UNREG)); // command, length 0, handle little-endian

    TEST_ASSERT_EQUAL_size_t(24u, protocore_eip_build_list_identity(buf, sizeof(buf), CTX));
    static const uint8_t LISTID[8] = {0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(LISTID, buf, sizeof(LISTID));
}

// SendRRData wraps an unconnected CIP message in the Common Packet Format:
//   interface handle(4)=0, timeout(2), item count(2)=2,
//   Null Address item   { type 0x0000, length 0 },
//   Unconnected Data item { type 0x00B2, length = CIP octets }, then the CIP message.
// With a 4-octet CIP request the command data is 4+2+2+4+4+4 = 20 octets, so length is 0x0014.
void test_send_rr_data_common_packet_format(void)
{
    static const uint8_t CIP[4] = {0x0E, 0x03, 0x20, 0x01};
    static const uint8_t WANT[44] = {
        0x6F, 0x00,                                     // SendRRData
        0x14, 0x00,                                     // 20 octets of command data
        0x21, 0x43, 0x65, 0x87,                         // session handle 0x87654321 little-endian
        0x00, 0x00, 0x00, 0x00,                         // status
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // sender context
        0x00, 0x00, 0x00, 0x00,                         // options
        0x00, 0x00, 0x00, 0x00,                         // interface handle (CIP)
        0x0A, 0x00,                                     // timeout 10 s
        0x02, 0x00,                                     // CPF item count
        0x00, 0x00, 0x00, 0x00,                         // Null Address item, length 0
        0xB2, 0x00, 0x04, 0x00,                         // Unconnected Data item, length 4
        0x0E, 0x03, 0x20, 0x01,                         // the CIP message
    };
    uint8_t buf[64];
    size_t n = protocore_eip_build_send_rr_data(buf, sizeof(buf), 0x87654321u, CTX, 10, CIP, sizeof(CIP));
    TEST_ASSERT_EQUAL_size_t(sizeof(WANT), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, sizeof(WANT));

    // The reply extractor walks the same CPF and hands back exactly the CIP octets.
    EipHeader h;
    const uint8_t *data = NULL;
    size_t data_len = 0;
    TEST_ASSERT_TRUE(protocore_eip_parse(buf, n, &h, &data, &data_len));
    TEST_ASSERT_EQUAL_HEX16(EIP_CMD_SEND_RR_DATA, h.command);
    TEST_ASSERT_EQUAL_HEX32(0x87654321u, h.session_handle);
    TEST_ASSERT_EQUAL_size_t(20u, data_len);

    const uint8_t *cip = NULL;
    size_t cip_len = 0;
    TEST_ASSERT_TRUE(protocore_eip_parse_send_rr_data(data, data_len, &cip, &cip_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(CIP), cip_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CIP, cip, sizeof(CIP));
}

// Every header field survives a build/parse round trip, including the eight context octets the
// target must echo so an originator can match a reply to its request.
void test_header_round_trip(void)
{
    EipHeader h;
    memset(&h, 0, sizeof(h));
    h.command = EIP_CMD_SEND_UNIT_DATA;
    h.session_handle = 0xDEADBEEFu;
    h.status = 0x00000064u;
    memcpy(h.sender_context, CTX, 8);
    h.options = 0xA5A5A5A5u;

    static const uint8_t DATA[3] = {0xAA, 0xBB, 0xCC};
    uint8_t buf[64];
    size_t n = protocore_eip_build(buf, sizeof(buf), &h, DATA, sizeof(DATA));
    TEST_ASSERT_EQUAL_size_t(27u, n);

    EipHeader got;
    const uint8_t *data = NULL;
    size_t data_len = 0;
    TEST_ASSERT_TRUE(protocore_eip_parse(buf, n, &got, &data, &data_len));
    TEST_ASSERT_EQUAL_HEX16(h.command, got.command);
    TEST_ASSERT_EQUAL_UINT16(3u, got.length);
    TEST_ASSERT_EQUAL_HEX32(h.session_handle, got.session_handle);
    TEST_ASSERT_EQUAL_HEX32(h.status, got.status);
    TEST_ASSERT_EQUAL_HEX32(h.options, got.options);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CTX, got.sender_context, 8);
    TEST_ASSERT_EQUAL_size_t(sizeof(DATA), data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, data, sizeof(DATA));
}

// A header whose Length field claims more data than arrived is refused rather than handing out a
// slice past the end of the buffer.
void test_parse_refuses_a_truncated_message(void)
{
    static const uint8_t DATA[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    EipHeader h;
    memset(&h, 0, sizeof(h));
    h.command = EIP_CMD_SEND_RR_DATA;

    uint8_t buf[64];
    size_t n = protocore_eip_build(buf, sizeof(buf), &h, DATA, sizeof(DATA));
    TEST_ASSERT_EQUAL_size_t(32u, n);

    EipHeader got;
    TEST_ASSERT_TRUE(protocore_eip_parse(buf, n, &got, NULL, NULL));
    TEST_ASSERT_FALSE(protocore_eip_parse(buf, n - 1, &got, NULL, NULL)); // one octet short
    TEST_ASSERT_FALSE(protocore_eip_parse(buf, 23, &got, NULL, NULL));    // shorter than the header
    TEST_ASSERT_FALSE(protocore_eip_parse(NULL, n, &got, NULL, NULL));
}

// A ListIdentity reply item, laid out from Volume 2's identity-item field table:
//   protocol version(2), CIP socket address(16), vendor id(2), device type(2), product code(2),
//   revision major(1) minor(1), status(2), serial number(4), name length(1), name, state(1).
// Vendor 1 is Rockwell Automation in the ODVA vendor-ID registry and device type 0x000C is the
// Communications Adapter profile, so this is a plausible reply as well as a well-formed one.
void test_list_identity_item(void)
{
    static const uint8_t NAME[6] = {'1', '7', '5', '6', '-', 'E'};
    uint8_t item[34 + sizeof(NAME)];
    memset(item, 0, sizeof(item));
    item[0] = 0x01;
    item[1] = 0x00; // protocol version 1
    // item[2..17] is the 16-octet socket address; left zero, the codec does not reinterpret it.
    item[18] = 0x01;
    item[19] = 0x00; // vendor id 1
    item[20] = 0x0C;
    item[21] = 0x00; // device type 0x000C
    item[22] = 0x34;
    item[23] = 0x12; // product code 0x1234
    item[24] = 0x02; // revision major
    item[25] = 0x05; // revision minor
    item[26] = 0x30;
    item[27] = 0x00; // status 0x0030
    item[28] = 0x78;
    item[29] = 0x56;
    item[30] = 0x34;
    item[31] = 0x12; // serial 0x12345678 little-endian
    item[32] = (uint8_t)sizeof(NAME);
    memcpy(item + 33, NAME, sizeof(NAME));
    item[33 + sizeof(NAME)] = 0x03; // device state

    // The CPF block: item count, then the item's type, length and body.
    uint8_t block[6 + sizeof(item)];
    block[0] = 0x01;
    block[1] = 0x00;
    block[2] = 0x0C;
    block[3] = 0x00;
    block[4] = (uint8_t)(sizeof(item) & 0xFF);
    block[5] = (uint8_t)(sizeof(item) >> 8);
    memcpy(block + 6, item, sizeof(item));

    EipIdentity id;
    memset(&id, 0, sizeof(id));
    TEST_ASSERT_TRUE(protocore_eip_parse_list_identity(block, sizeof(block), &id));
    TEST_ASSERT_EQUAL_HEX16(0x0001u, id.protocol_version);
    TEST_ASSERT_EQUAL_HEX16(0x0001u, id.vendor_id);
    TEST_ASSERT_EQUAL_HEX16(0x000Cu, id.device_type);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, id.product_code);
    TEST_ASSERT_EQUAL_HEX8(0x02u, id.revision_major);
    TEST_ASSERT_EQUAL_HEX8(0x05u, id.revision_minor);
    TEST_ASSERT_EQUAL_HEX16(0x0030u, id.status);
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, id.serial_number);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)sizeof(NAME), id.product_name_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NAME, (const uint8_t *)id.product_name, sizeof(NAME));
    TEST_ASSERT_EQUAL_HEX8(0x03u, id.state);

    // An item whose declared length stops short of the name is refused, not read past.
    block[4] = 33;
    TEST_ASSERT_FALSE(protocore_eip_parse_list_identity(block, sizeof(block), &id));
}

// A CPF block holding no item of the wanted type is a refusal, not a silent zero-length slice.
void test_cpf_walk_refuses_a_missing_item(void)
{
    // interface handle(4) + timeout(2) + count(2)=1 + a Connected Data item carrying two octets.
    static const uint8_t DATA[16] = {
        0x00, 0x00, 0x00, 0x00, // interface handle
        0x00, 0x00,             // timeout
        0x01, 0x00,             // one item
        0xB1, 0x00, 0x02, 0x00, // Connected Data item, length 2
        0xAA, 0xBB,             //
        0x00, 0x00,             // trailing octets the walk must not mistake for an item
    };
    const uint8_t *cip = (const uint8_t *)1;
    size_t cip_len = 99;
    TEST_ASSERT_FALSE(protocore_eip_parse_send_rr_data(DATA, 14, &cip, &cip_len));

    // An item whose declared length runs past the block is refused too.
    static const uint8_t OVERRUN[12] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xB2, 0x00, 0xFF, 0x00};
    TEST_ASSERT_FALSE(protocore_eip_parse_send_rr_data(OVERRUN, sizeof(OVERRUN), &cip, &cip_len));

    TEST_ASSERT_FALSE(protocore_eip_parse_send_rr_data(DATA, 7, &cip, &cip_len)); // shorter than the CPF preamble
}

// A buffer that cannot hold header plus command data yields 0 rather than a partial message.
void test_builders_refuse_a_short_buffer(void)
{
    uint8_t buf[64];
    static const uint8_t CIP[4] = {0x0E, 0x03, 0x20, 0x01};

    TEST_ASSERT_EQUAL_size_t(0u, protocore_eip_build_register_session(buf, 27, CTX));
    TEST_ASSERT_EQUAL_size_t(28u, protocore_eip_build_register_session(buf, 28, CTX));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_eip_build_list_identity(buf, 23, CTX));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_eip_build_send_rr_data(buf, 43, 1u, CTX, 10, CIP, sizeof(CIP)));
    TEST_ASSERT_EQUAL_size_t(44u, protocore_eip_build_send_rr_data(buf, 44, 1u, CTX, 10, CIP, sizeof(CIP)));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_eip_build_send_rr_data(buf, sizeof(buf), 1u, CTX, 10, NULL, 4));

    EipHeader h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_eip_build(NULL, sizeof(buf), &h, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_eip_build(buf, sizeof(buf), NULL, NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0u, protocore_eip_build(buf, sizeof(buf), &h, NULL, 4));
}
