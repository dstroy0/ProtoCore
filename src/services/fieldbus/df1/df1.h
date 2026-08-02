// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file df1.h
 * @brief Allen-Bradley DF1 full-duplex frame codec (PC_ENABLE_DF1) - zero-heap framing +
 *        DLE byte-stuffing + BCC/CRC for the Rockwell serial PLC link layer.
 *
 * A DF1 full-duplex message frame (AB pub. 1770-6.5.16):
 * @code
 *   DLE STX  <application data, DLE bytes doubled>  DLE ETX  BCC | CRC
 * @endcode
 *  - A data byte equal to DLE (0x10) is transmitted twice (DLE DLE); the doubled DLE is
 *    counted only once in the BCC/CRC.
 *  - BCC: the 2's complement of the modulo-256 sum of the application data bytes (the ETX
 *    is NOT included). One octet.
 *  - CRC: CRC-16 (poly X16+X15+X2+X0 = 0x8005, init 0x0000, reflected) over the application
 *    data bytes AND the ETX byte; two octets, transmitted low byte first.
 *
 * This is the data-link framing layer; the DST/SRC/CMD/STS/TNS application header lives
 * inside the application data. Field definitions verified against the 1770-6.5.16 manual.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DF1_H
#define PROTOCORE_DF1_H

#include "protocore_config.h"

#if PC_ENABLE_DF1

#define DF1_DLE 0x10
#define DF1_STX 0x02
#define DF1_ETX 0x03

/** @brief Which error check the frame carries. */
typedef enum PROTO_ENUM_PACKED
{
    DF1_CHECK_BCC, ///< 1-octet block check character
    DF1_CHECK_CRC  ///< 2-octet CRC-16
} Df1Check;

/** @brief BCC: 2's complement of the modulo-256 sum of [data, data+len). */
uint8_t pc_df1_bcc(const uint8_t *data, size_t len);

/** @brief CRC-16/ARC (poly 0x8005 / 0xA001 reflected, init 0) over [data, data+len). */
uint16_t pc_df1_crc(const uint8_t *data, size_t len);

/**
 * @brief Build a full-duplex frame around @p data: DLE STX + stuffed data + DLE ETX + check.
 * @param check DF1_CHECK_BCC (1 octet) or DF1_CHECK_CRC (2 octets, low byte first; CRC over
 *              the data + ETX).
 * @return total octets written, or 0 on overflow / bad input.
 */
size_t pc_df1_build_frame(uint8_t *buf, size_t cap, const uint8_t *data, size_t data_len, Df1Check check);

/**
 * @brief Parse + validate a full-duplex frame, un-stuffing the application data.
 * @param out      receives the de-stuffed application data.
 * @param out_cap  capacity of @p out.
 * @param out_len  receives the application-data length.
 * @return true on a complete, check-valid frame; false on bad framing, truncation, an
 *         out overflow, or a BCC/CRC mismatch.
 */
proto_bool pc_df1_parse_frame(const uint8_t *buf, size_t len, Df1Check check, uint8_t *out, size_t out_cap,
                              size_t *out_len);

#endif // PC_ENABLE_DF1

#endif // PROTOCORE_DF1_H
