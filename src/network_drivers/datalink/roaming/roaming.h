// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file roaming.h
 * @brief Layer 2 (Data Link) - the roam decision: which BSS to transition to (PROTOCORE_ENABLE_ROAMING).
 *
 * IEEE Std 802.11 defines every field this module reads, not an IETF RFC: the Neighbor Report element
 * (Element ID 52, IEEE 802.11 sec 9.4.2.36) that Radio Resource Measurement (802.11k) carries, and the
 * BSS Transition Management Request frame (WNM Action category 10, action 7) that Wireless Network
 * Management (802.11v) carries. Fast BSS Transition (802.11r) performs the transition and belongs to the
 * supplicant and the radio.
 *
 * The module decodes those two, then fuses the serving BSS's signal strength, the BSS Transition
 * Candidate List, and the BTM Request into one verdict: transition or stay, to which BSSID, on which
 * Channel Number, and under which reason. It reads no radio and keeps nothing between calls, so it runs
 * on a host against synthetic input; the caller measures the link and performs the transition.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ROAMING_H
#define PROTOCORE_ROAMING_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_ROAMING

PROTOCORE_BEGIN_DECLS

/** @brief One BSS Transition Candidate: a Neighbor Report entry (IEEE 802.11 sec 9.4.2.36) or a scan row. */
typedef struct
{
    uint8_t bssid[6]; ///< the candidate's BSSID
    uint8_t channel;  ///< its Channel Number
    int8_t rssi_dbm;  ///< its signal strength (dBm; more negative is weaker)
} protocore_roam_neighbor;
/** @brief What a BSS Transition Management Request asks of us (IEEE 802.11 WNM, 802.11v). */
typedef struct
{
    proto_bool present;           ///< a BTM Request decoded this cycle
    proto_bool disassoc_imminent; ///< Request Mode bit 2: the BSS disassociates us shortly
    proto_bool has_preferred;     ///< a Preferred Candidate List named @ref preferred_bssid
    uint8_t preferred_bssid[6];   ///< the highest-preference candidate's BSSID
} protocore_roam_btm;
/** @brief The thresholds a decision applies, carried by the caller rather than a global. */
typedef struct
{
    int8_t roam_rssi_threshold_dbm; ///< a signal-driven transition needs the serving BSS at/below this
    uint8_t hysteresis_db;          ///< a candidate must beat the serving BSS by this margin
} protocore_roam_policy;
/** @brief Which rule produced the decision. */
typedef enum
{
    PROTOCORE_ROAM_NONE = 0,      ///< stay on the serving BSS
    PROTOCORE_ROAM_BTM_IMMINENT,  ///< Request Mode bit 2 (disassociation imminent) forced the transition
    PROTOCORE_ROAM_BTM_SUGGESTED, ///< a BTM Request named a preferred, no-weaker candidate
    PROTOCORE_ROAM_LOW_RSSI,      ///< the serving signal is at/below threshold and a candidate clears hysteresis
} protocore_roam_reason;
/** @brief The verdict: transition or stay, and to what. */
typedef struct
{
    proto_bool roam;              ///< true to transition to @ref target_bssid
    uint8_t target_bssid[6];      ///< the BSSID to transition to (valid only when @ref roam)
    uint8_t target_channel;       ///< that candidate's Channel Number
    protocore_roam_reason reason; ///< which rule produced it (see @ref protocore_roam_reason)
} protocore_roam_decision;
/** @brief Neighbor Report Element ID (IEEE 802.11 sec 9.4.2.36). */
#define PROTOCORE_ROAM_NR_ELEM_ID 52
/** @brief Sentinel RSSI: the candidate carries no signal reading yet, and the caller supplies one. */
#define PROTOCORE_ROAM_RSSI_UNKNOWN ((int8_t)-128)
// BSS Transition Management Request, a WNM Action frame (IEEE 802.11, 802.11v).
#define PROTOCORE_ROAM_WNM_CATEGORY 0x0A     ///< WNM Action category
#define PROTOCORE_ROAM_BTM_REQ_ACTION 0x07   ///< BSS Transition Management Request action code
#define PROTOCORE_ROAM_BTM_PREF_LIST 0x01u   ///< Request Mode bit 0: Preferred Candidate List Included
#define PROTOCORE_ROAM_BTM_DISASSOC 0x04u    ///< Request Mode bit 2: Disassociation Imminent
#define PROTOCORE_ROAM_BTM_TERM_INCL 0x08u   ///< Request Mode bit 3: BSS Termination Included
#define PROTOCORE_ROAM_BTM_ESS_DISASSOC 0x10 ///< Request Mode bit 4: ESS Disassociation Imminent
/** @brief The serving BSS a decision measures against (IEEE 802.11 association). */
typedef struct
{
    const uint8_t *bssid; ///< the BSSID we are associated with; never chosen as a target
    int8_t rssi_dbm;      ///< its signal strength (dBm)
} RoamLinkArgs;
/** @brief The BSS Transition Candidate List a decision picks from. Nothing a parse reads. */
typedef struct
{
    const protocore_roam_neighbor *list; ///< the candidates
    uint8_t n;                           ///< how many entries @ref list holds
} RoamCandArgs;
/** @brief What constrains the choice: the network's request and the local thresholds. */
typedef struct
{
    const protocore_roam_btm *request;   ///< the BTM Request to honor, or NULL for none
    const protocore_roam_policy *policy; ///< the thresholds to apply, or NULL for the built-in default
} RoamRuleArgs;
/** @brief A Neighbor Report element list to decode, and where its candidates land (802.11k). */
typedef struct
{
    const uint8_t *elems;         ///< the element list, action header already stripped
    size_t len;                   ///< its length in octets
    protocore_roam_neighbor *out; ///< where the decoded candidates are written
    uint8_t max;                  ///< how many entries @ref out holds
} RoamNrArgs;
/** @brief A BSS Transition Management Request frame to decode (802.11v). */
typedef struct
{
    const uint8_t *frame; ///< the action frame, starting at its Category octet
    size_t len;           ///< its length in octets
} RoamBtmArgs;
/** @brief The policy's handle, described only in roaming.c. */
/**
 * @brief The roam decision layer.
 *
 * A caller sets the members a call takes, invokes it through ::Roam, and reads the outcome off the same
 * handle.
 *
 * @var RoamNs::link   the serving BSS a decision measures against
 * @var RoamNs::cand   the BSS Transition Candidate List a decision picks from
 * @var RoamNs::rules  the BTM Request to honor and the thresholds to apply
 * @var RoamNs::nr     the Neighbor Report element list to decode, and where its candidates land
 * @var RoamNs::btm    the BSS Transition Management Request frame to decode
 * @var RoamNs::decision  the verdict a decide reports
 * @var RoamNs::hint      the BTM Request a decode reports, cleared when @ref ok is false
 * @var RoamNs::n         candidates a Neighbor Report decode wrote
 * @var RoamNs::ok        true only for a well-formed BTM Request
 *
 * @var RoamNs::decide
 * Read @ref link, @ref cand and @ref rules and write @ref decision. Rules in order: a BTM Request with
 * Request Mode bit 2 (Disassociation Imminent) transitions to the preferred candidate when the list holds
 * it, else to the strongest; a BTM Request naming a preferred candidate that is not weaker than the
 * serving BSS transitions to it; a serving signal at or below the policy threshold with a strongest
 * candidate beating it by at least the hysteresis margin transitions to that candidate; otherwise stay.
 * The serving BSSID is excluded from the candidates. A null @c rules.request skips the first two rules, a
 * null @c rules.policy takes the built-in default thresholds, and a null @c link.bssid stays.
 *
 * @var RoamNs::parse_neighbor_report
 * Decode @c nr.elems into up to @c nr.max entries at @c nr.out and report the count in @ref n. Each
 * Neighbor Report element (Element ID 52) supplies BSSID and Channel Number; every other Element ID is
 * skipped, and a truncated element ends the walk. The report carries no signal reading, so each
 * candidate's @c rssi_dbm comes back as @ref PROTOCORE_ROAM_RSSI_UNKNOWN for the caller to fill before
 * @ref RoamNs::decide reads the list.
 *
 * @var RoamNs::parse_btm_request
 * Decode @c btm.frame into @ref hint and set @ref ok. @c btm.frame starts at the Category octet (WNM
 * category 10, BTM Request action 7), followed by Dialog Token, Request Mode, Disassociation Timer and
 * Validity Interval. Request Mode bit 2 sets @c disassoc_imminent; with bit 0 set, the first Neighbor
 * Report element of the Preferred Candidate List supplies @c preferred_bssid, read past the optional BSS
 * Termination Duration and Session Information URL.
 *
 *
 * No storage member: every call reads its inputs off this handle and writes its result back, so the
 * module holds nothing between calls.
 */
