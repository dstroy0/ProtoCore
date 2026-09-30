// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file codec.h
 * @brief One binary codec interface; a wire encoding is an instance of it.
 *
 * The interface fixes the operations, their order, and their signatures; a format supplies the
 * function pointers. The order is the field order of the table, so a format whose operations drift
 * out of order fails to compile.
 *
 * Dispatch is a `static const` table of function pointers in rodata.
 *
 * The region types come from spatium.h and the byte verbs from octetus_introitus_exitus.h. A codec
 * allocates nothing and owns no buffer: it writes into a mmgr_span the caller bound and reads from a
 * mmgr_cspan.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CODEC_H
#define PROTOCORE_CODEC_H

#include "spatium/spatium.h"

#include "protocore_config.h" // PROTOCORE_ENABLE_CBOR / PROTOCORE_ENABLE_MSGPACK gate the instances below

PROTOCORE_BEGIN_DECLS

/**
 * @brief The next item's type, reported by protocore_codec::peek without consuming it.
 *
 * One set of names across every format: CBOR calls a byte string "bytes" and MessagePack calls it
 * "bin"; CBOR has "null" and MessagePack "nil". They are the same item, so they get one name here
 * and the format maps its own tag onto it.
 */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_CODEC_UINT = 0,
    PROTOCORE_CODEC_INT,
    PROTOCORE_CODEC_BYTES,
    PROTOCORE_CODEC_STR,
    PROTOCORE_CODEC_ARRAY,
    PROTOCORE_CODEC_MAP,
    PROTOCORE_CODEC_BOOL,
    PROTOCORE_CODEC_NULL,
    PROTOCORE_CODEC_FLOAT,
    PROTOCORE_CODEC_INVALID ///< end of buffer, a prior error, or an item this format does not carry
} protocore_codec_type;

/**
 * @brief A wire encoding: the ten writes, the peek, and the nine reads.
 *
 * Field order is the operation order every format declares and implements in. `int`, `bool` and
 * `float` are keywords, so the members carry the put_ / get_ prefix that says which direction they
 * run in.
 */
typedef struct
{
    // --- encode into a caller-bound mmgr_span ---
    void (*put_uint)(mmgr_span *w, uint64_t v);
    void (*put_int)(mmgr_span *w, int64_t v);
    void (*put_bytes)(mmgr_span *w, const uint8_t *data, size_t len);
    void (*put_str)(mmgr_span *w, const char *s);
    void (*put_str_n)(mmgr_span *w, const char *s, size_t len);
    void (*put_bool)(mmgr_span *w, proto_bool b);
    void (*put_null)(mmgr_span *w);
    void (*put_float)(mmgr_span *w, float f);
    void (*put_array)(mmgr_span *w, size_t count);
    void (*put_map)(mmgr_span *w, size_t count);

    /**
     * @brief Emit a map key, given both spellings of it.
     *
     * A spec often names the same field differently per encoding: RFC 8428 labels a SenML base name
     * `"bn"` in JSON and `-2` in CBOR. That is the encoding's business, not the caller's, so the
     * caller hands over both and the format picks the one it is specified to write. Without this the
     * difference leaks upward and every producer keeps one walk per encoding.
     */
    void (*put_label)(mmgr_span *w, const char *name, int64_t num);

    // --- decode from a caller-bound mmgr_cspan ---
    protocore_codec_type (*peek)(mmgr_cspan *r);
    proto_bool (*get_uint)(mmgr_cspan *r, uint64_t *out);
    proto_bool (*get_int)(mmgr_cspan *r, int64_t *out);
    proto_bool (*get_bytes)(mmgr_cspan *r, const uint8_t **out, size_t *len);
    proto_bool (*get_str)(mmgr_cspan *r, const char **out, size_t *len);
    proto_bool (*get_array)(mmgr_cspan *r, size_t *count);
    proto_bool (*get_map)(mmgr_cspan *r, size_t *count);
    proto_bool (*get_bool)(mmgr_cspan *r, proto_bool *out);
    proto_bool (*get_null)(mmgr_cspan *r);
    proto_bool (*get_float)(mmgr_cspan *r, float *out);
} protocore_codec;

// Each format declares its own instance in its own header: cbor.h has Cbor, msgpack.h has
// MsgPack. The instance is the storage, so the format that owns the operations owns the
// table built from them, and a build that compiles a format out has neither.

PROTOCORE_END_DECLS

#endif // PROTOCORE_CODEC_H
