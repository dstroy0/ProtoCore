// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the 802.11 sniffer core (services/radio/wifi_sniffer/wifi_sniffer.h).
//
// IEEE Std 802.11 sec 9.2.4.1 lays the Frame Control field out as Protocol Version (B0-B1), Type
// (B2-B3), Subtype (B4-B7), To DS (B8), From DS (B9), More Fragments (B10), Retry (B11), Power
// Management (B12), More Data (B13), Protected Frame (B14), +HTC (B15), transmitted least
// significant octet first. test_beacon_mac_header is the load-bearing case: a beacon's header is
// built from that layout and from Table 9-1's Type/Subtype assignment (Management = 0, Beacon = 8),
// so a decoder that shifts the wrong way or reads the flags out of the wrong octet cannot pass it.
// The schedule and survey cases below are properties (wrap safety, ordering, bounds refusal) rather
// than standard values, since no standard fixes a channel-hop dwell policy.

#include "services/radio/wifi_sniffer/wifi_sniffer.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// A beacon MAC header, built field by field from sec 9.2.4: Frame Control(2) + Duration(2) +
// Address1(6) + Address2(6) + Address3(6) + Sequence Control(2) = 24 octets.
//
//   Frame Control octet 0 = Subtype(8) << 4 | Type(0) << 2 | Version(0) = 0x80
//   Frame Control octet 1 = every flag clear                            = 0x00
//   To DS = 0 and From DS = 0, so Table 9-26 makes Address1 the destination (broadcast),
//   Address2 the transmitter (the AP) and Address3 the BSSID.
static const uint8_t BEACON[24] = {
    0x80, 0x00,                         // Frame Control: management, subtype 8 (Beacon)
    0x00, 0x00,                         // Duration/ID
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Address1: broadcast destination
    0x02, 0x00, 0x5E, 0x10, 0x20, 0x30, // Address2: transmitter
    0x02, 0x00, 0x5E, 0x10, 0x20, 0x30, // Address3: BSSID
    0x00, 0x00,                         // Sequence Control
};

void test_beacon_mac_header(void)
{
    WifiFrame f;
    WifiSniffer.parse_args.frame = BEACON;
    WifiSniffer.parse_args.len = sizeof(BEACON);
    WifiSniffer.parse_args.out = &f;
    WifiSniffer.parse(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);
    TEST_ASSERT_EQUAL_UINT8(0, f.version);
    TEST_ASSERT_EQUAL_UINT8(WIFI_TYPE_MGMT, f.type);
    TEST_ASSERT_EQUAL_UINT8(8, f.subtype);
    TEST_ASSERT_FALSE(f.to_ds);
    TEST_ASSERT_FALSE(f.from_ds);
    TEST_ASSERT_FALSE(f.retry);
    TEST_ASSERT_FALSE(f.protected_frame);
    TEST_ASSERT_EQUAL_UINT8(3, f.naddr);
    TEST_ASSERT_EQUAL_MEMORY(BEACON + 4, f.addr1, 6);
    TEST_ASSERT_EQUAL_MEMORY(BEACON + 10, f.addr2, 6);
    TEST_ASSERT_EQUAL_MEMORY(BEACON + 16, f.addr3, 6);
}

// Table 9-1 assigns Type 0 to Management, 1 to Control, 2 to Data and 3 to Extension, in B2-B3.
void test_frame_control_type_field(void)
{
    static const uint8_t WANT[4] = {WIFI_TYPE_MGMT, WIFI_TYPE_CTRL, WIFI_TYPE_DATA, WIFI_TYPE_EXT};
    for (uint8_t t = 0; t < 4; t++)
    {
        uint8_t frame[24];
        memcpy(frame, BEACON, sizeof(frame));
        frame[0] = (uint8_t)(t << 2);
        WifiFrame f;
        WifiSniffer.parse_args.frame = frame;
        WifiSniffer.parse_args.len = sizeof(frame);
        WifiSniffer.parse_args.out = &f;
        WifiSniffer.parse(protocore_wifi_sniffer_span());
        TEST_ASSERT_TRUE(WifiSniffer.ok);
        TEST_ASSERT_EQUAL_UINT8(WANT[t], f.type);
        TEST_ASSERT_EQUAL_UINT8(0, f.subtype);
        TEST_ASSERT_EQUAL_UINT8(0, f.version);
    }
}

