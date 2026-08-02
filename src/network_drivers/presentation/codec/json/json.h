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
 *   pc_json_init(&w, buf, sizeof(buf));
 *   pc_json_begin_object(&w);
 *     pc_json_kv_str(&w, "status", "ok");
 *     pc_json_kv_int(&w, "count", 3);
 *     pc_json_key(&w, "items"); pc_json_begin_array(&w);
 *       pc_json_str(&w, "a"); pc_json_str(&w, "b");
 *     pc_json_end_array(&w);
 *   pc_json_end_object(&w);
 *   if (pc_json_ok(&w)) server.send(slot, 200, "application/json", pc_json_c_str(&w));
 *   // -> {"status":"ok","count":3,"items":["a","b"]}
 * @endcode
 *
 * ## Reading (top-level keys of an object body)
 * @code
 *   char ssid[33];
 *   if (json_get_str(req->body, "ssid", ssid, sizeof(ssid))) { ... }
 *   long port;
 *   if (json_get_int(req->body, "port", &port)) { ... }
 * @endcode
 */

#ifndef PROTOCORE_JSON_H
#define PROTOCORE_JSON_H

#include "protocore_config.h"

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
 * @brief Bind @p w to a caller buffer.
 * @param buf  Destination (must be non-null, cap >= 1).
 * @param cap  Capacity in bytes including the NUL terminator.
 */
void pc_json_init(pc_json_writer *w, char *buf, size_t cap);

void pc_json_begin_object(pc_json_writer *w); ///< Open `{` (as a value/element where applicable).
void pc_json_end_object(pc_json_writer *w);   ///< Close `}`.
void pc_json_begin_array(pc_json_writer *w);  ///< Open `[`.
void pc_json_end_array(pc_json_writer *w);    ///< Close `]`.

/// @brief Emit an object member name (`"k":`); follow with one value.
void pc_json_key(pc_json_writer *w, const char *k);

void pc_json_str(pc_json_writer *w, const char *v);       ///< Emit a quoted, escaped string value.
void pc_json_int(pc_json_writer *w, long v);              ///< Emit a signed integer value.
void pc_json_uint(pc_json_writer *w, unsigned long v);    ///< Emit an unsigned integer value.
void pc_json_bool(pc_json_writer *w, proto_bool v);       ///< Emit `true`/`false`.
void pc_json_null(pc_json_writer *w);                     ///< Emit `null`.
void pc_json_raw(pc_json_writer *w, const char *literal); ///< Emit a pre-formatted literal verbatim.

void pc_json_kv_str(pc_json_writer *w, const char *k, const char *v);       ///< `"k":"v"` (escaped).
void pc_json_kv_int(pc_json_writer *w, const char *k, long v);              ///< `"k":<int>`.
void pc_json_kv_uint(pc_json_writer *w, const char *k, unsigned long v);    ///< `"k":<uint>`.
void pc_json_kv_bool(pc_json_writer *w, const char *k, proto_bool v);       ///< `"k":true|false`.
void pc_json_kv_null(pc_json_writer *w, const char *k);                     ///< `"k":null`.
void pc_json_kv_raw(pc_json_writer *w, const char *k, const char *literal); ///< `"k":<literal>`.

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

/**
 * @brief Read a top-level string member from a JSON object body.
 *
 * Finds `"key": "..."` at the root object level (nested objects/arrays and
 * string contents are skipped, so a same-named nested key is not matched),
 * unescapes the value, and copies it (NUL-terminated, bounded by @p out_cap)
 * into @p out.
 *
 * @param json     NUL-terminated JSON object text.
 * @param key      Member name to find.
 * @param out      Destination buffer.
 * @param out_cap  Capacity of @p out including the NUL.
 * @return true if a string member was found and copied; false otherwise.
 */
proto_bool json_get_str(const char *json, const char *key, char *out, size_t out_cap);

/**
 * @brief Read a top-level integer member from a JSON object body.
 * @return true if the member exists and parses as an integer; false otherwise.
 */
proto_bool json_get_int(const char *json, const char *key, long *out);

/**
 * @brief Read a top-level boolean member (`true`/`false`) from a JSON object body.
 * @return true if the member exists and is a JSON boolean; false otherwise.
 */
proto_bool json_get_bool(const char *json, const char *key, proto_bool *out);

#endif // PROTOCORE_JSON_H
