// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the CANopen (CiA 301) message codec (services/fieldbus/canopen/canopen.h).
//
// The load-bearing case is test_expedited_sdo_upload_of_the_identity_vendor_id. CiA 301's
// pre-defined connection set fixes the COB-ID as function code plus node id, and section 7.2.4
// fixes the SDO command octet bit for bit: command specifier in bits 7..5, n (unused data octets)
// in bits 3..2, e (expedited) in bit 1, s (size indicated) in bit 0, with the object index
// little-endian in data[1..2] and the sub-index in data[3]. Reading object 0x1018 sub 1 - the
// Identity Object's Vendor-ID, an object every compliant node carries - exercises every one of
// those fields at once, and each expected octet below is assembled from the bit layout rather than
// read out of the encoder.

#include "services/fieldbus/canopen/canopen.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// The pre-defined connection set: each function code is a fixed 4-bit value in the 11-bit id, and
// the low 7 bits are the node id.
void test_predefined_connection_set(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x000u, CANOPEN_COB_NMT);
    TEST_ASSERT_EQUAL_HEX32(0x080u, CANOPEN_COB_SYNC);
    TEST_ASSERT_EQUAL_HEX32(0x080u, CANOPEN_COB_EMCY);
    TEST_ASSERT_EQUAL_HEX32(0x100u, CANOPEN_COB_TIME);
    TEST_ASSERT_EQUAL_HEX32(0x180u, CANOPEN_COB_TPDO1);
    TEST_ASSERT_EQUAL_HEX32(0x200u, CANOPEN_COB_RPDO1);
    TEST_ASSERT_EQUAL_HEX32(0x280u, CANOPEN_COB_TPDO2);
    TEST_ASSERT_EQUAL_HEX32(0x300u, CANOPEN_COB_RPDO2);
    TEST_ASSERT_EQUAL_HEX32(0x380u, CANOPEN_COB_TPDO3);
    TEST_ASSERT_EQUAL_HEX32(0x400u, CANOPEN_COB_RPDO3);
    TEST_ASSERT_EQUAL_HEX32(0x480u, CANOPEN_COB_TPDO4);
    TEST_ASSERT_EQUAL_HEX32(0x500u, CANOPEN_COB_RPDO4);
    TEST_ASSERT_EQUAL_HEX32(0x580u, CANOPEN_COB_SDO_TX);
    TEST_ASSERT_EQUAL_HEX32(0x600u, CANOPEN_COB_SDO_RX);
    TEST_ASSERT_EQUAL_HEX32(0x700u, CANOPEN_COB_HEARTBEAT);
    // The two masks partition the 11-bit identifier with nothing left over.
    TEST_ASSERT_EQUAL_HEX32(0x7FFu, CANOPEN_FUNC_MASK | CANOPEN_NODE_MASK);
    TEST_ASSERT_EQUAL_HEX32(0u, CANOPEN_FUNC_MASK & CANOPEN_NODE_MASK);
    // The bases are spaced one function code apart: 0x080 per step.
    TEST_ASSERT_EQUAL_HEX32(0x080u, CANOPEN_COB_RPDO1 - CANOPEN_COB_TPDO1);
    TEST_ASSERT_EQUAL_HEX32(0x080u, CANOPEN_COB_SDO_RX - CANOPEN_COB_SDO_TX);
}

