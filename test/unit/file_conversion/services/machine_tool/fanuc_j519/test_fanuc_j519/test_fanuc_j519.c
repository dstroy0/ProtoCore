// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the FANUC Stream Motion (J519) UDP codec (services/machine_tool/fanuc_j519.h).
//
// FANUC publishes no open specification for J519. The only public description of the wire format is
// the reference fanuc_j519.h:48-51 itself names - the Wireshark dissector
// packet-fanuc-stream-motion-j519.lua v0.1.1 (G.A. vd. Hoorn, GPL-2.0,
// github.com/fanuc-stream-motion/packet-fanuc-stream-motion-j519). Every field offset, packet size,
// type code, enumeration and status bit below is read out of that file, with the dissector's own
// line cited in the case. Two independent clients from the same author corroborate the layout and
// are cited where they add evidence:
//   fanuc_j519_utils/scripts/start_robot_state (Apache-2.0, talks to a real controller)
//   J4nn1K/fanuc-stream-motion src/utils.py    (explainRobData / commandpack)
//
// FAILING BY DESIGN - THE CODEC'S BYTE ORDER CONTRADICTS ITS OWN CITED REFERENCE.
// test_header_words_are_big_endian, test_body_integers_are_big_endian and
// test_axis_values_are_big_endian_binary32 assert big-endian and currently fail. All three sources
// read the wire big-endian and none reads it little-endian:
//   dissector:310  extract_pkt_type -> buf((offset+0),4):uint(), and Wireshark's TvbRange:uint() is
//                  documented "Get a Big Endian (network order) unsigned integer" (le_uint() is the
//                  little-endian call and appears nowhere in the file)
//   dissector:568  hdr_tree:add(fields.packet_type, buf(offset_,4)) - TreeItem:add() is documented
//                  "treated as a Big Endian (network order) value" (add_le() appears nowhere)
//   dissector:334  jbuf:float() for every axis word - TvbRange:float() is documented "Big Endian
//                  (network order)" (le_float() appears nowhere)
//   start_robot_state:29  struct.pack('>2I', J519_PKT_START, J519_VERSION) - '>' is big-endian
//   utils.py:20-126,136-178  every unpack/pack in explainRobData and commandpack uses '>I','>H','>B','>f'
// fanuc_j519.h:16 states the opposite ("every multi-octet field is LITTLE-endian") and fanuc_j519.c
// implements it with endian.wr32le / rd32le / wr16le / rd16le and wr_f32le / rd_f32le. The header's
// claim has no source; the three failing cases carry the source. A controller-facing fix is the six
// helpers in fanuc_j519.c switched to the be forms plus the sentence at fanuc_j519.h:16.
//
// PROPERTIES, NO PUBLISHED VALUE: the round-trip, residue, refusal and bounds cases. They hold for
// any byte order and are the reason the rest of the suite still means something while the order is
// wrong.

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
static uint8_t g_alt[256];

