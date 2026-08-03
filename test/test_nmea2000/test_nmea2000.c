// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the NMEA 2000 codec (services/timing_position/nmea2000): single-frame messages (J1939-based)
// and the Fast Packet transport (frame count, build, reassembly round-trip, out-of-order +
// interleaved-sequence handling). Pure host tests.

#include "services/timing_position/nmea2000/nmea2000.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_num_frames()
{
    TEST_ASSERT_EQUAL_UINT8(1, pc_n2k_fastpacket_num_frames(6));    // fits frame 0
    TEST_ASSERT_EQUAL_UINT8(2, pc_n2k_fastpacket_num_frames(7));    // 6 + 1
    TEST_ASSERT_EQUAL_UINT8(2, pc_n2k_fastpacket_num_frames(13));   // 6 + 7
    TEST_ASSERT_EQUAL_UINT8(3, pc_n2k_fastpacket_num_frames(14));   // 6 + 7 + 1
    TEST_ASSERT_EQUAL_UINT8(32, pc_n2k_fastpacket_num_frames(223)); // 6 + 31*7
}

void test_single_frame()
{
    const uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_n2k_build_single(&f, 2, 0x01F200, 0x15, 0xFF, payload, 8)); // a PDU2 PGN
    TEST_ASSERT_TRUE(f.extended);
    TEST_ASSERT_EQUAL_UINT8(8, f.dlc);
    TEST_ASSERT_EQUAL_MEMORY(payload, f.data, 8);
    J1939Id d;
    TEST_ASSERT_TRUE(pc_j1939_decode_id(f.id, &d));
    TEST_ASSERT_EQUAL_HEX32(0x01F200, d.pgn);
}

// Build all Fast Packet frames of a 20-octet message, feed them back, reassemble.
void test_fastpacket_roundtrip()
{
    uint8_t msg[20];
    for (int i = 0; i < 20; i++)
    {
        msg[i] = (uint8_t)(0x40 + i);
    }
    const uint32_t pgn = 0x01F801; // e.g. position rapid update, a Fast Packet PGN
    const uint8_t sa = 0x15;
    const uint8_t seq = 3;

    uint8_t frames = pc_n2k_fastpacket_num_frames(20); // 6 + 7 + 7 = 3
    TEST_ASSERT_EQUAL_UINT8(3, frames);

    N2kFastPacketRx rx;
    pc_n2k_fastpacket_reset(&rx);
    for (uint8_t i = 0; i < frames; i++)
    {
        CanFrame f;
        TEST_ASSERT_TRUE(pc_n2k_fastpacket_build_frame(&f, seq, i, 6, pgn, sa, 0xFF, msg, 20));
        N2kFpResult r = pc_n2k_fastpacket_feed(&rx, &f);
        if (i == 0)
        {
            TEST_ASSERT_EQUAL_INT(N2K_FP_STARTED, r);
        }
        else if (i + 1 < frames)
        {
            TEST_ASSERT_EQUAL_INT(N2K_FP_PROGRESS, r);
        }
        else
        {
            TEST_ASSERT_EQUAL_INT(N2K_FP_COMPLETE, r);
        }
    }
    TEST_ASSERT_EQUAL_UINT16(20, rx.total_len);
    TEST_ASSERT_EQUAL_HEX32(pgn, rx.pgn);
    TEST_ASSERT_EQUAL_MEMORY(msg, rx.buf, 20);
}

// A message that fits entirely in frame 0 completes immediately.
void test_fastpacket_single_frame_completes()
{
    uint8_t msg[5] = {9, 8, 7, 6, 5};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_n2k_fastpacket_build_frame(&f, 0, 0, 6, 0x01F200, 0x15, 0xFF, msg, 5));
    N2kFastPacketRx rx;
    pc_n2k_fastpacket_reset(&rx);
    TEST_ASSERT_EQUAL_INT(N2K_FP_COMPLETE, pc_n2k_fastpacket_feed(&rx, &f));
    TEST_ASSERT_EQUAL_UINT16(5, rx.total_len);
    TEST_ASSERT_EQUAL_MEMORY(msg, rx.buf, 5);
}

// A continuation frame from a different sequence is ignored, not merged.
void test_fastpacket_interleaved_sequence_ignored()
{
    uint8_t msg[20];
    for (int i = 0; i < 20; i++)
    {
        msg[i] = (uint8_t)i;
    }
    N2kFastPacketRx rx;
    pc_n2k_fastpacket_reset(&rx);
    CanFrame f0;
    pc_n2k_fastpacket_build_frame(&f0, 3, 0, 6, 0x01F801, 0x15, 0xFF, msg, 20);
    TEST_ASSERT_EQUAL_INT(N2K_FP_STARTED, pc_n2k_fastpacket_feed(&rx, &f0));

    // A frame from sequence 4 (different message) must not be accepted into seq 3.
    CanFrame other;
    pc_n2k_fastpacket_build_frame(&other, 4, 1, 6, 0x01F801, 0x15, 0xFF, msg, 20);
    TEST_ASSERT_EQUAL_INT(N2K_FP_IGNORED, pc_n2k_fastpacket_feed(&rx, &other));
    TEST_ASSERT_TRUE(rx.active); // seq 3 still in progress
}