// The request is COB-ID 0x600 + node with command 0x40 (ccs 2 = initiate upload, n/e/s all zero on
// a request); the response is COB-ID 0x580 + node with command
//   (2 << 5) | (n << 2) | (e << 1) | s  with n = 4 - 4 = 0, e = 1, s = 1  =  0x43,
// carrying the four data octets little-endian.
void test_expedited_sdo_upload_of_the_identity_vendor_id(void)
{
    CanFrame req;
    TEST_ASSERT_TRUE(protocore_canopen_build_sdo_read(&req, 5, 0x1018u, 1));
    TEST_ASSERT_EQUAL_HEX32(0x605u, req.id);
    TEST_ASSERT_FALSE(req.extended);
    TEST_ASSERT_EQUAL_UINT8(8u, req.dlc);
    static const uint8_t WANT[8] = {0x40, 0x18, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, req.data, 8);

    CanFrame resp;
    resp.id = CANOPEN_COB_SDO_TX + 5;
    resp.extended = PROTO_FALSE;
    resp.rtr = PROTO_FALSE;
    resp.dlc = 8;
    static const uint8_t RESP[8] = {0x43, 0x18, 0x10, 0x01, 0x9A, 0x02, 0x00, 0x00}; // vendor id 0x0000029A
    memcpy(resp.data, RESP, 8);

    CanopenSdoResponse out;
    TEST_ASSERT_TRUE(protocore_canopen_parse_sdo_response(&resp, &out));
    TEST_ASSERT_EQUAL_HEX16(0x1018u, out.index);
    TEST_ASSERT_EQUAL_UINT8(1u, out.sub);
    TEST_ASSERT_FALSE(out.is_abort);
    TEST_ASSERT_TRUE(out.is_upload);
    TEST_ASSERT_TRUE(out.expedited);
    TEST_ASSERT_EQUAL_UINT8(4u, out.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(RESP + 4, out.data, 4);
}

// The n field counts the UNUSED octets of data[4..7], so a shorter payload raises n and the reader
// must subtract it from 4. Command = (1 << 5) | ((4 - len) << 2) | 0x03 on a download request.
void test_sdo_expedited_download_encodes_the_unused_octet_count(void)
{
    struct
    {
        uint8_t len;
        uint8_t cmd;
    } static const CASES[] = {
        {1, 0x2F}, // n = 3: 0x20 | (3 << 2) | 0x03
        {2, 0x2B}, // n = 2
        {3, 0x27}, // n = 1
        {4, 0x23}, // n = 0
    };
    static const uint8_t DATA[4] = {0x11, 0x22, 0x33, 0x44};
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        CanFrame f;
        TEST_ASSERT_TRUE(protocore_canopen_build_sdo_write(&f, 0x7F, 0x6040u, 0, DATA, CASES[i].len));
        TEST_ASSERT_EQUAL_HEX32(0x600u + 0x7Fu, f.id);
        TEST_ASSERT_EQUAL_HEX8(CASES[i].cmd, f.data[0]);
        TEST_ASSERT_EQUAL_HEX8(0x40u, f.data[1]); // 0x6040 little-endian
        TEST_ASSERT_EQUAL_HEX8(0x60u, f.data[2]);
        TEST_ASSERT_EQUAL_UINT8(0u, f.data[3]);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, f.data + 4, CASES[i].len);
    }

    // The upload response side reads the same n back out.
    for (uint8_t len = 1; len <= 4; len++)
    {
        CanFrame f;
        f.id = CANOPEN_COB_SDO_TX + 1;
        f.extended = PROTO_FALSE;
        f.rtr = PROTO_FALSE;
        f.dlc = 8;
        memset(f.data, 0, 8);
        f.data[0] = (uint8_t)(0x40u | (((uint8_t)(4u - len)) << 2) | 0x03u);
        f.data[4] = 0xAA;
        CanopenSdoResponse out;
        TEST_ASSERT_TRUE(protocore_canopen_parse_sdo_response(&f, &out));
        TEST_ASSERT_TRUE(out.expedited);
        TEST_ASSERT_EQUAL_UINT8(len, out.len);
    }

    CanFrame f;
    TEST_ASSERT_FALSE(protocore_canopen_build_sdo_write(&f, 1, 0x1000u, 0, DATA, 0)); // expedited carries 1..4
    TEST_ASSERT_FALSE(protocore_canopen_build_sdo_write(&f, 1, 0x1000u, 0, DATA, 5));
    TEST_ASSERT_FALSE(protocore_canopen_build_sdo_write(&f, 1, 0x1000u, 0, NULL, 2));
    TEST_ASSERT_FALSE(protocore_canopen_build_sdo_read(&f, 0, 0x1000u, 0));   // node 0 is not an SDO server
    TEST_ASSERT_FALSE(protocore_canopen_build_sdo_read(&f, 128, 0x1000u, 0)); // node ids are 1..127
}

