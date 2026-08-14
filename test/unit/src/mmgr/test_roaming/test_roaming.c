// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the Layer 2 roam decision (network_drivers/datalink/roaming.h).
//
// IEEE Std 802.11 governs the two decodes, not an IETF RFC, and the IEEE text is not cached in this
// repo. The field assignments below are taken from published encodings of that standard: element ID
// 52 for the Neighbor Report and action category 10 for WNM are Linux's include/linux/ieee80211.h
// (WLAN_EID_NEIGHBOR_REPORT, WLAN_CATEGORY_WNM), and the BTM Request action code 7 plus the Request
// Mode bits 0..4 are hostapd's src/common/ieee802_11_defs.h (WNM_BSS_TRANS_MGMT_REQ,
// WNM_BSS_TM_REQ_PREF_CAND_LIST_INCLUDED / _ABRIDGED / _DISASSOC_IMMINENT /
// _BSS_TERMINATION_INCLUDED / _ESS_DISASSOC_IMMINENT). The decision rules themselves are the
// module's own, so those cases are PROPERTIES.
//
// test_a_btm_request_carries_its_preferred_candidate_past_the_optional_fields is the load-bearing
// case: the Preferred Candidate List does not sit at a fixed offset. Request Mode bit 3 inserts a
// 12-octet BSS Termination Duration subelement and bit 4 a length-prefixed Session Information URL
// ahead of it, so a decoder that reads the candidate at the fixed offset picks up whichever octets
// the optional fields left there and hands the supplicant a BSSID that was never named.

#include "network_drivers/datalink/roaming.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static const uint8_t SERVING[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t AP_A[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xA1};
static const uint8_t AP_B[6] = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xB2};

// Neighbor Report element (IEEE 802.11 sec 9.4.2.36): Element ID | Length | BSSID(6) |
// BSSID Information(4) | Operating Class(1) | Channel Number(1) | PHY Type(1). Body is 13 octets,
// so Channel Number sits 11 into the body.
static size_t put_neighbor_report(uint8_t *p, const uint8_t *bssid, uint8_t channel)
{
    p[0] = PROTOCORE_ROAM_NR_ELEM_ID;
    p[1] = 13;
    memcpy(&p[2], bssid, 6);
    p[8] = 0x00; // BSSID Information
    p[9] = 0x00;
    p[10] = 0x00;
    p[11] = 0x00;
    p[12] = 115; // Operating Class
    p[13] = channel;
    p[14] = 4; // PHY Type
    return 15;
}

// BTM Request fixed fields: Category | Action | Dialog Token | Request Mode |
// Disassociation Timer(2) | Validity Interval(1).
static size_t put_btm_head(uint8_t *p, uint8_t mode)
{
    p[0] = PROTOCORE_ROAM_WNM_CATEGORY;
    p[1] = PROTOCORE_ROAM_BTM_REQ_ACTION;
    p[2] = 0x2A; // Dialog Token
    p[3] = mode;
    p[4] = 0x0A; // Disassociation Timer, 2 octets
    p[5] = 0x00;
    p[6] = 0xFF; // Validity Interval
    return 7;
}

static void parse_nr(const uint8_t *elems, size_t len, protocore_roam_neighbor *out, uint8_t max)
{
    Roam.nr.elems = elems;
    Roam.nr.len = len;
    Roam.nr.out = out;
    Roam.nr.max = max;
    Roam.parse_neighbor_report(Roam.internal);
}

static void parse_btm(const uint8_t *frame, size_t len)
{
    Roam.btm.frame = frame;
    Roam.btm.len = len;
    Roam.parse_btm_request(Roam.internal);
}

static void decide(const uint8_t *serving, int8_t rssi, const protocore_roam_neighbor *list, uint8_t n,
                   const protocore_roam_btm *req, const protocore_roam_policy *pol)
{
    Roam.link.bssid = serving;
    Roam.link.rssi_dbm = rssi;
    Roam.cand.list = list;
    Roam.cand.n = n;
    Roam.rules.request = req;
    Roam.rules.policy = pol;
    Roam.decide(Roam.internal);
}

