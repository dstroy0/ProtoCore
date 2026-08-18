// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the flow-record Exporting Process (services/net/flow_export/flow_export.h).
//
// RFC 3954 sec 11 works a whole NetFlow v9 Export Packet through by hand, and this suite reproduces
// two of its figures octet for octet: sec 11.2's Template FlowSet (FlowSet ID 0, Length 28,
// Template ID 256, Field Count 5, the field specifiers 8/4, 12/4, 15/4, 2/4 and 1/4) and sec 11.3's
// Data FlowSet (FlowSet ID 256, Length 64, three 20-octet records, with the RFC's own note that
// "padding was not necessary in this example"). The field type numbers come from RFC 3954 sec 8:
// IN_BYTES 1, IN_PKTS 2, L4_SRC_PORT 7, IPV4_SRC_ADDR 8, L4_DST_PORT 11, IPV4_DST_ADDR 12. The
// headers are RFC 3954 sec 5.1 and RFC 7011 sec 3.1, and the Set ID rules RFC 7011 sec 3.3.2.
//
// NetFlow Version 5 has no IETF specification (RFC 3954 covers Version 9 only), so its 24-octet
// header and 48-octet record are asserted against the field layout flow_export.h states, plus the
// property that both are fixed width with their pad octets zero.
//
// test_rfc3954_template_flowset_example is the load-bearing case: the Template FlowSet is what
// teaches a collector how to read every Data Record that follows, so one wrong octet there
// mis-decodes the entire export rather than one field of it.

#include "services/net/flow_export/flow_export.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// RFC 3954 sec 11.2: the five Field Specifiers the worked example lists, in its order.
static const FlowFieldSpecifier RFC_FIELDS[5] = {
    {8, 4},  // IPV4_SRC_ADDR
    {12, 4}, // IPV4_DST_ADDR
    {15, 4}, // IP_NEXT_HOP
    {2, 4},  // IN_PKTS
    {1, 4},  // IN_BYTES
};

// RFC 3954 sec 11.3: its three Flow Records, each 5 * 4 octets in Template order.
// 198.168.1.12 -> 10.5.12.254 via 192.168.1.1, 5009 packets (0x1391), 5344385 octets (0x518C81)
static const uint8_t REC1[20] = {0xC6, 0xA8, 0x01, 0x0C, 0x0A, 0x05, 0x0C, 0xFE, 0xC0, 0xA8,
                                 0x01, 0x01, 0x00, 0x00, 0x13, 0x91, 0x00, 0x51, 0x8C, 0x81};
// 192.168.1.27 -> 10.5.12.23 via 192.168.1.1, 748 packets (0x2EC), 388934 octets (0x5EF46)
static const uint8_t REC2[20] = {0xC0, 0xA8, 0x01, 0x1B, 0x0A, 0x05, 0x0C, 0x17, 0xC0, 0xA8,
                                 0x01, 0x01, 0x00, 0x00, 0x02, 0xEC, 0x00, 0x05, 0xEF, 0x46};
// 192.168.1.56 -> 10.5.12.65 via 192.168.1.1, 5 packets, 6534 octets (0x1986)
static const uint8_t REC3[20] = {0xC0, 0xA8, 0x01, 0x38, 0x0A, 0x05, 0x0C, 0x41, 0xC0, 0xA8,
                                 0x01, 0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x19, 0x86};

static uint8_t g_buf[512];

static void begin_v9(size_t cap)
{
    FlowExport.out.buf = g_buf;
    FlowExport.out.cap = cap;
    FlowExport.message.sys_uptime = 0x11223344u;
    FlowExport.message.unix_secs = 0x55667788u;
    FlowExport.message.sequence_number = 0x99AABBCCu;
    FlowExport.message.observation_domain_id = 0x0000002Au;
    FlowExport.v9_begin(protocore_flow_export_span());
}

static void begin_ipfix(size_t cap)
{
    FlowExport.out.buf = g_buf;
    FlowExport.out.cap = cap;
    FlowExport.message.export_time = 0x55667788u;
    FlowExport.message.sequence_number = 0x99AABBCCu;
    FlowExport.message.observation_domain_id = 0x0000002Au;
    FlowExport.ipfix_begin(protocore_flow_export_span());
}

static void emit_template(uint16_t id, const FlowFieldSpecifier *fields, size_t count)
{
    FlowExport.template_id = id;
    FlowExport.tmpl.fields = fields;
    FlowExport.tmpl.field_count = count;
    FlowExport.template_set(protocore_flow_export_span());
}

static void emit_record(const uint8_t *rec, size_t len)
{
    FlowExport.data.record = rec;
    FlowExport.data.len = len;
    FlowExport.data_record(protocore_flow_export_span());
}