// Protocol Version is B0-B1 and Subtype is B4-B7, so the two never bleed into each other.
void test_frame_control_version_and_subtype(void)
{
    for (uint8_t v = 0; v < 4; v++)
    {
        for (uint8_t sub = 0; sub < 16; sub++)
        {
            uint8_t frame[24];
            memcpy(frame, BEACON, sizeof(frame));
            frame[0] = (uint8_t)((sub << 4) | (WIFI_TYPE_DATA << 2) | v);
            WifiFrame f;
            WifiSniffer.parse_args.frame = frame;
            WifiSniffer.parse_args.len = sizeof(frame);
            WifiSniffer.parse_args.out = &f;
            WifiSniffer.parse(protocore_wifi_sniffer_span());
            TEST_ASSERT_TRUE(WifiSniffer.ok);
            TEST_ASSERT_EQUAL_UINT8(v, f.version);
            TEST_ASSERT_EQUAL_UINT8(WIFI_TYPE_DATA, f.type);
            TEST_ASSERT_EQUAL_UINT8(sub, f.subtype);
        }
    }
}

// The four flags this decoder exposes sit at B8 (To DS), B9 (From DS), B11 (Retry) and B14
// (Protected Frame), which is octet 1 bits 0, 1, 3 and 6.
void test_frame_control_flag_bit_positions(void)
{
    struct
    {
        uint8_t octet1;
        proto_bool to_ds;
        proto_bool from_ds;
        proto_bool retry;
        proto_bool prot;
    } static const CASES[] = {
        {0x00, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE},
        {0x01, PROTO_TRUE, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE},  // To DS
        {0x02, PROTO_FALSE, PROTO_TRUE, PROTO_FALSE, PROTO_FALSE},  // From DS
        {0x04, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE}, // More Fragments, not exposed
        {0x08, PROTO_FALSE, PROTO_FALSE, PROTO_TRUE, PROTO_FALSE},  // Retry
        {0x10, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE}, // Power Management
        {0x20, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE}, // More Data
        {0x40, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, PROTO_TRUE},  // Protected Frame
        {0x80, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE}, // +HTC
        {0x03, PROTO_TRUE, PROTO_TRUE, PROTO_FALSE, PROTO_FALSE},   // a WDS frame
        {0x4B, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE},     // all four at once
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        uint8_t frame[24];
        memcpy(frame, BEACON, sizeof(frame));
        frame[1] = CASES[i].octet1;
        WifiFrame f;
        WifiSniffer.parse_args.frame = frame;
        WifiSniffer.parse_args.len = sizeof(frame);
        WifiSniffer.parse_args.out = &f;
        WifiSniffer.parse(protocore_wifi_sniffer_span());
        TEST_ASSERT_TRUE(WifiSniffer.ok);
        TEST_ASSERT_EQUAL_INT(CASES[i].to_ds, f.to_ds);
        TEST_ASSERT_EQUAL_INT(CASES[i].from_ds, f.from_ds);
        TEST_ASSERT_EQUAL_INT(CASES[i].retry, f.retry);
        TEST_ASSERT_EQUAL_INT(CASES[i].prot, f.protected_frame);
    }
}