// Element ID 52 yields BSSID and Channel Number. The report carries no signal reading, so every
// candidate comes back at the sentinel for the caller to fill.
void test_a_neighbor_report_yields_bssid_and_channel(void)
{
    uint8_t elems[64];
    size_t n = put_neighbor_report(elems, AP_A, 6);
    n += put_neighbor_report(elems + n, AP_B, 36);

    protocore_roam_neighbor out[4];
    memset(out, 0, sizeof out);
    parse_nr(elems, n, out, 4);

    TEST_ASSERT_EQUAL_UINT8(2, Roam.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, out[0].bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(6, out[0].channel);
    TEST_ASSERT_EQUAL_INT8(PROTOCORE_ROAM_RSSI_UNKNOWN, out[0].rssi_dbm);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_B, out[1].bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(36, out[1].channel);
    TEST_ASSERT_EQUAL_INT8(PROTOCORE_ROAM_RSSI_UNKNOWN, out[1].rssi_dbm);
}

// An element list carries whatever the AP put in it. Every other Element ID is stepped over by its
// own Length, so a candidate after one is still found at the right offset.
void test_other_element_ids_are_stepped_over(void)
{
    uint8_t elems[64];
    size_t n = 0;
    elems[n++] = 7; // Country, an element this decoder has no interest in
    elems[n++] = 6;
    memset(&elems[n], 0x55, 6);
    n += 6;
    n += put_neighbor_report(elems + n, AP_A, 11);
    elems[n++] = 0; // SSID, zero length
    elems[n++] = 0;
    n += put_neighbor_report(elems + n, AP_B, 149);

    protocore_roam_neighbor out[4];
    memset(out, 0, sizeof out);
    parse_nr(elems, n, out, 4);

    TEST_ASSERT_EQUAL_UINT8(2, Roam.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, out[0].bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(11, out[0].channel);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_B, out[1].bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(149, out[1].channel);
}

// A Length that runs past the octets present ends the walk, keeping whatever was complete. A body
// shorter than the 13 octets the element is defined to carry is not a candidate at all.
void test_a_truncated_or_short_element_yields_no_candidate(void)
{
    uint8_t elems[64];
    protocore_roam_neighbor out[4];

    size_t n = put_neighbor_report(elems, AP_A, 6);
    n += put_neighbor_report(elems + n, AP_B, 36);
    memset(out, 0, sizeof out);
    parse_nr(elems, n - 1, out, 4); // one octet short of the second element
    TEST_ASSERT_EQUAL_UINT8(1, Roam.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, out[0].bssid, 6);

    // Element 52 with a 12-octet body: one short of BSSID + BSSID Information + Operating Class +
    // Channel Number + PHY Type, so the Channel Number it would report is not there.
    elems[0] = PROTOCORE_ROAM_NR_ELEM_ID;
    elems[1] = 12;
    memcpy(&elems[2], AP_A, 6);
    memset(&elems[8], 0, 6);
    memset(out, 0, sizeof out);
    parse_nr(elems, 14, out, 4);
    TEST_ASSERT_EQUAL_UINT8(0, Roam.n);

    // An element header with nothing behind it, and an empty list.
    memset(out, 0, sizeof out);
    parse_nr(elems, 1, out, 4);
    TEST_ASSERT_EQUAL_UINT8(0, Roam.n);
    parse_nr(elems, 0, out, 4);
    TEST_ASSERT_EQUAL_UINT8(0, Roam.n);
    parse_nr(NULL, 32, out, 4);
    TEST_ASSERT_EQUAL_UINT8(0, Roam.n);
}

// The walk stops at the caller's bound, so a report longer than the array cannot write past it.
void test_the_decode_stops_at_the_output_bound(void)
{
    uint8_t elems[64];
    size_t n = put_neighbor_report(elems, AP_A, 1);
    n += put_neighbor_report(elems + n, AP_B, 6);
    n += put_neighbor_report(elems + n, SERVING, 11);

    protocore_roam_neighbor out[3];
    memset(out, 0, sizeof out);
    parse_nr(elems, n, out, 1);
    TEST_ASSERT_EQUAL_UINT8(1, Roam.n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, out[0].bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(0, out[1].bssid[0]); // the second entry was never written

    parse_nr(elems, n, out, 0);
    TEST_ASSERT_EQUAL_UINT8(0, Roam.n);
}

// Request Mode bit 2 is Disassociation Imminent. The seven fixed octets are a complete request on
// their own, with no candidate list behind them.
void test_a_btm_request_decodes_its_request_mode(void)
{
    uint8_t frame[32];
    size_t n = put_btm_head(frame, PROTOCORE_ROAM_BTM_DISASSOC);
    parse_btm(frame, n);
    TEST_ASSERT_TRUE(Roam.ok);
    TEST_ASSERT_TRUE(Roam.hint.present);
    TEST_ASSERT_TRUE(Roam.hint.disassoc_imminent);
    TEST_ASSERT_FALSE(Roam.hint.has_preferred);

    // Bit 1 (Abridged) is not bit 2, so it does not read as an imminent disassociation.
    n = put_btm_head(frame, 0x02u);
    parse_btm(frame, n);
    TEST_ASSERT_TRUE(Roam.ok);
    TEST_ASSERT_TRUE(Roam.hint.present);
    TEST_ASSERT_FALSE(Roam.hint.disassoc_imminent);
}

// Anything that is not a WNM BSS Transition Management Request is refused, and the hint is cleared
// rather than left holding the previous frame's answer.
void test_a_frame_that_is_not_a_btm_request_is_refused(void)
{
    uint8_t frame[32];
    size_t n = put_btm_head(frame, PROTOCORE_ROAM_BTM_DISASSOC);

    parse_btm(frame, n);
    TEST_ASSERT_TRUE(Roam.ok); // a good one first, so the clearing below is observable

    frame[0] = 0x0B; // not WNM
    parse_btm(frame, n);
    TEST_ASSERT_FALSE(Roam.ok);
    TEST_ASSERT_FALSE(Roam.hint.present);
    TEST_ASSERT_FALSE(Roam.hint.disassoc_imminent);

    frame[0] = PROTOCORE_ROAM_WNM_CATEGORY;
    frame[1] = 0x08; // BSS Transition Management Response, not Request
    parse_btm(frame, n);
    TEST_ASSERT_FALSE(Roam.ok);

    (void)put_btm_head(frame, PROTOCORE_ROAM_BTM_DISASSOC);
    parse_btm(frame, 6); // one octet short of the fixed fields
    TEST_ASSERT_FALSE(Roam.ok);
    parse_btm(NULL, 7);
    TEST_ASSERT_FALSE(Roam.ok);
}

// The Preferred Candidate List sits behind whichever optional fields Request Mode announced: a
// 12-octet BSS Termination Duration subelement for bit 3, and a length-prefixed Session Information
// URL for bit 4. All four combinations must land on the same BSSID.
void test_a_btm_request_carries_its_preferred_candidate_past_the_optional_fields(void)
{
    static const char URL[] = "https://example.test/s";
    for (unsigned combo = 0; combo < 4; combo++)
    {
        uint8_t frame[128];
        uint8_t mode = PROTOCORE_ROAM_BTM_PREF_LIST;
        if (combo & 1u)
        {
            mode |= PROTOCORE_ROAM_BTM_TERM_INCL;
        }
        if (combo & 2u)
        {
            mode |= PROTOCORE_ROAM_BTM_ESS_DISASSOC;
        }
        size_t n = put_btm_head(frame, mode);
        if (combo & 1u)
        {
            // BSS Termination Duration subelement: Subelement ID 4 | Length 10 | TSF(8) | Duration(2).
            frame[n++] = 4;
            frame[n++] = 10;
            memset(&frame[n], 0x77, 10);
            n += 10;
        }
        if (combo & 2u)
        {
            frame[n++] = (uint8_t)(sizeof URL - 1);
            memcpy(&frame[n], URL, sizeof URL - 1);
            n += sizeof URL - 1;
        }
        n += put_neighbor_report(frame + n, AP_B, 44);
        n += put_neighbor_report(frame + n, AP_A, 6); // a lower-preference entry behind it

        parse_btm(frame, n);
        TEST_ASSERT_TRUE(Roam.ok);
        TEST_ASSERT_TRUE(Roam.hint.present);
        TEST_ASSERT_TRUE(Roam.hint.has_preferred);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_B, Roam.hint.preferred_bssid, 6);
    }

    // Without bit 0 the same octets are not a Preferred Candidate List.
    uint8_t plain[128];
    size_t m = put_btm_head(plain, 0x00u);
    m += put_neighbor_report(plain + m, AP_B, 44);
    parse_btm(plain, m);
    TEST_ASSERT_TRUE(Roam.ok);
    TEST_ASSERT_FALSE(Roam.hint.has_preferred);
}

// Rule 1: Disassociation Imminent transitions whatever the signal says, to the preferred candidate
// when the list holds it and to the strongest otherwise. With nothing to move to it stays.
void test_disassociation_imminent_overrides_the_signal(void)
{
    protocore_roam_neighbor nb[2];
    memset(nb, 0, sizeof nb);
    memcpy(nb[0].bssid, AP_A, 6);
    nb[0].channel = 6;
    nb[0].rssi_dbm = -80;
    memcpy(nb[1].bssid, AP_B, 6);
    nb[1].channel = 44;
    nb[1].rssi_dbm = -60;

    protocore_roam_btm req;
    memset(&req, 0, sizeof req);
    req.present = PROTO_TRUE;
    req.disassoc_imminent = PROTO_TRUE;
    req.has_preferred = PROTO_TRUE;
    memcpy(req.preferred_bssid, AP_A, 6);

    // The link is strong (-40) and the preferred candidate is the weaker of the two: it still wins.
    decide(SERVING, -40, nb, 2, &req, NULL);
    TEST_ASSERT_TRUE(Roam.decision.roam);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_ROAM_BTM_IMMINENT, Roam.decision.reason);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, Roam.decision.target_bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(6, Roam.decision.target_channel);

    // No preferred candidate named: the strongest in the list.
    req.has_preferred = PROTO_FALSE;
    decide(SERVING, -40, nb, 2, &req, NULL);
    TEST_ASSERT_TRUE(Roam.decision.roam);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_ROAM_BTM_IMMINENT, Roam.decision.reason);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_B, Roam.decision.target_bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(44, Roam.decision.target_channel);

    // An empty candidate list leaves nowhere to go, so it stays rather than naming a target.
    decide(SERVING, -40, nb, 0, &req, NULL);
    TEST_ASSERT_FALSE(Roam.decision.roam);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_ROAM_NONE, Roam.decision.reason);
}