// dissector:49-57 states each packet length as a sum of its own fields:
//   SZ_HEADER      = 8
//   SZ_START_PKT   = SZ_HEADER                                                            =   8
//   SZ_STOP_PKT    = SZ_HEADER                                                            =   8
//   SZ_CMD_PKT     = 8 + (4 + (2*1) + (2*2) + (2*1) + (4*2) + (9*4)) = 8 + (4+2+4+2+8+36) =  64
//   SZ_REQUEST_PKT = 8 + (2*4)                                       = 8 + 8              =  16
//   SZ_STATE_PKT   = 8 + (4 + (2*1) + (3*2) + 4 + (9*4)*3)           = 8 + (4+2+6+4+108)  = 132
//   SZ_ACK_PKT     = 8 + ((4*4) + (20*4) + (20*4))                   = 8 + (16+80+80)     = 184
void test_packet_lengths(void)
{
    TEST_ASSERT_EQUAL_UINT(8u, (size_t)PROTOCORE_J519_LEN_START);
    TEST_ASSERT_EQUAL_UINT(8u, (size_t)PROTOCORE_J519_LEN_STOP);
    TEST_ASSERT_EQUAL_UINT(8u + 4u + 2u + 4u + 2u + 8u + 36u, (size_t)PROTOCORE_J519_LEN_MOTION);
    TEST_ASSERT_EQUAL_UINT(8u + 8u, (size_t)PROTOCORE_J519_LEN_REQUEST);
    TEST_ASSERT_EQUAL_UINT(8u + 4u + 2u + 6u + 4u + 108u, (size_t)PROTOCORE_J519_LEN_STATUS);
    TEST_ASSERT_EQUAL_UINT(8u + 16u + 80u + 80u, (size_t)PROTOCORE_J519_LEN_ACK);

    TEST_ASSERT_EQUAL_UINT(64u, (size_t)PROTOCORE_J519_LEN_MOTION);
    TEST_ASSERT_EQUAL_UINT(132u, (size_t)PROTOCORE_J519_LEN_STATUS);
    TEST_ASSERT_EQUAL_UINT(184u, (size_t)PROTOCORE_J519_LEN_ACK);
}

// dissector:41 DEFAULT_J519_PORT = 60015; :46 MAX_NUM_AXES = 9, the count disf_pos_data is called
// with for every position block; :348 num_elem = 20 inside disf_threshold_data.
void test_udp_port_and_block_counts(void)
{
    TEST_ASSERT_EQUAL_INT(60015, PROTOCORE_J519_UDP_PORT);
    TEST_ASSERT_EQUAL_INT(9, PROTOCORE_J519_AXES);
    TEST_ASSERT_EQUAL_INT(20, PROTOCORE_J519_THRESHOLDS);
}

// dissector:61-68: PC -> robot START 0, CMD 1, STOP 2, REQUEST 3; robot -> PC STATE 0, ACK 3.
void test_packet_type_codes(void)
{
    TEST_ASSERT_EQUAL_INT(0, J519_START_OR_STATUS);
    TEST_ASSERT_EQUAL_INT(1, J519_MOTION);
    TEST_ASSERT_EQUAL_INT(2, J519_STOP);
    TEST_ASSERT_EQUAL_INT(3, J519_REQUEST_OR_ACK);
}

// dissector:97-98: CMD_DATA_STYLE_CARTESIAN = 0, CMD_DATA_STYLE_JOINT = 1.
void test_data_style_codes(void)
{
    TEST_ASSERT_EQUAL_INT(0, J519_STYLE_CARTESIAN);
    TEST_ASSERT_EQUAL_INT(1, J519_STYLE_JOINT);
}

// dissector:80-93 for the named types, :126 for io_type_str[0] = "None".
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

// dissector:102-104: REQ_THRESHOLD_VEL 0 (deg/s), _ACC 1 (deg/s^2), _JRK 2 (deg/s^3).
void test_threshold_type_codes(void)
{
    TEST_ASSERT_EQUAL_INT(0, J519_THR_VELOCITY);
    TEST_ASSERT_EQUAL_INT(1, J519_THR_ACCELERATION);
    TEST_ASSERT_EQUAL_INT(2, J519_THR_JERK);
}

// dissector:71-76 publishes bit POSITIONS 0..3 ("NOTE: bit positions, not masks") and turns each into
// a mask at :241-244 with bit.lshift(1, pos). The same arithmetic:
//   CMD_READY     pos 0 -> 1 << 0 = 0x01
//   CMD_READY_ACK pos 1 -> 1 << 1 = 0x02
//   SYSRDY_ON     pos 2 -> 1 << 2 = 0x04
//   IN_MOTION     pos 3 -> 1 << 3 = 0x08
// utils.py:182-185 masks the same octet with 0b0001, 0b0010, 0b0100, 0b1000.
void test_status_bit_masks(void)
{
    TEST_ASSERT_EQUAL_HEX8(1u << 0, J519_STATUS_READY);
    TEST_ASSERT_EQUAL_HEX8(1u << 1, J519_STATUS_CMD_RECEIVED);
    TEST_ASSERT_EQUAL_HEX8(1u << 2, J519_STATUS_SYSRDY);
    TEST_ASSERT_EQUAL_HEX8(1u << 3, J519_STATUS_IN_MOTION);
    TEST_ASSERT_EQUAL_HEX8(0x0Fu,
                           J519_STATUS_READY | J519_STATUS_CMD_RECEIVED | J519_STATUS_SYSRDY | J519_STATUS_IN_MOTION);
}

