// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the EtherNet/IP encapsulation codec (services/fieldbus/enip): the header, the
// RegisterSession + SendRRData builders, and the SendRRData (CPF) reply extractor.
// Little-endian; constants per the Wireshark ENIP dissector. Pure host tests.

#include "services/fieldbus/enip/enip.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_header_round_trip()
{
    EipHeader h;
    memset(&h, 0, sizeof(h));
    h.command = EIP_CMD_REGISTER_SESSION;
    h.session_handle = 0x12345678;
    h.status = EIP_STATUS_SUCCESS;
    const uint8_t ctx[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    memcpy(h.sender_context, ctx, 8);
    const uint8_t data[] = {0xAA, 0xBB};
    uint8_t buf[32];
    size_t n = pc_eip_build(buf, sizeof(buf), &h, data, sizeof(data));
    TEST_ASSERT_EQUAL_size_t(EIP_HEADER_SIZE + 2, n);
    // command + length, little-endian.
    TEST_ASSERT_EQUAL_HEX8(0x65, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x78, buf[4]); // session handle LSB

    EipHeader p;
    const uint8_t *d;
    size_t dlen;
    TEST_ASSERT_TRUE(pc_eip_parse(buf, n, &p, &d, &dlen));
    TEST_ASSERT_EQUAL_HEX16(EIP_CMD_REGISTER_SESSION, p.command);
    TEST_ASSERT_EQUAL_HEX32(0x12345678, p.session_handle);
    TEST_ASSERT_EQUAL_MEMORY(ctx, p.sender_context, 8);
    TEST_ASSERT_EQUAL_size_t(2, dlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, d, 2);
}

void test_register_session()
{
    uint8_t buf[32];
    size_t n = pc_eip_build_register_session(buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_size_t(EIP_HEADER_SIZE + 4, n);
    TEST_ASSERT_EQUAL_HEX8(0x65, buf[0]); // RegisterSession
    TEST_ASSERT_EQUAL_HEX8(0x04, buf[2]); // length 4
    // data = protocol version 1 (LE) + options flags 0.
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[24]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[25]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[26]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[27]);
}

void test_unregister_session()
{
    uint8_t buf[32];
    const uint8_t ctx[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t n = pc_eip_build_unregister_session(buf, sizeof(buf), 0x12345678u, ctx);
    TEST_ASSERT_EQUAL_size_t(EIP_HEADER_SIZE, n); // header only, no command-specific data
    TEST_ASSERT_EQUAL_HEX8(0x66, buf[0]);         // UnRegisterSession (LE)
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]); // length 0
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]);

    EipHeader h;
    const uint8_t *data;
    size_t dlen;
    TEST_ASSERT_TRUE(pc_eip_parse(buf, n, &h, &data, &dlen));
    TEST_ASSERT_EQUAL_HEX16(EIP_CMD_UNREGISTER_SESSION, h.command);
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, h.session_handle);
    TEST_ASSERT_EQUAL_size_t(0, dlen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ctx, h.sender_context, 8);

    // A null sender_context zeros the field; a too-small buffer fails closed.
    n = pc_eip_build_unregister_session(buf, sizeof(buf), 0x1u, NULL);
    TEST_ASSERT_TRUE(pc_eip_parse(buf, n, &h, &data, &dlen));
    const uint8_t zero[8] = {0};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, h.sender_context, 8);
    TEST_ASSERT_EQUAL_size_t(0, pc_eip_build_unregister_session(buf, 16, 0x1u, NULL)); // needs 24
}

void test_send_rr_data_bytes()
{
    const uint8_t cip[] = {0x4C, 0x02}; // a (stub) CIP request
    uint8_t buf[64];
    size_t n = pc_eip_build_send_rr_data(buf, sizeof(buf), 0x12345678, NULL, 10, cip, sizeof(cip));
    TEST_ASSERT_EQUAL_size_t(EIP_HEADER_SIZE + 18, n); // data = 6 + 2 + 4(null) + 4(hdr) + 2(cip) = 18
    TEST_ASSERT_EQUAL_HEX8(0x6F, buf[0]);              // SendRRData
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[2]);              // length 18 (0x0012) LSB
    // command data (CPF) starting at offset 24.
    const uint8_t expect_data[] = {
        0x00, 0x00, 0x00, 0x00, // interface handle
        0x0A, 0x00,             // timeout 10
        0x02, 0x00,             // CPF item count 2
        0x00, 0x00, 0x00, 0x00, // null address item (type 0x0000, len 0)
        0xB2, 0x00, 0x02, 0x00, // unconnected data item (type 0x00B2, len 2)
        0x4C, 0x02              // the CIP message
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect_data, buf + EIP_HEADER_SIZE, sizeof(expect_data));
}

