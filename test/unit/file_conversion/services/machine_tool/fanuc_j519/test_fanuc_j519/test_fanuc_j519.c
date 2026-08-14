// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/machine_tool/fanuc_j519/fanuc_j519.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_buf[256];

void test_packet_lengths(void)
{
    TEST_ASSERT_EQUAL_UINT(8u, (size_t)PROTOCORE_J519_LEN_START);
    TEST_ASSERT_EQUAL_UINT(8u, (size_t)PROTOCORE_J519_LEN_STOP);
    TEST_ASSERT_EQUAL_UINT(64u, (size_t)PROTOCORE_J519_LEN_MOTION);
    TEST_ASSERT_EQUAL_UINT(16u, (size_t)PROTOCORE_J519_LEN_REQUEST);
    TEST_ASSERT_EQUAL_UINT(132u, (size_t)PROTOCORE_J519_LEN_STATUS);
    TEST_ASSERT_EQUAL_UINT(184u, (size_t)PROTOCORE_J519_LEN_ACK);

    TEST_ASSERT_EQUAL_UINT(8u + 4u + 2u + 4u + 2u + 8u + 36u, (size_t)PROTOCORE_J519_LEN_MOTION);

    TEST_ASSERT_EQUAL_UINT(8u + 4u + 2u + 6u + 4u + 108u, (size_t)PROTOCORE_J519_LEN_STATUS);

    TEST_ASSERT_EQUAL_UINT(8u + 16u + 160u, (size_t)PROTOCORE_J519_LEN_ACK);

    TEST_ASSERT_EQUAL_INT(9, PROTOCORE_J519_AXES);
    TEST_ASSERT_EQUAL_INT(20, PROTOCORE_J519_THRESHOLDS);
    TEST_ASSERT_EQUAL_INT(60015, PROTOCORE_J519_UDP_PORT);
}

void test_packet_type_codes(void)
{
    TEST_ASSERT_EQUAL_INT(0, J519_START_OR_STATUS);
    TEST_ASSERT_EQUAL_INT(1, J519_MOTION);
    TEST_ASSERT_EQUAL_INT(2, J519_STOP);
    TEST_ASSERT_EQUAL_INT(3, J519_REQUEST_OR_ACK);
}

void test_start_and_stop_are_header_only(void)
{
    memset(g_buf, 0xAA, sizeof(g_buf));
    TEST_ASSERT_EQUAL_UINT(8u, protocore_j519_build_start(g_buf, sizeof(g_buf), 1u));
    uint32_t type = 0xFFFFFFFFu, ver = 0;
    TEST_ASSERT_TRUE(protocore_j519_peek(g_buf, 8u, &type, &ver));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)J519_START_OR_STATUS, type);
    TEST_ASSERT_EQUAL_UINT32(1u, ver);
    TEST_ASSERT_EQUAL_HEX8(0xAA, g_buf[8]);

    TEST_ASSERT_EQUAL_UINT(8u, protocore_j519_build_stop(g_buf, sizeof(g_buf), 2u));
    TEST_ASSERT_TRUE(protocore_j519_peek(g_buf, 8u, &type, &ver));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)J519_STOP, type);
    TEST_ASSERT_EQUAL_UINT32(2u, ver);
}

void test_header_byte_order(void)
{
    TEST_ASSERT_EQUAL_UINT(8u, protocore_j519_build_start(g_buf, sizeof(g_buf), 0x04030201u));
    static const uint8_t WANT[8] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_buf, 8);
}

void test_peek_needs_a_whole_header(void)
{
    (void)protocore_j519_build_stop(g_buf, sizeof(g_buf), 7u);
    TEST_ASSERT_FALSE(protocore_j519_peek(g_buf, 7u, NULL, NULL));
    TEST_ASSERT_FALSE(protocore_j519_peek(NULL, 8u, NULL, NULL));
    TEST_ASSERT_TRUE(protocore_j519_peek(g_buf, 8u, NULL, NULL));
}

