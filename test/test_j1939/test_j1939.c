// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SAE J1939 codec (services/fieldbus/j1939): 29-bit id encode/decode (PDU1 + PDU2),
// single-frame messages, Request PGN, Address Claimed + NAME, and the Transport Protocol
// (BAM announce + TP.DT packets) round-tripped through the reassembler. Pure host tests.

#include "services/fieldbus/j1939/j1939.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// PDU2 (broadcast, PF >= 240): PS is part of the PGN, DA is global.
void test_id_pdu2_roundtrip()
{
    uint32_t id = 0;
    TEST_ASSERT_TRUE(pc_j1939_encode_id(&id, 6, 0x00FEEE, 0x21, 0xFF)); // engine temperature-ish PGN
    J1939Id d;
    TEST_ASSERT_TRUE(pc_j1939_decode_id(id, &d));
    TEST_ASSERT_EQUAL_UINT8(6, d.priority);
    TEST_ASSERT_EQUAL_HEX32(0x00FEEE, d.pgn);
    TEST_ASSERT_EQUAL_HEX8(0x21, d.sa);
    TEST_ASSERT_FALSE(d.pdu1);
    TEST_ASSERT_EQUAL_HEX8(0xFF, d.da);
}

// PDU1 (peer-to-peer, PF < 240): PS carries the destination address; the PGN low octet is 0.
void test_id_pdu1_roundtrip()
{
    uint32_t id = 0;
    TEST_ASSERT_TRUE(pc_j1939_encode_id(&id, 3, 0x00EF00, 0x21, 0x05)); // PF 0xEF = 239 -> PDU1
    J1939Id d;
    TEST_ASSERT_TRUE(pc_j1939_decode_id(id, &d));
    TEST_ASSERT_TRUE(d.pdu1);
    TEST_ASSERT_EQUAL_HEX8(0x05, d.da);
    TEST_ASSERT_EQUAL_HEX32(0x00EF00, d.pgn);
    TEST_ASSERT_EQUAL_HEX8(0x21, d.sa);
}

void test_encode_rejects_bad_args()
{
    uint32_t id = 0;
    TEST_ASSERT_FALSE(pc_j1939_encode_id(&id, 8, 0x00FEEE, 0x21, 0xFF)); // priority > 7
    TEST_ASSERT_FALSE(pc_j1939_encode_id(&id, 6, 0x40000, 0x21, 0xFF));  // pgn > 18 bits
}

void test_build_single_frame()
{
    const uint8_t payload[3] = {0x11, 0x22, 0x33};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_j1939_build_message(&f, 6, 0x00FEEE, 0x21, 0xFF, payload, 3));
    TEST_ASSERT_TRUE(f.extended);
    TEST_ASSERT_EQUAL_UINT8(3, f.dlc);
    TEST_ASSERT_EQUAL_MEMORY(payload, f.data, 3);
    J1939Id d;
    TEST_ASSERT_TRUE(pc_j1939_decode_id(f.id, &d));
    TEST_ASSERT_EQUAL_HEX32(0x00FEEE, d.pgn);
}

void test_request_pgn()
{
    CanFrame f;
    TEST_ASSERT_TRUE(pc_j1939_build_request(&f, 0x21, 0x00, 0x00FEEC)); // request the component ID PGN
    J1939Id d;
    TEST_ASSERT_TRUE(pc_j1939_decode_id(f.id, &d));
    TEST_ASSERT_EQUAL_HEX32(J1939_PGN_REQUEST, d.pgn);
    TEST_ASSERT_EQUAL_HEX8(0x00, d.da); // PDU1 to address 0
    TEST_ASSERT_EQUAL_UINT8(3, f.dlc);
    TEST_ASSERT_EQUAL_HEX8(0xEC, f.data[0]); // requested PGN, little-endian
    TEST_ASSERT_EQUAL_HEX8(0xFE, f.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[2]);
}

void test_address_claim_name()
{
    uint64_t name = pc_j1939_build_name(true, 2, 0, 5, 130, 0, 1, 0x1F2, 0x12345);
    CanFrame f;
    TEST_ASSERT_TRUE(pc_j1939_build_address_claim(&f, 0x80, name));
    J1939Id d;
    TEST_ASSERT_TRUE(pc_j1939_decode_id(f.id, &d));
    TEST_ASSERT_EQUAL_HEX32(J1939_PGN_ADDRESS_CLAIM, d.pgn);
    TEST_ASSERT_EQUAL_HEX8(0x80, d.sa);
    TEST_ASSERT_EQUAL_UINT8(8, f.dlc);
    // NAME transmitted little-endian; reconstruct and compare.
    uint64_t back = 0;
    for (int i = 0; i < 8; i++)
    {
        back |= (uint64_t)f.data[i] << (8 * i);
    }
    TEST_ASSERT_EQUAL_HEX64(name, back);
    // The high bit (arbitrary-address-capable) and identity number survive the round trip.
    TEST_ASSERT_EQUAL_HEX32(0x12345u, (uint32_t)(back & 0x1FFFFFu));
    TEST_ASSERT_TRUE((back >> 63) & 1u);
}