// A capture can be cut short. Address1 starts at octet 4, Address2 at 10 and Address3 at 16, so the
// count of decoded addresses follows from how many whole ones the capture holds, and any address
// that was not present stays zeroed rather than holding another frame's octets.
void test_truncated_capture_reports_how_many_addresses_it_held(void)
{
    static const uint8_t ZERO6[6] = {0, 0, 0, 0, 0, 0};
    WifiFrame f;

    for (size_t len = 0; len < 10; len++)
    {
        WifiSniffer.parse_args.frame = BEACON;
        WifiSniffer.parse_args.len = len;
        WifiSniffer.parse_args.out = &f;
        WifiSniffer.parse(protocore_wifi_sniffer_span());
        TEST_ASSERT_FALSE(WifiSniffer.ok);
    }

    WifiSniffer.parse_args.frame = BEACON;
    WifiSniffer.parse_args.len = 10;
    WifiSniffer.parse_args.out = &f;
    WifiSniffer.parse(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);
    TEST_ASSERT_EQUAL_UINT8(1, f.naddr);
    TEST_ASSERT_EQUAL_MEMORY(BEACON + 4, f.addr1, 6);
    TEST_ASSERT_EQUAL_MEMORY(ZERO6, f.addr2, 6);
    TEST_ASSERT_EQUAL_MEMORY(ZERO6, f.addr3, 6);

    WifiSniffer.parse_args.frame = BEACON;
    WifiSniffer.parse_args.len = 15;
    WifiSniffer.parse_args.out = &f;
    WifiSniffer.parse(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);
    TEST_ASSERT_EQUAL_UINT8(1, f.naddr);

    WifiSniffer.parse_args.frame = BEACON;
    WifiSniffer.parse_args.len = 16;
    WifiSniffer.parse_args.out = &f;
    WifiSniffer.parse(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);
    TEST_ASSERT_EQUAL_UINT8(2, f.naddr);
    TEST_ASSERT_EQUAL_MEMORY(BEACON + 10, f.addr2, 6);
    TEST_ASSERT_EQUAL_MEMORY(ZERO6, f.addr3, 6);

    WifiSniffer.parse_args.frame = BEACON;
    WifiSniffer.parse_args.len = 23;
    WifiSniffer.parse_args.out = &f;
    WifiSniffer.parse(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);
    TEST_ASSERT_EQUAL_UINT8(2, f.naddr);

    WifiSniffer.parse_args.frame = BEACON;
    WifiSniffer.parse_args.len = 24;
    WifiSniffer.parse_args.out = &f;
    WifiSniffer.parse(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);
    TEST_ASSERT_EQUAL_UINT8(3, f.naddr);
}

// Null arguments are reported, not written through.
void test_parse_null_arguments(void)
{
    WifiFrame f;
    WifiSniffer.parse_args.frame = NULL;
    WifiSniffer.parse_args.len = 24;
    WifiSniffer.parse_args.out = &f;
    WifiSniffer.parse(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok);
    WifiSniffer.parse_args.frame = BEACON;
    WifiSniffer.parse_args.len = 24;
    WifiSniffer.parse_args.out = NULL;
    WifiSniffer.parse(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok);
}

