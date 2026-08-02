// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cclink.h
 * @brief CC-Link (CLPA) cyclic fieldbus frame codec (PC_ENABLE_CCLINK).
 *
 * CC-Link is Mitsubishi's (CLPA) factory fieldbus. The classic CC-Link master polls remote stations
 * over RS-485 exchanging a cyclic process image split into bit devices (RX/RY - remote input/output
 * bits) and word devices (RWr/RWw - remote registers). This codec builds/validates the cyclic frame a
 * master/station exchanges:
 *
 *     [station][command][RX/RY bit data...][RWr/RWw word data...][sum-checksum]
 *
 * A station's process image is a fixed BSS block; this frames it. The checksum is the low byte of the
 * arithmetic sum of the framed bytes. The RS-485 timing and the CC-Link IE Field (Gigabit) PHY are the
 * hardware-gated part; this is the frame + process-image accessors. Pure, zero heap, host-testable.
 */

#ifndef PROTOCORE_CCLINK_H
#define PROTOCORE_CCLINK_H

#include "protocore_config.h"

#if PC_ENABLE_CCLINK

// CC-Link command bytes: wire values compared/emitted, so integer constants in a namespacing struct.
#define H 0x01 ///< cyclic refresh (master <-> station process image).
#define L 0x02 ///< poll a station.
#define T 0x0F ///< line test.

/** @brief Arithmetic-sum checksum: low byte of the sum of @p len bytes. */
uint8_t pc_cclink_sum(const uint8_t *bytes, size_t len);

/**
 * @brief Build a CC-Link cyclic frame: [station][command][bit_data...][word_data...][checksum].
 * @param station   station number 0..63.
 * @param command   CCLINK_CMD_*.
 * @param bits       the RX/RY bit-device bytes (may be null if bit_len == 0).
 * @param bit_len    number of bit-device bytes.
 * @param words      the RWr/RWw word-device bytes (little-endian words; may be null if word_len == 0).
 * @param word_len   number of word-device bytes.
 * @return the frame length (2 + bit_len + word_len + 1), or 0 on overflow / bad args.
 */
size_t pc_cclink_build(uint8_t station, uint8_t command, const uint8_t *bits, size_t bit_len, const uint8_t *words,
                       size_t word_len, uint8_t *out, size_t cap);

/** @brief A parsed CC-Link frame (payload points into the input; caller knows the bit/word split). */
typedef struct
{
    uint8_t station;
    uint8_t command;
    const uint8_t *payload; ///< the bit+word data region.
    size_t payload_len;
} CcLinkFrame;

/** @brief Validate the checksum and parse a CC-Link frame. @return true if the checksum matches. */
proto_bool pc_cclink_parse(const uint8_t *frame, size_t len, CcLinkFrame *out);

/** @brief Read bit @p index (0-based) from a bit-device byte array. */
proto_bool pc_cclink_get_bit(const uint8_t *bits, size_t bit_len, size_t index);

/** @brief Set/clear bit @p index in a bit-device byte array (no-op if out of range). */
void pc_cclink_set_bit(uint8_t *bits, size_t bit_len, size_t index, proto_bool value);

/** @brief Read word @p index (0-based, little-endian) from a word-device byte array. */
uint16_t pc_cclink_get_word(const uint8_t *words, size_t word_len, size_t index);

#endif // PC_ENABLE_CCLINK
#endif // PROTOCORE_CCLINK_H
