// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file enip.h
 * @brief EtherNet/IP encapsulation codec (PROTOCORE_ENABLE_ENIP) - zero-heap builder + parser
 *        for the ODVA EtherNet/IP encapsulation layer (TCP/UDP 44818), the transport that
 *        carries CIP. The reusable base for CIP / EtherNet/IP explicit messaging.
 *
 * The 24-octet encapsulation header (all fields LITTLE-endian):
 * @code
 *   Command(2) Length(2) SessionHandle(4) Status(4) SenderContext(8) Options(4)  <data>
 * @endcode
 * Length is the octet count of the command-specific data that follows the header.
 *  - RegisterSession (0x0065): data = protocol version(2)=1 + options flags(2)=0; the reply
 *    carries the assigned session handle.
 *  - SendRRData (0x006F): data = interface handle(4)=0 + timeout(2) + a Common Packet Format
 *    block: item count(2), then items (Type ID(2), Length(2), data). Unconnected explicit
 *    messaging uses a Null Address item (0x0000) then an Unconnected Data item (0x00B2)
 *    carrying the CIP request/response.
 *
 * Commands + CPF item types verified against the Wireshark ENIP dissector. This codec frames
 * the encapsulation; the CIP message inside the Unconnected Data item is the application's.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ENIP_H
#define PROTOCORE_ENIP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_ENIP

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define EIP_HEADER_SIZE 24

// Encapsulation commands.
#define EIP_CMD_LIST_SERVICES 0x0004
#define EIP_CMD_LIST_IDENTITY 0x0063
#define EIP_CMD_LIST_INTERFACES 0x0064
#define EIP_CMD_REGISTER_SESSION 0x0065
#define EIP_CMD_UNREGISTER_SESSION 0x0066
#define EIP_CMD_SEND_RR_DATA 0x006F
#define EIP_CMD_SEND_UNIT_DATA 0x0070

#define EIP_STATUS_SUCCESS 0x00000000u

// Common Packet Format item type ids.
#define EIP_CPF_NULL 0x0000              ///< null address item
#define EIP_CPF_CONNECTED_ADDRESS 0x00A1 ///< connected address item
#define EIP_CPF_CONNECTED_DATA 0x00B1    ///< connected data item
#define EIP_CPF_UNCONNECTED_DATA 0x00B2  ///< unconnected data item (carries the CIP message)
#define EIP_CPF_LIST_IDENTITY 0x000C     ///< List Identity response item (device identity)

/** @brief The 24-octet encapsulation header. */
typedef struct
{
    uint16_t command;
    uint16_t length; ///< octets of command-specific data after the header
    uint32_t session_handle;
    uint32_t status;
    uint8_t sender_context[8];
    uint32_t options;
} EipHeader;

/** @brief The device identity decoded from a ListIdentity response item. @ref product_name points INTO the
 *  source buffer and is NOT NUL-terminated. The 16-octet CIP socket address is skipped (its sin_* fields are
 *  network-order, unlike the little-endian encapsulation, so this codec does not reinterpret them). */
typedef struct
{
    uint16_t protocol_version; ///< encapsulation protocol version (1)
    uint16_t vendor_id;
    uint16_t device_type;
    uint16_t product_code;
    uint8_t revision_major;
    uint8_t revision_minor;
    uint16_t status;
    uint32_t serial_number;
    const char *product_name; ///< ASCII product name (into the buffer, not NUL-terminated)
    uint8_t product_name_len;
    uint8_t state; ///< device state
} EipIdentity;

/** @brief What build takes: buf, cap, h, data, data_len. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const EipHeader *h;
    const uint8_t *data;
    size_t data_len;
} EnipBuildArgs;

/** @brief What parse takes: buf, len, out, data, data_len. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    EipHeader *out;
    const uint8_t **data;
    size_t *data_len;
} EnipParseArgs;

/** @brief What build_register_session takes: buf, cap, sender_context. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const uint8_t *sender_context; ///< 8 bytes.
} EnipBuildRegisterSessionArgs;

/** @brief What build_unregister_session takes: buf, cap, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint32_t session_handle;
    const uint8_t *sender_context; ///< 8 bytes.
} EnipBuildUnregisterSessionArgs;

/** @brief What build_send_rr_data takes: buf, cap, session_handle, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    uint32_t session_handle;
    const uint8_t *sender_context; ///< 8 bytes.
    uint16_t timeout;
    const uint8_t *cip;
    size_t cip_len;
} EnipBuildSendRrDataArgs;

/** @brief What parse_send_rr_data takes: data, data_len, cip, cip_len. */
typedef struct
{
    const uint8_t *data;
    size_t data_len;
    const uint8_t **cip;
    size_t *cip_len;
} EnipParseSendRrDataArgs;