void test_fastpacket_out_of_order_errors()
{
    uint8_t msg[20] = {0};
    N2kFastPacketRx rx;
    pc_n2k_fastpacket_reset(&rx);
    CanFrame f0;
    pc_n2k_fastpacket_build_frame(&f0, 3, 0, 6, 0x01F801, 0x15, 0xFF, msg, 20);
    TEST_ASSERT_EQUAL_INT(N2K_FP_STARTED, pc_n2k_fastpacket_feed(&rx, &f0));
    CanFrame f2;
    pc_n2k_fastpacket_build_frame(&f2, 3, 2, 6, 0x01F801, 0x15, 0xFF, msg, 20); // skip frame 1
    TEST_ASSERT_EQUAL_INT(N2K_FP_ERR, pc_n2k_fastpacket_feed(&rx, &f2));
    TEST_ASSERT_FALSE(rx.active);
}

// Fast Packet builder guards, an out-of-range frame index, an encode failure (priority>7),
// and the feed reject branches (null/non-extended/short frame + a bad first-frame length).
void test_nmea2000_error_paths()
{
    CanFrame f;
    const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    TEST_ASSERT_FALSE(pc_n2k_fastpacket_build_frame(NULL, 0, 0, 6, 0x01F801, 0x15, 0xFF, data, 8)); // null out
    TEST_ASSERT_FALSE(pc_n2k_fastpacket_build_frame(&f, 0, 0, 6, 0x01F801, 0x15, 0xFF, NULL, 8));   // null data
    TEST_ASSERT_FALSE(pc_n2k_fastpacket_build_frame(&f, 8, 0, 6, 0x01F801, 0x15, 0xFF, data, 8));      // seq > 7
    TEST_ASSERT_FALSE(pc_n2k_fastpacket_build_frame(&f, 0, 0, 6, 0x01F801, 0x15, 0xFF, data, 0));      // total_len 0
    TEST_ASSERT_FALSE(
        pc_n2k_fastpacket_build_frame(&f, 0, 5, 6, 0x01F801, 0x15, 0xFF, data, 8)); // frame_idx past the count
    TEST_ASSERT_FALSE(
        pc_n2k_fastpacket_build_frame(&f, 0, 0, 8, 0x01F801, 0x15, 0xFF, data, 8)); // priority>7 -> encode fails

    N2kFastPacketRx rx;
    pc_n2k_fastpacket_reset(&rx);
    pc_n2k_fastpacket_build_frame(&f, 0, 0, 6, 0x01F801, 0x15, 0xFF, data, 8);
    TEST_ASSERT_EQUAL_INT(N2K_FP_IGNORED, pc_n2k_fastpacket_feed(NULL, &f));  // null rx
    TEST_ASSERT_EQUAL_INT(N2K_FP_IGNORED, pc_n2k_fastpacket_feed(&rx, NULL)); // null frame
    CanFrame notext = f;
    notext.extended = false;
    TEST_ASSERT_EQUAL_INT(N2K_FP_IGNORED, pc_n2k_fastpacket_feed(&rx, &notext)); // not a 29-bit frame
    CanFrame shortdlc = f;
    shortdlc.dlc = 1;
    TEST_ASSERT_EQUAL_INT(N2K_FP_IGNORED, pc_n2k_fastpacket_feed(&rx, &shortdlc)); // dlc < 2

    CanFrame bad_total = f;
    bad_total.data[1] = 0; // first frame declaring a zero total length
    TEST_ASSERT_EQUAL_INT(N2K_FP_ERR, pc_n2k_fastpacket_feed(&rx, &bad_total));
}

// Fast Packet builder rejects a total_len beyond PC_N2K_FP_MAX (the last guard clause of
// pc_n2k_fastpacket_build_frame, distinct from the total_len == 0 case already covered above).
void test_fastpacket_build_frame_total_too_large()
{
    CanFrame f;
    const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_FALSE(pc_n2k_fastpacket_build_frame(&f, 0, 0, 6, 0x01F801, 0x15, 0xFF, data, PC_N2K_FP_MAX + 1));
}

// Resetting a null context must be a safe no-op, not a crash.
void test_fastpacket_reset_null_is_safe()
{
    pc_n2k_fastpacket_reset(NULL);
    TEST_ASSERT_TRUE(true);
}

// A first frame declaring a total length beyond PC_N2K_FP_MAX is rejected by the reassembler
// (distinct from the total == 0 case already covered by test_nmea2000_error_paths).
void test_fastpacket_feed_total_too_large_errors()
{
    uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CanFrame f;
    TEST_ASSERT_TRUE(pc_n2k_fastpacket_build_frame(&f, 0, 0, 6, 0x01F801, 0x15, 0xFF, data, 8));
    f.data[1] = 250; // > PC_N2K_FP_MAX (223)
    N2kFastPacketRx rx;
    pc_n2k_fastpacket_reset(&rx);
    TEST_ASSERT_EQUAL_INT(N2K_FP_ERR, pc_n2k_fastpacket_feed(&rx, &f));
    TEST_ASSERT_FALSE(rx.active);
}