// RFC 3954 sec 5.1: Version 9, Count, sysUpTime, UNIX Secs, Sequence Number, Source ID - 20 octets,
// every field network byte order. Count is patched at finish.
void test_v9_packet_header(void)
{
    memset(g_buf, 0xAA, sizeof(g_buf));
    begin_v9(sizeof(g_buf));
    TEST_ASSERT_TRUE(FlowExport.ok);
    emit_template(256, RFC_FIELDS, 5);
    TEST_ASSERT_TRUE(FlowExport.ok);
    FlowExport.message_finish(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);

    static const uint8_t WANT[20] = {0x00, 0x09,              // Version 9
                                     0x00, 0x01,              // Count = the one Template Record
                                     0x11, 0x22, 0x33, 0x44,  // sysUpTime
                                     0x55, 0x66, 0x77, 0x88,  // UNIX Secs
                                     0x99, 0xAA, 0xBB, 0xCC,  // Sequence Number
                                     0x00, 0x00, 0x00, 0x2A}; // Source ID
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_buf, sizeof(WANT));
    TEST_ASSERT_EQUAL_size_t(20u + 28u, FlowExport.n);
}

// RFC 3954 sec 11.2, octet for octet.
void test_rfc3954_template_flowset_example(void)
{
    begin_v9(sizeof(g_buf));
    emit_template(256, RFC_FIELDS, 5);
    TEST_ASSERT_TRUE(FlowExport.ok);
    FlowExport.message_finish(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);

    static const uint8_t WANT[28] = {0x00, 0x00,              // FlowSet ID = 0
                                     0x00, 0x1C,              // Length = 28 bytes
                                     0x01, 0x00,              // Template ID 256
                                     0x00, 0x05,              // Field Count = 5
                                     0x00, 0x08, 0x00, 0x04,  // IP_SRC_ADDR = 8, length 4
                                     0x00, 0x0C, 0x00, 0x04,  // IP_DST_ADDR = 12, length 4
                                     0x00, 0x0F, 0x00, 0x04,  // IP_NEXT_HOP = 15, length 4
                                     0x00, 0x02, 0x00, 0x04,  // IN_PKTS = 2, length 4
                                     0x00, 0x01, 0x00, 0x04}; // IN_BYTES = 1, length 4
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_buf + 20, sizeof(WANT));
}

// RFC 3954 sec 11.3, octet for octet: FlowSet ID 256, Length 64, three records, no padding needed.
void test_rfc3954_data_flowset_example(void)
{
    begin_v9(sizeof(g_buf));
    emit_template(256, RFC_FIELDS, 5);
    FlowExport.template_id = 256;
    FlowExport.data_set_begin(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);
    emit_record(REC1, sizeof(REC1));
    emit_record(REC2, sizeof(REC2));
    emit_record(REC3, sizeof(REC3));
    TEST_ASSERT_TRUE(FlowExport.ok);
    FlowExport.data_set_end(protocore_flow_export_span());
    FlowExport.message_finish(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);

    const uint8_t *set = g_buf + 20 + 28;
    static const uint8_t HDR[4] = {0x01, 0x00, 0x00, 0x40}; // FlowSet ID 256, Length 64
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HDR, set, sizeof(HDR));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REC1, set + 4, sizeof(REC1));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REC2, set + 24, sizeof(REC2));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REC3, set + 44, sizeof(REC3));

    // RFC 3954 sec 5.1 Count = the Template Record plus the three Data Records
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x04, g_buf[3]);
    TEST_ASSERT_EQUAL_size_t(20u + 28u + 64u, FlowExport.n);
}

// RFC 7011 sec 3.1: Version 0x000a, then a Length that is the whole message, patched at finish.
// RFC 7011 sec 3.3.2: a Template Set's Set ID is 2, not the v9 FlowSet ID 0.
void test_ipfix_message_header_and_template_set_id(void)
{
    begin_ipfix(sizeof(g_buf));
    TEST_ASSERT_TRUE(FlowExport.ok);
    emit_template(256, RFC_FIELDS, 5);
    FlowExport.message_finish(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);

    // 16-octet Message Header, then the 28-octet Template Set
    TEST_ASSERT_EQUAL_size_t(16u + 28u, FlowExport.n);
    static const uint8_t WANT[16] = {0x00, 0x0A,              // Version 0x000a
                                     0x00, 0x2C,              // Length = 44 = 16 + 28
                                     0x55, 0x66, 0x77, 0x88,  // Export Time
                                     0x99, 0xAA, 0xBB, 0xCC,  // Sequence Number
                                     0x00, 0x00, 0x00, 0x2A}; // Observation Domain ID
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_buf, sizeof(WANT));
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[16]); // Set ID 2
    TEST_ASSERT_EQUAL_HEX8(0x02, g_buf[17]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[18]); // Set Length 28, unchanged from the v9 shape
    TEST_ASSERT_EQUAL_HEX8(0x1C, g_buf[19]);
}