// An abort is command specifier 4 (0x80) with the 32-bit abort code little-endian in data[4..7].
void test_sdo_abort_carries_the_code_little_endian(void)
{
    CanFrame f;
    TEST_ASSERT_TRUE(protocore_canopen_build_sdo_abort(&f, 3, 0x1018u, 9, CANOPEN_ABORT_NO_SUBINDEX, PROTO_FALSE));
    TEST_ASSERT_EQUAL_HEX32(0x583u, f.id);                                           // server -> client
    static const uint8_t WANT[8] = {0x80, 0x18, 0x10, 0x09, 0x11, 0x00, 0x09, 0x06}; // 0x06090011
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, f.data, 8);

    CanopenSdoResponse out;
    TEST_ASSERT_TRUE(protocore_canopen_parse_sdo_response(&f, &out));
    TEST_ASSERT_TRUE(out.is_abort);
    TEST_ASSERT_EQUAL_HEX32(CANOPEN_ABORT_NO_SUBINDEX, out.abort_code);
    TEST_ASSERT_EQUAL_HEX16(0x1018u, out.index);
    TEST_ASSERT_EQUAL_UINT8(9u, out.sub);
    TEST_ASSERT_EQUAL_UINT8(0u, out.len);

    // to_server picks the client -> server COB-ID instead.
    TEST_ASSERT_TRUE(protocore_canopen_build_sdo_abort(&f, 3, 0x1018u, 9, CANOPEN_ABORT_NO_OBJECT, PROTO_TRUE));
    TEST_ASSERT_EQUAL_HEX32(0x603u, f.id);
    TEST_ASSERT_EQUAL_HEX8(0x00u, f.data[4]); // 0x06020000
    TEST_ASSERT_EQUAL_HEX8(0x02u, f.data[6]);
    TEST_ASSERT_EQUAL_HEX8(0x06u, f.data[7]);
}

// The download-initiate acknowledgement is command specifier 3 (0x60) and carries no data.
void test_sdo_download_acknowledgement(void)
{
    CanFrame f;
    f.id = CANOPEN_COB_SDO_TX + 10;
    f.extended = PROTO_FALSE;
    f.rtr = PROTO_FALSE;
    f.dlc = 8;
    static const uint8_t ACK[8] = {0x60, 0x40, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00};
    memcpy(f.data, ACK, 8);

    CanopenSdoResponse out;
    TEST_ASSERT_TRUE(protocore_canopen_parse_sdo_response(&f, &out));
    TEST_ASSERT_FALSE(out.is_abort);
    TEST_ASSERT_FALSE(out.is_upload);
    TEST_ASSERT_FALSE(out.expedited);
    TEST_ASSERT_EQUAL_HEX16(0x6040u, out.index);
    TEST_ASSERT_EQUAL_UINT8(0u, out.len);

    // A command specifier the server never sends (1 = client download) is refused.
    f.data[0] = 0x20;
    TEST_ASSERT_FALSE(protocore_canopen_parse_sdo_response(&f, &out));

    // An SDO response must arrive on 0x580 + node with a nonzero node.
    f.data[0] = 0x60;
    f.id = CANOPEN_COB_SDO_RX + 10;
    TEST_ASSERT_FALSE(protocore_canopen_parse_sdo_response(&f, &out));
    f.id = CANOPEN_COB_SDO_TX;
    TEST_ASSERT_FALSE(protocore_canopen_parse_sdo_response(&f, &out));
    f.id = CANOPEN_COB_SDO_TX + 10;
    f.dlc = 7;
    TEST_ASSERT_FALSE(protocore_canopen_parse_sdo_response(&f, &out));
}

