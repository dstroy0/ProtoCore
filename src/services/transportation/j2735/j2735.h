// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file j2735.h
 * @brief SAE J2735 V2X - ASN.1 UPER primitive codec + Basic Safety Message core (PC_ENABLE_J2735).
 *
 * J2735 (the Vehicle-to-Everything message dictionary: BSM, SPaT, MAP) is serialized with ASN.1 **UPER**
 * (Unaligned Packed Encoding Rules). UPER packs fields at the bit level with no padding, so the codec is
 * a bit-writer / bit-reader plus the UPER rules for the common types:
 *
 *  - a **constrained INTEGER** in [lo, hi] is the offset (value - lo) in exactly ceil(log2(range)) bits;
 *  - a **BOOLEAN** is one bit;
 *  - an unconstrained/length-prefixed **OCTET STRING** uses a length determinant then the bytes.
 *
 * This provides that bit-level UPER primitive layer (host-testable against hand-computed bit patterns)
 * and, on top of it, the J2735 **BSMcore** position block (msgCnt, id, secMark, lat, long, elev, speed,
 * heading) encode/decode - the high-rate safety kernel every BSM carries. The DSRC / C-V2X radio is an
 * external module; this is the message codec. Pure, zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_J2735_H
#define PROTOCORE_J2735_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_J2735

/** @brief A UPER bit writer over a caller buffer (MSB-first within each octet). */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    size_t bit_pos; ///< bits written so far.
    proto_bool ok;  ///< cleared on overflow.
} UperWriter;

/** @brief A UPER bit reader over a caller buffer. */
typedef struct
{
    const uint8_t *buf;
    size_t nbits; ///< total bits available.
    size_t bit_pos;
    proto_bool ok; ///< cleared on read past the end.
} UperReader;

void pc_uper_writer_init(UperWriter *w, uint8_t *buf, size_t cap);
/** @return the number of whole octets produced (rounds the bit length up), or 0 if the writer overflowed. */
size_t pc_uper_writer_finish(UperWriter *w);

/** @brief Write @p nbits low bits of @p value, MSB-first. */
void pc_uper_put_bits(UperWriter *w, uint32_t value, unsigned nbits);
/** @brief Write a BOOLEAN (1 bit). */
void pc_uper_put_bool(UperWriter *w, proto_bool v);
/** @brief Write a constrained INTEGER in [lo, hi] as (value-lo) in ceil(log2(hi-lo+1)) bits. */
void pc_uper_put_cint(UperWriter *w, int64_t value, int64_t lo, int64_t hi);

void pc_uper_reader_init(UperReader *r, const uint8_t *buf, size_t nbits);
/** @brief Read @p nbits bits, MSB-first, into the low bits of the result. */
uint32_t pc_uper_get_bits(UperReader *r, unsigned nbits);
proto_bool pc_uper_get_bool(UperReader *r);
/** @brief Read a constrained INTEGER in [lo, hi]. */
int64_t pc_uper_get_cint(UperReader *r, int64_t lo, int64_t hi);

/** @brief Number of bits a constrained INTEGER in [lo, hi] occupies (0 when lo == hi). */
unsigned pc_uper_cint_bits(int64_t lo, int64_t hi);

/** @brief The J2735 BSMcoreData safety kernel (values in J2735 units; see the SAE ranges). */
typedef struct
{
    uint8_t msg_count; ///< MsgCount 0..127.
    uint32_t id;       ///< TemporaryID (4 octets).
    uint16_t sec_mark; ///< DSecond 0..65535 (ms of the minute; 65535 = unavailable).
    int32_t lat;       ///< Latitude 1/10 microdegree, -900000000..900000001.
    int32_t lon;       ///< Longitude 1/10 microdegree, -1799999999..1800000001.
    int32_t elev;      ///< Elevation, -4096..61439 (decimeters).
    uint16_t speed;    ///< Speed 0..8191 (0.02 m/s; 8191 = unavailable).
    uint16_t heading;  ///< Heading 0..28800 (0.0125 deg; 28800 = unavailable).
} J2735BsmCore;

/**
 * @brief UPER-encode a BSMcore block. @return octets written, or 0 on overflow.
 *
 * Encodes, in order: msgCnt [0..127], id (32 bits), secMark [0..65535], lat [-900000000..900000001],
 * long [-1799999999..1800000001], elev [-4096..61439], speed [0..8191], heading [0..28800].
 */
size_t pc_j2735_bsm_core_encode(const J2735BsmCore *c, uint8_t *out, size_t cap);