// A continuation frame arriving with no sequence in progress is ignored (rx->active is false).
void test_fastpacket_continuation_without_active_sequence_ignored()
{
    uint8_t msg[20] = {0};
    N2kFastPacketRx rx;
    pc_n2k_fastpacket_reset(&rx);
    CanFrame f1;
    TEST_ASSERT_TRUE(pc_n2k_fastpacket_build_frame(&f1, 3, 1, 6, 0x01F801, 0x15, 0xFF, msg, 20));
    TEST_ASSERT_EQUAL_INT(N2K_FP_IGNORED, pc_n2k_fastpacket_feed(&rx, &f1));
    TEST_ASSERT_FALSE(rx.active);
}

// A continuation frame with the matching sequence counter but a different source address must
// not be merged into the in-progress message.
void test_fastpacket_continuation_wrong_source_ignored()
{
    uint8_t msg[20];
    for (int i = 0; i < 20; i++)
    {
        msg[i] = (uint8_t)i;
    }
    N2kFastPacketRx rx;
    pc_n2k_fastpacket_reset(&rx);
    CanFrame f0;
    pc_n2k_fastpacket_build_frame(&f0, 3, 0, 6, 0x01F801, 0x15, 0xFF, msg, 20);
    TEST_ASSERT_EQUAL_INT(N2K_FP_STARTED, pc_n2k_fastpacket_feed(&rx, &f0));

    CanFrame other_sa;
    pc_n2k_fastpacket_build_frame(&other_sa, 3, 1, 6, 0x01F801, 0x22, 0xFF, msg, 20); // different sa
    TEST_ASSERT_EQUAL_INT(N2K_FP_IGNORED, pc_n2k_fastpacket_feed(&rx, &other_sa));
    TEST_ASSERT_TRUE(rx.active); // sequence 3 still in progress
}

// A continuation frame with the matching sequence counter and source but a different PGN must
// not be merged into the in-progress message.
void test_fastpacket_continuation_wrong_pgn_ignored()
{
    uint8_t msg[20];
    for (int i = 0; i < 20; i++)
    {
        msg[i] = (uint8_t)i;
    }
    N2kFastPacketRx rx;
    pc_n2k_fastpacket_reset(&rx);
    CanFrame f0;
    pc_n2k_fastpacket_build_frame(&f0, 3, 0, 6, 0x01F801, 0x15, 0xFF, msg, 20);
    TEST_ASSERT_EQUAL_INT(N2K_FP_STARTED, pc_n2k_fastpacket_feed(&rx, &f0));

    CanFrame other_pgn;
    pc_n2k_fastpacket_build_frame(&other_pgn, 3, 1, 6, 0x01FD00, 0x15, 0xFF, msg, 20); // different pgn
    TEST_ASSERT_EQUAL_INT(N2K_FP_IGNORED, pc_n2k_fastpacket_feed(&rx, &other_pgn));
    TEST_ASSERT_TRUE(rx.active); // sequence 3 still in progress
}

// A 19-octet message's final (3rd) continuation frame carries only 6 remaining octets, fewer
// than a full N2K_FP_FN_DATA continuation payload; exercises the "short last frame" branch in
// both the builder and the reassembler.
void test_fastpacket_roundtrip_short_last_frame()
{
    uint8_t msg[19];
    for (int i = 0; i < 19; i++)
    {
        msg[i] = (uint8_t)(0x60 + i);
    }
    const uint32_t pgn = 0x01F801;
    const uint8_t sa = 0x15;
    const uint8_t seq = 5;

    uint8_t frames = pc_n2k_fastpacket_num_frames(19);
    TEST_ASSERT_EQUAL_UINT8(3, frames);

    N2kFastPacketRx rx;
    pc_n2k_fastpacket_reset(&rx);
    for (uint8_t i = 0; i < frames; i++)
    {
        CanFrame f;
        TEST_ASSERT_TRUE(pc_n2k_fastpacket_build_frame(&f, seq, i, 6, pgn, sa, 0xFF, msg, 19));
        N2kFpResult r = pc_n2k_fastpacket_feed(&rx, &f);
        if (i == 0)
        {
            TEST_ASSERT_EQUAL_INT(N2K_FP_STARTED, r);
        }
        else if (i + 1 < frames)
        {
            TEST_ASSERT_EQUAL_INT(N2K_FP_PROGRESS, r);
        }
        else
        {
            TEST_ASSERT_EQUAL_INT(N2K_FP_COMPLETE, r);
        }
    }
    TEST_ASSERT_EQUAL_UINT16(19, rx.total_len);
    TEST_ASSERT_EQUAL_HEX32(pgn, rx.pgn);
    TEST_ASSERT_EQUAL_MEMORY(msg, rx.buf, 19);
}