void test_tp_num_packets()
{
    TEST_ASSERT_EQUAL_UINT8(2, pc_j1939_tp_num_packets(9));  // ceil(9/7)
    TEST_ASSERT_EQUAL_UINT8(3, pc_j1939_tp_num_packets(16)); // ceil(16/7)
    TEST_ASSERT_EQUAL_UINT8(1, pc_j1939_tp_num_packets(7));
}

// Build a BAM announce + its TP.DT packets, feed them to the reassembler, get the message back.
void test_tp_bam_roundtrip()
{
    uint8_t msg[16];
    for (int i = 0; i < 16; i++)
    {
        msg[i] = (uint8_t)(0xA0 + i);
    }
    const uint32_t pgn = 0x00FECA; // DM1-style broadcast PGN
    const uint8_t sa = 0x21;

    J1939TpRx rx;
    pc_j1939_tp_reset(&rx);

    CanFrame cm;
    TEST_ASSERT_TRUE(pc_j1939_build_bam_cm(&cm, sa, pgn, 16));
    TEST_ASSERT_EQUAL_INT(J1939_TP_STARTED, pc_j1939_tp_feed(&rx, &cm));

    uint8_t packets = pc_j1939_tp_num_packets(16); // 3
    for (uint8_t seq = 1; seq <= packets; seq++)
    {
        uint16_t off = (uint16_t)((seq - 1) * J1939_TP_DT_LEN);
        uint8_t len = (uint8_t)((16 - off) < J1939_TP_DT_LEN ? (16 - off) : J1939_TP_DT_LEN);
        CanFrame dt;
        TEST_ASSERT_TRUE(pc_j1939_build_tp_dt(&dt, sa, J1939_ADDR_GLOBAL, seq, msg + off, len));
        J1939TpResult r = pc_j1939_tp_feed(&rx, &dt);
        if (seq < packets)
        {
            TEST_ASSERT_EQUAL_INT(J1939_TP_PROGRESS, r);
        }
        else
        {
            TEST_ASSERT_EQUAL_INT(J1939_TP_COMPLETE, r);
        }
    }
    TEST_ASSERT_EQUAL_UINT16(16, rx.total_size);
    TEST_ASSERT_EQUAL_HEX32(pgn, rx.pgn);
    TEST_ASSERT_EQUAL_MEMORY(msg, rx.buf, 16);
}

// An out-of-sequence data packet aborts the session.
void test_tp_out_of_sequence_errors()
{
    J1939TpRx rx;
    pc_j1939_tp_reset(&rx);
    CanFrame cm;
    pc_j1939_build_bam_cm(&cm, 0x21, 0x00FECA, 16);
    TEST_ASSERT_EQUAL_INT(J1939_TP_STARTED, pc_j1939_tp_feed(&rx, &cm));

    uint8_t chunk[7] = {1, 2, 3, 4, 5, 6, 7};
    CanFrame dt;
    pc_j1939_build_tp_dt(&dt, 0x21, J1939_ADDR_GLOBAL, 2, chunk, 7); // skips seq 1
    TEST_ASSERT_EQUAL_INT(J1939_TP_ERROR, pc_j1939_tp_feed(&rx, &dt));
    TEST_ASSERT_FALSE(rx.active);
}

// Builder argument rejections, including the priority-out-of-range path that makes
// ext_frame's id encode fail.
void test_build_error_paths()
{
    CanFrame f;
    const uint8_t data[3] = {1, 2, 3};
    TEST_ASSERT_FALSE(pc_j1939_decode_id(0x18FEEE21u, NULL)); // null out

    TEST_ASSERT_FALSE(pc_j1939_build_message(NULL, 6, 0x00FEEE, 0x21, 0xFF, data, 3)); // null out
    TEST_ASSERT_FALSE(pc_j1939_build_message(&f, 6, 0x00FEEE, 0x21, 0xFF, NULL, 3));   // len but null data
    TEST_ASSERT_FALSE(pc_j1939_build_message(&f, 8, 0x00FEEE, 0x21, 0xFF, data, 3));      // priority>7 -> encode fails

    TEST_ASSERT_FALSE(pc_j1939_build_request(NULL, 0x21, 0, 0x00FEEC)); // null out
    TEST_ASSERT_FALSE(pc_j1939_build_request(&f, 0x21, 0, 0x40000));       // pgn > 18 bits

    TEST_ASSERT_FALSE(pc_j1939_build_bam_cm(NULL, 0x21, 0x00FECA, 16)); // null out
    TEST_ASSERT_FALSE(pc_j1939_build_bam_cm(&f, 0x21, 0x00FECA, 8));       // total_size < 9
    TEST_ASSERT_FALSE(pc_j1939_build_bam_cm(&f, 0x21, 0x40000, 16));       // pgn > 18 bits

    const uint8_t chunk[7] = {1, 2, 3, 4, 5, 6, 7};
    TEST_ASSERT_FALSE(pc_j1939_build_tp_dt(&f, 0x21, 0xFF, 0, chunk, 7));   // seq 0
    TEST_ASSERT_FALSE(pc_j1939_build_tp_dt(&f, 0x21, 0xFF, 1, chunk, 0));   // chunk_len 0
    TEST_ASSERT_FALSE(pc_j1939_build_tp_dt(&f, 0x21, 0xFF, 1, NULL, 7)); // null chunk
    TEST_ASSERT_FALSE(pc_j1939_build_tp_dt(&f, 0x21, 0xFF, 1, chunk, 9));   // chunk_len > 7
}