void test_motion_field_offsets(void)
{
    J519MotionCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.version_no = 1u;
    cmd.sequence_no = 0x11223344u;
    cmd.last_data = 0x5A;
    cmd.read_io_type = J519_IO_DI;
    cmd.read_io_index = 0x0102;
    cmd.read_io_mask = 0x0304;
    cmd.data_style = J519_STYLE_JOINT;
    cmd.write_io_type = J519_IO_RO;
    cmd.write_io_index = 0x0506;
    cmd.write_io_mask = 0x0708;
    cmd.write_io_value = 0x090A;
    for (int i = 0; i < PROTOCORE_J519_AXES; i++)
    {
        cmd.joint_data[i] = (float)(i + 1);
    }

    memset(g_buf, 0xEE, sizeof(g_buf));
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));

    TEST_ASSERT_EQUAL_HEX8(0x5A, g_buf[12]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)J519_IO_DI, g_buf[13]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)J519_STYLE_JOINT, g_buf[18]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)J519_IO_RO, g_buf[19]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[26]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[27]);
    TEST_ASSERT_EQUAL_HEX8(0xEE, g_buf[64]);

    static const uint8_t ONE_F[4] = {0x00, 0x00, 0x80, 0x3F};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ONE_F, g_buf + 28, 4);

    static const uint8_t NINE_F[4] = {0x00, 0x00, 0x10, 0x41};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NINE_F, g_buf + 60, 4);

    J519MotionCommand back;
    memset(&back, 0xFF, sizeof(back));
    TEST_ASSERT_TRUE(protocore_j519_parse_motion(g_buf, 64u, &back));
    TEST_ASSERT_EQUAL_UINT32(cmd.version_no, back.version_no);
    TEST_ASSERT_EQUAL_UINT32(cmd.sequence_no, back.sequence_no);
    TEST_ASSERT_EQUAL_UINT8(cmd.last_data, back.last_data);
    TEST_ASSERT_EQUAL_UINT8(cmd.read_io_type, back.read_io_type);
    TEST_ASSERT_EQUAL_UINT16(cmd.read_io_index, back.read_io_index);
    TEST_ASSERT_EQUAL_UINT16(cmd.read_io_mask, back.read_io_mask);
    TEST_ASSERT_EQUAL_UINT8(cmd.data_style, back.data_style);
    TEST_ASSERT_EQUAL_UINT8(cmd.write_io_type, back.write_io_type);
    TEST_ASSERT_EQUAL_UINT16(cmd.write_io_index, back.write_io_index);
    TEST_ASSERT_EQUAL_UINT16(cmd.write_io_mask, back.write_io_mask);
    TEST_ASSERT_EQUAL_UINT16(cmd.write_io_value, back.write_io_value);
    for (int i = 0; i < PROTOCORE_J519_AXES; i++)
    {
        TEST_ASSERT_EQUAL_FLOAT(cmd.joint_data[i], back.joint_data[i]);
    }
}