// --- typed PGN decoders ---
void test_decode_position_rapid()
{
    // lat 37.3749, lon -122.0841 (1e-7 deg/bit), little-endian.
    const uint8_t pos[8] = {0x08, 0xf5, 0x46, 0x16, 0xd8, 0x71, 0x3b, 0xb7};
    N2kPositionRapid p;
    TEST_ASSERT_TRUE(pc_n2k_decode_position_rapid(pos, sizeof(pos), &p));
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 37.3749f, (float)p.lat_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -122.0841f, (float)p.lon_deg);

    // 0x7FFFFFFF in either coordinate is not-available.
    const uint8_t na[8] = {0xFF, 0xFF, 0xFF, 0x7F, 0xd8, 0x71, 0x3b, 0xb7};
    TEST_ASSERT_TRUE(pc_n2k_decode_position_rapid(na, sizeof(na), &p));
    TEST_ASSERT_FALSE(p.valid);
    // Short payload + nulls are rejected.
    TEST_ASSERT_FALSE(pc_n2k_decode_position_rapid(pos, 7, &p));
    TEST_ASSERT_FALSE(pc_n2k_decode_position_rapid(NULL, 8, &p));
}

void test_decode_cog_sog_rapid()
{
    // sid 0x11, ref magnetic, COG 1.5708 rad (raw 15708), SOG 6.17 m/s (raw 617).
    const uint8_t cs[8] = {0x11, 0xfd, 0x5c, 0x3d, 0x69, 0x02, 0xff, 0xff};
    N2kCogSogRapid c;
    TEST_ASSERT_TRUE(pc_n2k_decode_cog_sog_rapid(cs, sizeof(cs), &c));
    TEST_ASSERT_EQUAL_UINT8(0x11, c.sid);
    TEST_ASSERT_EQUAL_UINT8(N2K_COG_REF_MAGNETIC, c.cog_ref);
    TEST_ASSERT_TRUE(c.cog_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 1.5708f, c.cog_rad);
    TEST_ASSERT_TRUE(c.sog_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.17f, c.sog_mps);

    // A 0xFFFF COG is not-available; the SOG stays valid, reference True.
    const uint8_t na[8] = {0x11, 0xfc, 0xff, 0xff, 0x69, 0x02, 0xff, 0xff};
    TEST_ASSERT_TRUE(pc_n2k_decode_cog_sog_rapid(na, sizeof(na), &c));
    TEST_ASSERT_EQUAL_UINT8(N2K_COG_REF_TRUE, c.cog_ref);
    TEST_ASSERT_FALSE(c.cog_valid);
    TEST_ASSERT_TRUE(c.sog_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.17f, c.sog_mps);
    // Short payload + nulls are rejected.
    TEST_ASSERT_FALSE(pc_n2k_decode_cog_sog_rapid(cs, 5, &c));
    TEST_ASSERT_FALSE(pc_n2k_decode_cog_sog_rapid(NULL, 8, &c));
}

void test_decode_engine_rapid()
{
    // instance 0, speed 2400 rpm (raw 9600), boost 150000 Pa (raw 1500), tilt +15 %.
    const uint8_t er[8] = {0x00, 0x80, 0x25, 0xdc, 0x05, 0x0f, 0xff, 0xff};
    N2kEngineRapid e;
    TEST_ASSERT_TRUE(pc_n2k_decode_engine_rapid(er, sizeof(er), &e));
    TEST_ASSERT_EQUAL_UINT8(0, e.instance);
    TEST_ASSERT_TRUE(e.speed_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 2400.0f, e.speed_rpm);
    TEST_ASSERT_TRUE(e.boost_valid);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 150000.0f, e.boost_pa);
    TEST_ASSERT_TRUE(e.tilt_valid);
    TEST_ASSERT_EQUAL_INT8(15, e.tilt_pct);

    // A 0xFFFF speed is not-available; a signed 0x7F tilt is not-available; a negative tilt decodes.
    const uint8_t na[8] = {0x01, 0xff, 0xff, 0xdc, 0x05, 0xf6, 0xff, 0xff};
    TEST_ASSERT_TRUE(pc_n2k_decode_engine_rapid(na, sizeof(na), &e));
    TEST_ASSERT_EQUAL_UINT8(1, e.instance);
    TEST_ASSERT_FALSE(e.speed_valid);
    TEST_ASSERT_TRUE(e.boost_valid);
    TEST_ASSERT_TRUE(e.tilt_valid);
    TEST_ASSERT_EQUAL_INT8(-10, e.tilt_pct); // 0xF6 as int8
    // Short payload + nulls are rejected.
    TEST_ASSERT_FALSE(pc_n2k_decode_engine_rapid(er, 5, &e));
    TEST_ASSERT_FALSE(pc_n2k_decode_engine_rapid(NULL, 8, &e));
}