// Null-pointer guard branches whose non-null-argument paths are already exercised above.
void test_null_guard_paths()
{
    TEST_ASSERT_FALSE(pc_j1939_encode_id(NULL, 6, 0x00FEEE, 0x21, 0xFF)); // null id
    const uint8_t chunk[7] = {1, 2, 3, 4, 5, 6, 7};
    TEST_ASSERT_FALSE(pc_j1939_build_tp_dt(NULL, 0x21, 0xFF, 1, chunk, 7)); // null out
    pc_j1939_tp_reset(NULL);                                                // null rx -> no-op, not a crash
}

// build_message's length edges: too long is rejected; zero-length (null data) still builds.
void test_build_message_length_edges()
{
    const uint8_t data[3] = {1, 2, 3};
    CanFrame f;
    TEST_ASSERT_FALSE(pc_j1939_build_message(&f, 6, 0x00FEEE, 0x21, 0xFF, data, PC_CAN_MAX_DLC + 1)); // too long

    TEST_ASSERT_TRUE(pc_j1939_build_message(&f, 6, 0x00FEEE, 0x21, 0xFF, NULL, 0)); // zero-length, no data needed
    TEST_ASSERT_EQUAL_UINT8(0, f.dlc);
}

// build_name: the false side of the arbitrary-address-capable ternary.
void test_build_name_not_arbitrary_capable()
{
    uint64_t name = pc_j1939_build_name(false, 2, 0, 5, 130, 0, 1, 0x1F2, 0x12345);
    TEST_ASSERT_FALSE((name >> 63) & 1u);
}

// build_bam_cm: total_size above PC_J1939_TP_MAX is rejected.
void test_build_bam_cm_too_large()
{
    CanFrame f;
    TEST_ASSERT_FALSE(pc_j1939_build_bam_cm(&f, 0x21, 0x00FECA, PC_J1939_TP_MAX + 1));
}

// A TP.CM frame shorter than 8 octets is not a valid session start, even with a BAM PGN.
void test_tp_feed_short_cm_frame_ignored()
{
    J1939TpRx rx;
    pc_j1939_tp_reset(&rx);
    CanFrame cm;
    TEST_ASSERT_TRUE(pc_j1939_build_bam_cm(&cm, 0x21, 0x00FECA, 16));
    cm.dlc = 4; // too short to be a real TP.CM frame
    TEST_ASSERT_EQUAL_INT(J1939_TP_IGNORED, pc_j1939_tp_feed(&rx, &cm));
    TEST_ASSERT_FALSE(rx.active);
}

// RTS (connection mode), like BAM, is a valid receiver-side session start.
void test_tp_feed_rts_starts_session()
{
    J1939TpRx rx;
    pc_j1939_tp_reset(&rx);
    CanFrame cm;
    TEST_ASSERT_TRUE(pc_j1939_build_bam_cm(&cm, 0x21, 0x00FECA, 16));
    cm.data[0] = J1939_TP_CM_RTS;
    TEST_ASSERT_EQUAL_INT(J1939_TP_STARTED, pc_j1939_tp_feed(&rx, &cm));
    TEST_ASSERT_TRUE(rx.active);
}

// A TP.CM announcing a size outside 9..PC_J1939_TP_MAX aborts with an error (bytes crafted
// directly, since the builder itself refuses to construct such a frame).
void test_tp_feed_cm_total_size_out_of_range()
{
    J1939TpRx rx;
    pc_j1939_tp_reset(&rx);
    CanFrame cm;
    TEST_ASSERT_TRUE(pc_j1939_build_bam_cm(&cm, 0x21, 0x00FECA, 16));

    CanFrame too_small = cm;
    too_small.data[1] = 5; // total_size = 5, below the 9-octet minimum
    too_small.data[2] = 0;
    TEST_ASSERT_EQUAL_INT(J1939_TP_ERROR, pc_j1939_tp_feed(&rx, &too_small));
    TEST_ASSERT_FALSE(rx.active);

    CanFrame too_big = cm;
    too_big.data[1] = (uint8_t)2000;
    too_big.data[2] = (uint8_t)(2000 >> 8); // total_size = 2000, above PC_J1939_TP_MAX
    TEST_ASSERT_EQUAL_INT(J1939_TP_ERROR, pc_j1939_tp_feed(&rx, &too_big));
    TEST_ASSERT_FALSE(rx.active);
}

