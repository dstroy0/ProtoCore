// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the CiA 402 / IEC 61800-7-201 drive profile (services/fieldbus/cia402/cia402.h).
//
// The load-bearing case is test_statusword_mask_value_table. IEC 61800-7-201 decodes the drive's
// power state from the Statusword with a mask/value table whose rows deliberately overlap: Quick
// Stop Active (x00x 0111) and Operation Enabled (x01x 0111) differ only in bit 5, so the row must
// be matched under mask 0x6F. Widen the mask to 0x4F and a drive that has already quick-stopped
// reads as running - the failure mode that moves an axis nobody asked to move. Every row below is
// written out as the standard's bit pattern with its don't-care bits both cleared and set, so a
// wrong mask cannot pass on the canonical value alone.

#include "services/fieldbus/canopen/canopen.h"
#include "services/fieldbus/cia402/cia402.h"
#include "shared/can/can.h"
#include <string.h>

#include <unity.h>

static uint8_t cia402_work[16]; // the borrow an entry takes; Cia402 never reads it

static uint8_t canopen_work[16]; // the borrow an entry takes; Canopen never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

// The object dictionary indices the profile reserves in the 0x6000 drive area.
void test_object_dictionary_indices(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x603Fu, CIA402_OD_ERROR_CODE);
    TEST_ASSERT_EQUAL_HEX16(0x6040u, CIA402_OD_CONTROLWORD);
    TEST_ASSERT_EQUAL_HEX16(0x6041u, CIA402_OD_STATUSWORD);
    TEST_ASSERT_EQUAL_HEX16(0x605Au, CIA402_OD_QUICK_STOP_OPTION);
    TEST_ASSERT_EQUAL_HEX16(0x6060u, CIA402_OD_MODES_OF_OPERATION);
    TEST_ASSERT_EQUAL_HEX16(0x6061u, CIA402_OD_MODES_DISPLAY);
    TEST_ASSERT_EQUAL_HEX16(0x6064u, CIA402_OD_POSITION_ACTUAL);
    TEST_ASSERT_EQUAL_HEX16(0x606Cu, CIA402_OD_VELOCITY_ACTUAL);
    TEST_ASSERT_EQUAL_HEX16(0x6071u, CIA402_OD_TARGET_TORQUE);
    TEST_ASSERT_EQUAL_HEX16(0x6077u, CIA402_OD_TORQUE_ACTUAL);
    TEST_ASSERT_EQUAL_HEX16(0x607Au, CIA402_OD_TARGET_POSITION);
    TEST_ASSERT_EQUAL_HEX16(0x6081u, CIA402_OD_PROFILE_VELOCITY);
    TEST_ASSERT_EQUAL_HEX16(0x6083u, CIA402_OD_PROFILE_ACCEL);
    TEST_ASSERT_EQUAL_HEX16(0x6084u, CIA402_OD_PROFILE_DECEL);
    TEST_ASSERT_EQUAL_HEX16(0x60FFu, CIA402_OD_TARGET_VELOCITY);
    TEST_ASSERT_EQUAL_HEX16(0x6502u, CIA402_OD_SUPPORTED_MODES);

    // Modes of Operation values; 5 is not assigned, so the run is not contiguous.
    TEST_ASSERT_EQUAL_INT(1, CIA402_MODE_PROFILE_POSITION);
    TEST_ASSERT_EQUAL_INT(2, CIA402_MODE_VELOCITY);
    TEST_ASSERT_EQUAL_INT(3, CIA402_MODE_PROFILE_VELOCITY);
    TEST_ASSERT_EQUAL_INT(4, CIA402_MODE_PROFILE_TORQUE);
    TEST_ASSERT_EQUAL_INT(6, CIA402_MODE_HOMING);
    TEST_ASSERT_EQUAL_INT(7, CIA402_MODE_INTERPOLATED_POSITION);
    TEST_ASSERT_EQUAL_INT(8, CIA402_MODE_CYCLIC_SYNC_POSITION);
    TEST_ASSERT_EQUAL_INT(9, CIA402_MODE_CYCLIC_SYNC_VELOCITY);
    TEST_ASSERT_EQUAL_INT(10, CIA402_MODE_CYCLIC_SYNC_TORQUE);

    // Statusword / Controlword bit positions.
    TEST_ASSERT_EQUAL_HEX16(0x0001u, CIA402_SW_READY_TO_SWITCH_ON);
    TEST_ASSERT_EQUAL_HEX16(0x0002u, CIA402_SW_SWITCHED_ON);
    TEST_ASSERT_EQUAL_HEX16(0x0004u, CIA402_SW_OPERATION_ENABLED);
    TEST_ASSERT_EQUAL_HEX16(0x0008u, CIA402_SW_FAULT);
    TEST_ASSERT_EQUAL_HEX16(0x0010u, CIA402_SW_VOLTAGE_ENABLED);
    TEST_ASSERT_EQUAL_HEX16(0x0020u, CIA402_SW_QUICK_STOP);
    TEST_ASSERT_EQUAL_HEX16(0x0040u, CIA402_SW_SWITCH_ON_DISABLED);
    TEST_ASSERT_EQUAL_HEX16(0x0080u, CIA402_SW_WARNING);
    TEST_ASSERT_EQUAL_HEX16(0x0200u, CIA402_SW_REMOTE);
    TEST_ASSERT_EQUAL_HEX16(0x0400u, CIA402_SW_TARGET_REACHED);
    TEST_ASSERT_EQUAL_HEX16(0x0800u, CIA402_SW_INTERNAL_LIMIT);
    TEST_ASSERT_EQUAL_HEX16(0x0001u, CIA402_CW_SWITCH_ON);
    TEST_ASSERT_EQUAL_HEX16(0x0002u, CIA402_CW_ENABLE_VOLTAGE);
    TEST_ASSERT_EQUAL_HEX16(0x0004u, CIA402_CW_QUICK_STOP);
    TEST_ASSERT_EQUAL_HEX16(0x0008u, CIA402_CW_ENABLE_OPERATION);
    TEST_ASSERT_EQUAL_HEX16(0x0080u, CIA402_CW_FAULT_RESET);
}

