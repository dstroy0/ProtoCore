// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the CiA 402 drive profile (services/fieldbus/cia402): the Statusword state decode, the
// Controlword commands + enable sequence, Statusword flag accessors, the CANopen SDO setters,
// and the cyclic PDO pack/unpack. State masks/values per IEC 61800-7-201. Pure host tests.

#include "services/fieldbus/cia402/cia402.h"
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// Build an SDO server upload/abort response frame (0x580+node, dlc 8) with a given command byte,
// echoed object index (sub 0), and up to 4 expedited payload octets (little-endian).
static CanFrame make_sdo_tx(uint8_t node, uint8_t cmd, uint16_t index, uint32_t payload)
{
    CanFrame f = {0};
    f.id = 0x580u + node;
    f.extended = PROTO_FALSE;
    f.rtr = PROTO_FALSE;
    f.dlc = 8;
    f.data[0] = cmd;
    f.data[1] = (uint8_t)index;
    f.data[2] = (uint8_t)(index >> 8);
    f.data[3] = 0;
    f.data[4] = (uint8_t)payload;
    f.data[5] = (uint8_t)(payload >> 8);
    f.data[6] = (uint8_t)(payload >> 16);
    f.data[7] = (uint8_t)(payload >> 24);
    return f;
}

void test_state_decode()
{
    TEST_ASSERT_TRUE(pc_cia402_state(0x0000) == not_ready_to_switch_on);
    TEST_ASSERT_TRUE(pc_cia402_state(0x0040) == switch_on_disabled);
    TEST_ASSERT_TRUE(pc_cia402_state(0x0021) == ready_to_switch_on);
    TEST_ASSERT_TRUE(pc_cia402_state(0x0023) == switched_on);
    TEST_ASSERT_TRUE(pc_cia402_state(0x0027) == operation_enabled);
    TEST_ASSERT_TRUE(pc_cia402_state(0x0007) == quick_stop_active);
    TEST_ASSERT_TRUE(pc_cia402_state(0x000F) == fault_reaction_active);
    TEST_ASSERT_TRUE(pc_cia402_state(0x0008) == fault);
}

void test_state_decode_ignores_high_bits()
{
    // The upper Statusword bits (voltage, remote, target reached, warning, ...) must not change
    // the decoded power state - only the masked bits do.
    TEST_ASSERT_TRUE(pc_cia402_state(0x0637) == operation_enabled); // 0x27 + remote + target
    TEST_ASSERT_TRUE(pc_cia402_state(0x1237) == operation_enabled);
    TEST_ASSERT_TRUE(pc_cia402_state(0x0233) == switched_on);
}

void test_controlword_commands()
{
    TEST_ASSERT_EQUAL_HEX16(0x0006, pc_cia402_controlword(shutdown));
    TEST_ASSERT_EQUAL_HEX16(0x0007, pc_cia402_controlword(switch_on));
    TEST_ASSERT_EQUAL_HEX16(0x0007, pc_cia402_controlword(disable_operation));
    TEST_ASSERT_EQUAL_HEX16(0x000F, pc_cia402_controlword(enable_operation));
    TEST_ASSERT_EQUAL_HEX16(0x0000, pc_cia402_controlword(disable_voltage));
    TEST_ASSERT_EQUAL_HEX16(0x0002, pc_cia402_controlword(quick_stop));
    TEST_ASSERT_EQUAL_HEX16(0x0080, pc_cia402_controlword(fault_reset));
}

void test_enable_sequence()
{
    TEST_ASSERT_EQUAL_HEX16(0x0080, pc_cia402_enable_sequence(fault));
    TEST_ASSERT_EQUAL_HEX16(0x0080, pc_cia402_enable_sequence(fault_reaction_active));
    TEST_ASSERT_EQUAL_HEX16(0x0006, pc_cia402_enable_sequence(switch_on_disabled));
    TEST_ASSERT_EQUAL_HEX16(0x0007, pc_cia402_enable_sequence(ready_to_switch_on));
    TEST_ASSERT_EQUAL_HEX16(0x000F, pc_cia402_enable_sequence(switched_on));
    TEST_ASSERT_EQUAL_HEX16(0x000F, pc_cia402_enable_sequence(operation_enabled));
    TEST_ASSERT_EQUAL_HEX16(0x0000, pc_cia402_enable_sequence(not_ready_to_switch_on));
}

