// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the NMEA 2000 codec (services/timing_position/nmea2000/nmea2000.h).
//
// NMEA 2000 is a paid document, so the expectations here are PROPERTIES plus arithmetic derived from
// the transport and scaling rules nmea2000.h itself states: the Fast Packet control octet carries the
// sequence counter in bits 7..5 and the frame counter in bits 4..0, the first frame gives up two of
// its eight octets to that control byte and the total length, and continuations carry seven each.
// test_fastpacket_split_and_reassemble is the load-bearing case: a message that does not come back
// byte for byte across that split is a corrupted instrument reading, and the frame arithmetic
// (1 + ceil((len - 6) / 7)) is the only thing standing between the two. The transport rides the
// SAE J1939-21 29-bit identifier, whose PGN and source address must survive every frame.

#include "services/timing_position/nmea2000/nmea2000.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static float absf(float v)
{
    return v < 0.0f ? -v : v;
}

static void near_f(float want, float got, float tol)
{
    TEST_ASSERT_TRUE(absf(want - got) <= tol);
}

static double dabs(double v)
{
    return v < 0 ? -v : v;
}

// The first frame carries six payload octets after the control and length bytes; each continuation
// carries seven. So a message of n octets needs 1 + ceil((n - 6) / 7) frames once n exceeds 6.
void test_fastpacket_frame_count(void)
{
    for (uint16_t n = 1; n <= 6; n++)
    {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, protocore_n2k_fastpacket_num_frames(n), "up to six fits one frame");
    }
    TEST_ASSERT_EQUAL_UINT8(2, protocore_n2k_fastpacket_num_frames(7));
    TEST_ASSERT_EQUAL_UINT8(2, protocore_n2k_fastpacket_num_frames(13)); // 6 + 7
    TEST_ASSERT_EQUAL_UINT8(3, protocore_n2k_fastpacket_num_frames(14)); // one octet past two frames
    TEST_ASSERT_EQUAL_UINT8(3, protocore_n2k_fastpacket_num_frames(20)); // 6 + 7 + 7
    TEST_ASSERT_EQUAL_UINT8(4, protocore_n2k_fastpacket_num_frames(21));
    // The transport tops out at 223 octets: 6 + 31 * 7 = 223, so exactly 32 frames.
    TEST_ASSERT_EQUAL_UINT8(32, protocore_n2k_fastpacket_num_frames(223));

    for (uint16_t n = 7; n <= 223; n++)
    {
        const uint8_t want = (uint8_t)(1u + ((n - N2K_FP_F0_DATA) + N2K_FP_FN_DATA - 1u) / N2K_FP_FN_DATA);
        TEST_ASSERT_EQUAL_UINT8(want, protocore_n2k_fastpacket_num_frames(n));
    }
}

// The load-bearing case: split a message across Fast Packet frames and feed them back. Every frame
// must carry the sequence counter in the control octet's high three bits and its own index in the
// low five, the first must announce the total length, and the reassembled body must be identical.
void test_fastpacket_split_and_reassemble(void)
{
    static const uint16_t LENGTHS[6] = {7, 8, 13, 14, 100, 223};
    for (unsigned c = 0; c < 6; c++)
    {
        const uint16_t total = LENGTHS[c];
        uint8_t body[223];
        for (uint16_t i = 0; i < total; i++)
        {
            body[i] = (uint8_t)(i * 3u + c);
        }
        const uint8_t seq = (uint8_t)(c % 8u);
        const uint8_t frames = protocore_n2k_fastpacket_num_frames(total);

        N2kFastPacketRx rx;
        protocore_n2k_fastpacket_reset(&rx);
        N2kFpResult last = N2K_FP_IGNORED;

        for (uint8_t i = 0; i < frames; i++)
        {
            CanFrame f;
            memset(&f, 0, sizeof(f));
            TEST_ASSERT_TRUE(
                protocore_n2k_fastpacket_build_frame(&f, seq, i, 3, N2K_PGN_ENGINE_DYNAMIC, 0x11, 0xFF, body, total));
            TEST_ASSERT_TRUE(f.extended); // a 29-bit J1939 identifier
            TEST_ASSERT_EQUAL_UINT8(seq, (uint8_t)(f.data[0] >> N2K_FP_SEQ_SHIFT));
            TEST_ASSERT_EQUAL_UINT8(i, (uint8_t)(f.data[0] & N2K_FP_FRAME_MASK));
            if (i == 0)
            {
                TEST_ASSERT_EQUAL_UINT8((uint8_t)total, f.data[1]);
            }

            last = protocore_n2k_fastpacket_feed(&rx, &f);
            if (i == 0)
            {
                TEST_ASSERT_EQUAL_INT(N2K_FP_STARTED, (int)last);
            }
            else if (i + 1 < frames)
            {
                TEST_ASSERT_EQUAL_INT(N2K_FP_PROGRESS, (int)last);
            }
        }
        TEST_ASSERT_EQUAL_INT(N2K_FP_COMPLETE, (int)last);
        TEST_ASSERT_EQUAL_UINT16(total, rx.total_len);
        TEST_ASSERT_EQUAL_UINT16(total, rx.received);
        TEST_ASSERT_EQUAL_UINT8(seq, rx.seq);
        TEST_ASSERT_EQUAL_UINT8(0x11, rx.sa);
        TEST_ASSERT_EQUAL_UINT32(N2K_PGN_ENGINE_DYNAMIC, rx.pgn);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(body, rx.buf, total);
    }
}

