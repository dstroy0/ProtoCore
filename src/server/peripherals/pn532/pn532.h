// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pn532.h
 * @brief PN532 NFC frame codec (PROTOCORE_ENABLE_PN532) - NXP PN532 NFC/RFID controller.
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
 * protocore_pn532_build_frame() assembles a frame carrying a command + parameters, protocore_pn532_parse_frame()
 * frames + verifies a response, and protocore_pn532_is_ack() detects the ACK. The per-command PData
 * (GetFirmwareVersion, InListPassiveTarget, InDataExchange, ...) is the application's. Pure -
 * you carry the bytes over your I2C / SPI / UART - so it is fully host-testable.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PN532_H
#define PROTOCORE_PN532_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_PN532

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define PN532_TFI_HOST 0xD4

#define PN532_TFI_PN532 0xD5

/** @brief What build_frame takes: tfi, data, len, out, cap. */
typedef struct
{
    uint8_t tfi;
    const uint8_t *data;
    uint8_t len;
    uint8_t *out;
    uint16_t cap;
} Pn532BuildFrameArgs;
/** @brief What parse_frame takes: raw, len, tfi, pdata, pdata_len. */
typedef struct
{
    const uint8_t *raw;
    uint16_t len;
    uint8_t *tfi;
    const uint8_t **pdata;
    uint8_t *pdata_len;
} Pn532ParseFrameArgs;
/** @brief What is_ack takes: raw, len. */
typedef struct
{
    const uint8_t *raw;
    uint16_t len;
} Pn532IsAckArgs;
/** @brief What build_ack takes: out, cap. */
typedef struct
{
    uint8_t *out;
    uint16_t cap;
} Pn532BuildAckArgs;
/**
 * @brief PN532 NFC frame codec (PROTOCORE_ENABLE_PN532) - NXP PN532 NFC/RFID controller. The command-frame protocol of
 * ...
 *
 * A caller sets the members a call takes, invokes it through ::Pn532 with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Pn532.build_frame_args.tfi = ...;
 *   Pn532.build_frame_args.data = ...;
 *   Pn532.build_frame_args.len = ...;
 *   Pn532.build_frame_args.out = ...;
 *   Pn532.build_frame_args.cap = ...;
 *   Pn532.build_frame(work);
 *   // Pn532.len is what the call reports
 *
 * @var Pn532Ns::build_frame_args  what build_frame takes: tfi, data, len, out, cap
 * @var Pn532Ns::parse_frame_args  what parse_frame takes: raw, len, tfi, pdata, pdata_len
 * @var Pn532Ns::is_ack_args  what is_ack takes: raw, len
 * @var Pn532Ns::build_ack_args  what build_ack takes: out, cap
 * @var Pn532Ns::ok  a call's true/false outcome
 * @var Pn532Ns::len  the total frame length, or 0 if it would not fit cap or len exceeds ...
 * @var Pn532Ns::n  the frame length consumed (> 0), 0 if more bytes are needed, or -1 ...
 * @var Pn532Ns::build_frame  assemble a normal information frame carrying tfi + data into out
 * @var Pn532Ns::parse_frame  frame one normal information frame from the front of raw and verify ...
 * @var Pn532Ns::is_ack  true if raw starts with a PN532 ACK frame (00 00 FF 00 FF 00)
 * @var Pn532Ns::build_ack  write the 6-byte ACK frame into out. 6, or 0 if cap < 6
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    Pn532BuildFrameArgs build_frame_args;
    Pn532ParseFrameArgs parse_frame_args;
    Pn532IsAckArgs is_ack_args;
    Pn532BuildAckArgs build_ack_args;
    proto_bool ok;
    uint16_t len;
    int n;
} Pn532Vars;

/** @brief The operands and the outcome. */
extern Pn532Vars Pn532V;

/** @brief The entries. */
typedef struct
{
    void (*const build_frame)(uint8_t *restrict work);
    void (*const parse_frame)(uint8_t *restrict work);
    void (*const is_ack)(uint8_t *restrict work);
    void (*const build_ack)(uint8_t *restrict work);
} Pn532Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Pn532V or a region of the borrow at a fixed offset.
void protocore_pn532_build_frame(uint8_t *restrict work);
void protocore_pn532_parse_frame(uint8_t *restrict work);
void protocore_pn532_is_ack(uint8_t *restrict work);
void protocore_pn532_build_ack(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Pn532.build_frame(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Pn532Ns Pn532 __attribute__((unused)) = {
    .build_frame = protocore_pn532_build_frame,
    .parse_frame = protocore_pn532_parse_frame,
    .is_ack = protocore_pn532_is_ack,
    .build_ack = protocore_pn532_build_ack,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PN532

#endif // PROTOCORE_PN532_H