void test_status_field_offsets(void)
{
    J519RobotStatus st;
    memset(&st, 0, sizeof(st));
    st.version_no = 1u;
    st.sequence_no = 0x01020304u;
    st.status = J519_STATUS_READY | J519_STATUS_SYSRDY;
    st.read_io_type = J519_IO_UI;
    st.read_io_index = 0x1111;
    st.read_io_mask = 0x2222;
    st.read_io_value = 0x3333;
    st.time_stamp = 0x44556677u;
    for (int i = 0; i < PROTOCORE_J519_AXES; i++)
    {
        st.cartesian_pose[i] = (float)(100 + i);
        st.joint_pose[i] = (float)(200 + i);
        st.motor_current[i] = (float)(300 + i);
    }

    memset(g_buf, 0xEE, sizeof(g_buf));
    TEST_ASSERT_EQUAL_UINT(132u, protocore_j519_build_status(g_buf, sizeof(g_buf), &st));
    TEST_ASSERT_EQUAL_HEX8(J519_STATUS_READY | J519_STATUS_SYSRDY, g_buf[12]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)J519_IO_UI, g_buf[13]);
    TEST_ASSERT_EQUAL_HEX8(0xEE, g_buf[132]);

    J519RobotStatus back;
    memset(&back, 0xFF, sizeof(back));
    TEST_ASSERT_TRUE(protocore_j519_parse_status(g_buf, 132u, &back));
    TEST_ASSERT_EQUAL_UINT32(st.sequence_no, back.sequence_no);
    TEST_ASSERT_EQUAL_UINT8(st.status, back.status);
    TEST_ASSERT_EQUAL_UINT16(st.read_io_index, back.read_io_index);
    TEST_ASSERT_EQUAL_UINT16(st.read_io_mask, back.read_io_mask);
    TEST_ASSERT_EQUAL_UINT16(st.read_io_value, back.read_io_value);
    TEST_ASSERT_EQUAL_UINT32(st.time_stamp, back.time_stamp);
    for (int i = 0; i < PROTOCORE_J519_AXES; i++)
    {
        TEST_ASSERT_EQUAL_FLOAT(st.cartesian_pose[i], back.cartesian_pose[i]);
        TEST_ASSERT_EQUAL_FLOAT(st.joint_pose[i], back.joint_pose[i]);
        TEST_ASSERT_EQUAL_FLOAT(st.motor_current[i], back.motor_current[i]);
    }
}

void test_status_bits(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01, J519_STATUS_READY);
    TEST_ASSERT_EQUAL_HEX8(0x02, J519_STATUS_CMD_RECEIVED);
    TEST_ASSERT_EQUAL_HEX8(0x04, J519_STATUS_SYSRDY);
    TEST_ASSERT_EQUAL_HEX8(0x08, J519_STATUS_IN_MOTION);

    J519RobotStatus st;
    memset(&st, 0, sizeof(st));
    st.status = J519_STATUS_READY | J519_STATUS_CMD_RECEIVED | J519_STATUS_SYSRDY | J519_STATUS_IN_MOTION;
    TEST_ASSERT_EQUAL_UINT(132u, protocore_j519_build_status(g_buf, sizeof(g_buf), &st));

    J519RobotStatus back;
    TEST_ASSERT_TRUE(protocore_j519_parse_status(g_buf, 132u, &back));
    TEST_ASSERT_EQUAL_HEX8(0x0F, back.status);
}

void test_request_and_ack_round_trip(void)
{
    J519Request req;
    memset(&req, 0, sizeof(req));
    req.version_no = 1u;
    req.axis_no = 3u;
    req.threshold_type = J519_THR_ACCELERATION;

    TEST_ASSERT_EQUAL_UINT(16u, protocore_j519_build_request(g_buf, sizeof(g_buf), &req));
    J519Request rback;
    memset(&rback, 0xFF, sizeof(rback));
    TEST_ASSERT_TRUE(protocore_j519_parse_request(g_buf, 16u, &rback));
    TEST_ASSERT_EQUAL_UINT32(1u, rback.version_no);
    TEST_ASSERT_EQUAL_UINT32(3u, rback.axis_no);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)J519_THR_ACCELERATION, rback.threshold_type);

    J519Ack ack;
    memset(&ack, 0, sizeof(ack));
    ack.version_no = 1u;
    ack.axis_no = 3u;
    ack.threshold_type = J519_THR_ACCELERATION;
    ack.max_cart_speed = 2000u;
    ack.unknown0 = 0xDEADBEEFu;
    for (int i = 0; i < PROTOCORE_J519_THRESHOLDS; i++)
    {

        ack.threshold_no_load[i] = (float)((i + 1) * 5);
        ack.threshold_max_load[i] = (float)((i + 1) * 5) / 2.0f;
    }

    memset(g_buf, 0xEE, sizeof(g_buf));
    TEST_ASSERT_EQUAL_UINT(184u, protocore_j519_build_ack(g_buf, sizeof(g_buf), &ack));
    TEST_ASSERT_EQUAL_HEX8(0xEE, g_buf[184]);

    J519Ack aback;
    memset(&aback, 0xFF, sizeof(aback));
    TEST_ASSERT_TRUE(protocore_j519_parse_ack(g_buf, 184u, &aback));
    TEST_ASSERT_EQUAL_UINT32(ack.axis_no, aback.axis_no);
    TEST_ASSERT_EQUAL_UINT32(ack.threshold_type, aback.threshold_type);
    TEST_ASSERT_EQUAL_UINT32(ack.max_cart_speed, aback.max_cart_speed);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, aback.unknown0);
    for (int i = 0; i < PROTOCORE_J519_THRESHOLDS; i++)
    {
        TEST_ASSERT_EQUAL_FLOAT(ack.threshold_no_load[i], aback.threshold_no_load[i]);
        TEST_ASSERT_EQUAL_FLOAT(ack.threshold_max_load[i], aback.threshold_max_load[i]);
    }
}

