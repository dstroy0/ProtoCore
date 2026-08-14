// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/application/ptp/ptp.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t CLOCK_ID[8] = {0x00, 0x1B, 0x19, 0xFF, 0xFE, 0x00, 0x00, 0x01};
static const uint8_t REQ_ID[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};

static void base_header(protocore_ptp_header *h)
{
    memset(h, 0, sizeof(*h));
    h->version = 2;
    h->domain = 0;
    memcpy(h->clock_identity, CLOCK_ID, 8);
    h->port_number = 1;
    h->sequence_id = 0x1234;
    h->log_interval = -3;
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

void test_correction_field_is_nanoseconds_scaled_by_2_16(void)
{
    protocore_ptp_header h;
    base_header(&h);
    h.message_type = PROTOCORE_PTP_SYNC;
    h.correction = 0x0000000000028000LL;
    uint8_t buf[PROTOCORE_PTP_HEADER_LEN];
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PTP_HEADER_LEN, protocore_ptp_build_header(buf, sizeof(buf), &h, 0));

    static const uint8_t WANT[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x80, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf + 8, 8);

    TEST_ASSERT_EQUAL_INT64(163840LL, h.correction);

    protocore_ptp_header back;
    TEST_ASSERT_TRUE(protocore_ptp_parse_header(buf, sizeof(buf), &back));
    TEST_ASSERT_EQUAL_INT64(0x0000000000028000LL, back.correction);

    h.correction = -163840LL;
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PTP_HEADER_LEN, protocore_ptp_build_header(buf, sizeof(buf), &h, 0));
    static const uint8_t WANT_NEG[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFD, 0x80, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_NEG, buf + 8, 8);
    TEST_ASSERT_TRUE(protocore_ptp_parse_header(buf, sizeof(buf), &back));
    TEST_ASSERT_EQUAL_INT64(-163840LL, back.correction);
}

void test_header_field_offsets(void)
{
    protocore_ptp_header h;
    base_header(&h);
    h.message_type = PROTOCORE_PTP_ANNOUNCE;
    h.transport_specific = 0x1;
    h.domain = 0x2A;
    h.flags = 0x0208;
    h.correction = 0;
    h.port_number = 0x0003;
    h.sequence_id = 0x1234;
    h.control = 0x05;
    h.log_interval = -3;

    uint8_t buf[PROTOCORE_PTP_HEADER_LEN];
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PTP_HEADER_LEN, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PTP_HEADER_LEN, protocore_ptp_build_header(buf, sizeof(buf), &h, 30));

    TEST_ASSERT_EQUAL_HEX8(0x1B, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1]);
    TEST_ASSERT_EQUAL_UINT16(64, be16(buf + 2));
    TEST_ASSERT_EQUAL_HEX8(0x2A, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[5]);
    TEST_ASSERT_EQUAL_UINT16(0x0208, be16(buf + 6));
    for (int i = 16; i < 20; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, buf[i]);
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CLOCK_ID, buf + 20, 8);
    TEST_ASSERT_EQUAL_UINT16(0x0003, be16(buf + 28));
    TEST_ASSERT_EQUAL_UINT16(0x1234, be16(buf + 30));
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[32]);
    TEST_ASSERT_EQUAL_HEX8(0xFD, buf[33]);
}

void test_header_round_trip(void)
{
    protocore_ptp_header h;
    base_header(&h);
    h.message_type = PROTOCORE_PTP_DELAY_RESP;
    h.transport_specific = 0xF;
    h.domain = 17;
    h.flags = 0xBEEF;
    h.correction = -1;
    h.port_number = 0xFFFF;
    h.sequence_id = 0xFFFF;
    h.control = 0x03;
    h.log_interval = -128;

    uint8_t buf[PROTOCORE_PTP_HEADER_LEN];
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_PTP_HEADER_LEN, protocore_ptp_build_header(buf, sizeof(buf), &h, 20));

    protocore_ptp_header g;
    TEST_ASSERT_TRUE(protocore_ptp_parse_header(buf, sizeof(buf), &g));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_DELAY_RESP, g.message_type);
    TEST_ASSERT_EQUAL_UINT8(0xF, g.transport_specific);
    TEST_ASSERT_EQUAL_UINT8(2, g.version);
    TEST_ASSERT_EQUAL_UINT16(54, g.message_length);
    TEST_ASSERT_EQUAL_UINT8(17, g.domain);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, g.flags);
    TEST_ASSERT_EQUAL_INT64(-1, g.correction);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CLOCK_ID, g.clock_identity, 8);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, g.port_number);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, g.sequence_id);
    TEST_ASSERT_EQUAL_HEX8(0x03, g.control);
    TEST_ASSERT_EQUAL_INT8(-128, g.log_interval);
}

