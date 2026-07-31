// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file enip.h
 * @brief EtherNet/IP encapsulation codec (PC_ENABLE_ENIP) - zero-heap builder + parser
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

#include "protocore_config.h"

#if PC_ENABLE_ENIP

#include <stddef.h>
#include <stdint.h>

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
struct EipHeader
{
    uint16_t command;
    uint16_t length; ///< octets of command-specific data after the header
    uint32_t session_handle;
    uint32_t status;
    uint8_t sender_context[8];
    uint32_t options;
};

/** @brief Build the encapsulation header + command data. Returns total octets, or 0. */
size_t pc_eip_build(uint8_t *buf, size_t cap, const EipHeader *h, const uint8_t *data, size_t data_len);

/** @brief Parse the encapsulation header and slice the command data. */
bool pc_eip_parse(const uint8_t *buf, size_t len, EipHeader *out, const uint8_t **data, size_t *data_len);

/** @brief Build a RegisterSession request (protocol version 1). @p sender_context may be null (zeros). */
size_t pc_eip_build_register_session(uint8_t *buf, size_t cap, const uint8_t sender_context[8]);

/**
 * @brief Build an UnRegisterSession request that closes @p session_handle (no command-specific data).
 * @p sender_context may be null (zeros).
 */
size_t pc_eip_build_unregister_session(uint8_t *buf, size_t cap, uint32_t session_handle,
                                       const uint8_t sender_context[8]);

/**
 * @brief Build a SendRRData request wrapping @p cip as an unconnected message (Null Address
 *        item + Unconnected Data item).
 */
size_t pc_eip_build_send_rr_data(uint8_t *buf, size_t cap, uint32_t session_handle, const uint8_t sender_context[8],
                                 uint16_t timeout, const uint8_t *cip, size_t pc_cip_len);

/** @brief From a SendRRData command-data block, extract the Unconnected Data item (the CIP reply). */
bool pc_eip_parse_send_rr_data(const uint8_t *data, size_t data_len, const uint8_t **cip, size_t *pc_cip_len);

/**
 * @brief Build a ListIdentity request (command 0x0063, no command-specific data) - the broadcast an
 *        originator sends to enumerate EtherNet/IP devices on a subnet. @p sender_context may be null (zeros).
 */
size_t pc_eip_build_list_identity(uint8_t *buf, size_t cap, const uint8_t sender_context[8]);

/** @brief The device identity decoded from a ListIdentity response item. @ref product_name points INTO the
 *  source buffer and is NOT NUL-terminated. The 16-octet CIP socket address is skipped (its sin_* fields are
 *  network-order, unlike the little-endian encapsulation, so this codec does not reinterpret them). */
struct EipIdentity
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
};

/**
 * @brief Parse a ListIdentity response command-data block (the octets after the encapsulation header): walk
 *        the CPF items for the List Identity item (0x000C) and decode its little-endian identity fields.
 * @return true iff a well-formed List Identity item is present and its declared length covers all fields.
 */
bool pc_eip_parse_list_identity(const uint8_t *data, size_t data_len, EipIdentity *out);

#endif // PC_ENABLE_ENIP

#endif // PROTOCORE_ENIP_H
