// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pn532.h
 * @brief PN532 NFC frame codec (PC_ENABLE_PN532) - NXP PN532 NFC/RFID controller.
 *
 * The command-frame protocol of the NXP PN532 (the ubiquitous NFC reader on I2C / SPI /
 * HSU breakouts): a tag read/write bridged to an HTTP / MQTT event. The host and the chip
 * exchange **normal information frames**:
 *
 *   00 | 00 FF | LEN | LCS | TFI | PD0..PDn | DCS | 00
 *
 * where TFI is 0xD4 (host -> PN532) or 0xD5 (PN532 -> host), LEN counts TFI + PData, LCS is
 * the length checksum (LEN + LCS == 0), and DCS is the data checksum (TFI + sum(PData) + DCS
 * == 0). A short 6-byte **ACK frame** (00 00 FF 00 FF 00) confirms each command.
 *
 * pc_pn532_build_frame() assembles a frame carrying a command + parameters, pc_pn532_parse_frame()
 * frames + verifies a response, and pc_pn532_is_ack() detects the ACK. The per-command PData
 * (GetFirmwareVersion, InListPassiveTarget, InDataExchange, ...) is the application's. Pure -
 * you carry the bytes over your I2C / SPI / UART - so it is fully host-testable.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PN532_H
#define PROTOCORE_PN532_H

#include "protocore_config.h"

#if PC_ENABLE_PN532

/** @brief Frame identifier: host -> PN532. */
#define PN532_TFI_HOST 0xD4
/** @brief Frame identifier: PN532 -> host. */
#define PN532_TFI_PN532 0xD5

/**
 * @brief Assemble a normal information frame carrying @p tfi + @p data into @p out.
 * @return the total frame length, or 0 if it would not fit @p cap or @p len exceeds
 *         PC_PN532_MAX_DATA.
 */
uint16_t pc_pn532_build_frame(uint8_t tfi, const uint8_t *data, uint8_t len, uint8_t *out, uint16_t cap);

/**
 * @brief Frame one normal information frame from the front of @p raw and verify LCS + DCS.
 * @param[out] tfi       set to the frame identifier.
 * @param[out] pdata     set to the first PData byte (points into @p raw).
 * @param[out] pdata_len set to the PData length (LEN - 1).
 * @return the frame length consumed (> 0), 0 if more bytes are needed, or -1 if @p raw[0]
 *         does not start a valid frame (wrong preamble / start / LCS / DCS / over-length).
 */
int pc_pn532_parse_frame(const uint8_t *raw, uint16_t len, uint8_t *tfi, const uint8_t **pdata, uint8_t *pdata_len);

/** @brief True if @p raw starts with a PN532 ACK frame (00 00 FF 00 FF 00). */
proto_bool pc_pn532_is_ack(const uint8_t *raw, uint16_t len);

/** @brief Write the 6-byte ACK frame into @p out. @return 6, or 0 if @p cap < 6. */
uint16_t pc_pn532_build_ack(uint8_t *out, uint16_t cap);

#endif // PC_ENABLE_PN532

#endif // PROTOCORE_PN532_H
