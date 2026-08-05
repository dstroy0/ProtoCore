// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the CANopen (CiA 301) message codec (services/fieldbus/canopen): NMT, SYNC,
// heartbeat, EMCY, PDO, and expedited SDO read/write/abort + response parsing, plus the
// COB-ID classifier. Values checked against the CiA 301 default identifier allocation.
// Pure host tests.

#include "services/fieldbus/canopen/canopen.h"
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// NMT "start node 5": COB-ID 0, [command, node].
void test_nmt_start_node()
{
    CanFrame f;
    TEST_ASSERT_TRUE(pc_canopen_build_nmt(&f, CANOPEN_NMT_START, 5));
    TEST_ASSERT_EQUAL_UINT32(0x000, f.id);
    TEST_ASSERT_FALSE(f.extended);
    TEST_ASSERT_EQUAL_UINT8(2, f.dlc);
    TEST_ASSERT_EQUAL_HEX8(0x01, f.data[0]);
    TEST_ASSERT_EQUAL_HEX8(5, f.data[1]);

    // node 0 = all nodes is allowed; node 200 is out of range.
    TEST_ASSERT_TRUE(pc_canopen_build_nmt(&f, CANOPEN_NMT_RESET_NODE, 0));
    TEST_ASSERT_FALSE(pc_canopen_build_nmt(&f, CANOPEN_NMT_START, 200));
}

