// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nema_ts2.h
 * @brief NEMA TS 2 traffic-cabinet SDLC frame codec (PC_ENABLE_NEMA_TS2).
 *
 * NEMA TS 2 links the devices inside a traffic-signal control cabinet - the controller, the Malfunction
 * Management Unit (MMU), the Bus Interface Units (BIUs) and detector racks - over a synchronous SDLC
 * bus. Each transaction is an SDLC frame:
 *
 *     [address][control][frame-type][data...][FCS-16]
 *
 * where the FCS is the HDLC/X.25 CRC-16 (CRC-16/X-25: reflected poly 0x1021, init 0xFFFF, xorout 0xFFFF)
 * over address..last-data, transmitted low byte first. This builds and validates those frames (the
 * frame-type identifies command / status / detector frames per the TS 2 frame set); the synchronous
 * serial PHY and the BIU/detector timing are the hardware-gated part. Pure, zero heap, host-testable.
 */

#ifndef PROTOCORE_NEMA_TS2_H
#define PROTOCORE_NEMA_TS2_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_NEMA_TS2

/** @brief Common TS 2 frame types (the third octet). */
// NEMA TS2 frame types: wire values compared, so integer constants in a namespacing struct.
struct NemaTs2
{
    static constexpr uint8_t NEMA_TS2_FT_CMD_LOADSWITCH = 0; ///< controller -> BIU: load-switch (signal-head) drivers.
    static constexpr uint8_t NEMA_TS2_FT_STATUS_LOADSWITCH =
        128;                                           ///< BIU -> controller: load-switch status (frame type + 128).
    static constexpr uint8_t NEMA_TS2_FT_CMD_MMU = 3;  ///< controller <-> MMU status frame.
    static constexpr uint8_t NEMA_TS2_FT_DETECTOR = 9; ///< detector BIU -> controller: detector call/status.
};

/** @brief HDLC/X.25 CRC-16 (CRC-16/X-25) over @p len bytes. */
uint16_t pc_nema_ts2_crc(const uint8_t *bytes, size_t len);

/**
 * @brief Build a TS 2 SDLC frame: [address][control][frame_type][data...][FCS lo][FCS hi].
 * @return the frame length (3 + data_len + 2), or 0 on overflow / bad args.
 */
size_t pc_nema_ts2_build(uint8_t address, uint8_t control, uint8_t frame_type, const uint8_t *data, size_t data_len,
                         uint8_t *out, size_t cap);

/** @brief A parsed TS 2 frame (data points into the input). */
struct NemaTs2Frame
{
    uint8_t address;
    uint8_t control;
    uint8_t frame_type;
    const uint8_t *data;
    size_t data_len;
};

/** @brief Validate the FCS and parse a TS 2 frame. @return true if the CRC matches and it is well-formed. */
bool pc_nema_ts2_parse(const uint8_t *frame, size_t len, NemaTs2Frame *out);

#endif // PC_ENABLE_NEMA_TS2
#endif // PROTOCORE_NEMA_TS2_H