// Fill a motion command with distinct values so any field landing at the wrong offset shows up.
static void fill_motion(J519MotionCommand *cmd)
{
    memset(cmd, 0, sizeof(*cmd));
    cmd->version_no = 1u;
    cmd->sequence_no = 0x11223344u;
    cmd->last_data = 0x5A;
    cmd->read_io_type = J519_IO_DI;
    cmd->read_io_index = 0x0102;
    cmd->read_io_mask = 0x0304;
    cmd->data_style = J519_STYLE_JOINT;
    cmd->write_io_type = J519_IO_RO;
    cmd->write_io_index = 0x0506;
    cmd->write_io_mask = 0x0708;
    cmd->write_io_value = 0x090A;
    for (int i = 0; i < PROTOCORE_J519_AXES; i++)
    {
        cmd->joint_data[i] = (float)(i + 1);
    }
}

static void fill_status(J519RobotStatus *st)
{
    memset(st, 0, sizeof(*st));
    st->version_no = 1u;
    st->sequence_no = 0x01020304u;
    st->status = J519_STATUS_READY | J519_STATUS_SYSRDY;
    st->read_io_type = J519_IO_UI;
    st->read_io_index = 0x1111;
    st->read_io_mask = 0x2222;
    st->read_io_value = 0x3333;
    st->time_stamp = 0x44556677u;
    for (int i = 0; i < PROTOCORE_J519_AXES; i++)
    {
        st->cartesian_pose[i] = (float)(100 + i);
        st->joint_pose[i] = (float)(200 + i);
        st->motor_current[i] = (float)(300 + i);
    }
}

// disf_p2r_cmd_pkt (dissector:383-428) walks the body from the end of the 8-octet header:
//   +0  sequence_no    4   -> 8..11
//   +4  last_data      1   -> 12
//   +5  read_io_type   1   -> 13
//   +6  read_io_index  2   -> 14..15
//   +8  read_io_mask   2   -> 16..17
//   +10 data_style     1   -> 18
//   +11 write_io_type  1   -> 19
//   +12 write_io_index 2   -> 20..21
//   +14 write_io_mask  2   -> 22..23
//   +16 write_io_value 2   -> 24..25
//   +18 cmd.unused     2   -> 26..27
//   +20 joint data   9*4   -> 28..63
// The single-octet fields pin those offsets without depending on byte order.
void test_motion_octet_field_offsets(void)
{
    J519MotionCommand cmd;
    fill_motion(&cmd);

    memset(g_buf, 0xEE, sizeof(g_buf));
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));

    TEST_ASSERT_EQUAL_HEX8(0x5A, g_buf[12]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)J519_IO_DI, g_buf[13]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)J519_STYLE_JOINT, g_buf[18]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)J519_IO_RO, g_buf[19]);
    TEST_ASSERT_EQUAL_HEX8(0xEE, g_buf[64]);

    cmd.data_style = J519_STYLE_CARTESIAN;
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));
    TEST_ASSERT_EQUAL_HEX8(0x00, g_buf[18]);
}