void test_sync()
{
    CanFrame f;
    TEST_ASSERT_TRUE(pc_canopen_build_sync(&f));
    TEST_ASSERT_EQUAL_UINT32(0x080, f.id);
    TEST_ASSERT_EQUAL_UINT8(0, f.dlc);

    CanopenMsg m;
    TEST_ASSERT_TRUE(pc_canopen_parse(&f, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_SYNC, m.type);
    TEST_ASSERT_EQUAL_UINT8(0, m.node_id);
}

void test_heartbeat_roundtrip()
{
    CanFrame f;
    TEST_ASSERT_TRUE(pc_canopen_build_heartbeat(&f, 10, CANOPEN_STATE_OPERATIONAL));
    TEST_ASSERT_EQUAL_UINT32(0x70A, f.id); // 0x700 + 10
    TEST_ASSERT_EQUAL_UINT8(1, f.dlc);

    CanopenMsg m;
    TEST_ASSERT_TRUE(pc_canopen_parse(&f, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_HEARTBEAT, m.type);
    TEST_ASSERT_EQUAL_UINT8(10, m.node_id);

    uint8_t node = 0, state = 0;
    TEST_ASSERT_TRUE(pc_canopen_parse_heartbeat(&f, &node, &state));
    TEST_ASSERT_EQUAL_UINT8(10, node);
    TEST_ASSERT_EQUAL_HEX8(CANOPEN_STATE_OPERATIONAL, state);
}

void test_time_roundtrip()
{
    CanFrame f;
    // 12:34:56.789 -> 45296789 ms after midnight; day 15545 since 1984-01-01.
    TEST_ASSERT_TRUE(pc_canopen_build_time(&f, 45296789u, 15545));
    TEST_ASSERT_EQUAL_UINT32(0x100, f.id); // TIME COB, broadcast
    TEST_ASSERT_EQUAL_UINT8(6, f.dlc);

    CanopenMsg m;
    TEST_ASSERT_TRUE(pc_canopen_parse(&f, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_TIME, m.type);

    CanopenTime t;
    TEST_ASSERT_TRUE(pc_canopen_parse_time(&f, &t));
    TEST_ASSERT_EQUAL_UINT32(45296789u, t.ms_since_midnight);
    TEST_ASSERT_EQUAL_UINT16(15545, t.days_since_1984);

    // The reserved top 4 bits of the ms field are ignored by both build (masked) and parse.
    f.data[3] |= 0xF0u; // set the reserved nibble on the wire
    TEST_ASSERT_TRUE(pc_canopen_parse_time(&f, &t));
    TEST_ASSERT_EQUAL_UINT32(45296789u, t.ms_since_midnight); // still masked cleanly

    // A non-TIME COB (SYNC), a short frame, and null args are rejected.
    CanFrame sync;
    pc_canopen_build_sync(&sync);
    TEST_ASSERT_FALSE(pc_canopen_parse_time(&sync, &t));
    f.dlc = 5;
    TEST_ASSERT_FALSE(pc_canopen_parse_time(&f, &t));
    TEST_ASSERT_FALSE(pc_canopen_parse_time(NULL, &t));
    TEST_ASSERT_FALSE(pc_canopen_parse_time(&f, NULL));
}

void test_emcy_roundtrip()
{
    const uint8_t msef[5] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_canopen_build_emcy(&f, 3, 0x8130, 0x11, msef));
    TEST_ASSERT_EQUAL_UINT32(0x083, f.id); // 0x080 + 3
    TEST_ASSERT_EQUAL_UINT8(8, f.dlc);
    TEST_ASSERT_EQUAL_HEX8(0x30, f.data[0]); // error code LE
    TEST_ASSERT_EQUAL_HEX8(0x81, f.data[1]);

    uint8_t node = 0, reg = 0, out_msef[5] = {0};
    uint16_t code = 0;
    TEST_ASSERT_TRUE(pc_canopen_parse_emcy(&f, &node, &code, &reg, out_msef));
    TEST_ASSERT_EQUAL_UINT8(3, node);
    TEST_ASSERT_EQUAL_HEX16(0x8130, code);
    TEST_ASSERT_EQUAL_HEX8(0x11, reg);
    TEST_ASSERT_EQUAL_MEMORY(msef, out_msef, 5);

    // 0x080 with node 0 is SYNC, not EMCY: parse_emcy must reject it.
    CanFrame sync;
    pc_canopen_build_sync(&sync);
    sync.dlc = 8;
    TEST_ASSERT_FALSE(pc_canopen_parse_emcy(&sync, &node, &code, &reg, out_msef));
}

void test_pdo_roundtrip()
{
    const uint8_t payload[6] = {1, 2, 3, 4, 5, 6};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_canopen_build_tpdo(&f, 2, 7, payload, 6));
    TEST_ASSERT_EQUAL_UINT32(0x287, f.id); // TPDO2 base 0x280 + 7
    TEST_ASSERT_EQUAL_UINT8(6, f.dlc);
    TEST_ASSERT_EQUAL_MEMORY(payload, f.data, 6);

    CanopenMsg m;
    TEST_ASSERT_TRUE(pc_canopen_parse(&f, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_TPDO, m.type);
    TEST_ASSERT_EQUAL_UINT8(2, m.pdo_num);
    TEST_ASSERT_EQUAL_UINT8(7, m.node_id);

    TEST_ASSERT_TRUE(pc_canopen_build_rpdo(&f, 4, 1, payload, 8));
    TEST_ASSERT_EQUAL_UINT32(0x501, f.id);                          // RPDO4 base 0x500 + 1
    TEST_ASSERT_FALSE(pc_canopen_build_tpdo(&f, 5, 1, payload, 1)); // pdo_num out of range
    TEST_ASSERT_FALSE(pc_canopen_build_tpdo(&f, 1, 1, payload, 9)); // len > 8
}

void test_sdo_read_request()
{
    CanFrame f;
    TEST_ASSERT_TRUE(pc_canopen_build_sdo_read(&f, 0x20, 0x1018, 1)); // identity object, vendor id
    TEST_ASSERT_EQUAL_UINT32(0x620, f.id);                            // 0x600 + 0x20
    TEST_ASSERT_EQUAL_UINT8(8, f.dlc);
    TEST_ASSERT_EQUAL_HEX8(0x40, f.data[0]); // upload initiate
    TEST_ASSERT_EQUAL_HEX8(0x18, f.data[1]); // index LE
    TEST_ASSERT_EQUAL_HEX8(0x10, f.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, f.data[3]); // sub
}

void test_sdo_write_expedited()
{
    const uint8_t val[2] = {0x34, 0x12}; // 0x1234, little-endian
    CanFrame f;
    TEST_ASSERT_TRUE(pc_canopen_build_sdo_write(&f, 5, 0x6040, 0, val, 2));
    TEST_ASSERT_EQUAL_UINT32(0x605, f.id);
    TEST_ASSERT_EQUAL_HEX8(0x2B, f.data[0]); // download, expedited, size indicated, 2 bytes
    TEST_ASSERT_EQUAL_HEX8(0x40, f.data[1]); // index LE
    TEST_ASSERT_EQUAL_HEX8(0x60, f.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[3]);
    TEST_ASSERT_EQUAL_HEX8(0x34, f.data[4]);
    TEST_ASSERT_EQUAL_HEX8(0x12, f.data[5]);

    // 1-byte write -> command 0x2F; 4-byte -> 0x23; bad lengths fail.
    TEST_ASSERT_TRUE(pc_canopen_build_sdo_write(&f, 5, 0x6040, 0, val, 1));
    TEST_ASSERT_EQUAL_HEX8(0x2F, f.data[0]);
    const uint8_t four[4] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(pc_canopen_build_sdo_write(&f, 5, 0x6040, 0, four, 4));
    TEST_ASSERT_EQUAL_HEX8(0x23, f.data[0]);
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_write(&f, 5, 0x6040, 0, four, 0));
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_write(&f, 5, 0x6040, 0, four, 5));
}

