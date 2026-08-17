// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Siemens S7comm PDU codec (services/fieldbus/s7comm/s7comm.h).
//
// S7comm is not published by Siemens, so the reference here is the one the module itself names: the
// Wireshark dissector epan/dissectors/packet-s7comm.c, whose constants and length rules every S7
// tool on the wire agrees with. The load-bearing case is test_read_response_item_length_rule: the
// dissector computes a data item's byte length as len/8 rounded up for the BBIT / BBYTE / BINT
// transport sizes (3 / 4 / 5) and as len for the rest, and pads every item but the last to an even
// length. Read that rule backwards and a multi-item response decodes into garbage from item two on.

#include "services/fieldbus/s7comm/s7comm.h"
#include <string.h>

#include <unity.h>

static uint8_t s7comm_work[16]; // the borrow an entry takes; S7comm never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// packet-s7comm.c: S7COMM_PROT_ID, the ROSCTR values, the parameter services, the memory areas, the
// item transport sizes, the data transport sizes, the S7ANY syntax id and the OK return code.
void test_dissector_constants(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x32, S7_PROTOCOL_ID);

    TEST_ASSERT_EQUAL_HEX8(0x01, S7_ROSCTR_JOB);
    TEST_ASSERT_EQUAL_HEX8(0x02, S7_ROSCTR_ACK);
    TEST_ASSERT_EQUAL_HEX8(0x03, S7_ROSCTR_ACK_DATA);
    TEST_ASSERT_EQUAL_HEX8(0x07, S7_ROSCTR_USERDATA);

    TEST_ASSERT_EQUAL_HEX8(0xF0, S7_FUNC_SETUP_COMM);
    TEST_ASSERT_EQUAL_HEX8(0x04, S7_FUNC_READ_VAR);
    TEST_ASSERT_EQUAL_HEX8(0x05, S7_FUNC_WRITE_VAR);

    TEST_ASSERT_EQUAL_HEX8(0x81, S7_AREA_INPUTS);
    TEST_ASSERT_EQUAL_HEX8(0x82, S7_AREA_OUTPUTS);
    TEST_ASSERT_EQUAL_HEX8(0x83, S7_AREA_FLAGS);
    TEST_ASSERT_EQUAL_HEX8(0x84, S7_AREA_DB);
    TEST_ASSERT_EQUAL_HEX8(28, S7_AREA_COUNTER);
    TEST_ASSERT_EQUAL_HEX8(29, S7_AREA_TIMER);

    TEST_ASSERT_EQUAL_INT(1, S7_TS_BIT);
    TEST_ASSERT_EQUAL_INT(2, S7_TS_BYTE);
    TEST_ASSERT_EQUAL_INT(3, S7_TS_CHAR);
    TEST_ASSERT_EQUAL_INT(4, S7_TS_WORD);
    TEST_ASSERT_EQUAL_INT(5, S7_TS_INT);
    TEST_ASSERT_EQUAL_INT(6, S7_TS_DWORD);
    TEST_ASSERT_EQUAL_INT(7, S7_TS_DINT);
    TEST_ASSERT_EQUAL_INT(8, S7_TS_REAL);

    TEST_ASSERT_EQUAL_INT(0, S7_DTS_NULL);
    TEST_ASSERT_EQUAL_INT(3, S7_DTS_BIT);   // BBIT, len is in bits
    TEST_ASSERT_EQUAL_INT(4, S7_DTS_BYTE);  // BBYTE, len is in bits
    TEST_ASSERT_EQUAL_INT(5, S7_DTS_INT);   // BINT, len is in bits
    TEST_ASSERT_EQUAL_INT(6, S7_DTS_DINT);  // BDINT, len is in bytes
    TEST_ASSERT_EQUAL_INT(7, S7_DTS_REAL);  // BREAL, len is in bytes
    TEST_ASSERT_EQUAL_INT(9, S7_DTS_OCTET); // BSTR, len is in bytes

    TEST_ASSERT_EQUAL_HEX8(0x10, S7_SYNTAX_S7ANY);
    TEST_ASSERT_EQUAL_HEX8(0xFF, S7_RET_OK);
}

