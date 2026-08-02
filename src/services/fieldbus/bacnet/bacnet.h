// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bacnet.h
 * @brief BACnet/IP BVLC + NPDU codec (PC_ENABLE_BACNET) - zero-heap framing for the
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

#include "protocore_config.h"

#if PC_ENABLE_BACNET

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

// ---- BVLC ----

/** @brief Wrap an NPDU in a BVLC envelope. Returns total octets, or 0 on overflow. */
size_t pc_bvlc_build(uint8_t *buf, size_t cap, uint8_t function, const uint8_t *npdu, size_t pc_npdu_len);

/** @brief Parse a BVLC envelope; reports the function and the NPDU slice. */
proto_bool pc_bvlc_parse(const uint8_t *buf, size_t len, uint8_t *function, const uint8_t **npdu, size_t *pc_npdu_len);

// ---- NPDU ----

/**
 * @brief Build an NPDU carrying @p apdu. With @p has_dest, the destination addressing
 *        (DNET / DLEN / DADR) and the hop count are emitted (DLEN 0 + @p dnet 0xFFFF is a
 *        remote/global broadcast).
 */
size_t pc_npdu_build(uint8_t *buf, size_t cap, proto_bool expecting_reply, uint8_t priority, proto_bool has_dest,
                     uint16_t dnet, const uint8_t *dadr, uint8_t dadr_len, uint8_t hop_count, const uint8_t *apdu,
                     size_t apdu_len);

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

/** @brief Parse + validate an NPDU (version, control, optional addressing) and slice the APDU. */
proto_bool pc_npdu_parse(const uint8_t *buf, size_t len, NpduInfo *out);

// --- APDU header (the application layer sliced out by pc_npdu_parse) ---

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

/** @brief A decoded APDU header (from pc_apdu_parse). Service data points INTO the source buffer. */
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

/**
 * @brief Decode an APDU header (PDU type, flags, invoke id, service choice) and slice the service data.
 * @return true iff @p len covers the header for a supported PDU type (confirmed / unconfirmed request,
 *         simple / complex ACK); false for a short buffer or an unsupported type (segment-ack / error /
 *         reject / abort).
 */
proto_bool pc_apdu_parse(const uint8_t *apdu, size_t len, BacnetApdu *out);

/**
 * @brief Build a Who-Is unconfirmed-request APDU (service choice 8). With @p has_limits, the device-instance
 *        search range is appended as context-tagged unsigned ints (tag 0 = low limit, tag 1 = high limit, each
 *        encoded minimal-length); without it, the APDU is the 2-octet unbounded form that every device answers.
 * @return the APDU length, or 0 on overflow, a limit above BACNET_MAX_INSTANCE, or low > high.
 */
size_t pc_apdu_build_who_is(uint8_t *buf, size_t cap, uint32_t low_limit, uint32_t high_limit, proto_bool has_limits);

/**
 * @brief Build an I-Am unconfirmed-request APDU (service choice 0) - a device's answer to Who-Is. Carries the
 *        device object identifier (@p device_instance, object type Device), the max APDU length accepted, the
 *        segmentation-supported enumeration (0..3), and the vendor id, each as an application-tagged value.
 * @return the APDU length, or 0 on overflow, @p device_instance above BACNET_MAX_INSTANCE, or @p segmentation > 3.
 */
size_t pc_apdu_build_i_am(uint8_t *buf, size_t cap, uint32_t device_instance, uint32_t max_apdu, uint8_t segmentation,
                          uint16_t vendor_id);

/**
 * @brief Build a ReadProperty confirmed-request APDU (service choice 12) - the BACnet workhorse a client sends to
 *        read one property of one object. Frames the confirmed-request header (unsegmented; @p invoke_id and the
 *        @p max_resp octet - the max-segments-accepted / max-APDU-length-accepted field the peer echoes limits
 *        against), then the object identifier as context tag 0 ((@p object_type << 22) | @p object_instance, a
 *        4-octet field) and the property identifier as context tag 1 (an enumerated value, minimal-length). The
 *        optional property-array-index (context tag 2) is not emitted - it applies only to array-typed properties.
 * @return the APDU length, or 0 on overflow, @p object_instance above BACNET_MAX_INSTANCE, or @p object_type > 0x3FF.
 */
size_t pc_apdu_build_read_property(uint8_t *buf, size_t cap, uint8_t invoke_id, uint8_t max_resp, uint16_t object_type,
                                   uint32_t object_instance, uint32_t property_id);

#endif // PC_ENABLE_BACNET

#endif // PROTOCORE_BACNET_H