// A server's expedited upload response (0x580+node) decodes to the value.
void test_sdo_upload_response_expedited()
{
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x580 + 0x20;
    f.dlc = 8;
    f.data[0] = 0x4B; // upload response, expedited, size indicated, 2 bytes (n=2 -> 4-2)
    f.data[1] = 0x18; // index 0x1018
    f.data[2] = 0x10;
    f.data[3] = 0x01; // sub 1
    f.data[4] = 0x9A; // value 0x029A, little-endian
    f.data[5] = 0x02;

    CanopenSdoResponse r;
    TEST_ASSERT_TRUE(pc_canopen_parse_sdo_response(&f, &r));
    TEST_ASSERT_TRUE(r.is_upload);
    TEST_ASSERT_TRUE(r.expedited);
    TEST_ASSERT_FALSE(r.is_abort);
    TEST_ASSERT_EQUAL_HEX16(0x1018, r.index);
    TEST_ASSERT_EQUAL_UINT8(1, r.sub);
    TEST_ASSERT_EQUAL_UINT8(2, r.len);
    TEST_ASSERT_EQUAL_HEX8(0x9A, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, r.data[1]);
}

void test_sdo_abort_roundtrip()
{
    CanFrame f;
    TEST_ASSERT_TRUE(pc_canopen_build_sdo_abort(&f, 0x20, 0x1000, 0, CANOPEN_ABORT_NO_OBJECT, PROTO_FALSE));
    TEST_ASSERT_EQUAL_UINT32(0x580 + 0x20, f.id); // server -> client
    TEST_ASSERT_EQUAL_HEX8(0x80, f.data[0]);

    CanopenSdoResponse r;
    TEST_ASSERT_TRUE(pc_canopen_parse_sdo_response(&f, &r));
    TEST_ASSERT_TRUE(r.is_abort);
    TEST_ASSERT_EQUAL_HEX32(CANOPEN_ABORT_NO_OBJECT, r.abort_code);
    TEST_ASSERT_EQUAL_HEX16(0x1000, r.index);
}

// The download (write) acknowledge: scs=3, no payload.
void test_sdo_download_ack()
{
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x580 + 5;
    f.dlc = 8;
    f.data[0] = 0x60; // download initiate response
    f.data[1] = 0x40;
    f.data[2] = 0x60; // index 0x6040
    CanopenSdoResponse r;
    TEST_ASSERT_TRUE(pc_canopen_parse_sdo_response(&f, &r));
    TEST_ASSERT_FALSE(r.is_abort);
    TEST_ASSERT_FALSE(r.is_upload);
    TEST_ASSERT_EQUAL_UINT8(0, r.len);
}