void test_decode_temperature()
{
    // sid 5, instance 0, source inside, actual 25.0 C (raw 29815 = 298.15 K), set 20.0 C (raw 29315).
    const uint8_t t[8] = {0x05, 0x00, 0x02, 0x77, 0x74, 0x83, 0x72, 0xff};
    N2kTemperature d;
    TEST_ASSERT_TRUE(pc_n2k_decode_temperature(t, sizeof(t), &d));
    TEST_ASSERT_EQUAL_UINT8(5, d.sid);
    TEST_ASSERT_EQUAL_UINT8(0, d.instance);
    TEST_ASSERT_EQUAL_UINT8(N2K_TEMP_SRC_INSIDE, d.source);
    TEST_ASSERT_TRUE(d.actual_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, d.actual_c);
    TEST_ASSERT_TRUE(d.set_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, d.set_c);

    // A 0xFFFF set temperature is not-available; the actual stays valid.
    const uint8_t na[8] = {0x05, 0x00, 0x00, 0x77, 0x74, 0xff, 0xff, 0xff};
    TEST_ASSERT_TRUE(pc_n2k_decode_temperature(na, sizeof(na), &d));
    TEST_ASSERT_EQUAL_UINT8(N2K_TEMP_SRC_SEA, d.source);
    TEST_ASSERT_TRUE(d.actual_valid);
    TEST_ASSERT_FALSE(d.set_valid);
    // Short payload + nulls are rejected.
    TEST_ASSERT_FALSE(pc_n2k_decode_temperature(t, 6, &d));
    TEST_ASSERT_FALSE(pc_n2k_decode_temperature(NULL, 8, &d));
}

void test_decode_battery_status()
{
    // instance 1, voltage 12.6 V (raw 1260 = 0x04EC), current 5.5 A (raw 55), temp 25.0 C (raw 29815 = 0x7477), sid 10.
    const uint8_t b[8] = {0x01, 0xEC, 0x04, 0x37, 0x00, 0x77, 0x74, 0x0A};
    N2kBatteryStatus d;
    TEST_ASSERT_TRUE(pc_n2k_decode_battery_status(b, sizeof(b), &d));
    TEST_ASSERT_EQUAL_UINT8(1, d.instance);
    TEST_ASSERT_TRUE(d.voltage_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.6f, d.voltage_v);
    TEST_ASSERT_TRUE(d.current_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.5f, d.current_a);
    TEST_ASSERT_TRUE(d.temp_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, d.temp_c);
    TEST_ASSERT_EQUAL_UINT8(10, d.sid);

    // A discharging (negative) current, and a not-available voltage (0x7FFF) that clears just its flag.
    const uint8_t neg[8] = {0x01, 0xFF, 0x7F, 0x9C,
                            0xFF, 0x77, 0x74, 0x0A}; // voltage 0x7FFF n/a, current -100 -> -10.0 A
    TEST_ASSERT_TRUE(pc_n2k_decode_battery_status(neg, sizeof(neg), &d));
    TEST_ASSERT_FALSE(d.voltage_valid);
    TEST_ASSERT_TRUE(d.current_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.0f, d.current_a);

    // Short payload + nulls are rejected.
    TEST_ASSERT_FALSE(pc_n2k_decode_battery_status(b, 7, &d));
    TEST_ASSERT_FALSE(pc_n2k_decode_battery_status(NULL, 8, &d));
}

void test_decode_fluid_level()
{
    // instance 2, fuel (type 0) -> byte0 0x02; level 75% (raw 18750 = 0x493E); capacity 200 L (raw 2000 = 0x07D0).
    const uint8_t b[8] = {0x02, 0x3E, 0x49, 0xD0, 0x07, 0x00, 0x00, 0xFF};
    N2kFluidLevel d;
    TEST_ASSERT_TRUE(pc_n2k_decode_fluid_level(b, sizeof(b), &d));
    TEST_ASSERT_EQUAL_UINT8(2, d.instance);
    TEST_ASSERT_EQUAL_UINT8(PC_N2K_FLUID_FUEL, d.fluid_type);
    TEST_ASSERT_TRUE(d.level_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.0f, d.level_pct);
    TEST_ASSERT_TRUE(d.capacity_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 200.0f, d.capacity_l);

    // The fluid type rides the high nibble: instance 3, water (type 1) -> byte0 0x13.
    const uint8_t w[8] = {0x13, 0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // level 0x7FFF n/a, capacity 0xFFFFFFFF n/a
    TEST_ASSERT_TRUE(pc_n2k_decode_fluid_level(w, sizeof(w), &d));
    TEST_ASSERT_EQUAL_UINT8(3, d.instance);
    TEST_ASSERT_EQUAL_UINT8(PC_N2K_FLUID_WATER, d.fluid_type);
    TEST_ASSERT_FALSE(d.level_valid);
    TEST_ASSERT_FALSE(d.capacity_valid);

    // Short payload + nulls are rejected.
    TEST_ASSERT_FALSE(pc_n2k_decode_fluid_level(b, 6, &d));
    TEST_ASSERT_FALSE(pc_n2k_decode_fluid_level(NULL, 8, &d));
}

void test_decode_actual_pressure()
{
    // sid 7, instance 0, atmospheric source, 101325 Pa (raw 1013250 = 0x000F7602 at 0.1 Pa/bit).
    const uint8_t p[8] = {0x07, 0x00, PC_N2K_PRESSURE_ATMOSPHERIC, 0x02, 0x76, 0x0F, 0x00, 0xFF};
    N2kActualPressure d;
    TEST_ASSERT_TRUE(pc_n2k_decode_actual_pressure(p, sizeof(p), &d));
    TEST_ASSERT_EQUAL_UINT8(7, d.sid);
    TEST_ASSERT_EQUAL_UINT8(0, d.instance);
    TEST_ASSERT_EQUAL_UINT8(PC_N2K_PRESSURE_ATMOSPHERIC, d.source);
    TEST_ASSERT_TRUE(d.pressure_valid);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 101325.0f, d.pressure_pa);

    // instance 1, oil source, 250000 Pa (raw 2500000 = 0x002625A0).
    const uint8_t o[8] = {0x03, 0x01, PC_N2K_PRESSURE_OIL, 0xA0, 0x25, 0x26, 0x00, 0xFF};
    TEST_ASSERT_TRUE(pc_n2k_decode_actual_pressure(o, sizeof(o), &d));
    TEST_ASSERT_EQUAL_UINT8(1, d.instance);
    TEST_ASSERT_EQUAL_UINT8(PC_N2K_PRESSURE_OIL, d.source);
    TEST_ASSERT_TRUE(d.pressure_valid);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 250000.0f, d.pressure_pa);

    // A 0x7FFFFFFF pressure is not-available; the source still decodes.
    const uint8_t na[8] = {0x07, 0x00, PC_N2K_PRESSURE_WATER, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF};
    TEST_ASSERT_TRUE(pc_n2k_decode_actual_pressure(na, sizeof(na), &d));
    TEST_ASSERT_EQUAL_UINT8(PC_N2K_PRESSURE_WATER, d.source);
    TEST_ASSERT_FALSE(d.pressure_valid);

    // The trailing reserved octet is optional (7 suffice); a shorter payload + nulls are rejected.
    TEST_ASSERT_TRUE(pc_n2k_decode_actual_pressure(p, 7, &d));
    TEST_ASSERT_FALSE(pc_n2k_decode_actual_pressure(p, 6, &d));
    TEST_ASSERT_FALSE(pc_n2k_decode_actual_pressure(NULL, 8, &d));
}

