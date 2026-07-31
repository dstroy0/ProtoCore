// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the flow-export codec (services/net/flow_export): NetFlow v5 fixed records,
// and the NetFlow v9 / IPFIX template-then-data cursor. Pure host tests with exact
// wire-byte assertions checked against RFC 7011 / RFC 3954 / the v5 layout.

#include "services/net/flow_export/flow_export.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_v5_header_bytes()
{
    FlowV5Header h = {};
    h.count = 1;
    h.sys_uptime = 1000; // 0x000003E8
    h.unix_secs = 0x5F5E1100;
    h.unix_nsecs = 0;
    h.flow_sequence = 1;
    h.engine_type = 0;
    h.engine_id = 0;
    h.sampling_interval = 0;
    uint8_t buf[FLOW_V5_HEADER_SIZE];
    size_t n = flow_v5_write_header(buf, sizeof(buf), &h);
    TEST_ASSERT_EQUAL_size_t(FLOW_V5_HEADER_SIZE, n);
    const uint8_t expect[] = {0x00, 0x05, 0x00, 0x01, 0x00, 0x00, 0x03, 0xE8, 0x5F, 0x5E, 0x11, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

void test_v5_record_bytes()
{
    FlowV5Record r = {};
    r.src_addr = 0x0A000001;
    r.dst_addr = 0x0A000002;
    r.next_hop = 0x0A0000FE;
    r.input = 1;
    r.output = 2;
    r.d_pkts = 10;
    r.d_octets = 1500; // 0x5DC
    r.first = 100;     // 0x64
    r.last = 200;      // 0xC8
    r.src_port = 12345;
    r.dst_port = 80;
    r.tcp_flags = 0x18;
    r.prot = 6;
    r.tos = 0;
    r.src_as = 0;
    r.dst_as = 0;
    r.src_mask = 24;
    r.dst_mask = 24;
    uint8_t buf[FLOW_V5_RECORD_SIZE];
    size_t n = flow_v5_write_record(buf, sizeof(buf), &r);
    TEST_ASSERT_EQUAL_size_t(FLOW_V5_RECORD_SIZE, n);
    const uint8_t expect[] = {
        0x0A, 0x00, 0x00, 0x01, // srcaddr
        0x0A, 0x00, 0x00, 0x02, // dstaddr
        0x0A, 0x00, 0x00, 0xFE, // nexthop
        0x00, 0x01, 0x00, 0x02, // input, output
        0x00, 0x00, 0x00, 0x0A, // dPkts
        0x00, 0x00, 0x05, 0xDC, // dOctets
        0x00, 0x00, 0x00, 0x64, // first
        0x00, 0x00, 0x00, 0xC8, // last
        0x30, 0x39, 0x00, 0x50, // srcport, dstport
        0x00, 0x18, 0x06, 0x00, // pad1, tcp_flags, prot, tos
        0x00, 0x00, 0x00, 0x00, // src_as, dst_as
        0x18, 0x18, 0x00, 0x00  // src_mask, dst_mask, pad2
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

void test_v5_overflow_fails_closed()
{
    FlowV5Record r = {};
    uint8_t small[10];
    TEST_ASSERT_EQUAL_size_t(0, flow_v5_write_record(small, sizeof(small), &r));
}

// IPFIX message: header(16) + template set(16) + data set(12) = 44 octets.
void test_ipfix_message_bytes()
{
    uint8_t buf[128];
    FlowWriter w;
    TEST_ASSERT_TRUE(flow_ipfix_begin(&w, buf, sizeof(buf), 0x11223344, 1, 0x2A));
    FlowField fields[] = {{8, 4}, {12, 4}}; // sourceIPv4Address, destinationIPv4Address
    TEST_ASSERT_TRUE(flow_export_template(&w, 256, fields, 2));
    TEST_ASSERT_TRUE(flow_export_data_begin(&w, 256));
    const uint8_t rec[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    TEST_ASSERT_TRUE(flow_export_data_record(&w, rec, sizeof(rec)));
    size_t n = flow_export_finish(&w); // auto-closes the data set

    const uint8_t expect[] = {
        // message header
        0x00, 0x0A, 0x00, 0x2C, 0x11, 0x22, 0x33, 0x44, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x2A,
        // template set (id 2, len 16): template 256, 2 fields
        0x00, 0x02, 0x00, 0x10, 0x01, 0x00, 0x00, 0x02, 0x00, 0x08, 0x00, 0x04, 0x00, 0x0C, 0x00, 0x04,
        // data set (id 256, len 12): one record
        0x01, 0x00, 0x00, 0x0C, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    TEST_ASSERT_EQUAL_size_t(sizeof(expect), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// NetFlow v9: count counts records (template + data), and data FlowSets pad to 4 octets.
void test_v9_count_and_padding()
{
    uint8_t buf[128];
    FlowWriter w;
    TEST_ASSERT_TRUE(flow_v9_begin(&w, buf, sizeof(buf), 1000, 0x5F000000, 7, 1));
    FlowField fields[] = {{1, 4}, {10, 2}}; // a 6-octet record layout
    TEST_ASSERT_TRUE(flow_export_template(&w, 256, fields, 2));
    TEST_ASSERT_TRUE(flow_export_data_begin(&w, 256));
    const uint8_t rec[] = {0x00, 0x00, 0x00, 0x64, 0x00, 0x07}; // 6 octets (not 4-aligned)
    TEST_ASSERT_TRUE(flow_export_data_record(&w, rec, sizeof(rec)));
    size_t n = flow_export_finish(&w);

    // header(20) + template flowset(16) + data flowset(4 hdr + 6 rec + 2 pad = 12) = 48
    TEST_ASSERT_EQUAL_size_t(48, n);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x09, buf[1]); // version 9
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[3]); // count = 2 records
    // template flowset starts at 20: FlowSet ID 0, length 16.
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[20]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[21]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[22]);
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[23]);
    // data flowset starts at 36: FlowSet ID 256, length 12 (incl. 2 pad octets).
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[36]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[37]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[38]);
    TEST_ASSERT_EQUAL_HEX8(0x0C, buf[39]);
    const uint8_t rec_expect[] = {0x00, 0x00, 0x00, 0x64, 0x00, 0x07};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(rec_expect, buf + 40, 6);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[46]); // padding
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[47]);
}

void test_finish_overflow_fails_closed()
{
    uint8_t tiny[8]; // too small even for the 16-octet IPFIX header
    FlowWriter w;
    flow_ipfix_begin(&w, tiny, sizeof(tiny), 0, 0, 0);
    FlowField fields[] = {{8, 4}};
    flow_export_template(&w, 256, fields, 1);
    TEST_ASSERT_EQUAL_size_t(0, flow_export_finish(&w));
}

void test_v5_write_overflow()
{
    uint8_t buf[4]; // smaller than a v5 header or record
    FlowV5Header h = {};
    TEST_ASSERT_EQUAL_size_t(0, flow_v5_write_header(buf, sizeof(buf), &h));
    FlowV5Record r = {};
    TEST_ASSERT_EQUAL_size_t(0, flow_v5_write_record(buf, sizeof(buf), &r));
}

void test_flow_guards_and_overflows()
{
    uint8_t buf[128];
    FlowWriter w;
    FlowField f = {8, 4};

    // begin null-arg guards + finish(null).
    TEST_ASSERT_FALSE(flow_ipfix_begin(nullptr, buf, sizeof(buf), 0, 0, 0));
    TEST_ASSERT_FALSE(flow_ipfix_begin(&w, nullptr, sizeof(buf), 0, 0, 0));
    TEST_ASSERT_FALSE(flow_v9_begin(nullptr, buf, sizeof(buf), 0, 0, 0, 0));
    TEST_ASSERT_FALSE(flow_v9_begin(&w, nullptr, sizeof(buf), 0, 0, 0, 0));
    TEST_ASSERT_EQUAL_size_t(0, flow_export_finish(nullptr));

    // w_u16 overflow: a 3-octet buffer can't fit even version+length.
    uint8_t b3[3];
    TEST_ASSERT_FALSE(flow_ipfix_begin(&w, b3, sizeof(b3), 0, 0, 0));

    // misuse guards on a valid writer.
    TEST_ASSERT_TRUE(flow_ipfix_begin(&w, buf, sizeof(buf), 1, 2, 3));
    TEST_ASSERT_FALSE(flow_export_template(nullptr, 256, &f, 1));
    TEST_ASSERT_FALSE(flow_export_template(&w, 256, nullptr, 1));
    TEST_ASSERT_FALSE(flow_export_template(&w, 256, &f, 0));
    TEST_ASSERT_FALSE(flow_export_data_begin(nullptr, 256));
    TEST_ASSERT_FALSE(flow_export_data_begin(&w, 255)); // id < 256
    uint8_t rec[4] = {1, 2, 3, 4};
    TEST_ASSERT_FALSE(flow_export_data_record(nullptr, rec, 4));
    TEST_ASSERT_FALSE(flow_export_data_record(&w, rec, 4)); // no set open
    TEST_ASSERT_FALSE(flow_export_data_record(&w, nullptr, 4));
    TEST_ASSERT_FALSE(flow_export_data_end(nullptr));
    TEST_ASSERT_FALSE(flow_export_data_end(&w)); // no set open

    // Auto-close: opening a data/template set while another is open closes the first.
    TEST_ASSERT_TRUE(flow_v9_begin(&w, buf, sizeof(buf), 0, 0, 0, 0));
    TEST_ASSERT_TRUE(flow_export_template(&w, 256, &f, 1));
    TEST_ASSERT_TRUE(flow_export_data_begin(&w, 256));
    TEST_ASSERT_TRUE(flow_export_data_record(&w, rec, 4));
    TEST_ASSERT_TRUE(flow_export_data_begin(&w, 257)); // set open -> data_end
    TEST_ASSERT_TRUE(flow_export_data_record(&w, rec, 4));
    TEST_ASSERT_TRUE(flow_export_template(&w, 258, &f, 1)); // set open -> data_end
    TEST_ASSERT_TRUE(flow_export_finish(&w) > 0);

    // w_bytes overflow + sticky-error early return: header(20)+set(4)=24, cap 26, so a
    // 6-octet record overruns; the next record then hits the sticky-error early return, and
    // finish's auto data_end hits w_zero's sticky-error early return.
    uint8_t b26[26];
    TEST_ASSERT_TRUE(flow_v9_begin(&w, b26, sizeof(b26), 0, 0, 0, 0));
    TEST_ASSERT_TRUE(flow_export_data_begin(&w, 256));
    uint8_t rec6[6] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_FALSE(flow_export_data_record(&w, rec6, 6)); // w_bytes overflow
    TEST_ASSERT_FALSE(flow_export_data_record(&w, rec6, 6)); // w_bytes sticky-error
    TEST_ASSERT_EQUAL_size_t(0, flow_export_finish(&w));     // w_zero sticky-error

    // w_zero overflow: a v9 set with a 1-octet record needs 3 pad octets that don't fit.
    uint8_t b25[25];
    TEST_ASSERT_TRUE(flow_v9_begin(&w, b25, sizeof(b25), 0, 0, 0, 0)); // pos 20
    TEST_ASSERT_TRUE(flow_export_data_begin(&w, 256));                 // pos 24
    uint8_t r1[1] = {7};
    TEST_ASSERT_TRUE(flow_export_data_record(&w, r1, 1)); // pos 25 == cap
    TEST_ASSERT_EQUAL_size_t(0, flow_export_finish(&w));  // pad 3 overflows

    // IPFIX 16-bit length overflow: a message past 0xFFFF octets fails closed.
    TEST_ASSERT_TRUE(flow_ipfix_begin(&w, buf, sizeof(buf), 0, 0, 0));
    w.pos = 0x10000; // force past the IPFIX length field's range
    TEST_ASSERT_EQUAL_size_t(0, flow_export_finish(&w));
}

// v5 writers' null-arg guards: buf==nullptr and header/record==nullptr, each in isolation
// (test_v5_write_overflow only exercises the cap-too-small branch of the same guard).
void test_v5_write_null_guards()
{
    FlowV5Header h = {};
    uint8_t hbuf[FLOW_V5_HEADER_SIZE];
    TEST_ASSERT_EQUAL_size_t(0, flow_v5_write_header(nullptr, sizeof(hbuf), &h));
    TEST_ASSERT_EQUAL_size_t(0, flow_v5_write_header(hbuf, sizeof(hbuf), nullptr));

    FlowV5Record r = {};
    uint8_t rbuf[FLOW_V5_RECORD_SIZE];
    TEST_ASSERT_EQUAL_size_t(0, flow_v5_write_record(nullptr, sizeof(rbuf), &r));
    TEST_ASSERT_EQUAL_size_t(0, flow_v5_write_record(rbuf, sizeof(rbuf), nullptr));
}

// flow_export_data_record's !record and len==0 guard branches, taken with a set actually
// open (test_flow_guards_and_overflows only reaches !record / len==0 with no set open, so
// those two conditions short-circuit away before being evaluated).
void test_data_record_null_and_zero_len_with_set_open()
{
    uint8_t buf[128];
    FlowWriter w;
    TEST_ASSERT_TRUE(flow_v9_begin(&w, buf, sizeof(buf), 0, 0, 0, 0));
    TEST_ASSERT_TRUE(flow_export_data_begin(&w, 256));
    uint8_t rec[4] = {1, 2, 3, 4};
    TEST_ASSERT_FALSE(flow_export_data_record(&w, nullptr, 4)); // !record, set open
    TEST_ASSERT_FALSE(flow_export_data_record(&w, rec, 0));     // len == 0, set open + valid record
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_v5_header_bytes);
    RUN_TEST(test_v5_record_bytes);
    RUN_TEST(test_v5_overflow_fails_closed);
    RUN_TEST(test_ipfix_message_bytes);
    RUN_TEST(test_v9_count_and_padding);
    RUN_TEST(test_finish_overflow_fails_closed);
    RUN_TEST(test_v5_write_overflow);
    RUN_TEST(test_flow_guards_and_overflows);
    RUN_TEST(test_v5_write_null_guards);
    RUN_TEST(test_data_record_null_and_zero_len_with_set_open);
    return UNITY_END();
}
