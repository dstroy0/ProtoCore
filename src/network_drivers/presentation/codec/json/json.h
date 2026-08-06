// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 *   pc_json_writer w;
 *   Json.init(&w, buf, sizeof(buf));
 *   Json.begin_object(&w);
 *     Json.kv_str(&w, "status", "ok");
 *     Json.kv_int(&w, "count", 3);
 *     Json.key(&w, "items"); Json.begin_array(&w);
 *       Json.put_str(&w, "a"); Json.put_str(&w, "b");
 *     Json.end_array(&w);
 *   Json.end_object(&w);
 *   if (pc_json_ok(&w)) server.send(slot, 200, "application/json", pc_json_c_str(&w));
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

#include "protocore_config.h"

PROTO_BEGIN_DECLS

/**
 * @brief Builds a JSON document into a fixed caller buffer, no heap.
 *
 * Commas, key quoting, and string escaping are emitted automatically. On buffer
 * overflow or a structural error (nesting past JSON_MAX_DEPTH), writing stops
 * and pc_json_ok() returns false; pc_json_c_str() still yields a NUL-terminated
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
} pc_json_writer;

/**
 * @brief The writer's calls, the key+value shorthands, and the top-level reader.
 *
 * `int` and `bool` are keywords, so the bare-value group carries the `put_` prefix the codec
 * interface uses for the same reason, and the shorthands `kv_`. A member emitted through `key()`
 * takes one value call after it; `kv_*` is that pair in one call.
 *
 * @var JsonNs::init          bind the writer to a caller buffer, capacity including the NUL
 * @var JsonNs::begin_object  open `{`, as a value or an element where that applies
 * @var JsonNs::end_object    close `}`
 * @var JsonNs::begin_array   open `[`
 * @var JsonNs::end_array     close `]`
 * @var JsonNs::key           an object member name (`"k":`); one value call follows it
 * @var JsonNs::put_str       a quoted, escaped string value
 * @var JsonNs::put_int       a signed integer value
 * @var JsonNs::put_uint      an unsigned integer value
 * @var JsonNs::put_bool      `true` or `false`
 * @var JsonNs::put_null      `null`
 * @var JsonNs::put_raw       a pre-formatted literal, verbatim
 * @var JsonNs::kv_str        `"k":"v"`, escaped
 * @var JsonNs::kv_int        `"k":<int>`
 * @var JsonNs::kv_uint       `"k":<uint>`
 * @var JsonNs::kv_bool       `"k":true|false`
 * @var JsonNs::kv_null       `"k":null`
 * @var JsonNs::kv_raw        `"k":<literal>`
 * @var JsonNs::get_str       a top-level string member, unescaped into @p out and bounded by
 *                            @p out_cap. Nested objects, arrays and string contents are skipped, so
 *                            a same-named nested key does not match
 * @var JsonNs::get_int       a top-level member that parses as an integer
 * @var JsonNs::get_bool      a top-level member that is a JSON boolean
 */
typedef struct
{
    void (*init)(pc_json_writer *w, char *buf, size_t cap);
    void (*begin_object)(pc_json_writer *w);
    void (*end_object)(pc_json_writer *w);
    void (*begin_array)(pc_json_writer *w);
    void (*end_array)(pc_json_writer *w);
    void (*key)(pc_json_writer *w, const char *k);

    void (*put_str)(pc_json_writer *w, const char *v);
    void (*put_int)(pc_json_writer *w, long v);
    void (*put_uint)(pc_json_writer *w, unsigned long v);
    void (*put_bool)(pc_json_writer *w, proto_bool v);
    void (*put_null)(pc_json_writer *w);
    void (*put_raw)(pc_json_writer *w, const char *literal);

    void (*kv_str)(pc_json_writer *w, const char *k, const char *v);
    void (*kv_int)(pc_json_writer *w, const char *k, long v);
    void (*kv_uint)(pc_json_writer *w, const char *k, unsigned long v);
    void (*kv_bool)(pc_json_writer *w, const char *k, proto_bool v);
    void (*kv_null)(pc_json_writer *w, const char *k);
    void (*kv_raw)(pc_json_writer *w, const char *k, const char *literal);

    proto_bool (*get_str)(const char *json, const char *key, char *out, size_t out_cap);
    proto_bool (*get_int)(const char *json, const char *key, long *out);
    proto_bool (*get_bool)(const char *json, const char *key, proto_bool *out);
} JsonNs;

/** @brief The one symbol this module exports. The three below inline against the caller's writer. */
extern const JsonNs Json;

/** @brief False after any overflow / structural error. */
PC_INLINE proto_bool pc_json_ok(const pc_json_writer *w)
{
    return w->ok;
}
/** @brief Bytes written so far (excludes the NUL). */
PC_INLINE size_t pc_json_length(const pc_json_writer *w)
{
    return w->len;
}
/** @brief NUL-terminated output (truncated if !pc_json_ok()). */
PC_INLINE const char *pc_json_c_str(const pc_json_writer *w)
{
    return w->buf;
}

PROTO_END_DECLS

#endif // PROTOCORE_JSON_H
