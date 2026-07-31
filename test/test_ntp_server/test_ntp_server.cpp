// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the NTP server response codec (services/pc_ntp_server_build_response): a pure
// RFC 5905 server-mode reply builder - version echo, mode/LI/stratum, origin-timestamp copy,
// reference/receive/transmit stamps, big-endian encoding, and the length guards.

#include "services/timing_position/ntp_server/ntp_server.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// Build a plausible client request: LI=0, VN, Mode=3 (client), a poll, and a transmit stamp.
static void make_request(uint8_t *req, uint8_t vn, uint8_t poll, uint32_t xmit_s, uint32_t xmit_f)
{
    memset(req, 0, NTP_PACKET_LEN);
    req[0] = (uint8_t)((0u << 6) | (vn << 3) | 3u);
    req[2] = poll;
    req[40] = (uint8_t)(xmit_s >> 24);
    req[41] = (uint8_t)(xmit_s >> 16);
    req[42] = (uint8_t)(xmit_s >> 8);
    req[43] = (uint8_t)xmit_s;
    req[44] = (uint8_t)(xmit_f >> 24);
    req[45] = (uint8_t)(xmit_f >> 16);
    req[46] = (uint8_t)(xmit_f >> 8);
    req[47] = (uint8_t)xmit_f;
}

void setUp()
{
}
void tearDown()
{
}

