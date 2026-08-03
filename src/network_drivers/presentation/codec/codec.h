// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file codec.h
 * @brief One binary codec interface; a wire encoding is an instance of it.
 *
 * CBOR and MessagePack encode the same ten things - unsigned, signed, bytes, string, bool, null,
 * float, array header, map header - into different bytes. Written out separately they were two
 * parallel APIs over two field-identical cursor structs, and a caller had to pick one at the call
 * site, so SenML-over-CBOR and SenML-over-MessagePack could not be the same code.
 *
 * They are one interface with two instances. The operations, their order, and their signatures are
 * fixed here; a format supplies the function pointers. Order is load-bearing: it is the field order
 * of the table, so a format whose operations drift out of order fails to compile rather than
 * silently binding the wrong encoder.
 *
 * Dispatch is a `static const` table of plain function pointers in rodata, the same shape as
 * ProtoHandler. Not virtual, not RTTI, not std::function (SRCBANNED #22): the set of reachable
 * targets stays a closed list the linker can see whole, so the worst-case path is still a number.
 *
 * The region types come from span.h and the byte verbs from bytes.h - a codec does no allocation and
 * owns no buffer; it writes into a pc_span the caller bound and reads from a pc_cspan.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CODEC_H
#define PROTOCORE_CODEC_H

#include "protocore_config.h" // PC_NEED_CBOR / PC_ENABLE_MSGPACK gate the instances below
#include "shared_primitives/span.h"

PROTO_BEGIN_DECLS

/**
 * @brief The next item's type, reported by pc_codec::peek without consuming it.
 *
 * One set of names across every format: CBOR calls a byte string "bytes" and MessagePack calls it
 * "bin"; CBOR has "null" and MessagePack "nil". They are the same item, so they get one name here
 * and the format maps its own tag onto it.
 */
typedef enum PROTO_ENUM_PACKED
{
    PC_CODEC_UINT = 0,
    PC_CODEC_INT,
    PC_CODEC_BYTES,
    PC_CODEC_STR,
    PC_CODEC_ARRAY,
    PC_CODEC_MAP,
    PC_CODEC_BOOL,
    PC_CODEC_NULL,
    PC_CODEC_FLOAT,
    PC_CODEC_INVALID ///< end of buffer, a prior error, or an item this format does not carry
} pc_codec_type;

/**
 * @brief A wire encoding: the ten writes, the peek, and the nine reads.
 *
 * Field order is the operation order every format declares and implements in. `int`, `bool` and
 * `float` are keywords, so the members carry the put_ / get_ prefix that says which direction they
 * run in.
 */
typedef struct
{
    // --- encode into a caller-bound pc_span ---
    void (*put_uint)(pc_span *w, uint64_t v);
    void (*put_int)(pc_span *w, int64_t v);
    void (*put_bytes)(pc_span *w, const uint8_t *data, size_t len);
    void (*put_str)(pc_span *w, const char *s);
    void (*put_str_n)(pc_span *w, const char *s, size_t len);
    void (*put_bool)(pc_span *w, proto_bool b);
    void (*put_null)(pc_span *w);
    void (*put_float)(pc_span *w, float f);
    void (*put_array)(pc_span *w, size_t count);
    void (*put_map)(pc_span *w, size_t count);

    /**
     * @brief Emit a map key, given both spellings of it.
     *
     * A spec often names the same field differently per encoding: RFC 8428 labels a SenML base name
     * `"bn"` in JSON and `-2` in CBOR. That is the encoding's business, not the caller's, so the
     * caller hands over both and the format picks the one it is specified to write. Without this the
     * difference leaks upward and every producer keeps one walk per encoding.
     */
    void (*put_label)(pc_span *w, const char *name, int64_t num);

    // --- decode from a caller-bound pc_cspan ---
    pc_codec_type (*peek)(pc_cspan *r);
    proto_bool (*get_uint)(pc_cspan *r, uint64_t *out);
    proto_bool (*get_int)(pc_cspan *r, int64_t *out);
    proto_bool (*get_bytes)(pc_cspan *r, const uint8_t **out, size_t *len);
    proto_bool (*get_str)(pc_cspan *r, const char **out, size_t *len);
    proto_bool (*get_array)(pc_cspan *r, size_t *count);
    proto_bool (*get_map)(pc_cspan *r, size_t *count);
    proto_bool (*get_bool)(pc_cspan *r, proto_bool *out);
    proto_bool (*get_null)(pc_cspan *r);
    proto_bool (*get_float)(pc_cspan *r, float *out);
} pc_codec;

// --- the formats, as instances ---
//
// Storage is opaque: each table is internal linkage in codec.c and reached only through its
// accessor, so no caller can name it, copy it, or keep a second one. Guarded so a build that
// compiles a format out has no accessor to call and no table to link.

#if PC_NEED_CBOR
/** @brief CBOR (RFC 8949) as an instance of the codec interface. */
const pc_codec *pc_codec_cbor(void);
#endif

#if PC_ENABLE_MSGPACK
/** @brief MessagePack as an instance of the codec interface. */
const pc_codec *pc_codec_msgpack(void);
#endif

PROTO_END_DECLS

#endif // PROTOCORE_CODEC_H
