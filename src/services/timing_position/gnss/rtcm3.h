// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rtcm3.h
 * @brief RTCM 3.x framing + station-reference message codec (PC_ENABLE_NTRIP_CASTER).
 *
 * The pure, zero-heap, host-testable core of the GNSS RTK base / NTRIP caster: the RTCM3 transport frame
 * (0xD3 preamble, 6 reserved + 10-bit length, payload, 24-bit CRC-24Q) and the MSB-first bit I/O every
 * RTCM3 message is packed with, plus encode/decode of the Stationary Antenna Reference Point messages
 * 1005 (no height) and 1006 (with antenna height) that advertise the base's surveyed position.
 *
 * RTCM 10403.x numbers every field (DFxxx) and packs them big-endian, MSB-first, with no byte alignment
 * inside the payload; this codec follows that layout exactly and is verified byte-for-byte against pyrtcm
 * / RTKLIB. ECEF coordinates use the standard 0.0001 m (0.1 mm) resolution as 38-bit signed integers, so
 * they are carried here in units of 0.1 mm.
 *
 * Observation messages (the MSM sets 1074/1077/1084/... that actually let a rover fix ambiguities) are
 * built from a receiver's raw carrier-phase / pseudorange measurements and are added when a raw-capable
 * receiver (u-blox RXM-RAWX: F9P / M8T class) drives the base; this file is the framing + reference-point
 * foundation they and the caster share.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RTCM3_H
#define PROTOCORE_RTCM3_H

#include "protocore_config.h"

#if PC_ENABLE_NTRIP_CASTER

#define RTCM3_PREAMBLE 0xD3u ///< frame start byte
#define RTCM3_HDR_LEN 3      ///< preamble (1) + 6 reserved bits + 10-bit length (2)
#define RTCM3_CRC_LEN 3      ///< trailing CRC-24Q
#define RTCM3_MAX_PAYLOAD 1023
#define RTCM3_MAX_FRAME (RTCM3_HDR_LEN + RTCM3_MAX_PAYLOAD + RTCM3_CRC_LEN)

// ---------------------------------------------------------------------------------------------
// CRC-24Q (the RTCM3 / Qualcomm CRC): polynomial 0x1864CFB, initial value 0, no reflection.
// ---------------------------------------------------------------------------------------------

/** @brief CRC-24Q over @p len bytes of @p data (returned in the low 24 bits). */
uint32_t pc_rtcm3_crc24q(const uint8_t *data, size_t len);

// ---------------------------------------------------------------------------------------------
// MSB-first bit I/O (RTCM3 packs fields back to back, most-significant bit first, up to 64 bits).
// ---------------------------------------------------------------------------------------------

/// A bit cursor over a byte buffer for writing MSB-first fields. Pre-zero the buffer before writing.
typedef struct
{
    uint8_t *buf;
    size_t cap_bits; ///< capacity in bits
    size_t pos;      ///< current bit offset
    proto_bool ok;   ///< cleared if a write would overflow
} RtcmBitWriter;

/** @brief Start a writer over @p buf (@p cap bytes). The buffer must already be zeroed. */
void pc_rtcm_bw_init(RtcmBitWriter *w, uint8_t *buf, size_t cap);
/** @brief Append @p nbits (1..64) of @p val, MSB-first (unsigned). */
void pc_rtcm_bw_u(RtcmBitWriter *w, uint64_t val, uint8_t nbits);
/** @brief Append @p nbits (1..64) of @p val as two's-complement (signed). */
void pc_rtcm_bw_s(RtcmBitWriter *w, int64_t val, uint8_t nbits);

/** @brief Read @p nbits (1..64) unsigned, MSB-first, advancing @p pos (bit offset). */
uint64_t pc_rtcm_br_u(const uint8_t *buf, size_t *pos, uint8_t nbits);
/** @brief Read @p nbits (1..64) as a sign-extended two's-complement value, advancing @p pos. */
int64_t pc_rtcm_br_s(const uint8_t *buf, size_t *pos, uint8_t nbits);

// ---------------------------------------------------------------------------------------------
// Transport frame.
// ---------------------------------------------------------------------------------------------

/// A parsed RTCM3 frame view (payload points into the caller's buffer).
typedef struct
{
    uint16_t msg_type;      ///< DF002 message number (first 12 payload bits)
    const uint8_t *payload; ///< start of the payload inside the parsed buffer
    uint16_t payload_len;   ///< payload length in bytes
    proto_bool crc_ok;      ///< the trailing CRC-24Q matched
} Rtcm3Frame;

/**
 * @brief Parse one RTCM3 frame beginning at @p buf[0] (which must be the preamble).
 *
 * @return the total frame length (header + payload + CRC) when a whole frame is buffered - with
 *         @p out->crc_ok reflecting the CRC check - or 0 when @p buf does not yet hold the full frame.
 *         Use pc_rtcm3_sync() first to align @p buf to a preamble in a byte stream.
 */
size_t pc_rtcm3_frame_parse(const uint8_t *buf, size_t len, Rtcm3Frame *out);

/** @brief Index of the next 0xD3 preamble in @p buf, or @p len if there is none. */
size_t pc_rtcm3_sync(const uint8_t *buf, size_t len);

/**
 * @brief Wrap @p payload (@p payload_len bytes) in a full frame: preamble + length + payload + CRC-24Q.
 * @return the total frame length written to @p out, or 0 if @p cap is too small or payload_len > 1023.
 */
size_t pc_rtcm3_frame_build(uint8_t *out, size_t cap, const uint8_t *payload, uint16_t payload_len);

// ---------------------------------------------------------------------------------------------
// Message 1005 / 1006 - Stationary Antenna Reference Point. Coordinates are ECEF in 0.1 mm units.
// ---------------------------------------------------------------------------------------------

/// Decoded 1005 / 1006 antenna reference point.
typedef struct
{
    uint16_t station_id;
    int64_t ecef_x_01mm; ///< ECEF X in 0.1 mm (RTCM3 0.0001 m resolution)
    int64_t ecef_y_01mm;
    int64_t ecef_z_01mm;
    uint16_t antenna_height_01mm; ///< 1006 only (0 for 1005)
    proto_bool has_height;        ///< true for 1006
} Rtcm3StationArp;

/** @brief Build a full 1005 frame (ARP, no antenna height). @return frame length or 0. */
size_t pc_rtcm3_build_1005(uint8_t *out, size_t cap, uint16_t station_id, int64_t ecef_x_01mm, int64_t ecef_y_01mm,
                           int64_t ecef_z_01mm);

/** @brief Build a full 1006 frame (ARP with antenna height). @return frame length or 0. */
size_t pc_rtcm3_build_1006(uint8_t *out, size_t cap, uint16_t station_id, int64_t ecef_x_01mm, int64_t ecef_y_01mm,
                           int64_t ecef_z_01mm, uint16_t antenna_height_01mm);

/**
 * @brief Decode a 1005 / 1006 payload (not the framed message) into @p out.
 * @return true if @p payload is a well-formed 1005 or 1006; false otherwise.
 */
proto_bool pc_rtcm3_parse_1005(const uint8_t *payload, uint16_t payload_len, Rtcm3StationArp *out);

#endif // PC_ENABLE_NTRIP_CASTER

#endif // PROTOCORE_RTCM3_H