// NMT node control is COB-ID 0 with two octets: the command, then the addressed node (0 = all).
void test_nmt_node_control(void)
{
    CanFrame f;
    TEST_ASSERT_TRUE(protocore_canopen_build_nmt(&f, CANOPEN_NMT_START, 0));
    TEST_ASSERT_EQUAL_HEX32(0x000u, f.id);
    TEST_ASSERT_EQUAL_UINT8(2u, f.dlc);
    TEST_ASSERT_EQUAL_HEX8(0x01u, f.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0u, f.data[1]);

    TEST_ASSERT_TRUE(protocore_canopen_build_nmt(&f, CANOPEN_NMT_RESET_COMM, 42));
    TEST_ASSERT_EQUAL_HEX8(0x82u, f.data[0]);
    TEST_ASSERT_EQUAL_UINT8(42u, f.data[1]);

    TEST_ASSERT_EQUAL_HEX8(0x01u, CANOPEN_NMT_START);
    TEST_ASSERT_EQUAL_HEX8(0x02u, CANOPEN_NMT_STOP);
    TEST_ASSERT_EQUAL_HEX8(0x80u, CANOPEN_NMT_PRE_OP);
    TEST_ASSERT_EQUAL_HEX8(0x81u, CANOPEN_NMT_RESET_NODE);
    TEST_ASSERT_EQUAL_HEX8(0x82u, CANOPEN_NMT_RESET_COMM);

    TEST_ASSERT_FALSE(protocore_canopen_build_nmt(&f, CANOPEN_NMT_START, 128));
    TEST_ASSERT_FALSE(protocore_canopen_build_nmt(NULL, CANOPEN_NMT_START, 1));

    CanopenMsg m;
    TEST_ASSERT_TRUE(protocore_canopen_parse(&f, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_NMT, m.type);
    TEST_ASSERT_EQUAL_UINT8(0u, m.node_id);
}

// SYNC is the 0x080 function code with node 0 and no data; EMCY is the same function code with a
// nonzero node, so classification turns on the node bits alone.
void test_sync_and_emcy_share_a_function_code(void)
{
    CanFrame sync;
    TEST_ASSERT_TRUE(protocore_canopen_build_sync(&sync));
    TEST_ASSERT_EQUAL_HEX32(0x080u, sync.id);
    TEST_ASSERT_EQUAL_UINT8(0u, sync.dlc);

    CanopenMsg m;
    TEST_ASSERT_TRUE(protocore_canopen_parse(&sync, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_SYNC, m.type);
    TEST_ASSERT_EQUAL_UINT8(0u, m.node_id);

    static const uint8_t MSEF[5] = {0x01, 0x02, 0x03, 0x04, 0x05};
    CanFrame emcy;
    // 0x8130 = the "communication error / bus off" emergency error code
    TEST_ASSERT_TRUE(protocore_canopen_build_emcy(&emcy, 4, 0x8130u, 0x11, MSEF));
    TEST_ASSERT_EQUAL_HEX32(0x084u, emcy.id);
    TEST_ASSERT_EQUAL_UINT8(8u, emcy.dlc);
    TEST_ASSERT_EQUAL_HEX8(0x30u, emcy.data[0]); // error code little-endian
    TEST_ASSERT_EQUAL_HEX8(0x81u, emcy.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x11u, emcy.data[2]); // object 0x1001 error register
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MSEF, emcy.data + 3, 5);

    TEST_ASSERT_TRUE(protocore_canopen_parse(&emcy, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_EMCY, m.type);
    TEST_ASSERT_EQUAL_UINT8(4u, m.node_id);

    uint8_t node = 0, reg = 0;
    uint16_t code = 0;
    uint8_t got[5];
    TEST_ASSERT_TRUE(protocore_canopen_parse_emcy(&emcy, &node, &code, &reg, got));
    TEST_ASSERT_EQUAL_UINT8(4u, node);
    TEST_ASSERT_EQUAL_HEX16(0x8130u, code);
    TEST_ASSERT_EQUAL_HEX8(0x11u, reg);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MSEF, got, 5);

    // A SYNC frame is never an EMCY, whatever its length.
    sync.dlc = 8;
    memset(sync.data, 0, 8);
    TEST_ASSERT_FALSE(protocore_canopen_parse_emcy(&sync, &node, &code, &reg, got));
    TEST_ASSERT_FALSE(protocore_canopen_build_emcy(&emcy, 0, 0, 0, MSEF));
}

// The heartbeat is 0x700 + node with one octet of NMT state; bit 7 is the boot toggle and is masked
// off, so a toggled Operational still reads as Operational.
void test_heartbeat_state_and_toggle_bit(void)
{
    CanFrame f;
    TEST_ASSERT_TRUE(protocore_canopen_build_heartbeat(&f, 0x7F, CANOPEN_STATE_OPERATIONAL));
    TEST_ASSERT_EQUAL_HEX32(0x77Fu, f.id); // 0x700 + 127
    TEST_ASSERT_EQUAL_UINT8(1u, f.dlc);
    TEST_ASSERT_EQUAL_HEX8(0x05u, f.data[0]);

    uint8_t node = 0, state = 0xFF;
    TEST_ASSERT_TRUE(protocore_canopen_parse_heartbeat(&f, &node, &state));
    TEST_ASSERT_EQUAL_UINT8(0x7Fu, node);
    TEST_ASSERT_EQUAL_HEX8(CANOPEN_STATE_OPERATIONAL, state);

    f.data[0] = (uint8_t)(0x80u | CANOPEN_STATE_OPERATIONAL);
    TEST_ASSERT_TRUE(protocore_canopen_parse_heartbeat(&f, &node, &state));
    TEST_ASSERT_EQUAL_HEX8(CANOPEN_STATE_OPERATIONAL, state);

    // Boot-up is the state-0 heartbeat a node emits on entering Pre-operational.
    TEST_ASSERT_TRUE(protocore_canopen_build_heartbeat(&f, 1, CANOPEN_STATE_BOOTUP));
    TEST_ASSERT_TRUE(protocore_canopen_parse_heartbeat(&f, &node, &state));
    TEST_ASSERT_EQUAL_HEX8(0x00u, state);

    TEST_ASSERT_EQUAL_HEX8(0x00u, CANOPEN_STATE_BOOTUP);
    TEST_ASSERT_EQUAL_HEX8(0x04u, CANOPEN_STATE_STOPPED);
    TEST_ASSERT_EQUAL_HEX8(0x05u, CANOPEN_STATE_OPERATIONAL);
    TEST_ASSERT_EQUAL_HEX8(0x7Fu, CANOPEN_STATE_PRE_OP);

    TEST_ASSERT_FALSE(protocore_canopen_build_heartbeat(&f, 0, CANOPEN_STATE_OPERATIONAL));
    TEST_ASSERT_FALSE(protocore_canopen_build_heartbeat(&f, 128, CANOPEN_STATE_OPERATIONAL));

    CanFrame other = f;
    other.id = CANOPEN_COB_SDO_TX + 1;
    TEST_ASSERT_FALSE(protocore_canopen_parse_heartbeat(&other, &node, &state));
    other.id = CANOPEN_COB_HEARTBEAT; // node 0 is not a heartbeat producer
    TEST_ASSERT_FALSE(protocore_canopen_parse_heartbeat(&other, &node, &state));
}

// TIME_OF_DAY is a 28-bit millisecond-after-midnight field plus 16 bits of days since 1984-01-01,
// both little-endian. The reserved top 4 bits of the millisecond word are masked off on both sides.
//
// 2000-01-01 is 16 years after the CANopen epoch, of which 1984, 1988, 1992 and 1996 are leap:
//   16 * 365 + 4 = 5844 days.
// The largest legal instant is 86400000 - 1 = 86399999 ms = 0x05265BFF, which fits the 28 bits.
void test_time_of_day(void)
{
    CanFrame f;
    TEST_ASSERT_TRUE(protocore_canopen_build_time(&f, 86399999u, 5844u));
    TEST_ASSERT_EQUAL_HEX32(0x100u, f.id);
    TEST_ASSERT_EQUAL_UINT8(CANOPEN_TIME_LEN, f.dlc);
    static const uint8_t WANT[6] = {0xFF, 0x5B, 0x26, 0x05, 0xD4, 0x16}; // 5844 = 0x16D4
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, f.data, 6);

    CanopenTime t;
    TEST_ASSERT_TRUE(protocore_canopen_parse_time(&f, &t));
    TEST_ASSERT_EQUAL_UINT32(86399999u, t.ms_since_midnight);
    TEST_ASSERT_EQUAL_UINT16(5844u, t.days_since_1984);

    // The four reserved bits never reach the wire, and never come back off it.
    TEST_ASSERT_TRUE(protocore_canopen_build_time(&f, 0xF0000001u, 1));
    TEST_ASSERT_EQUAL_HEX8(0x00u, f.data[3]);
    TEST_ASSERT_TRUE(protocore_canopen_parse_time(&f, &t));
    TEST_ASSERT_EQUAL_UINT32(1u, t.ms_since_midnight);

    f.data[3] = 0xF0; // a peer that left them set
    TEST_ASSERT_TRUE(protocore_canopen_parse_time(&f, &t));
    TEST_ASSERT_EQUAL_UINT32(1u, t.ms_since_midnight);

    CanopenMsg m;
    TEST_ASSERT_TRUE(protocore_canopen_parse(&f, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_TIME, m.type);

    f.dlc = 5; // shorter than the TIME_OF_DAY structure
    TEST_ASSERT_FALSE(protocore_canopen_parse_time(&f, &t));
    f.dlc = CANOPEN_TIME_LEN;
    f.id = CANOPEN_COB_SYNC;
    TEST_ASSERT_FALSE(protocore_canopen_parse_time(&f, &t));
}

// Each PDO number has its own base and the classifier reports which one a frame belongs to.
void test_pdo_bases_and_classification(void)
{
    static const uint32_t TX[4] = {CANOPEN_COB_TPDO1, CANOPEN_COB_TPDO2, CANOPEN_COB_TPDO3, CANOPEN_COB_TPDO4};
    static const uint32_t RX[4] = {CANOPEN_COB_RPDO1, CANOPEN_COB_RPDO2, CANOPEN_COB_RPDO3, CANOPEN_COB_RPDO4};
    static const uint8_t DATA[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    for (uint8_t n = 1; n <= 4; n++)
    {
        CanFrame f;
        CanopenMsg m;
        TEST_ASSERT_TRUE(protocore_canopen_build_tpdo(&f, n, 9, DATA, 8));
        TEST_ASSERT_EQUAL_HEX32(TX[n - 1] + 9u, f.id);
        TEST_ASSERT_EQUAL_UINT8(8u, f.dlc);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, f.data, 8);
        TEST_ASSERT_TRUE(protocore_canopen_parse(&f, &m));
        TEST_ASSERT_EQUAL_INT(CANOPEN_T_TPDO, m.type);
        TEST_ASSERT_EQUAL_UINT8(n, m.pdo_num);
        TEST_ASSERT_EQUAL_UINT8(9u, m.node_id);

        TEST_ASSERT_TRUE(protocore_canopen_build_rpdo(&f, n, 9, DATA, 2));
        TEST_ASSERT_EQUAL_HEX32(RX[n - 1] + 9u, f.id);
        TEST_ASSERT_EQUAL_UINT8(2u, f.dlc);
        TEST_ASSERT_TRUE(protocore_canopen_parse(&f, &m));
        TEST_ASSERT_EQUAL_INT(CANOPEN_T_RPDO, m.type);
        TEST_ASSERT_EQUAL_UINT8(n, m.pdo_num);
    }

    CanFrame f;
    TEST_ASSERT_FALSE(protocore_canopen_build_tpdo(&f, 0, 9, DATA, 8)); // PDO numbers are 1..4
    TEST_ASSERT_FALSE(protocore_canopen_build_tpdo(&f, 5, 9, DATA, 8));
    TEST_ASSERT_FALSE(protocore_canopen_build_tpdo(&f, 1, 9, DATA, 9)); // classic CAN carries 8
    TEST_ASSERT_FALSE(protocore_canopen_build_rpdo(&f, 1, 0, DATA, 8));
    TEST_ASSERT_FALSE(protocore_canopen_build_rpdo(&f, 1, 9, NULL, 4));
    // A zero-length PDO is legal.
    TEST_ASSERT_TRUE(protocore_canopen_build_tpdo(&f, 1, 9, NULL, 0));
    TEST_ASSERT_EQUAL_UINT8(0u, f.dlc);
}

// The default profile is 11-bit standard frames only, and an unassigned function code stays
// unclassified rather than being forced into the nearest one.
void test_classifier_rejects_extended_and_unknown(void)
{
    CanFrame f;
    CanopenMsg m;
    TEST_ASSERT_TRUE(protocore_canopen_build_heartbeat(&f, 1, CANOPEN_STATE_OPERATIONAL));
    f.extended = PROTO_TRUE;
    TEST_ASSERT_FALSE(protocore_canopen_parse(&f, &m));
    f.extended = PROTO_FALSE;
    TEST_ASSERT_TRUE(protocore_canopen_parse(&f, &m));

    f.id = 0x781u; // function code 0x780 is not in the pre-defined connection set
    TEST_ASSERT_TRUE(protocore_canopen_parse(&f, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_UNKNOWN, m.type);

    f.id = 0x180u; // a PDO base with node 0 addresses nobody
    TEST_ASSERT_TRUE(protocore_canopen_parse(&f, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_UNKNOWN, m.type);

    TEST_ASSERT_FALSE(protocore_canopen_parse(NULL, &m));
    TEST_ASSERT_FALSE(protocore_canopen_parse(&f, NULL));
}

// A segmented transfer starts with an initiate naming the object and the total size (e = 0, s = 1,
// so the command is (1 << 5) | 1 = 0x21) and the size little-endian in data[4..7].
void test_segmented_download_initiate(void)
{
    CanFrame f;
    TEST_ASSERT_TRUE(protocore_canopen_build_sdo_download_init(&f, 2, 0x1008u, 0, 17));
    TEST_ASSERT_EQUAL_HEX32(0x602u, f.id);
    static const uint8_t WANT[8] = {0x21, 0x08, 0x10, 0x00, 0x11, 0x00, 0x00, 0x00}; // 17 = 0x11
    TEST_ASSERT_EQUAL_HEX8_ARRAY(WANT, f.data, 8);
    TEST_ASSERT_FALSE(protocore_canopen_build_sdo_download_init(&f, 0, 0x1008u, 0, 17));
}

// A segment octet is  t (bit 4) | n (bits 3..1) | c (bit 0), where n counts the UNUSED octets of
// data[1..7], so a full 7-octet segment has n = 0 and a 3-octet final segment has n = 4:
//   (4 << 1) | 1 = 0x09.
void test_segment_command_octet_layout(void)
{
    static const uint8_t SEVEN[7] = {1, 2, 3, 4, 5, 6, 7};
    CanFrame f;

    TEST_ASSERT_TRUE(protocore_canopen_build_sdo_download_segment(&f, 2, PROTO_FALSE, SEVEN, 7, PROTO_FALSE));
    TEST_ASSERT_EQUAL_HEX8(0x00u, f.data[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SEVEN, f.data + 1, 7);

    TEST_ASSERT_TRUE(protocore_canopen_build_sdo_download_segment(&f, 2, PROTO_TRUE, SEVEN, 7, PROTO_FALSE));
    TEST_ASSERT_EQUAL_HEX8(0x10u, f.data[0]);

    TEST_ASSERT_TRUE(protocore_canopen_build_sdo_download_segment(&f, 2, PROTO_FALSE, SEVEN, 3, PROTO_TRUE));
    TEST_ASSERT_EQUAL_HEX8(0x09u, f.data[0]);

    proto_bool toggle = PROTO_TRUE, last = PROTO_FALSE;
    uint8_t data[7], len = 0;
    TEST_ASSERT_TRUE(protocore_canopen_parse_sdo_segment(&f, &toggle, data, &len, &last));
    TEST_ASSERT_FALSE(toggle);
    TEST_ASSERT_TRUE(last);
    TEST_ASSERT_EQUAL_UINT8(3u, len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SEVEN, data, 3);

    // An upload segment request is command specifier 3, so it is not a segment frame.
    TEST_ASSERT_TRUE(protocore_canopen_build_sdo_upload_segment_req(&f, 2, PROTO_TRUE));
    TEST_ASSERT_EQUAL_HEX8(0x70u, f.data[0]); // (3 << 5) | 0x10
    TEST_ASSERT_FALSE(protocore_canopen_parse_sdo_segment(&f, &toggle, data, &len, &last));
    TEST_ASSERT_TRUE(protocore_canopen_build_sdo_upload_segment_req(&f, 2, PROTO_FALSE));
    TEST_ASSERT_EQUAL_HEX8(0x60u, f.data[0]);

    TEST_ASSERT_FALSE(protocore_canopen_build_sdo_download_segment(&f, 2, PROTO_FALSE, SEVEN, 0, PROTO_FALSE));
    TEST_ASSERT_FALSE(protocore_canopen_build_sdo_download_segment(&f, 2, PROTO_FALSE, SEVEN, 8, PROTO_FALSE));
    TEST_ASSERT_FALSE(protocore_canopen_build_sdo_download_segment(&f, 2, PROTO_FALSE, NULL, 4, PROTO_FALSE));
    TEST_ASSERT_FALSE(protocore_canopen_build_sdo_upload_segment_req(&f, 128, PROTO_FALSE));
}

// Reassembly accepts segments only while the toggle alternates 0,1,0,1 and stops on the last one.
// 17 octets arrive as 7 + 7 + 3, matching the size the initiate declared.
void test_segmented_upload_reassembly(void)
{
    static const uint8_t SRC[17] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint8_t buf[32];
    CanopenSdoReasm r;
    protocore_canopen_sdo_reasm_init(&r, buf, sizeof(buf));
    TEST_ASSERT_FALSE(r.expect_toggle); // the first segment carries toggle 0
    TEST_ASSERT_FALSE(r.done);

    TEST_ASSERT_TRUE(protocore_canopen_sdo_reasm_feed(&r, SRC, 7, PROTO_FALSE, PROTO_FALSE));
    TEST_ASSERT_TRUE(protocore_canopen_sdo_reasm_feed(&r, SRC + 7, 7, PROTO_TRUE, PROTO_FALSE));
    TEST_ASSERT_TRUE(protocore_canopen_sdo_reasm_feed(&r, SRC + 14, 3, PROTO_FALSE, PROTO_TRUE));
    TEST_ASSERT_TRUE(r.done);
    TEST_ASSERT_EQUAL_size_t(17u, r.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(SRC, buf, 17);

    // Nothing is accepted after the last segment.
    TEST_ASSERT_FALSE(protocore_canopen_sdo_reasm_feed(&r, SRC, 1, PROTO_TRUE, PROTO_FALSE));

    // A repeated toggle is the CiA 301 "toggle bit not alternated" condition and is refused.
    protocore_canopen_sdo_reasm_init(&r, buf, sizeof(buf));
    TEST_ASSERT_TRUE(protocore_canopen_sdo_reasm_feed(&r, SRC, 7, PROTO_FALSE, PROTO_FALSE));
    TEST_ASSERT_FALSE(protocore_canopen_sdo_reasm_feed(&r, SRC + 7, 7, PROTO_FALSE, PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(7u, r.len); // the refused segment did not land
    TEST_ASSERT_EQUAL_HEX32(0x05030000u, CANOPEN_ABORT_TOGGLE);

    // A segment that will not fit is refused rather than truncated into the buffer.
    uint8_t tiny[8];
    protocore_canopen_sdo_reasm_init(&r, tiny, sizeof(tiny));
    TEST_ASSERT_TRUE(protocore_canopen_sdo_reasm_feed(&r, SRC, 7, PROTO_FALSE, PROTO_FALSE));
    TEST_ASSERT_FALSE(protocore_canopen_sdo_reasm_feed(&r, SRC + 7, 7, PROTO_TRUE, PROTO_FALSE));
    TEST_ASSERT_EQUAL_size_t(7u, r.len);

    protocore_canopen_sdo_reasm_init(&r, NULL, 0);
    TEST_ASSERT_FALSE(protocore_canopen_sdo_reasm_feed(&r, SRC, 1, PROTO_FALSE, PROTO_FALSE));
    protocore_canopen_sdo_reasm_init(NULL, buf, sizeof(buf)); // must not fault
}
