// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iolink.h
 * @brief IO-Link (SDCI, IEC 61131-9) data-link message codec (PROTOCORE_ENABLE_IOLINK).
 *
 * IO-Link is the point-to-point 3-wire serial link to smart sensors / actuators. This codec
 * implements the data-link **message layer**: the M-sequence Control octet (MC), the
 * checksum / M-sequence-type octet (CKT) of a master message, the checksum / status octet
 * (CKS) of a device reply, and the SDCI message checksum that protects both directions.
 *
 * The checksum is the part everyone gets wrong, so it is implemented straight from the spec
 * (IO-Link Interface and System Specification v1.1.4, Annex A.1.6): a 0x52 seed XORed octet
 * by octet across the message (the check octet included with its checksum bits 0), then the
 * 8-to-6-bit compression of equation (A.1). `protocore_iol_finalize` writes it into the check octet and
 * `protocore_iol_verify` checks it.
 *
 * Scope: the message / DL layer. The per-type M-sequence octet layout (process + on-request
 * data widths) is the device's profile, and the ISDU on-request service framing layers on top;
 * lay those octets out per your device, then finalize / verify with this codec. The wire is a
 * UART at 4.8 / 38.4 / 230.4 kbit/s through an IO-Link transceiver (e.g. MAX14819 / L6360);
 * pure and host-tested.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_IOLINK_H
#define PROTOCORE_IOLINK_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_IOLINK

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define IOL_CHECKSUM_SEED 0x52u ///< checksum seed XORed with the first octet (spec A.1.6)

// M-sequence Control (MC) octet fields.
#define IOL_MC_READ 0x80u   ///< bit 7 set => read access
#define IOL_MC_WRITE 0x00u  ///< bit 7 clear => write access
#define IOL_CH_PROCESS 0u   ///< communication channel: Process Data
#define IOL_CH_PAGE 1u      ///< communication channel: Page (direct parameters)
#define IOL_CH_DIAGNOSIS 2u ///< communication channel: Diagnosis
#define IOL_CH_ISDU 3u      ///< communication channel: ISDU (on-request data)

// M-sequence types (CKT bits 7-6).
#define IOL_MSEQ_TYPE_0 0u
#define IOL_MSEQ_TYPE_1 1u
#define IOL_MSEQ_TYPE_2 2u

// Checksum / status (CKS) octet flags.
#define IOL_CKS_EVENT 0x80u      ///< bit 7: Device has an Event pending
#define IOL_CKS_PD_INVALID 0x40u ///< bit 6: Process Data invalid

#define IOL_CHECK_HIGH_MASK 0xC0u ///< the non-checksum (type / status) bits of a check octet
#define IOL_CHECK_SUM_MASK 0x3Fu  ///< the 6-bit checksum field of a check octet

