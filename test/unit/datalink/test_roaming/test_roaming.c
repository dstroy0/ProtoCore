// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Wi-Fi roaming decision layer (network_drivers/network/roaming): the pure policy that fuses the
// current RSSI, a candidate neighbour list, and an optional 802.11v BTM hint into a roam/stay decision.

#include "network_drivers/datalink/roaming.h"

#include "mmgr/plaintext.h" // pc_roam_btm is written through mem, so it is borrowed, not declared
#include "mmgr/protomem.h"  // mem.cpy / mem.zero
#include <string.h>

#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static const uint8_t CUR[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t AP_A[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
static const uint8_t AP_B[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};

static const pc_roam_policy POLICY = {/*roam_rssi_threshold_dbm=*/-70, /*hysteresis_db=*/8};

void test_stay_when_link_strong()
{
    // Strong current link (-50); even a stronger candidate does not trigger a roam below the threshold.
    pc_roam_neighbor nb[2] = {{{0}, 6, -45}, {{0}, 11, -80}};
    memcpy(nb[0].bssid, AP_A, 6);
    memcpy(nb[1].bssid, AP_B, 6);
    pc_roam_decision d;
    Roam.decide(CUR, -50, nb, 2, NULL, &POLICY, &d);
    TEST_ASSERT_FALSE(d.roam);
    TEST_ASSERT_EQUAL(PC_ROAM_NONE, d.reason);
}

void test_roam_on_low_rssi_to_strongest()
{
    // Weak current link (-78) and AP_A is clearly stronger (-55): roam to AP_A.
    pc_roam_neighbor nb[2] = {{{0}, 6, -55}, {{0}, 11, -85}};
    memcpy(nb[0].bssid, AP_A, 6);
    memcpy(nb[1].bssid, AP_B, 6);
    pc_roam_decision d;
    Roam.decide(CUR, -78, nb, 2, NULL, &POLICY, &d);
    TEST_ASSERT_TRUE(d.roam);
    TEST_ASSERT_EQUAL(PC_ROAM_LOW_RSSI, d.reason);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, d.target_bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(6, d.target_channel);
}

void test_hysteresis_blocks_marginal_roam()
{
    // Weak link (-78) but the best candidate is only 4 dB better (< 8 dB hysteresis): stay.
    pc_roam_neighbor nb[1] = {{{0}, 6, -74}};
    memcpy(nb[0].bssid, AP_A, 6);
    pc_roam_decision d;
    Roam.decide(CUR, -78, nb, 1, NULL, &POLICY, &d);
    TEST_ASSERT_FALSE(d.roam);
}

void test_btm_imminent_forces_roam()
{
    pc_roam_neighbor nb[2] = {{{0}, 6, -60}, {{0}, 11, -50}};
    memcpy(nb[0].bssid, AP_A, 6);
    memcpy(nb[1].bssid, AP_B, 6);
    size_t scope = pc_plaintext_mark();
    pc_span bs = pc_plaintext_span(sizeof(pc_roam_btm), 8);
    TEST_ASSERT_TRUE(pc_span_ok(bs));
    pc_roam_btm *btm = (pc_roam_btm *)bs.buf;
    mem.zero(btm, sizeof(*btm));
    btm->present = PROTO_TRUE;
    btm->disassoc_imminent = PROTO_TRUE;
    btm->has_preferred = PROTO_TRUE;
    mem.cpy(btm->preferred_bssid, AP_A, 6); // network names AP_A even though AP_B is stronger

    // A strong current link (-45) would normally stay, but disassoc-imminent forces the roam to the
    // preferred AP_A.
    pc_roam_decision d;
    Roam.decide(CUR, -45, nb, 2, btm, &POLICY, &d);
    TEST_ASSERT_TRUE(d.roam);
    TEST_ASSERT_EQUAL(PC_ROAM_BTM_IMMINENT, d.reason);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, d.target_bssid, 6);

    // No preferred -> roam to the strongest candidate (AP_B).
    btm->has_preferred = PROTO_FALSE;
    Roam.decide(CUR, -45, nb, 2, btm, &POLICY, &d);
    TEST_ASSERT_TRUE(d.roam);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_B, d.target_bssid, 6);
    pc_plaintext_release(scope);
}