// The header is protocol id(1) ROSCTR(1) redundancy(2) pdu-ref(2) param-len(2) data-len(2), all
// big-endian. A Setup Communication job's parameter is function(1) reserved(1) MaxAmQ calling(2)
// MaxAmQ called(2) PDU length(2) = 8 octets, so the whole job is 18.
void test_setup_communication_job(void)
{
    uint8_t buf[32];
    S7comm.build_setup_args.buf = buf;
    S7comm.build_setup_args.cap = sizeof(buf);
    S7comm.build_setup_args.pdu_ref = 0x0400;
    S7comm.build_setup_args.max_amq_calling = 1;
    S7comm.build_setup_args.max_amq_called = 1;
    S7comm.build_setup_args.pdu_size = 480;
    S7comm.build_setup(s7comm_work);
    size_t n = S7comm.n;
    TEST_ASSERT_EQUAL_UINT(18u, n);
    static const uint8_t WANT[18] = {
        0x32, 0x01, // protocol id, ROSCTR Job
        0x00, 0x00, // redundancy id (reserved)
        0x04, 0x00, // pdu reference
        0x00, 0x08, // parameter length 8
        0x00, 0x00, // data length 0
        0xF0, 0x00, // Setup communication, reserved
        0x00, 0x01, // MaxAmQ calling
        0x00, 0x01, // MaxAmQ called
        0x01, 0xE0, // PDU length 480
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 18);

    S7Header h;
    S7comm.parse_header_args.buf = buf;
    S7comm.parse_header_args.len = n;
    S7comm.parse_header_args.out = &h;
    S7comm.parse_header(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_HEX8(S7_ROSCTR_JOB, h.rosctr);
    TEST_ASSERT_EQUAL_HEX16(0x0400, h.pdu_ref);
    TEST_ASSERT_EQUAL_UINT16(8, h.param_len);
    TEST_ASSERT_EQUAL_UINT16(0, h.data_len);
    TEST_ASSERT_EQUAL_UINT(10u, h.header_len); // a Job header carries no error code
    TEST_ASSERT_EQUAL_PTR(buf + 10, h.param);
}

// A Read Var job's parameter is function(1) item count(1) then a 12-octet S7-ANY item spec each:
// 12h 0Ah 10h transport-size(1) count(2) db-number(2) area(1) bit-address(3). The bit address is
// the byte address times eight, so DB1.DBW20 with two words is spec DB1, addr 20*8 = 160 = 0xA0.
void test_read_request_s7any_item(void)
{
    static const S7ReadItem ITEMS[1] = {{S7_AREA_DB, 1, 20, S7_TS_WORD, 2}};
    uint8_t buf[64];
    S7comm.build_read_request_args.buf = buf;
    S7comm.build_read_request_args.cap = sizeof(buf);
    S7comm.build_read_request_args.pdu_ref = 0x0500;
    S7comm.build_read_request_args.items = ITEMS;
    S7comm.build_read_request_args.n = 1;
    S7comm.build_read_request(s7comm_work);
    size_t n = S7comm.n;
    TEST_ASSERT_EQUAL_UINT(10u + 2u + 12u, n);
    static const uint8_t WANT[24] = {
        0x32, 0x01,       // protocol id, Job
        0x00, 0x00,       // redundancy id
        0x05, 0x00,       // pdu reference
        0x00, 0x0E,       // parameter length 14 = 2 + 12
        0x00, 0x00,       // data length 0
        0x04, 0x01,       // Read Var, one item
        0x12, 0x0A, 0x10, // variable spec, address spec length 10, S7ANY syntax
        0x04,             // transport size WORD
        0x00, 0x02,       // count
        0x00, 0x01,       // DB number
        0x84,             // area DB
        0x00, 0x00, 0xA0, // bit address 160 = byte 20 * 8
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 24);

    S7Header h;
    S7comm.parse_header_args.buf = buf;
    S7comm.parse_header_args.len = n;
    S7comm.parse_header_args.out = &h;
    S7comm.parse_header(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_UINT16(14, h.param_len);
    TEST_ASSERT_EQUAL_HEX8(S7_FUNC_READ_VAR, h.param[0]);
    TEST_ASSERT_EQUAL_HEX8(1, h.param[1]);
}

// Each further item adds another 12-octet spec, and the parameter length tracks it.
void test_read_request_multiple_items(void)
{
    static const S7ReadItem ITEMS[3] = {
        {S7_AREA_DB, 1, 0, S7_TS_BYTE, 4},
        {S7_AREA_FLAGS, 0, 10, S7_TS_BIT, 1},
        {S7_AREA_OUTPUTS, 0, 0x1FFFF, S7_TS_REAL, 1},
    };
    uint8_t buf[64];
    S7comm.build_read_request_args.buf = buf;
    S7comm.build_read_request_args.cap = sizeof(buf);
    S7comm.build_read_request_args.pdu_ref = 1;
    S7comm.build_read_request_args.items = ITEMS;
    S7comm.build_read_request_args.n = 3;
    S7comm.build_read_request(s7comm_work);
    size_t n = S7comm.n;
    TEST_ASSERT_EQUAL_UINT(10u + 2u + 36u, n);

    S7Header h;
    S7comm.parse_header_args.buf = buf;
    S7comm.parse_header_args.len = n;
    S7comm.parse_header_args.out = &h;
    S7comm.parse_header(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_UINT16(38, h.param_len);
    TEST_ASSERT_EQUAL_HEX8(3, h.param[1]);
    for (int i = 0; i < 3; i++)
    {
        const uint8_t *spec = h.param + 2 + 12 * i;
        TEST_ASSERT_EQUAL_HEX8(0x12, spec[0]);
        TEST_ASSERT_EQUAL_HEX8(0x0A, spec[1]);
        TEST_ASSERT_EQUAL_HEX8(S7_SYNTAX_S7ANY, spec[2]);
        TEST_ASSERT_EQUAL_HEX8(ITEMS[i].transport_size, spec[3]);
        TEST_ASSERT_EQUAL_UINT16(ITEMS[i].count, (uint16_t)((spec[4] << 8) | spec[5]));
        TEST_ASSERT_EQUAL_UINT16(ITEMS[i].db_number, (uint16_t)((spec[6] << 8) | spec[7]));
        TEST_ASSERT_EQUAL_HEX8(ITEMS[i].area, spec[8]);
        uint32_t bitaddr = ((uint32_t)spec[9] << 16) | ((uint32_t)spec[10] << 8) | spec[11];
        TEST_ASSERT_EQUAL_HEX32(ITEMS[i].byte_address * 8u, bitaddr);
    }
}

// The dissector's rule: a data item's length field is in bits for BBIT / BBYTE / BINT and in bytes
// otherwise, rounded up to whole octets. Each case below is a hand-assembled Ack_Data data section.
void test_read_response_item_length_rule(void)
{
    // one BYTE item of four octets: length 4 * 8 = 32 bits
    static const uint8_t BITS_LEN[8] = {0xFF, 0x04, 0x00, 0x20, 0x11, 0x22, 0x33, 0x44};
    size_t off = 0;
    S7DataItem it;
    S7comm.read_next_item_args.data = BITS_LEN;
    S7comm.read_next_item_args.data_len = sizeof(BITS_LEN);
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_HEX8(S7_RET_OK, it.return_code);
    TEST_ASSERT_EQUAL_HEX8(S7_DTS_BYTE, it.transport_size);
    TEST_ASSERT_EQUAL_UINT(4u, it.data_len);
    TEST_ASSERT_EQUAL_HEX8(0x11, it.data[0]);
    TEST_ASSERT_EQUAL_UINT(sizeof(BITS_LEN), off);

    // a single bit: length 1 bit, which rounds up to one octet
    static const uint8_t ONE_BIT[5] = {0xFF, 0x03, 0x00, 0x01, 0x01};
    off = 0;
    S7comm.read_next_item_args.data = ONE_BIT;
    S7comm.read_next_item_args.data_len = sizeof(ONE_BIT);
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_HEX8(S7_DTS_BIT, it.transport_size);
    TEST_ASSERT_EQUAL_UINT(1u, it.data_len);
    TEST_ASSERT_EQUAL_HEX8(0x01, it.data[0]);

    // a REAL is the byte-length form: length 4 means four octets
    static const uint8_t REAL[8] = {0xFF, 0x07, 0x00, 0x04, 0x41, 0x20, 0x00, 0x00};
    off = 0;
    S7comm.read_next_item_args.data = REAL;
    S7comm.read_next_item_args.data_len = sizeof(REAL);
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_HEX8(S7_DTS_REAL, it.transport_size);
    TEST_ASSERT_EQUAL_UINT(4u, it.data_len);

    // an octet string is the byte-length form too
    static const uint8_t OCTET[7] = {0xFF, 0x09, 0x00, 0x03, 'a', 'b', 'c'};
    off = 0;
    S7comm.read_next_item_args.data = OCTET;
    S7comm.read_next_item_args.data_len = sizeof(OCTET);
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_HEX8(S7_DTS_OCTET, it.transport_size);
    TEST_ASSERT_EQUAL_UINT(3u, it.data_len);
    TEST_ASSERT_EQUAL_HEX8('a', it.data[0]);
}

// "if ((len % 2) && (i < item_count-1))": every item but the last is padded to an even length, and
// the walk has to step over that fill octet or the next item's return code is read from the pad.
void test_read_response_even_padding(void)
{
    static const uint8_t TWO_ITEMS[] = {
        0xFF, 0x04, 0x00, 0x08, 0xAA,       // item 1: BYTE, 8 bits = 1 octet, odd
        0x00,                               // fill octet, not part of the value
        0xFF, 0x04, 0x00, 0x10, 0xBB, 0xCC, // item 2: BYTE, 16 bits = 2 octets, and the last
    };
    size_t off = 0;
    S7DataItem it;

    S7comm.read_next_item_args.data = TWO_ITEMS;
    S7comm.read_next_item_args.data_len = sizeof(TWO_ITEMS);
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_UINT(1u, it.data_len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, it.data[0]);
    TEST_ASSERT_EQUAL_UINT(6u, off); // 4 header + 1 value + 1 pad

    S7comm.read_next_item_args.data = TWO_ITEMS;
    S7comm.read_next_item_args.data_len = sizeof(TWO_ITEMS);
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_UINT(2u, it.data_len);
    TEST_ASSERT_EQUAL_HEX8(0xBB, it.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, it.data[1]);
    TEST_ASSERT_EQUAL_UINT(sizeof(TWO_ITEMS), off);

    S7comm.read_next_item_args.data = TWO_ITEMS;
    S7comm.read_next_item_args.data_len = sizeof(TWO_ITEMS);
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);

    // an odd-length LAST item carries no pad, so the section ends exactly on its value
    static const uint8_t LAST_ODD[5] = {0xFF, 0x04, 0x00, 0x08, 0xAA};
    off = 0;
    S7comm.read_next_item_args.data = LAST_ODD;
    S7comm.read_next_item_args.data_len = sizeof(LAST_ODD);
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(LAST_ODD), off);
    S7comm.read_next_item_args.data = LAST_ODD;
    S7comm.read_next_item_args.data_len = sizeof(LAST_ODD);
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);
}