void test_happy_path_fields()
{
    uint8_t req[NTP_PACKET_LEN], out[NTP_PACKET_LEN];
    make_request(req, 4, 6, 0xDEADBEEFu, 0x12345678u);
    uint32_t secs = 0xE6C50000u, frac = 0x80000000u; // arbitrary NTP time, half-second fraction

    size_t n = pc_ntp_server_build_response(req, sizeof(req), 3, NTP_REFID_LOCL, secs, frac, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT(NTP_PACKET_LEN, n);

    TEST_ASSERT_EQUAL_UINT8(0, out[0] >> 6);         // LI = 0 (in sync)
    TEST_ASSERT_EQUAL_UINT8(4, (out[0] >> 3) & 0x7); // VN echoed from the request
    TEST_ASSERT_EQUAL_UINT8(4, out[0] & 0x7);        // Mode = 4 (server)
    TEST_ASSERT_EQUAL_UINT8(3, out[1]);              // stratum
    TEST_ASSERT_EQUAL_UINT32(NTP_REFID_LOCL, rd_be32(out + 12));
    TEST_ASSERT_EQUAL_UINT32(secs, rd_be32(out + 16)); // reference timestamp
    TEST_ASSERT_EQUAL_UINT32(frac, rd_be32(out + 20));
    TEST_ASSERT_EQUAL_UINT32(secs, rd_be32(out + 32)); // receive timestamp
    TEST_ASSERT_EQUAL_UINT32(frac, rd_be32(out + 36));
    TEST_ASSERT_EQUAL_UINT32(secs, rd_be32(out + 40)); // transmit timestamp
    TEST_ASSERT_EQUAL_UINT32(frac, rd_be32(out + 44));
}

void test_origin_is_client_transmit()
{
    uint8_t req[NTP_PACKET_LEN], out[NTP_PACKET_LEN];
    make_request(req, 4, 6, 0xCAFEF00Du, 0x0000FFFFu);
    pc_ntp_server_build_response(req, sizeof(req), 3, NTP_REFID_LOCL, 1, 2, out, sizeof(out));
    // Origin timestamp (bytes 24..31) must be a byte-exact copy of the request's transmit stamp.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(req + 40, out + 24, 8);
    TEST_ASSERT_EQUAL_UINT32(0xCAFEF00Du, rd_be32(out + 24));
    TEST_ASSERT_EQUAL_UINT32(0x0000FFFFu, rd_be32(out + 28));
}

void test_version_echo()
{
    uint8_t req[NTP_PACKET_LEN], out[NTP_PACKET_LEN];
    for (uint8_t vn = 1; vn <= 4; vn++)
    {
        make_request(req, vn, 6, 1, 1);
        pc_ntp_server_build_response(req, sizeof(req), 3, NTP_REFID_LOCL, 1, 1, out, sizeof(out));
        TEST_ASSERT_EQUAL_UINT8(vn, (out[0] >> 3) & 0x7);
        TEST_ASSERT_EQUAL_UINT8(4, out[0] & 0x7); // always answers as server
    }
}

void test_poll_echo_and_default()
{
    uint8_t req[NTP_PACKET_LEN], out[NTP_PACKET_LEN];
    make_request(req, 4, 10, 1, 1);
    pc_ntp_server_build_response(req, sizeof(req), 3, NTP_REFID_LOCL, 1, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT8(10, out[2]); // echoes the client's poll
    make_request(req, 4, 0, 1, 1);
    pc_ntp_server_build_response(req, sizeof(req), 3, NTP_REFID_LOCL, 1, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT8(6, out[2]); // default when the client sent 0
}

void test_stratum_passthrough()
{
    uint8_t req[NTP_PACKET_LEN], out[NTP_PACKET_LEN];
    make_request(req, 4, 6, 1, 1);
    pc_ntp_server_build_response(req, sizeof(req), 7, NTP_REFID_LOCL, 1, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT8(7, out[1]);
}

void test_big_endian_encoding()
{
    uint8_t req[NTP_PACKET_LEN], out[NTP_PACKET_LEN];
    make_request(req, 4, 6, 1, 1);
    pc_ntp_server_build_response(req, sizeof(req), 3, NTP_REFID_LOCL, 0x01020304u, 0x0A0B0C0Du, out, sizeof(out));
    // Transmit seconds, big-endian.
    TEST_ASSERT_EQUAL_UINT8(0x01, out[40]);
    TEST_ASSERT_EQUAL_UINT8(0x02, out[41]);
    TEST_ASSERT_EQUAL_UINT8(0x03, out[42]);
    TEST_ASSERT_EQUAL_UINT8(0x04, out[43]);
    TEST_ASSERT_EQUAL_UINT8(0x0A, out[44]);
    TEST_ASSERT_EQUAL_UINT8(0x0D, out[47]);
}

void test_length_guards()
{
    uint8_t req[NTP_PACKET_LEN], out[NTP_PACKET_LEN];
    make_request(req, 4, 6, 1, 1);
    TEST_ASSERT_EQUAL_UINT(0, pc_ntp_server_build_response(req, 47, 3, NTP_REFID_LOCL, 1, 1, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, pc_ntp_server_build_response(req, sizeof(req), 3, NTP_REFID_LOCL, 1, 1, out, 47));
    TEST_ASSERT_EQUAL_UINT(0, pc_ntp_server_build_response(nullptr, 48, 3, NTP_REFID_LOCL, 1, 1, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT(0, pc_ntp_server_build_response(req, sizeof(req), 3, NTP_REFID_LOCL, 1, 1, nullptr, 48));
}

void test_root_dispersion_advertised()
{
    uint8_t req[NTP_PACKET_LEN], out[NTP_PACKET_LEN];
    make_request(req, 4, 6, 1, 1);
    pc_ntp_server_build_response(req, sizeof(req), 3, NTP_REFID_LOCL, 1, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(0x00010000u, rd_be32(out + 8)); // ~1 s dispersion (coarse clock)
    TEST_ASSERT_EQUAL_UINT32(0u, rd_be32(out + 4));          // root delay 0
}

void test_begin_is_host_stub()
{
    // On a host build (no ARDUINO/lwIP) pc_ntp_server_begin() cannot bind UDP/123, so it must
    // always report false regardless of the stratum/refid it is asked to advertise.
    TEST_ASSERT_FALSE(pc_ntp_server_begin(1, NTP_REFID_GPS));
    TEST_ASSERT_FALSE(pc_ntp_server_begin(3));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_happy_path_fields);
    RUN_TEST(test_origin_is_client_transmit);
    RUN_TEST(test_version_echo);
    RUN_TEST(test_poll_echo_and_default);
    RUN_TEST(test_stratum_passthrough);
    RUN_TEST(test_big_endian_encoding);
    RUN_TEST(test_length_guards);
    RUN_TEST(test_root_dispersion_advertised);
    RUN_TEST(test_begin_is_host_stub);
    return UNITY_END();
}