/** @brief UPER-decode a BSMcore block. @return true on success. */
proto_bool pc_j2735_bsm_core_decode(const uint8_t *in, size_t len, J2735BsmCore *c);

/** @brief J2735 MovementPhaseState (the signal-group state in a SPaT MovementState). */
typedef enum PROTO_ENUM_PACKED
{
    J2735_PHASE_DARK = 0,                        ///< unavailable / dark.
    J2735_PHASE_STOP_THEN_PROCEED = 1,           ///< flashing red.
    J2735_PHASE_STOP_AND_REMAIN = 3,             ///< red.
    J2735_PHASE_PERMISSIVE_MOVEMENT_ALLOWED = 5, ///< permissive green.
    J2735_PHASE_PROTECTED_MOVEMENT_ALLOWED = 6,  ///< protected green.
    J2735_PHASE_PERMISSIVE_CLEARANCE = 7,        ///< permissive yellow.
    J2735_PHASE_PROTECTED_CLEARANCE = 8,         ///< protected yellow.
    J2735_PHASE_CAUTION_CONFLICTING_TRAFFIC = 9  ///< flashing yellow.
} J2735PhaseState;

/** @brief One SPaT MovementState: a signal group, its current phase, and the min/max end times. */
typedef struct
{
    uint8_t signal_group;  ///< SignalGroupID 0..255.
    uint8_t phase;         ///< J2735PhaseState (eventState 0..9).
    uint16_t min_end_time; ///< TimeMark 0..36000 (tenths of a second in the hour; 36001 = undefined here).
    uint16_t max_end_time; ///< TimeMark 0..36000.
} J2735MovementState;

/**
 * @brief UPER-encode a SPaT MovementState list into @p out.
 * @param states  the movement states.
 * @param count   number of states (0..31; encoded as a 5-bit count).
 * @return octets written, or 0 on overflow.
 *
 * Encodes count [0..31] then, per state: signalGroup [0..255], eventState [0..9], minEndTime [0..36000],
 * maxEndTime [0..36000] - the timing core a vehicle uses for a countdown.
 */
size_t pc_j2735_spat_encode(const J2735MovementState *states, size_t count, uint8_t *out, size_t cap);

/**
 * @brief UPER-decode a SPaT MovementState list.
 * @param out_states  buffer for the decoded states.
 * @param max_states  capacity of @p out_states.
 * @param out_count   set to the number decoded.
 * @return true on success (and the encoded count fit in @p max_states).
 */
proto_bool pc_j2735_spat_decode(const uint8_t *in, size_t len, J2735MovementState *out_states, size_t max_states,
                                size_t *out_count);

/** @brief One MAP lane: an id and an approach/egress flag (the minimal LaneID + directionalUse bit). */
typedef struct
{
    uint8_t lane_id;       ///< LaneID 0..255.
    proto_bool is_ingress; ///< true = an approach (ingress) lane, false = egress.
    int16_t node_x;        ///< first node offset X, -2048..2047 (cm, node-XY offset).
    int16_t node_y;        ///< first node offset Y, -2048..2047 (cm).
} J2735Lane;

/** @brief The MAP intersection-geometry core: an intersection id + a list of lanes. */
typedef struct
{
    uint16_t intersection_id; ///< IntersectionID 0..65535.
    uint16_t ref_lat;         ///< reference point offset lat surrogate (0..65535 here for the codec).
    uint16_t ref_lon;         ///< reference point offset lon surrogate (0..65535 here).
} J2735MapIntersection;

/**
 * @brief UPER-encode a MAP intersection: id + refLat + refLon, then a 5-bit lane count and each lane
 *        (laneID [0..255], directionalUse bit, nodeX [-2048..2047], nodeY [-2048..2047]).
 * @param count number of lanes (0..31).
 * @return octets written, or 0 on overflow.
 */
size_t pc_j2735_map_encode(const J2735MapIntersection *isect, const J2735Lane *lanes, size_t count, uint8_t *out,
                           size_t cap);

/**
 * @brief UPER-decode a MAP intersection. @return true on success (and the lane count fit @p max_lanes).
 */
proto_bool pc_j2735_map_decode(const uint8_t *in, size_t len, J2735MapIntersection *isect, J2735Lane *out_lanes,
                               size_t max_lanes, size_t *out_count);

#endif // PC_ENABLE_J2735

PROTO_END_DECLS

#endif // PROTOCORE_J2735_H