// Rule 2: a suggested candidate is taken only while it is no weaker than the serving BSS - a
// suggestion is not a reason to move to a worse radio.
void test_a_suggested_candidate_is_taken_only_when_it_is_no_weaker(void)
{
    protocore_roam_neighbor nb[1];
    memset(nb, 0, sizeof nb);
    memcpy(nb[0].bssid, AP_A, 6);
    nb[0].channel = 6;

    protocore_roam_btm req;
    memset(&req, 0, sizeof req);
    req.present = PROTO_TRUE;
    req.has_preferred = PROTO_TRUE;
    memcpy(req.preferred_bssid, AP_A, 6);

    nb[0].rssi_dbm = -50; // equal to the serving BSS: "not weaker" includes equal
    decide(SERVING, -50, nb, 1, &req, NULL);
    TEST_ASSERT_TRUE(Roam.decision.roam);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_ROAM_BTM_SUGGESTED, Roam.decision.reason);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, Roam.decision.target_bssid, 6);

    nb[0].rssi_dbm = -51; // one dB weaker: the suggestion is declined
    decide(SERVING, -50, nb, 1, &req, NULL);
    TEST_ASSERT_FALSE(Roam.decision.roam);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_ROAM_NONE, Roam.decision.reason);

    // A preferred BSSID that is not in the candidate list names nothing to move to.
    memcpy(req.preferred_bssid, AP_B, 6);
    nb[0].rssi_dbm = -50;
    decide(SERVING, -50, nb, 1, &req, NULL);
    TEST_ASSERT_FALSE(Roam.decision.roam);
}