// A zero-length TP.DT frame carries no sequence number and is ignored, leaving the session
// untouched.
void test_tp_feed_dt_short_frame_ignored()
{
    J1939TpRx rx;
    pc_j1939_tp_reset(&rx);
    CanFrame cm;
    pc_j1939_build_bam_cm(&cm, 0x21, 0x00FECA, 16);
    TEST_ASSERT_EQUAL_INT(J1939_TP_STARTED, pc_j1939_tp_feed(&rx, &cm));

    uint8_t chunk[7] = {1, 2, 3, 4, 5, 6, 7};
    CanFrame dt;
    TEST_ASSERT_TRUE(pc_j1939_build_tp_dt(&dt, 0x21, J1939_ADDR_GLOBAL, 1, chunk, 7));
    dt.dlc = 0;
    TEST_ASSERT_EQUAL_INT(J1939_TP_IGNORED, pc_j1939_tp_feed(&rx, &dt));
    TEST_ASSERT_TRUE(rx.active);
    TEST_ASSERT_EQUAL_UINT8(1, rx.next_seq);
}

// A TP.DT from a source other than the active session's is ignored, leaving the session
// untouched.
void test_tp_feed_dt_wrong_source_ignored()
{
    J1939TpRx rx;
    pc_j1939_tp_reset(&rx);
    CanFrame cm;
    pc_j1939_build_bam_cm(&cm, 0x21, 0x00FECA, 16);
    TEST_ASSERT_EQUAL_INT(J1939_TP_STARTED, pc_j1939_tp_feed(&rx, &cm));

    uint8_t chunk[7] = {1, 2, 3, 4, 5, 6, 7};
    CanFrame dt;
    TEST_ASSERT_TRUE(pc_j1939_build_tp_dt(&dt, 0x22, J1939_ADDR_GLOBAL, 1, chunk, 7)); // different source address
    TEST_ASSERT_EQUAL_INT(J1939_TP_IGNORED, pc_j1939_tp_feed(&rx, &dt));
    TEST_ASSERT_TRUE(rx.active);
    TEST_ASSERT_EQUAL_UINT8(1, rx.next_seq);
}

// The transport-protocol reassembler's ignore/error branches.
void test_tp_feed_error_paths()
{
    J1939TpRx rx;
    pc_j1939_tp_reset(&rx);
    CanFrame cm;
    pc_j1939_build_bam_cm(&cm, 0x21, 0x00FECA, 16);

    TEST_ASSERT_EQUAL_INT(J1939_TP_IGNORED, pc_j1939_tp_feed(NULL, &cm)); // null rx
    TEST_ASSERT_EQUAL_INT(J1939_TP_IGNORED, pc_j1939_tp_feed(&rx, NULL)); // null frame
    CanFrame notext = cm;
    notext.extended = false;
    TEST_ASSERT_EQUAL_INT(J1939_TP_IGNORED, pc_j1939_tp_feed(&rx, &notext)); // not a 29-bit frame

    CanFrame cm_ctrl = cm;
    cm_ctrl.data[0] = 0xFF; // not BAM/RTS -> not a receiver-side session start
    TEST_ASSERT_EQUAL_INT(J1939_TP_IGNORED, pc_j1939_tp_feed(&rx, &cm_ctrl));

    CanFrame cm_bad = cm;
    cm_bad.data[3] = 99; // packet count != ceil(total/7)
    TEST_ASSERT_EQUAL_INT(J1939_TP_ERROR, pc_j1939_tp_feed(&rx, &cm_bad));

    // A TP.DT with no active session is ignored.
    pc_j1939_tp_reset(&rx);
    const uint8_t chunk[7] = {1, 2, 3, 4, 5, 6, 7};
    CanFrame dt;
    pc_j1939_build_tp_dt(&dt, 0x21, J1939_ADDR_GLOBAL, 1, chunk, 7);
    TEST_ASSERT_EQUAL_INT(J1939_TP_IGNORED, pc_j1939_tp_feed(&rx, &dt));

    // An extended frame whose PGN is neither TP.CM nor TP.DT is ignored.
    CanFrame other;
    pc_j1939_build_message(&other, 6, 0x00FEEE, 0x21, 0xFF, chunk, 3);
    TEST_ASSERT_EQUAL_INT(J1939_TP_IGNORED, pc_j1939_tp_feed(&rx, &other));
}