void test_threshold_types(void)
{
    TEST_ASSERT_EQUAL_INT(0, J519_THR_VELOCITY);
    TEST_ASSERT_EQUAL_INT(1, J519_THR_ACCELERATION);
    TEST_ASSERT_EQUAL_INT(2, J519_THR_JERK);
}

void test_io_type_codes(void)
{
    TEST_ASSERT_EQUAL_INT(0, J519_IO_NONE);
    TEST_ASSERT_EQUAL_INT(1, J519_IO_DI);
    TEST_ASSERT_EQUAL_INT(2, J519_IO_DO);
    TEST_ASSERT_EQUAL_INT(8, J519_IO_RI);
    TEST_ASSERT_EQUAL_INT(9, J519_IO_RO);
    TEST_ASSERT_EQUAL_INT(11, J519_IO_SI);
    TEST_ASSERT_EQUAL_INT(12, J519_IO_SO);
    TEST_ASSERT_EQUAL_INT(16, J519_IO_WI);
    TEST_ASSERT_EQUAL_INT(17, J519_IO_WO);
    TEST_ASSERT_EQUAL_INT(20, J519_IO_UI);
    TEST_ASSERT_EQUAL_INT(21, J519_IO_UO);
    TEST_ASSERT_EQUAL_INT(26, J519_IO_WSI);
    TEST_ASSERT_EQUAL_INT(27, J519_IO_WSO);
    TEST_ASSERT_EQUAL_INT(35, J519_IO_F);
    TEST_ASSERT_EQUAL_INT(36, J519_IO_M);
}

void test_data_style(void)
{
    TEST_ASSERT_EQUAL_INT(0, J519_STYLE_CARTESIAN);
    TEST_ASSERT_EQUAL_INT(1, J519_STYLE_JOINT);

    J519MotionCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.data_style = J519_STYLE_CARTESIAN;
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[18]);

    cmd.data_style = J519_STYLE_JOINT;
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));
    TEST_ASSERT_EQUAL_HEX8(0x01, g_buf[18]);
}

void test_length_separates_the_shared_type_codes(void)
{
    J519RobotStatus st;
    memset(&st, 0, sizeof(st));
    st.version_no = 1u;
    TEST_ASSERT_EQUAL_UINT(132u, protocore_j519_build_status(g_buf, sizeof(g_buf), &st));

    J519RobotStatus sback;
    TEST_ASSERT_TRUE(protocore_j519_parse_status(g_buf, 132u, &sback));
    TEST_ASSERT_FALSE(protocore_j519_parse_status(g_buf, 131u, &sback));
    TEST_ASSERT_FALSE(protocore_j519_parse_status(g_buf, 133u, &sback));

    TEST_ASSERT_EQUAL_UINT(8u, protocore_j519_build_start(g_buf, sizeof(g_buf), 1u));
    TEST_ASSERT_FALSE(protocore_j519_parse_status(g_buf, 8u, &sback));

    J519Ack ack;
    memset(&ack, 0, sizeof(ack));
    J519Ack aback;
    TEST_ASSERT_EQUAL_UINT(184u, protocore_j519_build_ack(g_buf, sizeof(g_buf), &ack));
    TEST_ASSERT_TRUE(protocore_j519_parse_ack(g_buf, 184u, &aback));
    TEST_ASSERT_FALSE(protocore_j519_parse_ack(g_buf, 183u, &aback));

    J519Request req;
    memset(&req, 0, sizeof(req));
    J519Request rback;
    TEST_ASSERT_EQUAL_UINT(16u, protocore_j519_build_request(g_buf, sizeof(g_buf), &req));
    TEST_ASSERT_TRUE(protocore_j519_parse_request(g_buf, 16u, &rback));
    TEST_ASSERT_FALSE(protocore_j519_parse_ack(g_buf, 16u, &aback));
}