void test_parse_classifies()
{
    CanFrame f;
    memset(&f, 0, sizeof(f));
    CanopenMsg m;

    f.id = 0x18A; // TPDO1 + node 10
    TEST_ASSERT_TRUE(pc_canopen_parse(&f, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_TPDO, m.type);
    TEST_ASSERT_EQUAL_UINT8(1, m.pdo_num);
    TEST_ASSERT_EQUAL_UINT8(10, m.node_id);

    f.id = 0x60F; // SDO_RX + node 15
    TEST_ASSERT_TRUE(pc_canopen_parse(&f, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_SDO_RX, m.type);
    TEST_ASSERT_EQUAL_UINT8(15, m.node_id);

    // Extended frames are not CANopen default-profile.
    f.extended = PROTO_TRUE;
    TEST_ASSERT_FALSE(pc_canopen_parse(&f, &m));
}

// Every builder rejects a null out and an out-of-range node (0 or > 127); node 0
// is only valid as the NMT broadcast target.
void test_build_arg_validation()
{
    CanFrame f;
    const uint8_t d[4] = {1, 2, 3, 4};

    TEST_ASSERT_FALSE(pc_canopen_build_nmt(NULL, CANOPEN_NMT_START, 5));
    TEST_ASSERT_FALSE(pc_canopen_build_sync(NULL));
    TEST_ASSERT_FALSE(pc_canopen_build_heartbeat(NULL, 5, 0));
    TEST_ASSERT_FALSE(pc_canopen_build_heartbeat(&f, 0, 0));   // node 0 is not a heartbeat producer
    TEST_ASSERT_FALSE(pc_canopen_build_heartbeat(&f, 128, 0)); // > 127
    TEST_ASSERT_FALSE(pc_canopen_build_emcy(&f, 0, 0, 0, NULL));
    TEST_ASSERT_FALSE(pc_canopen_build_emcy(&f, 128, 0, 0, NULL));
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_read(NULL, 5, 0, 0));
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_read(&f, 0, 0, 0));
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_read(&f, 128, 0, 0));
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_abort(&f, 0, 0, 0, 0, PROTO_TRUE));
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_abort(&f, 128, 0, 0, 0, PROTO_TRUE));
    // PDO builders reject a null out, node 0, and len>0 with a null payload.
    TEST_ASSERT_FALSE(pc_canopen_build_tpdo(NULL, 1, 5, d, 4));
    TEST_ASSERT_FALSE(pc_canopen_build_tpdo(&f, 1, 0, d, 4));
    TEST_ASSERT_FALSE(pc_canopen_build_tpdo(&f, 1, 5, NULL, 4)); // len && !data
    TEST_ASSERT_FALSE(pc_canopen_build_rpdo(&f, 0, 5, d, 4));    // pdo_num 0
}

// EMCY with no manufacturer-specific octets leaves data[3..7] zero.
void test_emcy_build_null_msef()
{
    CanFrame f;
    TEST_ASSERT_TRUE(pc_canopen_build_emcy(&f, 3, 0x1000, 0x01, NULL));
    for (int i = 3; i < 8; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(0x00, f.data[i]);
    }
}