// The published rows, written as (mask, value, state). A statusword matches a row when
// sw & mask == value; the bits outside the mask are don't-cares and must not change the answer,
// so each row is tried with them all clear and all set.
void test_statusword_mask_value_table(void)
{
    struct
    {
        uint16_t mask;
        uint16_t value;
        Cia402State state;
    } static const ROWS[] = {
        {0x4Fu, 0x00u, CIA402_STATE_NOT_READY_TO_SWITCH_ON}, // x0xx 0000
        {0x4Fu, 0x40u, CIA402_STATE_SWITCH_ON_DISABLED},     // x1xx 0000
        {0x6Fu, 0x21u, CIA402_STATE_READY_TO_SWITCH_ON},     // x01x 0001
        {0x6Fu, 0x23u, CIA402_STATE_SWITCHED_ON},            // x01x 0011
        {0x6Fu, 0x27u, CIA402_STATE_OPERATION_ENABLED},      // x01x 0111
        {0x6Fu, 0x07u, CIA402_STATE_QUICK_STOP_ACTIVE},      // x00x 0111
        {0x4Fu, 0x0Fu, CIA402_STATE_FAULT_REACTION_ACTIVE},  // x0xx 1111
        {0x4Fu, 0x08u, CIA402_STATE_FAULT},                  // x0xx 1000
    };
    for (size_t i = 0; i < sizeof(ROWS) / sizeof(ROWS[0]); i++)
    {
        uint16_t bare = ROWS[i].value;
        uint16_t decorated = (uint16_t)(ROWS[i].value | (uint16_t)~ROWS[i].mask);
        Cia402V.state_args.statusword = bare;
        Cia402V.state(cia402_work);
        TEST_ASSERT_EQUAL_INT_MESSAGE(ROWS[i].state, Cia402V.value, "bare statusword");
        Cia402V.state_args.statusword = decorated;
        Cia402V.state(cia402_work);
        TEST_ASSERT_EQUAL_INT_MESSAGE(ROWS[i].state, Cia402V.value, "don't-care bits set");
    }
}