// The Write Var job mirrors the read's parameter and appends one data item per write, whose length
// field follows the same bit/byte rule the response reader decodes. Building and reading back
// through that one rule is what proves the two agree.
void test_write_request_round_trips_the_length_rule(void)
{
    static const uint8_t V1[1] = {0xAA};
    static const uint8_t V2[4] = {0x41, 0x20, 0x00, 0x00};
    static const S7WriteItem ITEMS[2] = {
        {S7_AREA_DB, 1, 0, S7_TS_BYTE, 1, S7_DTS_BYTE, V1, 1},
        {S7_AREA_DB, 1, 4, S7_TS_REAL, 1, S7_DTS_REAL, V2, 4},
    };
    uint8_t buf[64];
    S7comm.build_write_request_args.buf = buf;
    S7comm.build_write_request_args.cap = sizeof(buf);
    S7comm.build_write_request_args.pdu_ref = 0x0600;
    S7comm.build_write_request_args.items = ITEMS;
    S7comm.build_write_request_args.n = 2;
    S7comm.build_write_request(s7comm_work);
    size_t n = S7comm.n;
    // parameter 2 + 24, data (4 + 1 + 1 pad) + (4 + 4) = 14, header 10
    TEST_ASSERT_EQUAL_UINT(10u + 26u + 14u, n);

    S7Header h;
    S7comm.parse_header_args.buf = buf;
    S7comm.parse_header_args.len = n;
    S7comm.parse_header_args.out = &h;
    S7comm.parse_header(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_UINT16(26, h.param_len);
    TEST_ASSERT_EQUAL_UINT16(14, h.data_len);
    TEST_ASSERT_EQUAL_HEX8(S7_FUNC_WRITE_VAR, h.param[0]);
    TEST_ASSERT_EQUAL_HEX8(2, h.param[1]);

    // the first item's length field is in bits (8) and the second's in bytes (4)
    TEST_ASSERT_EQUAL_HEX8(0x00, h.data[0]); // return code, reserved in a request
    TEST_ASSERT_EQUAL_HEX8(S7_DTS_BYTE, h.data[1]);
    TEST_ASSERT_EQUAL_UINT16(8, (uint16_t)((h.data[2] << 8) | h.data[3]));
    TEST_ASSERT_EQUAL_HEX8(S7_DTS_REAL, h.data[7]);
    TEST_ASSERT_EQUAL_UINT16(4, (uint16_t)((h.data[8] << 8) | h.data[9]));

    size_t off = 0;
    S7DataItem it;
    S7comm.read_next_item_args.data = h.data;
    S7comm.read_next_item_args.data_len = h.data_len;
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_UINT(1u, it.data_len);
    TEST_ASSERT_EQUAL_HEX8(0xAA, it.data[0]);
    S7comm.read_next_item_args.data = h.data;
    S7comm.read_next_item_args.data_len = h.data_len;
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_UINT(4u, it.data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(V2, it.data, 4);
    TEST_ASSERT_EQUAL_UINT(h.data_len, off);

    // the parameter's S7-ANY specs are the same 12 octets a read would write
    uint8_t rd[64];
    static const S7ReadItem READ_SPEC[2] = {{S7_AREA_DB, 1, 0, S7_TS_BYTE, 1}, {S7_AREA_DB, 1, 4, S7_TS_REAL, 1}};
    S7comm.build_read_request_args.buf = rd;
    S7comm.build_read_request_args.cap = sizeof(rd);
    S7comm.build_read_request_args.pdu_ref = 0x0600;
    S7comm.build_read_request_args.items = READ_SPEC;
    S7comm.build_read_request_args.n = 2;
    S7comm.build_read_request(s7comm_work);
    TEST_ASSERT_EQUAL_UINT(10u + 26u, S7comm.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(rd + 12, buf + 12, 24);
}

// A response ROSCTR carries an error class and error code after the lengths, so its header is 12
// octets and the parameter section starts two octets later than a job's.
void test_response_header_carries_the_error_code(void)
{
    static const uint8_t ACK_DATA[] = {
        0x32, 0x03,                   // protocol id, ROSCTR Ack_Data
        0x00, 0x00,                   // redundancy id
        0x05, 0x00,                   // pdu reference
        0x00, 0x02,                   // parameter length 2
        0x00, 0x05,                   // data length 5
        0x00, 0x00,                   // error class, error code
        0x04, 0x01,                   // parameter: Read Var, one item
        0xFF, 0x04, 0x00, 0x08, 0xAA, // data: one BYTE item of one octet
    };
    S7Header h;
    S7comm.parse_header_args.buf = ACK_DATA;
    S7comm.parse_header_args.len = sizeof(ACK_DATA);
    S7comm.parse_header_args.out = &h;
    S7comm.parse_header(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_HEX8(S7_ROSCTR_ACK_DATA, h.rosctr);
    TEST_ASSERT_EQUAL_UINT(12u, h.header_len);
    TEST_ASSERT_EQUAL_UINT16(2, h.param_len);
    TEST_ASSERT_EQUAL_UINT16(5, h.data_len);
    TEST_ASSERT_EQUAL_HEX8(0x00, h.error_class);
    TEST_ASSERT_EQUAL_HEX8(0x00, h.error_code);
    TEST_ASSERT_EQUAL_PTR(ACK_DATA + 12, h.param);
    TEST_ASSERT_EQUAL_PTR(ACK_DATA + 14, h.data);
    TEST_ASSERT_EQUAL_HEX8(S7_FUNC_READ_VAR, h.param[0]);

    size_t off = 0;
    S7DataItem it;
    S7comm.read_next_item_args.data = h.data;
    S7comm.read_next_item_args.data_len = h.data_len;
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_HEX8(0xAA, it.data[0]);

    // a plain Ack response is the other 12-octet ROSCTR
    static const uint8_t ACK[12] = {0x32, 0x02, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x04};
    S7comm.parse_header_args.buf = ACK;
    S7comm.parse_header_args.len = sizeof(ACK);
    S7comm.parse_header_args.out = &h;
    S7comm.parse_header(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);
    TEST_ASSERT_EQUAL_UINT(12u, h.header_len);
    TEST_ASSERT_EQUAL_HEX8(0x81, h.error_class);
    TEST_ASSERT_EQUAL_HEX8(0x04, h.error_code);
}

// The first octet of every S7comm PDU is 32h, the declared lengths must fit the buffer, and a
// header shorter than its own ROSCTR requires is not a PDU.
void test_header_validation(void)
{
    S7Header h;
    uint8_t buf[32];

    S7comm.build_setup_args.buf = buf;
    S7comm.build_setup_args.cap = sizeof(buf);
    S7comm.build_setup_args.pdu_ref = 1;
    S7comm.build_setup_args.max_amq_calling = 1;
    S7comm.build_setup_args.max_amq_called = 1;
    S7comm.build_setup_args.pdu_size = 480;
    S7comm.build_setup(s7comm_work);
    TEST_ASSERT_EQUAL_UINT(18u, S7comm.n);
    S7comm.parse_header_args.buf = buf;
    S7comm.parse_header_args.len = 18;
    S7comm.parse_header_args.out = &h;
    S7comm.parse_header(s7comm_work);
    TEST_ASSERT_TRUE(S7comm.ok);

    buf[0] = 0x33; // not the S7comm protocol id
    S7comm.parse_header_args.buf = buf;
    S7comm.parse_header_args.len = 18;
    S7comm.parse_header_args.out = &h;
    S7comm.parse_header(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);
    buf[0] = S7_PROTOCOL_ID;

    buf[7] = 0x40; // a parameter length the buffer does not hold
    S7comm.parse_header_args.buf = buf;
    S7comm.parse_header_args.len = 18;
    S7comm.parse_header_args.out = &h;
    S7comm.parse_header(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);
    buf[7] = 0x08;
    buf[9] = 0x40; // a data length the buffer does not hold
    S7comm.parse_header_args.buf = buf;
    S7comm.parse_header_args.len = 18;
    S7comm.parse_header_args.out = &h;
    S7comm.parse_header(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);

    for (size_t n = 0; n < 10; n++)
    {
        S7comm.parse_header_args.buf = buf;
        S7comm.parse_header_args.len = n;
        S7comm.parse_header_args.out = &h;
        S7comm.parse_header(s7comm_work);
        TEST_ASSERT_FALSE(S7comm.ok);
    }
    // an Ack_Data truncated inside its own error code
    static const uint8_t SHORT_ACK[11] = {0x32, 0x03, 0, 0, 0, 1, 0, 0, 0, 0, 0};
    S7comm.parse_header_args.buf = SHORT_ACK;
    S7comm.parse_header_args.len = sizeof(SHORT_ACK);
    S7comm.parse_header_args.out = &h;
    S7comm.parse_header(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);

    S7comm.parse_header_args.buf = NULL;
    S7comm.parse_header_args.len = 18;
    S7comm.parse_header_args.out = &h;
    S7comm.parse_header(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);
    S7comm.parse_header_args.buf = buf;
    S7comm.parse_header_args.len = 18;
    S7comm.parse_header_args.out = NULL;
    S7comm.parse_header(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);
}

// A data item whose declared length runs past the end of the section is refused rather than read
// past it, and a section too short even for the 4-octet item header ends the walk.
void test_read_item_refuses_an_overrun(void)
{
    size_t off = 0;
    S7DataItem it;

    static const uint8_t OVER[6] = {0xFF, 0x09, 0x00, 0x08, 0x01, 0x02}; // claims 8 bytes, carries 2
    off = 0;
    S7comm.read_next_item_args.data = OVER;
    S7comm.read_next_item_args.data_len = sizeof(OVER);
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);

    static const uint8_t OVER_BITS[6] = {0xFF, 0x04, 0x00, 0x40, 0x01, 0x02}; // 64 bits = 8 bytes, carries 2
    off = 0;
    S7comm.read_next_item_args.data = OVER_BITS;
    S7comm.read_next_item_args.data_len = sizeof(OVER_BITS);
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);

    static const uint8_t STUB[3] = {0xFF, 0x04, 0x00};
    off = 0;
    S7comm.read_next_item_args.data = STUB;
    S7comm.read_next_item_args.data_len = sizeof(STUB);
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);

    S7comm.read_next_item_args.data = NULL;
    S7comm.read_next_item_args.data_len = 4;
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);
    S7comm.read_next_item_args.data = STUB;
    S7comm.read_next_item_args.data_len = 4;
    S7comm.read_next_item_args.offset = NULL;
    S7comm.read_next_item_args.out = &it;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);
    S7comm.read_next_item_args.data = STUB;
    S7comm.read_next_item_args.data_len = 4;
    S7comm.read_next_item_args.offset = &off;
    S7comm.read_next_item_args.out = NULL;
    S7comm.read_next_item(s7comm_work);
    TEST_ASSERT_FALSE(S7comm.ok);
}

