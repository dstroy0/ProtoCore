// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the FANUC Stream Motion (J519) UDP codec: byte-exact field placement against the
// layout documented by the public Wireshark dissector, symmetric build->parse round trips, the
// IEEE-754 little-endian float encoding, and the length/type guards that separate the two packets
// which share a type code in each direction (Start 8 vs Status 132, Request 16 vs Ack 184).

#include "services/machine_tool/fanuc_j519/fanuc_j519.h"

#include <unity.h>

// Known IEEE-754 binary32 bit patterns, little-endian on the wire.
//   1.0f  = 0x3F800000 -> 00 00 80 3F
//  -2.5f  = 0xC0200000 -> 00 00 20 C0
//   0.5f  = 0x3F000000 -> 00 00 00 3F
static const uint8_t F_ONE[4] = {0x00, 0x00, 0x80, 0x3F};
static const uint8_t F_NEG2_5[4] = {0x00, 0x00, 0x20, 0xC0};

static uint8_t g_buf[256];

void setUp(void)
{
    memset(g_buf, 0xAA, sizeof(g_buf)); // poison: every asserted octet must be written by the builder
}

void tearDown(void)
{
}

// Fill a motion command with distinguishable values.
static void make_motion(J519MotionCommand *m)
{
    memset(m, 0, sizeof(*m));
    m->version_no = 1;
    m->sequence_no = 0x11223344;
    m->last_data = 1;
    m->read_io_type = (uint8_t)J519_IO_DI;
    m->read_io_index = 0x0102;
    m->read_io_mask = 0x0304;
    m->data_style = (uint8_t)J519_STYLE_JOINT;
    m->write_io_type = (uint8_t)J519_IO_DO;
    m->write_io_index = 0x0506;
    m->write_io_mask = 0x0708;
    m->write_io_value = 0x090A;
    for (int i = 0; i < PC_J519_AXES; i++)
    {
        m->joint_data[i] = (float)i;
    }
    m->joint_data[0] = 1.0f;
    m->joint_data[1] = -2.5f;
}

static void make_status(J519RobotStatus *s)
{
    memset(s, 0, sizeof(*s));
    s->version_no = 1;
    s->sequence_no = 0x55667788;
    s->status = J519_STATUS_READY | J519_STATUS_SYSRDY;
    s->read_io_type = (uint8_t)J519_IO_RO;
    s->read_io_index = 0x1112;
    s->read_io_mask = 0x1314;
    s->read_io_value = 0x1516;
    s->time_stamp = 0x99AABBCC;
    for (int i = 0; i < PC_J519_AXES; i++)
    {
        s->cartesian_pose[i] = (float)(100 + i);
        s->joint_pose[i] = (float)(200 + i);
        s->motor_current[i] = (float)(300 + i);
    }
    s->cartesian_pose[0] = 1.0f;
    s->joint_pose[0] = -2.5f;
}

static void make_ack(J519Ack *a)
{
    memset(a, 0, sizeof(*a));
    a->version_no = 1;
    a->axis_no = 6;
    a->threshold_type = (uint32_t)J519_THR_JERK;
    a->max_cart_speed = 2000;
    a->unknown0 = 0xDEADBEEF;
    for (int i = 0; i < PC_J519_THRESHOLDS; i++)
    {
        a->threshold_no_load[i] = (float)i;
        a->threshold_max_load[i] = (float)(1000 + i);
    }
}

// --- header + trivial packets ---------------------------------------------------------------------

void test_build_start_and_stop_exact_bytes(void)
{
    TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)pc_j519_build_start(g_buf, sizeof(g_buf), 1));
    static const uint8_t want_start[8] = {0, 0, 0, 0, 1, 0, 0, 0}; // type 0, version 1
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_start, g_buf, 8);

    TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)pc_j519_build_stop(g_buf, sizeof(g_buf), 1));
    static const uint8_t want_stop[8] = {2, 0, 0, 0, 1, 0, 0, 0}; // type 2, version 1
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_stop, g_buf, 8);
}