void test_decode_attitude()
{
    // sid 5, yaw 0.5236 rad (raw 5236), pitch 0.1 rad (raw 1000), roll -0.2 rad (raw -2000).
    const uint8_t a[8] = {0x05, 0x74, 0x14, 0xe8, 0x03, 0x30, 0xf8, 0xff};
    N2kAttitude at;
    TEST_ASSERT_TRUE(pc_n2k_decode_attitude(a, sizeof(a), &at));
    TEST_ASSERT_EQUAL_UINT8(5, at.sid);
    TEST_ASSERT_TRUE(at.yaw_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.5236f, at.yaw_rad);
    TEST_ASSERT_TRUE(at.pitch_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.1f, at.pitch_rad);
    TEST_ASSERT_TRUE(at.roll_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, -0.2f, at.roll_rad); // signed roll decodes negative

    // A 0x7FFF yaw is not-available; pitch and roll stay valid.
    const uint8_t na[8] = {0x05, 0xff, 0x7f, 0xe8, 0x03, 0x30, 0xf8, 0xff};
    TEST_ASSERT_TRUE(pc_n2k_decode_attitude(na, sizeof(na), &at));
    TEST_ASSERT_FALSE(at.yaw_valid);
    TEST_ASSERT_TRUE(at.pitch_valid);
    TEST_ASSERT_TRUE(at.roll_valid);
    // Short payload + nulls are rejected.
    TEST_ASSERT_FALSE(pc_n2k_decode_attitude(a, 6, &at));
    TEST_ASSERT_FALSE(pc_n2k_decode_attitude(NULL, 8, &at));
}

void test_decode_rudder()
{
    // instance 0, move-to-starboard order, angle order 0.1745 rad (raw 1745), position 0.1571 rad (raw 1571).
    const uint8_t r[8] = {0x00, 0x01, 0xd1, 0x06, 0x23, 0x06, 0xff, 0xff};
    N2kRudder d;
    TEST_ASSERT_TRUE(pc_n2k_decode_rudder(r, sizeof(r), &d));
    TEST_ASSERT_EQUAL_UINT8(0, d.instance);
    TEST_ASSERT_EQUAL_UINT8(N2K_RUDDER_MOVE_TO_STARBOARD, d.direction_order);
    TEST_ASSERT_TRUE(d.angle_order_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.1745f, d.angle_order_rad);
    TEST_ASSERT_TRUE(d.position_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.1571f, d.position_rad);

    // A negative position (port), a 0x7FFF not-available angle order, and the no-order direction.
    const uint8_t na[8] = {0x00, 0x00, 0xff, 0x7f, 0x30, 0xf8, 0xff, 0xff};
    TEST_ASSERT_TRUE(pc_n2k_decode_rudder(na, sizeof(na), &d));
    TEST_ASSERT_EQUAL_UINT8(N2K_RUDDER_NO_ORDER, d.direction_order);
    TEST_ASSERT_FALSE(d.angle_order_valid);
    TEST_ASSERT_TRUE(d.position_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, -0.2f, d.position_rad); // raw -2000 = 0xF830
    // Short payload + nulls are rejected.
    TEST_ASSERT_FALSE(pc_n2k_decode_rudder(r, 5, &d));
    TEST_ASSERT_FALSE(pc_n2k_decode_rudder(NULL, 8, &d));
}

