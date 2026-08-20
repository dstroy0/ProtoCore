// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dmx.h
 * @brief DMX512 framing + RDM (ANSI E1.20) management codec (PROTOCORE_ENABLE_DMX).
 *
 * DMX512 (lighting / stage control over RS-485) is positional: after a break, a start code
 * octet (0x00 for dimmer data) is followed by up to 512 channel slots, with no checksum or
 * in-frame addressing. This codec assembles / reads that slot array, and implements **RDM**
 * (Remote Device Management, ANSI E1.20) - the addressed management layer that shares the
 * DMX wire: a real packet with 48-bit source / destination UIDs, a command class + parameter
 * id, and a 16-bit additive checksum.
 *
 * The break + RS-485 direction are the application's (a `MAX485`-class transceiver on a UART
 * at 250 kbit/s, 8N2). This is the byte-level framing layer. Pure and host-tested. Bridge a
 * lighting rig onto Wi-Fi: drive DMX slots or discover / configure RDM fixtures from the web.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DMX_H
#define PROTOCORE_DMX_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_DMX

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define DMX_MAX_CHANNELS 512u ///< slots per DMX512 universe

#define DMX_SC_DIMMER 0x00u ///< start code for standard dimmer data

#define RDM_SC 0xCCu ///< RDM start code (SC_RDM)

#define RDM_SUB_SC 0x01u ///< RDM sub-start code (SC_SUB_MESSAGE)

#define RDM_OVERHEAD 26u ///< full packet octets with PDL 0 (24-octet message + 2 checksum)

#define RDM_CC_DISCOVERY 0x10u

#define RDM_CC_DISCOVERY_RESPONSE 0x11u

#define RDM_CC_GET 0x20u

#define RDM_CC_GET_RESPONSE 0x21u

#define RDM_CC_SET 0x30u

#define RDM_CC_SET_RESPONSE 0x31u

#define RDM_RESPONSE_ACK 0x00u

#define RDM_RESPONSE_ACK_TIMER 0x01u

#define RDM_RESPONSE_NACK_REASON 0x02u

#define RDM_RESPONSE_ACK_OVERFLOW 0x03u

#define RDM_PID_DISC_UNIQUE_BRANCH 0x0001u

#define RDM_PID_DISC_MUTE 0x0002u

#define RDM_PID_DISC_UN_MUTE 0x0003u

#define RDM_PID_SUPPORTED_PARAMETERS 0x0050u

#define RDM_PID_DEVICE_INFO 0x0060u

#define RDM_PID_DMX_START_ADDRESS 0x00F0u

#define RDM_PID_IDENTIFY_DEVICE 0x1000u

#define PROTOCORE_RDM_DEVICE_INFO_PDL 19 ///< octets in a DEVICE_INFO (PID 0x0060) GET-response parameter block

/** @brief A parsed / to-be-built RDM packet. UIDs are 48-bit (manufacturer<<32 | device). */
typedef struct
{
    uint64_t dest_uid;
    uint64_t src_uid;
    uint8_t tn;           ///< transaction number
    uint8_t port_id;      ///< port id (request) / response type (response)
    uint8_t msg_count;    ///< queued message count
    uint16_t sub_device;  ///< sub-device (0 = root)
    uint8_t cc;           ///< command class (RDM_CC_*)
    uint16_t pid;         ///< parameter id
    uint8_t pdl;          ///< parameter data length
    const uint8_t *pdata; ///< parameter data (points into the parsed buffer); nullptr when pdl 0
} RdmPacket;
/** @brief Decoded DEVICE_INFO (PID 0x0060) parameter data - the descriptor every RDM responder must
 *  answer, carrying the fields a controller needs to patch and identify the device. */