// A frame arriving out of order, or one belonging to a different sequence, must not be folded into
// the message under way.
void test_fastpacket_rejects_out_of_order_and_foreign_frames(void)
{
    uint8_t body[20];
    for (unsigned i = 0; i < sizeof(body); i++)
    {
        body[i] = (uint8_t)i;
    }
    CanFrame f0;
    CanFrame f1;
    CanFrame f2;
    memset(&f0, 0, sizeof(f0));
    memset(&f1, 0, sizeof(f1));
    memset(&f2, 0, sizeof(f2));
    TEST_ASSERT_TRUE(protocore_n2k_fastpacket_build_frame(&f0, 2, 0, 3, N2K_PGN_ENGINE_DYNAMIC, 5, 0xFF, body, 20));
    TEST_ASSERT_TRUE(protocore_n2k_fastpacket_build_frame(&f1, 2, 1, 3, N2K_PGN_ENGINE_DYNAMIC, 5, 0xFF, body, 20));
    TEST_ASSERT_TRUE(protocore_n2k_fastpacket_build_frame(&f2, 2, 2, 3, N2K_PGN_ENGINE_DYNAMIC, 5, 0xFF, body, 20));

    // Frame 2 before frame 1 is out of order.
    N2kFastPacketRx rx;
    protocore_n2k_fastpacket_reset(&rx);
    TEST_ASSERT_EQUAL_INT(N2K_FP_STARTED, (int)protocore_n2k_fastpacket_feed(&rx, &f0));
    TEST_ASSERT_EQUAL_INT(N2K_FP_ERR, (int)protocore_n2k_fastpacket_feed(&rx, &f2));

    // A continuation with a different sequence counter belongs to another message.
    CanFrame other;
    memset(&other, 0, sizeof(other));
    TEST_ASSERT_TRUE(protocore_n2k_fastpacket_build_frame(&other, 5, 1, 3, N2K_PGN_ENGINE_DYNAMIC, 5, 0xFF, body, 20));
    protocore_n2k_fastpacket_reset(&rx);
    TEST_ASSERT_EQUAL_INT(N2K_FP_STARTED, (int)protocore_n2k_fastpacket_feed(&rx, &f0));
    TEST_ASSERT_NOT_EQUAL_INT(N2K_FP_PROGRESS, (int)protocore_n2k_fastpacket_feed(&rx, &other));

    // A continuation with no first frame ahead of it starts nothing.
    protocore_n2k_fastpacket_reset(&rx);
    TEST_ASSERT_NOT_EQUAL_INT(N2K_FP_PROGRESS, (int)protocore_n2k_fastpacket_feed(&rx, &f1));
    TEST_ASSERT_FALSE(rx.active);

    // A fresh first frame restarts the sequence rather than continuing the abandoned one.
    protocore_n2k_fastpacket_reset(&rx);
    TEST_ASSERT_EQUAL_INT(N2K_FP_STARTED, (int)protocore_n2k_fastpacket_feed(&rx, &f0));
    TEST_ASSERT_EQUAL_INT(N2K_FP_PROGRESS, (int)protocore_n2k_fastpacket_feed(&rx, &f1));
    TEST_ASSERT_EQUAL_INT(N2K_FP_STARTED, (int)protocore_n2k_fastpacket_feed(&rx, &f0));
    TEST_ASSERT_EQUAL_UINT16(N2K_FP_F0_DATA, rx.received);
}

