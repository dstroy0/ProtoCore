// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/application/ntp_server/ntp_server.h"
#include "network_drivers/transport/udp/server/server.h"
#include "protocore_net_host.h"
#include "services/timing_position/time_source/time_source.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void make_request(uint8_t *req, uint8_t vn, uint8_t poll, uint32_t tx_sec, uint32_t tx_frac)
{
    memset(req, 0, PROTOCORE_NTP_PACKET_LEN);
    req[PROTOCORE_NTP_OFF_LI_VN_MODE] = PROTOCORE_NTP_LI_VN_MODE(PROTOCORE_NTP_LI_NONE, vn, PROTOCORE_NTP_MODE_CLIENT);
    req[PROTOCORE_NTP_OFF_POLL] = poll;
    wr_be32(req + PROTOCORE_NTP_OFF_TX_SEC, tx_sec);
    wr_be32(req + PROTOCORE_NTP_OFF_TX_FRAC, tx_frac);
}

void test_rfc4330_reply_field_table(void)
{
    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    uint8_t out[PROTOCORE_NTP_PACKET_LEN];
    const uint32_t secs = 0xE4A2C1F0u;
    const uint32_t frac = 0x80000000u;

    make_request(req, 4, 6, 0xCAFEF00Du, 0x0000FFFFu);
    TEST_ASSERT_EQUAL_UINT(PROTOCORE_NTP_PACKET_LEN, protocore_ntp_server_build_response(
                                                         req, sizeof req, 7, PROTOCORE_NTP_REFID_GPS, secs, frac, out,
                                                         sizeof out));

    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_NTP_LI_NONE, PROTOCORE_NTP_LI_OF(out[PROTOCORE_NTP_OFF_LI_VN_MODE]));
    TEST_ASSERT_EQUAL_UINT8(4, PROTOCORE_NTP_VN_OF(out[PROTOCORE_NTP_OFF_LI_VN_MODE]));
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_NTP_MODE_SERVER, PROTOCORE_NTP_MODE_OF(out[PROTOCORE_NTP_OFF_LI_VN_MODE]));

    TEST_ASSERT_EQUAL_UINT8(7, out[PROTOCORE_NTP_OFF_STRATUM]);
    TEST_ASSERT_EQUAL_HEX32(PROTOCORE_NTP_REFID_GPS, rd_be32(out + PROTOCORE_NTP_OFF_REFID));

    TEST_ASSERT_EQUAL_UINT8(6, out[PROTOCORE_NTP_OFF_POLL]);

    TEST_ASSERT_EQUAL_HEX32(secs, rd_be32(out + PROTOCORE_NTP_OFF_REF_SEC));
    TEST_ASSERT_EQUAL_HEX32(frac, rd_be32(out + PROTOCORE_NTP_OFF_REF_FRAC));

    TEST_ASSERT_EQUAL_HEX32(0xCAFEF00Du, rd_be32(out + PROTOCORE_NTP_OFF_ORIGIN_SEC));
    TEST_ASSERT_EQUAL_HEX32(0x0000FFFFu, rd_be32(out + PROTOCORE_NTP_OFF_ORIGIN_FRAC));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(req + PROTOCORE_NTP_OFF_TX_SEC, out + PROTOCORE_NTP_OFF_ORIGIN_SEC, 8);

    TEST_ASSERT_EQUAL_HEX32(secs, rd_be32(out + PROTOCORE_NTP_OFF_RX_SEC));
    TEST_ASSERT_EQUAL_HEX32(frac, rd_be32(out + PROTOCORE_NTP_OFF_RX_FRAC));
    TEST_ASSERT_EQUAL_HEX32(secs, rd_be32(out + PROTOCORE_NTP_OFF_TX_SEC));
    TEST_ASSERT_EQUAL_HEX32(frac, rd_be32(out + PROTOCORE_NTP_OFF_TX_FRAC));
}