void test_message_type_numbers(void)
{
    TEST_ASSERT_EQUAL_UINT8(0x0, PROTOCORE_PTP_SYNC);
    TEST_ASSERT_EQUAL_UINT8(0x1, PROTOCORE_PTP_DELAY_REQ);
    TEST_ASSERT_EQUAL_UINT8(0x2, PROTOCORE_PTP_PDELAY_REQ);
    TEST_ASSERT_EQUAL_UINT8(0x3, PROTOCORE_PTP_PDELAY_RESP);
    TEST_ASSERT_EQUAL_UINT8(0x8, PROTOCORE_PTP_FOLLOW_UP);
    TEST_ASSERT_EQUAL_UINT8(0x9, PROTOCORE_PTP_DELAY_RESP);
    TEST_ASSERT_EQUAL_UINT8(0xA, PROTOCORE_PTP_PDELAY_RESP_FOLLOW_UP);
    TEST_ASSERT_EQUAL_UINT8(0xB, PROTOCORE_PTP_ANNOUNCE);

    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_PTP_SYNC & 0x8);
    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_PTP_DELAY_REQ & 0x8);
    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_PTP_PDELAY_REQ & 0x8);
    TEST_ASSERT_EQUAL_UINT8(0, PROTOCORE_PTP_PDELAY_RESP & 0x8);
    TEST_ASSERT_EQUAL_UINT8(8, PROTOCORE_PTP_FOLLOW_UP & 0x8);
    TEST_ASSERT_EQUAL_UINT8(8, PROTOCORE_PTP_DELAY_RESP & 0x8);
    TEST_ASSERT_EQUAL_UINT8(8, PROTOCORE_PTP_PDELAY_RESP_FOLLOW_UP & 0x8);
    TEST_ASSERT_EQUAL_UINT8(8, PROTOCORE_PTP_ANNOUNCE & 0x8);
}

