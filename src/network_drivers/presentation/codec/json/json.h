// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file json.h
 * @brief Layer 6 (Presentation) - zero-heap JSON: a bounded writer and top-level reader.
 *
 * A deliberately small JSON helper for the common IoT shapes (a flat-ish object
 * of strings / numbers / booleans, with bounded nesting). It allocates nothing:
 * the writer formats into a caller-provided buffer, and the reader scans a
 * NUL-terminated body in place. ArduinoJson remains the option when you need a
 * full DOM - it heap-allocates, which this library avoids.
 *
 * ## Writing
 * @code
 *   char buf[128];
 *   protocore_json_writer w;
 *   Json.init(&w, buf, sizeof(buf));
 *   Json.begin_object(&w);
 *     Json.kv_str(&w, "status", "ok");
 *     Json.kv_int(&w, "count", 3);
 *     Json.key(&w, "items"); Json.begin_array(&w);
 *       Json.put_str(&w, "a"); Json.put_str(&w, "b");
 *     Json.end_array(&w);
 *   Json.end_object(&w);
 *   if (protocore_json_ok(&w)) server.send(slot, 200, "application/json", protocore_json_c_str(&w));
 *   // -> {"status":"ok","count":3,"items":["a","b"]}
 * @endcode
 *
 * ## Reading (top-level keys of an object body)
 * @code
 *   char ssid[33];
 *   if (Json.get_str(req->body, "ssid", ssid, sizeof(ssid))) { ... }
 *   long port;
 *   if (Json.get_int(req->body, "port", &port)) { ... }
 * @endcode
 */

#ifndef PROTOCORE_JSON_H
#define PROTOCORE_JSON_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_JSON

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/**
 * @brief Builds a JSON document into a fixed caller buffer, no heap.
 *
 * Commas, key quoting, and string escaping are emitted automatically. On buffer
 * overflow or a structural error (nesting past JSON_MAX_DEPTH), writing stops
 * and protocore_json_ok() returns false; protocore_json_c_str() still yields a NUL-terminated
 * (truncated) string so a partial result never runs off the end.
 *
 * The caller owns the struct as well as the buffer, so the whole writer is one
 * local with no allocation behind it. Its fields are the writer's business:
 * reach them through the calls below, never directly.
 */
