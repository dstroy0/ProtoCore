// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bacnet.h
 * @brief BACnet/IP BVLC + NPDU codec (PROTOCORE_ENABLE_BACNET) - zero-heap framing for the
 *        ASHRAE 135 building-automation network layer over UDP (default port 47808).
 *
 * Two stacked layers:
 *  - BVLC (Annex J): `Type(1)=0x81  Function(1)  Length(2, big-endian, whole BVLL)` then the
 *    NPDU. Functions include Original-Unicast-NPDU (0x0A) and Original-Broadcast-NPDU (0x0B).
 *  - NPDU (Clause 6): `Version(1)=0x01  NPCI-Control(1)` then optional addressing. The NPCI
 *    control bits: 0x80 = network-layer message (else APDU), 0x20 = destination present
 *    (DNET(2) DLEN(1) DADR(DLEN); DLEN 0 = remote broadcast), 0x08 = source present (SNET(2)
 *    SLEN(1) SADR), 0x04 = expecting reply, low 2 bits = priority. A hop count octet follows
 *    the source fields when a destination is present. The APDU is whatever remains.
 *
 * The builders frame an APDU into a caller buffer (fail-closed); the parsers validate and
 * report the slices. Layout verified against ASHRAE 135 Annex J / Clause 6.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_BACNET_H
#define PROTOCORE_BACNET_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_BACNET

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define BVLC_TYPE_BIP 0x81 ///< BVLC Type: BACnet/IP
#define BVLC_HEADER_SIZE 4 ///< type + function + 2-octet length

// BVLC functions (Annex J).
#define BVLC_FUNC_RESULT 0x00
#define BVLC_FUNC_WRITE_BDT 0x01
#define BVLC_FUNC_FORWARDED_NPDU 0x04
#define BVLC_FUNC_REGISTER_FD 0x05
#define BVLC_FUNC_ORIGINAL_UNICAST 0x0A
#define BVLC_FUNC_ORIGINAL_BROADCAST 0x0B

#define NPDU_VERSION 0x01 ///< ASCII 1, the only defined protocol version

// NPCI control-octet bits (Clause 6.2.2).
#define NPCI_NETWORK_MSG 0x80     ///< NSDU is a network-layer message (else an APDU)
#define NPCI_DEST_PRESENT 0x20    ///< DNET / DLEN / DADR present
#define NPCI_SRC_PRESENT 0x08     ///< SNET / SLEN / SADR present
#define NPCI_EXPECTING_REPLY 0x04 ///< a reply is expected
#define NPCI_PRIORITY_MASK 0x03   ///< message priority (low 2 bits)

// Message priorities.
#define NPDU_PRIO_NORMAL 0x00
#define NPDU_PRIO_URGENT 0x01
#define NPDU_PRIO_CRITICAL 0x02
#define NPDU_PRIO_LIFE_SAFETY 0x03

// PDU types (the high nibble of the first APDU octet).
#define BACNET_PDU_CONFIRMED_REQUEST 0
#define BACNET_PDU_UNCONFIRMED_REQUEST 1
#define BACNET_PDU_SIMPLE_ACK 2
#define BACNET_PDU_COMPLEX_ACK 3
#define BACNET_PDU_SEGMENT_ACK 4
#define BACNET_PDU_ERROR 5
#define BACNET_PDU_REJECT 6
#define BACNET_PDU_ABORT 7

// PDU flags (the low nibble of the first octet, on confirmed-request / complex-ack).
#define BACNET_APDU_SEG 0x08 ///< the message is segmented
#define BACNET_APDU_MOR 0x04 ///< more segments follow
#define BACNET_APDU_SA 0x02  ///< the sender accepts a segmented response (confirmed-request only)

// Unconfirmed-request service choices (ASHRAE 135 §21).
#define BACNET_SVC_UN_I_AM 0   ///< I-Am
#define BACNET_SVC_UN_WHO_IS 8 ///< Who-Is

// Confirmed-request service choices (ASHRAE 135 §15).
#define BACNET_SVC_CONF_READ_PROPERTY 12 ///< ReadProperty