// pc_canopen_parse classifies every function-code base + node combination.
void test_parse_all_function_codes()
{
    CanFrame f;
    memset(&f, 0, sizeof(f));
    CanopenMsg m;

    typedef struct
    {
        uint32_t id;
        CanopenType type;
        uint8_t node;
        uint8_t pdo;
    } Case;
    const Case cases[] = {
        {0x000, CANOPEN_T_NMT, 0, 0},    {0x100, CANOPEN_T_TIME, 0, 0},       {0x087, CANOPEN_T_EMCY, 7, 0},
        {0x181, CANOPEN_T_TPDO, 1, 1},   {0x201, CANOPEN_T_RPDO, 1, 1},       {0x282, CANOPEN_T_TPDO, 2, 2},
        {0x303, CANOPEN_T_RPDO, 3, 2},   {0x384, CANOPEN_T_TPDO, 4, 3},       {0x405, CANOPEN_T_RPDO, 5, 3},
        {0x486, CANOPEN_T_TPDO, 6, 4},   {0x507, CANOPEN_T_RPDO, 7, 4},       {0x588, CANOPEN_T_SDO_TX, 8, 0},
        {0x609, CANOPEN_T_SDO_RX, 9, 0}, {0x70A, CANOPEN_T_HEARTBEAT, 10, 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        f.id = cases[i].id;
        TEST_ASSERT_TRUE(pc_canopen_parse(&f, &m));
        TEST_ASSERT_EQUAL_INT(cases[i].type, m.type);
        TEST_ASSERT_EQUAL_UINT8(cases[i].node, m.node_id);
        TEST_ASSERT_EQUAL_UINT8(cases[i].pdo, m.pdo_num);
    }
    // An unallocated function code parses true but stays UNKNOWN.
    f.id = 0x681; // between SDO_RX (0x600) and HEARTBEAT (0x700), not allocated
    TEST_ASSERT_TRUE(pc_canopen_parse(&f, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_UNKNOWN, m.type);
    // A function base carried with node 0 is not classified further.
    f.id = 0x180; // TPDO1 base, node 0
    TEST_ASSERT_TRUE(pc_canopen_parse(&f, &m));
    TEST_ASSERT_EQUAL_INT(CANOPEN_T_UNKNOWN, m.type);
    // Null frame / output rejected.
    TEST_ASSERT_FALSE(pc_canopen_parse(NULL, &m));
    TEST_ASSERT_FALSE(pc_canopen_parse(&f, NULL));
}

// parse_emcy rejects short frames and non-EMCY function codes.
void test_parse_emcy_rejections()
{
    CanFrame f;
    uint8_t node = 0, reg = 0, msef[5] = {0};
    uint16_t code = 0;
    pc_canopen_build_emcy(&f, 5, 0x1000, 0, NULL);
    f.dlc = 7; // EMCY needs 8 octets
    TEST_ASSERT_FALSE(pc_canopen_parse_emcy(&f, &node, &code, &reg, msef));
    pc_canopen_build_heartbeat(&f, 5, 0); // a heartbeat is not an EMCY
    f.dlc = 8;
    TEST_ASSERT_FALSE(pc_canopen_parse_emcy(&f, &node, &code, &reg, msef));
    TEST_ASSERT_FALSE(pc_canopen_parse_emcy(NULL, &node, &code, &reg, msef));
}

// parse_heartbeat rejects short/foreign frames and strips the boot toggle (bit 7).
void test_parse_heartbeat_rejections()
{
    CanFrame f;
    uint8_t node = 0, state = 0;
    pc_canopen_build_heartbeat(&f, 9, CANOPEN_STATE_OPERATIONAL);
    f.dlc = 0; // needs >= 1
    TEST_ASSERT_FALSE(pc_canopen_parse_heartbeat(&f, &node, &state));
    pc_canopen_build_sync(&f); // a SYNC is not a heartbeat
    f.dlc = 1;
    TEST_ASSERT_FALSE(pc_canopen_parse_heartbeat(&f, &node, &state));
    pc_canopen_build_heartbeat(&f, 9, CANOPEN_STATE_OPERATIONAL);
    f.data[0] = 0x80u | CANOPEN_STATE_OPERATIONAL; // boot toggle set
    TEST_ASSERT_TRUE(pc_canopen_parse_heartbeat(&f, &node, &state));
    TEST_ASSERT_EQUAL_HEX8(CANOPEN_STATE_OPERATIONAL, state);
}

// parse_sdo_response rejects short/foreign frames and unknown command specifiers, and
// decodes the expedited-without-size and segmented upload variants.
void test_parse_sdo_response_variants()
{
    CanFrame f;
    CanopenSdoResponse r;
    memset(&f, 0, sizeof(f));

    f.id = 0x580 + 5;
    f.dlc = 7; // < 8
    TEST_ASSERT_FALSE(pc_canopen_parse_sdo_response(&f, &r));
    f.dlc = 8;
    f.id = 0x600 + 5; // SDO_RX (request), not a TX response
    TEST_ASSERT_FALSE(pc_canopen_parse_sdo_response(&f, &r));
    f.id = 0x580 + 0; // node 0
    TEST_ASSERT_FALSE(pc_canopen_parse_sdo_response(&f, &r));

    f.id = 0x580 + 5;
    f.data[0] = 0x00; // unknown command specifier (scs 0)
    TEST_ASSERT_FALSE(pc_canopen_parse_sdo_response(&f, &r));

    // Expedited upload without size indicated (e=1, s=0) reports the full 4 octets.
    f.data[0] = 0x42;
    f.data[4] = 0x11;
    f.data[5] = 0x22;
    f.data[6] = 0x33;
    f.data[7] = 0x44;
    TEST_ASSERT_TRUE(pc_canopen_parse_sdo_response(&f, &r));
    TEST_ASSERT_TRUE(r.is_upload);
    TEST_ASSERT_TRUE(r.expedited);
    TEST_ASSERT_EQUAL_UINT8(4, r.len);
    TEST_ASSERT_EQUAL_HEX8(0x11, r.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x44, r.data[3]);

    // Segmented upload (e=0) is reported as upload with no inline payload.
    f.data[0] = 0x40;
    TEST_ASSERT_TRUE(pc_canopen_parse_sdo_response(&f, &r));
    TEST_ASSERT_TRUE(r.is_upload);
    TEST_ASSERT_FALSE(r.expedited);
    TEST_ASSERT_EQUAL_UINT8(0, r.len);

    TEST_ASSERT_FALSE(pc_canopen_parse_sdo_response(NULL, &r));
    TEST_ASSERT_FALSE(pc_canopen_parse_sdo_response(&f, NULL));
}

// A zero-length PDO is valid (the `len && !data` guard must not reject len==0 with a
// null payload), and it must produce a zero-length frame.
void test_pdo_zero_length()
{
    CanFrame f;
    TEST_ASSERT_TRUE(pc_canopen_build_tpdo(&f, 1, 5, NULL, 0));
    TEST_ASSERT_EQUAL_UINT32(0x185, f.id); // TPDO1 base + node 5
    TEST_ASSERT_EQUAL_UINT8(0, f.dlc);
}

// build_sdo_write's argument-validation OR-chain needs a null-out, an out-of-range node
// (both 0 and >127), and a null data pointer paired with an in-range length to exercise
// every branch of `!out || !valid_node(node_id) || len < 1 || len > 4 || !data`.
void test_sdo_write_arg_validation()
{
    CanFrame f;
    const uint8_t val[2] = {1, 2};
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_write(NULL, 5, 0x6040, 0, val, 2));
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_write(&f, 0, 0x6040, 0, val, 2));
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_write(&f, 128, 0x6040, 0, val, 2));
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_write(&f, 5, 0x6040, 0, NULL, 2));
}