void test_send_rr_data_round_trip()
{
    const uint8_t cip[] = {0x4C, 0x20, 0x01, 0x24, 0x01};
    uint8_t buf[64];
    size_t n = pc_eip_build_send_rr_data(buf, sizeof(buf), 0x01, NULL, 5, cip, sizeof(cip));

    EipHeader p;
    const uint8_t *d;
    size_t dlen;
    TEST_ASSERT_TRUE(pc_eip_parse(buf, n, &p, &d, &dlen));
    TEST_ASSERT_EQUAL_HEX16(EIP_CMD_SEND_RR_DATA, p.command);

    const uint8_t *out_cip;
    size_t out_len;
    TEST_ASSERT_TRUE(pc_eip_parse_send_rr_data(d, dlen, &out_cip, &out_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(cip), out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(cip, out_cip, sizeof(cip));
}

void test_list_identity()
{
    // Request: a header-only ListIdentity (command 0x0063, length 0).
    uint8_t buf[32];
    size_t n = pc_eip_build_list_identity(buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_size_t(EIP_HEADER_SIZE, n);
    TEST_ASSERT_EQUAL_HEX8(0x63, buf[0]); // ListIdentity
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]); // length 0
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]);
    TEST_ASSERT_EQUAL_size_t(0, pc_eip_build_list_identity(buf, 16, NULL)); // needs 24

    // Response command-data block: item count 1, one List Identity item (0x000C, length 0x27 = 39).
    const uint8_t resp[] = {
        0x01, 0x00,             // item count = 1
        0x0C, 0x00, 0x27, 0x00, // item type 0x000C, length 39
        0x01, 0x00,             // encapsulation protocol version 1
        0x00, 0x02, 0xAF, 0x12, 0xC0, 0xA8, 0x01, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00,                        // 16-octet socket address (skipped)
        0x1D, 0x00,                  // vendor id 0x001D
        0x0C, 0x00,                  // device type 0x000C
        0x36, 0x00,                  // product code 0x0036
        0x14, 0x0B,                  // revision 20.11
        0x30, 0x00,                  // status 0x0030
        0x78, 0x56, 0x34, 0x12,      // serial 0x12345678
        0x05,                        // product name length 5
        'P',  'L',  'C',  '-',  '1', // "PLC-1"
        0x03                         // state 3
    };
    EipIdentity id;
    TEST_ASSERT_TRUE(pc_eip_parse_list_identity(resp, sizeof(resp), &id));
    TEST_ASSERT_EQUAL_UINT16(1, id.protocol_version);
    TEST_ASSERT_EQUAL_HEX16(0x001D, id.vendor_id);
    TEST_ASSERT_EQUAL_HEX16(0x000C, id.device_type);
    TEST_ASSERT_EQUAL_HEX16(0x0036, id.product_code);
    TEST_ASSERT_EQUAL_UINT8(20, id.revision_major);
    TEST_ASSERT_EQUAL_UINT8(11, id.revision_minor);
    TEST_ASSERT_EQUAL_HEX16(0x0030, id.status);
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, id.serial_number);
    TEST_ASSERT_EQUAL_UINT8(5, id.product_name_len);
    TEST_ASSERT_EQUAL_MEMORY("PLC-1", id.product_name, 5);
    TEST_ASSERT_EQUAL_UINT8(3, id.state);

    // Guards: too short for the item count, a non-identity item, a truncated identity item, and null args.
    EipIdentity id2;
    TEST_ASSERT_FALSE(pc_eip_parse_list_identity(resp, 1, &id2)); // < 2 octets
    const uint8_t other[] = {0x01, 0x00, 0xB2, 0x00, 0x00, 0x00}; // one unconnected-data item, not 0x000C
    TEST_ASSERT_FALSE(pc_eip_parse_list_identity(other, sizeof(other), &id2));
    const uint8_t trunc[] = {0x01, 0x00, 0x0C, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}; // item len 5 < 33
    TEST_ASSERT_FALSE(pc_eip_parse_list_identity(trunc, sizeof(trunc), &id2));
    TEST_ASSERT_FALSE(pc_eip_parse_list_identity(NULL, 10, &id2));
}

