// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protobuf.h
 * @brief Protocol Buffers wire codec (PC_ENABLE_PROTOBUF) - zero-heap streaming writer
 *        + cursor reader over caller buffers, the same shape as the shipped CBOR /
 *        MessagePack codecs. This is the standalone Protobuf deliverable; gRPC (framed
 *        Protobuf over HTTP/2) is gated on the HTTP/2 roadmap item.
 *
 * Wire format (https://protobuf.dev/programming-guides/encoding/):
 *  - A field is a tag varint `(field_number << 3) | wire_type` then the value.
 *  - Varints are little-endian base-128 with the high bit as a continuation flag.
 *  - Wire types: 0 VARINT (int/uint/sint/bool/enum), 1 I64 (fixed64/double, 8 bytes LE),
 *    2 LEN (string/bytes/embedded message), 5 I32 (fixed32/float, 4 bytes LE). Groups
 *    (3/4) are deprecated and rejected by the reader.
 *  - sint32/sint64 use ZigZag: `(n << 1) ^ (n >> 31|63)`.
 *
 * The writer encodes one field at a time into a caller buffer (fail-closed on overflow);
 * embedded messages are built into a separate buffer and added with @ref pc_pb_bytes. The
 * reader is a cursor: it decodes the field at the buffer head and reports bytes consumed.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PROTOBUF_H
#define PROTOCORE_PROTOBUF_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_NEED_PROTOBUF

// Wire types.
#define PB_WT_VARINT 0
#define PB_WT_I64 1
#define PB_WT_LEN 2
#define PB_WT_I32 5

// ---- writer ----

/** @brief Streaming encoder over a caller buffer. Treat the fields as opaque. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    size_t pos;
    proto_bool error; ///< sticky overflow flag
} PbWriter;

void pc_pb_writer_init(PbWriter *w, uint8_t *buf, size_t cap);

/** @brief Write a raw varint (no tag). */
proto_bool pc_pb_write_varint(PbWriter *w, uint64_t v);

/** @brief Write a field tag `(field << 3) | wire_type`. */
proto_bool pc_pb_write_tag(PbWriter *w, uint32_t field, uint8_t wire_type);

proto_bool pc_pb_uint64(PbWriter *w, uint32_t field, uint64_t v); ///< varint (uint32/uint64/enum)
proto_bool pc_pb_int64(PbWriter *w, uint32_t field, int64_t v);   ///< varint, two's complement (int32/int64)
proto_bool pc_pb_sint64(PbWriter *w, uint32_t field, int64_t v);  ///< ZigZag varint (sint32/sint64)
proto_bool pc_pb_bool(PbWriter *w, uint32_t field, proto_bool v);
proto_bool pc_pb_fixed32(PbWriter *w, uint32_t field, uint32_t v); ///< wire type 5
proto_bool pc_pb_fixed64(PbWriter *w, uint32_t field, uint64_t v); ///< wire type 1
proto_bool pc_pb_float(PbWriter *w, uint32_t field, float v);
proto_bool pc_pb_double(PbWriter *w, uint32_t field, double v);
proto_bool pc_pb_bytes(PbWriter *w, uint32_t field, const uint8_t *data, size_t len); ///< wire type 2
proto_bool pc_pb_string(PbWriter *w, uint32_t field, const char *s);

/** @brief Finish: returns the encoded byte count, or 0 if any write overflowed. */
size_t pc_pb_writer_finish(PbWriter *w);

// ---- reader ----

/** @brief One decoded field. For LEN, @ref data / @ref len point INTO the source buffer. */
typedef struct
{
    uint32_t field_number;
    uint8_t wire_type;
    uint64_t value;      ///< VARINT value, or the raw LE bits for I32 / I64
    const uint8_t *data; ///< LEN payload (not copied)
    size_t len;          ///< LEN length
} PbField;

/** @brief Read a raw varint at [buf+*pos]; advances *pos. False on truncation / overlong (>10 bytes). */
proto_bool pc_pb_read_varint(const uint8_t *buf, size_t len, size_t *pos, uint64_t *out);

/**
 * @brief Read one field at [buf+*pos]; advances *pos past it.
 * @return true on a complete field; false at end-of-buffer or on a malformed / group field.
 */
proto_bool pc_pb_read_field(const uint8_t *buf, size_t len, size_t *pos, PbField *out);

// Value decoders.
int64_t pc_pb_zigzag64(uint64_t v);
int32_t pc_pb_zigzag32(uint32_t v);
float pc_pb_float_bits(uint32_t bits);
double pc_pb_double_bits(uint64_t bits);

#endif // PC_NEED_PROTOBUF

PROTO_END_DECLS

#endif // PROTOCORE_PROTOBUF_H
