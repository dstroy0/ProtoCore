// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/iot/dds/dds.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t GUID[RTPS_GUIDPREFIX_LEN] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                                  0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};

static const uint8_t VENDOR[2] = {0x01, 0x0f};

typedef struct
{
    uint8_t id;
    uint8_t flags;
    const uint8_t *contents;
    size_t contents_len;
} Seen;

static Seen g_seen[8];
static size_t g_seen_n;
static void *g_seen_arg;

static void on_submessage(uint8_t id, uint8_t flags, const uint8_t *contents, size_t len, void *arg)
{
    g_seen_arg = arg;
    if (g_seen_n < sizeof(g_seen) / sizeof(g_seen[0]))
    {
        g_seen[g_seen_n].id = id;
        g_seen[g_seen_n].flags = flags;
        g_seen[g_seen_n].contents = contents;
        g_seen[g_seen_n].contents_len = len;
        g_seen_n++;
    }
}

static size_t build_header(uint8_t *buf, size_t cap)
{
    Rtps.hdr.guid_prefix = GUID;
    Rtps.hdr.vendor_id = VENDOR;
    Rtps.out.buf = buf;
    Rtps.out.cap = cap;
    Rtps.header(Rtps.internal);
    return Rtps.n;
}

static size_t build_submessage(uint8_t *buf, size_t cap, uint8_t id, uint8_t flags, const uint8_t *contents,
                               uint16_t len)
{
    Rtps.sub.submessage_id = id;
    Rtps.sub.flags = flags;
    Rtps.sub.contents = contents;
    Rtps.sub.contents_len = len;
    Rtps.out.buf = buf;
    Rtps.out.cap = cap;
    Rtps.submessage(Rtps.internal);
    return Rtps.n;
}

static proto_bool walk(const uint8_t *msg, size_t len)
{
    g_seen_n = 0;
    g_seen_arg = NULL;
    Rtps.msg.msg = msg;
    Rtps.msg.len = len;
    Rtps.sink.on_submessage = on_submessage;
    Rtps.sink.arg = g_seen;
    Rtps.parse(Rtps.internal);
    return Rtps.ok;
}

void test_header_matches_the_published_layout(void)
{
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(20u, build_header(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT(20u, (unsigned)RTPS_HEADER_LEN);

    TEST_ASSERT_EQUAL_HEX8(0x52, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x54, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x50, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x53, buf[3]);

    TEST_ASSERT_EQUAL_HEX8(RTPS_VERSION[0], buf[4]);
    TEST_ASSERT_EQUAL_HEX8(RTPS_VERSION[1], buf[5]);
    TEST_ASSERT_EQUAL_UINT8(2, RTPS_VERSION[0]);

    TEST_ASSERT_EQUAL_HEX8(VENDOR[0], buf[6]);
    TEST_ASSERT_EQUAL_HEX8(VENDOR[1], buf[7]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(GUID, buf + 8, RTPS_GUIDPREFIX_LEN);

    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[20]);
}

void test_header_refuses_a_short_buffer(void)
{
    uint8_t buf[RTPS_HEADER_LEN];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(0u, build_header(buf, RTPS_HEADER_LEN - 1));
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);
    TEST_ASSERT_EQUAL_UINT(20u, build_header(buf, RTPS_HEADER_LEN));
}

void test_submessage_header_is_four_octets_then_contents(void)
{
    static const uint8_t BODY[6] = {0xde, 0xad, 0xbe, 0xef, 0x00, 0x11};
    uint8_t buf[32];
    memset(buf, 0xAA, sizeof(buf));

    TEST_ASSERT_EQUAL_UINT(4u + 6u, build_submessage(buf, sizeof(buf), RTPS_SM_DATA, RTPS_FLAG_ENDIAN, BODY, 6));
    TEST_ASSERT_EQUAL_HEX8(0x15, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x06, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[3]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BODY, buf + 4, 6);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[10]);
}