void test_parsers_check_the_type_word(void)
{
    J519MotionCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));

    J519Request rback;
    TEST_ASSERT_FALSE(protocore_j519_parse_request(g_buf, 64u, &rback));

    TEST_ASSERT_EQUAL_UINT(8u, protocore_j519_build_stop(g_buf, sizeof(g_buf), 1u));
    J519MotionCommand mback;
    TEST_ASSERT_FALSE(protocore_j519_parse_motion(g_buf, 8u, &mback));

    J519Request req;
    memset(&req, 0, sizeof(req));
    TEST_ASSERT_EQUAL_UINT(16u, protocore_j519_build_request(g_buf, sizeof(g_buf), &req));
    g_buf[0] = (uint8_t)J519_MOTION;
    TEST_ASSERT_FALSE(protocore_j519_parse_request(g_buf, 16u, &rback));
}

void test_builders_refuse_a_short_buffer(void)
{
    J519MotionCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    J519RobotStatus st;
    memset(&st, 0, sizeof(st));
    J519Request req;
    memset(&req, 0, sizeof(req));
    J519Ack ack;
    memset(&ack, 0, sizeof(ack));

    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_start(g_buf, 7u, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_stop(g_buf, 7u, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_motion(g_buf, 63u, &cmd));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_request(g_buf, 15u, &req));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_status(g_buf, 131u, &st));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_ack(g_buf, 183u, &ack));

    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_start(NULL, 8u, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_motion(g_buf, sizeof(g_buf), NULL));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_status(g_buf, sizeof(g_buf), NULL));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_request(g_buf, sizeof(g_buf), NULL));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_ack(g_buf, sizeof(g_buf), NULL));
}

void test_parsers_refuse_missing_arguments(void)
{
    J519MotionCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));

    TEST_ASSERT_FALSE(protocore_j519_parse_motion(g_buf, 64u, NULL));
    TEST_ASSERT_FALSE(protocore_j519_parse_motion(NULL, 64u, &cmd));

    J519RobotStatus st;
    TEST_ASSERT_FALSE(protocore_j519_parse_status(NULL, 132u, &st));
    J519Request req;
    TEST_ASSERT_FALSE(protocore_j519_parse_request(NULL, 16u, &req));
    J519Ack ack;
    TEST_ASSERT_FALSE(protocore_j519_parse_ack(NULL, 184u, &ack));
}

void test_axis_values_survive_the_binary32_packing(void)
{
    static const float V[PROTOCORE_J519_AXES] = {0.0f,   -1.0f,       0.5f,      -0.5f, 1234.5f,
                                                 -0.125f, 3.4028235e38f, 1.5e-38f, -0.0f};
    J519MotionCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    memcpy(cmd.joint_data, V, sizeof(V));
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));

    J519MotionCommand back;
    TEST_ASSERT_TRUE(protocore_j519_parse_motion(g_buf, 64u, &back));
    for (int i = 0; i < PROTOCORE_J519_AXES; i++)
    {
        TEST_ASSERT_EQUAL_MEMORY(&V[i], &back.joint_data[i], sizeof(float));
    }
}
