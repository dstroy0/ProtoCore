// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file roaming.h
 * @brief Wi-Fi roaming decision layer (PC_ENABLE_ROAMING) - the policy that picks a roam target.
 *
 * The three 802.11 roaming primitives are supplicant / hardware territory: 802.11k surfaces a neighbor
 * report (candidate APs), 802.11v delivers a BSS-Transition-Management hint from the network, and 802.11r
 * does the fast transition. This module is the piece between them: the pure, deterministic policy that
 * fuses the current link's RSSI, a candidate neighbor list, and an optional BTM hint into a decision -
 * roam or stay, and to which AP and why. It holds no state and touches no radio, so it is fully
 * host-testable with synthetic inputs; the caller feeds it real data and executes the transition.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ROAMING_H
#define PROTOCORE_ROAMING_H

#include "protocore_config.h"

#if PC_ENABLE_ROAMING

/** @brief A candidate access point (from an 802.11k neighbor report or a scan). */
typedef struct
{
    uint8_t bssid[6]; ///< the AP's BSSID
    uint8_t channel;  ///< operating channel
    int8_t rssi_dbm;  ///< measured signal strength (dBm; more negative is weaker)
} pc_roam_neighbor;

/** @brief An 802.11v BSS-Transition-Management hint from the network. */
typedef struct
{
    proto_bool present;           ///< a BTM request was received this cycle
    proto_bool disassoc_imminent; ///< the AP will disassociate us shortly (we must leave)
    proto_bool has_preferred;     ///< @ref preferred_bssid names a specific target
    uint8_t preferred_bssid[6];
} pc_roam_btm;

/** @brief Roaming policy thresholds (caller-supplied, so no global tuning knob). */
typedef struct
{
    int8_t roam_rssi_threshold_dbm; ///< only consider an RSSI-driven roam when the link is at/below this
    uint8_t hysteresis_db;          ///< a candidate must beat the current link by this margin to be worth it
} pc_roam_policy;

/** @brief Why the decision was made. */
typedef enum
{
    PC_ROAM_NONE = 0,      ///< stay on the current AP
    PC_ROAM_BTM_IMMINENT,  ///< forced off by a disassociation-imminent BTM request
    PC_ROAM_BTM_SUGGESTED, ///< the network steered us (BTM) to a preferred, no-weaker AP
    PC_ROAM_LOW_RSSI,      ///< the link is weak and a candidate is clearly stronger
} pc_roam_reason;

/** @brief The roaming decision. */
typedef struct
{
    proto_bool roam;         ///< true to transition to @ref target_bssid
    uint8_t target_bssid[6]; ///< the AP to roam to (valid only when @ref roam)
    uint8_t target_channel;  ///< that AP's channel
    pc_roam_reason reason;   ///< why (see @ref pc_roam_reason)
} pc_roam_decision;

/**
 * @brief Decide whether and where to roam (pure, stateless).
 *
 * Priority order: a disassociation-imminent BTM forces a roam (to the preferred candidate if it is in the
 * list, else the strongest); a non-imminent BTM with a preferred, no-weaker candidate is honoured next;
 * otherwise, when the current RSSI is at/below the policy threshold and the strongest candidate beats it
 * by at least the hysteresis margin, roam to that candidate. The current AP is never chosen as a target.
 *
 * @param current_bssid    the BSSID we are associated with (excluded from the candidates).
 * @param current_rssi_dbm the current link's RSSI.
 * @param neighbors        candidate APs (@p n of them).
 * @param btm              an optional BTM hint (may be null).
 * @param policy           the thresholds (may be null -> a conservative default is used).
 * @param out              receives the decision (never null).
 */
void pc_roam_decide(const uint8_t current_bssid[6], int8_t current_rssi_dbm, const pc_roam_neighbor *neighbors,
                    uint8_t n, const pc_roam_btm *btm, const pc_roam_policy *policy, pc_roam_decision *out);

/** @brief 802.11 Neighbor Report element id (IEEE 802.11 §9.4.2.36). */
#define PC_ROAM_NR_ELEM_ID 52
/** @brief Sentinel RSSI meaning "not yet measured" - the caller fills it after scanning the candidate. */
#define PC_ROAM_RSSI_UNKNOWN ((int8_t)-128)

/**
 * @brief Parse a sequence of 802.11k Neighbor Report elements into candidate APs.
 *
 * @p elems is the element list an 802.11k Radio Measurement Neighbor Report Response action frame carries
 * (the caller strips the action header first). Each Neighbor Report element (id 52) supplies a candidate's
 * BSSID and operating channel; other element ids are skipped. The report does not carry RSSI, so each
 * candidate's @c rssi_dbm is set to @ref PC_ROAM_RSSI_UNKNOWN for the caller to fill after measuring, then
 * feed the list to @ref pc_roam_decide.
 * @param out receives up to @p max candidates.
 * @return the number of candidates parsed (0..@p max).
 */
uint8_t pc_roam_parse_neighbor_report(const uint8_t *elems, size_t len, pc_roam_neighbor *out, uint8_t max);

// 802.11v BSS Transition Management Request (WNM action frame).
#define PC_ROAM_WNM_CATEGORY 0x0A     ///< WNM action category
#define PC_ROAM_BTM_REQ_ACTION 0x07   ///< BSS Transition Management Request action code
#define PC_ROAM_BTM_PREF_LIST 0x01u   ///< Request Mode bit 0: a preferred candidate list is included
#define PC_ROAM_BTM_DISASSOC 0x04u    ///< Request Mode bit 2: disassociation imminent
#define PC_ROAM_BTM_TERM_INCL 0x08u   ///< Request Mode bit 3: BSS Termination Duration is included
#define PC_ROAM_BTM_ESS_DISASSOC 0x10 ///< Request Mode bit 4: an ESS-disassoc Session Info URL is included

/**
 * @brief Parse an 802.11v BSS Transition Management Request action frame into a @ref pc_roam_btm hint.
 *
 * @p frame starts at the action-frame Category octet (WNM category 0x0A, BTM-Request action 0x07). The
 * Request Mode flags set @c disassoc_imminent (bit 2); when the preferred-candidate-list bit (bit 0) is
 * set, the highest-preference candidate's BSSID (the first Neighbor Report element in the list, decoded
 * past the optional BSS Termination Duration and Session Information URL) becomes @c preferred_bssid. The
 * result feeds @ref pc_roam_decide.
 * @return true iff @p frame is a well-formed BTM Request; false otherwise (@p out is cleared).
 */
proto_bool pc_roam_parse_btm_request(const uint8_t *frame, size_t len, pc_roam_btm *out);

#endif // PC_ENABLE_ROAMING
#endif // PROTOCORE_ROAMING_H