void test_parse_rejects_bad()
{
    EipHeader p;
    const uint8_t *d;
    size_t dlen;
    const uint8_t short_hdr[] = {0x65, 0x00, 0x00, 0x00}; // < 24 octets
    TEST_ASSERT_FALSE(pc_eip_parse(short_hdr, sizeof(short_hdr), &p, &d, &dlen));

    // A 24-octet header that declares 8 data octets but has none.
    uint8_t hdr[EIP_HEADER_SIZE] = {0};
    hdr[0] = 0x6F;
    hdr[2] = 0x08; // length 8
    TEST_ASSERT_FALSE(pc_eip_parse(hdr, sizeof(hdr), &p, &d, &dlen));

    // A SendRRData block with no unconnected-data item.
    const uint8_t no_item[] = {0, 0, 0, 0, 0, 0, 0x00, 0x00}; // interface + timeout + item count 0
    const uint8_t *cip;
    size_t clen;
    TEST_ASSERT_FALSE(pc_eip_parse_send_rr_data(no_item, sizeof(no_item), &cip, &clen));
}

void test_build_overflow_fails_closed()
{
    const uint8_t cip[] = {0x4C, 0x02};
    uint8_t small[24]; // room for the header but not the CPF data
    TEST_ASSERT_EQUAL_size_t(0, pc_eip_build_send_rr_data(small, sizeof(small), 1, NULL, 1, cip, sizeof(cip)));
    TEST_ASSERT_EQUAL_size_t(0, pc_eip_build_register_session(small, 16, NULL));
}

// Builder null/oversize guards, the sender-context copy path in both convenience
// builders, and the SendRRData reply extractor's short/truncated/oversize rejects.
void test_build_and_parse_guards()
{
    uint8_t buf[64];
    EipHeader h;
    memset(&h, 0, sizeof(h));
    const uint8_t data[] = {0xAA, 0xBB};

    TEST_ASSERT_EQUAL_size_t(0, pc_eip_build(NULL, sizeof(buf), &h, data, 2));   // null buf
    TEST_ASSERT_EQUAL_size_t(0, pc_eip_build(buf, sizeof(buf), NULL, data, 2));  // null header
    TEST_ASSERT_EQUAL_size_t(0, pc_eip_build(buf, sizeof(buf), &h, data, 0x10000)); // data_len > 0xFFFF

    // Passing a sender context exercises the memcpy in each convenience builder.
    const uint8_t ctx[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t n = pc_eip_build_register_session(buf, sizeof(buf), ctx);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ctx, buf + 12, 8); // sender context at offset 12
    n = pc_eip_build_send_rr_data(buf, sizeof(buf), 0x01, ctx, 5, data, 2);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ctx, buf + 12, 8);

    TEST_ASSERT_EQUAL_size_t(0, pc_eip_build_send_rr_data(NULL, sizeof(buf), 1, NULL, 5, data, 2)); // null buf
    TEST_ASSERT_EQUAL_size_t(
        0, pc_eip_build_send_rr_data(buf, sizeof(buf), 1, NULL, 5, NULL, 2)); // pc_cip_len && !cip

    const uint8_t *cip;
    size_t clen;
    TEST_ASSERT_FALSE(pc_eip_parse_send_rr_data(NULL, 8, &cip, &clen)); // null data
    const uint8_t tooshort[7] = {0};
    TEST_ASSERT_FALSE(pc_eip_parse_send_rr_data(tooshort, sizeof(tooshort), &cip, &clen)); // data_len < 8
    const uint8_t trunc_item[8] = {0, 0, 0, 0, 0, 0, 0x01, 0x00};                          // count 1 but no item octets
    TEST_ASSERT_FALSE(pc_eip_parse_send_rr_data(trunc_item, sizeof(trunc_item), &cip, &clen));
    const uint8_t over_item[12] = {0, 0, 0, 0, 0, 0, 0x01, 0x00, 0xB2, 0x00, 0x10, 0x00}; // item len 16 > buffer
    TEST_ASSERT_FALSE(pc_eip_parse_send_rr_data(over_item, sizeof(over_item), &cip, &clen));
}