// A message longer than the reassembly buffer, and a frame index past the message's own frame count,
// are both refused rather than written past the end.
void test_fastpacket_bounds(void)
{
    uint8_t body[8] = {0};
    CanFrame f;
    memset(&f, 0, sizeof(f));
    TEST_ASSERT_FALSE(protocore_n2k_fastpacket_build_frame(&f, 0, 2, 3, N2K_PGN_ENGINE_DYNAMIC, 1, 0xFF, body, 8));
    TEST_ASSERT_FALSE(protocore_n2k_fastpacket_build_frame(NULL, 0, 0, 3, N2K_PGN_ENGINE_DYNAMIC, 1, 0xFF, body, 8));
    TEST_ASSERT_FALSE(protocore_n2k_fastpacket_build_frame(&f, 0, 0, 3, N2K_PGN_ENGINE_DYNAMIC, 1, 0xFF, NULL, 8));

    uint8_t big[PROTOCORE_N2K_FP_MAX + 8];
    memset(big, 0, sizeof(big));
    TEST_ASSERT_FALSE(
        protocore_n2k_fastpacket_build_frame(&f, 0, 0, 3, N2K_PGN_ENGINE_DYNAMIC, 1, 0xFF, big, (uint16_t)sizeof(big)));

    N2kFastPacketRx rx;
    protocore_n2k_fastpacket_reset(&rx);
    TEST_ASSERT_FALSE(rx.active);
    TEST_ASSERT_EQUAL_INT(N2K_FP_IGNORED, (int)protocore_n2k_fastpacket_feed(&rx, NULL));
}