void test_decode_engine_dynamic()
{
    // A reassembled 26-octet engine record: oil 3.0 bar (raw 3000), oil 90.05 C (raw 3632), coolant 90.0 C
    // (raw 36315), alternator 14.0 V (raw 1400), fuel 25.0 L/h (raw 250), 100 h (raw 360000 s), coolant 1.0
    // bar (raw 1000), fuel 3.0 bar (raw 300), discrete 1 = 0x0001, load 75 %, torque -10 %.
    const uint8_t e[26] = {0x00, 0xB8, 0x0B, 0x30, 0x0E, 0xDB, 0x8D, 0x78, 0x05, 0xFA, 0x00, 0x40, 0x7E,
                           0x05, 0x00, 0xE8, 0x03, 0x2C, 0x01, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x4B, 0xF6};
    N2kEngineDynamic d;
    TEST_ASSERT_TRUE(pc_n2k_decode_engine_dynamic(e, sizeof(e), &d));
    TEST_ASSERT_EQUAL_UINT8(0, d.instance);
    TEST_ASSERT_TRUE(d.oil_pressure_valid);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 300000.0f, d.oil_pressure_pa);
    TEST_ASSERT_TRUE(d.oil_temp_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.06f, 90.05f, d.oil_temp_c);
    TEST_ASSERT_TRUE(d.coolant_temp_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 90.0f, d.coolant_temp_c);
    TEST_ASSERT_TRUE(d.alt_voltage_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.0f, d.alt_voltage_v);
    TEST_ASSERT_TRUE(d.fuel_rate_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 25.0f, d.fuel_rate_lph);
    TEST_ASSERT_TRUE(d.engine_hours_valid);
    TEST_ASSERT_EQUAL_UINT32(360000u, d.engine_hours_s); // 100 hours
    TEST_ASSERT_TRUE(d.coolant_pressure_valid);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 100000.0f, d.coolant_pressure_pa);
    TEST_ASSERT_TRUE(d.fuel_pressure_valid);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 300000.0f, d.fuel_pressure_pa);
    TEST_ASSERT_EQUAL_HEX16(0x0001, d.discrete_status_1);
    TEST_ASSERT_EQUAL_HEX16(0x0000, d.discrete_status_2);
    TEST_ASSERT_TRUE(d.load_valid);
    TEST_ASSERT_EQUAL_INT8(75, d.load_pct);
    TEST_ASSERT_TRUE(d.torque_valid);
    TEST_ASSERT_EQUAL_INT8(-10, d.torque_pct); // signed

    // A not-available oil pressure clears just that flag; a 0x7F torque is not-available.
    uint8_t na[26];
    memcpy(na, e, sizeof(na));
    na[1] = 0xFF;
    na[2] = 0xFF;  // oil pressure not-available
    na[25] = 0x7F; // torque not-available
    TEST_ASSERT_TRUE(pc_n2k_decode_engine_dynamic(na, sizeof(na), &d));
    TEST_ASSERT_FALSE(d.oil_pressure_valid);
    TEST_ASSERT_TRUE(d.oil_temp_valid);
    TEST_ASSERT_FALSE(d.torque_valid);
    // Short payload + nulls are rejected (this is a Fast Packet PGN; the body must be complete).
    TEST_ASSERT_FALSE(pc_n2k_decode_engine_dynamic(e, 25, &d));
    TEST_ASSERT_FALSE(pc_n2k_decode_engine_dynamic(NULL, 26, &d));
}

void test_decode_wind_data()
{
    // sid 0x2A, speed 5.00 m/s (raw 500), angle 1.5708 rad (raw 15708), reference apparent.
    const uint8_t wind[8] = {0x2a, 0xf4, 0x01, 0x5c, 0x3d, 0x02, 0xff, 0xff};
    N2kWindData w;
    TEST_ASSERT_TRUE(pc_n2k_decode_wind_data(wind, sizeof(wind), &w));
    TEST_ASSERT_EQUAL_UINT8(0x2A, w.sid);
    TEST_ASSERT_TRUE(w.speed_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, w.speed_mps);
    TEST_ASSERT_TRUE(w.angle_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 1.5708f, w.angle_rad);
    TEST_ASSERT_EQUAL_UINT8(N2K_WIND_REF_APPARENT, w.reference);

    // A 0xFFFF speed is not-available; the angle stays valid.
    const uint8_t na[8] = {0x2a, 0xff, 0xff, 0x5c, 0x3d, 0x00, 0xff, 0xff};
    TEST_ASSERT_TRUE(pc_n2k_decode_wind_data(na, sizeof(na), &w));
    TEST_ASSERT_FALSE(w.speed_valid);
    TEST_ASSERT_TRUE(w.angle_valid);
    TEST_ASSERT_EQUAL_UINT8(N2K_WIND_REF_TRUE_NORTH, w.reference);
    TEST_ASSERT_FALSE(pc_n2k_decode_wind_data(wind, 5, &w)); // too short
}