// Bit 5 alone separates a running drive from one that has quick-stopped. Both carry the low nibble
// 0111, so only the 0x6F mask tells them apart.
void test_bit_five_separates_quick_stop_from_operation_enabled(void)
{
    Cia402V.state_args.statusword = 0x0027u;
    Cia402V.state(cia402_work);
    TEST_ASSERT_EQUAL_INT(CIA402_STATE_OPERATION_ENABLED, Cia402V.value);
    Cia402V.state_args.statusword = 0x0007u;
    Cia402V.state(cia402_work);
    TEST_ASSERT_EQUAL_INT(CIA402_STATE_QUICK_STOP_ACTIVE, Cia402V.value);
    // The same pair with a realistic upper byte: voltage enabled, remote, target reached.
    Cia402V.state_args.statusword = 0x0637u;
    Cia402V.state(cia402_work);
    TEST_ASSERT_EQUAL_INT(CIA402_STATE_OPERATION_ENABLED, Cia402V.value);
    Cia402V.state_args.statusword = 0x0617u;
    Cia402V.state(cia402_work);
    TEST_ASSERT_EQUAL_INT(CIA402_STATE_QUICK_STOP_ACTIVE, Cia402V.value);
}

// A Statusword matching no row is reported as unknown rather than guessed at.
void test_unmatched_statusword_is_unknown(void)
{
    Cia402V.state_args.statusword = 0x0041u;
    Cia402V.state(cia402_work);
    TEST_ASSERT_EQUAL_INT(CIA402_STATE_UNKNOWN, Cia402V.value); // x1xx 0001 is no row
    Cia402V.state_args.statusword = 0x0002u;
    Cia402V.state(cia402_work);
    TEST_ASSERT_EQUAL_INT(CIA402_STATE_UNKNOWN, Cia402V.value); // switched-on bit alone
    Cia402V.state_args.statusword = 0x0004u;
    Cia402V.state(cia402_work);
    TEST_ASSERT_EQUAL_INT(CIA402_STATE_UNKNOWN, Cia402V.value);
    Cia402V.state_args.statusword = 0x0043u;
    Cia402V.state(cia402_work);
    TEST_ASSERT_EQUAL_INT(CIA402_STATE_UNKNOWN, Cia402V.value);
}

// The Statusword flag accessors read the bit their name gives.
void test_statusword_flag_accessors(void)
{
    TEST_ASSERT_TRUE(protocore_cia402_target_reached(CIA402_SW_TARGET_REACHED));
    TEST_ASSERT_FALSE(protocore_cia402_target_reached((uint16_t)~CIA402_SW_TARGET_REACHED));
    TEST_ASSERT_TRUE(protocore_cia402_has_fault(CIA402_SW_FAULT));
    TEST_ASSERT_FALSE(protocore_cia402_has_fault((uint16_t)~CIA402_SW_FAULT));
    TEST_ASSERT_TRUE(protocore_cia402_warning(CIA402_SW_WARNING));
    TEST_ASSERT_FALSE(protocore_cia402_warning((uint16_t)~CIA402_SW_WARNING));
    TEST_ASSERT_TRUE(protocore_cia402_voltage_enabled(CIA402_SW_VOLTAGE_ENABLED));
    TEST_ASSERT_FALSE(protocore_cia402_voltage_enabled((uint16_t)~CIA402_SW_VOLTAGE_ENABLED));
    TEST_ASSERT_TRUE(protocore_cia402_remote(CIA402_SW_REMOTE));
    TEST_ASSERT_FALSE(protocore_cia402_remote((uint16_t)~CIA402_SW_REMOTE));
    TEST_ASSERT_TRUE(protocore_cia402_internal_limit(CIA402_SW_INTERNAL_LIMIT));
    TEST_ASSERT_FALSE(protocore_cia402_internal_limit((uint16_t)~CIA402_SW_INTERNAL_LIMIT));
}

