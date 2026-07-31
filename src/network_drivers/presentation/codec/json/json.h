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
 *   JsonWriter w(buf, sizeof(buf));
 *   w.begin_object();
 *     w.kv_str("status", "ok");
 *     w.kv_int("count", 3);
 *     w.key("items"); w.begin_array();
 *       w.str("a"); w.str("b");
 *     w.end_array();
 *   w.end_object();
 *   if (w.ok()) server.send(slot, 200, "application/json", w.c_str());
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
#include <stddef.h>
#include <stdint.h>

/**
 * @class JsonWriter
 * @brief Builds a JSON document into a fixed caller buffer, no heap.
 *
 * Commas, key quoting, and string escaping are emitted automatically. On buffer
 * overflow or a structural error (nesting past JSON_MAX_DEPTH), writing stops
 * and ok() returns false; c_str() still yields a NUL-terminated (truncated)
 * string so a partial result never runs off the end.
 *
 * Value methods (str(), integer(), ...) emit a single value - use them for array
 * elements or immediately after key(). The kv_*() helpers emit a key and value
 * together for object members.
 */
class JsonWriter
{
  public:
    /**
     * @brief Construct over a caller buffer.
     * @param buf  Destination (must be non-null, cap >= 1).
     * @param cap  Capacity in bytes including the NUL terminator.
     */
    JsonWriter(char *buf, size_t cap);

    void begin_object(); ///< Open `{` (as a value/element where applicable).
    void end_object();   ///< Close `}`.
    void begin_array();  ///< Open `[`.
    void end_array();    ///< Close `]`.

    /// @brief Emit an object member name (`"k":`); follow with one value.
    void key(const char *k);

    void str(const char *v);        ///< Emit a quoted, escaped string value.
    void integer(long v);           ///< Emit a signed integer value.
    void uinteger(unsigned long v); ///< Emit an unsigned integer value.
    void boolean(bool v);           ///< Emit `true`/`false`.
    void null_value();              ///< Emit `null`.
    void raw(const char *literal);  ///< Emit a pre-formatted literal verbatim.

    void kv_str(const char *k, const char *v);       ///< `"k":"v"` (escaped).
    void kv_int(const char *k, long v);              ///< `"k":<int>`.
    void kv_uint(const char *k, unsigned long v);    ///< `"k":<uint>`.
    void kv_bool(const char *k, bool v);             ///< `"k":true|false`.
    void kv_null(const char *k);                     ///< `"k":null`.
    void kv_raw(const char *k, const char *literal); ///< `"k":<literal>`.

    bool ok() const ///< False after any overflow / structural error.
    {
        return _ok;
    }
    size_t length() const ///< Bytes written so far (excludes the NUL).
    {
        return _len;
    }
    const char *c_str() const ///< NUL-terminated output (truncated if !ok()).
    {
        return _buf;
    }

  private:
    void put(char c);
    void put_raw(const char *s);
    void put_escaped(const char *s);
    void value_prefix(); // emit a separating comma at the current level if needed
    void push(char open);
    void pop(char close);

    char *_buf;
    size_t _cap;
    size_t _len;
    bool _ok;
    bool _after_key;                  // next value follows a key(): suppress its comma
    uint8_t _depth;                   // open containers
    bool _need_comma[JSON_MAX_DEPTH]; // per-level: has a prior item been emitted?
};

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
bool json_get_str(const char *json, const char *key, char *out, size_t out_cap);

/**
 * @brief Read a top-level integer member from a JSON object body.
 * @return true if the member exists and parses as an integer; false otherwise.
 */
bool json_get_int(const char *json, const char *key, long *out);

/**
 * @brief Read a top-level boolean member (`true`/`false`) from a JSON object body.
 * @return true if the member exists and is a JSON boolean; false otherwise.
 */
bool json_get_bool(const char *json, const char *key, bool *out);

#endif // PROTOCORE_JSON_H