void test_statusword_flags()
{
    TEST_ASSERT_TRUE(pc_cia402_target_reached(0x0400));
    TEST_ASSERT_FALSE(pc_cia402_target_reached(0x0000));
    TEST_ASSERT_TRUE(pc_cia402_has_fault(0x0008));
    TEST_ASSERT_TRUE(pc_cia402_warning(0x0080));
    TEST_ASSERT_TRUE(pc_cia402_voltage_enabled(0x0010));
    TEST_ASSERT_TRUE(pc_cia402_remote(0x0200));
    TEST_ASSERT_TRUE(pc_cia402_internal_limit(0x0800));
}

void test_sdo_set_controlword()
{
    CanFrame f;
    TEST_ASSERT_TRUE(pc_cia402_sdo_set_controlword(&f, 5, 0x000F));
    TEST_ASSERT_EQUAL_HEX32(0x605u, f.id);   // SDO_RX (0x600) + node 5
    TEST_ASSERT_EQUAL_HEX8(0x40, f.data[1]); // index 0x6040 LE
    TEST_ASSERT_EQUAL_HEX8(0x60, f.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[3]); // sub 0
    TEST_ASSERT_EQUAL_HEX8(0x0F, f.data[4]); // value 0x000F LE
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[5]);
}

void test_sdo_set_targets()
{
    CanFrame f;
    TEST_ASSERT_TRUE(pc_cia402_sdo_set_target_position(&f, 1, 0x01020304));
    TEST_ASSERT_EQUAL_HEX32(0x601u, f.id);
    TEST_ASSERT_EQUAL_HEX8(0x7A, f.data[1]); // 0x607A LE
    TEST_ASSERT_EQUAL_HEX8(0x60, f.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x04, f.data[4]);
    TEST_ASSERT_EQUAL_HEX8(0x03, f.data[5]);
    TEST_ASSERT_EQUAL_HEX8(0x02, f.data[6]);
    TEST_ASSERT_EQUAL_HEX8(0x01, f.data[7]);

    TEST_ASSERT_TRUE(pc_cia402_sdo_set_mode(&f, 3, cyclic_sync_position));
    TEST_ASSERT_EQUAL_HEX8(0x60, f.data[1]); // 0x6060 LE
    TEST_ASSERT_EQUAL_HEX8(0x60, f.data[2]);
    TEST_ASSERT_EQUAL_HEX8(8, f.data[4]); // CSP = 8
}

void test_sdo_get_roundtrip()
{
    // Build a read request, then decode a crafted SDO upload response for the Statusword.
    CanFrame req;
    TEST_ASSERT_TRUE(pc_cia402_sdo_read(&req, 7, CIA402_OD_STATUSWORD, 0));
    TEST_ASSERT_EQUAL_HEX32(0x607u, req.id);

    // Server upload response (0x580+node): scs=2 expedited, 2 data bytes -> command 0x4B.
    CanFrame resp;
    resp.id = 0x587u; // SDO_TX + node 7
    resp.extended = PROTO_FALSE;
    resp.dlc = 8;
    resp.data[0] = 0x4B; // scs upload, expedited, size, n=2 unused
    resp.data[1] = 0x41;
    resp.data[2] = 0x60; // index 0x6041
    resp.data[3] = 0x00;
    resp.data[4] = 0x37;
    resp.data[5] = 0x06; // value 0x0637
    resp.data[6] = 0x00;
    resp.data[7] = 0x00;
    uint16_t sw = 0;
    TEST_ASSERT_TRUE(pc_cia402_sdo_get_u16(&resp, CIA402_OD_STATUSWORD, &sw));
    TEST_ASSERT_EQUAL_HEX16(0x0637, sw);
    TEST_ASSERT_TRUE(pc_cia402_state(sw) == operation_enabled);

    // wrong-index guard
    uint16_t v = 0;
    TEST_ASSERT_FALSE(pc_cia402_sdo_get_u16(&resp, CIA402_OD_CONTROLWORD, &v));
}