// --- typed PGN decoders (EEC1 / ET1) ---
void test_decode_eec1()
{
    // 1500 rpm (raw 12000 = 0x2EE0), driver's demand +100 % (raw 225), actual +80 % (raw 205), mode 3.
    const uint8_t data[8] = {0x03, 225, 205, 0xE0, 0x2E, 0xFF, 0xFF, 0xFF};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_j1939_build_message(&f, 3, J1939_PGN_EEC1, 0x00, J1939_ADDR_GLOBAL, data, 8));

    J1939Eec1 e;
    TEST_ASSERT_TRUE(pc_j1939_decode_eec1(&f, &e));
    TEST_ASSERT_EQUAL_UINT8(3, e.torque_mode);
    TEST_ASSERT_EQUAL_INT16(100, e.drivers_demand_torque_pct);
    TEST_ASSERT_EQUAL_INT16(80, e.actual_engine_torque_pct);
    TEST_ASSERT_TRUE(e.engine_speed_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1500.0f, e.engine_speed_rpm);

    // Not-available raws clear the flag / return the torque sentinel.
    const uint8_t na[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CanFrame fna;
    pc_j1939_build_message(&fna, 3, J1939_PGN_EEC1, 0x00, J1939_ADDR_GLOBAL, na, 8);
    TEST_ASSERT_TRUE(pc_j1939_decode_eec1(&fna, &e));
    TEST_ASSERT_FALSE(e.engine_speed_valid);
    TEST_ASSERT_EQUAL_INT16(J1939_TORQUE_NA, e.drivers_demand_torque_pct);
}

void test_decode_et1()
{
    // coolant 90 C (raw 130), fuel 60 C (raw 100), oil 100 C (raw (100+273)/0.03125 = 11936 = 0x2EA0).
    const uint8_t data[8] = {130, 100, 0xA0, 0x2E, 0xFF, 0xFF, 0xFF, 0xFF};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_j1939_build_message(&f, 6, J1939_PGN_ET1, 0x00, J1939_ADDR_GLOBAL, data, 8));

    J1939Et1 t;
    TEST_ASSERT_TRUE(pc_j1939_decode_et1(&f, &t));
    TEST_ASSERT_TRUE(t.coolant_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, t.coolant_temp_c);
    TEST_ASSERT_TRUE(t.fuel_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, t.fuel_temp_c);
    TEST_ASSERT_TRUE(t.oil_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, t.oil_temp_c);

    // An oil raw in the not-available range clears just that flag.
    const uint8_t na[8] = {130, 100, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CanFrame fna;
    pc_j1939_build_message(&fna, 6, J1939_PGN_ET1, 0x00, J1939_ADDR_GLOBAL, na, 8);
    pc_j1939_decode_et1(&fna, &t);
    TEST_ASSERT_TRUE(t.coolant_valid);
    TEST_ASSERT_FALSE(t.oil_valid);
}

void test_decode_lfe()
{
    // fuel rate 20.0 L/h (raw 400), instant econ 5.0 km/L (raw 2560), avg 4.5 (raw 2304), throttle 40 % (raw 100).
    const uint8_t data[8] = {0x90, 0x01, 0x00, 0x0A, 0x00, 0x09, 100, 0xFF};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_j1939_build_message(&f, 6, J1939_PGN_LFE, 0x00, J1939_ADDR_GLOBAL, data, 8));

    J1939Lfe l;
    TEST_ASSERT_TRUE(pc_j1939_decode_lfe(&f, &l));
    TEST_ASSERT_TRUE(l.fuel_rate_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, l.fuel_rate_lph);
    TEST_ASSERT_TRUE(l.instant_econ_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, l.instant_econ_kmpl);
    TEST_ASSERT_TRUE(l.avg_econ_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.5f, l.avg_econ_kmpl);
    TEST_ASSERT_TRUE(l.throttle_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, l.throttle_pct);

    // A not-available fuel rate clears just that flag.
    const uint8_t na[8] = {0xFF, 0xFF, 0x00, 0x0A, 0x00, 0x09, 100, 0xFF};
    CanFrame fna;
    pc_j1939_build_message(&fna, 6, J1939_PGN_LFE, 0x00, J1939_ADDR_GLOBAL, na, 8);
    pc_j1939_decode_lfe(&fna, &l);
    TEST_ASSERT_FALSE(l.fuel_rate_valid);
    TEST_ASSERT_TRUE(l.instant_econ_valid);
    // A non-LFE frame (EEC1) is rejected.
    CanFrame eec1;
    pc_j1939_build_message(&eec1, 3, J1939_PGN_EEC1, 0x00, J1939_ADDR_GLOBAL, data, 8);
    TEST_ASSERT_FALSE(pc_j1939_decode_lfe(&eec1, &l));
}