// Rule 3: the signal-driven transition needs BOTH the serving BSS at or below the threshold and a
// candidate beating it by the hysteresis margin. Each boundary is checked from either side.
void test_the_signal_transition_needs_the_threshold_and_the_margin(void)
{
    static const protocore_roam_policy POLICY = {-70, 8};
    protocore_roam_neighbor nb[1];
    memset(nb, 0, sizeof nb);
    memcpy(nb[0].bssid, AP_A, 6);
    nb[0].channel = 36;

    // Serving one dB above the threshold: no transition however strong the candidate.
    nb[0].rssi_dbm = -30;
    decide(SERVING, -69, nb, 1, NULL, &POLICY);
    TEST_ASSERT_FALSE(Roam.decision.roam);

    // At the threshold, with exactly the margin: -70 + 8 = -62.
    nb[0].rssi_dbm = -62;
    decide(SERVING, -70, nb, 1, NULL, &POLICY);
    TEST_ASSERT_TRUE(Roam.decision.roam);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_ROAM_LOW_RSSI, Roam.decision.reason);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, Roam.decision.target_bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(36, Roam.decision.target_channel);

    // One dB inside the margin: stay.
    nb[0].rssi_dbm = -63;
    decide(SERVING, -70, nb, 1, NULL, &POLICY);
    TEST_ASSERT_FALSE(Roam.decision.roam);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_ROAM_NONE, Roam.decision.reason);
}

