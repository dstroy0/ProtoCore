// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file directnet.h
 * @brief AutomationDirect / Koyo DirectNET serial frame codec (PC_ENABLE_DIRECTNET).
 *
 * DirectNET is the AutomationDirect (Koyo) DirectLOGIC-PLC master-slave serial protocol for reading and
 * writing V-memory. A transaction is a control-char-delimited frame with an LRC checksum. This builds
 * the two framed messages the master sends:
 *
 *  - **Header/enquiry**: `SOH [slave-hex][type][addr-hex 4][blocks-hex 2] ETB [LRC]` - the request that
 *    announces a read/write of N data blocks at a V-memory address.
 *  - **Data frame**: `STX [data...] ETX [LRC]` - the payload block.
 *
 * The LRC is the longitudinal XOR of the framed bytes (between the start control char and the LRC,
 * inclusive of the terminating ETB/ETX). This provides the framing + LRC + the ASCII-hex field helpers;
 * the UART transport + the ACK/NAK handshake sequencing are the device step. Pure, zero heap,
 * host-testable.
 */

#ifndef PROTOCORE_DIRECTNET_H
#define PROTOCORE_DIRECTNET_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_DIRECTNET

/** @brief DirectNET control bytes: wire values compared/emitted, so integer constants in a struct. */
struct DnetByte
{
    static constexpr uint8_t DNET_ENQ = 0x05;
    static constexpr uint8_t DNET_ACK = 0x06;
    static constexpr uint8_t DNET_NAK = 0x15;
    static constexpr uint8_t DNET_SOH = 0x01;
    static constexpr uint8_t DNET_STX = 0x02;
    static constexpr uint8_t DNET_ETX = 0x03;
    static constexpr uint8_t DNET_ETB = 0x17;
    static constexpr uint8_t DNET_EOT = 0x04;
    static constexpr uint8_t DNET_READ = 0x30;  ///< request type: read ('0').
    static constexpr uint8_t DNET_WRITE = 0x38; ///< request type: write ('8').
};

/** @brief Longitudinal XOR checksum (the DirectNET LRC) over @p len bytes. */
uint8_t pc_dnet_lrc(const uint8_t *bytes, size_t len);

/**
 * @brief Build a DirectNET header frame: SOH + [slave][type][addr:4hex][blocks:2hex] + ETB + LRC.
 * @param slave   station number 0..99 (emitted as two ASCII-hex digits).
 * @param type    DNET_READ or DNET_WRITE.
 * @param address V-memory octal address, emitted as 4 ASCII-hex digits.
 * @param blocks  number of data blocks, emitted as 2 ASCII-hex digits.
 * @return the frame length, or 0 on overflow. The LRC covers slave..ETB.
 */
size_t pc_dnet_header(uint8_t slave, uint8_t type, uint16_t address, uint8_t blocks, uint8_t *out, size_t cap);

/**
 * @brief Build a DirectNET data frame: STX + data + ETX + LRC. The LRC covers data..ETX.
 * @return the frame length (1 + data_len + 1 + 1), or 0 on overflow.
 */
size_t pc_dnet_data(const uint8_t *data, size_t data_len, uint8_t *out, size_t cap);

/**
 * @brief Validate a DirectNET data frame (STX..ETX + LRC) and expose its payload.
 * @return true if it is well-formed and the LRC matches; sets @p data / @p data_len (pointers into @p frame).
 */
bool pc_dnet_data_parse(const uint8_t *frame, size_t len, const uint8_t **data, size_t *data_len);

#endif // PC_ENABLE_DIRECTNET
#endif // PROTOCORE_DIRECTNET_H