void test_peek_reads_type_and_version(void)
{
    pc_j519_build_stop(g_buf, sizeof(g_buf), 0x01020304);
    uint32_t type = 0, ver = 0;
    TEST_ASSERT_TRUE(pc_j519_peek(g_buf, 8, &type, &ver));
    TEST_ASSERT_EQUAL_UINT32(2, type);
    TEST_ASSERT_EQUAL_UINT32(0x01020304, ver);

    // null out-params are permitted; a short buffer is refused
    TEST_ASSERT_TRUE(pc_j519_peek(g_buf, 8, NULL, NULL));
    TEST_ASSERT_FALSE(pc_j519_peek(g_buf, 7, &type, &ver));
    TEST_ASSERT_FALSE(pc_j519_peek(NULL, 8, &type, &ver));
}

// --- motion command -------------------------------------------------------------------------------

void test_build_motion_exact_field_offsets(void)
{
    J519MotionCommand m;
    make_motion(&m);
    TEST_ASSERT_EQUAL_UINT32(64, (uint32_t)pc_j519_build_motion(g_buf, sizeof(g_buf), &m));

    TEST_ASSERT_EQUAL_UINT8(1, g_buf[0]); // type 1, little-endian
    TEST_ASSERT_EQUAL_UINT8(0, g_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1, g_buf[4]); // version
    // sequence 0x11223344 little-endian at 8
    TEST_ASSERT_EQUAL_UINT8(0x44, g_buf[8]);
    TEST_ASSERT_EQUAL_UINT8(0x33, g_buf[9]);
    TEST_ASSERT_EQUAL_UINT8(0x22, g_buf[10]);
    TEST_ASSERT_EQUAL_UINT8(0x11, g_buf[11]);
    TEST_ASSERT_EQUAL_UINT8(1, g_buf[12]);    // last_data
    TEST_ASSERT_EQUAL_UINT8(1, g_buf[13]);    // read_io_type = DI
    TEST_ASSERT_EQUAL_UINT8(0x02, g_buf[14]); // read_io_index 0x0102 le
    TEST_ASSERT_EQUAL_UINT8(0x01, g_buf[15]);
    TEST_ASSERT_EQUAL_UINT8(0x04, g_buf[16]); // read_io_mask 0x0304 le
    TEST_ASSERT_EQUAL_UINT8(0x03, g_buf[17]);
    TEST_ASSERT_EQUAL_UINT8(1, g_buf[18]);    // data_style = joint
    TEST_ASSERT_EQUAL_UINT8(2, g_buf[19]);    // write_io_type = DO
    TEST_ASSERT_EQUAL_UINT8(0x06, g_buf[20]); // write_io_index 0x0506 le
    TEST_ASSERT_EQUAL_UINT8(0x05, g_buf[21]);
    TEST_ASSERT_EQUAL_UINT8(0x08, g_buf[22]); // write_io_mask 0x0708 le
    TEST_ASSERT_EQUAL_UINT8(0x07, g_buf[23]);
    TEST_ASSERT_EQUAL_UINT8(0x0A, g_buf[24]); // write_io_value 0x090A le
    TEST_ASSERT_EQUAL_UINT8(0x09, g_buf[25]);
    TEST_ASSERT_EQUAL_UINT8(0, g_buf[26]); // unused must be zeroed, not left poisoned
    TEST_ASSERT_EQUAL_UINT8(0, g_buf[27]);
    // joint data starts at 28: IEEE-754 binary32 little-endian
    TEST_ASSERT_EQUAL_UINT8_ARRAY(F_ONE, g_buf + 28, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(F_NEG2_5, g_buf + 32, 4);
}

void test_motion_roundtrip(void)
{
    J519MotionCommand m, got;
    make_motion(&m);
    TEST_ASSERT_EQUAL_UINT32(64, (uint32_t)pc_j519_build_motion(g_buf, sizeof(g_buf), &m));
    memset(&got, 0, sizeof(got));
    TEST_ASSERT_TRUE(pc_j519_parse_motion(g_buf, 64, &got));

    TEST_ASSERT_EQUAL_UINT32(m.version_no, got.version_no);
    TEST_ASSERT_EQUAL_UINT32(m.sequence_no, got.sequence_no);
    TEST_ASSERT_EQUAL_UINT8(m.last_data, got.last_data);
    TEST_ASSERT_EQUAL_UINT8(m.read_io_type, got.read_io_type);
    TEST_ASSERT_EQUAL_UINT16(m.read_io_index, got.read_io_index);
    TEST_ASSERT_EQUAL_UINT16(m.read_io_mask, got.read_io_mask);
    TEST_ASSERT_EQUAL_UINT8(m.data_style, got.data_style);
    TEST_ASSERT_EQUAL_UINT8(m.write_io_type, got.write_io_type);
    TEST_ASSERT_EQUAL_UINT16(m.write_io_index, got.write_io_index);
    TEST_ASSERT_EQUAL_UINT16(m.write_io_mask, got.write_io_mask);
    TEST_ASSERT_EQUAL_UINT16(m.write_io_value, got.write_io_value);
    for (int i = 0; i < PC_J519_AXES; i++)
    {
        TEST_ASSERT_EQUAL_FLOAT(m.joint_data[i], got.joint_data[i]);
    }
}

// --- robot status ---------------------------------------------------------------------------------

void test_build_status_exact_field_offsets(void)
{
    J519RobotStatus s;
    make_status(&s);
    TEST_ASSERT_EQUAL_UINT32(132, (uint32_t)pc_j519_build_status(g_buf, sizeof(g_buf), &s));

    TEST_ASSERT_EQUAL_UINT8(0, g_buf[0]); // type 0 (robot->PC = Status)
    TEST_ASSERT_EQUAL_UINT8(0x88, g_buf[8]);
    TEST_ASSERT_EQUAL_UINT8(J519_STATUS_READY | J519_STATUS_SYSRDY, g_buf[12]);
    TEST_ASSERT_EQUAL_UINT8(9, g_buf[13]);    // read_io_type = RO
    TEST_ASSERT_EQUAL_UINT8(0x16, g_buf[18]); // read_io_value 0x1516 le
    TEST_ASSERT_EQUAL_UINT8(0x15, g_buf[19]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, g_buf[20]); // time_stamp 0x99AABBCC le
    TEST_ASSERT_EQUAL_UINT8(0x99, g_buf[23]);
    // the three 9-float blocks sit at 24 / 60 / 96
    TEST_ASSERT_EQUAL_UINT8_ARRAY(F_ONE, g_buf + 24, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(F_NEG2_5, g_buf + 60, 4);
}

void test_status_roundtrip(void)
{
    J519RobotStatus s, got;
    make_status(&s);
    TEST_ASSERT_EQUAL_UINT32(132, (uint32_t)pc_j519_build_status(g_buf, sizeof(g_buf), &s));
    memset(&got, 0, sizeof(got));
    TEST_ASSERT_TRUE(pc_j519_parse_status(g_buf, 132, &got));

    TEST_ASSERT_EQUAL_UINT32(s.sequence_no, got.sequence_no);
    TEST_ASSERT_EQUAL_UINT8(s.status, got.status);
    TEST_ASSERT_EQUAL_UINT16(s.read_io_value, got.read_io_value);
    TEST_ASSERT_EQUAL_UINT32(s.time_stamp, got.time_stamp);
    for (int i = 0; i < PC_J519_AXES; i++)
    {
        TEST_ASSERT_EQUAL_FLOAT(s.cartesian_pose[i], got.cartesian_pose[i]);
        TEST_ASSERT_EQUAL_FLOAT(s.joint_pose[i], got.joint_pose[i]);
        TEST_ASSERT_EQUAL_FLOAT(s.motor_current[i], got.motor_current[i]);
    }
}

// --- request / ack --------------------------------------------------------------------------------

void test_request_roundtrip_and_bytes(void)
{
    J519Request q, got;
    memset(&q, 0, sizeof(q));
    q.version_no = 1;
    q.axis_no = 3;
    q.threshold_type = (uint32_t)J519_THR_ACCELERATION;
    TEST_ASSERT_EQUAL_UINT32(16, (uint32_t)pc_j519_build_request(g_buf, sizeof(g_buf), &q));
    TEST_ASSERT_EQUAL_UINT8(3, g_buf[0]);  // type 3
    TEST_ASSERT_EQUAL_UINT8(3, g_buf[8]);  // axis
    TEST_ASSERT_EQUAL_UINT8(1, g_buf[12]); // threshold type = acceleration

    memset(&got, 0, sizeof(got));
    TEST_ASSERT_TRUE(pc_j519_parse_request(g_buf, 16, &got));
    TEST_ASSERT_EQUAL_UINT32(q.axis_no, got.axis_no);
    TEST_ASSERT_EQUAL_UINT32(q.threshold_type, got.threshold_type);
}

void test_ack_roundtrip_and_table_offsets(void)
{
    J519Ack a, got;
    make_ack(&a);
    TEST_ASSERT_EQUAL_UINT32(184, (uint32_t)pc_j519_build_ack(g_buf, sizeof(g_buf), &a));
    TEST_ASSERT_EQUAL_UINT8(3, g_buf[0]);     // type 3
    TEST_ASSERT_EQUAL_UINT8(0xEF, g_buf[20]); // unknown0 preserved verbatim, little-endian
    TEST_ASSERT_EQUAL_UINT8(0xDE, g_buf[23]);
    // no-load table at 24, max-load table at 104
    TEST_ASSERT_EQUAL_UINT8_ARRAY(F_ONE, g_buf + 24 + 4, 4); // no_load[1] == 1.0f

    memset(&got, 0, sizeof(got));
    TEST_ASSERT_TRUE(pc_j519_parse_ack(g_buf, 184, &got));
    TEST_ASSERT_EQUAL_UINT32(a.axis_no, got.axis_no);
    TEST_ASSERT_EQUAL_UINT32(a.threshold_type, got.threshold_type);
    TEST_ASSERT_EQUAL_UINT32(a.max_cart_speed, got.max_cart_speed);
    TEST_ASSERT_EQUAL_UINT32(a.unknown0, got.unknown0);
    for (int i = 0; i < PC_J519_THRESHOLDS; i++)
    {
        TEST_ASSERT_EQUAL_FLOAT(a.threshold_no_load[i], got.threshold_no_load[i]);
        TEST_ASSERT_EQUAL_FLOAT(a.threshold_max_load[i], got.threshold_max_load[i]);
    }
}

// --- guards ---------------------------------------------------------------------------------------

// The type space is shared per direction, so a parser must not accept the other packet that carries
// its type code. Length is the discriminator: Start(8)/Status(132) both type 0, Request(16)/Ack(184)
// both type 3.
void test_shared_type_codes_are_separated_by_length(void)
{
    // an 8-octet Start must not parse as a Robot Status (both type 0)
    TEST_ASSERT_EQUAL_UINT32(8, (uint32_t)pc_j519_build_start(g_buf, sizeof(g_buf), 1));
    J519RobotStatus st;
    TEST_ASSERT_FALSE(pc_j519_parse_status(g_buf, 8, &st));

    // a 16-octet Request must not parse as an Ack (both type 3)
    J519Request q;
    memset(&q, 0, sizeof(q));
    q.version_no = 1;
    TEST_ASSERT_EQUAL_UINT32(16, (uint32_t)pc_j519_build_request(g_buf, sizeof(g_buf), &q));
    J519Ack ack;
    TEST_ASSERT_FALSE(pc_j519_parse_ack(g_buf, 16, &ack));
    // (The converse - handing the Ack parser len=184 for a 16-octet datagram - is caller error the
    // codec cannot detect: @p len IS the received datagram length, so the caller must pass the true
    // one. Nothing here can distinguish a short read from a genuine 184-octet Ack.)
}

void test_parsers_reject_wrong_type(void)
{
    J519MotionCommand m;
    make_motion(&m);
    pc_j519_build_motion(g_buf, sizeof(g_buf), &m);
    // right length for a motion command, but ask the request parser for it
    J519Request q;
    TEST_ASSERT_FALSE(pc_j519_parse_request(g_buf, 16, &q));

    // corrupt the type word; the motion parser must refuse it
    g_buf[0] = 7;
    J519MotionCommand got;
    TEST_ASSERT_FALSE(pc_j519_parse_motion(g_buf, 64, &got));
}

void test_parsers_reject_off_by_one_lengths(void)
{
    J519MotionCommand m, got;
    make_motion(&m);
    pc_j519_build_motion(g_buf, sizeof(g_buf), &m);
    TEST_ASSERT_FALSE(pc_j519_parse_motion(g_buf, 63, &got));
    TEST_ASSERT_FALSE(pc_j519_parse_motion(g_buf, 65, &got));
    TEST_ASSERT_TRUE(pc_j519_parse_motion(g_buf, 64, &got));
}

void test_builders_reject_short_capacity(void)
{
    J519MotionCommand m;
    make_motion(&m);
    J519RobotStatus s;
    make_status(&s);
    J519Ack a;
    make_ack(&a);
    J519Request q;
    memset(&q, 0, sizeof(q));

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_start(g_buf, 7, 1));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_stop(g_buf, 7, 1));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_motion(g_buf, 63, &m));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_request(g_buf, 15, &q));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_status(g_buf, 131, &s));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_ack(g_buf, 183, &a));
}