// disf_r2p_state_pkt (dissector:440-485), same walk from the end of the header:
//   +0  sequence_no    4   -> 8..11
//   +4  status         1   -> 12
//   +5  read_io_type   1   -> 13
//   +6  read_io_index  2   -> 14..15
//   +8  read_io_mask   2   -> 16..17
//   +10 read_io_value  2   -> 18..19
//   +12 time_stamp     4   -> 20..23
//   +16 cartesian    9*4   -> 24..59
//   +52 joint pose   9*4   -> 60..95
//   +88 motor current 9*4  -> 96..131
// utils.py explainRobData slices the identical windows (data[12:13] status, data[13:14] read io type,
// data[20:24] time stamp, data[24:28] X, data[60:64] J1, data[96:100] J1 motor current).
void test_status_octet_field_offsets(void)
{
    J519RobotStatus st;
    fill_status(&st);

    memset(g_buf, 0xEE, sizeof(g_buf));
    TEST_ASSERT_EQUAL_UINT(132u, protocore_j519_build_status(g_buf, sizeof(g_buf), &st));
    TEST_ASSERT_EQUAL_HEX8(J519_STATUS_READY | J519_STATUS_SYSRDY, g_buf[12]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)J519_IO_UI, g_buf[13]);
    TEST_ASSERT_EQUAL_HEX8(0xEE, g_buf[132]);
}

// FAILING BY DESIGN. dissector:310 reads the type word with TvbRange:uint() and :568 adds both header
// words with TreeItem:add(); both are documented big-endian, and the little-endian calls (le_uint,
// add_le) appear nowhere in the file. start_robot_state:29 packs the same two words '>2I'.
// So a Start carrying version 0x04030201 is
//   type    0x00000000 -> 00 00 00 00
//   version 0x04030201 -> 04 03 02 01
// fanuc_j519.c wr_header uses endian.wr32le, which emits 01 02 03 04 for the version word.
void test_header_words_are_big_endian(void)
{
    TEST_ASSERT_EQUAL_UINT(8u, protocore_j519_build_start(g_buf, sizeof(g_buf), 0x04030201u));
    static const uint8_t WANT[8] = {0x00, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, g_buf, 8);

    TEST_ASSERT_EQUAL_UINT(8u, protocore_j519_build_stop(g_buf, sizeof(g_buf), 1u));
    static const uint8_t WANT_STOP[8] = {0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT_STOP, g_buf, 8);
}

// FAILING BY DESIGN. Same reference, same reason, for the body integers: utils.py unpacks every one
// with '>I' / '>H' (sequence_no data[8:12], read io index data[14:16], time stamp data[20:24]) and
// commandpack packs them the same way.
//   Motion sequence_no   0x11223344 -> 11 22 33 44 at 8..11
//   Motion read_io_index 0x0102     -> 01 02       at 14..15
//   Status time_stamp    0x44556677 -> 44 55 66 77 at 20..23
void test_body_integers_are_big_endian(void)
{
    J519MotionCommand cmd;
    fill_motion(&cmd);
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));

    static const uint8_t SEQ[4] = {0x11, 0x22, 0x33, 0x44};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SEQ, g_buf + 8, 4);
    static const uint8_t RD_IDX[2] = {0x01, 0x02};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RD_IDX, g_buf + 14, 2);

    J519RobotStatus st;
    fill_status(&st);
    TEST_ASSERT_EQUAL_UINT(132u, protocore_j519_build_status(g_buf, sizeof(g_buf), &st));
    static const uint8_t TS[4] = {0x44, 0x55, 0x66, 0x77};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TS, g_buf + 20, 4);
}