void test_btm_suggested_honoured_only_if_not_weaker()
{
    pc_roam_neighbor nb[1] = {{{0}, 11, -55}}; // AP_B at -55
    memcpy(nb[0].bssid, AP_B, 6);
    size_t scope = pc_plaintext_mark();
    pc_span bs = pc_plaintext_span(sizeof(pc_roam_btm), 8);
    TEST_ASSERT_TRUE(pc_span_ok(bs));
    pc_roam_btm *btm = (pc_roam_btm *)bs.buf;
    mem.zero(btm, sizeof(*btm));
    btm->present = PROTO_TRUE;
    btm->has_preferred = PROTO_TRUE;
    mem.cpy(btm->preferred_bssid, AP_B, 6);

    // Current link -50 (strong), suggested AP_B is -55 (weaker): do NOT chase into a worse AP.
    pc_roam_decision d;
    Roam.decide(CUR, -50, nb, 1, btm, &POLICY, &d);
    TEST_ASSERT_FALSE(d.roam);

    // If AP_B is at least as strong as the current link, honour the steering hint.
    nb[0].rssi_dbm = -48;
    Roam.decide(CUR, -50, nb, 1, btm, &POLICY, &d);
    TEST_ASSERT_TRUE(d.roam);
    TEST_ASSERT_EQUAL(PC_ROAM_BTM_SUGGESTED, d.reason);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_B, d.target_bssid, 6);
    pc_plaintext_release(scope);
}

void test_never_targets_current_and_guards()
{
    // The neighbour list contains only the current BSSID -> nothing to roam to even on a weak link.
    pc_roam_neighbor nb[1] = {{{0}, 6, -30}};
    memcpy(nb[0].bssid, CUR, 6);
    pc_roam_decision d;
    Roam.decide(CUR, -90, nb, 1, NULL, &POLICY, &d);
    TEST_ASSERT_FALSE(d.roam);

    // A null policy falls back to a conservative default (threshold -75, hysteresis 8).
    pc_roam_neighbor good[1] = {{{0}, 6, -50}};
    memcpy(good[0].bssid, AP_A, 6);
    Roam.decide(CUR, -80, good, 1, NULL, NULL, &d);
    TEST_ASSERT_TRUE(d.roam);
    TEST_ASSERT_EQUAL(PC_ROAM_LOW_RSSI, d.reason);

    // Null out is a no-op (no crash); an empty neighbour list stays.
    Roam.decide(CUR, -90, NULL, 0, NULL, &POLICY, NULL);
    Roam.decide(CUR, -90, NULL, 0, NULL, &POLICY, &d);
    TEST_ASSERT_FALSE(d.roam);
}

// Append an id-52 Neighbor Report element for @p bssid on @p channel.
static size_t nr_elem(uint8_t *buf, size_t p, const uint8_t *bssid, uint8_t channel)
{
    buf[p++] = 52; // element id
    buf[p++] = 13; // body length
    memcpy(buf + p, bssid, 6);
    p += 6;
    buf[p++] = 0; // BSSID Info (4)
    buf[p++] = 0;
    buf[p++] = 0;
    buf[p++] = 0;
    buf[p++] = 0x51; // operating class
    buf[p++] = channel;
    buf[p++] = 0x09; // PHY type
    return p;
}

void test_parse_neighbor_report()
{
    uint8_t buf[64];
    size_t p = nr_elem(buf, 0, AP_A, 6);
    // A non-neighbor element (id 7, len 3) between the two must be skipped.
    buf[p++] = 7;
    buf[p++] = 3;
    buf[p++] = 1;
    buf[p++] = 2;
    buf[p++] = 3;
    p = nr_elem(buf, p, AP_B, 11);

    pc_roam_neighbor nb[4];
    uint8_t count = Roam.parse_neighbor_report(buf, p, nb, 4);
    TEST_ASSERT_EQUAL_UINT8(2, count);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, nb[0].bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(6, nb[0].channel);
    TEST_ASSERT_EQUAL_INT8(PC_ROAM_RSSI_UNKNOWN, nb[0].rssi_dbm); // RSSI not in the report
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_B, nb[1].bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(11, nb[1].channel);

    // End to end: fill measured RSSI, then the decision layer picks the strong candidate on a weak link.
    nb[0].rssi_dbm = -50;
    nb[1].rssi_dbm = -80;
    pc_roam_decision d;
    Roam.decide(CUR, -78, nb, count, NULL, &POLICY, &d);
    TEST_ASSERT_TRUE(d.roam);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, d.target_bssid, 6);

    // max cap + null guard.
    TEST_ASSERT_EQUAL_UINT8(1, Roam.parse_neighbor_report(buf, p, nb, 1));
    TEST_ASSERT_EQUAL_UINT8(0, Roam.parse_neighbor_report(NULL, p, nb, 4));
}