// The Controlword command table. Each value is the bit pattern the standard prints, assembled from
// the named bits: Shutdown is enable-voltage + quick-stop with switch-on clear, Switch On adds
// switch-on, Enable Operation adds enable-operation, and Fault Reset is bit 7 alone.
void test_controlword_command_table(void)
{
    Cia402V.controlword_args.cmd = CIA402_COMMAND_SHUTDOWN;
    Cia402V.controlword(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(CIA402_CW_ENABLE_VOLTAGE | CIA402_CW_QUICK_STOP, Cia402V.u16);
    Cia402V.controlword_args.cmd = CIA402_COMMAND_SHUTDOWN;
    Cia402V.controlword(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x0006u, Cia402V.u16);

    Cia402V.controlword_args.cmd = CIA402_COMMAND_SWITCH_ON;
    Cia402V.controlword(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(CIA402_CW_SWITCH_ON | CIA402_CW_ENABLE_VOLTAGE | CIA402_CW_QUICK_STOP, Cia402V.u16);
    Cia402V.controlword_args.cmd = CIA402_COMMAND_SWITCH_ON;
    Cia402V.controlword(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x0007u, Cia402V.u16);

    Cia402V.controlword_args.cmd = CIA402_COMMAND_ENABLE_OPERATION;
    Cia402V.controlword(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x000Fu, Cia402V.u16);
    Cia402V.controlword_args.cmd = CIA402_COMMAND_ENABLE_OPERATION;
    Cia402V.controlword(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(CIA402_CW_SWITCH_ON | CIA402_CW_ENABLE_VOLTAGE | CIA402_CW_QUICK_STOP |
                                CIA402_CW_ENABLE_OPERATION,
                            Cia402V.u16);

    // Disable Voltage clears the enable-voltage bit; Quick Stop keeps voltage but clears the
    // active-low quick-stop bit.
    Cia402V.controlword_args.cmd = CIA402_COMMAND_DISABLE_VOLTAGE;
    Cia402V.controlword(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, Cia402V.u16);
    Cia402V.controlword_args.cmd = CIA402_COMMAND_QUICK_STOP;
    Cia402V.controlword(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x0002u, Cia402V.u16);
    Cia402V.controlword_args.cmd = CIA402_COMMAND_QUICK_STOP;
    Cia402V.controlword(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0u, Cia402V.u16 & CIA402_CW_QUICK_STOP);

    // Disable Operation drops back to the Switched On word.
    Cia402V.controlword_args.cmd = CIA402_COMMAND_DISABLE_OPERATION;
    Cia402V.controlword(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x0007u, Cia402V.u16);

    // Fault Reset is the bit-7 rising edge and nothing else.
    Cia402V.controlword_args.cmd = CIA402_COMMAND_FAULT_RESET;
    Cia402V.controlword(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(CIA402_CW_FAULT_RESET, Cia402V.u16);
    Cia402V.controlword_args.cmd = CIA402_COMMAND_FAULT_RESET;
    Cia402V.controlword(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x0080u, Cia402V.u16);
}

// The bring-up loop: from any state, the Controlword that moves one step toward Operation Enabled.
// Feeding each step's Controlword back as the state it produces walks the whole power state machine
// Switch On Disabled -> Ready To Switch On -> Switched On -> Operation Enabled.
void test_enable_sequence_walks_the_state_machine(void)
{
    Cia402V.enable_sequence_args.state = CIA402_STATE_SWITCH_ON_DISABLED;
    Cia402V.enable_sequence(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x0006u, Cia402V.u16);
    Cia402V.enable_sequence_args.state = CIA402_STATE_READY_TO_SWITCH_ON;
    Cia402V.enable_sequence(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x0007u, Cia402V.u16);
    Cia402V.enable_sequence_args.state = CIA402_STATE_SWITCHED_ON;
    Cia402V.enable_sequence(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x000Fu, Cia402V.u16);
    Cia402V.enable_sequence_args.state = CIA402_STATE_OPERATION_ENABLED;
    Cia402V.enable_sequence(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x000Fu, Cia402V.u16);
    Cia402V.enable_sequence_args.state = CIA402_STATE_QUICK_STOP_ACTIVE;
    Cia402V.enable_sequence(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x000Fu, Cia402V.u16);

    // A fault must be reset before the sequence can restart.
    Cia402V.enable_sequence_args.state = CIA402_STATE_FAULT;
    Cia402V.enable_sequence(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x0080u, Cia402V.u16);
    Cia402V.enable_sequence_args.state = CIA402_STATE_FAULT_REACTION_ACTIVE;
    Cia402V.enable_sequence(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x0080u, Cia402V.u16);

    // Nothing is commanded until the drive has left Not Ready To Switch On.
    Cia402V.enable_sequence_args.state = CIA402_STATE_NOT_READY_TO_SWITCH_ON;
    Cia402V.enable_sequence(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, Cia402V.u16);
    Cia402V.enable_sequence_args.state = CIA402_STATE_UNKNOWN;
    Cia402V.enable_sequence(cia402_work);
    TEST_ASSERT_EQUAL_HEX16(0x0000u, Cia402V.u16);

    // Walked as a loop over the statuswords the drive would report at each step.
    static const uint16_t WALK[4] = {0x0040u, 0x0021u, 0x0023u, 0x0027u};
    static const uint16_t WANT[4] = {0x0006u, 0x0007u, 0x000Fu, 0x000Fu};
    for (size_t i = 0; i < 4; i++)
    {
        Cia402V.state_args.statusword = WALK[i];
        Cia402V.state(cia402_work);
        Cia402V.enable_sequence_args.state = Cia402V.value;
        Cia402V.enable_sequence(cia402_work);
        TEST_ASSERT_EQUAL_HEX16(WANT[i], Cia402V.u16);
    }
}

// The SDO setters are expedited CANopen downloads to the profile's objects: COB-ID 0x600 + node,
// command (1 << 5) | ((4 - n) << 2) | 0x03, index little-endian, then n value octets.
void test_sdo_setters_target_the_right_objects(void)
{
    CanFrame f;

    Cia402V.sdo_set_controlword_args.out = &f;
    Cia402V.sdo_set_controlword_args.node = 5;
    Cia402V.sdo_set_controlword_args.controlword = 0x000Fu;
    Cia402V.sdo_set_controlword(cia402_work);
    TEST_ASSERT_TRUE(Cia402V.ok);
    TEST_ASSERT_EQUAL_HEX32(0x605u, f.id);
    static const uint8_t CW[8] = {0x2B, 0x40, 0x60, 0x00, 0x0F, 0x00, 0x00, 0x00}; // 2 octets -> 0x2B
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CW, f.data, 8);

    Cia402V.sdo_set_mode_args.out = &f;
    Cia402V.sdo_set_mode_args.node = 5;
    Cia402V.sdo_set_mode_args.mode = CIA402_MODE_CYCLIC_SYNC_POSITION;
    Cia402V.sdo_set_mode(cia402_work);
    TEST_ASSERT_TRUE(Cia402V.ok);
    static const uint8_t MODE[8] = {0x2F, 0x60, 0x60, 0x00, 0x08, 0x00, 0x00, 0x00}; // 1 octet -> 0x2F
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MODE, f.data, 8);

    // -1000 as a 32-bit two's complement is 0x100000000 - 1000 = 0xFFFFFC18, little-endian.
    Cia402V.sdo_set_target_position_args.out = &f;
    Cia402V.sdo_set_target_position_args.node = 5;
    Cia402V.sdo_set_target_position_args.position = -1000;
    Cia402V.sdo_set_target_position(cia402_work);
    TEST_ASSERT_TRUE(Cia402V.ok);
    static const uint8_t POS[8] = {0x23, 0x7A, 0x60, 0x00, 0x18, 0xFC, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(POS, f.data, 8);

    // 100000 = 0x000186A0
    Cia402V.sdo_set_target_velocity_args.out = &f;
    Cia402V.sdo_set_target_velocity_args.node = 5;
    Cia402V.sdo_set_target_velocity_args.velocity = 100000;
    Cia402V.sdo_set_target_velocity(cia402_work);
    TEST_ASSERT_TRUE(Cia402V.ok);
    static const uint8_t VEL[8] = {0x23, 0xFF, 0x60, 0x00, 0xA0, 0x86, 0x01, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(VEL, f.data, 8);

    // -500 as a 16-bit two's complement is 0x10000 - 500 = 0xFE0C, little-endian.
    Cia402V.sdo_set_target_torque_args.out = &f;
    Cia402V.sdo_set_target_torque_args.node = 5;
    Cia402V.sdo_set_target_torque_args.torque = -500;
    Cia402V.sdo_set_target_torque(cia402_work);
    TEST_ASSERT_TRUE(Cia402V.ok);
    static const uint8_t TRQ[8] = {0x2B, 0x71, 0x60, 0x00, 0x0C, 0xFE, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TRQ, f.data, 8);

    // The read wrapper is the CANopen upload initiate: command 0x40, index little-endian.
    Cia402V.sdo_read_args.out = &f;
    Cia402V.sdo_read_args.node = 5;
    Cia402V.sdo_read_args.index = CIA402_OD_STATUSWORD;
    Cia402V.sdo_read_args.sub = 0;
    Cia402V.sdo_read(cia402_work);
    TEST_ASSERT_TRUE(Cia402V.ok);
    static const uint8_t RD[8] = {0x40, 0x41, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RD, f.data, 8);

    // A node id outside 1..127 is refused by the CANopen layer underneath.
    Cia402V.sdo_set_controlword_args.out = &f;
    Cia402V.sdo_set_controlword_args.node = 0;
    Cia402V.sdo_set_controlword_args.controlword = 0x000Fu;
    Cia402V.sdo_set_controlword(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);
    Cia402V.sdo_set_controlword_args.out = &f;
    Cia402V.sdo_set_controlword_args.node = 128;
    Cia402V.sdo_set_controlword_args.controlword = 0x000Fu;
    Cia402V.sdo_set_controlword(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);
    Cia402V.sdo_read_args.out = &f;
    Cia402V.sdo_read_args.node = 0;
    Cia402V.sdo_read_args.index = CIA402_OD_STATUSWORD;
    Cia402V.sdo_read_args.sub = 0;
    Cia402V.sdo_read(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);
}

// Helper: an expedited SDO upload response from @p node carrying @p n octets of @p index.
static CanFrame upload_response(uint8_t node, uint16_t index, const uint8_t *value, uint8_t n)
{
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x580u + node;
    f.extended = PROTO_FALSE;
    f.rtr = PROTO_FALSE;
    f.dlc = 8;
    f.data[0] = (uint8_t)(0x40u | (((uint8_t)(4u - n)) << 2) | 0x03u); // scs 2, expedited, size indicated
    f.data[1] = (uint8_t)index;
    f.data[2] = (uint8_t)(index >> 8);
    f.data[3] = 0;
    memcpy(f.data + 4, value, n);
    return f;
}

// A Statusword read back out of an SDO upload response, with the index checked so a reply to a
// different object is never mistaken for the one asked for.
void test_sdo_get_u16_checks_the_index(void)
{
    static const uint8_t SW[2] = {0x37, 0x06}; // 0x0637: operation enabled, remote, target reached
    CanFrame f = upload_response(5, CIA402_OD_STATUSWORD, SW, 2);

    uint16_t value = 0;
    Cia402V.sdo_get_u16_args.f = &f;
    Cia402V.sdo_get_u16_args.want_index = CIA402_OD_STATUSWORD;
    Cia402V.sdo_get_u16_args.value = &value;
    Cia402V.sdo_get_u16(cia402_work);
    TEST_ASSERT_TRUE(Cia402V.ok);
    TEST_ASSERT_EQUAL_HEX16(0x0637u, value);
    Cia402V.state_args.statusword = value;
    Cia402V.state(cia402_work);
    TEST_ASSERT_EQUAL_INT(CIA402_STATE_OPERATION_ENABLED, Cia402V.value);

    // A zero want_index accepts whatever object the reply names.
    value = 0;
    Cia402V.sdo_get_u16_args.f = &f;
    Cia402V.sdo_get_u16_args.want_index = 0;
    Cia402V.sdo_get_u16_args.value = &value;
    Cia402V.sdo_get_u16(cia402_work);
    TEST_ASSERT_TRUE(Cia402V.ok);
    TEST_ASSERT_EQUAL_HEX16(0x0637u, value);

    // The reply to a different object is refused.
    Cia402V.sdo_get_u16_args.f = &f;
    Cia402V.sdo_get_u16_args.want_index = CIA402_OD_ERROR_CODE;
    Cia402V.sdo_get_u16_args.value = &value;
    Cia402V.sdo_get_u16(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);
    Cia402V.sdo_get_u16_args.f = &f;
    Cia402V.sdo_get_u16_args.want_index = CIA402_OD_STATUSWORD;
    Cia402V.sdo_get_u16_args.value = NULL;
    Cia402V.sdo_get_u16(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);

    // An abort is not a value.
    CanFrame abort_frame;
    CanopenV.build_sdo_abort_args.out = &abort_frame;
    CanopenV.build_sdo_abort_args.node_id = 5;
    CanopenV.build_sdo_abort_args.index = CIA402_OD_STATUSWORD;
    CanopenV.build_sdo_abort_args.sub = 0;
    CanopenV.build_sdo_abort_args.abort_code = CANOPEN_ABORT_NO_OBJECT;
    CanopenV.build_sdo_abort_args.to_server = PROTO_FALSE;
    Canopen.build_sdo_abort(canopen_work);
    TEST_ASSERT_TRUE(CanopenV.ok);
    Cia402V.sdo_get_u16_args.f = &abort_frame;
    Cia402V.sdo_get_u16_args.want_index = CIA402_OD_STATUSWORD;
    Cia402V.sdo_get_u16_args.value = &value;
    Cia402V.sdo_get_u16(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);

    // A one-octet reply cannot carry a 16-bit object.
    static const uint8_t ONE[1] = {0x37};
    CanFrame narrow = upload_response(5, CIA402_OD_STATUSWORD, ONE, 1);
    Cia402V.sdo_get_u16_args.f = &narrow;
    Cia402V.sdo_get_u16_args.want_index = CIA402_OD_STATUSWORD;
    Cia402V.sdo_get_u16_args.value = &value;
    Cia402V.sdo_get_u16(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);
}

// Position Actual Value is signed: 0xFFFFFC18 little-endian is -1000, not 4294966296.
void test_sdo_get_i32_is_signed(void)
{
    static const uint8_t NEG[4] = {0x18, 0xFC, 0xFF, 0xFF};
    CanFrame f = upload_response(9, CIA402_OD_POSITION_ACTUAL, NEG, 4);

    int32_t value = 0;
    Cia402V.sdo_get_i32_args.f = &f;
    Cia402V.sdo_get_i32_args.want_index = CIA402_OD_POSITION_ACTUAL;
    Cia402V.sdo_get_i32_args.value = &value;
    Cia402V.sdo_get_i32(cia402_work);
    TEST_ASSERT_TRUE(Cia402V.ok);
    TEST_ASSERT_EQUAL_INT32(-1000, value);

    static const uint8_t MIN[4] = {0x00, 0x00, 0x00, 0x80}; // 0x80000000
    f = upload_response(9, CIA402_OD_VELOCITY_ACTUAL, MIN, 4);
    Cia402V.sdo_get_i32_args.f = &f;
    Cia402V.sdo_get_i32_args.want_index = CIA402_OD_VELOCITY_ACTUAL;
    Cia402V.sdo_get_i32_args.value = &value;
    Cia402V.sdo_get_i32(cia402_work);
    TEST_ASSERT_TRUE(Cia402V.ok);
    TEST_ASSERT_EQUAL_INT32(-2147483647 - 1, value);

    Cia402V.sdo_get_i32_args.f = &f;
    Cia402V.sdo_get_i32_args.want_index = CIA402_OD_POSITION_ACTUAL;
    Cia402V.sdo_get_i32_args.value = &value;
    Cia402V.sdo_get_i32(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);
    Cia402V.sdo_get_i32_args.f = &f;
    Cia402V.sdo_get_i32_args.want_index = 0;
    Cia402V.sdo_get_i32_args.value = NULL;
    Cia402V.sdo_get_i32(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);

    // A two-octet reply cannot carry a 32-bit object.
    static const uint8_t TWO[2] = {0x18, 0xFC};
    CanFrame narrow = upload_response(9, CIA402_OD_POSITION_ACTUAL, TWO, 2);
    Cia402V.sdo_get_i32_args.f = &narrow;
    Cia402V.sdo_get_i32_args.want_index = CIA402_OD_POSITION_ACTUAL;
    Cia402V.sdo_get_i32_args.value = &value;
    Cia402V.sdo_get_i32(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);
}

// The cyclic PDO map: Controlword (u16 LE) + Target (i32 LE) out, Statusword + Actual back in,
// six octets each way. Packing then unpacking the same octets returns what went in.
void test_pdo_pack_and_unpack_round_trip(void)
{
    uint8_t buf[8];
    memset(buf, 0xEE, sizeof(buf));
    Cia402V.pack_command_args.buf = buf;
    Cia402V.pack_command_args.cap = sizeof(buf);
    Cia402V.pack_command_args.controlword = 0x000Fu;
    Cia402V.pack_command_args.target = -1000;
    Cia402V.pack_command(cia402_work);
    size_t n = Cia402V.n;
    TEST_ASSERT_EQUAL_size_t(6u, n);
    static const uint8_t WANT[6] = {0x0F, 0x00, 0x18, 0xFC, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, buf, 6);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, buf[6]);

    uint16_t sw = 0;
    int32_t actual = 0;
    Cia402V.unpack_status_args.buf = buf;
    Cia402V.unpack_status_args.len = n;
    Cia402V.unpack_status_args.statusword = &sw;
    Cia402V.unpack_status_args.actual = &actual;
    Cia402V.unpack_status(cia402_work);
    TEST_ASSERT_TRUE(Cia402V.ok);
    TEST_ASSERT_EQUAL_HEX16(0x000Fu, sw);
    TEST_ASSERT_EQUAL_INT32(-1000, actual);

    // A real cyclic exchange: Statusword 0x0637 with a position of 2147483647.
    static const uint8_t TPDO[6] = {0x37, 0x06, 0xFF, 0xFF, 0xFF, 0x7F};
    Cia402V.unpack_status_args.buf = TPDO;
    Cia402V.unpack_status_args.len = sizeof(TPDO);
    Cia402V.unpack_status_args.statusword = &sw;
    Cia402V.unpack_status_args.actual = &actual;
    Cia402V.unpack_status(cia402_work);
    TEST_ASSERT_TRUE(Cia402V.ok);
    TEST_ASSERT_EQUAL_HEX16(0x0637u, sw);
    TEST_ASSERT_EQUAL_INT32(2147483647, actual);
    Cia402V.state_args.statusword = sw;
    Cia402V.state(cia402_work);
    TEST_ASSERT_EQUAL_INT(CIA402_STATE_OPERATION_ENABLED, Cia402V.value);
    TEST_ASSERT_TRUE(protocore_cia402_target_reached(sw));

    Cia402V.pack_command_args.buf = buf;
    Cia402V.pack_command_args.cap = 5;
    Cia402V.pack_command_args.controlword = 0x000Fu;
    Cia402V.pack_command_args.target = 0;
    Cia402V.pack_command(cia402_work);
    TEST_ASSERT_EQUAL_size_t(0u, Cia402V.n);
    Cia402V.pack_command_args.buf = NULL;
    Cia402V.pack_command_args.cap = sizeof(buf);
    Cia402V.pack_command_args.controlword = 0x000Fu;
    Cia402V.pack_command_args.target = 0;
    Cia402V.pack_command(cia402_work);
    TEST_ASSERT_EQUAL_size_t(0u, Cia402V.n);
    Cia402V.unpack_status_args.buf = TPDO;
    Cia402V.unpack_status_args.len = 5;
    Cia402V.unpack_status_args.statusword = &sw;
    Cia402V.unpack_status_args.actual = &actual;
    Cia402V.unpack_status(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);
    Cia402V.unpack_status_args.buf = NULL;
    Cia402V.unpack_status_args.len = 6;
    Cia402V.unpack_status_args.statusword = &sw;
    Cia402V.unpack_status_args.actual = &actual;
    Cia402V.unpack_status(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);
    Cia402V.unpack_status_args.buf = TPDO;
    Cia402V.unpack_status_args.len = 6;
    Cia402V.unpack_status_args.statusword = NULL;
    Cia402V.unpack_status_args.actual = &actual;
    Cia402V.unpack_status(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);
    Cia402V.unpack_status_args.buf = TPDO;
    Cia402V.unpack_status_args.len = 6;
    Cia402V.unpack_status_args.statusword = &sw;
    Cia402V.unpack_status_args.actual = NULL;
    Cia402V.unpack_status(cia402_work);
    TEST_ASSERT_FALSE(Cia402V.ok);
}