// RFC 7011 sec 3.3.2: "Values 256 and above are used for Data Sets", and RFC 3954 sec 5.2 reserves
// FlowSet IDs 0 through 255, so a Data Set below 256 is refused rather than colliding with a
// Template FlowSet.
void test_data_set_id_must_be_256_or_above(void)
{
    begin_v9(sizeof(g_buf));
    emit_template(256, RFC_FIELDS, 5);

    FlowExport.template_id = 255;
    FlowExport.data_set_begin(protocore_flow_export_span());
    TEST_ASSERT_FALSE(FlowExport.ok);
    FlowExport.template_id = 0;
    FlowExport.data_set_begin(protocore_flow_export_span());
    TEST_ASSERT_FALSE(FlowExport.ok);

    FlowExport.template_id = 256;
    FlowExport.data_set_begin(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);
    FlowExport.template_id = 65535;
    FlowExport.data_set_begin(protocore_flow_export_span()); // closes the previous Set and opens the next
    TEST_ASSERT_TRUE(FlowExport.ok);
}

// RFC 3954 sec 5.3: a v9 Data FlowSet is padded so the next FlowSet starts on a 4-octet boundary,
// and the Length covers the padding. A 3-octet record makes a 7-octet Set, padded to 8.
void test_v9_data_set_is_padded_to_a_four_octet_boundary(void)
{
    static const uint8_t REC[3] = {0xDE, 0xAD, 0xBE};
    memset(g_buf, 0xAA, sizeof(g_buf));
    begin_v9(sizeof(g_buf));
    FlowExport.template_id = 256;
    FlowExport.data_set_begin(protocore_flow_export_span());
    emit_record(REC, sizeof(REC));
    FlowExport.data_set_end(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);
    FlowExport.message_finish(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);

    TEST_ASSERT_EQUAL_size_t(20u + 8u, FlowExport.n);
    static const uint8_t WANT[8] = {0x01, 0x00,       // FlowSet ID 256
                                    0x00, 0x08,       // Length 8: header + 3 + 1 pad
                                    0xDE, 0xAD, 0xBE, // the record
                                    0x00};            // RFC 7011 sec 3.3.1: padding is zero
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_buf + 20, sizeof(WANT));

    // an already-aligned Set gets no padding
    begin_v9(sizeof(g_buf));
    FlowExport.template_id = 256;
    FlowExport.data_set_begin(protocore_flow_export_span());
    emit_record(REC1, sizeof(REC1)); // 4 + 20 = 24, already a multiple of 4
    FlowExport.data_set_end(protocore_flow_export_span());
    FlowExport.message_finish(protocore_flow_export_span());
    TEST_ASSERT_EQUAL_size_t(20u + 24u, FlowExport.n);
}

// The 4-octet alignment is a v9 rule; an IPFIX Data Set carries the same record with no padding.
void test_ipfix_data_set_is_not_padded(void)
{
    static const uint8_t REC[3] = {0xDE, 0xAD, 0xBE};
    begin_ipfix(sizeof(g_buf));
    FlowExport.template_id = 256;
    FlowExport.data_set_begin(protocore_flow_export_span());
    emit_record(REC, sizeof(REC));
    FlowExport.data_set_end(protocore_flow_export_span());
    FlowExport.message_finish(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);

    TEST_ASSERT_EQUAL_size_t(16u + 7u, FlowExport.n);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[18]); // Set Length 7
    TEST_ASSERT_EQUAL_HEX8(0x07, g_buf[19]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[2]); // IPFIX Length 23 = 16 + 7
    TEST_ASSERT_EQUAL_HEX8(0x17, g_buf[3]);
}