/** @brief What mc takes: read, channel, address. */
typedef struct
{
    proto_bool read;
    uint8_t channel;
    uint8_t address;
} IolinkMcArgs;
/** @brief What mc_is_read takes: mc. */
typedef struct
{
    uint8_t mc;
} IolinkMcIsReadArgs;
/** @brief What mc_channel takes: mc. */
typedef struct
{
    uint8_t mc;
} IolinkMcChannelArgs;
/** @brief What mc_address takes: mc. */
typedef struct
{
    uint8_t mc;
} IolinkMcAddressArgs;
/** @brief What ckt takes: mseq_type, checksum6. */
typedef struct
{
    uint8_t mseq_type;
    uint8_t checksum6;
} IolinkCktArgs;
/** @brief What cks takes: event, pd_invalid, checksum6. */
typedef struct
{
    proto_bool event;
    proto_bool pd_invalid;
    uint8_t checksum6;
} IolinkCksArgs;
/** @brief What checksum6 takes: msg, len. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
} IolinkChecksum6Args;
/** @brief What finalize takes: msg, len, check_idx. */
typedef struct
{
    uint8_t *msg;
    size_t len;
    size_t check_idx;
} IolinkFinalizeArgs;
/** @brief What verify takes: msg, len, check_idx. */
typedef struct
{
    const uint8_t *msg;
    size_t len;
    size_t check_idx;
} IolinkVerifyArgs;
/**
 * @brief IO-Link (SDCI, IEC 61131-9) data-link message codec (PROTOCORE_ENABLE_IOLINK).
 *
 * A caller sets the members a call takes, invokes it through ::Iolink with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Iolink.mc_args.read = ...;
 *   Iolink.mc_args.channel = ...;
 *   Iolink.mc_args.address = ...;
 *   Iolink.mc(work);
 *   // Iolink.value is what the call reports
 *
 * @var IolinkNs::mc_args  what mc takes: read, channel, address
 * @var IolinkNs::mc_is_read_args  what mc_is_read takes: mc
 * @var IolinkNs::mc_channel_args  what mc_channel takes: mc
 * @var IolinkNs::mc_address_args  what mc_address takes: mc
 * @var IolinkNs::ckt_args  what ckt takes: mseq_type, checksum6
 * @var IolinkNs::cks_args  what cks takes: event, pd_invalid, checksum6
 * @var IolinkNs::checksum6_args  what checksum6 takes: msg, len
 * @var IolinkNs::finalize_args  what finalize takes: msg, len, check_idx
 * @var IolinkNs::verify_args  what verify takes: msg, len, check_idx
 * @var IolinkNs::ok  a call's true/false outcome
 * @var IolinkNs::value  the value a call reports
 * @var IolinkNs::mc  build the M-sequence Control octet from access / channel / address ...
 * @var IolinkNs::mc_is_read  true if the MC octet requests a read
 * @var IolinkNs::mc_channel  communication channel from an MC octet (IOL_CH_*)
 * @var IolinkNs::mc_address  address (5-bit) from an MC octet
 * @var IolinkNs::ckt  build a CKT octet from an M-sequence type and a 6-bit checksum (use ...
 * @var IolinkNs::cks  build a CKS octet from the Event / PD-invalid flags and a 6-bit ...
 * @var IolinkNs::checksum6  the compressed 6-bit SDCI checksum over msg (the check octet must ...
 * @var IolinkNs::finalize  finalize a message in place: compute the checksum over msg ...
 * @var IolinkNs::verify  verify a received message: recompute the checksum (masking off the ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    IolinkMcArgs mc_args;
    IolinkMcIsReadArgs mc_is_read_args;
    IolinkMcChannelArgs mc_channel_args;
    IolinkMcAddressArgs mc_address_args;
    IolinkCktArgs ckt_args;
    IolinkCksArgs cks_args;
    IolinkChecksum6Args checksum6_args;
    IolinkFinalizeArgs finalize_args;
    IolinkVerifyArgs verify_args;
    proto_bool ok;
    uint8_t value;
} IolinkVars;

/** @brief The operands and the outcome. */
extern IolinkVars IolinkV;

/** @brief The entries. */
typedef struct
{
    void (*const mc)(uint8_t *restrict work);
    void (*const mc_is_read)(uint8_t *restrict work);
    void (*const mc_channel)(uint8_t *restrict work);
    void (*const mc_address)(uint8_t *restrict work);
    void (*const ckt)(uint8_t *restrict work);
    void (*const cks)(uint8_t *restrict work);
    void (*const checksum6)(uint8_t *restrict work);
    void (*const finalize)(uint8_t *restrict work);
    void (*const verify)(uint8_t *restrict work);
} IolinkNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in IolinkV or a region of the borrow at a fixed offset.
void protocore_iolink_mc(uint8_t *restrict work);
void protocore_iolink_mc_is_read(uint8_t *restrict work);
void protocore_iolink_mc_channel(uint8_t *restrict work);
void protocore_iolink_mc_address(uint8_t *restrict work);
void protocore_iolink_ckt(uint8_t *restrict work);
void protocore_iolink_cks(uint8_t *restrict work);
void protocore_iolink_checksum6(uint8_t *restrict work);
void protocore_iolink_finalize(uint8_t *restrict work);
void protocore_iolink_verify(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Iolink.mc(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const IolinkNs Iolink __attribute__((unused)) = {
    .mc = protocore_iolink_mc,
    .mc_is_read = protocore_iolink_mc_is_read,
    .mc_channel = protocore_iolink_mc_channel,
    .mc_address = protocore_iolink_mc_address,
    .ckt = protocore_iolink_ckt,
    .cks = protocore_iolink_cks,
    .checksum6 = protocore_iolink_checksum6,
    .finalize = protocore_iolink_finalize,
    .verify = protocore_iolink_verify,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IOLINK

#endif // PROTOCORE_IOLINK_H
