// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file directnet.h
 * @brief AutomationDirect / Koyo DirectNET serial frame codec (PROTOCORE_ENABLE_DIRECTNET).
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_DIRECTNET

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief DirectNET control bytes: wire values compared/emitted, so integer constants in a struct. */
#define DNET_ENQ 0x05
#define DNET_ACK 0x06
#define DNET_NAK 0x15
#define DNET_SOH 0x01
#define DNET_STX 0x02
#define DNET_ETX 0x03
#define DNET_ETB 0x17
#define DNET_EOT 0x04
#define DNET_READ 0x30  ///< request type: read ('0').
#define DNET_WRITE 0x38 ///< request type: write ('8').

/** @brief What lrc takes: bytes, len. */
typedef struct
{
    const uint8_t *bytes;
    size_t len;
} DirectnetLrcArgs;

/** @brief What header takes: slave, type, address, blocks, out, cap. */
typedef struct
{
    uint8_t slave;    ///< station number 0..99 (emitted as two ASCII-hex digits)
    uint8_t type;     ///< DNET_READ or DNET_WRITE
    uint16_t address; ///< V-memory octal address, emitted as 4 ASCII-hex digits
    uint8_t blocks;   ///< number of data blocks, emitted as 2 ASCII-hex digits
    uint8_t *out;
    size_t cap;
} DirectnetHeaderArgs;

/** @brief What data takes: data, data_len, out, cap. */
typedef struct
{
    const uint8_t *data;
    size_t data_len;
    uint8_t *out;
    size_t cap;
} DirectnetDataArgs;

/** @brief What data_parse takes: frame, len, data, data_len. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    const uint8_t **data;
    size_t *data_len;
} DirectnetDataParseArgs;

/**
 * @brief AutomationDirect / Koyo DirectNET serial frame codec (PROTOCORE_ENABLE_DIRECTNET).
 *
 * A caller sets the members a call takes, invokes it through ::Directnet with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Directnet.lrc_args.bytes = ...;
 *   Directnet.lrc_args.len = ...;
 *   Directnet.lrc(work);
 *   // Directnet.value is what the call reports
 *
 * @var DirectnetNs::lrc_args  what lrc takes: bytes, len
 * @var DirectnetNs::header_args  what header takes: slave, type, address, blocks, out, cap
 * @var DirectnetNs::data_args  what data takes: data, data_len, out, cap
 * @var DirectnetNs::data_parse_args  what data_parse takes: frame, len, data, data_len
 * @var DirectnetNs::ok  true if it is well-formed and the LRC matches; sets data / data_len ...
 * @var DirectnetNs::value  the value a call reports
 * @var DirectnetNs::n  the frame length, or 0 on overflow. The LRC covers slave..ETB
 * @var DirectnetNs::lrc  longitudinal XOR checksum (the DirectNET LRC) over len bytes
 * @var DirectnetNs::header  build a DirectNET header frame: SOH + ...
 * @var DirectnetNs::data  build a DirectNET data frame: STX + data + ETX + LRC. The LRC ...
 * @var DirectnetNs::data_parse  validate a DirectNET data frame (STX..ETX + LRC) and expose its ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    DirectnetLrcArgs lrc_args;
    DirectnetHeaderArgs header_args;
    DirectnetDataArgs data_args;
    DirectnetDataParseArgs data_parse_args;

    proto_bool ok;
    uint8_t value;
    size_t n;

    void (*const lrc)(uint8_t *restrict work);
    void (*const header)(uint8_t *restrict work);
    void (*const data)(uint8_t *restrict work);
    void (*const data_parse)(uint8_t *restrict work);
} DirectnetNs;

/** @brief The one symbol this module exports. */
extern DirectnetNs Directnet;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DIRECTNET

#endif // PROTOCORE_DIRECTNET_H
