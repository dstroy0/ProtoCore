// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file profibus.h
 * @brief PROFIBUS-DP FDL telegram codec (PC_ENABLE_PROFIBUS).
 *
 * PROFIBUS-DP is the Siemens RS-485 master/slave fieldbus (the DP-V0 cyclic I/O exchange). Its FDL data
 * link uses fixed telegram formats delimited by a start byte (SD):
 *
 *  - **SD1 (0x10)**: no data - `SD1 DA SA FC FCS ED` (a request/status telegram).
 *  - **SD2 (0x68)**: variable data - `SD2 LE LEr SD2 DA SA FC [data...] FCS ED`, where LE = length of
 *    (DA + SA + FC + data), repeated as LEr for redundancy.
 *  - **SD3 (0xA2)**: fixed 8 data bytes (not built here).
 *
 * DA = destination, SA = source, FC = frame control. The FCS is the arithmetic sum (mod 256) of DA + SA
 * + FC + data. ED (end delimiter) is 0x16. This builds/validates the SD1 and SD2 telegrams a DP master
 * exchanges with slaves; the RS-485 UART timing + the DP-V0 state machine are the device step. Pure,
 * zero heap, no stdlib, host-testable.
 */

#ifndef PROTOCORE_PROFIBUS_H
#define PROTOCORE_PROFIBUS_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_PROFIBUS

// PROFIBUS telegram delimiters + Frame Control values: wire bytes, so integer constants in a struct.
struct Profibus
{
    static constexpr uint8_t PB_SD1 = 0x10; ///< start delimiter: no data.
    static constexpr uint8_t PB_SD2 = 0x68; ///< start delimiter: variable data.
    static constexpr uint8_t PB_SD3 = 0xA2; ///< start delimiter: fixed 8 data.
    static constexpr uint8_t PB_SD4 = 0xDC; ///< token telegram.
    static constexpr uint8_t PB_ED = 0x16;  ///< end delimiter.
    // Frame Control (FC) common values.
    static constexpr uint8_t PB_FC_REQUEST_FDL_STATUS = 0x49; ///< request FDL status (with FCB/FCV).
    static constexpr uint8_t PB_FC_SRD_LOW = 0x6C;            ///< Send and Request Data, low priority.
    static constexpr uint8_t PB_FC_SRD_HIGH = 0x7C;           ///< Send and Request Data, high priority.
};

/** @brief PROFIBUS FCS: arithmetic sum (mod 256) of @p len bytes (DA + SA + FC + data). */
uint8_t pc_pb_fcs(const uint8_t *bytes, size_t len);

/**
 * @brief Build an SD1 (no-data) telegram: SD1 DA SA FC FCS ED. @return 6, or 0 on overflow.
 */
size_t pc_pb_build_sd1(uint8_t da, uint8_t sa, uint8_t fc, uint8_t *out, size_t cap);

/**
 * @brief Build an SD2 (variable-data) telegram: SD2 LE LEr SD2 DA SA FC data FCS ED.
 * @param data     the data unit (may be null if data_len == 0).
 * @param data_len 0..246 (the DP process data).
 * @return the telegram length (6 + 3 + data_len... = 9 + data_len), or 0 on overflow / bad args.
 */
size_t pc_pb_build_sd2(uint8_t da, uint8_t sa, uint8_t fc, const uint8_t *data, size_t data_len, uint8_t *out,
                       size_t cap);

/**
 * @brief Build an SD3 (fixed 8-octet data) telegram: SD3 DA SA FC data[8] FCS ED. The data length is always
 *        exactly 8 (the fixed-length data telegram), so @p data must point to 8 octets.
 * @return 14 (the telegram length), or 0 on a null buffer / null data / overflow.
 */
size_t pc_pb_build_sd3(uint8_t da, uint8_t sa, uint8_t fc, const uint8_t *data, uint8_t *out, size_t cap);

/** @brief A parsed PROFIBUS telegram (data points into the input, null for SD1). */
struct PbTelegram
{
    uint8_t sd; ///< the start delimiter (PB_SD1 / PB_SD2 / PB_SD3).
    uint8_t da;
    uint8_t sa;
    uint8_t fc;
    const uint8_t *data;
    size_t data_len;
};

/** @brief Validate + parse an SD1 / SD2 / SD3 telegram (FCS + ED checked). @return true if well-formed. */
bool pc_pb_parse(const uint8_t *frame, size_t len, PbTelegram *out);

#endif // PC_ENABLE_PROFIBUS
#endif // PROTOCORE_PROFIBUS_H
