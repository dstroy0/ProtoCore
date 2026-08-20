// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file roaming.c
 * @brief Layer 2 (Data Link) - the roam decision and the two IEEE 802.11 decodes it feeds on - see roaming.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_ROAMING

#include "mmgr/protomem/protomem.h" // mem.cmp / mem.cpy / mem.zero
#include "network_drivers/datalink/roaming/roaming.h"

PROTOCORE_BEGIN_DECLS

/** @brief A BSSID is six octets, and every compare and copy below moves exactly that. */
#define PROTOCORE_ROAM_BSSID_LEN 6u

/** @brief Fixed fields of a BTM Request: Category | Action | Dialog Token | Request Mode |
 *  Disassociation Timer(2) | Validity Interval(1). */
#define PROTOCORE_ROAM_BTM_FIXED_LEN 7u

/** @brief BSS Termination Duration subelement length in octets. */
#define PROTOCORE_ROAM_BTM_TERM_LEN 12u

/** @brief Neighbor Report element body: BSSID(6) | BSSID Information(4) | Operating Class(1) |
 *  Channel Number(1) | PHY Type(1), then optional subelements. */
#define PROTOCORE_ROAM_NR_BODY_MIN 13u

/** @brief Offset of Channel Number inside a Neighbor Report element body. */
#define PROTOCORE_ROAM_NR_CHANNEL_OFF 11u

static proto_bool mac_eq(const uint8_t *a, const uint8_t *b)
{
    return mem.cmp(a, b, PROTOCORE_ROAM_BSSID_LEN) == 0;
}

// Index of the strongest candidate that is not the serving BSS, or -1 if there is none.
static int best_other(const uint8_t *current, const protocore_roam_neighbor *nb, uint8_t n)
{
    int best = -1;
    for (uint8_t i = 0; i < n; i++)
    {
        if (mac_eq(nb[i].bssid, current))
        {
            continue;
        }
        if (best < 0 || nb[i].rssi_dbm > nb[best].rssi_dbm)
        {
            best = (int)i;
        }
    }
    return best;
}

// Index of the candidate whose BSSID equals target, or -1.
static int find_bssid(const uint8_t *target, const protocore_roam_neighbor *nb, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
    {
        if (mac_eq(nb[i].bssid, target))
        {
            return (int)i;
        }
    }
    return -1;
}

// Write candidate idx into the decision with the reason that chose it.
static void pick(uint8_t *restrict work, int idx, protocore_roam_reason reason)
{
    (void)work;
    const protocore_roam_neighbor *nb = RoamV.cand.list;

    RoamV.decision.roam = PROTO_TRUE;
    mem.cpy(RoamV.decision.target_bssid, nb[idx].bssid, PROTOCORE_ROAM_BSSID_LEN);
    RoamV.decision.target_channel = nb[idx].channel;
    RoamV.decision.reason = reason;
}

void protocore_roam_decide(uint8_t *restrict work)
{
    const uint8_t *serving = RoamV.link.bssid;
    const int8_t serving_rssi = RoamV.link.rssi_dbm;
    const protocore_roam_neighbor *nb = RoamV.cand.list;
    const uint8_t n = RoamV.cand.n;
    const protocore_roam_btm *btm = RoamV.rules.request;
    const protocore_roam_policy *policy = RoamV.rules.policy;

    RoamV.decision.roam = PROTO_FALSE;
    mem.zero(RoamV.decision.target_bssid, PROTOCORE_ROAM_BSSID_LEN);
    RoamV.decision.target_channel = 0;
    RoamV.decision.reason = PROTOCORE_ROAM_NONE;
    if (!serving || (n && !nb))
    {
        return;
    }

    // The thresholds a null policy takes.
    const int8_t threshold = policy ? policy->roam_rssi_threshold_dbm : (int8_t)-75;
    const uint8_t hysteresis = policy ? policy->hysteresis_db : (uint8_t)8;

    const int best = best_other(serving, nb, n);

    // 1. Request Mode bit 2 (Disassociation Imminent): transition to the preferred candidate when the
    //    list holds it, else to the strongest. An empty candidate list stays.
    if (btm && btm->present && btm->disassoc_imminent)
    {
        int target = -1;
        if (btm->has_preferred)
        {
            target = find_bssid(btm->preferred_bssid, nb, n);
        }
        if (target < 0)
        {
            target = best;
        }
        if (target >= 0)
        {
            pick(work, target, PROTOCORE_ROAM_BTM_IMMINENT);
        }
        return;
    }

    // 2. A BTM Request naming a Preferred Candidate: transition when that candidate is in the list and
    //    is not weaker than the serving BSS.
    if (btm && btm->present && btm->has_preferred)
    {
        const int target = find_bssid(btm->preferred_bssid, nb, n);
        if (target >= 0 && nb[target].rssi_dbm >= serving_rssi)
        {
            pick(work, target, PROTOCORE_ROAM_BTM_SUGGESTED);
            return;
        }
    }

    // 3. Signal-driven: the serving BSS at or below the threshold, and the strongest candidate beating it
    //    by at least the hysteresis margin.
    if (best >= 0 && serving_rssi <= threshold && (int)nb[best].rssi_dbm >= (int)serving_rssi + (int)hysteresis)
    {
        pick(work, best, PROTOCORE_ROAM_LOW_RSSI);
        return;
    }

    // 4. Otherwise stay put.
}