void test_timestamp_octet_layout(void)
{
    protocore_ptp_timestamp ts;
    ts.seconds = 0x010203040506ULL;
    ts.nanoseconds = 0x0708090AU;
    uint8_t w[PROTOCORE_PTP_TS_LEN];
    protocore_ptp_ts_write(w, &ts);
    static const uint8_t WANT[10] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
    TEST_ASSERT_EQUAL_size_t(10, sizeof(w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, w, sizeof(WANT));

    protocore_ptp_timestamp back;
    protocore_ptp_ts_read(w, &back);
    TEST_ASSERT_EQUAL_UINT64(0x010203040506ULL, back.seconds);
    TEST_ASSERT_EQUAL_UINT32(0x0708090AU, back.nanoseconds);

    ts.seconds = 0xFFFFFFFFFFFFULL;
    ts.nanoseconds = 999999999U;
    protocore_ptp_ts_write(w, &ts);
    protocore_ptp_ts_read(w, &back);
    TEST_ASSERT_EQUAL_UINT64(0xFFFFFFFFFFFFULL, back.seconds);
    TEST_ASSERT_EQUAL_UINT32(999999999U, back.nanoseconds);
}

void test_timestamp_nanosecond_conversion(void)
{
    static const int64_t NS[] = {0, 1, 999999999LL, 1000000000LL, 1000000001LL, 1234567890123456789LL};
    for (size_t i = 0; i < sizeof(NS) / sizeof(NS[0]); i++)
    {
        protocore_ptp_timestamp ts;
        protocore_ptp_ts_from_ns(NS[i], &ts);
        TEST_ASSERT_TRUE(ts.nanoseconds < 1000000000U);
        TEST_ASSERT_EQUAL_INT64(NS[i], protocore_ptp_ts_to_ns(&ts));
    }

    protocore_ptp_timestamp ts;
    protocore_ptp_ts_from_ns(1500000000LL, &ts);
    TEST_ASSERT_EQUAL_UINT64(1, ts.seconds);
    TEST_ASSERT_EQUAL_UINT32(500000000U, ts.nanoseconds);

    protocore_ptp_ts_from_ns(-1, &ts);
    TEST_ASSERT_EQUAL_UINT64(0, ts.seconds);
    TEST_ASSERT_EQUAL_UINT32(0, ts.nanoseconds);
}

void test_message_lengths(void)
{
    protocore_ptp_header h;
    base_header(&h);
    protocore_ptp_timestamp ts = {12, 500};
    protocore_ptp_announce a;
    memset(&a, 0, sizeof(a));
    a.origin = ts;
    uint8_t buf[128];

    struct
    {
        size_t total;
        uint16_t stamped;
    } got;

    got.total = protocore_ptp_build_sync(buf, sizeof(buf), &h, &ts);
    got.stamped = be16(buf + 2);
    TEST_ASSERT_EQUAL_size_t(44, got.total);
    TEST_ASSERT_EQUAL_UINT16(44, got.stamped);

    got.total = protocore_ptp_build_delay_req(buf, sizeof(buf), &h, &ts);
    TEST_ASSERT_EQUAL_size_t(44, got.total);
    TEST_ASSERT_EQUAL_UINT16(44, be16(buf + 2));

    got.total = protocore_ptp_build_follow_up(buf, sizeof(buf), &h, &ts);
    TEST_ASSERT_EQUAL_size_t(44, got.total);
    TEST_ASSERT_EQUAL_UINT16(44, be16(buf + 2));

    got.total = protocore_ptp_build_delay_resp(buf, sizeof(buf), &h, &ts, REQ_ID, 7);
    TEST_ASSERT_EQUAL_size_t(54, got.total);
    TEST_ASSERT_EQUAL_UINT16(54, be16(buf + 2));

    got.total = protocore_ptp_build_pdelay_req(buf, sizeof(buf), &h, &ts);
    TEST_ASSERT_EQUAL_size_t(54, got.total);
    TEST_ASSERT_EQUAL_UINT16(54, be16(buf + 2));

    got.total = protocore_ptp_build_pdelay_resp(buf, sizeof(buf), &h, &ts, REQ_ID, 7);
    TEST_ASSERT_EQUAL_size_t(54, got.total);
    TEST_ASSERT_EQUAL_UINT16(54, be16(buf + 2));

    got.total = protocore_ptp_build_pdelay_resp_follow_up(buf, sizeof(buf), &h, &ts, REQ_ID, 7);
    TEST_ASSERT_EQUAL_size_t(54, got.total);
    TEST_ASSERT_EQUAL_UINT16(54, be16(buf + 2));

    got.total = protocore_ptp_build_announce(buf, sizeof(buf), &h, &a);
    TEST_ASSERT_EQUAL_size_t(64, got.total);
    TEST_ASSERT_EQUAL_UINT16(64, be16(buf + 2));
}

void test_timestamp_message_build_and_parse(void)
{
    protocore_ptp_header h;
    base_header(&h);
    protocore_ptp_timestamp ts = {0x0000000012345678ULL, 987654321U};
    uint8_t buf[64];

    struct
    {
        size_t (*build)(uint8_t *, size_t, const protocore_ptp_header *, const protocore_ptp_timestamp *);
        uint8_t type;
    } static const CASES[] = {
        {protocore_ptp_build_sync, PROTOCORE_PTP_SYNC},
        {protocore_ptp_build_delay_req, PROTOCORE_PTP_DELAY_REQ},
        {protocore_ptp_build_follow_up, PROTOCORE_PTP_FOLLOW_UP},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        size_t n = CASES[i].build(buf, sizeof(buf), &h, &ts);
        TEST_ASSERT_EQUAL_size_t(44, n);
        TEST_ASSERT_EQUAL_UINT8(CASES[i].type, buf[0] & 0x0F);

        protocore_ptp_timestamp on_wire;
        protocore_ptp_ts_read(buf + PROTOCORE_PTP_HEADER_LEN, &on_wire);
        TEST_ASSERT_EQUAL_UINT64(ts.seconds, on_wire.seconds);
        TEST_ASSERT_EQUAL_UINT32(ts.nanoseconds, on_wire.nanoseconds);

        protocore_ptp_header g;
        protocore_ptp_timestamp got;
        TEST_ASSERT_TRUE(protocore_ptp_parse_timestamp_msg(buf, n, &g, &got));
        TEST_ASSERT_EQUAL_UINT8(CASES[i].type, g.message_type);
        TEST_ASSERT_EQUAL_UINT64(ts.seconds, got.seconds);
        TEST_ASSERT_EQUAL_UINT32(ts.nanoseconds, got.nanoseconds);
    }

    protocore_ptp_announce a;
    memset(&a, 0, sizeof(a));
    size_t n = protocore_ptp_build_announce(buf, sizeof(buf), &h, &a);
    protocore_ptp_header g;
    protocore_ptp_timestamp got;
    TEST_ASSERT_FALSE(protocore_ptp_parse_timestamp_msg(buf, n, &g, &got));
}

void test_delay_resp_body(void)
{
    protocore_ptp_header h;
    base_header(&h);
    protocore_ptp_timestamp t4 = {1000, 250000000U};
    uint8_t buf[64];
    size_t n = protocore_ptp_build_delay_resp(buf, sizeof(buf), &h, &t4, REQ_ID, 0x0007);
    TEST_ASSERT_EQUAL_size_t(54, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ_ID, buf + 44, 8);
    TEST_ASSERT_EQUAL_UINT16(0x0007, be16(buf + 52));

    protocore_ptp_header g;
    protocore_ptp_delay_resp out;
    TEST_ASSERT_TRUE(protocore_ptp_parse_delay_resp(buf, n, &g, &out));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_DELAY_RESP, g.message_type);
    TEST_ASSERT_EQUAL_UINT64(1000, out.receive.seconds);
    TEST_ASSERT_EQUAL_UINT32(250000000U, out.receive.nanoseconds);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ_ID, out.req_clock_id, 8);
    TEST_ASSERT_EQUAL_UINT16(0x0007, out.req_port);

    protocore_ptp_timestamp ts = {0, 0};
    size_t sn = protocore_ptp_build_sync(buf, sizeof(buf), &h, &ts);
    TEST_ASSERT_FALSE(protocore_ptp_parse_delay_resp(buf, sn, &g, &out));
}