// Remaining branch-coverage gaps not hit by the tests above:
//  - pc_eip_build's `(data_len && !data)` true side, called directly (the convenience
//    builders only ever pass data_len==0 with a null data pointer, never data_len>0 with one).
//  - pc_eip_parse's null-buf and null-out guards, and its two output slice pointers (data,
//    data_len) both being optional (NULL).
//  - pc_eip_build_send_rr_data's pc_cip_len==0 path (cip may be null), which is also the
//    only case that takes the "skip memcpy" side of `if (pc_cip_len)`.
//  - pc_eip_build_send_rr_data rejecting pc_cip_len > 0xFFFF outright, and separately
//    rejecting a pc_cip_len that itself fits in 16 bits but whose CPF-wrapped data_len does
//    not (a generously large `cap` proves the reject is on data_len's own merits, not the cap
//    check; the function returns before writing anything, so `buf` need not really be that big).
//  - pc_eip_parse_send_rr_data's two output pointers (cip, pc_cip_len) both being optional.
void test_more_branch_coverage()
{
    EipHeader h;
    memset(&h, 0, sizeof(h));
    uint8_t buf[64];

    TEST_ASSERT_EQUAL_size_t(0, pc_eip_build(buf, sizeof(buf), &h, NULL, 5)); // data_len && !data

    EipHeader p;
    const uint8_t *d;
    size_t dlen;
    TEST_ASSERT_FALSE(pc_eip_parse(NULL, sizeof(buf), &p, &d, &dlen));  // null buf
    TEST_ASSERT_FALSE(pc_eip_parse(buf, sizeof(buf), NULL, &d, &dlen)); // null out

    size_t n = pc_eip_build_register_session(buf, sizeof(buf), NULL);
    TEST_ASSERT_TRUE(pc_eip_parse(buf, n, &p, NULL, NULL)); // both output slices optional

    // pc_cip_len == 0 (cip may be null); also exercises the memcpy-skip side of `if (pc_cip_len)`.
    size_t rr = pc_eip_build_send_rr_data(buf, sizeof(buf), 1, NULL, 5, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(EIP_HEADER_SIZE + 16, rr);

    uint8_t dummy_cip[1] = {0};
    TEST_ASSERT_EQUAL_size_t(
        0, pc_eip_build_send_rr_data(buf, sizeof(buf), 1, NULL, 5, dummy_cip, 0x10000)); // pc_cip_len > 0xFFFF
    TEST_ASSERT_EQUAL_size_t(
        0, pc_eip_build_send_rr_data(buf, 100000, 1, NULL, 5, dummy_cip, 0xFFF0)); // data_len > 0xFFFF

    const uint8_t item[] = {0, 0, 0, 0, 0, 0, 0x01, 0x00, 0xB2, 0x00, 0x02, 0x00, 0xAB, 0xCD};
    TEST_ASSERT_TRUE(pc_eip_parse_send_rr_data(item, sizeof(item), NULL, NULL)); // both outputs optional
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_header_round_trip);
    RUN_TEST(test_register_session);
    RUN_TEST(test_unregister_session);
    RUN_TEST(test_send_rr_data_bytes);
    RUN_TEST(test_send_rr_data_round_trip);
    RUN_TEST(test_list_identity);
    RUN_TEST(test_parse_rejects_bad);
    RUN_TEST(test_build_overflow_fails_closed);
    RUN_TEST(test_build_and_parse_guards);
    RUN_TEST(test_more_branch_coverage);
    return UNITY_END();
}