typedef struct
{
    char *buf;
    size_t cap;
    size_t len;
    proto_bool ok;
    proto_bool after_key;                  // next value follows a key(): suppress its comma
    uint8_t depth;                         // open containers
    proto_bool need_comma[JSON_MAX_DEPTH]; // per-level: has a prior item been emitted?
} protocore_json_writer;
/** @brief False after any overflow / structural error. */
PROTOCORE_INLINE proto_bool protocore_json_ok(const protocore_json_writer *w)
{
    return w->ok;
}
/** @brief Bytes written so far (excludes the NUL). */
PROTOCORE_INLINE size_t protocore_json_length(const protocore_json_writer *w)
{
    return w->len;
}
/** @brief NUL-terminated output (truncated if !protocore_json_ok()). */
PROTOCORE_INLINE const char *protocore_json_c_str(const protocore_json_writer *w)
{
    return w->buf;
}
/** @brief What init takes: w, buf, cap. */
typedef struct
{
    protocore_json_writer *w;
    char *buf;
    size_t cap;
} JsonInitArgs;
/** @brief What begin_object takes: w. */
typedef struct
{
    protocore_json_writer *w;
} JsonBeginObjectArgs;
/** @brief What end_object takes: w. */
typedef struct
{
    protocore_json_writer *w;
} JsonEndObjectArgs;
/** @brief What begin_array takes: w. */
typedef struct
{
    protocore_json_writer *w;
} JsonBeginArrayArgs;
/** @brief What end_array takes: w. */
typedef struct
{
    protocore_json_writer *w;
} JsonEndArrayArgs;
/** @brief What key takes: w, k. */
typedef struct
{
    protocore_json_writer *w;
    const char *k;
} JsonKeyArgs;
/** @brief What put_str takes: w, v. */
typedef struct
{
    protocore_json_writer *w;
    const char *v;
} JsonPutStrArgs;
/** @brief What put_int takes: w, v. */
typedef struct
{
    protocore_json_writer *w;
    long v;
} JsonPutIntArgs;
/** @brief What put_uint takes: w, v. */
typedef struct
{
    protocore_json_writer *w;
    unsigned long v;
} JsonPutUintArgs;
/** @brief What put_bool takes: w, v. */
typedef struct
{
    protocore_json_writer *w;
    proto_bool v;
} JsonPutBoolArgs;
/** @brief What put_null takes: w. */
typedef struct
{
    protocore_json_writer *w;
} JsonPutNullArgs;
/** @brief What put_raw takes: w, literal. */
typedef struct
{
    protocore_json_writer *w;
    const char *literal;
} JsonPutRawArgs;
/** @brief What kv_str takes: w, k, v. */
typedef struct
{
    protocore_json_writer *w;
    const char *k;
    const char *v;
} JsonKvStrArgs;
/** @brief What kv_int takes: w, k, v. */
typedef struct
{
    protocore_json_writer *w;
    const char *k;
    long v;
} JsonKvIntArgs;
/** @brief What kv_uint takes: w, k, v. */
typedef struct
{
    protocore_json_writer *w;
    const char *k;
    unsigned long v;
} JsonKvUintArgs;
/** @brief What kv_bool takes: w, k, v. */
typedef struct
{
    protocore_json_writer *w;
    const char *k;
    proto_bool v;
} JsonKvBoolArgs;
/** @brief What kv_null takes: w, k. */
typedef struct
{
    protocore_json_writer *w;
    const char *k;
} JsonKvNullArgs;
/** @brief What kv_raw takes: w, k, literal. */
typedef struct
{
    protocore_json_writer *w;
    const char *k;
    const char *literal;
} JsonKvRawArgs;
/** @brief What get_str takes: json, key, out, out_cap. */
typedef struct
{
    const char *json;
    const char *key;
    char *out;
    size_t out_cap;
} JsonGetStrArgs;
/** @brief What get_int takes: json, key, out. */
typedef struct
{
    const char *json;
    const char *key;
    long *out;
} JsonGetIntArgs;
/** @brief What get_bool takes: json, key, out. */
typedef struct
{
    const char *json;
    const char *key;
    proto_bool *out;
} JsonGetBoolArgs;
/**
 * @brief Layer 6 (Presentation) - zero-heap JSON: a bounded writer and top-level reader.
 *
 * A caller sets the members a call takes, invokes it through ::Json with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Json.init_args.w = ...;
 *   Json.init_args.buf = ...;
 *   Json.init_args.cap = ...;
 *   Json.init(work);
 *
 * @var JsonNs::init_args  what init takes: w, buf, cap
 * @var JsonNs::begin_object_args  what begin_object takes: w
 * @var JsonNs::end_object_args  what end_object takes: w
 * @var JsonNs::begin_array_args  what begin_array takes: w
 * @var JsonNs::end_array_args  what end_array takes: w
 * @var JsonNs::key_args  what key takes: w, k
 * @var JsonNs::put_str_args  what put_str takes: w, v
 * @var JsonNs::put_int_args  what put_int takes: w, v
 * @var JsonNs::put_uint_args  what put_uint takes: w, v
 * @var JsonNs::put_bool_args  what put_bool takes: w, v
 * @var JsonNs::put_null_args  what put_null takes: w
 * @var JsonNs::put_raw_args  what put_raw takes: w, literal
 * @var JsonNs::kv_str_args  what kv_str takes: w, k, v
 * @var JsonNs::kv_int_args  what kv_int takes: w, k, v
 * @var JsonNs::kv_uint_args  what kv_uint takes: w, k, v
 * @var JsonNs::kv_bool_args  what kv_bool takes: w, k, v
 * @var JsonNs::kv_null_args  what kv_null takes: w, k
 * @var JsonNs::kv_raw_args  what kv_raw takes: w, k, literal
 * @var JsonNs::get_str_args  what get_str takes: json, key, out, out_cap
 * @var JsonNs::get_int_args  what get_int takes: json, key, out
 * @var JsonNs::get_bool_args  what get_bool takes: json, key, out
 * @var JsonNs::ok  a call's true/false outcome
 * @var JsonNs::init  bind the writer to a caller buffer, capacity including the NUL
 * @var JsonNs::begin_object  open `{`, as a value or an element where that applies
 * @var JsonNs::end_object  close `}`
 * @var JsonNs::begin_array  open `[`
 * @var JsonNs::end_array  close `]`
 * @var JsonNs::key  an object member name (`"k":`); one value call follows it
 * @var JsonNs::put_str  a quoted, escaped string value
 * @var JsonNs::put_int  a signed integer value
 * @var JsonNs::put_uint  an unsigned integer value
 * @var JsonNs::put_bool  `true` or `false`
 * @var JsonNs::put_null  `null`
 * @var JsonNs::put_raw  a pre-formatted literal, verbatim
 * @var JsonNs::kv_str  `"k":"v"`, escaped
 * @var JsonNs::kv_int  `"k":<int>`
 * @var JsonNs::kv_uint  `"k":<uint>`
 * @var JsonNs::kv_bool  `"k":true|false`
 * @var JsonNs::kv_null  `"k":null`
 * @var JsonNs::kv_raw  `"k":<literal>`
 * @var JsonNs::get_str  a top-level string member, unescaped into out and bounded by
 * @var JsonNs::get_int  a top-level member that parses as an integer
 * @var JsonNs::get_bool  a top-level member that is a JSON boolean
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    JsonInitArgs init_args;
    JsonBeginObjectArgs begin_object_args;
    JsonEndObjectArgs end_object_args;
    JsonBeginArrayArgs begin_array_args;
    JsonEndArrayArgs end_array_args;
    JsonKeyArgs key_args;
    JsonPutStrArgs put_str_args;
    JsonPutIntArgs put_int_args;
    JsonPutUintArgs put_uint_args;
    JsonPutBoolArgs put_bool_args;
    JsonPutNullArgs put_null_args;
    JsonPutRawArgs put_raw_args;
    JsonKvStrArgs kv_str_args;
    JsonKvIntArgs kv_int_args;
    JsonKvUintArgs kv_uint_args;
    JsonKvBoolArgs kv_bool_args;
    JsonKvNullArgs kv_null_args;
    JsonKvRawArgs kv_raw_args;
    JsonGetStrArgs get_str_args;
    JsonGetIntArgs get_int_args;
    JsonGetBoolArgs get_bool_args;
    proto_bool ok;
} JsonVars;

/** @brief The operands and the outcome. */
extern JsonVars JsonV;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
    void (*const begin_object)(uint8_t *restrict work);
    void (*const end_object)(uint8_t *restrict work);
    void (*const begin_array)(uint8_t *restrict work);
    void (*const end_array)(uint8_t *restrict work);
    void (*const key)(uint8_t *restrict work);
    void (*const put_str)(uint8_t *restrict work);
    void (*const put_int)(uint8_t *restrict work);
    void (*const put_uint)(uint8_t *restrict work);
    void (*const put_bool)(uint8_t *restrict work);
    void (*const put_null)(uint8_t *restrict work);
    void (*const put_raw)(uint8_t *restrict work);
    void (*const kv_str)(uint8_t *restrict work);
    void (*const kv_int)(uint8_t *restrict work);
    void (*const kv_uint)(uint8_t *restrict work);
    void (*const kv_bool)(uint8_t *restrict work);
    void (*const kv_null)(uint8_t *restrict work);
    void (*const kv_raw)(uint8_t *restrict work);
    void (*const get_str)(uint8_t *restrict work);
    void (*const get_int)(uint8_t *restrict work);
    void (*const get_bool)(uint8_t *restrict work);
} JsonNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in JsonV or a region of the borrow at a fixed offset.
void protocore_json_init(uint8_t *restrict work);
void protocore_json_begin_object(uint8_t *restrict work);
void protocore_json_end_object(uint8_t *restrict work);
void protocore_json_begin_array(uint8_t *restrict work);
void protocore_json_end_array(uint8_t *restrict work);
void protocore_json_key(uint8_t *restrict work);
void protocore_json_put_str(uint8_t *restrict work);
void protocore_json_put_int(uint8_t *restrict work);
void protocore_json_put_uint(uint8_t *restrict work);
void protocore_json_put_bool(uint8_t *restrict work);
void protocore_json_put_null(uint8_t *restrict work);
void protocore_json_put_raw(uint8_t *restrict work);
void protocore_json_kv_str(uint8_t *restrict work);
void protocore_json_kv_int(uint8_t *restrict work);
void protocore_json_kv_uint(uint8_t *restrict work);
void protocore_json_kv_bool(uint8_t *restrict work);
void protocore_json_kv_null(uint8_t *restrict work);
void protocore_json_kv_raw(uint8_t *restrict work);
void protocore_json_get_str(uint8_t *restrict work);
void protocore_json_get_int(uint8_t *restrict work);
void protocore_json_get_bool(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Json.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const JsonNs Json __attribute__((unused)) = {
    .init = protocore_json_init,
    .begin_object = protocore_json_begin_object,
    .end_object = protocore_json_end_object,
    .begin_array = protocore_json_begin_array,
    .end_array = protocore_json_end_array,
    .key = protocore_json_key,
    .put_str = protocore_json_put_str,
    .put_int = protocore_json_put_int,
    .put_uint = protocore_json_put_uint,
    .put_bool = protocore_json_put_bool,
    .put_null = protocore_json_put_null,
    .put_raw = protocore_json_put_raw,
    .kv_str = protocore_json_kv_str,
    .kv_int = protocore_json_kv_int,
    .kv_uint = protocore_json_kv_uint,
    .kv_bool = protocore_json_kv_bool,
    .kv_null = protocore_json_kv_null,
    .kv_raw = protocore_json_kv_raw,
    .get_str = protocore_json_get_str,
    .get_int = protocore_json_get_int,
    .get_bool = protocore_json_get_bool,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_JSON

#endif // PROTOCORE_JSON_H