void test_peer_delay_messages(void)
{
    protocore_ptp_header h;
    base_header(&h);
    uint8_t buf[64];

    protocore_ptp_timestamp origin = {0, 0};
    size_t n = protocore_ptp_build_pdelay_req(buf, sizeof(buf), &h, &origin);
    TEST_ASSERT_EQUAL_size_t(54, n);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_PDELAY_REQ, buf[0] & 0x0F);
    for (size_t i = 44; i < 54; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, buf[i]);
    }
    protocore_ptp_header g;
    protocore_ptp_timestamp got;
    TEST_ASSERT_TRUE(protocore_ptp_parse_pdelay_req(buf, n, &g, &got));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_PDELAY_REQ, g.message_type);

    protocore_ptp_timestamp t2 = {5, 6};
    n = protocore_ptp_build_pdelay_resp(buf, sizeof(buf), &h, &t2, REQ_ID, 0x0042);
    TEST_ASSERT_EQUAL_size_t(54, n);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_PDELAY_RESP, buf[0] & 0x0F);
    protocore_ptp_pdelay_resp pr;
    TEST_ASSERT_TRUE(protocore_ptp_parse_pdelay_resp(buf, n, &g, &pr));
    TEST_ASSERT_EQUAL_UINT64(5, pr.timestamp.seconds);
    TEST_ASSERT_EQUAL_UINT32(6, pr.timestamp.nanoseconds);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ_ID, pr.req_clock_id, 8);
    TEST_ASSERT_EQUAL_UINT16(0x0042, pr.req_port);

    TEST_ASSERT_FALSE(protocore_ptp_parse_pdelay_resp_follow_up(buf, n, &g, &pr));

    protocore_ptp_timestamp t3 = {7, 8};
    n = protocore_ptp_build_pdelay_resp_follow_up(buf, sizeof(buf), &h, &t3, REQ_ID, 0x0042);
    TEST_ASSERT_EQUAL_size_t(54, n);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_PDELAY_RESP_FOLLOW_UP, buf[0] & 0x0F);
    TEST_ASSERT_TRUE(protocore_ptp_parse_pdelay_resp_follow_up(buf, n, &g, &pr));
    TEST_ASSERT_EQUAL_UINT64(7, pr.timestamp.seconds);
    TEST_ASSERT_EQUAL_UINT32(8, pr.timestamp.nanoseconds);
    TEST_ASSERT_FALSE(protocore_ptp_parse_pdelay_resp(buf, n, &g, &pr));
}