void test_pdo_pack_unpack()
{
    uint8_t buf[8];
    size_t n = pc_cia402_pack_command(buf, sizeof(buf), 0x000F, -12345);
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_HEX8(0x0F, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);

    uint16_t sw = 0;
    int32_t actual = 0;
    const uint8_t tpdo[6] = {0x37, 0x06, 0xC7, 0xCF, 0xFF, 0xFF}; // status 0x0637, actual -12345
    TEST_ASSERT_TRUE(pc_cia402_unpack_status(tpdo, sizeof(tpdo), &sw, &actual));
    TEST_ASSERT_EQUAL_HEX16(0x0637, sw);
    TEST_ASSERT_EQUAL_INT32(-12345, actual);

    TEST_ASSERT_EQUAL_size_t(0, pc_cia402_pack_command(buf, 4, 0, 0)); // too small
    TEST_ASSERT_FALSE(pc_cia402_unpack_status(tpdo, 5, &sw, &actual)); // too short
}

// A Statusword matching no CiA 402 mask/value pattern decodes to unknown (the fall-through after
// the fault mask). 0x0001 hits none of the eight defined states.
void test_state_decode_unknown()
{
    TEST_ASSERT_TRUE(pc_cia402_state(0x0001) == unknown);
}

// An out-of-range command value hits the Controlword switch default and returns a safe 0x0000.
void test_controlword_invalid_command()
{
    TEST_ASSERT_EQUAL_HEX16(0x0000, pc_cia402_controlword((Cia402Command)99));
}

// The velocity / torque SDO setters the happy-path test skips, byte-checked, plus each setter's
// null-frame reject (the false side of the pc_canopen_build_sdo_write return).
void test_sdo_set_velocity_torque()
{
    CanFrame f = {0};
    TEST_ASSERT_TRUE(pc_cia402_sdo_set_target_velocity(&f, 2, 0x0A0B0C0D));
    TEST_ASSERT_EQUAL_HEX32(0x602u, f.id);   // SDO_RX (0x600) + node 2
    TEST_ASSERT_EQUAL_HEX8(0xFF, f.data[1]); // index 0x60FF LE
    TEST_ASSERT_EQUAL_HEX8(0x60, f.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[3]); // sub 0
    TEST_ASSERT_EQUAL_HEX8(0x0D, f.data[4]); // value LE
    TEST_ASSERT_EQUAL_HEX8(0x0C, f.data[5]);
    TEST_ASSERT_EQUAL_HEX8(0x0B, f.data[6]);
    TEST_ASSERT_EQUAL_HEX8(0x0A, f.data[7]);

    TEST_ASSERT_TRUE(pc_cia402_sdo_set_target_torque(&f, 4, -2)); // -2 = 0xFFFE
    TEST_ASSERT_EQUAL_HEX32(0x604u, f.id);
    TEST_ASSERT_EQUAL_HEX8(0x71, f.data[1]); // index 0x6071 LE
    TEST_ASSERT_EQUAL_HEX8(0x60, f.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, f.data[4]); // -2 LE
    TEST_ASSERT_EQUAL_HEX8(0xFF, f.data[5]);

    // null out frame -> false, for every SDO setter (each forwards pc_canopen_build_sdo_write's false).
    TEST_ASSERT_FALSE(pc_cia402_sdo_set_controlword(NULL, 5, 0x000F));
    TEST_ASSERT_FALSE(pc_cia402_sdo_set_mode(NULL, 3, cyclic_sync_position));
    TEST_ASSERT_FALSE(pc_cia402_sdo_set_target_position(NULL, 1, 0));
    TEST_ASSERT_FALSE(pc_cia402_sdo_set_target_velocity(NULL, 2, 0));
    TEST_ASSERT_FALSE(pc_cia402_sdo_set_target_torque(NULL, 4, 0));
}

// Decode a signed 32-bit object from an expedited SDO upload (cmd 0x43: scs=2, e=1, s=1, len 4).
void test_sdo_get_i32_roundtrip()
{
    CanFrame resp = make_sdo_tx(7, 0x43, CIA402_OD_POSITION_ACTUAL, 0xFFFFFFECu); // -20
    int32_t val = 0;
    TEST_ASSERT_TRUE(pc_cia402_sdo_get_i32(&resp, CIA402_OD_POSITION_ACTUAL, &val));
    TEST_ASSERT_EQUAL_INT32(-20, val);
    // want_index 0 skips the index match (the short-circuited first arm) and still decodes.
    val = 0;
    TEST_ASSERT_TRUE(pc_cia402_sdo_get_i32(&resp, 0, &val));
    TEST_ASSERT_EQUAL_INT32(-20, val);
    // null value pointer -> false.
    TEST_ASSERT_FALSE(pc_cia402_sdo_get_i32(&resp, 0, NULL));
}

