// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ocit.h
 * @brief OCIT-Outstations message codec (PC_ENABLE_OCIT).
 *
 * OCIT (Open Communication Interface for Road Traffic Control) is the DE/AT/CH open field-controller
 * interface between central traffic computers, field controllers, and detectors. The OCIT-Outstations
 * (OCIT-O) message set exchanges **objects** identified by an object type + instance, carrying typed
 * values, in a compact binary message:
 *
 *   [message-type : 1][object-type : 2][instance : 2][data-type : 1][value...]
 *
 * where message-type is a get / set / report, object-type + instance address the field object (a signal
 * group, a detector, a controller state), and the data-type tags the value (a bool, a byte, a 16/32-bit
 * integer, or a raw octet string). This codec builds/parses those messages. Pure, zero heap, no stdlib,
 * host-testable; the OCIT transport (TCP / the OCIT-O BTPPL profile) is the shipped transport.
 */

#ifndef PROTOCORE_OCIT_H
#define PROTOCORE_OCIT_H

#include "protocore_config.h"

#if PC_ENABLE_OCIT

// OCIT message types: wire bytes compared/emitted, so integer constants in a namespacing struct.
// (OcitMsg is the parsed-message struct below; these codes live in OcitMsgType.)
#define OCIT_MSG_GET 0x01    ///< read an object value.
#define OCIT_MSG_SET 0x02    ///< write an object value.
#define OCIT_MSG_REPORT 0x03 ///< an unsolicited value report.
#define OCIT_MSG_ERROR 0x0F  ///< error response.

/** @brief OCIT value data types. */
#define OCIT_TYPE_BOOL 0x01   ///< 1 byte (0/1).
#define OCIT_TYPE_BYTE 0x02   ///< 1 byte.
#define OCIT_TYPE_UINT16 0x03 ///< 2 bytes, big-endian.
#define OCIT_TYPE_UINT32 0x04 ///< 4 bytes, big-endian.
#define OCIT_TYPE_OCTETS 0x05 ///< raw octet string (length is the remaining message).

/**
 * @brief Build an OCIT message: [msg-type][object-type:2][instance:2][data-type][value...].
 * @param msg_type    OCIT_MSG_*.
 * @param object_type the object type id.
 * @param instance    the object instance.
 * @param data_type   OCIT_TYPE_*.
 * @param value       the value bytes (big-endian for the integer types; may be null if value_len == 0).
 * @param value_len   value length.
 * @return the message length (6 + value_len), or 0 on overflow / bad args.
 */
size_t pc_ocit_build(uint8_t msg_type, uint16_t object_type, uint16_t instance, uint8_t data_type, const uint8_t *value,
                     size_t value_len, uint8_t *out, size_t cap);

/** @brief Convenience: build a SET of a uint16 value. */
size_t pc_ocit_set_u16(uint16_t object_type, uint16_t instance, uint16_t value, uint8_t *out, size_t cap);

/** @brief A parsed OCIT message (value points into the input). */
typedef struct
{
    uint8_t msg_type;
    uint16_t object_type;
    uint16_t instance;
    uint8_t data_type;
    const uint8_t *value;
    size_t value_len;
} OcitMsg;

/** @brief Parse an OCIT message. @return true if @p len >= 6. */
proto_bool pc_ocit_parse(const uint8_t *msg, size_t len, OcitMsg *out);

/** @brief Read a big-endian uint16 value out of a parsed message (0 if the type/length does not match). */
uint16_t pc_ocit_value_u16(const OcitMsg *m);

#endif // PC_ENABLE_OCIT
#endif // PROTOCORE_OCIT_H