typedef struct
{
    uint8_t proto_major;          ///< RDM protocol version major (1 for E1.20)
    uint8_t proto_minor;          ///< RDM protocol version minor
    uint16_t device_model_id;     ///< manufacturer-specific device model id
    uint16_t product_category;    ///< E1.20 product category code
    uint32_t software_version_id; ///< manufacturer-specific software version id
    uint16_t dmx_footprint;       ///< number of DMX512 slots the current personality occupies
    uint8_t current_personality;  ///< current DMX personality (1-based)
    uint8_t personality_count;    ///< total number of DMX personalities
    uint16_t dmx_start_address;   ///< DMX512 start address (1-512; 0xFFFF if the device uses no DMX)
    uint16_t sub_device_count;    ///< number of sub-devices (0 = none)
    uint8_t sensor_count;         ///< number of sensors
} RdmDeviceInfo;
/** @brief What build takes: buf, cap, start_code, channels, n. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint8_t start_code;
    const uint8_t *channels;
    uint16_t n;
} DmxBuildArgs;
/** @brief What get_channel takes: buf, len, ch. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    uint16_t ch;
} DmxGetChannelArgs;
/** @brief What rdm_uid takes: manufacturer, device. */
typedef struct
{
    uint16_t manufacturer;
    uint32_t device;
} DmxRdmUidArgs;
/** @brief What rdm_checksum takes: buf, len. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
} DmxRdmChecksumArgs;
/** @brief What rdm_build takes: buf, cap, p, pdata, pdl. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const RdmPacket *p;
    const uint8_t *pdata;
    uint8_t pdl;
} DmxRdmBuildArgs;
/** @brief What rdm_parse takes: buf, len, out, consumed. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    RdmPacket *out;
    size_t *consumed;
} DmxRdmParseArgs;
/** @brief What rdm_decode_disc_response takes: buf, len, uid. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    uint64_t *uid;
} DmxRdmDecodeDiscResponseArgs;
/** @brief What rdm_build_disc_response takes: buf, cap, uid, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint64_t uid;
    uint8_t preamble_len;
} DmxRdmBuildDiscResponseArgs;
/** @brief What rdm_build_device_info takes: pdata, cap, info. */
typedef struct
{
    uint8_t *pdata;
    size_t cap;
    const RdmDeviceInfo *info;
} DmxRdmBuildDeviceInfoArgs;
/** @brief What rdm_parse_device_info takes: pdata, pdl, out. */
typedef struct
{
    const uint8_t *pdata;
    uint8_t pdl;
    RdmDeviceInfo *out;
} DmxRdmParseDeviceInfoArgs;
/**
 * @brief DMX512 framing + RDM (ANSI E1.20) management codec (PROTOCORE_ENABLE_DMX). DMX512 (lighting / stage control
 * ...
 *
 * A caller sets the members a call takes, invokes it through ::Dmx with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Dmx.build_args.buf = ...;
 *   Dmx.build_args.cap = ...;
 *   Dmx.build_args.start_code = ...;
 *   Dmx.build_args.channels = ...;
 *   Dmx.build_args.n = ...;
 *   Dmx.build(work);
 *   // Dmx.n is what the call reports
 *
 * @var DmxNs::build_args  what build takes: buf, cap, start_code, channels, n
 * @var DmxNs::get_channel_args  what get_channel takes: buf, len, ch
 * @var DmxNs::rdm_uid_args  what rdm_uid takes: manufacturer, device
 * @var DmxNs::rdm_checksum_args  what rdm_checksum takes: buf, len
 * @var DmxNs::rdm_build_args  what rdm_build takes: buf, cap, p, pdata, pdl
 * @var DmxNs::rdm_parse_args  what rdm_parse takes: buf, len, out, consumed
 * @var DmxNs::rdm_decode_disc_response_args  what rdm_decode_disc_response takes: buf, len, uid
 * @var DmxNs::rdm_build_disc_response_args  what rdm_build_disc_response takes: buf, cap, uid,
 * @var DmxNs::rdm_build_device_info_args  what rdm_build_device_info takes: pdata, cap, info
 * @var DmxNs::rdm_parse_device_info_args  what rdm_parse_device_info takes: pdata, pdl, out
 * @var DmxNs::ok  true iff the separator is present, the 16 encoded octets fit, and ...
 * @var DmxNs::n  octets written (preamble_len + 17), or 0 on a null buffer, ...
 * @var DmxNs::u8  what a call reports
 * @var DmxNs::uid  what a call reports
 * @var DmxNs::checksum  what a call reports
 * @var DmxNs::build  assemble a DMX512 packet body: [start code][channel slots]. n <= ...
 * @var DmxNs::get_channel  read channel ch (1-based, per DMX convention) from a received ...
 * @var DmxNs::rdm_uid  compose a 48-bit RDM UID from a manufacturer id and a device id
 * @var DmxNs::rdm_checksum  16-bit additive checksum over len octets (RDM message block)
 * @var DmxNs::rdm_build  build a full RDM packet (incl. the trailing 16-bit checksum) from p ...
 * @var DmxNs::rdm_parse  parse an RDM packet: validates the start codes, the message length ...
 * @var DmxNs::rdm_decode_disc_response  decode a DISC_UNIQUE_BRANCH discovery response into the responder's ...
 * @var DmxNs::rdm_build_disc_response  build the DISC_UNIQUE_BRANCH discovery response a responder sends ...
 * @var DmxNs::rdm_build_device_info  pack a DEVICE_INFO (PID 0x0060) GET-response parameter block from ...
 * @var DmxNs::rdm_parse_device_info  decode a DEVICE_INFO (PID 0x0060) GET-response parameter block into ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    DmxBuildArgs build_args;
    DmxGetChannelArgs get_channel_args;
    DmxRdmUidArgs rdm_uid_args;
    DmxRdmChecksumArgs rdm_checksum_args;
    DmxRdmBuildArgs rdm_build_args;
    DmxRdmParseArgs rdm_parse_args;
    DmxRdmDecodeDiscResponseArgs rdm_decode_disc_response_args;
    DmxRdmBuildDiscResponseArgs rdm_build_disc_response_args;
    DmxRdmBuildDeviceInfoArgs rdm_build_device_info_args;
    DmxRdmParseDeviceInfoArgs rdm_parse_device_info_args;
    proto_bool ok;
    size_t n;
    uint8_t u8;
    uint64_t uid;
    uint16_t checksum;
} DmxVars;

/** @brief The operands and the outcome. */
extern DmxVars DmxV;