// A builder given less room than the PDU needs writes nothing, and a request with no items or with
// a null value pointer is refused.
void test_builders_refuse_bad_arguments(void)
{
    uint8_t buf[64];
    static const S7ReadItem READ_ONE[1] = {{S7_AREA_DB, 1, 0, S7_TS_WORD, 1}};
    static const uint8_t V[2] = {1, 2};
    static const S7WriteItem WRITE_ONE[1] = {{S7_AREA_DB, 1, 0, S7_TS_WORD, 1, S7_DTS_BYTE, V, 2}};

    for (size_t cap = 0; cap < 18; cap++)
    {
        S7comm.build_setup_args.buf = buf;
        S7comm.build_setup_args.cap = cap;
        S7comm.build_setup_args.pdu_ref = 1;
        S7comm.build_setup_args.max_amq_calling = 1;
        S7comm.build_setup_args.max_amq_called = 1;
        S7comm.build_setup_args.pdu_size = 480;
        S7comm.build_setup(s7comm_work);
        TEST_ASSERT_EQUAL_UINT(0u, S7comm.n);
    }
    S7comm.build_setup_args.buf = NULL;
    S7comm.build_setup_args.cap = sizeof(buf);
    S7comm.build_setup_args.pdu_ref = 1;
    S7comm.build_setup_args.max_amq_calling = 1;
    S7comm.build_setup_args.max_amq_called = 1;
    S7comm.build_setup_args.pdu_size = 480;
    S7comm.build_setup(s7comm_work);
    TEST_ASSERT_EQUAL_UINT(0u, S7comm.n);

    for (size_t cap = 0; cap < 24; cap++)
    {
        S7comm.build_read_request_args.buf = buf;
        S7comm.build_read_request_args.cap = cap;
        S7comm.build_read_request_args.pdu_ref = 1;
        S7comm.build_read_request_args.items = READ_ONE;
        S7comm.build_read_request_args.n = 1;
        S7comm.build_read_request(s7comm_work);
        TEST_ASSERT_EQUAL_UINT(0u, S7comm.n);
    }
    S7comm.build_read_request_args.buf = buf;
    S7comm.build_read_request_args.cap = sizeof(buf);
    S7comm.build_read_request_args.pdu_ref = 1;
    S7comm.build_read_request_args.items = READ_ONE;
    S7comm.build_read_request_args.n = 0;
    S7comm.build_read_request(s7comm_work);
    TEST_ASSERT_EQUAL_UINT(0u, S7comm.n);
    S7comm.build_read_request_args.buf = buf;
    S7comm.build_read_request_args.cap = sizeof(buf);
    S7comm.build_read_request_args.pdu_ref = 1;
    S7comm.build_read_request_args.items = NULL;
    S7comm.build_read_request_args.n = 1;
    S7comm.build_read_request(s7comm_work);
    TEST_ASSERT_EQUAL_UINT(0u, S7comm.n);
    S7comm.build_read_request_args.buf = NULL;
    S7comm.build_read_request_args.cap = sizeof(buf);
    S7comm.build_read_request_args.pdu_ref = 1;
    S7comm.build_read_request_args.items = READ_ONE;
    S7comm.build_read_request_args.n = 1;
    S7comm.build_read_request(s7comm_work);
    TEST_ASSERT_EQUAL_UINT(0u, S7comm.n);

    for (size_t cap = 0; cap < 30; cap++)
    {
        S7comm.build_write_request_args.buf = buf;
        S7comm.build_write_request_args.cap = cap;
        S7comm.build_write_request_args.pdu_ref = 1;
        S7comm.build_write_request_args.items = WRITE_ONE;
        S7comm.build_write_request_args.n = 1;
        S7comm.build_write_request(s7comm_work);
        TEST_ASSERT_EQUAL_UINT(0u, S7comm.n);
    }
    S7comm.build_write_request_args.buf = buf;
    S7comm.build_write_request_args.cap = 30;
    S7comm.build_write_request_args.pdu_ref = 1;
    S7comm.build_write_request_args.items = WRITE_ONE;
    S7comm.build_write_request_args.n = 1;
    S7comm.build_write_request(s7comm_work);
    TEST_ASSERT_EQUAL_UINT(30u, S7comm.n);
    S7comm.build_write_request_args.buf = buf;
    S7comm.build_write_request_args.cap = sizeof(buf);
    S7comm.build_write_request_args.pdu_ref = 1;
    S7comm.build_write_request_args.items = WRITE_ONE;
    S7comm.build_write_request_args.n = 0;
    S7comm.build_write_request(s7comm_work);
    TEST_ASSERT_EQUAL_UINT(0u, S7comm.n);
    S7comm.build_write_request_args.buf = NULL;
    S7comm.build_write_request_args.cap = sizeof(buf);
    S7comm.build_write_request_args.pdu_ref = 1;
    S7comm.build_write_request_args.items = WRITE_ONE;
    S7comm.build_write_request_args.n = 1;
    S7comm.build_write_request(s7comm_work);
    TEST_ASSERT_EQUAL_UINT(0u, S7comm.n);

    static const S7WriteItem NULL_DATA[1] = {{S7_AREA_DB, 1, 0, S7_TS_WORD, 1, S7_DTS_BYTE, NULL, 2}};
    S7comm.build_write_request_args.buf = buf;
    S7comm.build_write_request_args.cap = sizeof(buf);
    S7comm.build_write_request_args.pdu_ref = 1;
    S7comm.build_write_request_args.items = NULL_DATA;
    S7comm.build_write_request_args.n = 1;
    S7comm.build_write_request(s7comm_work);
    TEST_ASSERT_EQUAL_UINT(0u, S7comm.n);
}