void test_octets_to_next_header_follows_the_endianness_flag(void)
{
    static const uint8_t BODY[0x0102] = {0};
    uint8_t big[4 + 0x0102];
    uint8_t little[4 + 0x0102];
    TEST_ASSERT_EQUAL_HEX8(0x01, RTPS_FLAG_ENDIAN);

    TEST_ASSERT_EQUAL_UINT(4u + 0x0102u, build_submessage(big, sizeof(big), RTPS_SM_HEARTBEAT, 0x00, BODY, 0x0102));
    TEST_ASSERT_EQUAL_HEX8(0x01, big[2]);
    TEST_ASSERT_EQUAL_HEX8(0x02, big[3]);

    TEST_ASSERT_EQUAL_UINT(4u + 0x0102u,
                           build_submessage(little, sizeof(little), RTPS_SM_HEARTBEAT, RTPS_FLAG_ENDIAN, BODY, 0x0102));
    TEST_ASSERT_EQUAL_HEX8(0x02, little[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, little[3]);

    uint8_t msg[4 + 0x0102 + RTPS_HEADER_LEN];
    size_t n = build_header(msg, sizeof(msg));
    n += build_submessage(msg + n, sizeof(msg) - n, RTPS_SM_HEARTBEAT, 0x00, BODY, 0x0102);
    TEST_ASSERT_TRUE(walk(msg, n));
    TEST_ASSERT_EQUAL_UINT(0x0102u, g_seen[0].contents_len);
    n = build_header(msg, sizeof(msg));
    n += build_submessage(msg + n, sizeof(msg) - n, RTPS_SM_HEARTBEAT, RTPS_FLAG_ENDIAN, BODY, 0x0102);
    TEST_ASSERT_TRUE(walk(msg, n));
    TEST_ASSERT_EQUAL_UINT(0x0102u, g_seen[0].contents_len);
}

void test_parse_walks_every_submessage(void)
{
    static const uint8_t HB[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const uint8_t DATA[4] = {0xa0, 0xa1, 0xa2, 0xa3};
    uint8_t msg[64];
    size_t n = build_header(msg, sizeof(msg));
    n += build_submessage(msg + n, sizeof(msg) - n, RTPS_SM_HEARTBEAT, RTPS_FLAG_ENDIAN, HB, 8);
    n += build_submessage(msg + n, sizeof(msg) - n, RTPS_SM_DATA, 0x00, DATA, 4);
    TEST_ASSERT_EQUAL_UINT(20u + 12u + 8u, n);

    TEST_ASSERT_TRUE(walk(msg, n));
    TEST_ASSERT_EQUAL_UINT(2u, g_seen_n);
    TEST_ASSERT_EQUAL_HEX8(RTPS_SM_HEARTBEAT, g_seen[0].id);
    TEST_ASSERT_EQUAL_HEX8(RTPS_FLAG_ENDIAN, g_seen[0].flags);
    TEST_ASSERT_EQUAL_UINT(8u, g_seen[0].contents_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(HB, g_seen[0].contents, 8);
    TEST_ASSERT_EQUAL_HEX8(RTPS_SM_DATA, g_seen[1].id);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_seen[1].flags);
    TEST_ASSERT_EQUAL_UINT(4u, g_seen[1].contents_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DATA, g_seen[1].contents, 4);
    TEST_ASSERT_EQUAL_PTR(g_seen, g_seen_arg);
}

void test_zero_octets_to_next_header_runs_to_the_end(void)
{
    uint8_t msg[32];
    size_t n = build_header(msg, sizeof(msg));
    msg[n + 0] = RTPS_SM_DATA;
    msg[n + 1] = 0x00;
    msg[n + 2] = 0x00;
    msg[n + 3] = 0x00;
    msg[n + 4] = 0x77;
    msg[n + 5] = 0x88;
    n += 6;

    TEST_ASSERT_TRUE(walk(msg, n));
    TEST_ASSERT_EQUAL_UINT(1u, g_seen_n);
    TEST_ASSERT_EQUAL_HEX8(RTPS_SM_DATA, g_seen[0].id);
    TEST_ASSERT_EQUAL_UINT(2u, g_seen[0].contents_len);
    TEST_ASSERT_EQUAL_HEX8(0x77, g_seen[0].contents[0]);
    TEST_ASSERT_EQUAL_HEX8(0x88, g_seen[0].contents[1]);
}

void test_pad_and_info_ts_do_not_swallow_the_rest(void)
{
    static const uint8_t KIND[2] = {RTPS_SM_PAD, RTPS_SM_INFO_TS};
    for (size_t k = 0; k < 2; k++)
    {
        uint8_t msg[32];
        size_t n = build_header(msg, sizeof(msg));
        msg[n + 0] = KIND[k];
        msg[n + 1] = 0x00;
        msg[n + 2] = 0x00;
        msg[n + 3] = 0x00;
        msg[n + 4] = RTPS_SM_GAP;
        msg[n + 5] = 0x00;
        msg[n + 6] = 0x00;
        msg[n + 7] = 0x02;
        msg[n + 8] = 0xc0;
        msg[n + 9] = 0xde;
        n += 10;

        TEST_ASSERT_TRUE(walk(msg, n));
        TEST_ASSERT_EQUAL_UINT(2u, g_seen_n);
        TEST_ASSERT_EQUAL_HEX8(KIND[k], g_seen[0].id);
        TEST_ASSERT_EQUAL_UINT(0u, g_seen[0].contents_len);
        TEST_ASSERT_NULL(g_seen[0].contents);
        TEST_ASSERT_EQUAL_HEX8(RTPS_SM_GAP, g_seen[1].id);
        TEST_ASSERT_EQUAL_UINT(2u, g_seen[1].contents_len);
    }
}

void test_parse_refuses_a_foreign_protocol(void)
{
    uint8_t msg[RTPS_HEADER_LEN];
    for (size_t i = 0; i < 4; i++)
    {
        (void)build_header(msg, sizeof(msg));
        msg[i] = (uint8_t)(msg[i] ^ 0xFFu);
        TEST_ASSERT_FALSE(walk(msg, sizeof(msg)));
    }
    (void)build_header(msg, sizeof(msg));
    TEST_ASSERT_TRUE(walk(msg, sizeof(msg)));
}

void test_parse_refuses_an_unsupported_version(void)
{
    uint8_t msg[RTPS_HEADER_LEN];
    (void)build_header(msg, sizeof(msg));
    msg[4] = (uint8_t)(RTPS_VERSION[0] + 1);
    TEST_ASSERT_FALSE(walk(msg, sizeof(msg)));

    (void)build_header(msg, sizeof(msg));
    msg[5] = (uint8_t)(RTPS_VERSION[1] + 1);
    TEST_ASSERT_FALSE(walk(msg, sizeof(msg)));

    (void)build_header(msg, sizeof(msg));
    msg[5] = 0;
    TEST_ASSERT_TRUE(walk(msg, sizeof(msg)));
}

void test_parse_refuses_a_message_short_of_the_header(void)
{
    uint8_t msg[RTPS_HEADER_LEN];
    (void)build_header(msg, sizeof(msg));
    for (size_t len = 0; len < RTPS_HEADER_LEN; len++)
    {
        TEST_ASSERT_FALSE(walk(msg, len));
    }
    TEST_ASSERT_TRUE(walk(msg, RTPS_HEADER_LEN));
    TEST_ASSERT_EQUAL_UINT(0u, g_seen_n);
    TEST_ASSERT_FALSE(walk(NULL, RTPS_HEADER_LEN));
}

void test_contents_past_the_end_are_refused(void)
{
    uint8_t msg[32];
    size_t n = build_header(msg, sizeof(msg));
    msg[n + 0] = RTPS_SM_GAP;
    msg[n + 1] = 0x00;
    msg[n + 2] = 0x00;
    msg[n + 3] = 0x10;
    msg[n + 4] = 0x00;
    msg[n + 5] = 0x00;
    n += 6;
    TEST_ASSERT_FALSE(walk(msg, n));
}

void test_a_trailing_stub_ends_the_walk(void)
{
    static const uint8_t BODY[2] = {0x5a, 0xa5};
    uint8_t msg[32];
    size_t n = build_header(msg, sizeof(msg));
    n += build_submessage(msg + n, sizeof(msg) - n, RTPS_SM_ACKNACK, 0x00, BODY, 2);
    msg[n++] = RTPS_SM_HEARTBEAT;
    msg[n++] = 0x00;
    msg[n++] = 0x00;

    TEST_ASSERT_TRUE(walk(msg, n));
    TEST_ASSERT_EQUAL_UINT(1u, g_seen_n);
    TEST_ASSERT_EQUAL_HEX8(RTPS_SM_ACKNACK, g_seen[0].id);
}

void test_submessage_kinds_round_trip(void)
{
    static const uint8_t KIND[] = {RTPS_SM_PAD,      RTPS_SM_ACKNACK,        RTPS_SM_HEARTBEAT, RTPS_SM_GAP,
                                   RTPS_SM_INFO_TS,  RTPS_SM_INFO_SRC,       RTPS_SM_INFO_DST,  RTPS_SM_INFO_REPLY,
                                   RTPS_SM_DATA,     RTPS_SM_DATA_FRAG,      RTPS_SM_INFO_REPLY_IP4};
    static const uint8_t BODY[3] = {0x11, 0x22, 0x33};

    TEST_ASSERT_EQUAL_HEX8(0x01, RTPS_SM_PAD);
    TEST_ASSERT_EQUAL_HEX8(0x06, RTPS_SM_ACKNACK);
    TEST_ASSERT_EQUAL_HEX8(0x07, RTPS_SM_HEARTBEAT);
    TEST_ASSERT_EQUAL_HEX8(0x08, RTPS_SM_GAP);
    TEST_ASSERT_EQUAL_HEX8(0x09, RTPS_SM_INFO_TS);
    TEST_ASSERT_EQUAL_HEX8(0x0c, RTPS_SM_INFO_SRC);
    TEST_ASSERT_EQUAL_HEX8(0x0d, RTPS_SM_INFO_REPLY_IP4);
    TEST_ASSERT_EQUAL_HEX8(0x0e, RTPS_SM_INFO_DST);
    TEST_ASSERT_EQUAL_HEX8(0x0f, RTPS_SM_INFO_REPLY);
    TEST_ASSERT_EQUAL_HEX8(0x15, RTPS_SM_DATA);
    TEST_ASSERT_EQUAL_HEX8(0x16, RTPS_SM_DATA_FRAG);

    for (size_t i = 0; i < sizeof(KIND); i++)
    {
        uint8_t msg[32];
        size_t n = build_header(msg, sizeof(msg));
        n += build_submessage(msg + n, sizeof(msg) - n, KIND[i], RTPS_FLAG_ENDIAN, BODY, 3);
        TEST_ASSERT_TRUE(walk(msg, n));
        TEST_ASSERT_EQUAL_UINT(1u, g_seen_n);
        TEST_ASSERT_EQUAL_HEX8(KIND[i], g_seen[0].id);
        TEST_ASSERT_EQUAL_UINT(3u, g_seen[0].contents_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(BODY, g_seen[0].contents, 3);
    }
}

void test_submessage_refuses_a_short_buffer(void)
{
    static const uint8_t BODY[4] = {1, 2, 3, 4};
    uint8_t buf[8];
    memset(buf, 0xAA, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(0u, build_submessage(buf, 7, RTPS_SM_DATA, 0x00, BODY, 4));
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[0]);
    TEST_ASSERT_EQUAL_UINT(8u, build_submessage(buf, 8, RTPS_SM_DATA, 0x00, BODY, 4));

    TEST_ASSERT_EQUAL_UINT(0u, build_submessage(buf, sizeof(buf), RTPS_SM_DATA, 0x00, NULL, 4));

    TEST_ASSERT_EQUAL_UINT(0u, build_submessage(NULL, 64, RTPS_SM_DATA, 0x00, BODY, 4));
}

void test_parse_without_a_sink_still_validates(void)
{
    static const uint8_t BODY[2] = {0x01, 0x02};
    uint8_t msg[32];
    size_t n = build_header(msg, sizeof(msg));
    n += build_submessage(msg + n, sizeof(msg) - n, RTPS_SM_DATA, 0x00, BODY, 2);

    Rtps.msg.msg = msg;
    Rtps.msg.len = n;
    Rtps.sink.on_submessage = NULL;
    Rtps.sink.arg = NULL;
    Rtps.parse(Rtps.internal);
    TEST_ASSERT_TRUE(Rtps.ok);

    Rtps.msg.msg = msg;
    Rtps.msg.len = n - 1;
    Rtps.parse(Rtps.internal);
    TEST_ASSERT_FALSE(Rtps.ok);
}
