// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file zwave.h
 * @brief Z-Wave Serial API frame codec (PROTOCORE_ENABLE_ZWAVE) - Silicon Labs controller.
 *
 * The host-side Serial API of a Silicon Labs 500 / 700-series Z-Wave controller reached
 * over UART: a Z-Wave mesh bridged to the web. The host and the controller exchange **data
 * frames**:
 *
 *   SOF (0x01) | LEN | Type | Command | Data... | Checksum
 *
 * where LEN counts Type + Command + Data + Checksum, Type is 0x00 (REQ) or 0x01 (RES), and
 * the checksum is 0xFF XOR-folded over LEN through the last Data byte. Each data frame is
 * acknowledged by a single-byte **ACK (0x06)**, or rejected with **NAK (0x15)** / **CAN
 * (0x18)**.
 *
 * protocore_zwave_build_frame() assembles a data frame carrying a function command, protocore_zwave_parse_frame()
 * frames + verifies one, and protocore_zwave_is_ack() / protocore_zwave_is_nak() / protocore_zwave_is_can() /
 * protocore_zwave_build_ack() handle the flow-control bytes. The per-command payload (GetVersion,
 * SendData, AddNodeToNetwork, an ApplicationCommandHandler report, ...) is the application's.
 * Pure - you carry the bytes over your UART - so it is fully host-testable.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ZWAVE_H
#define PROTOCORE_ZWAVE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_ZWAVE

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Z-Wave Serial API control bytes / frame markers. */
#define ZWAVE_SOF 0x01 ///< start of a data frame
#define ZWAVE_ACK 0x06 ///< frame acknowledged
#define ZWAVE_NAK 0x15 ///< frame rejected (checksum)
#define ZWAVE_CAN 0x18 ///< frame cancelled (retransmit)

/** @brief Data-frame type. */
typedef enum PROTO_ENUM_PACKED
{
    ZWAVE_REQ = 0x00, ///< request
    ZWAVE_RES = 0x01, ///< response
} protocore_zwave_type;

/** @brief What build_frame takes: type, cmd, data, data_len, out, cap. */
typedef struct
{
    protocore_zwave_type type;
    uint8_t cmd;
    const uint8_t *data;
    uint8_t data_len;
    uint8_t *out;
    uint16_t cap;
} ZwaveBuildFrameArgs;
/** @brief What parse_frame takes: raw, len, type, cmd, pdata, ... */
typedef struct
{
    const uint8_t *raw;
    uint16_t len;
    uint8_t *type;
    uint8_t *cmd;
    const uint8_t **pdata;
    uint8_t *pdata_len;
} ZwaveParseFrameArgs;
/** @brief What is_ack takes: b. */
typedef struct
{
    uint8_t b;
} ZwaveIsAckArgs;
/** @brief What is_nak takes: b. */
typedef struct
{
    uint8_t b;
} ZwaveIsNakArgs;
/** @brief What is_can takes: b. */
typedef struct
{
    uint8_t b;
} ZwaveIsCanArgs;
/** @brief What build_ack takes: out, cap. */
typedef struct
{
    uint8_t *out;
    uint16_t cap;
} ZwaveBuildAckArgs;
/**
 * @brief Z-Wave Serial API frame codec (PROTOCORE_ENABLE_ZWAVE) - Silicon Labs controller.
 *
 * A caller sets the members a call takes, invokes it through ::Zwave with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Zwave.build_frame_args.type = ...;
 *   Zwave.build_frame_args.cmd = ...;
 *   Zwave.build_frame_args.data = ...;
 *   Zwave.build_frame_args.data_len = ...;
 *   Zwave.build_frame_args.out = ...;
 *   Zwave.build_frame_args.cap = ...;
 *   Zwave.build_frame(work);
 *   // Zwave.value is what the call reports
 *
 * @var ZwaveNs::build_frame_args  what build_frame takes: type, cmd, data, data_len, out, cap
 * @var ZwaveNs::parse_frame_args  what parse_frame takes: raw, len, type, cmd, pdata,
 * @var ZwaveNs::is_ack_args  what is_ack takes: b
 * @var ZwaveNs::is_nak_args  what is_nak takes: b
 * @var ZwaveNs::is_can_args  what is_can takes: b
 * @var ZwaveNs::build_ack_args  what build_ack takes: out, cap
 * @var ZwaveNs::ok  a call's true/false outcome
 * @var ZwaveNs::value  the total frame length, or 0 if it would not fit cap or data_len ...
 * @var ZwaveNs::n  the frame length consumed (> 0), 0 if more bytes are needed, or -1 ...
 * @var ZwaveNs::build_frame  assemble a data frame carrying type + cmd + data into out
 * @var ZwaveNs::parse_frame  frame one data frame from the front of raw and verify the checksum
 * @var ZwaveNs::is_ack  true if b is the ACK control byte
 * @var ZwaveNs::is_nak  true if b is the NAK control byte
 * @var ZwaveNs::is_can  true if b is the CAN control byte
 * @var ZwaveNs::build_ack  write the single ACK byte into out. 1, or 0 if cap < 1
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    ZwaveBuildFrameArgs build_frame_args;
    ZwaveParseFrameArgs parse_frame_args;
    ZwaveIsAckArgs is_ack_args;
    ZwaveIsNakArgs is_nak_args;
    ZwaveIsCanArgs is_can_args;
    ZwaveBuildAckArgs build_ack_args;
    proto_bool ok;
    uint16_t value;
    int n;
} ZwaveVars;

/** @brief The operands and the outcome. */
extern ZwaveVars ZwaveV;

/** @brief The entries. */
typedef struct
{
    void (*const build_frame)(uint8_t *restrict work);
    void (*const parse_frame)(uint8_t *restrict work);
    void (*const is_ack)(uint8_t *restrict work);
    void (*const is_nak)(uint8_t *restrict work);
    void (*const is_can)(uint8_t *restrict work);
    void (*const build_ack)(uint8_t *restrict work);
} ZwaveNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in ZwaveV or a region of the borrow at a fixed offset.
void protocore_zwave_build_frame(uint8_t *restrict work);
void protocore_zwave_parse_frame(uint8_t *restrict work);
void protocore_zwave_is_ack(uint8_t *restrict work);
void protocore_zwave_is_nak(uint8_t *restrict work);
void protocore_zwave_is_can(uint8_t *restrict work);
void protocore_zwave_build_ack(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Zwave.build_frame(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const ZwaveNs Zwave __attribute__((unused)) = {
    .build_frame = protocore_zwave_build_frame,
    .parse_frame = protocore_zwave_parse_frame,
    .is_ack = protocore_zwave_is_ack,
    .is_nak = protocore_zwave_is_nak,
    .is_can = protocore_zwave_is_can,
    .build_ack = protocore_zwave_build_ack,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ZWAVE

#endif // PROTOCORE_ZWAVE_H
