// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sercos.h
 * @brief SERCOS III motion-bus telegram + IDN codec (PC_ENABLE_SERCOS).
 *
 * SERCOS III is the real-time drive/motion bus over Ethernet (raw L2, ethertype 0x88CD, on the shipped
 * services/fieldbus/rawl2). The master cyclically sends **MDT** (Master Data Telegrams) carrying setpoints to the
 * drives, and the drives answer with **AT** (Acknowledge / drive Telegrams) carrying actual values. Both
 * carry a short SERCOS header then the cyclic device data; a separate **service channel** transfers
 * parameters addressed by an **IDN** (IDentification Number):
 *
 *   Telegram header: [type MDT/AT : 1][phase/counter : 1][cycle count : 2]
 *   IDN (16 bit):    S/P bit(1) | parameter-set(3) | data-block(12)  -> "S-0-0100" style addressing
 *
 * This provides the MDT/AT telegram framing + the IDN encode/decode (the addressing every drive
 * parameter uses). The isochronous timing + the ring/line topology are the hardware-gated part. Pure,
 * zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_SERCOS_H
#define PROTOCORE_SERCOS_H

#include "protocore_config.h"

#if PC_ENABLE_SERCOS

// SERCOS telegram types + header length: wire values, so integer constants in a struct.
#define T 0x00 ///< Master Data Telegram (master -> drives).
#define T 0x01 ///< Acknowledge Telegram (drive -> master).
#define N 4

/**
 * @brief Encode a SERCOS IDN (16-bit) from its parts.
 * @param is_product true = a P-parameter (product-specific), false = an S-parameter (standard).
 * @param param_set  the parameter set 0..7.
 * @param data_block the data block number 0..4095.
 * @return the 16-bit IDN: bit15 = S/P, bits14..12 = set, bits11..0 = block.
 */
uint16_t pc_sercos_idn(proto_bool is_product, uint8_t param_set, uint16_t data_block);

/** @brief Decode a SERCOS IDN into its parts (any out-pointer may be null). */
void pc_sercos_idn_parse(uint16_t idn, proto_bool *is_product, uint8_t *param_set, uint16_t *data_block);

/**
 * @brief Build a SERCOS telegram: [type][phase][cycle:2 LE][data...].
 * @param type  SERCOS_TEL_MDT or SERCOS_TEL_AT.
 * @param phase the communication phase / counter byte.
 * @param cycle the cycle count.
 * @param data  the cyclic device data (may be null if data_len == 0).
 * @return the telegram length (4 + data_len), or 0 on overflow.
 */
size_t pc_sercos_build(uint8_t type, uint8_t phase, uint16_t cycle, const uint8_t *data, size_t data_len, uint8_t *out,
                       size_t cap);

/** @brief A parsed SERCOS telegram (data points into the input). */
typedef struct
{
    uint8_t type;
    uint8_t phase;
    uint16_t cycle;
    const uint8_t *data;
    size_t data_len;
} SercosTelegram;

/** @brief Parse a SERCOS telegram. @return true if @p len >= 4 and the type is MDT/AT. */
proto_bool pc_sercos_parse(const uint8_t *frame, size_t len, SercosTelegram *out);

#endif // PC_ENABLE_SERCOS
#endif // PROTOCORE_SERCOS_H