/** @brief What build_list_identity takes: buf, cap, sender_context. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const uint8_t *sender_context; ///< 8 bytes.
} EnipBuildListIdentityArgs;

/** @brief What parse_list_identity takes: data, data_len, out. */
typedef struct
{
    const uint8_t *data;
    size_t data_len;
    EipIdentity *out;
} EnipParseListIdentityArgs;

/**
 * @brief EtherNet/IP encapsulation codec (PROTOCORE_ENABLE_ENIP) - zero-heap builder + parser for the ODVA EtherNet/IP
 * encapsulation layer (TCP/UDP 44818), the transport that carries CIP.
 *
 * A caller sets the members a call takes, invokes it through ::Enip with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Enip.build_args.buf = ...;
 *   Enip.build_args.cap = ...;
 *   Enip.build_args.h = ...;
 *   Enip.build_args.data = ...;
 *   Enip.build_args.data_len = ...;
 *   Enip.build(work);
 *   // Enip.n is what the call reports
 *
 * @var EnipNs::build_args  what build takes: buf, cap, h, data, data_len
 * @var EnipNs::parse_args  what parse takes: buf, len, out, data, data_len
 * @var EnipNs::build_register_session_args  what build_register_session takes: buf, cap, sender_context
 * @var EnipNs::build_unregister_session_args  what build_unregister_session takes: buf, cap,
 * @var EnipNs::build_send_rr_data_args  what build_send_rr_data takes: buf, cap, session_handle,
 * @var EnipNs::parse_send_rr_data_args  what parse_send_rr_data takes: data, data_len, cip, cip_len
 * @var EnipNs::build_list_identity_args  what build_list_identity takes: buf, cap, sender_context
 * @var EnipNs::parse_list_identity_args  what parse_list_identity takes: data, data_len, out
 * @var EnipNs::ok  true iff a well-formed List Identity item is present and its ...
 * @var EnipNs::n  the count a call reports
 * @var EnipNs::build  build the encapsulation header + command data. Returns total ...
 * @var EnipNs::parse  parse the encapsulation header and slice the command data
 * @var EnipNs::build_register_session  build a RegisterSession request (protocol version 1). ...
 * @var EnipNs::build_unregister_session  build an UnRegisterSession request that closes session_handle (no ...
 * @var EnipNs::build_send_rr_data  build a SendRRData request wrapping cip as an unconnected message ...
 * @var EnipNs::parse_send_rr_data  from a SendRRData command-data block, extract the Unconnected Data ...
 * @var EnipNs::build_list_identity  build a ListIdentity request (command 0x0063, no command-specific ...
 * @var EnipNs::parse_list_identity  parse a ListIdentity response command-data block (the octets after ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    EnipBuildArgs build_args;
    EnipParseArgs parse_args;
    EnipBuildRegisterSessionArgs build_register_session_args;
    EnipBuildUnregisterSessionArgs build_unregister_session_args;
    EnipBuildSendRrDataArgs build_send_rr_data_args;
    EnipParseSendRrDataArgs parse_send_rr_data_args;
    EnipBuildListIdentityArgs build_list_identity_args;
    EnipParseListIdentityArgs parse_list_identity_args;

    proto_bool ok;
    size_t n;

    void (*const build)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
    void (*const build_register_session)(uint8_t *restrict work);
    void (*const build_unregister_session)(uint8_t *restrict work);
    void (*const build_send_rr_data)(uint8_t *restrict work);
    void (*const parse_send_rr_data)(uint8_t *restrict work);
    void (*const build_list_identity)(uint8_t *restrict work);
    void (*const parse_list_identity)(uint8_t *restrict work);
} EnipNs;

/** @brief The one symbol this module exports. */
extern EnipNs Enip;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ENIP

#endif // PROTOCORE_ENIP_H