// A null policy takes the built-in thresholds: -75 dBm and an 8 dB margin. -75 + 8 = -67 is the
// weakest candidate that clears them.
void test_a_null_policy_takes_the_built_in_thresholds(void)
{
    protocore_roam_neighbor nb[1];
    memset(nb, 0, sizeof nb);
    memcpy(nb[0].bssid, AP_A, 6);
    nb[0].channel = 1;

    nb[0].rssi_dbm = -67;
    decide(SERVING, -75, nb, 1, NULL, NULL);
    TEST_ASSERT_TRUE(Roam.decision.roam);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_ROAM_LOW_RSSI, Roam.decision.reason);

    nb[0].rssi_dbm = -68;
    decide(SERVING, -75, nb, 1, NULL, NULL);
    TEST_ASSERT_FALSE(Roam.decision.roam);

    nb[0].rssi_dbm = -67;
    decide(SERVING, -74, nb, 1, NULL, NULL); // one dB above the default threshold
    TEST_ASSERT_FALSE(Roam.decision.roam);
}

// The BSS we are already on is never a target, so a candidate list that includes it cannot produce
// a transition to where we already are.
void test_the_serving_bss_is_never_the_target(void)
{
    protocore_roam_neighbor nb[2];
    memset(nb, 0, sizeof nb);
    memcpy(nb[0].bssid, SERVING, 6); // the strongest entry is the one we are on
    nb[0].channel = 1;
    nb[0].rssi_dbm = -20;
    memcpy(nb[1].bssid, AP_A, 6);
    nb[1].channel = 6;
    nb[1].rssi_dbm = -60;

    decide(SERVING, -80, nb, 2, NULL, NULL);
    TEST_ASSERT_TRUE(Roam.decision.roam);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(AP_A, Roam.decision.target_bssid, 6);

    // With only ourselves in the list there is nothing to move to.
    decide(SERVING, -80, nb, 1, NULL, NULL);
    TEST_ASSERT_FALSE(Roam.decision.roam);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_ROAM_NONE, Roam.decision.reason);
}

// With no serving BSSID there is nothing to measure against, and the verdict is cleared rather than
// left holding the previous call's target.
void test_no_serving_bssid_stays_and_clears_the_verdict(void)
{
    static const uint8_t ZERO[6] = {0, 0, 0, 0, 0, 0};
    protocore_roam_neighbor nb[1];
    memset(nb, 0, sizeof nb);
    memcpy(nb[0].bssid, AP_A, 6);
    nb[0].channel = 6;
    nb[0].rssi_dbm = -40;

    decide(SERVING, -90, nb, 1, NULL, NULL);
    TEST_ASSERT_TRUE(Roam.decision.roam); // a transition first, so the clearing below is observable

    decide(NULL, -90, nb, 1, NULL, NULL);
    TEST_ASSERT_FALSE(Roam.decision.roam);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_ROAM_NONE, Roam.decision.reason);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ZERO, Roam.decision.target_bssid, 6);
    TEST_ASSERT_EQUAL_UINT8(0, Roam.decision.target_channel);

    // A candidate count with no list behind it is the same refusal.
    decide(SERVING, -90, NULL, 1, NULL, NULL);
    TEST_ASSERT_FALSE(Roam.decision.roam);
}