// The tally counts one frame per type and keeps the total equal to their sum.
void test_stats_tally(void)
{
    WifiStats s;
    WifiSniffer.stats_reset_args.s = &s;
    WifiSniffer.stats_reset(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT32(0, s.total);

    static const uint8_t TYPES[6] = {WIFI_TYPE_MGMT, WIFI_TYPE_MGMT, WIFI_TYPE_CTRL,
                                     WIFI_TYPE_DATA, WIFI_TYPE_DATA, WIFI_TYPE_EXT};
    for (size_t i = 0; i < sizeof(TYPES); i++)
    {
        uint8_t frame[24];
        memcpy(frame, BEACON, sizeof(frame));
        frame[0] = (uint8_t)(TYPES[i] << 2);
        WifiFrame f;
        WifiSniffer.parse_args.frame = frame;
        WifiSniffer.parse_args.len = sizeof(frame);
        WifiSniffer.parse_args.out = &f;
        WifiSniffer.parse(protocore_wifi_sniffer_span());
        TEST_ASSERT_TRUE(WifiSniffer.ok);
        WifiSniffer.stats_add_args.s = &s;
        WifiSniffer.stats_add_args.f = &f;
        WifiSniffer.stats_add(protocore_wifi_sniffer_span());
    }
    TEST_ASSERT_EQUAL_UINT32(2, s.mgmt);
    TEST_ASSERT_EQUAL_UINT32(1, s.ctrl);
    TEST_ASSERT_EQUAL_UINT32(2, s.data);
    TEST_ASSERT_EQUAL_UINT32(1, s.other); // Extension falls outside the three named buckets
    TEST_ASSERT_EQUAL_UINT32(6, s.total);
    TEST_ASSERT_EQUAL_UINT32(s.mgmt + s.ctrl + s.data + s.other, s.total);

    WifiSniffer.stats_reset_args.s = &s;
    WifiSniffer.stats_reset(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT32(0, s.total);
    WifiSniffer.stats_add_args.s = NULL;
    WifiSniffer.stats_add_args.f = NULL;
    WifiSniffer.stats_add(protocore_wifi_sniffer_span()); // neither argument is dereferenced
    WifiSniffer.stats_add_args.s = &s;
    WifiSniffer.stats_add_args.f = NULL;
    WifiSniffer.stats_add(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT32(0, s.total);
}

// RSSI is negative dBm, so stronger is closer to zero. The candidate must clear the current by MORE
// than the hysteresis: exactly the hysteresis holds position, which is what stops a roam loop
// between two APs that are equally far apart.
void test_roam_needs_to_clear_the_hysteresis(void)
{
    WifiSniffer.should_roam_args.cur_rssi = -70;
    WifiSniffer.should_roam_args.cand_rssi = -75;
    WifiSniffer.should_roam_args.hysteresis_db = 5;
    WifiSniffer.should_roam(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok); // candidate weaker
    WifiSniffer.should_roam_args.cur_rssi = -70;
    WifiSniffer.should_roam_args.cand_rssi = -70;
    WifiSniffer.should_roam_args.hysteresis_db = 5;
    WifiSniffer.should_roam(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok); // equal
    WifiSniffer.should_roam_args.cur_rssi = -70;
    WifiSniffer.should_roam_args.cand_rssi = -66;
    WifiSniffer.should_roam_args.hysteresis_db = 5;
    WifiSniffer.should_roam(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok); // 4 dB, under
    WifiSniffer.should_roam_args.cur_rssi = -70;
    WifiSniffer.should_roam_args.cand_rssi = -65;
    WifiSniffer.should_roam_args.hysteresis_db = 5;
    WifiSniffer.should_roam(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok); // 5 dB, exactly the hysteresis
    WifiSniffer.should_roam_args.cur_rssi = -70;
    WifiSniffer.should_roam_args.cand_rssi = -64;
    WifiSniffer.should_roam_args.hysteresis_db = 5;
    WifiSniffer.should_roam(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok); // 6 dB, over

    // Zero hysteresis roams on any improvement at all.
    WifiSniffer.should_roam_args.cur_rssi = -70;
    WifiSniffer.should_roam_args.cand_rssi = -69;
    WifiSniffer.should_roam_args.hysteresis_db = 0;
    WifiSniffer.should_roam(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);
    WifiSniffer.should_roam_args.cur_rssi = -70;
    WifiSniffer.should_roam_args.cand_rssi = -70;
    WifiSniffer.should_roam_args.hysteresis_db = 0;
    WifiSniffer.should_roam(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok);

    // The widest span an int8 pair can produce must not wrap: -128 to 127 is 255 dB.
    WifiSniffer.should_roam_args.cur_rssi = -128;
    WifiSniffer.should_roam_args.cand_rssi = 127;
    WifiSniffer.should_roam_args.hysteresis_db = 254;
    WifiSniffer.should_roam(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);
    WifiSniffer.should_roam_args.cur_rssi = -128;
    WifiSniffer.should_roam_args.cand_rssi = 127;
    WifiSniffer.should_roam_args.hysteresis_db = 255;
    WifiSniffer.should_roam(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok);
    WifiSniffer.should_roam_args.cur_rssi = 127;
    WifiSniffer.should_roam_args.cand_rssi = -128;
    WifiSniffer.should_roam_args.hysteresis_db = 0;
    WifiSniffer.should_roam(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok);
}

// A sweep starts on its first channel, hops one channel per dwell, and wraps back to the first
// while counting the completed sweep.
void test_scan_walks_the_range_and_wraps(void)
{
    WifiScan s;
    WifiSniffer.scan_init_args.s = &s;
    WifiSniffer.scan_init_args.first = 1;
    WifiSniffer.scan_init_args.last = 3;
    WifiSniffer.scan_init_args.dwell_ms = 100;
    WifiSniffer.scan_init_args.now_ms = 1000;
    WifiSniffer.scan_init(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT8(1, s.chan_first);
    TEST_ASSERT_EQUAL_UINT8(3, s.chan_last);
    TEST_ASSERT_EQUAL_UINT8(1, s.channel);
    TEST_ASSERT_EQUAL_UINT32(0, s.sweeps);

    WifiSniffer.scan_next_args.s = &s;
    WifiSniffer.scan_next_args.now_ms = 1100;
    WifiSniffer.scan_next(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT8(2, WifiSniffer.value);
    TEST_ASSERT_EQUAL_UINT32(0, s.sweeps);
    WifiSniffer.scan_next_args.s = &s;
    WifiSniffer.scan_next_args.now_ms = 1200;
    WifiSniffer.scan_next(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT8(3, WifiSniffer.value);
    TEST_ASSERT_EQUAL_UINT32(0, s.sweeps);
    WifiSniffer.scan_next_args.s = &s;
    WifiSniffer.scan_next_args.now_ms = 1300;
    WifiSniffer.scan_next(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT8(1, WifiSniffer.value); // wrap
    TEST_ASSERT_EQUAL_UINT32(1, s.sweeps);
    TEST_ASSERT_EQUAL_UINT32(1300, s.last_hop_ms);

    // A single-channel sweep re-selects the same channel and counts a sweep every hop.
    WifiScan one;
    WifiSniffer.scan_init_args.s = &one;
    WifiSniffer.scan_init_args.first = 6;
    WifiSniffer.scan_init_args.last = 6;
    WifiSniffer.scan_init_args.dwell_ms = 50;
    WifiSniffer.scan_init_args.now_ms = 0;
    WifiSniffer.scan_init(protocore_wifi_sniffer_span());
    WifiSniffer.scan_next_args.s = &one;
    WifiSniffer.scan_next_args.now_ms = 50;
    WifiSniffer.scan_next(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT8(6, WifiSniffer.value);
    TEST_ASSERT_EQUAL_UINT32(1, one.sweeps);

    WifiSniffer.scan_next_args.s = NULL;
    WifiSniffer.scan_next_args.now_ms = 0;
    WifiSniffer.scan_next(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT8(0, WifiSniffer.value);
}

// 2.4 GHz channels run 1..14, and the range cannot end before it starts.
void test_scan_init_clamps_the_range(void)
{
    WifiScan s;
    WifiSniffer.scan_init_args.s = &s;
    WifiSniffer.scan_init_args.first = 0;
    WifiSniffer.scan_init_args.last = 200;
    WifiSniffer.scan_init_args.dwell_ms = 100;
    WifiSniffer.scan_init_args.now_ms = 0;
    WifiSniffer.scan_init(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT8(1, s.chan_first);
    TEST_ASSERT_EQUAL_UINT8(14, s.chan_last);

    WifiSniffer.scan_init_args.s = &s;
    WifiSniffer.scan_init_args.first = 11;
    WifiSniffer.scan_init_args.last = 3;
    WifiSniffer.scan_init_args.dwell_ms = 100;
    WifiSniffer.scan_init_args.now_ms = 0;
    WifiSniffer.scan_init(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT8(11, s.chan_first);
    TEST_ASSERT_EQUAL_UINT8(11, s.chan_last); // last is pulled up to first, never below it
    TEST_ASSERT_EQUAL_UINT8(11, s.channel);
}

// The dwell is due once it has fully elapsed, and the comparison is unsigned so it stays correct
// across the 32-bit millisecond counter's rollover.
void test_scan_due_is_rollover_safe(void)
{
    WifiScan s;
    WifiSniffer.scan_init_args.s = &s;
    WifiSniffer.scan_init_args.first = 1;
    WifiSniffer.scan_init_args.last = 11;
    WifiSniffer.scan_init_args.dwell_ms = 120;
    WifiSniffer.scan_init_args.now_ms = 1000;
    WifiSniffer.scan_init(protocore_wifi_sniffer_span());
    WifiSniffer.scan_due_args.s = &s;
    WifiSniffer.scan_due_args.now_ms = 1000;
    WifiSniffer.scan_due(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok);
    WifiSniffer.scan_due_args.s = &s;
    WifiSniffer.scan_due_args.now_ms = 1119;
    WifiSniffer.scan_due(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok);
    WifiSniffer.scan_due_args.s = &s;
    WifiSniffer.scan_due_args.now_ms = 1120;
    WifiSniffer.scan_due(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);
    WifiSniffer.scan_due_args.s = &s;
    WifiSniffer.scan_due_args.now_ms = 5000;
    WifiSniffer.scan_due(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);

    // The dwell starts 100 ms before the counter wraps and ends 20 ms after it.
    WifiSniffer.scan_init_args.s = &s;
    WifiSniffer.scan_init_args.first = 1;
    WifiSniffer.scan_init_args.last = 11;
    WifiSniffer.scan_init_args.dwell_ms = 120;
    WifiSniffer.scan_init_args.now_ms = 0xFFFFFF9Cu;
    WifiSniffer.scan_init(protocore_wifi_sniffer_span());
    WifiSniffer.scan_due_args.s = &s;
    WifiSniffer.scan_due_args.now_ms = 0xFFFFFFFFu;
    WifiSniffer.scan_due(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok); // 99 ms in
    WifiSniffer.scan_due_args.s = &s;
    WifiSniffer.scan_due_args.now_ms = 0x00000013u;
    WifiSniffer.scan_due(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok); // 119 ms in, past the wrap
    WifiSniffer.scan_due_args.s = &s;
    WifiSniffer.scan_due_args.now_ms = 0x00000014u;
    WifiSniffer.scan_due(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok); // 120 ms in

    WifiSniffer.scan_due_args.s = NULL;
    WifiSniffer.scan_due_args.now_ms = 0;
    WifiSniffer.scan_due(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok);
}

// Build a frame whose transmitter address is the given octet repeated, so a survey entry can be
// checked against the AP it heard.
static void frame_from(uint8_t tag, uint8_t *out24)
{
    memcpy(out24, BEACON, 24);
    memset(out24 + 10, tag, 6);
}

// The survey keeps the strongest RSSI per channel with the transmitter that produced it, counts
// every frame, and ignores channels outside the swept range.
void test_survey_keeps_the_strongest_per_channel(void)
{
    WifiSurvey s;
    WifiSniffer.survey_reset_args.s = &s;
    WifiSniffer.survey_reset_args.first = 1;
    WifiSniffer.survey_reset_args.count = 3;
    WifiSniffer.survey_reset(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT8(1, s.first);
    TEST_ASSERT_EQUAL_UINT8(3, s.count);

    WifiSniffer.survey_get_args.s = &s;
    WifiSniffer.survey_get_args.channel = 1;
    WifiSniffer.survey_get(protocore_wifi_sniffer_span());
    const WifiChannelSurvey *e = WifiSniffer.ptr;
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_INT8(PROTOCORE_WIFI_RSSI_NONE, e->best_rssi);
    TEST_ASSERT_EQUAL_UINT32(0, e->frames);

    uint8_t weak[24];
    uint8_t strong[24];
    WifiFrame fw;
    WifiFrame fs;
    frame_from(0x11, weak);
    frame_from(0x22, strong);
    WifiSniffer.parse_args.frame = weak;
    WifiSniffer.parse_args.len = 24;
    WifiSniffer.parse_args.out = &fw;
    WifiSniffer.parse(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);
    WifiSniffer.parse_args.frame = strong;
    WifiSniffer.parse_args.len = 24;
    WifiSniffer.parse_args.out = &fs;
    WifiSniffer.parse(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);

    WifiSniffer.survey_add_args.s = &s;
    WifiSniffer.survey_add_args.channel = 1;
    WifiSniffer.survey_add_args.rssi = -80;
    WifiSniffer.survey_add_args.f = &fw;
    WifiSniffer.survey_add(protocore_wifi_sniffer_span());
    WifiSniffer.survey_add_args.s = &s;
    WifiSniffer.survey_add_args.channel = 1;
    WifiSniffer.survey_add_args.rssi = -60;
    WifiSniffer.survey_add_args.f = &fs;
    WifiSniffer.survey_add(protocore_wifi_sniffer_span()); // stronger, so it takes the entry
    WifiSniffer.survey_add_args.s = &s;
    WifiSniffer.survey_add_args.channel = 1;
    WifiSniffer.survey_add_args.rssi = -70;
    WifiSniffer.survey_add_args.f = &fw;
    WifiSniffer.survey_add(protocore_wifi_sniffer_span()); // weaker again, counted but not recorded

    WifiSniffer.survey_get_args.s = &s;
    WifiSniffer.survey_get_args.channel = 1;
    WifiSniffer.survey_get(protocore_wifi_sniffer_span());
    e = WifiSniffer.ptr;
    TEST_ASSERT_EQUAL_UINT32(3, e->frames);
    TEST_ASSERT_EQUAL_INT8(-60, e->best_rssi);
    static const uint8_t WANT_BSSID[6] = {0x22, 0x22, 0x22, 0x22, 0x22, 0x22};
    TEST_ASSERT_EQUAL_MEMORY(WANT_BSSID, e->best_bssid, 6);

    // Channels outside [first, first + count) are dropped, not folded into a neighbor.
    WifiSniffer.survey_add_args.s = &s;
    WifiSniffer.survey_add_args.channel = 4;
    WifiSniffer.survey_add_args.rssi = -10;
    WifiSniffer.survey_add_args.f = &fs;
    WifiSniffer.survey_add(protocore_wifi_sniffer_span());
    WifiSniffer.survey_add_args.s = &s;
    WifiSniffer.survey_add_args.channel = 0;
    WifiSniffer.survey_add_args.rssi = -10;
    WifiSniffer.survey_add_args.f = &fs;
    WifiSniffer.survey_add(protocore_wifi_sniffer_span());
    WifiSniffer.survey_get_args.s = &s;
    WifiSniffer.survey_get_args.channel = 4;
    WifiSniffer.survey_get(protocore_wifi_sniffer_span());
    TEST_ASSERT_NULL(WifiSniffer.ptr);
    WifiSniffer.survey_get_args.s = &s;
    WifiSniffer.survey_get_args.channel = 0;
    WifiSniffer.survey_get(protocore_wifi_sniffer_span());
    TEST_ASSERT_NULL(WifiSniffer.ptr);
    WifiSniffer.survey_get_args.s = &s;
    WifiSniffer.survey_get_args.channel = 1;
    WifiSniffer.survey_get(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT32(3, WifiSniffer.ptr->frames);
    WifiSniffer.survey_get_args.s = &s;
    WifiSniffer.survey_get_args.channel = 3;
    WifiSniffer.survey_get(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT32(0, WifiSniffer.ptr->frames);

    WifiSniffer.survey_reset_args.s = &s;
    WifiSniffer.survey_reset_args.first = 1;
    WifiSniffer.survey_reset_args.count = PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS + 1;
    WifiSniffer.survey_reset(protocore_wifi_sniffer_span());
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_WIFI_SNIFFER_MAX_CHANNELS, s.count);
}

// The roam candidate is the strongest channel other than the one being dwelt on; a survey that
// heard nothing has no candidate.
void test_survey_best_excludes_the_current_channel(void)
{
    WifiSurvey s;
    WifiSniffer.survey_reset_args.s = &s;
    WifiSniffer.survey_reset_args.first = 1;
    WifiSniffer.survey_reset_args.count = 6;
    WifiSniffer.survey_reset(protocore_wifi_sniffer_span());

    uint8_t channel = 0xFF;
    int8_t rssi = 0;
    WifiSniffer.survey_best_args.s = &s;
    WifiSniffer.survey_best_args.exclude_channel = 0;
    WifiSniffer.survey_best_args.out_channel = &channel;
    WifiSniffer.survey_best_args.out_rssi = &rssi;
    WifiSniffer.survey_best(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok);
    TEST_ASSERT_EQUAL_UINT8(0xFF, channel); // untouched when nothing was heard

    uint8_t frame[24];
    WifiFrame f;
    frame_from(0xAB, frame);
    WifiSniffer.parse_args.frame = frame;
    WifiSniffer.parse_args.len = sizeof(frame);
    WifiSniffer.parse_args.out = &f;
    WifiSniffer.parse(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);
    WifiSniffer.survey_add_args.s = &s;
    WifiSniffer.survey_add_args.channel = 1;
    WifiSniffer.survey_add_args.rssi = -75;
    WifiSniffer.survey_add_args.f = &f;
    WifiSniffer.survey_add(protocore_wifi_sniffer_span());
    WifiSniffer.survey_add_args.s = &s;
    WifiSniffer.survey_add_args.channel = 6;
    WifiSniffer.survey_add_args.rssi = -55;
    WifiSniffer.survey_add_args.f = &f;
    WifiSniffer.survey_add(protocore_wifi_sniffer_span());
    WifiSniffer.survey_add_args.s = &s;
    WifiSniffer.survey_add_args.channel = 3;
    WifiSniffer.survey_add_args.rssi = -65;
    WifiSniffer.survey_add_args.f = &f;
    WifiSniffer.survey_add(protocore_wifi_sniffer_span());

    WifiSniffer.survey_best_args.s = &s;
    WifiSniffer.survey_best_args.exclude_channel = 0;
    WifiSniffer.survey_best_args.out_channel = &channel;
    WifiSniffer.survey_best_args.out_rssi = &rssi;
    WifiSniffer.survey_best(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);
    TEST_ASSERT_EQUAL_UINT8(6, channel);
    TEST_ASSERT_EQUAL_INT8(-55, rssi);

    WifiSniffer.survey_best_args.s = &s;
    WifiSniffer.survey_best_args.exclude_channel = 6;
    WifiSniffer.survey_best_args.out_channel = &channel;
    WifiSniffer.survey_best_args.out_rssi = &rssi;
    WifiSniffer.survey_best(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok); // exclude the strongest
    TEST_ASSERT_EQUAL_UINT8(3, channel);
    TEST_ASSERT_EQUAL_INT8(-65, rssi);

    // The candidate feeds the roam decision: 6 clears 3 by 10 dB, so a 5 dB hysteresis roams.
    WifiSniffer.should_roam_args.cur_rssi = -65;
    WifiSniffer.should_roam_args.cand_rssi = -55;
    WifiSniffer.should_roam_args.hysteresis_db = 5;
    WifiSniffer.should_roam(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok);

    WifiSniffer.survey_best_args.s = NULL;
    WifiSniffer.survey_best_args.exclude_channel = 0;
    WifiSniffer.survey_best_args.out_channel = &channel;
    WifiSniffer.survey_best_args.out_rssi = &rssi;
    WifiSniffer.survey_best(protocore_wifi_sniffer_span());
    TEST_ASSERT_FALSE(WifiSniffer.ok);
    WifiSniffer.survey_best_args.s = &s;
    WifiSniffer.survey_best_args.exclude_channel = 0;
    WifiSniffer.survey_best_args.out_channel = NULL;
    WifiSniffer.survey_best_args.out_rssi = NULL;
    WifiSniffer.survey_best(protocore_wifi_sniffer_span());
    TEST_ASSERT_TRUE(WifiSniffer.ok); // both outputs are optional
}

// Every entry-point tolerates a null state rather than writing through it.
void test_null_state_is_refused(void)
{
    WifiSniffer.stats_reset_args.s = NULL;
    WifiSniffer.stats_reset(protocore_wifi_sniffer_span());
    WifiSniffer.scan_init_args.s = NULL;
    WifiSniffer.scan_init_args.first = 1;
    WifiSniffer.scan_init_args.last = 11;
    WifiSniffer.scan_init_args.dwell_ms = 100;
    WifiSniffer.scan_init_args.now_ms = 0;
    WifiSniffer.scan_init(protocore_wifi_sniffer_span());
    WifiSniffer.survey_reset_args.s = NULL;
    WifiSniffer.survey_reset_args.first = 1;
    WifiSniffer.survey_reset_args.count = 3;
    WifiSniffer.survey_reset(protocore_wifi_sniffer_span());
    WifiSniffer.survey_add_args.s = NULL;
    WifiSniffer.survey_add_args.channel = 1;
    WifiSniffer.survey_add_args.rssi = -50;
    WifiSniffer.survey_add_args.f = NULL;
    WifiSniffer.survey_add(protocore_wifi_sniffer_span());
    WifiSniffer.survey_get_args.s = NULL;
    WifiSniffer.survey_get_args.channel = 1;
    WifiSniffer.survey_get(protocore_wifi_sniffer_span());
    TEST_ASSERT_NULL(WifiSniffer.ptr);
}