void test_parse_neighbor_report_edges()
{
    pc_roam_neighbor nb[4];
    // A neighbor element shorter than the 13-octet body is skipped (not decoded).
    uint8_t shortelem[12] = {52, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8(0, Roam.parse_neighbor_report(shortelem, sizeof(shortelem), nb, 4));
    // A truncated element (claims 13 but the buffer holds less) ends the walk with nothing parsed.
    uint8_t trunc[5] = {52, 13, 0xAA, 0xAA, 0xAA};
    TEST_ASSERT_EQUAL_UINT8(0, Roam.parse_neighbor_report(trunc, sizeof(trunc), nb, 4));
}

void test_parse_btm_request()
{
    // BTM Request: preferred-list (bit 0) + disassoc-imminent (bit 2) = 0x05, one candidate (AP_A).
    uint8_t f[64];
    size_t p = 0;
    f[p++] = 0x0A; // WNM category
    f[p++] = 0x07; // BTM Request action
    f[p++] = 0x01; // dialog token
    f[p++] = 0x05; // request mode
    f[p++] = 0x00; // disassoc timer
    f[p++] = 0x00;
    f[p++] = 0x0A;              // validity interval
    p = nr_elem(f, p, AP_A, 6); // candidate list

    size_t scope = pc_plaintext_mark();
    pc_span bs = pc_plaintext_span(sizeof(pc_roam_btm), 8);
    TEST_ASSERT_TRUE(pc_span_ok(bs));
    pc_roam_btm *btm = (pc_roam_btm *)bs.buf;
    TEST_ASSERT_TRUE(Roam.parse_btm_request(f, p, btm));
    TEST_ASSERT_TRUE(btm->present);
    TEST_ASSERT_TRUE(btm->disassoc_imminent);
    TEST_ASSERT_TRUE(btm->has_preferred);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, btm->preferred_bssid, 6);

    // Feed the parsed hint to the decision layer: even a strong link roams to the preferred AP.
    pc_roam_neighbor nb[1] = {{{0}, 6, -60}};
    memcpy(nb[0].bssid, AP_A, 6);
    pc_roam_decision d;
    Roam.decide(CUR, -45, nb, 1, btm, &POLICY, &d);
    TEST_ASSERT_TRUE(d.roam);
    TEST_ASSERT_EQUAL(PC_ROAM_BTM_IMMINENT, d.reason);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, d.target_bssid, 6);
    pc_plaintext_release(scope);
}

void test_parse_btm_request_optional_fields_and_guards()
{
    // Request mode with BSS-Termination-Included (bit 3) + preferred list (bit 0) = 0x09: the candidate
    // list sits 12 octets further along, past the BSS Termination Duration.
    uint8_t f[80];
    size_t p = 0;
    f[p++] = 0x0A;
    f[p++] = 0x07;
    f[p++] = 0x01;
    f[p++] = 0x09; // mode: pref list + BSS termination included
    f[p++] = 0x00;
    f[p++] = 0x00;
    f[p++] = 0x0A;
    for (int i = 0; i < 12; i++) // BSS Termination Duration (12 octets)
    {
        f[p++] = 0x00;
    }
    p = nr_elem(f, p, AP_B, 11);

    size_t scope = pc_plaintext_mark();
    pc_span bs = pc_plaintext_span(sizeof(pc_roam_btm), 8);
    TEST_ASSERT_TRUE(pc_span_ok(bs));
    pc_roam_btm *btm = (pc_roam_btm *)bs.buf;
    TEST_ASSERT_TRUE(Roam.parse_btm_request(f, p, btm));
    TEST_ASSERT_TRUE(btm->has_preferred);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_B, btm->preferred_bssid, 6); // decoded past the termination field
    TEST_ASSERT_FALSE(btm->disassoc_imminent);                   // bit 2 not set

    // A request without the preferred-list bit yields no preferred target.
    uint8_t plain[7] = {0x0A, 0x07, 0x01, 0x04, 0x00, 0x00, 0x0A}; // disassoc only
    TEST_ASSERT_TRUE(Roam.parse_btm_request(plain, sizeof(plain), btm));
    TEST_ASSERT_TRUE(btm->disassoc_imminent);
    TEST_ASSERT_FALSE(btm->has_preferred);

    // A non-BTM frame (wrong category/action) and a truncated frame are rejected.
    uint8_t wrong[7] = {0x05, 0x07, 0x01, 0x05, 0, 0, 0}; // category 5, not WNM
    TEST_ASSERT_FALSE(Roam.parse_btm_request(wrong, sizeof(wrong), btm));
    TEST_ASSERT_FALSE(Roam.parse_btm_request(f, 5, btm)); // too short
    TEST_ASSERT_FALSE(Roam.parse_btm_request(NULL, 7, btm));
    pc_plaintext_release(scope);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_stay_when_link_strong);
    RUN_TEST(test_roam_on_low_rssi_to_strongest);
    RUN_TEST(test_hysteresis_blocks_marginal_roam);
    RUN_TEST(test_btm_imminent_forces_roam);
    RUN_TEST(test_btm_suggested_honoured_only_if_not_weaker);
    RUN_TEST(test_never_targets_current_and_guards);
    RUN_TEST(test_parse_neighbor_report);
    RUN_TEST(test_parse_neighbor_report_edges);
    RUN_TEST(test_parse_btm_request);
    RUN_TEST(test_parse_btm_request_optional_fields_and_guards);
    return UNITY_END();
}