// build_emcy and build_sdo_abort must reject a null out (test_build_arg_validation covers
// their invalid-node branches but not this one), and build_sdo_abort's to_server=true path
// (client -> server, 0x600 base) is otherwise never exercised (only to_server=false is).
void test_emcy_and_sdo_abort_null_out_and_to_server()
{
    TEST_ASSERT_FALSE(pc_canopen_build_emcy(NULL, 5, 0, 0, NULL));
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_abort(NULL, 5, 0, 0, 0, PROTO_TRUE));

    CanFrame f;
    TEST_ASSERT_TRUE(pc_canopen_build_sdo_abort(&f, 0x20, 0x1000, 0, CANOPEN_ABORT_TIMEOUT, PROTO_TRUE));
    TEST_ASSERT_EQUAL_UINT32(0x600 + 0x20, f.id); // client -> server
    TEST_ASSERT_EQUAL_HEX8(0x80, f.data[0]);
}

// parse_emcy: the `f->extended` disjunct is never true in the other tests, and each
// optional output pointer (node_id/error_code/error_reg/msef) is always non-null there,
// so their `if (ptr)` false branches are otherwise never taken.
void test_parse_emcy_extended_and_null_outputs()
{
    const uint8_t msef[5] = {1, 2, 3, 4, 5};
    CanFrame f;
    pc_canopen_build_emcy(&f, 5, 0x1000, 0x02, msef);
    f.extended = PROTO_TRUE;
    uint8_t node = 0, reg = 0, out_msef[5] = {0};
    uint16_t code = 0;
    TEST_ASSERT_FALSE(pc_canopen_parse_emcy(&f, &node, &code, &reg, out_msef));

    pc_canopen_build_emcy(&f, 5, 0x1000, 0x02, msef); // rebuild: not extended
    TEST_ASSERT_TRUE(pc_canopen_parse_emcy(&f, NULL, NULL, NULL, NULL));
}