void test_decode_amb()
{
    // baro 101.5 kPa (raw 203), cab 21.5 C (raw 9424), ambient 15.0 C (raw 9216),
    // inlet 30 C (raw 70), road 12.0 C (raw 9120).
    const uint8_t data[8] = {0xCB, 0xD0, 0x24, 0x00, 0x24, 0x46, 0xA0, 0x23};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_j1939_build_message(&f, 6, J1939_PGN_AMB, 0x00, J1939_ADDR_GLOBAL, data, 8));

    J1939Amb a;
    TEST_ASSERT_TRUE(pc_j1939_decode_amb(&f, &a));
    TEST_ASSERT_TRUE(a.baro_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 101.5f, a.baro_kpa);
    TEST_ASSERT_TRUE(a.cab_temp_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.5f, a.cab_temp_c);
    TEST_ASSERT_TRUE(a.ambient_temp_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.0f, a.ambient_temp_c);
    TEST_ASSERT_TRUE(a.inlet_temp_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, a.inlet_temp_c);
    TEST_ASSERT_TRUE(a.road_temp_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.0f, a.road_temp_c);

    // A not-available barometric pressure clears just that flag; the ambient temperature stays valid.
    const uint8_t na[8] = {0xFF, 0xD0, 0x24, 0x00, 0x24, 0x46, 0xA0, 0x23};
    CanFrame fna;
    pc_j1939_build_message(&fna, 6, J1939_PGN_AMB, 0x00, J1939_ADDR_GLOBAL, na, 8);
    pc_j1939_decode_amb(&fna, &a);
    TEST_ASSERT_FALSE(a.baro_valid);
    TEST_ASSERT_TRUE(a.ambient_temp_valid);
    // A non-AMB frame (LFE) is rejected.
    CanFrame lfe;
    pc_j1939_build_message(&lfe, 6, J1939_PGN_LFE, 0x00, J1939_ADDR_GLOBAL, data, 8);
    TEST_ASSERT_FALSE(pc_j1939_decode_amb(&lfe, &a));
}

void test_decode_ic1()
{
    // trap inlet 25 kPa (raw 50), boost 200 kPa (raw 100), intake 60 C (raw 100), air inlet 100 kPa (raw 50),
    // air filter 2.5 kPa (raw 50), exhaust 400 C (raw 21536), coolant filter 10 kPa (raw 20).
    const uint8_t data[8] = {0x32, 0x64, 0x64, 0x32, 0x32, 0x20, 0x54, 0x14};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_j1939_build_message(&f, 6, J1939_PGN_IC1, 0x00, J1939_ADDR_GLOBAL, data, 8));

    J1939Ic1 c;
    TEST_ASSERT_TRUE(pc_j1939_decode_ic1(&f, &c));
    TEST_ASSERT_TRUE(c.trap_inlet_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, c.trap_inlet_kpa);
    TEST_ASSERT_TRUE(c.boost_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 200.0f, c.boost_kpa);
    TEST_ASSERT_TRUE(c.intake_temp_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, c.intake_temp_c);
    TEST_ASSERT_TRUE(c.air_inlet_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, c.air_inlet_kpa);
    TEST_ASSERT_TRUE(c.air_filter_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, c.air_filter_kpa);
    TEST_ASSERT_TRUE(c.exhaust_temp_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 400.0f, c.exhaust_temp_c);
    TEST_ASSERT_TRUE(c.coolant_filter_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, c.coolant_filter_kpa);

    // A not-available boost pressure clears just that flag; the intake temperature stays valid.
    const uint8_t na[8] = {0x32, 0xFF, 0x64, 0x32, 0x32, 0x20, 0x54, 0x14};
    CanFrame fna;
    pc_j1939_build_message(&fna, 6, J1939_PGN_IC1, 0x00, J1939_ADDR_GLOBAL, na, 8);
    pc_j1939_decode_ic1(&fna, &c);
    TEST_ASSERT_FALSE(c.boost_valid);
    TEST_ASSERT_TRUE(c.intake_temp_valid);
    // A non-IC1 frame (AMB) is rejected.
    CanFrame amb;
    pc_j1939_build_message(&amb, 6, J1939_PGN_AMB, 0x00, J1939_ADDR_GLOBAL, data, 8);
    TEST_ASSERT_FALSE(pc_j1939_decode_ic1(&amb, &c));
}