// FAILING BY DESIGN. dissector:334 reads every axis word with TvbRange:float(), documented big-endian
// (le_float() appears nowhere); utils.py packs and unpacks each one '>f'.
// IEEE-754 binary32, from the format's own definition (sign | 8-bit exponent, bias 127 | 23-bit
// fraction), then written most significant octet first:
//   1.0f   = +1.0    * 2^0 -> exp 127 = 0x7F -> 0 01111111 000...  = 0x3F800000 -> 3F 80 00 00
//   9.0f   = +1.125  * 2^3 -> exp 130 = 0x82 -> 0 10000010 0010... = 0x41100000 -> 41 10 00 00
//   100.0f = +1.5625 * 2^6 -> exp 133 = 0x85 -> 0 10000101 1001... = 0x42C80000 -> 42 C8 00 00
// The Motion joint block starts at 28, so axis 1 (1.0f) is 28..31 and axis 9 (9.0f) is 60..63.
// The Status Cartesian block starts at 24, so its axis 1 (100.0f) is 24..27.
void test_axis_values_are_big_endian_binary32(void)
{
    J519MotionCommand cmd;
    fill_motion(&cmd);
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));

    static const uint8_t ONE_F[4] = {0x3F, 0x80, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ONE_F, g_buf + 28, 4);
    static const uint8_t NINE_F[4] = {0x41, 0x10, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NINE_F, g_buf + 60, 4);

    J519RobotStatus st;
    fill_status(&st);
    TEST_ASSERT_EQUAL_UINT(132u, protocore_j519_build_status(g_buf, sizeof(g_buf), &st));
    static const uint8_t HUNDRED_F[4] = {0x42, 0xC8, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(HUNDRED_F, g_buf + 24, 4);
}

// Property: a packet is a function of the command alone, so building the same command into buffers
// pre-filled with different patterns gives identical octets - no reserved field carries residue.
void test_builders_leave_no_residue(void)
{
    J519MotionCommand cmd;
    fill_motion(&cmd);
    memset(g_buf, 0xEE, sizeof(g_buf));
    memset(g_alt, 0x00, sizeof(g_alt));
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_alt, sizeof(g_alt), &cmd));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_buf, g_alt, 64);

    J519RobotStatus st;
    fill_status(&st);
    memset(g_buf, 0xEE, sizeof(g_buf));
    memset(g_alt, 0x00, sizeof(g_alt));
    TEST_ASSERT_EQUAL_UINT(132u, protocore_j519_build_status(g_buf, sizeof(g_buf), &st));
    TEST_ASSERT_EQUAL_UINT(132u, protocore_j519_build_status(g_alt, sizeof(g_alt), &st));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_buf, g_alt, 132);

    J519Ack ack;
    memset(&ack, 0, sizeof(ack));
    ack.axis_no = 3u;
    memset(g_buf, 0xEE, sizeof(g_buf));
    memset(g_alt, 0x00, sizeof(g_alt));
    TEST_ASSERT_EQUAL_UINT(184u, protocore_j519_build_ack(g_buf, sizeof(g_buf), &ack));
    TEST_ASSERT_EQUAL_UINT(184u, protocore_j519_build_ack(g_alt, sizeof(g_alt), &ack));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(g_buf, g_alt, 184);
}

// Property: the builder and the parser are the two halves of one codec, so every field a Motion
// carries comes back unchanged whatever order the octets are in.
void test_motion_round_trip(void)
{
    J519MotionCommand cmd;
    fill_motion(&cmd);
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));

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

