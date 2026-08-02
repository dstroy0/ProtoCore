// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file c37118.h
 * @brief IEEE C37.118.2 synchrophasor frame codec (PC_ENABLE_C37118) - zero-heap frame
 *        builder + parser for the PMU / PDC wide-area-measurement wire protocol.
 *
 * A C37.118.2 frame (all fields big-endian / network order):
 * @code
 *   SYNC(2)  FRAMESIZE(2)  IDCODE(2)  SOC(4)  FRACSEC(4)  DATA(n)  CHK(2)
 * @endcode
 *  - SYNC: byte 0 = 0xAA; byte 1 = (frame_type << 4) | version (bits 6-4 type, 3-0 version).
 *  - FRAMESIZE: total octets in the frame, the CHK included.
 *  - SOC: seconds-of-century (Unix epoch seconds); FRACSEC: top 8 bits time quality, low
 *    24 bits the fraction of a second.
 *  - CHK: CRC-CCITT (poly 0x1021, init 0xFFFF, no reflection, no final mask) over every
 *    octet up to but excluding the CHK itself.
 *
 * Frame types: data / header / config-1 / config-2 / command / config-3. The DATA contents
 * are message-type specific (data frames depend on the configuration); this codec frames
 * any payload and fully builds + parses the fixed Command frame. CRC verified against the
 * canonical CRC-CCITT-FALSE check value.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_C37118_H
#define PROTOCORE_C37118_H

#include "protocore_config.h"

#if PC_ENABLE_C37118

#define C37118_SYNC_LEADER 0xAA  ///< SYNC byte 0 (frame leader)
#define C37118_TYPE_SHIFT 4      ///< SYNC byte 1: frame type occupies bits 6-4
#define C37118_TYPE_MASK 0x07    ///< frame-type field width (after the shift)
#define C37118_VERSION_MASK 0x0F ///< SYNC byte 1: version occupies bits 3-0

// Frame types (SYNC byte 1, bits 6-4).
#define C37118_TYPE_DATA 0
#define C37118_TYPE_HEADER 1
#define C37118_TYPE_CFG1 2
#define C37118_TYPE_CFG2 3
#define C37118_TYPE_CMD 4
#define C37118_TYPE_CFG3 5

#define C37118_VERSION_2005 1
#define C37118_VERSION_2011 2

// Command codes (Command-frame DATA word).
#define C37118_CMD_DATA_OFF 1
#define C37118_CMD_DATA_ON 2
#define C37118_CMD_SEND_HDR 3
#define C37118_CMD_SEND_CFG1 4
#define C37118_CMD_SEND_CFG2 5
#define C37118_CMD_SEND_CFG3 6

#define C37118_MIN_FRAME 16 ///< header (14) + CHK (2), no payload

/** @brief CRC-CCITT (0x1021, init 0xFFFF, no reflection, no final mask). */
uint16_t pc_c37118_crc(const uint8_t *data, size_t len);

/**
 * @brief Build a frame: SYNC + FRAMESIZE + IDCODE + SOC + FRACSEC + payload + CHK.
 * @param type    frame type (C37118_TYPE_*), placed in SYNC bits 6-4.
 * @param version version nibble (e.g. C37118_VERSION_2011).
 * @return total octets written, or 0 on overflow / bad input.
 */
size_t pc_c37118_build_frame(uint8_t *buf, size_t cap, uint8_t type, uint8_t version, uint16_t idcode, uint32_t soc,
                             uint32_t fracsec, const uint8_t *payload, size_t payload_len);

/** @brief Build a Command frame (DATA = the 16-bit command word), version 2011. */
size_t pc_c37118_build_command(uint8_t *buf, size_t cap, uint16_t idcode, uint32_t soc, uint32_t fracsec, uint16_t cmd);

/** @brief A parsed frame; @ref data points INTO the source buffer (between FRACSEC and CHK). */
typedef struct
{
    uint8_t type;    ///< frame type (bits 6-4 of SYNC byte 1)
    uint8_t version; ///< version nibble (bits 3-0)
    uint16_t framesize;
    uint16_t idcode;
    uint32_t soc;
    uint32_t fracsec;
    const uint8_t *data;
    size_t data_len;
} C37118Frame;

/**
 * @brief Parse and CRC-validate a frame at the head of [buf, buf+len).
 * @return true on a complete, CRC-valid frame; false on a bad SYNC, truncation, an
 *         out-of-range FRAMESIZE, or a CHK mismatch.
 */
proto_bool pc_c37118_parse_frame(const uint8_t *buf, size_t len, C37118Frame *out);

/** @brief Read the command word from a parsed Command frame. */
proto_bool pc_c37118_parse_command(const C37118Frame *f, uint16_t *cmd);

// --- DATA frame STAT word (IEEE C37.118.2-2011 Table 6): the 16-bit status opening a data frame ---

// Unlocked-time code (STAT bits 5-4): how long the PMU has been unlocked from its time source.
#define C37118_UNLOCKED_UNDER_10S 0  ///< sync locked, or unlocked < 10 s
#define C37118_UNLOCKED_10_100S 1    ///< 10 s .. 100 s since unlock
#define C37118_UNLOCKED_100_1000S 2  ///< 100 s .. 1000 s
#define C37118_UNLOCKED_OVER_1000S 3 ///< > 1000 s

// Common trigger reasons (STAT bits 3-0).
#define C37118_TRIGGER_MANUAL 0      ///< manual / no trigger
#define C37118_TRIGGER_MAG_LOW 1     ///< magnitude low
#define C37118_TRIGGER_MAG_HIGH 2    ///< magnitude high
#define C37118_TRIGGER_PHASE_ANGLE 3 ///< phase-angle difference
#define C37118_TRIGGER_FREQ 4        ///< frequency high or low
#define C37118_TRIGGER_DFDT 5        ///< df/dt high

/** @brief The decoded STAT word of a data frame. */
typedef struct
{
    uint16_t raw;                 ///< the raw 16-bit STAT word
    proto_bool data_valid;        ///< data is valid (STAT bit 15 == 0)
    proto_bool pmu_error;         ///< PMU error, including configuration error (bit 14)
    proto_bool in_sync;           ///< PMU is in sync with UTC (bit 13 == 0)
    proto_bool sorted_by_arrival; ///< data sorting: false = by time stamp, true = by arrival (bit 12)
    proto_bool trigger;           ///< PMU trigger detected (bit 11)
    proto_bool config_change;     ///< configuration change effective within 1 min (bit 10)
    proto_bool data_modified;     ///< data modified by post-processing (bit 9)
    uint8_t time_quality;         ///< PMU time quality (bits 8-6, 0..7)
    uint8_t unlocked_time;        ///< unlocked-time code (bits 5-4, @ref C37118_UNLOCKED_UNDER_10S etc.)
    uint8_t trigger_reason;       ///< trigger reason (bits 3-0, @ref C37118_TRIGGER_MANUAL etc.)
} C37118Stat;

/**
 * @brief Decode the 16-bit STAT word that opens a DATA frame's payload (IEEE C37.118.2-2011 Table 6).
 * @return true iff @p f is a data frame (type C37118_TYPE_DATA) carrying at least the 2-octet STAT word.
 */
proto_bool pc_c37118_decode_stat(const C37118Frame *f, C37118Stat *out);

#endif // PC_ENABLE_C37118

#endif // PROTOCORE_C37118_H