#define BACNET_MAX_INSTANCE 0x3FFFFFu ///< maximum BACnet object / device instance (22-bit)

// Object types (the 10-bit high field of a BACnetObjectIdentifier).
#define BACNET_OBJ_ANALOG_INPUT 0  ///< object type: Analog Input
#define BACNET_OBJ_ANALOG_OUTPUT 1 ///< object type: Analog Output
#define BACNET_OBJ_ANALOG_VALUE 2  ///< object type: Analog Value
#define BACNET_OBJ_BINARY_INPUT 3  ///< object type: Binary Input
#define BACNET_OBJ_BINARY_OUTPUT 4 ///< object type: Binary Output
#define BACNET_OBJ_BINARY_VALUE 5  ///< object type: Binary Value
#define BACNET_OBJ_DEVICE 8        ///< object type: Device (used in the I-Am object identifier)

// Common property identifiers (ASHRAE 135 §12).
#define BACNET_PROP_OBJECT_NAME 77   ///< object-name property
#define BACNET_PROP_PRESENT_VALUE 85 ///< present-value property

/** @brief A parsed NPDU. @ref apdu points INTO the source buffer. */
typedef struct
{
    uint8_t control;
    proto_bool network_message; ///< control & 0x80
    proto_bool dest_present;
    uint16_t dnet;
    proto_bool src_present;
    uint16_t snet;
    uint8_t hop_count; ///< valid when dest_present
    const uint8_t *apdu;
    size_t apdu_len;
} NpduInfo;

/** @brief A decoded APDU header (from Bacnet.apdu_parse). Service data points INTO the source buffer. */
typedef struct
{
    uint8_t pdu_type;            ///< PDU type (BACNET_PDU_*)
    proto_bool segmented;        ///< SEG flag (confirmed-request / complex-ack)
    proto_bool more_follows;     ///< MOR flag
    proto_bool sa;               ///< segmented-response-accepted flag (confirmed-request)
    uint8_t invoke_id;           ///< invoke id (confirmed-request / simple-ack / complex-ack)
    uint8_t service_choice;      ///< service choice
    const uint8_t *service_data; ///< the service parameters after the header, or nullptr if none
    size_t service_data_len;     ///< octets remaining after the header
} BacnetApdu;

/** @brief What bvlc_build takes: buf, cap, function, npdu, npdu_len. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint8_t function;
    const uint8_t *npdu;
    size_t npdu_len;
} BacnetBvlcBuildArgs;

/** @brief What bvlc_parse takes: buf, len, function, npdu, npdu_len. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    uint8_t *function;
    const uint8_t **npdu;
    size_t *npdu_len;
} BacnetBvlcParseArgs;

/** @brief What npdu_build takes: buf, cap, expecting_reply, priority, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    proto_bool expecting_reply;
    uint8_t priority;
    proto_bool has_dest;
    uint16_t dnet;
    const uint8_t *dadr;
    uint8_t dadr_len;
    uint8_t hop_count;
    const uint8_t *apdu;
    size_t apdu_len;
} BacnetNpduBuildArgs;

/** @brief What npdu_parse takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    NpduInfo *out;
} BacnetNpduParseArgs;

/** @brief What apdu_parse takes: apdu, len, out. */
typedef struct
{
    const uint8_t *apdu;
    size_t len;
    BacnetApdu *out;
} BacnetApduParseArgs;

