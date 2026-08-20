// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file melsec.h
 * @brief Mitsubishi MELSEC MC protocol (binary 3E frame) codec (PROTOCORE_ENABLE_MELSEC) -
 *        zero-heap batch-read request builder + response parser for MELSEC PLCs over TCP/UDP.
 *
 * The QnA-compatible binary 3E request frame (all multi-octet fields LITTLE-endian, unlike
 * the big-endian PLC protocols):
 * @code
 *   50 00            subheader (request)
 *   00               network number
 *   FF               PC (station) number
 *   FF 03            request destination module I/O number (0x03FF)
 *   00               request destination multidrop station
 *   LL LL            request data length (the octets from the monitoring timer on)
 *   TT TT            monitoring timer
 *   01 04            command (0x0401 batch read)
 *   00 00            subcommand (0x0000 word units)
 *   dd dd dd         head device number (3 octets)
 *   CC               device code (D = 0xA8, M = 0x90, ...)
 *   pp pp            number of device points
 * @endcode
 * The response subheader is `D0 00`, followed by the same routing, a 2-octet length, a
 * 2-octet end code (0x0000 = success), then the read data (2 octets per word point).
 *
 * Frame layout + device codes verified against a third-party MC-protocol implementation.
 * This is the codec; the TCP/UDP send is the application's.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MELSEC_H
#define PROTOCORE_MELSEC_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_MELSEC

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define MELSEC_3E_REQ_SUBHEADER0 0x50 ///< request subheader (sent 0x50 then 0x00)
#define MELSEC_3E_REQ_SUBHEADER1 0x00
#define MELSEC_3E_RES_SUBHEADER0 0xD0 ///< response subheader (0xD0 0x00)
#define MELSEC_3E_RES_SUBHEADER1 0x00

#define MELSEC_NETWORK_DEFAULT 0x00   ///< local network
#define MELSEC_PROTOCORE_DEFAULT 0xFF ///< host station
#define MELSEC_DEST_IO_DEFAULT 0x03FF ///< own station CPU
#define MELSEC_DEST_MULTIDROP_DEFAULT 0x00

#define MELSEC_CMD_BATCH_READ 0x0401  ///< batch read
#define MELSEC_CMD_BATCH_WRITE 0x1401 ///< batch write
#define MELSEC_SUBCMD_WORD 0x0000     ///< word units
#define MELSEC_SUBCMD_BIT 0x0001      ///< bit units

// Device (area) codes for the binary 3E frame.
#define MELSEC_DEV_D 0xA8  ///< data register (D)
#define MELSEC_DEV_R 0xAF  ///< file/extension register (R)
#define MELSEC_DEV_M 0x90  ///< auxiliary relay (M)
#define MELSEC_DEV_S 0x98  ///< state (S)
#define MELSEC_DEV_X 0x9C  ///< input (X)
#define MELSEC_DEV_Y 0x9D  ///< output (Y)
#define MELSEC_DEV_TN 0xC2 ///< timer current value (TN)
#define MELSEC_DEV_TS 0xC1 ///< timer contact (TS)
#define MELSEC_DEV_CN 0xC5 ///< counter current value (CN)
#define MELSEC_DEV_CS 0xC4 ///< counter contact (CS)

#define MELSEC_ENDCODE_OK 0x0000 ///< response end code: success

// Binary 3E frame geometry (octet offsets and fixed lengths).
#define MELSEC_3E_READ_REQ_LEN 21      ///< total octets in a batch-read request frame
#define MELSEC_3E_READ_REQ_DATA_LEN 12 ///< batch-read request data-length field: timer..points
#define MELSEC_3E_RES_MIN_LEN 11       ///< shortest valid response: subheader through end code
#define MELSEC_3E_RES_LEN_OFFSET 7     ///< offset of the response data-length field
#define MELSEC_3E_RES_DATALEN_BASE 9   ///< offset where the data-length-counted region (end code) begins
#define MELSEC_3E_RES_DATA_OFFSET 11   ///< offset of the response read data (after the end code)
#define MELSEC_ENDCODE_LEN 2           ///< end-code octets (counted inside the data length)

/** @brief A parsed 3E response. @ref data points INTO the source buffer (LE word values). */
typedef struct
{
    uint16_t end_code;   ///< 0x0000 on success
    const uint8_t *data; ///< response payload (empty on error)
    size_t data_len;
} MelsecResponse;
/** @brief What build_read takes: buf, cap, device_code, head_device, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint8_t device_code;       ///< MELSEC_DEV_* (or a raw device code)
    uint32_t head_device;      ///< starting device number (24-bit)
    uint16_t points;           ///< number of word points to read
    uint16_t monitoring_timer; ///< the CPU monitoring timer (units of 250 ms; 0 = wait indefinitely)
} MelsecBuildReadArgs;
/** @brief What build_write takes: buf, cap, device_code, head_device, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint8_t device_code;
    uint32_t head_device;
    uint16_t points;
    uint16_t monitoring_timer;
    const uint8_t *data;
    size_t data_len;
} MelsecBuildWriteArgs;
/** @brief What parse_response takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    MelsecResponse *out;
} MelsecParseResponseArgs;
/**
 * @brief Mitsubishi MELSEC MC protocol (binary 3E frame) codec (PROTOCORE_ENABLE_MELSEC) - zero-heap batch-read request
 * builder + response parser for MELSEC PLCs over TCP/UDP.
 *
 * A caller sets the members a call takes, invokes it through ::Melsec with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Melsec.build_read_args.buf = ...;
 *   Melsec.build_read_args.cap = ...;
 *   Melsec.build_read_args.device_code = ...;
 *   Melsec.build_read_args.head_device = ...;
 *   Melsec.build_read_args.points = ...;
 *   Melsec.build_read_args.monitoring_timer = ...;
 *   Melsec.build_read(work);
 *   // Melsec.n is what the call reports
 *
 * @var MelsecNs::build_read_args  what build_read takes: buf, cap, device_code, head_device,
 * @var MelsecNs::build_write_args  what build_write takes: buf, cap, device_code, head_device,
 * @var MelsecNs::parse_response_args  what parse_response takes: buf, len, out
 * @var MelsecNs::ok  a call's true/false outcome
 * @var MelsecNs::n  total octets written (21), or 0 on overflow / bad input
 * @var MelsecNs::build_read  build a binary 3E batch-read (word units) request
 * @var MelsecNs::build_write  build a binary 3E batch-write (word units) request: the same device ...
 * @var MelsecNs::parse_response  parse + validate a binary 3E response (subheader 0xD0 0x00, length, ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    MelsecBuildReadArgs build_read_args;
    MelsecBuildWriteArgs build_write_args;
    MelsecParseResponseArgs parse_response_args;
    proto_bool ok;
    size_t n;
} MelsecVars;

/** @brief The operands and the outcome. */
extern MelsecVars MelsecV;

/** @brief The entries. */
typedef struct
{
    void (*const build_read)(uint8_t *restrict work);
    void (*const build_write)(uint8_t *restrict work);
    void (*const parse_response)(uint8_t *restrict work);
} MelsecNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in MelsecV or a region of the borrow at a fixed offset.
void protocore_melsec_build_read(uint8_t *restrict work);
void protocore_melsec_build_write(uint8_t *restrict work);
void protocore_melsec_parse_response(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Melsec.build_read(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const MelsecNs Melsec __attribute__((unused)) = {
    .build_read = protocore_melsec_build_read,
    .build_write = protocore_melsec_build_write,
    .parse_response = protocore_melsec_parse_response,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MELSEC

#endif // PROTOCORE_MELSEC_H