void protocore_roam_parse_neighbor_report(uint8_t *restrict work)
{
    const uint8_t *elems = RoamV.nr.elems;
    const size_t len = RoamV.nr.len;
    protocore_roam_neighbor *out = RoamV.nr.out;
    const uint8_t max = RoamV.nr.max;

    RoamV.n = 0;
    if (!elems || !out)
    {
        return;
    }
    uint8_t count = 0;
    size_t off = 0;
    while (off + 2 <= len && count < max) // element header: Element ID + Length
    {
        const uint8_t id = elems[off];
        const uint8_t elen = elems[off + 1];
        if (off + 2 + elen > len) // a truncated element ends the walk
        {
            break;
        }
        if (id == PROTOCORE_ROAM_NR_ELEM_ID && elen >= PROTOCORE_ROAM_NR_BODY_MIN)
        {
            const uint8_t *body = elems + off + 2;
            mem.cpy(out[count].bssid, body, PROTOCORE_ROAM_BSSID_LEN);
            out[count].channel = body[PROTOCORE_ROAM_NR_CHANNEL_OFF];
            out[count].rssi_dbm = PROTOCORE_ROAM_RSSI_UNKNOWN;
            count++;
        }
        off += (size_t)2 + elen; // step over this element, matched or not
    }
    RoamV.n = count;
}

void protocore_roam_parse_btm_request(uint8_t *restrict work)
{
    const uint8_t *frame = RoamV.btm.frame;
    const size_t len = RoamV.btm.len;
    protocore_roam_btm *out = &RoamV.hint;

    RoamV.ok = PROTO_FALSE;
    mem.zero(out, sizeof(*out));
    if (!frame || len < PROTOCORE_ROAM_BTM_FIXED_LEN || frame[0] != PROTOCORE_ROAM_WNM_CATEGORY ||
        frame[1] != PROTOCORE_ROAM_BTM_REQ_ACTION)
    {
        return;
    }
    const uint8_t mode = frame[3]; // Request Mode
    out->present = PROTO_TRUE;
    out->disassoc_imminent = (mode & PROTOCORE_ROAM_BTM_DISASSOC) != 0;
    RoamV.ok = PROTO_TRUE;

    // Step past the optional fields to the BSS Transition Candidate List Entries.
    size_t off = PROTOCORE_ROAM_BTM_FIXED_LEN;
    if (mode & PROTOCORE_ROAM_BTM_TERM_INCL) // BSS Termination Duration
    {
        off += PROTOCORE_ROAM_BTM_TERM_LEN;
    }
    if (mode & PROTOCORE_ROAM_BTM_ESS_DISASSOC) // Session Information URL: 1-octet length, then the URL
    {
        if (off >= len)
        {
            return; // a truncated tail leaves the Request Mode flags above standing
        }
        off += (size_t)1 + frame[off];
    }
    // The first Neighbor Report element of the Preferred Candidate List is the highest-preference target.
    if ((mode & PROTOCORE_ROAM_BTM_PREF_LIST) && off + 2 + PROTOCORE_ROAM_BSSID_LEN <= len &&
        frame[off] == PROTOCORE_ROAM_NR_ELEM_ID && frame[off + 1] >= PROTOCORE_ROAM_NR_BODY_MIN)
    {
        mem.cpy(out->preferred_bssid, frame + off + 2, PROTOCORE_ROAM_BSSID_LEN);
        out->has_preferred = PROTO_TRUE;
    }
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
RoamVars RoamV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ROAMING