/** @brief The entries. */
typedef struct
{
    void (*const build)(uint8_t *restrict work);
    void (*const get_channel)(uint8_t *restrict work);
    void (*const rdm_uid)(uint8_t *restrict work);
    void (*const rdm_checksum)(uint8_t *restrict work);
    void (*const rdm_build)(uint8_t *restrict work);
    void (*const rdm_parse)(uint8_t *restrict work);
    void (*const rdm_decode_disc_response)(uint8_t *restrict work);
    void (*const rdm_build_disc_response)(uint8_t *restrict work);
    void (*const rdm_build_device_info)(uint8_t *restrict work);
    void (*const rdm_parse_device_info)(uint8_t *restrict work);
} DmxNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in DmxV or a region of the borrow at a fixed offset.
void protocore_dmx_build(uint8_t *restrict work);
void protocore_dmx_get_channel(uint8_t *restrict work);
void protocore_dmx_rdm_uid(uint8_t *restrict work);
void protocore_dmx_rdm_checksum(uint8_t *restrict work);
void protocore_dmx_rdm_build(uint8_t *restrict work);
void protocore_dmx_rdm_parse(uint8_t *restrict work);
void protocore_dmx_rdm_decode_disc_response(uint8_t *restrict work);
void protocore_dmx_rdm_build_disc_response(uint8_t *restrict work);
void protocore_dmx_rdm_build_device_info(uint8_t *restrict work);
void protocore_dmx_rdm_parse_device_info(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Dmx.build(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const DmxNs Dmx __attribute__((unused)) = {
    .build = protocore_dmx_build,
    .get_channel = protocore_dmx_get_channel,
    .rdm_uid = protocore_dmx_rdm_uid,
    .rdm_checksum = protocore_dmx_rdm_checksum,
    .rdm_build = protocore_dmx_rdm_build,
    .rdm_parse = protocore_dmx_rdm_parse,
    .rdm_decode_disc_response = protocore_dmx_rdm_decode_disc_response,
    .rdm_build_disc_response = protocore_dmx_rdm_build_disc_response,
    .rdm_build_device_info = protocore_dmx_rdm_build_device_info,
    .rdm_parse_device_info = protocore_dmx_rdm_parse_device_info,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DMX

#endif // PROTOCORE_DMX_H