void test_decode_speed()
{
    // sid 5, water speed 5.14 m/s (raw 514), ground speed 5.50 m/s (raw 550), paddle-wheel sensor.
    const uint8_t s[8] = {0x05, 0x02, 0x02, 0x26, 0x02, 0x00, 0xff, 0xff};
    N2kSpeed d;
    TEST_ASSERT_TRUE(pc_n2k_decode_speed(s, sizeof(s), &d));
    TEST_ASSERT_EQUAL_UINT8(5, d.sid);
    TEST_ASSERT_TRUE(d.water_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.14f, d.water_mps);
    TEST_ASSERT_TRUE(d.ground_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.50f, d.ground_mps);
    TEST_ASSERT_EQUAL_UINT8(N2K_SPEED_TYPE_PADDLE_WHEEL, d.water_ref_type);

    // A 0xFFFF ground speed is not-available; the water speed stays valid; a Doppler sensor.
    const uint8_t na[8] = {0x05, 0x02, 0x02, 0xff, 0xff, 0x02, 0xff, 0xff};
    TEST_ASSERT_TRUE(pc_n2k_decode_speed(na, sizeof(na), &d));
    TEST_ASSERT_TRUE(d.water_valid);
    TEST_ASSERT_FALSE(d.ground_valid);
    TEST_ASSERT_EQUAL_UINT8(N2K_SPEED_TYPE_DOPPLER, d.water_ref_type);
    // Short payload + nulls are rejected.
    TEST_ASSERT_FALSE(pc_n2k_decode_speed(s, 5, &d));
    TEST_ASSERT_FALSE(pc_n2k_decode_speed(NULL, 8, &d));
}

void test_decode_water_depth()
{
    // SID 1, depth 12.34 m (raw 1234), transducer offset 0.5 m (raw 500).
    const uint8_t wd[8] = {0x01, 0xd2, 0x04, 0x00, 0x00, 0xf4, 0x01, 0x00};
    N2kWaterDepth d;
    TEST_ASSERT_TRUE(pc_n2k_decode_water_depth(wd, sizeof(wd), &d));
    TEST_ASSERT_EQUAL_UINT8(1, d.sid);
    TEST_ASSERT_TRUE(d.depth_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.34f, d.depth_m);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, d.offset_m);

    // A 0xFFFFFFFF depth is not-available; short / null payloads are rejected.
    const uint8_t na[8] = {0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(pc_n2k_decode_water_depth(na, sizeof(na), &d));
    TEST_ASSERT_FALSE(d.depth_valid);
    TEST_ASSERT_FALSE(pc_n2k_decode_water_depth(wd, 6, &d));
    TEST_ASSERT_FALSE(pc_n2k_decode_water_depth(NULL, 8, &d));
}

void test_decode_vessel_heading()
{
    // SID 2, heading 1.5708 rad (90 deg, raw 15708), deviation 0, variation -0.1 rad, reference magnetic.
    const uint8_t vh[8] = {0x02, 0x5c, 0x3d, 0x00, 0x00, 0x18, 0xfc, 0x01};
    N2kVesselHeading h;
    TEST_ASSERT_TRUE(pc_n2k_decode_vessel_heading(vh, sizeof(vh), &h));
    TEST_ASSERT_EQUAL_UINT8(2, h.sid);
    TEST_ASSERT_TRUE(h.heading_valid);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 1.5708f, h.heading_rad);
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, -0.1f, h.variation_rad);
    TEST_ASSERT_EQUAL_UINT8(N2K_HEADING_REF_MAGNETIC, h.reference);

    // A 0xFFFF heading is not-available; a short payload is rejected.
    const uint8_t na[8] = {0x02, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(pc_n2k_decode_vessel_heading(na, sizeof(na), &h));
    TEST_ASSERT_FALSE(h.heading_valid);
    TEST_ASSERT_FALSE(pc_n2k_decode_vessel_heading(vh, 7, &h));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_num_frames);
    RUN_TEST(test_single_frame);
    RUN_TEST(test_fastpacket_roundtrip);
    RUN_TEST(test_fastpacket_single_frame_completes);
    RUN_TEST(test_fastpacket_interleaved_sequence_ignored);
    RUN_TEST(test_fastpacket_out_of_order_errors);
    RUN_TEST(test_nmea2000_error_paths);
    RUN_TEST(test_fastpacket_build_frame_total_too_large);
    RUN_TEST(test_fastpacket_reset_null_is_safe);
    RUN_TEST(test_fastpacket_feed_total_too_large_errors);
    RUN_TEST(test_fastpacket_continuation_without_active_sequence_ignored);
    RUN_TEST(test_fastpacket_continuation_wrong_source_ignored);
    RUN_TEST(test_fastpacket_continuation_wrong_pgn_ignored);
    RUN_TEST(test_fastpacket_roundtrip_short_last_frame);
    RUN_TEST(test_decode_position_rapid);
    RUN_TEST(test_decode_cog_sog_rapid);
    RUN_TEST(test_decode_engine_rapid);
    RUN_TEST(test_decode_engine_dynamic);
    RUN_TEST(test_decode_temperature);
    RUN_TEST(test_decode_battery_status);
    RUN_TEST(test_decode_fluid_level);
    RUN_TEST(test_decode_actual_pressure);
    RUN_TEST(test_decode_attitude);
    RUN_TEST(test_decode_rudder);
    RUN_TEST(test_decode_wind_data);
    RUN_TEST(test_decode_speed);
    RUN_TEST(test_decode_water_depth);
    RUN_TEST(test_decode_vessel_heading);
    return UNITY_END();
}