// An open Set is closed by whatever comes next, so a caller that forgets never emits a Set whose
// Length field is still zero.
void test_an_open_set_is_closed_by_what_follows(void)
{
    begin_v9(sizeof(g_buf));
    FlowExport.template_id = 256;
    FlowExport.data_set_begin(protocore_flow_export_span());
    emit_record(REC1, sizeof(REC1));
    // no data_set_end: the template that follows must close it
    emit_template(257, RFC_FIELDS, 5);
    TEST_ASSERT_TRUE(FlowExport.ok);
    FlowExport.message_finish(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);

    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[22]); // the Data Set's Length was patched to 24
    TEST_ASSERT_EQUAL_HEX8(0x18, g_buf[23]);
    TEST_ASSERT_EQUAL_size_t(20u + 24u + 28u, FlowExport.n);

    // and message_finish closes one that is still open at the end
    begin_v9(sizeof(g_buf));
    FlowExport.template_id = 256;
    FlowExport.data_set_begin(protocore_flow_export_span());
    emit_record(REC1, sizeof(REC1));
    FlowExport.message_finish(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);
    TEST_ASSERT_EQUAL_HEX8(0x18, g_buf[23]);
    TEST_ASSERT_EQUAL_size_t(20u + 24u, FlowExport.n);
}

// A record with no Set open, an empty template, and a null record are all refused.
void test_calls_out_of_order_are_refused(void)
{
    begin_v9(sizeof(g_buf));
    emit_record(REC1, sizeof(REC1)); // no Data Set open
    TEST_ASSERT_FALSE(FlowExport.ok);

    emit_template(256, NULL, 5);
    TEST_ASSERT_FALSE(FlowExport.ok);
    emit_template(256, RFC_FIELDS, 0);
    TEST_ASSERT_FALSE(FlowExport.ok);

    FlowExport.template_id = 256;
    FlowExport.data_set_begin(protocore_flow_export_span());
    emit_record(NULL, 20);
    TEST_ASSERT_FALSE(FlowExport.ok);
    emit_record(REC1, 0);
    TEST_ASSERT_FALSE(FlowExport.ok);

    // closing a Set that is not open is refused too
    FlowExport.data_set_end(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);
    FlowExport.data_set_end(protocore_flow_export_span());
    TEST_ASSERT_FALSE(FlowExport.ok);

    // and a begin with nowhere to write never starts a message
    FlowExport.out.buf = NULL;
    FlowExport.out.cap = 64;
    FlowExport.v9_begin(protocore_flow_export_span());
    TEST_ASSERT_FALSE(FlowExport.ok);
    FlowExport.ipfix_begin(protocore_flow_export_span());
    TEST_ASSERT_FALSE(FlowExport.ok);
}

// A span too small for the message fails closed: the overflow is sticky, so nothing downstream
// reports a length for octets that were never written.
void test_overflow_fails_closed(void)
{
    begin_v9(20); // exactly the header, no room for a Set
    TEST_ASSERT_TRUE(FlowExport.ok);
    emit_template(256, RFC_FIELDS, 5);
    TEST_ASSERT_FALSE(FlowExport.ok);
    FlowExport.message_finish(protocore_flow_export_span());
    TEST_ASSERT_FALSE(FlowExport.ok);
    TEST_ASSERT_EQUAL_size_t(0u, FlowExport.n);

    // a header that does not fit either
    begin_v9(19);
    TEST_ASSERT_FALSE(FlowExport.ok);
    FlowExport.message_finish(protocore_flow_export_span());
    TEST_ASSERT_FALSE(FlowExport.ok);
    TEST_ASSERT_EQUAL_size_t(0u, FlowExport.n);
}

// The vendor Version 5 packet header: 24 fixed octets, Version 5 written by the builder itself.
void test_v5_header_is_twenty_four_octets(void)
{
    static const FlowV5Header H = {.count = 2,
                                   .sys_uptime = 0x11223344u,
                                   .unix_secs = 0x55667788u,
                                   .unix_nsecs = 0x99AABBCCu,
                                   .flow_sequence = 0xDDEEFF00u,
                                   .engine_type = 0x01,
                                   .engine_id = 0x02,
                                   .sampling_interval = 0x0304};
    memset(g_buf, 0xAA, sizeof(g_buf));
    FlowExport.out.buf = g_buf;
    FlowExport.out.cap = sizeof(g_buf);
    FlowExport.v5.header = &H;
    FlowExport.v5_header(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);
    TEST_ASSERT_EQUAL_size_t((size_t)FLOW_V5_HEADER_SIZE, FlowExport.n);

    static const uint8_t WANT[FLOW_V5_HEADER_SIZE] = {0x00, 0x05,             // Version 5
                                                      0x00, 0x02,             // count
                                                      0x11, 0x22, 0x33, 0x44, // sys_uptime
                                                      0x55, 0x66, 0x77, 0x88, // unix_secs
                                                      0x99, 0xAA, 0xBB, 0xCC, // unix_nsecs
                                                      0xDD, 0xEE, 0xFF, 0x00, // flow_sequence
                                                      0x01,                   // engine_type
                                                      0x02,                   // engine_id
                                                      0x03, 0x04};            // sampling_interval
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_buf, sizeof(WANT));
    TEST_ASSERT_EQUAL_HEX8(0xAA, g_buf[FLOW_V5_HEADER_SIZE]); // nothing past the header
}