void test_decode_vd()
{
    // trip 1000.000 km (raw 8000 = 0x1F40), total 250000.000 km (raw 2,000,000 = 0x1E8480).
    const uint8_t data[8] = {0x40, 0x1F, 0x00, 0x00, 0x80, 0x84, 0x1E, 0x00};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_j1939_build_message(&f, 6, J1939_PGN_VD, 0x00, J1939_ADDR_GLOBAL, data, 8));

    J1939Vd v;
    TEST_ASSERT_TRUE(pc_j1939_decode_vd(&f, &v));
    TEST_ASSERT_TRUE(v.trip_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1000.0f, (float)v.trip_km);
    TEST_ASSERT_TRUE(v.total_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 250000.0f, (float)v.total_km);

    // A near-max odometer (raw 0xFAFFFFFF) keeps full precision in the double; a float would round it up.
    const uint8_t big[8] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFA};
    CanFrame fb;
    pc_j1939_build_message(&fb, 6, J1939_PGN_VD, 0x00, J1939_ADDR_GLOBAL, big, 8);
    pc_j1939_decode_vd(&fb, &v);
    TEST_ASSERT_TRUE(v.total_valid);
    TEST_ASSERT_EQUAL_UINT32(0xFAFFFFFFu, (uint32_t)(v.total_km / 0.125 + 0.5)); // exact round-trip

    // A not-available trip distance clears just that flag; the total stays valid.
    const uint8_t na[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x84, 0x1E, 0x00};
    CanFrame fna;
    pc_j1939_build_message(&fna, 6, J1939_PGN_VD, 0x00, J1939_ADDR_GLOBAL, na, 8);
    pc_j1939_decode_vd(&fna, &v);
    TEST_ASSERT_FALSE(v.trip_valid);
    TEST_ASSERT_TRUE(v.total_valid);
    // A non-VD frame (IC1) is rejected.
    CanFrame ic1;
    pc_j1939_build_message(&ic1, 6, J1939_PGN_IC1, 0x00, J1939_ADDR_GLOBAL, data, 8);
    TEST_ASSERT_FALSE(pc_j1939_decode_vd(&ic1, &v));
}

void test_decode_ccvs()
{
    // Wheel-based vehicle speed 65.5 km/h (raw 65.5*256 = 16768 = 0x4180, LE bytes 80 41); cruise-active = 1
    // in byte 4 bits 1-2 (data[3] low 2 bits) - the upper bits of data[3] are set to prove the mask.
    const uint8_t data[8] = {0xFF, 0x80, 0x41, 0xF5, 0xFF, 0xFF, 0xFF, 0xFF};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_j1939_build_message(&f, 6, J1939_PGN_CCVS, 0x00, J1939_ADDR_GLOBAL, data, 8));

    J1939Ccvs c;
    TEST_ASSERT_TRUE(pc_j1939_decode_ccvs(&f, &c));
    TEST_ASSERT_TRUE(c.speed_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 65.5f, c.wheel_speed_kmh);
    TEST_ASSERT_EQUAL_UINT8(1, c.cruise_active); // low 2 bits of 0xF5

    // A not-available wheel speed (0xFFFF) clears the validity flag; data[3]=0 gives cruise_active 0.
    const uint8_t na[8] = {0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    CanFrame fna;
    pc_j1939_build_message(&fna, 6, J1939_PGN_CCVS, 0x00, J1939_ADDR_GLOBAL, na, 8);
    TEST_ASSERT_TRUE(pc_j1939_decode_ccvs(&fna, &c));
    TEST_ASSERT_FALSE(c.speed_valid);
    TEST_ASSERT_EQUAL_UINT8(0, c.cruise_active);

    // A non-CCVS frame (ET1) is rejected.
    CanFrame et1;
    pc_j1939_build_message(&et1, 6, J1939_PGN_ET1, 0x00, J1939_ADDR_GLOBAL, data, 8);
    TEST_ASSERT_FALSE(pc_j1939_decode_ccvs(&et1, &c));
}