// parse_heartbeat: a null frame, an extended frame, a function-matching frame whose node
// is 0 (crafted directly - the builder itself refuses node 0), and null optional outputs
// are otherwise never exercised.
void test_parse_heartbeat_extended_null_and_node_zero()
{
    uint8_t node = 0, state = 0;
    TEST_ASSERT_FALSE(pc_canopen_parse_heartbeat(NULL, &node, &state));

    CanFrame f;
    pc_canopen_build_heartbeat(&f, 9, CANOPEN_STATE_OPERATIONAL);
    f.extended = PROTO_TRUE;
    TEST_ASSERT_FALSE(pc_canopen_parse_heartbeat(&f, &node, &state));

    CanFrame node_zero;
    memset(&node_zero, 0, sizeof(node_zero));
    node_zero.id = CANOPEN_COB_HEARTBEAT; // function matches, node field is 0
    node_zero.dlc = 1;
    TEST_ASSERT_FALSE(pc_canopen_parse_heartbeat(&node_zero, &node, &state));

    pc_canopen_build_heartbeat(&f, 9, CANOPEN_STATE_OPERATIONAL);
    TEST_ASSERT_TRUE(pc_canopen_parse_heartbeat(&f, NULL, NULL));
}

// parse_sdo_response's `f->extended` disjunct is never true elsewhere.
void test_parse_sdo_response_extended()
{
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x580 + 5;
    f.dlc = 8;
    f.extended = PROTO_TRUE;
    CanopenSdoResponse r;
    TEST_ASSERT_FALSE(pc_canopen_parse_sdo_response(&f, &r));
}

// --- segmented SDO (CiA 301 §7.2.4.3) ---
void test_sdo_segmented_download_build()
{
    const uint8_t obj[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_canopen_build_sdo_download_init(&f, 5, 0x2000, 1, 10));
    TEST_ASSERT_EQUAL_HEX32(CANOPEN_COB_SDO_RX + 5, f.id);
    TEST_ASSERT_EQUAL_HEX8(0x21, f.data[0]); // download initiate, segmented, size indicated
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[1]); // index 0x2000 LE
    TEST_ASSERT_EQUAL_HEX8(0x20, f.data[2]);
    TEST_ASSERT_EQUAL_HEX8(1, f.data[3]);
    TEST_ASSERT_EQUAL_HEX8(10, f.data[4]); // total size LE

    // First segment: toggle 0, 7 octets, not last -> command 0x00.
    TEST_ASSERT_TRUE(pc_canopen_build_sdo_download_segment(&f, 5, PROTO_FALSE, obj, 7, PROTO_FALSE));
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[0]);
    TEST_ASSERT_EQUAL_MEMORY(obj, f.data + 1, 7);
    // Last segment: toggle 1, 3 octets -> command 0x10 | ((7-3)<<1) | 1 = 0x19.
    TEST_ASSERT_TRUE(pc_canopen_build_sdo_download_segment(&f, 5, PROTO_TRUE, obj + 7, 3, PROTO_TRUE));
    TEST_ASSERT_EQUAL_HEX8(0x19, f.data[0]);
    // Guards.
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_download_segment(&f, 5, PROTO_FALSE, obj, 8, PROTO_FALSE)); // len > 7
    TEST_ASSERT_FALSE(pc_canopen_build_sdo_download_segment(&f, 0, PROTO_FALSE, obj, 3, PROTO_FALSE)); // bad node
}

void test_sdo_segmented_upload_roundtrip()
{
    const uint8_t obj[10] = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    CanFrame req;
    TEST_ASSERT_TRUE(pc_canopen_build_sdo_upload_segment_req(&req, 5, PROTO_FALSE));
    TEST_ASSERT_EQUAL_HEX8(0x60, req.data[0]);
    TEST_ASSERT_TRUE(pc_canopen_build_sdo_upload_segment_req(&req, 5, PROTO_TRUE));
    TEST_ASSERT_EQUAL_HEX8(0x70, req.data[0]); // 0x60 | toggle bit

    // Two server-response segments (same segment wire form), parsed + reassembled into the object.
    CanFrame s1, s2;
    pc_canopen_build_sdo_download_segment(&s1, 5, PROTO_FALSE, obj, 7, PROTO_FALSE);
    pc_canopen_build_sdo_download_segment(&s2, 5, PROTO_TRUE, obj + 7, 3, PROTO_TRUE);

    uint8_t buf[16];
    CanopenSdoReasm r;
    pc_canopen_sdo_reasm_init(&r, buf, sizeof(buf));
    proto_bool tg = PROTO_TRUE, last = PROTO_TRUE;
    uint8_t data[7], len = 0;
    TEST_ASSERT_TRUE(pc_canopen_parse_sdo_segment(&s1, &tg, data, &len, &last));
    TEST_ASSERT_FALSE(tg);
    TEST_ASSERT_EQUAL_UINT8(7, len);
    TEST_ASSERT_FALSE(last);
    TEST_ASSERT_TRUE(pc_canopen_sdo_reasm_feed(&r, data, len, tg, last));
    TEST_ASSERT_FALSE(r.done);
    TEST_ASSERT_TRUE(pc_canopen_parse_sdo_segment(&s2, &tg, data, &len, &last));
    TEST_ASSERT_TRUE(tg);
    TEST_ASSERT_EQUAL_UINT8(3, len);
    TEST_ASSERT_TRUE(last);
    TEST_ASSERT_TRUE(pc_canopen_sdo_reasm_feed(&r, data, len, tg, last));
    TEST_ASSERT_TRUE(r.done);
    TEST_ASSERT_EQUAL_size_t(10, r.len);
    TEST_ASSERT_EQUAL_MEMORY(obj, buf, 10);
}

