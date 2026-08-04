// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_lwm2m_tlv.h
 * @brief OMA LwM2M TLV codec (PC_ENABLE_LWM2M) - zero-heap writer + cursor reader for the
 *        `application/vnd.oma.lwm2m+tlv` resource encoding, carried over the shipped CoAP
 *        service for LwM2M device management.
 *
 * Each TLV is `Type(1) Identifier(1-2) Length(0-3) Value(n)`:
 *  - Type bits 7-6: identifier kind - 00 Object Instance, 01 Resource Instance, 10 Multiple
 *    Resource, 11 Resource with Value.
 *  - Type bit 5: identifier width (0 = 8-bit, 1 = 16-bit).
 *  - Type bits 4-3: length kind - 00 inline in bits 2-0, 01 = 8-bit, 10 = 16-bit, 11 = 24-bit.
 *  - Type bits 2-0: the value length when the length kind is inline (0..7).
 *  Identifiers and lengths are big-endian. Resource values are big-endian: integers are the
 *  shortest of 1/2/4/8 octets (two's complement), booleans one octet, strings UTF-8.
 *
 * The writer emits TLVs into a caller buffer (fail-closed on overflow); the reader is a
 * cursor that decodes one TLV at the buffer head. Type-byte layout verified against the
 * OMA LwM2M spec.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_LWM2M_TLV_H
#define PROTOCORE_LWM2M_TLV_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_LWM2M

// Identifier kinds (Type byte bits 7-6).
#define LWM2M_TLV_OBJECT_INSTANCE 0x00
#define LWM2M_TLV_RESOURCE_INSTANCE 0x40
#define LWM2M_TLV_MULTIPLE_RESOURCE 0x80
#define LWM2M_TLV_RESOURCE 0xC0

// Type-byte bit-field layout (Type byte = idkind | id16 | lentype | inline-len).
#define LWM2M_TLV_IDKIND_MASK 0xC0     ///< bits 7-6: identifier kind (the LWM2M_TLV_* above)
#define LWM2M_TLV_ID16_FLAG 0x20       ///< bit 5: identifier is 16-bit (else 8-bit)
#define LWM2M_TLV_LENTYPE_SHIFT 3      ///< bits 4-3: length-type field position
#define LWM2M_TLV_LENTYPE_MASK 0x03    ///< 0 = inline, 1 = 8-bit, 2 = 16-bit, 3 = 24-bit length
#define LWM2M_TLV_INLINE_LEN_MASK 0x07 ///< bits 2-0: the value length when length-type == 0

/** @brief Cursor for building a TLV payload. Treat the fields as opaque. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    size_t pos;
    proto_bool error;
} Lwm2mTlvWriter;

void pc_lwm2m_tlv_init(Lwm2mTlvWriter *w, uint8_t *buf, size_t cap);

/** @brief Write a TLV with raw value bytes. @p id_type is one of LWM2M_TLV_*. */
proto_bool pc_lwm2m_tlv_write(Lwm2mTlvWriter *w, uint8_t id_type, uint16_t id, const uint8_t *value, size_t value_len);

/** @brief Write a Resource integer (shortest of 1/2/4/8 octets, big-endian two's complement). */
proto_bool pc_lwm2m_tlv_write_int(Lwm2mTlvWriter *w, uint16_t id, int64_t v);

/** @brief Write a Resource boolean (one octet, 0/1). */
proto_bool pc_lwm2m_tlv_write_bool(Lwm2mTlvWriter *w, uint16_t id, proto_bool v);

/** @brief Write a Resource UTF-8 string. */
proto_bool pc_lwm2m_tlv_write_string(Lwm2mTlvWriter *w, uint16_t id, const char *s);

/** @brief Write a Resource float (8-octet IEEE-754, big-endian). */
proto_bool pc_lwm2m_tlv_write_float(Lwm2mTlvWriter *w, uint16_t id, double v);

/** @brief Bytes written so far, or 0 if any write overflowed. */
size_t pc_lwm2m_tlv_finish(Lwm2mTlvWriter *w);

/** @brief One decoded TLV; @ref value points INTO the source buffer. */
typedef struct
{
    uint8_t id_type; ///< LWM2M_TLV_* (Type bits 7-6)
    uint16_t id;
    const uint8_t *value;
    size_t value_len;
} Lwm2mTlv;

/**
 * @brief Read one TLV at [buf+*pos]; advances *pos past it.
 * @return true on a complete TLV; false at end-of-buffer or on truncation.
 */
proto_bool pc_lwm2m_tlv_read(const uint8_t *buf, size_t len, size_t *pos, Lwm2mTlv *out);

/** @brief Decode a TLV integer value (1/2/4/8 octets, big-endian two's complement). */
proto_bool pc_lwm2m_tlv_value_int(const uint8_t *value, size_t len, int64_t *out);

#endif // PC_ENABLE_LWM2M

PROTO_END_DECLS

#endif // PROTOCORE_LWM2M_TLV_H