void test_decode_dm1()
{
    J1939Dm1 dm;
    J1939Dtc dtcs[4];
    // Single-frame DM1: amber warning on, one DTC (SPN 100 oil pressure, FMI 1, OC 5), 0xFF padding.
    const uint8_t single[8] = {0x04, 0x00, 0x64, 0x00, 0x01, 0x05, 0xFF, 0xFF};
    TEST_ASSERT_TRUE(pc_j1939_decode_dm1(single, sizeof(single), &dm, dtcs, 4));
    TEST_ASSERT_EQUAL_UINT8(0, dm.protect);
    TEST_ASSERT_EQUAL_UINT8(1, dm.amber_warning);
    TEST_ASSERT_EQUAL_UINT8(0, dm.red_stop);
    TEST_ASSERT_EQUAL_UINT8(0, dm.mil);
    TEST_ASSERT_EQUAL_UINT8(1, dm.dtc_count);
    TEST_ASSERT_EQUAL_UINT32(100, dtcs[0].spn);
    TEST_ASSERT_EQUAL_UINT8(1, dtcs[0].fmi);
    TEST_ASSERT_EQUAL_UINT8(0, dtcs[0].cm);
    TEST_ASSERT_EQUAL_UINT8(5, dtcs[0].oc);

    // A multi-DTC DM1 (as reassembled over TP): amber + red on, two DTCs.
    const uint8_t multi[10] = {0x14, 0x00, 0x64, 0x00, 0x01, 0x05, 0xBE, 0x00, 0x00, 0x02};
    TEST_ASSERT_TRUE(pc_j1939_decode_dm1(multi, sizeof(multi), &dm, dtcs, 4));
    TEST_ASSERT_EQUAL_UINT8(1, dm.amber_warning);
    TEST_ASSERT_EQUAL_UINT8(1, dm.red_stop);
    TEST_ASSERT_EQUAL_UINT8(2, dm.dtc_count);
    TEST_ASSERT_EQUAL_UINT32(190, dtcs[1].spn); // SPN 190 engine speed, FMI 0, OC 2
    TEST_ASSERT_EQUAL_UINT8(0, dtcs[1].fmi);
    TEST_ASSERT_EQUAL_UINT8(2, dtcs[1].oc);

    // The DTC array is clamped to @p max.
    TEST_ASSERT_TRUE(pc_j1939_decode_dm1(multi, sizeof(multi), &dm, dtcs, 1));
    TEST_ASSERT_EQUAL_UINT8(1, dm.dtc_count);

    // A "no active fault" DM1 is the all-zero placeholder DTC -> lamps off, zero DTCs.
    const uint8_t none[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(pc_j1939_decode_dm1(none, sizeof(none), &dm, dtcs, 4));
    TEST_ASSERT_EQUAL_UINT8(0, dm.dtc_count);
    TEST_ASSERT_EQUAL_UINT8(0, dm.amber_warning);

    // A null DTC array reads just the lamps (dtc_count still counts). Short/null bodies fail closed.
    TEST_ASSERT_TRUE(pc_j1939_decode_dm1(single, sizeof(single), &dm, NULL, 0));
    TEST_ASSERT_EQUAL_UINT8(1, dm.amber_warning);
    TEST_ASSERT_FALSE(pc_j1939_decode_dm1(single, 1, &dm, dtcs, 4));
    TEST_ASSERT_FALSE(pc_j1939_decode_dm1(NULL, 8, &dm, dtcs, 4));
}

void test_decode_pgn_mismatch_and_guards()
{
    const uint8_t data[8] = {0};
    CanFrame et1;
    pc_j1939_build_message(&et1, 6, J1939_PGN_ET1, 0x00, J1939_ADDR_GLOBAL, data, 8);
    J1939Eec1 e;
    TEST_ASSERT_FALSE(pc_j1939_decode_eec1(&et1, &e)); // an ET1 frame is not EEC1
    J1939Et1 t;
    CanFrame eec1;
    pc_j1939_build_message(&eec1, 3, J1939_PGN_EEC1, 0x00, J1939_ADDR_GLOBAL, data, 8);
    TEST_ASSERT_FALSE(pc_j1939_decode_et1(&eec1, &t)); // an EEC1 frame is not ET1
    // Short DLC + null guards.
    eec1.dlc = 5;
    TEST_ASSERT_FALSE(pc_j1939_decode_eec1(&eec1, &e));
    TEST_ASSERT_FALSE(pc_j1939_decode_eec1(NULL, &e));
    TEST_ASSERT_FALSE(pc_j1939_decode_et1(&et1, NULL));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_id_pdu2_roundtrip);
    RUN_TEST(test_id_pdu1_roundtrip);
    RUN_TEST(test_encode_rejects_bad_args);
    RUN_TEST(test_build_single_frame);
    RUN_TEST(test_request_pgn);
    RUN_TEST(test_address_claim_name);
    RUN_TEST(test_tp_num_packets);
    RUN_TEST(test_tp_bam_roundtrip);
    RUN_TEST(test_tp_out_of_sequence_errors);
    RUN_TEST(test_build_error_paths);
    RUN_TEST(test_tp_feed_error_paths);
    RUN_TEST(test_null_guard_paths);
    RUN_TEST(test_build_message_length_edges);
    RUN_TEST(test_build_name_not_arbitrary_capable);
    RUN_TEST(test_build_bam_cm_too_large);
    RUN_TEST(test_tp_feed_short_cm_frame_ignored);
    RUN_TEST(test_tp_feed_rts_starts_session);
    RUN_TEST(test_tp_feed_cm_total_size_out_of_range);
    RUN_TEST(test_tp_feed_dt_short_frame_ignored);
    RUN_TEST(test_tp_feed_dt_wrong_source_ignored);
    RUN_TEST(test_decode_eec1);
    RUN_TEST(test_decode_et1);
    RUN_TEST(test_decode_lfe);
    RUN_TEST(test_decode_amb);
    RUN_TEST(test_decode_ic1);
    RUN_TEST(test_decode_vd);
    RUN_TEST(test_decode_ccvs);
    RUN_TEST(test_decode_dm1);
    RUN_TEST(test_decode_pgn_mismatch_and_guards);
    return UNITY_END();
}
