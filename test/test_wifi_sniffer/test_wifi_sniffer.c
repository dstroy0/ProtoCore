// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/wifi_sniffer: 802.11 header decode, traffic tally, roaming decision.

#include "services/radio/wifi_sniffer/wifi_sniffer.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// A data frame from an AP (FromDS=1): FC=0x08,0x02, dur, 3 addresses, seq.
static const uint8_t DATA_FROMDS[] = {
    0x08, 0x02,                         // FC: type=2 (data), subtype=0, FromDS
    0x00, 0x00,                         // duration
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, // addr1 (RA / destination)
    0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, // addr2 (TA / BSSID)
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // addr3 (SA)
    0x00, 0x00                          // seq ctrl
};

// A beacon (management, subtype 8): FC=0x80,0x00.
static const uint8_t BEACON[] = {0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xaa, 0xbb,
                                 0xcc, 0xdd, 0xee, 0xff, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00, 0x00};

// A CTS control frame (subtype 0xC): FC=0xC4,0x00, dur, addr1 only (10 bytes).
static const uint8_t CTS[] = {0xc4, 0x00, 0x3a, 0x01, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

void test_parse_data(void)
{
    WifiFrame f;
    TEST_ASSERT_TRUE(pc_wifi_parse(DATA_FROMDS, sizeof(DATA_FROMDS), &f));
    TEST_ASSERT_EQUAL_UINT8(WIFI_TYPE_DATA, f.type);
    TEST_ASSERT_EQUAL_UINT8(0, f.subtype);
    TEST_ASSERT_TRUE(f.from_ds);
    TEST_ASSERT_FALSE(f.to_ds);
    TEST_ASSERT_EQUAL_UINT8(3, f.naddr);
    uint8_t a2[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a2, f.addr2, 6);
}

void test_parse_beacon(void)
{
    WifiFrame f;
    TEST_ASSERT_TRUE(pc_wifi_parse(BEACON, sizeof(BEACON), &f));
    TEST_ASSERT_EQUAL_UINT8(WIFI_TYPE_MGMT, f.type);
    TEST_ASSERT_EQUAL_UINT8(8, f.subtype); // beacon subtype
    TEST_ASSERT_EQUAL_UINT8(3, f.naddr);
}

void test_parse_ctrl_short(void)
{
    WifiFrame f;
    TEST_ASSERT_TRUE(pc_wifi_parse(CTS, sizeof(CTS), &f));
    TEST_ASSERT_EQUAL_UINT8(WIFI_TYPE_CTRL, f.type);
    TEST_ASSERT_EQUAL_UINT8(0x0C, f.subtype); // CTS
    TEST_ASSERT_EQUAL_UINT8(1, f.naddr);      // only addr1 fits
    // Too short is rejected.
    TEST_ASSERT_FALSE(pc_wifi_parse(CTS, 9, &f));
    TEST_ASSERT_FALSE(pc_wifi_parse(NULL, 24, &f));
    TEST_ASSERT_FALSE(pc_wifi_parse(CTS, sizeof(CTS), NULL)); // null out
}

void test_stats(void)
{
    WifiStats s;
    pc_wifi_stats_reset(&s);
    WifiFrame f;
    pc_wifi_parse(BEACON, sizeof(BEACON), &f);
    pc_wifi_stats_add(&s, &f);
    pc_wifi_parse(DATA_FROMDS, sizeof(DATA_FROMDS), &f);
    pc_wifi_stats_add(&s, &f);
    pc_wifi_parse(CTS, sizeof(CTS), &f);
    pc_wifi_stats_add(&s, &f);
    TEST_ASSERT_EQUAL_UINT32(1, s.mgmt);
    TEST_ASSERT_EQUAL_UINT32(1, s.data);
    TEST_ASSERT_EQUAL_UINT32(1, s.ctrl);
    TEST_ASSERT_EQUAL_UINT32(3, s.total);
}

void test_roam(void)
{
    // Current -80 dBm, candidate -70 dBm, 8 dB hysteresis: 10 > 8 -> roam.
    TEST_ASSERT_TRUE(pc_wifi_should_roam(-80, -70, 8));
    // Candidate only 5 dB stronger -> stay.
    TEST_ASSERT_FALSE(pc_wifi_should_roam(-80, -75, 8));
    // Candidate weaker -> stay.
    TEST_ASSERT_FALSE(pc_wifi_should_roam(-60, -75, 8));
}

void test_stats_add_null_and_default_type()
{
    pc_wifi_stats_reset(NULL);     // null guard
    pc_wifi_stats_add(NULL, NULL); // null guard
    WifiStats st = {0};
    WifiFrame f = {0};
    f.type = 3; // reserved/extension type -> default switch arm (other++)
    pc_wifi_stats_add(&st, &f);
    TEST_ASSERT_EQUAL_UINT32(1, st.other);

    WifiStats st2 = {0};
    pc_wifi_stats_add(&st2, NULL); // valid stats, null frame -> no-op
    TEST_ASSERT_EQUAL_UINT32(0, st2.total);
}

// --- Channel-hop scan schedule ----------------------------------------------------------

void test_scan_hops_and_wraps()
{
    WifiScan s;
    pc_wifi_scan_init(&s, 1, 3, 100, 1000);
    TEST_ASSERT_EQUAL_UINT8(1, s.channel);
    TEST_ASSERT_EQUAL_UINT32(0, s.sweeps);

    TEST_ASSERT_FALSE(pc_wifi_scan_due(&s, 1099)); // dwell not elapsed
    TEST_ASSERT_TRUE(pc_wifi_scan_due(&s, 1100));  // exactly at the dwell

    TEST_ASSERT_EQUAL_UINT8(2, pc_wifi_scan_next(&s, 1100));
    TEST_ASSERT_EQUAL_UINT8(3, pc_wifi_scan_next(&s, 1200));
    TEST_ASSERT_EQUAL_UINT8(1, pc_wifi_scan_next(&s, 1300)); // wraps
    TEST_ASSERT_EQUAL_UINT32(1, s.sweeps);
    TEST_ASSERT_FALSE(pc_wifi_scan_due(&s, 1350)); // dwell restarted on hop
}

void test_scan_clamps_and_single_channel()
{
    WifiScan s;
    pc_wifi_scan_init(&s, 0, 99, 50, 0); // out-of-range clamps to 1..14
    TEST_ASSERT_EQUAL_UINT8(1, s.chan_first);
    TEST_ASSERT_EQUAL_UINT8(14, s.chan_last);

    pc_wifi_scan_init(&s, 6, 3, 50, 0); // last < first -> single channel
    TEST_ASSERT_EQUAL_UINT8(6, s.chan_first);
    TEST_ASSERT_EQUAL_UINT8(6, s.chan_last);
    TEST_ASSERT_EQUAL_UINT8(6, pc_wifi_scan_next(&s, 100)); // stays put, counts a sweep
    TEST_ASSERT_EQUAL_UINT32(1, s.sweeps);
}

void test_scan_wrapsafe_across_millis_rollover()
{
    WifiScan s;
    pc_wifi_scan_init(&s, 1, 3, 100, 0xFFFFFFC0);        // 64 ms before rollover
    TEST_ASSERT_FALSE(pc_wifi_scan_due(&s, 0xFFFFFFFF)); // 63 ms elapsed
    TEST_ASSERT_FALSE(pc_wifi_scan_due(&s, 0x00000020)); // 64 + 32 = 96 ms - still short
    TEST_ASSERT_TRUE(pc_wifi_scan_due(&s, 0x00000024));  // 64 + 36 = 100 ms - due, across the wrap
    TEST_ASSERT_TRUE(pc_wifi_scan_due(&s, 0x00000100));  // well past
}

void test_scan_null_guards()
{
    pc_wifi_scan_init(NULL, 1, 3, 10, 0);
    TEST_ASSERT_FALSE(pc_wifi_scan_due(NULL, 100));
    TEST_ASSERT_EQUAL_UINT8(0, pc_wifi_scan_next(NULL, 100));
}

// --- Per-channel survey -----------------------------------------------------------------

static WifiFrame make_frame(const uint8_t *bssid)
{
    WifiFrame f = {0};
    f.type = WIFI_TYPE_MGMT;
    f.naddr = 2;
    if (bssid)
    {
        memcpy(f.addr2, bssid, 6);
    }
    return f;
}

void test_survey_tracks_best_rssi_per_channel()
{
    const uint8_t ap_a[6] = {0xAA, 0, 0, 0, 0, 1};
    const uint8_t ap_b[6] = {0xBB, 0, 0, 0, 0, 2};
    WifiSurvey s;
    pc_wifi_survey_reset(&s, 1, 6);

    WifiFrame fa = make_frame(ap_a);
    WifiFrame fb = make_frame(ap_b);
    pc_wifi_survey_add(&s, 1, -70, &fa);
    pc_wifi_survey_add(&s, 1, -55, &fb); // stronger on the same channel -> replaces
    pc_wifi_survey_add(&s, 1, -80, &fa); // weaker -> ignored for best
    pc_wifi_survey_add(&s, 6, -60, &fa);

    const WifiChannelSurvey *c1 = pc_wifi_survey_get(&s, 1);
    TEST_ASSERT_NOT_NULL(c1);
    TEST_ASSERT_EQUAL_UINT32(3, c1->frames);
    TEST_ASSERT_EQUAL_INT8(-55, c1->best_rssi);
    TEST_ASSERT_EQUAL_MEMORY(ap_b, c1->best_bssid, 6); // the strongest frame's transmitter

    const WifiChannelSurvey *c6 = pc_wifi_survey_get(&s, 6);
    TEST_ASSERT_NOT_NULL(c6);
    TEST_ASSERT_EQUAL_INT8(-60, c6->best_rssi);

    // An untouched channel stays "nothing heard".
    const WifiChannelSurvey *c3 = pc_wifi_survey_get(&s, 3);
    TEST_ASSERT_NOT_NULL(c3);
    TEST_ASSERT_EQUAL_UINT32(0, c3->frames);
    TEST_ASSERT_EQUAL_INT8(PC_WIFI_RSSI_NONE, c3->best_rssi);
}

void test_survey_out_of_range_ignored()
{
    WifiSurvey s;
    pc_wifi_survey_reset(&s, 5, 3); // tracks 5,6,7 only
    WifiFrame f = make_frame(NULL);
    pc_wifi_survey_add(&s, 1, -40, &f); // below range
    pc_wifi_survey_add(&s, 9, -40, &f); // above range
    pc_wifi_survey_add(&s, 6, -50, &f); // in range
    TEST_ASSERT_NULL(pc_wifi_survey_get(&s, 1));
    TEST_ASSERT_NULL(pc_wifi_survey_get(&s, 9));
    TEST_ASSERT_EQUAL_UINT32(1, pc_wifi_survey_get(&s, 6)->frames);
}

void test_survey_best_picks_strongest_and_excludes()
{
    WifiSurvey s;
    pc_wifi_survey_reset(&s, 1, 11);
    WifiFrame f = make_frame(NULL);
    pc_wifi_survey_add(&s, 1, -70, &f);
    pc_wifi_survey_add(&s, 6, -45, &f); // strongest
    pc_wifi_survey_add(&s, 11, -60, &f);

    uint8_t ch = 0;
    int8_t rssi = 0;
    TEST_ASSERT_TRUE(pc_wifi_survey_best(&s, 0, &ch, &rssi));
    TEST_ASSERT_EQUAL_UINT8(6, ch);
    TEST_ASSERT_EQUAL_INT8(-45, rssi);

    // Excluding the current channel yields the next best - the roaming candidate.
    TEST_ASSERT_TRUE(pc_wifi_survey_best(&s, 6, &ch, &rssi));
    TEST_ASSERT_EQUAL_UINT8(11, ch);
    TEST_ASSERT_EQUAL_INT8(-60, rssi);

    // A silent survey has no candidate.
    WifiSurvey empty;
    pc_wifi_survey_reset(&empty, 1, 11);
    TEST_ASSERT_FALSE(pc_wifi_survey_best(&empty, 0, &ch, &rssi));
    TEST_ASSERT_FALSE(pc_wifi_survey_best(NULL, 0, &ch, &rssi));
}

void test_survey_feeds_roam_decision()
{
    // The end-to-end decision a channel-agility roam makes: survey -> best candidate -> hysteresis.
    WifiSurvey s;
    pc_wifi_survey_reset(&s, 1, 11);
    WifiFrame f = make_frame(NULL);
    pc_wifi_survey_add(&s, 1, -75, &f); // current channel, weak
    pc_wifi_survey_add(&s, 6, -50, &f); // candidate, much stronger

    uint8_t cand_ch = 0;
    int8_t cand_rssi = 0;
    TEST_ASSERT_TRUE(pc_wifi_survey_best(&s, 1, &cand_ch, &cand_rssi));
    TEST_ASSERT_EQUAL_UINT8(6, cand_ch);
    TEST_ASSERT_TRUE(pc_wifi_should_roam(-75, cand_rssi, 8));  // 25 dB better -> roam
    TEST_ASSERT_FALSE(pc_wifi_should_roam(-55, cand_rssi, 8)); // only 5 dB -> stay
}

void test_survey_add_null_frame_and_short_naddr()
{
    WifiSurvey s;
    pc_wifi_survey_reset(&s, 1, 6);

    // Valid slot, null frame pointer -> tally counts but the bssid copy is skipped.
    pc_wifi_survey_add(&s, 1, -50, NULL);
    const WifiChannelSurvey *c1 = pc_wifi_survey_get(&s, 1);
    TEST_ASSERT_NOT_NULL(c1);
    TEST_ASSERT_EQUAL_UINT32(1, c1->frames);
    TEST_ASSERT_EQUAL_INT8(-50, c1->best_rssi);

    // Non-null frame but naddr < 2 (addr2 never decoded) -> best_rssi still updates, bssid copy skipped.
    WifiFrame f = make_frame(NULL);
    f.naddr = 1;
    pc_wifi_survey_add(&s, 1, -40, &f); // stronger -> replaces best_rssi
    const WifiChannelSurvey *c1b = pc_wifi_survey_get(&s, 1);
    TEST_ASSERT_EQUAL_UINT32(2, c1b->frames);
    TEST_ASSERT_EQUAL_INT8(-40, c1b->best_rssi);
}

void test_survey_best_null_out_params()
{
    // A caller that only wants the bool (does it need to roam at all?) may pass null outs.
    WifiSurvey s;
    pc_wifi_survey_reset(&s, 1, 11);
    WifiFrame f = make_frame(NULL);
    pc_wifi_survey_add(&s, 6, -45, &f);

    TEST_ASSERT_TRUE(pc_wifi_survey_best(&s, 0, NULL, NULL));
}

void test_survey_reset_clamps_count()
{
    WifiSurvey s;
    pc_wifi_survey_reset(&s, 1, 200); // clamps to the table size
    TEST_ASSERT_EQUAL_UINT8(PC_WIFI_SNIFFER_MAX_CHANNELS, s.count);
    pc_wifi_survey_reset(NULL, 1, 4); // null guard
    pc_wifi_survey_add(NULL, 1, -50, NULL);
    TEST_ASSERT_NULL(pc_wifi_survey_get(NULL, 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_data);
    RUN_TEST(test_parse_beacon);
    RUN_TEST(test_parse_ctrl_short);
    RUN_TEST(test_stats);
    RUN_TEST(test_roam);
    RUN_TEST(test_stats_add_null_and_default_type);
    RUN_TEST(test_scan_hops_and_wraps);
    RUN_TEST(test_scan_clamps_and_single_channel);
    RUN_TEST(test_scan_wrapsafe_across_millis_rollover);
    RUN_TEST(test_scan_null_guards);
    RUN_TEST(test_survey_tracks_best_rssi_per_channel);
    RUN_TEST(test_survey_out_of_range_ignored);
    RUN_TEST(test_survey_best_picks_strongest_and_excludes);
    RUN_TEST(test_survey_feeds_roam_decision);
    RUN_TEST(test_survey_add_null_frame_and_short_naddr);
    RUN_TEST(test_survey_best_null_out_params);
    RUN_TEST(test_survey_reset_clamps_count);
    return UNITY_END();
}