// Every reject path inside the SDO upload validation: parse failure, abort, download-ack,
// segmented (non-expedited), and payload shorter than the requested width.
void test_sdo_upload_reject_paths()
{
    uint16_t v = 0;
    // (a) parse failure: dlc < 8 makes pc_canopen_parse_sdo_response fail.
    CanFrame shortf = make_sdo_tx(7, 0x4B, CIA402_OD_STATUSWORD, 0);
    shortf.dlc = 4;
    TEST_ASSERT_FALSE(pc_cia402_sdo_get_u16(&shortf, 0, &v));
    // (b) abort response (scs=4): is_abort.
    CanFrame ab = make_sdo_tx(7, (uint8_t)(4u << 5), CIA402_OD_STATUSWORD, 0);
    TEST_ASSERT_FALSE(pc_cia402_sdo_get_u16(&ab, 0, &v));
    // (c) download-initiate ack (scs=3): not an upload.
    CanFrame dl = make_sdo_tx(7, (uint8_t)(3u << 5), CIA402_OD_STATUSWORD, 0);
    TEST_ASSERT_FALSE(pc_cia402_sdo_get_u16(&dl, 0, &v));
    // (d) segmented upload (scs=2, e=0): not expedited.
    CanFrame seg = make_sdo_tx(7, (uint8_t)(2u << 5), CIA402_OD_STATUSWORD, 0);
    TEST_ASSERT_FALSE(pc_cia402_sdo_get_u16(&seg, 0, &v));
    // (e) expedited upload of only 1 octet (cmd 0x4F -> len 1): shorter than the 2 needed.
    CanFrame shortlen = make_sdo_tx(7, 0x4F, CIA402_OD_STATUSWORD, 0);
    TEST_ASSERT_FALSE(pc_cia402_sdo_get_u16(&shortlen, 0, &v));
    // null value pointer to get_u16.
    CanFrame okresp = make_sdo_tx(7, 0x4B, CIA402_OD_STATUSWORD, 0x0637);
    TEST_ASSERT_FALSE(pc_cia402_sdo_get_u16(&okresp, 0, NULL));
    // a 2-octet upload is too short for a 4-octet get_i32.
    CanFrame u16resp = make_sdo_tx(7, 0x4B, CIA402_OD_POSITION_ACTUAL, 0x1234);
    int32_t iv = 0;
    TEST_ASSERT_FALSE(pc_cia402_sdo_get_i32(&u16resp, 0, &iv));
}

// Null-argument guards on the cyclic PDO pack/unpack (complementing the too-short cases).
void test_pdo_null_guards()
{
    uint8_t buf[8];
    uint16_t sw = 0;
    int32_t actual = 0;
    const uint8_t tpdo[6] = {0x37, 0x06, 0xC7, 0xCF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_size_t(0, pc_cia402_pack_command(NULL, sizeof(buf), 0, 0));  // null buffer
    TEST_ASSERT_FALSE(pc_cia402_unpack_status(NULL, sizeof(tpdo), &sw, &actual));  // null buffer
    TEST_ASSERT_FALSE(pc_cia402_unpack_status(tpdo, sizeof(tpdo), NULL, &actual)); // null statusword
    TEST_ASSERT_FALSE(pc_cia402_unpack_status(tpdo, sizeof(tpdo), &sw, NULL));     // null actual
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_state_decode);
    RUN_TEST(test_state_decode_ignores_high_bits);
    RUN_TEST(test_controlword_commands);
    RUN_TEST(test_enable_sequence);
    RUN_TEST(test_statusword_flags);
    RUN_TEST(test_sdo_set_controlword);
    RUN_TEST(test_sdo_set_targets);
    RUN_TEST(test_sdo_get_roundtrip);
    RUN_TEST(test_pdo_pack_unpack);
    RUN_TEST(test_state_decode_unknown);
    RUN_TEST(test_controlword_invalid_command);
    RUN_TEST(test_sdo_set_velocity_torque);
    RUN_TEST(test_sdo_get_i32_roundtrip);
    RUN_TEST(test_sdo_upload_reject_paths);
    RUN_TEST(test_pdo_null_guards);
    return UNITY_END();
}