// The vendor Version 5 flow record: 48 fixed octets, both pad spans zero.
void test_v5_record_is_forty_eight_octets(void)
{
    static const FlowV5Record R = {.src_addr = 0xC0A8010Cu,
                                   .dst_addr = 0x0A050CFEu,
                                   .next_hop = 0xC0A80101u,
                                   .input = 1,
                                   .output = 2,
                                   .d_pkts = 5009,
                                   .d_octets = 5344385,
                                   .first = 100,
                                   .last = 200,
                                   .src_port = 80,
                                   .dst_port = 49152,
                                   .tcp_flags = 0x1B,
                                   .prot = 6,
                                   .tos = 0,
                                   .src_as = 1,
                                   .dst_as = 2,
                                   .src_mask = 24,
                                   .dst_mask = 24};
    memset(g_buf, 0xAA, sizeof(g_buf));
    FlowExport.out.buf = g_buf;
    FlowExport.out.cap = sizeof(g_buf);
    FlowExport.v5.record = &R;
    FlowExport.v5_record(protocore_flow_export_span());
    TEST_ASSERT_TRUE(FlowExport.ok);
    TEST_ASSERT_EQUAL_size_t((size_t)FLOW_V5_RECORD_SIZE, FlowExport.n);

    static const uint8_t WANT[FLOW_V5_RECORD_SIZE] = {0xC0, 0xA8, 0x01, 0x0C, // src_addr 192.168.1.12
                                                      0x0A, 0x05, 0x0C, 0xFE, // dst_addr 10.5.12.254
                                                      0xC0, 0xA8, 0x01, 0x01, // next_hop 192.168.1.1
                                                      0x00, 0x01,             // input
                                                      0x00, 0x02,             // output
                                                      0x00, 0x00, 0x13, 0x91, // d_pkts 5009
                                                      0x00, 0x51, 0x8C, 0x81, // d_octets 5344385
                                                      0x00, 0x00, 0x00, 0x64, // first 100
                                                      0x00, 0x00, 0x00, 0xC8, // last 200
                                                      0x00, 0x50,             // src_port 80
                                                      0xC0, 0x00,             // dst_port 49152
                                                      0x00,                   // pad1
                                                      0x1B,                   // tcp_flags
                                                      0x06,                   // prot TCP
                                                      0x00,                   // tos
                                                      0x00, 0x01,             // src_as
                                                      0x00, 0x02,             // dst_as
                                                      0x18,                   // src_mask 24
                                                      0x18,                   // dst_mask 24
                                                      0x00, 0x00};            // pad2
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_buf, sizeof(WANT));
    TEST_ASSERT_EQUAL_HEX8(0xAA, g_buf[FLOW_V5_RECORD_SIZE]);
}

// A v5 write with nowhere to put its fixed width writes nothing and reports nothing.
void test_v5_refuses_a_short_span(void)
{
    static const FlowV5Header H = {0};
    static const FlowV5Record R = {0};
    memset(g_buf, 0xAA, sizeof(g_buf));

    FlowExport.out.buf = g_buf;
    FlowExport.out.cap = FLOW_V5_HEADER_SIZE - 1;
    FlowExport.v5.header = &H;
    FlowExport.v5_header(protocore_flow_export_span());
    TEST_ASSERT_FALSE(FlowExport.ok);
    TEST_ASSERT_EQUAL_size_t(0u, FlowExport.n);

    FlowExport.out.cap = FLOW_V5_RECORD_SIZE - 1;
    FlowExport.v5.record = &R;
    FlowExport.v5_record(protocore_flow_export_span());
    TEST_ASSERT_FALSE(FlowExport.ok);
    TEST_ASSERT_EQUAL_size_t(0u, FlowExport.n);

    FlowExport.out.buf = NULL;
    FlowExport.out.cap = sizeof(g_buf);
    FlowExport.v5_header(protocore_flow_export_span());
    TEST_ASSERT_FALSE(FlowExport.ok);
    FlowExport.out.buf = g_buf;
    FlowExport.v5.header = NULL;
    FlowExport.v5_header(protocore_flow_export_span());
    TEST_ASSERT_FALSE(FlowExport.ok);
    FlowExport.v5.record = NULL;
    FlowExport.v5_record(protocore_flow_export_span());
    TEST_ASSERT_FALSE(FlowExport.ok);

    TEST_ASSERT_EQUAL_HEX8(0xAA, g_buf[0]); // untouched throughout
}