void test_the_version_is_echoed_and_the_mode_is_always_server(void)
{
    for (uint8_t vn = 1; vn <= 4; vn++)
    {
        uint8_t req[PROTOCORE_NTP_PACKET_LEN];
        uint8_t out[PROTOCORE_NTP_PACKET_LEN];
        make_request(req, vn, 6, 1, 1);
        TEST_ASSERT_EQUAL_UINT(PROTOCORE_NTP_PACKET_LEN,
                               protocore_ntp_server_build_response(req, sizeof req, 3, PROTOCORE_NTP_REFID_LOCL, 1, 1,
                                                                   out, sizeof out));
        TEST_ASSERT_EQUAL_UINT8(vn, PROTOCORE_NTP_VN_OF(out[PROTOCORE_NTP_OFF_LI_VN_MODE]));
        TEST_ASSERT_EQUAL_UINT8(PROTOCORE_NTP_MODE_SERVER, PROTOCORE_NTP_MODE_OF(out[PROTOCORE_NTP_OFF_LI_VN_MODE]));
    }
}

void test_a_zero_poll_field_is_answered_with_the_servers_own(void)
{
    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    uint8_t out[PROTOCORE_NTP_PACKET_LEN];

    make_request(req, 4, 0, 1, 1);
    (void)protocore_ntp_server_build_response(req, sizeof req, 3, PROTOCORE_NTP_REFID_LOCL, 1, 1, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT8(6, out[PROTOCORE_NTP_OFF_POLL]);

    make_request(req, 4, 10, 1, 1);
    (void)protocore_ntp_server_build_response(req, sizeof req, 3, PROTOCORE_NTP_REFID_LOCL, 1, 1, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT8(10, out[PROTOCORE_NTP_OFF_POLL]);
}

void test_the_clock_quality_fields_describe_a_millisecond_clock(void)
{
    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    uint8_t out[PROTOCORE_NTP_PACKET_LEN];
    make_request(req, 4, 6, 1, 1);
    (void)protocore_ntp_server_build_response(req, sizeof req, 3, PROTOCORE_NTP_REFID_LOCL, 1, 1, out, sizeof out);

    TEST_ASSERT_EQUAL_INT8(-6, (int8_t)out[PROTOCORE_NTP_OFF_PRECISION]);
    TEST_ASSERT_EQUAL_HEX32(0u, rd_be32(out + PROTOCORE_NTP_OFF_ROOT_DELAY));
    TEST_ASSERT_EQUAL_HEX32(0x00010000u, rd_be32(out + PROTOCORE_NTP_OFF_ROOT_DISP));
}

void test_a_packet_short_of_48_octets_is_refused(void)
{
    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    uint8_t out[PROTOCORE_NTP_PACKET_LEN];
    make_request(req, 4, 6, 1, 1);

    TEST_ASSERT_EQUAL_UINT(0, protocore_ntp_server_build_response(req, PROTOCORE_NTP_PACKET_LEN - 1, 3,
                                                                  PROTOCORE_NTP_REFID_LOCL, 1, 1, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT(0, protocore_ntp_server_build_response(req, sizeof req, 3, PROTOCORE_NTP_REFID_LOCL, 1, 1,
                                                                  out, PROTOCORE_NTP_PACKET_LEN - 1));
    TEST_ASSERT_EQUAL_UINT(0, protocore_ntp_server_build_response(NULL, PROTOCORE_NTP_PACKET_LEN, 3,
                                                                  PROTOCORE_NTP_REFID_LOCL, 1, 1, out, sizeof out));
    TEST_ASSERT_EQUAL_UINT(0, protocore_ntp_server_build_response(req, sizeof req, 3, PROTOCORE_NTP_REFID_LOCL, 1, 1,
                                                                  NULL, PROTOCORE_NTP_PACKET_LEN));

    TEST_ASSERT_EQUAL_UINT(PROTOCORE_NTP_PACKET_LEN,
                           protocore_ntp_server_build_response(req, PROTOCORE_NTP_PACKET_LEN, 3,
                                                               PROTOCORE_NTP_REFID_LOCL, 1, 1, out,
                                                               PROTOCORE_NTP_PACKET_LEN));
}

static uint32_t g_epoch = 0;
static uint32_t fake_clock(void)
{
    return g_epoch;
}

static void poll_once(void)
{
    UdpListener.poll(UdpListener.internal);
}

static proto_bool close_ntp_port(void)
{
    UdpListener.port = PROTOCORE_NTP_PORT;
    UdpListener.close(UdpListener.internal);
    return UdpListener.ok;
}

static void serve(const void *req, uint16_t len, const char *from_ip, uint16_t from_port)
{
    protocore_net_host_udp_reset();
    TEST_ASSERT_TRUE(protocore_net_host_udp_deliver(PROTOCORE_NTP_PORT, from_ip, from_port, (void *)req, len));
    poll_once();
}

static void start_server(uint32_t epoch, uint8_t stratum, uint32_t refid)
{
    protocore_net_host_reset();
    protocore_time_source_reset();
    g_epoch = epoch;
    TEST_ASSERT_TRUE(protocore_time_source_add("test", 0, fake_clock));
    TEST_ASSERT_TRUE(protocore_ntp_server_begin(stratum, refid));
}

void test_begin_binds_udp_123(void)
{
    protocore_net_host_reset();
    TEST_ASSERT_TRUE(protocore_ntp_server_begin(3, PROTOCORE_NTP_REFID_LOCL));
    TEST_ASSERT_NOT_NULL(protocore_net_host_udp_pcb(PROTOCORE_NTP_PORT));
    TEST_ASSERT_TRUE(close_ntp_port());
}

void test_a_request_is_answered_on_the_wire(void)
{
    start_server(1700000000u, 7, PROTOCORE_NTP_REFID_GPS);

    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    make_request(req, 4, 6, 0xCAFEF00Du, 0x0000FFFFu);
    serve(req, PROTOCORE_NTP_PACKET_LEN, "192.0.2.5", 40000);

    TEST_ASSERT_EQUAL_size_t(1u, protocore_net_host_udp_count());
    const protocore_net_host_dgram *d = protocore_net_host_udp_at(0);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_NTP_PACKET_LEN, d->len);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_NTP_PORT, d->src_port);
    TEST_ASSERT_EQUAL_UINT16(40000, d->dst_port);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_NTP_MODE_SERVER, PROTOCORE_NTP_MODE_OF(d->data[PROTOCORE_NTP_OFF_LI_VN_MODE]));
    TEST_ASSERT_EQUAL_UINT8(7, d->data[PROTOCORE_NTP_OFF_STRATUM]);
    TEST_ASSERT_EQUAL_HEX32(PROTOCORE_NTP_REFID_GPS, rd_be32(d->data + PROTOCORE_NTP_OFF_REFID));
    TEST_ASSERT_EQUAL_HEX32(0xCAFEF00Du, rd_be32(d->data + PROTOCORE_NTP_OFF_ORIGIN_SEC));
    TEST_ASSERT_EQUAL_HEX32(1700000000u + PROTOCORE_NTP_UNIX_OFFSET, rd_be32(d->data + PROTOCORE_NTP_OFF_TX_SEC));

    TEST_ASSERT_TRUE(close_ntp_port());
}

void test_a_server_with_no_clock_answers_nothing(void)
{
    start_server(0u, 3, PROTOCORE_NTP_REFID_LOCL);

    uint8_t req[PROTOCORE_NTP_PACKET_LEN];
    make_request(req, 4, 6, 1, 1);
    serve(req, PROTOCORE_NTP_PACKET_LEN, "192.0.2.5", 40000);

    TEST_ASSERT_EQUAL_size_t(0u, protocore_net_host_udp_sent());
    TEST_ASSERT_TRUE(close_ntp_port());
}

void test_a_runt_datagram_is_dropped(void)
{
    start_server(1700000000u, 3, PROTOCORE_NTP_REFID_LOCL);

    uint8_t runt[8] = {0x23, 0, 6, 0, 0, 0, 0, 0};
    serve(runt, (uint16_t)sizeof runt, "192.0.2.5", 40000);

    TEST_ASSERT_EQUAL_size_t(0u, protocore_net_host_udp_sent());
    TEST_ASSERT_TRUE(close_ntp_port());
}