void test_announce_body_offsets(void)
{
    protocore_ptp_header h;
    base_header(&h);
    protocore_ptp_announce a;
    memset(&a, 0, sizeof(a));
    a.origin.seconds = 0x0000AABBCCDDULL;
    a.origin.nanoseconds = 0x11223344U;
    a.utc_offset = 37;
    a.gm_priority1 = 128;
    a.gm_clock_class = 6;
    a.gm_clock_accuracy = 0x21;
    a.gm_variance = 0x436A;
    a.gm_priority2 = 128;
    memcpy(a.gm_identity, REQ_ID, 8);
    a.steps_removed = 3;
    a.time_source = 0x20;

    uint8_t buf[80];
    size_t n = protocore_ptp_build_announce(buf, sizeof(buf), &h, &a);
    TEST_ASSERT_EQUAL_size_t(64, n);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_PTP_ANNOUNCE, buf[0] & 0x0F);
    TEST_ASSERT_EQUAL_UINT16(37, be16(buf + 44));
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[46]);
    TEST_ASSERT_EQUAL_HEX8(128, buf[47]);
    TEST_ASSERT_EQUAL_HEX8(6, buf[48]);
    TEST_ASSERT_EQUAL_HEX8(0x21, buf[49]);
    TEST_ASSERT_EQUAL_UINT16(0x436A, be16(buf + 50));
    TEST_ASSERT_EQUAL_HEX8(128, buf[52]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ_ID, buf + 53, 8);
    TEST_ASSERT_EQUAL_UINT16(3, be16(buf + 61));
    TEST_ASSERT_EQUAL_HEX8(0x20, buf[63]);

    protocore_ptp_header g;
    protocore_ptp_announce got;
    TEST_ASSERT_TRUE(protocore_ptp_parse_announce(buf, n, &g, &got));
    TEST_ASSERT_EQUAL_UINT64(0x0000AABBCCDDULL, got.origin.seconds);
    TEST_ASSERT_EQUAL_UINT32(0x11223344U, got.origin.nanoseconds);
    TEST_ASSERT_EQUAL_INT16(37, got.utc_offset);
    TEST_ASSERT_EQUAL_UINT8(128, got.gm_priority1);
    TEST_ASSERT_EQUAL_UINT8(6, got.gm_clock_class);
    TEST_ASSERT_EQUAL_UINT8(0x21, got.gm_clock_accuracy);
    TEST_ASSERT_EQUAL_UINT16(0x436A, got.gm_variance);
    TEST_ASSERT_EQUAL_UINT8(128, got.gm_priority2);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REQ_ID, got.gm_identity, 8);
    TEST_ASSERT_EQUAL_UINT16(3, got.steps_removed);
    TEST_ASSERT_EQUAL_UINT8(0x20, got.time_source);
}

void test_announce_utc_offset_is_signed(void)
{
    protocore_ptp_header h;
    base_header(&h);
    protocore_ptp_announce a;
    memset(&a, 0, sizeof(a));
    a.utc_offset = -1;
    uint8_t buf[80];
    size_t n = protocore_ptp_build_announce(buf, sizeof(buf), &h, &a);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[44]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[45]);

    protocore_ptp_header g;
    protocore_ptp_announce got;
    TEST_ASSERT_TRUE(protocore_ptp_parse_announce(buf, n, &g, &got));
    TEST_ASSERT_EQUAL_INT16(-1, got.utc_offset);
}

void test_builders_stamp_version_two(void)
{
    protocore_ptp_header h;
    memset(&h, 0, sizeof(h));
    protocore_ptp_timestamp ts = {1, 2};
    protocore_ptp_announce a;
    memset(&a, 0, sizeof(a));
    uint8_t buf[80];

    TEST_ASSERT_EQUAL_size_t(44, protocore_ptp_build_sync(buf, sizeof(buf), &h, &ts));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1]);
    TEST_ASSERT_EQUAL_size_t(54, protocore_ptp_build_delay_resp(buf, sizeof(buf), &h, &ts, REQ_ID, 1));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1]);
    TEST_ASSERT_EQUAL_size_t(54, protocore_ptp_build_pdelay_req(buf, sizeof(buf), &h, &ts));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1]);
    TEST_ASSERT_EQUAL_size_t(54, protocore_ptp_build_pdelay_resp(buf, sizeof(buf), &h, &ts, REQ_ID, 1));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1]);
    TEST_ASSERT_EQUAL_size_t(64, protocore_ptp_build_announce(buf, sizeof(buf), &h, &a));
    TEST_ASSERT_EQUAL_HEX8(0x02, buf[1]);
}