typedef struct
{
    RoamLinkArgs link;  ///< IEEE 802.11: the serving BSS
    RoamCandArgs cand;  ///< IEEE 802.11: the BSS Transition Candidate List
    RoamRuleArgs rules; ///< the BTM Request and the local thresholds
    RoamNrArgs nr;      ///< 802.11k: a Neighbor Report element list
    RoamBtmArgs btm;    ///< 802.11v: a BSS Transition Management Request frame
    protocore_roam_decision decision;
    protocore_roam_btm hint;
    uint8_t n;
    proto_bool ok;
} RoamVars;

/** @brief The operands and the outcome. */
extern RoamVars RoamV;

/** @brief The entries. */
typedef struct
{
    void (*const decide)(uint8_t *restrict work);
    void (*const parse_neighbor_report)(uint8_t *restrict work);
    void (*const parse_btm_request)(uint8_t *restrict work);
} RoamNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in RoamV or a region of the borrow at a fixed offset.
void protocore_roaming_decide(uint8_t *restrict work);
void protocore_roaming_parse_neighbor_report(uint8_t *restrict work);
void protocore_roaming_parse_btm_request(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Roam.decide(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const RoamNs Roam __attribute__((unused)) = {
    .decide = protocore_roaming_decide,
    .parse_neighbor_report = protocore_roaming_parse_neighbor_report,
    .parse_btm_request = protocore_roaming_parse_btm_request,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ROAMING

#endif // PROTOCORE_ROAMING_H