// A single-frame message is a plain J1939 frame: the identifier carries the PGN, the priority and
// the source address, and the payload is the frame's data.
void test_single_frame_message(void)
{
    static const uint8_t DATA[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    CanFrame f;
    memset(&f, 0, sizeof(f));
    TEST_ASSERT_TRUE(protocore_n2k_build_single(&f, 2, N2K_PGN_POSITION_RAPID, 0x23, 0xFF, DATA, 8));
    TEST_ASSERT_TRUE(f.extended);
    TEST_ASSERT_EQUAL_UINT8(8, f.dlc);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(DATA, f.data, 8);
    TEST_ASSERT_EQUAL_HEX8(0x23, (uint8_t)(f.id & 0xFFu));       // J1939 source address is the low octet
    TEST_ASSERT_EQUAL_UINT8(2, (uint8_t)((f.id >> 26) & 0x07u)); // priority is the top three bits

    // More than eight octets is not a single frame.
    static const uint8_t TOO_LONG[9] = {0};
    TEST_ASSERT_FALSE(protocore_n2k_build_single(&f, 2, N2K_PGN_POSITION_RAPID, 0x23, 0xFF, TOO_LONG, 9));
    TEST_ASSERT_FALSE(protocore_n2k_build_single(NULL, 2, N2K_PGN_POSITION_RAPID, 0x23, 0xFF, DATA, 8));
}

// Position Rapid Update (129025): two 32-bit signed little-endian coordinates at 1e-7 degrees per
// bit, with 0x7FFFFFFF meaning "not available". 44.0690060 deg is 440690060 raw.
void test_position_rapid_update(void)
{
    uint8_t p[8];
    const int32_t lat_raw = 440690060;   // 44.0690060 / 1e-7
    const int32_t lon_raw = -1213143268; // -121.3143268 / 1e-7
    p[0] = (uint8_t)(lat_raw & 0xFF);
    p[1] = (uint8_t)((lat_raw >> 8) & 0xFF);
    p[2] = (uint8_t)((lat_raw >> 16) & 0xFF);
    p[3] = (uint8_t)((lat_raw >> 24) & 0xFF);
    p[4] = (uint8_t)(lon_raw & 0xFF);
    p[5] = (uint8_t)((lon_raw >> 8) & 0xFF);
    p[6] = (uint8_t)((lon_raw >> 16) & 0xFF);
    p[7] = (uint8_t)((lon_raw >> 24) & 0xFF);

    N2kPositionRapid pos;
    TEST_ASSERT_TRUE(protocore_n2k_decode_position_rapid(p, sizeof(p), &pos));
    TEST_ASSERT_TRUE(pos.valid);
    TEST_ASSERT_TRUE(dabs(pos.lat_deg - 44.0690060) < 1e-7);
    TEST_ASSERT_TRUE(dabs(pos.lon_deg - (-121.3143268)) < 1e-7);

    // The all-ones signed raw is the not-available marker.
    p[0] = 0xFF;
    p[1] = 0xFF;
    p[2] = 0xFF;
    p[3] = 0x7F;
    TEST_ASSERT_TRUE(protocore_n2k_decode_position_rapid(p, sizeof(p), &pos));
    TEST_ASSERT_FALSE(pos.valid);

    // Fewer than eight octets is not a position.
    TEST_ASSERT_FALSE(protocore_n2k_decode_position_rapid(p, 7, &pos));
}

// COG & SOG Rapid Update (129026): SID, a 2-bit course reference, course at 0.0001 rad/bit and speed
// at 0.01 m/s per bit, both unsigned 16-bit little-endian with 0xFFFF not available.
void test_cog_sog_rapid_update(void)
{
    uint8_t p[8];
    memset(p, 0, sizeof(p));
    p[0] = 0x2A;                    // SID
    p[1] = N2K_COG_REF_TRUE;        // reference in the low two bits
    p[2] = (uint8_t)(31416 & 0xFF); // 3.1416 rad
    p[3] = (uint8_t)(31416 >> 8);
    p[4] = (uint8_t)(550 & 0xFF); // 5.50 m/s
    p[5] = (uint8_t)(550 >> 8);

    N2kCogSogRapid c;
    TEST_ASSERT_TRUE(protocore_n2k_decode_cog_sog_rapid(p, sizeof(p), &c));
    TEST_ASSERT_EQUAL_UINT8(0x2A, c.sid);
    TEST_ASSERT_EQUAL_UINT8(N2K_COG_REF_TRUE, c.cog_ref);
    TEST_ASSERT_TRUE(c.cog_valid);
    near_f(3.1416f, c.cog_rad, 1e-4f);
    TEST_ASSERT_TRUE(c.sog_valid);
    near_f(5.50f, c.sog_mps, 1e-4f);

    p[2] = 0xFF;
    p[3] = 0xFF;
    p[4] = 0xFF;
    p[5] = 0xFF;
    TEST_ASSERT_TRUE(protocore_n2k_decode_cog_sog_rapid(p, sizeof(p), &c));
    TEST_ASSERT_FALSE(c.cog_valid);
    TEST_ASSERT_FALSE(c.sog_valid);
    TEST_ASSERT_FALSE(protocore_n2k_decode_cog_sog_rapid(p, 5, &c));
}

// Engine Parameters Rapid Update (127488): instance, speed at 0.25 rpm/bit, boost at 100 Pa/bit,
// and a signed tilt/trim percentage.
void test_engine_rapid_update(void)
{
    uint8_t p[8];
    memset(p, 0xFF, sizeof(p));
    p[0] = 1;                      // instance
    p[1] = (uint8_t)(7200 & 0xFF); // 1800 rpm at 0.25 rpm/bit
    p[2] = (uint8_t)(7200 >> 8);
    p[3] = (uint8_t)(1200 & 0xFF); // 120000 Pa at 100 Pa/bit
    p[4] = (uint8_t)(1200 >> 8);
    p[5] = (uint8_t)(int8_t)(-25); // tilt/trim

    N2kEngineRapid e;
    TEST_ASSERT_TRUE(protocore_n2k_decode_engine_rapid(p, sizeof(p), &e));
    TEST_ASSERT_EQUAL_UINT8(1, e.instance);
    TEST_ASSERT_TRUE(e.speed_valid);
    near_f(1800.0f, e.speed_rpm, 1e-3f);
    TEST_ASSERT_TRUE(e.boost_valid);
    near_f(120000.0f, e.boost_pa, 1.0f);
    TEST_ASSERT_TRUE(e.tilt_valid);
    TEST_ASSERT_EQUAL_INT8(-25, e.tilt_pct);

    p[1] = 0xFF;
    p[2] = 0xFF;
    TEST_ASSERT_TRUE(protocore_n2k_decode_engine_rapid(p, sizeof(p), &e));
    TEST_ASSERT_FALSE(e.speed_valid);
    TEST_ASSERT_FALSE(protocore_n2k_decode_engine_rapid(p, 5, &e));
}

// Wind Data (130306): SID, speed at 0.01 m/s per bit, angle at 0.0001 rad/bit, and the wind
// reference in the low three bits of the fifth octet.
void test_wind_data(void)
{
    uint8_t p[8];
    memset(p, 0xFF, sizeof(p));
    p[0] = 7;
    p[1] = (uint8_t)(1234 & 0xFF); // 12.34 m/s
    p[2] = (uint8_t)(1234 >> 8);
    p[3] = (uint8_t)(15708 & 0xFF); // 1.5708 rad
    p[4] = (uint8_t)(15708 >> 8);
    p[5] = N2K_WIND_REF_APPARENT;

    N2kWindData w;
    TEST_ASSERT_TRUE(protocore_n2k_decode_wind_data(p, sizeof(p), &w));
    TEST_ASSERT_EQUAL_UINT8(7, w.sid);
    TEST_ASSERT_TRUE(w.speed_valid);
    near_f(12.34f, w.speed_mps, 1e-4f);
    TEST_ASSERT_TRUE(w.angle_valid);
    near_f(1.5708f, w.angle_rad, 1e-4f);
    TEST_ASSERT_EQUAL_UINT8(N2K_WIND_REF_APPARENT, w.reference);

    p[1] = 0xFF;
    p[2] = 0xFF;
    TEST_ASSERT_TRUE(protocore_n2k_decode_wind_data(p, sizeof(p), &w));
    TEST_ASSERT_FALSE(w.speed_valid);
    TEST_ASSERT_FALSE(protocore_n2k_decode_wind_data(p, 5, &w));
}

// Water Depth (128267): depth at 0.01 m/bit unsigned 32-bit, then a signed 16-bit transducer offset
// at 0.001 m/bit.
void test_water_depth(void)
{
    uint8_t p[8];
    memset(p, 0xFF, sizeof(p));
    p[0] = 3;
    const uint32_t depth_raw = 1234; // 12.34 m
    p[1] = (uint8_t)(depth_raw & 0xFF);
    p[2] = (uint8_t)((depth_raw >> 8) & 0xFF);
    p[3] = (uint8_t)((depth_raw >> 16) & 0xFF);
    p[4] = (uint8_t)((depth_raw >> 24) & 0xFF);
    const int16_t offset_raw = -500;
    p[5] = (uint8_t)((uint16_t)offset_raw & 0xFF);
    p[6] = (uint8_t)(((uint16_t)offset_raw >> 8) & 0xFF);

    N2kWaterDepth d;
    TEST_ASSERT_TRUE(protocore_n2k_decode_water_depth(p, sizeof(p), &d));
    TEST_ASSERT_EQUAL_UINT8(3, d.sid);
    TEST_ASSERT_TRUE(d.depth_valid);
    near_f(12.34f, d.depth_m, 1e-3f);
    TEST_ASSERT_TRUE(absf(d.offset_m) > 0.0f); // a negative offset means "to the keel"
    TEST_ASSERT_TRUE(d.offset_m < 0.0f);

    p[1] = 0xFF;
    p[2] = 0xFF;
    p[3] = 0xFF;
    p[4] = 0xFF;
    TEST_ASSERT_TRUE(protocore_n2k_decode_water_depth(p, sizeof(p), &d));
    TEST_ASSERT_FALSE(d.depth_valid);
    TEST_ASSERT_FALSE(protocore_n2k_decode_water_depth(p, 6, &d));
}

// Vessel Heading (127250): SID, heading / deviation / variation each 16-bit at 0.0001 rad/bit, and
// the heading reference in the low two bits of the last octet.
void test_vessel_heading(void)
{
    uint8_t p[8];
    memset(p, 0xFF, sizeof(p));
    p[0] = 9;
    p[1] = (uint8_t)(17453 & 0xFF); // 1.7453 rad, about 100 degrees
    p[2] = (uint8_t)(17453 >> 8);
    p[3] = 0;
    p[4] = 0;
    p[5] = 0;
    p[6] = 0;
    p[7] = N2K_HEADING_REF_MAGNETIC;

    N2kVesselHeading h;
    TEST_ASSERT_TRUE(protocore_n2k_decode_vessel_heading(p, sizeof(p), &h));
    TEST_ASSERT_EQUAL_UINT8(9, h.sid);
    TEST_ASSERT_TRUE(h.heading_valid);
    near_f(1.7453f, h.heading_rad, 1e-4f);
    TEST_ASSERT_EQUAL_UINT8(N2K_HEADING_REF_MAGNETIC, h.reference);

    p[1] = 0xFF;
    p[2] = 0xFF;
    TEST_ASSERT_TRUE(protocore_n2k_decode_vessel_heading(p, sizeof(p), &h));
    TEST_ASSERT_FALSE(h.heading_valid);
    TEST_ASSERT_FALSE(protocore_n2k_decode_vessel_heading(p, 7, &h));
}

// Temperature (130312) carries Kelvin at 0.01 K/bit and is reported in Celsius, so 29315 raw is
// 293.15 K = 20.00 C - the 273.15 offset is the whole of the conversion.
void test_temperature_converts_kelvin_to_celsius(void)
{
    uint8_t p[8];
    memset(p, 0xFF, sizeof(p));
    p[0] = 1;
    p[1] = 2;
    p[2] = N2K_TEMP_SRC_SEA;
    p[3] = (uint8_t)(29315 & 0xFF);
    p[4] = (uint8_t)(29315 >> 8);
    p[5] = (uint8_t)(30315 & 0xFF); // 303.15 K = 30.00 C
    p[6] = (uint8_t)(30315 >> 8);

    N2kTemperature t;
    TEST_ASSERT_TRUE(protocore_n2k_decode_temperature(p, sizeof(p), &t));
    TEST_ASSERT_EQUAL_UINT8(1, t.sid);
    TEST_ASSERT_EQUAL_UINT8(2, t.instance);
    TEST_ASSERT_EQUAL_UINT8(N2K_TEMP_SRC_SEA, t.source);
    TEST_ASSERT_TRUE(t.actual_valid);
    near_f(20.0f, t.actual_c, 0.01f);
    TEST_ASSERT_TRUE(t.set_valid);
    near_f(30.0f, t.set_c, 0.01f);

    p[3] = 0xFF;
    p[4] = 0xFF;
    TEST_ASSERT_TRUE(protocore_n2k_decode_temperature(p, sizeof(p), &t));
    TEST_ASSERT_FALSE(t.actual_valid);
    TEST_ASSERT_FALSE(protocore_n2k_decode_temperature(p, 6, &t));
}

// Attitude (127257): three signed 16-bit angles at 0.0001 rad/bit, so a negative raw is a negative
// angle rather than a large positive one.
void test_attitude_angles_are_signed(void)
{
    uint8_t p[7];
    p[0] = 4;
    const int16_t yaw = 15708;   // +1.5708 rad
    const int16_t pitch = -1000; // -0.1000 rad
    const int16_t roll = 500;    // +0.0500 rad
    p[1] = (uint8_t)((uint16_t)yaw & 0xFF);
    p[2] = (uint8_t)(((uint16_t)yaw >> 8) & 0xFF);
    p[3] = (uint8_t)((uint16_t)pitch & 0xFF);
    p[4] = (uint8_t)(((uint16_t)pitch >> 8) & 0xFF);
    p[5] = (uint8_t)((uint16_t)roll & 0xFF);
    p[6] = (uint8_t)(((uint16_t)roll >> 8) & 0xFF);

    N2kAttitude a;
    TEST_ASSERT_TRUE(protocore_n2k_decode_attitude(p, sizeof(p), &a));
    TEST_ASSERT_EQUAL_UINT8(4, a.sid);
    TEST_ASSERT_TRUE(a.yaw_valid);
    near_f(1.5708f, a.yaw_rad, 1e-4f);
    TEST_ASSERT_TRUE(a.pitch_valid);
    near_f(-0.1000f, a.pitch_rad, 1e-4f);
    TEST_ASSERT_TRUE(a.roll_valid);
    near_f(0.0500f, a.roll_rad, 1e-4f);
    TEST_ASSERT_FALSE(protocore_n2k_decode_attitude(p, 6, &a));
}

// Battery Status (127508): instance, a signed voltage at 0.01 V/bit, a signed current at 0.1 A/bit,
// a Kelvin temperature at 0.01 K/bit, and the SID.
void test_battery_status(void)
{
    uint8_t p[8];
    p[0] = 0;
    const int16_t volt = 1264;     // 12.64 V
    const int16_t amp = -152;      // -15.2 A (discharging)
    const uint16_t kelvin = 29815; // 298.15 K = 25.00 C
    p[1] = (uint8_t)((uint16_t)volt & 0xFF);
    p[2] = (uint8_t)(((uint16_t)volt >> 8) & 0xFF);
    p[3] = (uint8_t)((uint16_t)amp & 0xFF);
    p[4] = (uint8_t)(((uint16_t)amp >> 8) & 0xFF);
    p[5] = (uint8_t)(kelvin & 0xFF);
    p[6] = (uint8_t)(kelvin >> 8);
    p[7] = 6;

    N2kBatteryStatus b;
    TEST_ASSERT_TRUE(protocore_n2k_decode_battery_status(p, sizeof(p), &b));
    TEST_ASSERT_EQUAL_UINT8(0, b.instance);
    TEST_ASSERT_TRUE(b.voltage_valid);
    near_f(12.64f, b.voltage_v, 1e-3f);
    TEST_ASSERT_TRUE(b.current_valid);
    near_f(-15.2f, b.current_a, 1e-3f);
    TEST_ASSERT_TRUE(b.temp_valid);
    near_f(25.0f, b.temp_c, 0.01f);
    TEST_ASSERT_EQUAL_UINT8(6, b.sid);
    TEST_ASSERT_FALSE(protocore_n2k_decode_battery_status(p, 7, &b));
}

// A Fast Packet PGN decoded from the reassembled body: Engine Parameters Dynamic (127489) needs the
// full 26-octet record, which is more than one CAN frame can carry.
void test_engine_dynamic_rides_the_fast_packet(void)
{
    uint8_t body[26];
    memset(body, 0xFF, sizeof(body));
    body[0] = 0;                      // instance
    body[1] = (uint8_t)(4500 & 0xFF); // oil pressure 450000 Pa at 100 Pa/bit
    body[2] = (uint8_t)(4500 >> 8);

    // 26 octets is 6 + 7 + 7 + 6, so four frames.
    TEST_ASSERT_EQUAL_UINT8(4, protocore_n2k_fastpacket_num_frames((uint16_t)sizeof(body)));

    N2kFastPacketRx rx;
    protocore_n2k_fastpacket_reset(&rx);
    N2kFpResult last = N2K_FP_IGNORED;
    for (uint8_t i = 0; i < 4; i++)
    {
        CanFrame f;
        memset(&f, 0, sizeof(f));
        TEST_ASSERT_TRUE(protocore_n2k_fastpacket_build_frame(&f, 1, i, 2, N2K_PGN_ENGINE_DYNAMIC, 0x0A, 0xFF, body,
                                                              (uint16_t)sizeof(body)));
        last = protocore_n2k_fastpacket_feed(&rx, &f);
    }
    TEST_ASSERT_EQUAL_INT(N2K_FP_COMPLETE, (int)last);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(body, rx.buf, sizeof(body));

    N2kEngineDynamic e;
    TEST_ASSERT_TRUE(protocore_n2k_decode_engine_dynamic(rx.buf, rx.total_len, &e));
    TEST_ASSERT_EQUAL_UINT8(0, e.instance);
    TEST_ASSERT_TRUE(e.oil_pressure_valid);
    near_f(450000.0f, e.oil_pressure_pa, 1.0f);
    // A record short of the full 26 octets is not decodable.
    TEST_ASSERT_FALSE(protocore_n2k_decode_engine_dynamic(rx.buf, 25, &e));
}

// Every decoder refuses a payload shorter than the fields it must read, so a truncated frame never
// yields a fabricated instrument reading.
void test_short_payloads_are_refused(void)
{
    uint8_t p[32];
    memset(p, 0, sizeof(p));
    N2kPositionRapid pos;
    N2kCogSogRapid cog;
    N2kEngineRapid eng;
    N2kWindData wind;
    N2kSpeed spd;
    N2kWaterDepth dep;
    N2kVesselHeading hdg;
    N2kRudder rud;
    N2kAttitude att;
    N2kTemperature tmp;
    N2kBatteryStatus bat;
    N2kFluidLevel fl;
    N2kActualPressure pr;

    TEST_ASSERT_FALSE(protocore_n2k_decode_position_rapid(p, 0, &pos));
    TEST_ASSERT_FALSE(protocore_n2k_decode_cog_sog_rapid(p, 0, &cog));
    TEST_ASSERT_FALSE(protocore_n2k_decode_engine_rapid(p, 0, &eng));
    TEST_ASSERT_FALSE(protocore_n2k_decode_wind_data(p, 0, &wind));
    TEST_ASSERT_FALSE(protocore_n2k_decode_speed(p, 0, &spd));
    TEST_ASSERT_FALSE(protocore_n2k_decode_water_depth(p, 0, &dep));
    TEST_ASSERT_FALSE(protocore_n2k_decode_vessel_heading(p, 0, &hdg));
    TEST_ASSERT_FALSE(protocore_n2k_decode_rudder(p, 0, &rud));
    TEST_ASSERT_FALSE(protocore_n2k_decode_attitude(p, 0, &att));
    TEST_ASSERT_FALSE(protocore_n2k_decode_temperature(p, 0, &tmp));
    TEST_ASSERT_FALSE(protocore_n2k_decode_battery_status(p, 0, &bat));
    TEST_ASSERT_FALSE(protocore_n2k_decode_fluid_level(p, 0, &fl));
    TEST_ASSERT_FALSE(protocore_n2k_decode_actual_pressure(p, 0, &pr));

    // At their documented minimum lengths they all succeed.
    memset(p, 0, sizeof(p));
    TEST_ASSERT_TRUE(protocore_n2k_decode_position_rapid(p, 8, &pos));
    TEST_ASSERT_TRUE(protocore_n2k_decode_cog_sog_rapid(p, 6, &cog));
    TEST_ASSERT_TRUE(protocore_n2k_decode_engine_rapid(p, 6, &eng));
    TEST_ASSERT_TRUE(protocore_n2k_decode_wind_data(p, 6, &wind));
    TEST_ASSERT_TRUE(protocore_n2k_decode_speed(p, 6, &spd));
    TEST_ASSERT_TRUE(protocore_n2k_decode_water_depth(p, 7, &dep));
    TEST_ASSERT_TRUE(protocore_n2k_decode_vessel_heading(p, 8, &hdg));
    TEST_ASSERT_TRUE(protocore_n2k_decode_rudder(p, 6, &rud));
    TEST_ASSERT_TRUE(protocore_n2k_decode_attitude(p, 7, &att));
    TEST_ASSERT_TRUE(protocore_n2k_decode_temperature(p, 7, &tmp));
    TEST_ASSERT_TRUE(protocore_n2k_decode_battery_status(p, 8, &bat));
    TEST_ASSERT_TRUE(protocore_n2k_decode_fluid_level(p, 7, &fl));
    TEST_ASSERT_TRUE(protocore_n2k_decode_actual_pressure(p, 7, &pr));
}