void test_offset_and_delay_from_the_four_timestamps(void)
{
    struct
    {
        int64_t t1, d, o;
    } static const CASES[] = {
        {1000000000LL, 5000LL, 1234000LL},
        {1000000000LL, 5000LL, -1234000LL},
        {0LL, 5000LL, 0LL},
        {0LL, 0LL, 7LL},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        const int64_t t1 = CASES[i].t1;
        const int64_t t2 = t1 + CASES[i].d + CASES[i].o;
        const int64_t t3 = t1 + 2000000LL;
        const int64_t t4 = t3 + CASES[i].d - CASES[i].o;
        protocore_ptp_sync s;
        protocore_ptp_compute(t1, t2, t3, t4, &s);
        TEST_ASSERT_EQUAL_INT64(CASES[i].o, s.offset_ns);
        TEST_ASSERT_EQUAL_INT64(CASES[i].d, s.delay_ns);
    }
}

void test_offset_and_delay_worked_example(void)
{
    protocore_ptp_sync s;
    protocore_ptp_compute(1000000000LL, 1001239000LL, 1002000000LL, 1000771000LL, &s);
    TEST_ASSERT_EQUAL_INT64(1234000LL, s.offset_ns);
    TEST_ASSERT_EQUAL_INT64(5000LL, s.delay_ns);
}

void test_peer_link_delay_is_independent_of_the_peer_offset(void)
{
    const int64_t d = 7500LL;
    const int64_t t1 = 0LL;
    const int64_t t3 = 3500000LL;

    const int64_t o1 = 3000000LL;
    TEST_ASSERT_EQUAL_INT64(d, protocore_ptp_compute_link_delay(t1, t1 + d + o1, t3, t3 + d - o1));

    const int64_t o2 = -12345678LL;
    TEST_ASSERT_EQUAL_INT64(d, protocore_ptp_compute_link_delay(t1, t1 + d + o2, t3, t3 + d - o2));

    TEST_ASSERT_EQUAL_INT64(d, protocore_ptp_compute_link_delay(t1, t1 + d, t3, t3 + d));
}

void test_short_buffers_are_refused(void)
{
    protocore_ptp_header h;
    base_header(&h);
    protocore_ptp_timestamp ts = {1, 2};
    protocore_ptp_announce a;
    memset(&a, 0, sizeof(a));
    uint8_t buf[80];

    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_header(buf, 33, &h, 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_header(NULL, sizeof(buf), &h, 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_header(buf, sizeof(buf), NULL, 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_sync(buf, 43, &h, &ts));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_delay_resp(buf, 53, &h, &ts, REQ_ID, 1));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_delay_resp(buf, sizeof(buf), &h, &ts, NULL, 1));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_pdelay_req(buf, 53, &h, &ts));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_pdelay_resp(buf, 53, &h, &ts, REQ_ID, 1));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_pdelay_resp_follow_up(buf, 53, &h, &ts, REQ_ID, 1));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ptp_build_announce(buf, 63, &h, &a));

    protocore_ptp_header g;
    protocore_ptp_timestamp got;
    protocore_ptp_delay_resp dr;
    protocore_ptp_announce an;
    protocore_ptp_pdelay_resp pr;

    TEST_ASSERT_FALSE(protocore_ptp_parse_header(buf, 33, &g));
    TEST_ASSERT_FALSE(protocore_ptp_parse_header(NULL, 34, &g));

    size_t n = protocore_ptp_build_sync(buf, sizeof(buf), &h, &ts);
    TEST_ASSERT_FALSE(protocore_ptp_parse_timestamp_msg(buf, n - 1, &g, &got));

    n = protocore_ptp_build_delay_resp(buf, sizeof(buf), &h, &ts, REQ_ID, 1);
    TEST_ASSERT_FALSE(protocore_ptp_parse_delay_resp(buf, n - 1, &g, &dr));

    n = protocore_ptp_build_announce(buf, sizeof(buf), &h, &a);
    TEST_ASSERT_FALSE(protocore_ptp_parse_announce(buf, n - 1, &g, &an));

    n = protocore_ptp_build_pdelay_resp(buf, sizeof(buf), &h, &ts, REQ_ID, 1);
    TEST_ASSERT_FALSE(protocore_ptp_parse_pdelay_resp(buf, n - 1, &g, &pr));
}

void test_transport_ports(void)
{
    TEST_ASSERT_EQUAL_UINT16(319, PROTOCORE_PTP_EVENT_PORT);
    TEST_ASSERT_EQUAL_UINT16(320, PROTOCORE_PTP_GENERAL_PORT);
}