void test_sdo_segmented_guards()
{
    uint8_t buf[16];
    CanopenSdoReasm r;
    pc_canopen_sdo_reasm_init(&r, buf, sizeof(buf));
    uint8_t d[7] = {0};
    TEST_ASSERT_FALSE(pc_canopen_sdo_reasm_feed(&r, d, 7, PROTO_TRUE, PROTO_FALSE)); // first segment must be toggle 0
    TEST_ASSERT_TRUE(pc_canopen_sdo_reasm_feed(&r, d, 7, PROTO_FALSE, PROTO_FALSE));
    TEST_ASSERT_FALSE(pc_canopen_sdo_reasm_feed(&r, d, 7, PROTO_FALSE, PROTO_FALSE)); // toggle must have flipped

    uint8_t tiny[4];
    CanopenSdoReasm r2;
    pc_canopen_sdo_reasm_init(&r2, tiny, sizeof(tiny));
    TEST_ASSERT_FALSE(pc_canopen_sdo_reasm_feed(&r2, d, 7, PROTO_FALSE, PROTO_FALSE)); // overflow

    // parse rejects an expedited response (command high bits non-zero) as not a segment.
    CanFrame exp;
    pc_canopen_build_sdo_read(&exp, 5, 0x1000, 0); // 0x40 (ccs=2)
    proto_bool tg, last;
    uint8_t data[7], len;
    TEST_ASSERT_FALSE(pc_canopen_parse_sdo_segment(&exp, &tg, data, &len, &last));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_nmt_start_node);
    RUN_TEST(test_sync);
    RUN_TEST(test_time_roundtrip);
    RUN_TEST(test_heartbeat_roundtrip);
    RUN_TEST(test_emcy_roundtrip);
    RUN_TEST(test_pdo_roundtrip);
    RUN_TEST(test_sdo_read_request);
    RUN_TEST(test_sdo_write_expedited);
    RUN_TEST(test_sdo_upload_response_expedited);
    RUN_TEST(test_sdo_abort_roundtrip);
    RUN_TEST(test_sdo_download_ack);
    RUN_TEST(test_parse_classifies);
    RUN_TEST(test_build_arg_validation);
    RUN_TEST(test_emcy_build_null_msef);
    RUN_TEST(test_parse_all_function_codes);
    RUN_TEST(test_parse_emcy_rejections);
    RUN_TEST(test_parse_heartbeat_rejections);
    RUN_TEST(test_parse_sdo_response_variants);
    RUN_TEST(test_pdo_zero_length);
    RUN_TEST(test_sdo_write_arg_validation);
    RUN_TEST(test_emcy_and_sdo_abort_null_out_and_to_server);
    RUN_TEST(test_parse_emcy_extended_and_null_outputs);
    RUN_TEST(test_parse_heartbeat_extended_null_and_node_zero);
    RUN_TEST(test_parse_sdo_response_extended);
    RUN_TEST(test_sdo_segmented_download_build);
    RUN_TEST(test_sdo_segmented_upload_roundtrip);
    RUN_TEST(test_sdo_segmented_guards);
    return UNITY_END();
}
