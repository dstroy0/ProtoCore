// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file roaming.c
 * @brief Wi-Fi roaming decision layer - see roaming.h.
 */

#include "network_drivers/datalink/roaming.h"
#include "mmgr/protomem.h" // mem.cmp / mem.cpy / mem.zero

#if PC_ENABLE_ROAMING

/** @brief A BSSID is six octets, and every compare and copy below moves exactly that. */
#define PC_ROAM_BSSID_LEN 6u

static proto_bool mac_eq(const uint8_t *a, const uint8_t *b)
{
    return mem.cmp(a, b, PC_ROAM_BSSID_LEN) == 0;
}

// Index of the strongest candidate that is not the current AP, or -1 if there is none.
static int best_other(const uint8_t *current, const pc_roam_neighbor *nb, uint8_t n)
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
static int find_bssid(const uint8_t *target, const pc_roam_neighbor *nb, uint8_t n)
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

static void pick(pc_roam_decision *out, const pc_roam_neighbor *nb, int idx, pc_roam_reason reason)
{
    out->roam = PROTO_TRUE;
    mem.cpy(out->target_bssid, nb[idx].bssid, PC_ROAM_BSSID_LEN);
    out->target_channel = nb[idx].channel;
    out->reason = reason;
}

static void decide(const uint8_t current_bssid[6], int8_t current_rssi_dbm, const pc_roam_neighbor *neighbors,
                   uint8_t n, const pc_roam_btm *btm, const pc_roam_policy *policy, pc_roam_decision *out)
{
    if (!out)
    {
        return;
    }
    out->roam = PROTO_FALSE;
    mem.zero(out->target_bssid, PC_ROAM_BSSID_LEN);
    out->target_channel = 0;
    out->reason = PC_ROAM_NONE;
    if (!current_bssid || (n && !neighbors))
    {
        return;
    }

    // A conservative default when the caller passes no policy.
    int8_t threshold = policy ? policy->roam_rssi_threshold_dbm : (int8_t)-75;
    uint8_t hysteresis = policy ? policy->hysteresis_db : (uint8_t)8;

    int best = best_other(current_bssid, neighbors, n);

    // 1. A disassociation-imminent BTM forces us off: roam to the preferred candidate if present, else the
    //    strongest one. (If there is nowhere to go, we cannot roam.)
    if (btm && btm->present && btm->disassoc_imminent)
    {
        int target = -1;
        if (btm->has_preferred)
        {
            target = find_bssid(btm->preferred_bssid, neighbors, n);
        }
        if (target < 0)
        {
            target = best;
        }
        if (target >= 0)
        {
            pick(out, neighbors, target, PC_ROAM_BTM_IMMINENT);
        }
        return;
    }

    // 2. A non-imminent BTM steering hint: honour it when the preferred AP is a real candidate that is not
    //    weaker than the current link (do not chase the network into a worse AP).
    if (btm && btm->present && btm->has_preferred)
    {
        int target = find_bssid(btm->preferred_bssid, neighbors, n);
        if (target >= 0 && neighbors[target].rssi_dbm >= current_rssi_dbm)
        {
            pick(out, neighbors, target, PC_ROAM_BTM_SUGGESTED);
            return;
        }
    }

    // 3. RSSI-driven: only when the current link is at/below the threshold and the strongest candidate beats
    //    it by at least the hysteresis margin (so we do not ping-pong on a marginal difference).
    if (best >= 0 && current_rssi_dbm <= threshold &&
        (int)neighbors[best].rssi_dbm >= (int)current_rssi_dbm + (int)hysteresis)
    {
        pick(out, neighbors, best, PC_ROAM_LOW_RSSI);
        return;
    }

    // 4. Otherwise stay put.
}

static uint8_t parse_neighbor_report(const uint8_t *elems, size_t len, pc_roam_neighbor *out, uint8_t max)
{
    if (!elems || !out)
    {
        return 0;
    }
    // Each Neighbor Report element (id 52) carries: BSSID(6) | BSSID Info(4) | Operating Class(1) |
    // Channel(1) | PHY Type(1) | optional subelements - so a fixed 13-octet body, plus any subelements.
    const uint8_t NR_BODY_MIN = 13;
    uint8_t count = 0;
    size_t off = 0;
    while (off + 2 <= len && count < max) // element header: id + length
    {
        uint8_t id = elems[off];
        uint8_t elen = elems[off + 1];
        if (off + 2 + elen > len) // a truncated element ends the walk
        {
            break;
        }
        if (id == PC_ROAM_NR_ELEM_ID && elen >= NR_BODY_MIN)
        {
            const uint8_t *b = elems + off + 2;
            mem.cpy(out[count].bssid, b, PC_ROAM_BSSID_LEN);
            out[count].channel = b[11]; // after BSSID(6) + BSSID Info(4) + Operating Class(1)
            out[count].rssi_dbm = PC_ROAM_RSSI_UNKNOWN;
            count++;
        }
        off += (size_t)2 + elen; // skip this element (and any non-52 / short one)
    }
    return count;
}

static proto_bool parse_btm_request(const uint8_t *frame, size_t len, pc_roam_btm *out)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    mem.zero(out, sizeof(*out));
    // Fixed fields: Category | Action | Dialog Token | Request Mode | Disassoc Timer(2) | Validity(1) = 7.
    if (!frame || len < 7 || frame[0] != PC_ROAM_WNM_CATEGORY || frame[1] != PC_ROAM_BTM_REQ_ACTION)
    {
        return PROTO_FALSE;
    }
    uint8_t mode = frame[3];
    out->present = PROTO_TRUE;
    out->disassoc_imminent = (mode & PC_ROAM_BTM_DISASSOC) != 0;

    // Walk past the optional fields to reach the candidate list.
    size_t off = 7;
    if (mode & PC_ROAM_BTM_TERM_INCL) // BSS Termination Duration is 12 octets
    {
        off += 12;
    }
    if (mode & PC_ROAM_BTM_ESS_DISASSOC) // Session Information URL: 1-octet length + URL
    {
        if (off >= len)
        {
            return PROTO_TRUE; // truncated optional tail; the flags above are still valid
        }
        off += (size_t)1 + frame[off];
    }
    // Preferred candidate list: the first Neighbor Report element (id 52) is the top-preference target.
    if ((mode & PC_ROAM_BTM_PREF_LIST) && off + 2 + 6 <= len && frame[off] == PC_ROAM_NR_ELEM_ID &&
        frame[off + 1] >= 13)
    {
        mem.cpy(out->preferred_bssid, frame + off + 2, PC_ROAM_BSSID_LEN);
        out->has_preferred = PROTO_TRUE;
    }
    return PROTO_TRUE;
}

const RoamNs Roam = {decide, parse_neighbor_report, parse_btm_request};

#endif // PC_ENABLE_ROAMING
