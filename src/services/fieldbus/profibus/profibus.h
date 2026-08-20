// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file profibus.h
 * @brief PROFIBUS-DP FDL telegram codec (PROTOCORE_ENABLE_PROFIBUS).
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_PROFIBUS

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

// PROFIBUS telegram delimiters + Frame Control values.
#define PB_SD1 0x10 ///< start delimiter: no data.
#define PB_SD2 0x68 ///< start delimiter: variable data.
#define PB_SD3 0xA2 ///< start delimiter: fixed 8 data.
#define PB_SD4 0xDC ///< token telegram.
#define PB_ED 0x16  ///< end delimiter.

// Frame Control (FC) common values.
#define PB_FC_REQUEST_FDL_STATUS 0x49 ///< request FDL status: function code 9, request frame, FCB 0, FCV 0.
#define PB_FC_SRD_LOW 0x6C            ///< Send and Request Data low: function code 12, request frame, FCB 1, FCV 0.
#define PB_FC_SRD_HIGH 0x7D           ///< Send and Request Data high: function code 13, request frame, FCB 1, FCV 1.

/** @brief A parsed PROFIBUS telegram (data points into the input, null for SD1). */
typedef struct
{
    uint8_t sd; ///< the start delimiter (PB_SD1 / PB_SD2 / PB_SD3).
    uint8_t da;
    uint8_t sa;
    uint8_t fc;
    const uint8_t *data;
    size_t data_len;
} PbTelegram;

/** @brief What fcs takes: bytes, len. */
typedef struct
{
    const uint8_t *bytes;
    size_t len;
} ProfibusFcsArgs;

/** @brief What build_sd1 takes: da, sa, fc, out, cap. */
typedef struct
{
    uint8_t da;
    uint8_t sa;
    uint8_t fc;
    uint8_t *out;
    size_t cap;
} ProfibusBuildSd1Args;

/** @brief What build_sd2 takes: da, sa, fc, data, data_len, out, cap. */
typedef struct
{
    uint8_t da;
    uint8_t sa;
    uint8_t fc;
    const uint8_t *data; ///< the data unit
    size_t data_len;     ///< 1..246 (the DP process data); SD1 carries a telegram with no data field
    uint8_t *out;
    size_t cap;
} ProfibusBuildSd2Args;

/** @brief What build_sd3 takes: da, sa, fc, data, out, cap. */
typedef struct
{
    uint8_t da;
    uint8_t sa;
    uint8_t fc;
    const uint8_t *data;
    uint8_t *out;
    size_t cap;
} ProfibusBuildSd3Args;

/** @brief What parse takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    PbTelegram *out;
} ProfibusParseArgs;

/**
 * @brief PROFIBUS-DP FDL telegram codec (PROTOCORE_ENABLE_PROFIBUS).
 *
 * A caller sets the members a call takes, invokes it through ::Profibus with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Profibus.fcs_args.bytes = ...;
 *   Profibus.fcs_args.len = ...;
 *   Profibus.fcs(work);
 *   // Profibus.value is what the call reports
 *
 * @var ProfibusNs::fcs_args  what fcs takes: bytes, len
 * @var ProfibusNs::build_sd1_args  what build_sd1 takes: da, sa, fc, out, cap
 * @var ProfibusNs::build_sd2_args  what build_sd2 takes: da, sa, fc, data, data_len, out, cap
 * @var ProfibusNs::build_sd3_args  what build_sd3 takes: da, sa, fc, data, out, cap
 * @var ProfibusNs::parse_args  what parse takes: frame, len, out
 * @var ProfibusNs::ok  a call's true/false outcome
 * @var ProfibusNs::value  the value a call reports
 * @var ProfibusNs::n  the telegram length (6 + 3 + data_len... = 9 + data_len), or 0 on ...
 * @var ProfibusNs::fcs  PROFIBUS FCS: arithmetic sum (mod 256) of len bytes (DA + SA + FC + ...
 * @var ProfibusNs::build_sd1  build an SD1 (no-data) telegram: SD1 DA SA FC FCS ED. 6, or 0 on ...
 * @var ProfibusNs::build_sd2  build an SD2 (variable-data) telegram: SD2 LE LEr SD2 DA SA FC data ...
 * @var ProfibusNs::build_sd3  build an SD3 (fixed 8-octet data) telegram: SD3 DA SA FC data[8] ...
 * @var ProfibusNs::parse  validate + parse an SD1 / SD2 / SD3 telegram (FCS + ED checked). ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    ProfibusFcsArgs fcs_args;
    ProfibusBuildSd1Args build_sd1_args;
    ProfibusBuildSd2Args build_sd2_args;
    ProfibusBuildSd3Args build_sd3_args;
    ProfibusParseArgs parse_args;

    proto_bool ok;
    uint8_t value;
    size_t n;

    void (*const fcs)(uint8_t *restrict work);
    void (*const build_sd1)(uint8_t *restrict work);
    void (*const build_sd2)(uint8_t *restrict work);
    void (*const build_sd3)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
} ProfibusNs;

/** @brief The one symbol this module exports. */
extern ProfibusNs Profibus;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PROFIBUS

#endif // PROTOCORE_PROFIBUS_H