void test_null_guards(void)
{
    J519MotionCommand m;
    make_motion(&m);
    J519MotionCommand got;

    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_start(NULL, 64, 1));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_motion(g_buf, sizeof(g_buf), NULL));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_motion(NULL, 64, &m));
    TEST_ASSERT_FALSE(pc_j519_parse_motion(NULL, 64, &got));

    pc_j519_build_motion(g_buf, sizeof(g_buf), &m);
    TEST_ASSERT_FALSE(pc_j519_parse_motion(g_buf, 64, NULL));
}

// test_null_guards and test_builders_reject_short_capacity only exercise the null-buf and
// short-cap branches of each `!buf || ... || cap < LEN` guard for start/motion. The remaining
// builders/parsers share the same OR-chain shape but never got a null-buf / null-payload /
// null-out case, so those branches were never taken: cover exactly those here.
void test_remaining_null_guard_branches(void)
{
    J519Request q;
    memset(&q, 0, sizeof(q));
    q.version_no = 1;

    J519RobotStatus s;
    make_status(&s);

    J519Ack a;
    make_ack(&a);

    // builders: null buf (buf is checked before the payload pointer or cap)
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_stop(NULL, 64, 1));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_request(NULL, 64, &q));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_status(NULL, 200, &s));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_ack(NULL, 200, &a));

    // builders: valid buf but null payload pointer
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_request(g_buf, sizeof(g_buf), NULL));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_status(g_buf, sizeof(g_buf), NULL));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)pc_j519_build_ack(g_buf, sizeof(g_buf), NULL));

    // parsers: null out-param - `!out` short-circuits before hdr_ok touches buf/len at all
    TEST_ASSERT_FALSE(pc_j519_parse_request(g_buf, PC_J519_LEN_REQUEST, NULL));
    TEST_ASSERT_FALSE(pc_j519_parse_status(g_buf, PC_J519_LEN_STATUS, NULL));
    TEST_ASSERT_FALSE(pc_j519_parse_ack(g_buf, PC_J519_LEN_ACK, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_start_and_stop_exact_bytes);
    RUN_TEST(test_peek_reads_type_and_version);
    RUN_TEST(test_build_motion_exact_field_offsets);
    RUN_TEST(test_motion_roundtrip);
    RUN_TEST(test_build_status_exact_field_offsets);
    RUN_TEST(test_status_roundtrip);
    RUN_TEST(test_request_roundtrip_and_bytes);
    RUN_TEST(test_ack_roundtrip_and_table_offsets);
    RUN_TEST(test_shared_type_codes_are_separated_by_length);
    RUN_TEST(test_parsers_reject_wrong_type);
    RUN_TEST(test_parsers_reject_off_by_one_lengths);
    RUN_TEST(test_builders_reject_short_capacity);
    RUN_TEST(test_null_guards);
    RUN_TEST(test_remaining_null_guard_branches);
    return UNITY_END();
}