// Property: the same identity for the robot -> PC Status, including all three position blocks.
void test_status_round_trip(void)
{
    J519RobotStatus st;
    fill_status(&st);
    TEST_ASSERT_EQUAL_UINT(132u, protocore_j519_build_status(g_buf, sizeof(g_buf), &st));

    J519RobotStatus back;
    memset(&back, 0xFF, sizeof(back));
    TEST_ASSERT_TRUE(protocore_j519_parse_status(g_buf, 132u, &back));
    TEST_ASSERT_EQUAL_UINT32(st.version_no, back.version_no);
    TEST_ASSERT_EQUAL_UINT32(st.sequence_no, back.sequence_no);
    TEST_ASSERT_EQUAL_UINT8(st.status, back.status);
    TEST_ASSERT_EQUAL_UINT8(st.read_io_type, back.read_io_type);
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

// Property: the Request / Ack pair round-trips too, both threshold tables and the reserved word the
// dissector labels Unknown0 (:251) included - a word with no known meaning must still survive.
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
    TEST_ASSERT_EQUAL_UINT32(req.version_no, rback.version_no);
    TEST_ASSERT_EQUAL_UINT32(req.axis_no, rback.axis_no);
    TEST_ASSERT_EQUAL_UINT32(req.threshold_type, rback.threshold_type);

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

// Property: binary32 is a bit pattern, so signed zero, the subnormal edge and the largest finite
// value must come back bit-identical, not merely numerically close.
void test_axis_values_survive_the_binary32_packing(void)
{
    static const float V[PROTOCORE_J519_AXES] = {0.0f,    -1.0f,         0.5f,     -0.5f, 1234.5f,
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

// dissector:290-307 selects a packet's length from its type AND its direction, and :637-641 works
// around the shared code 0 by testing the datagram length ("if pkt_type == PKT_TYPE_STATE_PKT and
// buf_len == SZ_START_PKT then it is a Start"). Length is therefore what separates Start from Status
// and Request from Ack, so a parser must take its own length and no other.
void test_length_separates_the_shared_type_codes(void)
{
    J519RobotStatus st;
    fill_status(&st);
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

// Property, byte order aside: the type word occupies header octets 0..3, so altering any one of them
// alters the word and the parser must refuse the packet whichever end the word is read from.
void test_parsers_check_the_type_word(void)
{
    J519Request req;
    memset(&req, 0, sizeof(req));
    J519Request rback;
    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_UINT(16u, protocore_j519_build_request(g_buf, sizeof(g_buf), &req));
        TEST_ASSERT_TRUE(protocore_j519_parse_request(g_buf, 16u, &rback));
        g_buf[i] ^= 0xFF;
        TEST_ASSERT_FALSE(protocore_j519_parse_request(g_buf, 16u, &rback));
    }

    J519MotionCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));
    J519MotionCommand mback;
    TEST_ASSERT_TRUE(protocore_j519_parse_motion(g_buf, 64u, &mback));
    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_UINT(64u, protocore_j519_build_motion(g_buf, sizeof(g_buf), &cmd));
        g_buf[i] ^= 0xFF;
        TEST_ASSERT_FALSE(protocore_j519_parse_motion(g_buf, 64u, &mback));
    }
}

// dissector:608 refuses a datagram shorter than SZ_HEADER (8). The peek is the same bound, and the
// type / version it reports round-trip through the codec's own header writer.
void test_peek_needs_a_whole_header(void)
{
    TEST_ASSERT_EQUAL_UINT(8u, protocore_j519_build_stop(g_buf, sizeof(g_buf), 7u));
    TEST_ASSERT_FALSE(protocore_j519_peek(g_buf, 7u, NULL, NULL));
    TEST_ASSERT_FALSE(protocore_j519_peek(NULL, 8u, NULL, NULL));

    uint32_t type = 0xFFFFFFFFu, ver = 0;
    TEST_ASSERT_TRUE(protocore_j519_peek(g_buf, 8u, &type, &ver));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)J519_STOP, type);
    TEST_ASSERT_EQUAL_UINT32(7u, ver);

    TEST_ASSERT_EQUAL_UINT(8u, protocore_j519_build_start(g_buf, sizeof(g_buf), 1u));
    TEST_ASSERT_TRUE(protocore_j519_peek(g_buf, 8u, &type, &ver));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)J519_START_OR_STATUS, type);
    TEST_ASSERT_EQUAL_UINT32(1u, ver);
}

// Bounds refusal: one octet short of the packet's own length writes nothing, and a missing argument
// is reported rather than dereferenced.
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
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_stop(NULL, 8u, 1u));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_motion(g_buf, sizeof(g_buf), NULL));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_status(g_buf, sizeof(g_buf), NULL));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_request(g_buf, sizeof(g_buf), NULL));
    TEST_ASSERT_EQUAL_UINT(0u, protocore_j519_build_ack(g_buf, sizeof(g_buf), NULL));
}

// The same refusal on the parse side.
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