/** @brief What apdu_build_who_is takes: buf, cap, low_limit, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint32_t low_limit;
    uint32_t high_limit;
    proto_bool has_limits;
} BacnetApduBuildWhoIsArgs;

/** @brief What apdu_build_i_am takes: buf, cap, device_instance, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint32_t device_instance;
    uint32_t max_apdu;
    uint8_t segmentation;
    uint16_t vendor_id;
} BacnetApduBuildIAmArgs;

/** @brief What apdu_build_read_property takes: buf, cap, invoke_id, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint8_t invoke_id;
    uint8_t max_resp;
    uint16_t object_type;
    uint32_t object_instance;
    uint32_t property_id;
} BacnetApduBuildReadPropertyArgs;

/**
 * @brief BACnet/IP BVLC + NPDU codec (PROTOCORE_ENABLE_BACNET) - zero-heap framing for the ASHRAE 135
 * building-automation network layer over UDP (default port 47808).
 *
 * A caller sets the members a call takes, invokes it through ::Bacnet with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Bacnet.bvlc_build_args.buf = ...;
 *   Bacnet.bvlc_build_args.cap = ...;
 *   Bacnet.bvlc_build_args.function = ...;
 *   Bacnet.bvlc_build_args.npdu = ...;
 *   Bacnet.bvlc_build_args.npdu_len = ...;
 *   Bacnet.bvlc_build(work);
 *   // Bacnet.n is what the call reports
 *
 * @var BacnetNs::bvlc_build_args  what bvlc_build takes: buf, cap, function, npdu, npdu_len
 * @var BacnetNs::bvlc_parse_args  what bvlc_parse takes: buf, len, function, npdu, npdu_len
 * @var BacnetNs::npdu_build_args  what npdu_build takes: buf, cap, expecting_reply, priority,
 * @var BacnetNs::npdu_parse_args  what npdu_parse takes: buf, len, out
 * @var BacnetNs::apdu_parse_args  what apdu_parse takes: apdu, len, out
 * @var BacnetNs::apdu_build_who_is_args  what apdu_build_who_is takes: buf, cap, low_limit,
 * @var BacnetNs::apdu_build_i_am_args  what apdu_build_i_am takes: buf, cap, device_instance,
 * @var BacnetNs::apdu_build_read_property_args  what apdu_build_read_property takes: buf, cap, invoke_id,
 * @var BacnetNs::ok  true iff len covers the header for a supported PDU type (confirmed ...
 * @var BacnetNs::n  the APDU length, or 0 on overflow, a limit above ...
 * @var BacnetNs::bvlc_build  wrap an NPDU in a BVLC envelope. Returns total octets, or 0 on ...
 * @var BacnetNs::bvlc_parse  parse a BVLC envelope; reports the function and the NPDU slice
 * @var BacnetNs::npdu_build  build an NPDU carrying apdu. With has_dest, the destination ...
 * @var BacnetNs::npdu_parse  parse + validate an NPDU (version, control, optional addressing) ...
 * @var BacnetNs::apdu_parse  decode an APDU header (PDU type, flags, invoke id, service choice) ...
 * @var BacnetNs::apdu_build_who_is  build a Who-Is unconfirmed-request APDU (service choice 8). With ...
 * @var BacnetNs::apdu_build_i_am  build an I-Am unconfirmed-request APDU (service choice 0) - a ...
 * @var BacnetNs::apdu_build_read_property  build a ReadProperty confirmed-request APDU (service choice 12) - ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    BacnetBvlcBuildArgs bvlc_build_args;
    BacnetBvlcParseArgs bvlc_parse_args;
    BacnetNpduBuildArgs npdu_build_args;
    BacnetNpduParseArgs npdu_parse_args;
    BacnetApduParseArgs apdu_parse_args;
    BacnetApduBuildWhoIsArgs apdu_build_who_is_args;
    BacnetApduBuildIAmArgs apdu_build_i_am_args;
    BacnetApduBuildReadPropertyArgs apdu_build_read_property_args;

    proto_bool ok;
    size_t n;

    void (*const bvlc_build)(uint8_t *restrict work);
    void (*const bvlc_parse)(uint8_t *restrict work);
    void (*const npdu_build)(uint8_t *restrict work);
    void (*const npdu_parse)(uint8_t *restrict work);
    void (*const apdu_parse)(uint8_t *restrict work);
    void (*const apdu_build_who_is)(uint8_t *restrict work);
    void (*const apdu_build_i_am)(uint8_t *restrict work);
    void (*const apdu_build_read_property)(uint8_t *restrict work);
} BacnetNs;

/** @brief The one symbol this module exports. */
extern BacnetNs Bacnet;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_BACNET

#endif // PROTOCORE_BACNET_H
