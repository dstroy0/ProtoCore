// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file json.cpp
 * @brief Implementation of the zero-heap JSON writer and top-level reader.
 */

#include "json.h"
#include "mmgr/membuild.h" // pc_sb frame builder
#include "shared_primitives/numparse.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// JsonWriter
// ---------------------------------------------------------------------------

JsonWriter::JsonWriter(char *buf, size_t cap)
    : _buf(buf), _cap(cap), _len(0), _ok(buf != nullptr && cap >= 1), _after_key(false), _depth(0)
{
    if (_ok)
    {
        _buf[0] = '\0';
    }
}

void JsonWriter::put(char c)
{
    if (!_ok)
    {
        return;
    }
    if (_len + 1 >= _cap) // leave room for the NUL
    {
        _ok = false;
        return;
    }
    _buf[_len++] = c;
    _buf[_len] = '\0';
}

void JsonWriter::put_raw(const char *s)
{
    if (!s)
    {
        return;
    }
    for (; *s; s++)
    {
        put(*s);
    }
}

void JsonWriter::put_escaped(const char *s)
{
    if (!s)
    {
        return;
    }
    for (; *s; s++)
    {
        unsigned char c = (unsigned char)*s;
        switch (c)
        {
        case '"':
            put('\\');
            put('"');
            break;
        case '\\':
            put('\\');
            put('\\');
            break;
        case '\n':
            put('\\');
            put('n');
            break;
        case '\r':
            put('\\');
            put('r');
            break;
        case '\t':
            put('\\');
            put('t');
            break;
        case '\b':
            put('\\');
            put('b');
            break;
        case '\f':
            put('\\');
            put('f');
            break;
        default:
            if (c < 0x20)
            {
                // Control char -> \u00XX
                static const char hexd[] = "0123456789abcdef";
                put('\\');
                put('u');
                put('0');
                put('0');
                put(hexd[(c >> 4) & 0x0f]);
                put(hexd[c & 0x0f]);
            }
            else
            {
                put((char)c);
            }
            break;
        }
    }
}

void JsonWriter::value_prefix()
{
    if (_after_key)
    {
        _after_key = false; // a value right after key(): the comma was its own
        return;
    }
    if (_depth > 0)
    {
        size_t lvl = (size_t)(_depth - 1);
        if (_need_comma[lvl])
        {
            put(',');
        }
        _need_comma[lvl] = true;
    }
}

void JsonWriter::push(char open)
{
    value_prefix();
    put(open);
    if (_depth < JSON_MAX_DEPTH)
    {
        _need_comma[_depth] = false;
        _depth++;
    }
    else
    {
        _ok = false; // nesting too deep
    }
}

void JsonWriter::pop(char close)
{
    put(close);
    if (_depth > 0)
    {
        _depth--;
    }
    else
    {
        _ok = false; // unbalanced close
    }
}

void JsonWriter::begin_object()
{
    push('{');
}
void JsonWriter::end_object()
{
    pop('}');
}
void JsonWriter::begin_array()
{
    push('[');
}
void JsonWriter::end_array()
{
    pop(']');
}

void JsonWriter::key(const char *k)
{
    value_prefix();
    put('"');
    put_escaped(k);
    put('"');
    put(':');
    _after_key = true; // suppress the following value's own comma
}

void JsonWriter::str(const char *v)
{
    value_prefix();
    put('"');
    put_escaped(v);
    put('"');
}

void JsonWriter::integer(long v)
{
    char tmp[24];
    pc_sb sb_tmp = {tmp, sizeof(tmp), 0, true};
    pc_sb_i64(&sb_tmp, (int64_t)(v));
    if (pc_sb_finish(&sb_tmp) == 0)
    {
        tmp[0] = '\0';
    }
    value_prefix();
    put_raw(tmp);
}

void JsonWriter::uinteger(unsigned long v)
{
    char tmp[24];
    pc_sb sb_tmp2 = {tmp, sizeof(tmp), 0, true};
    pc_sb_u32(&sb_tmp2, (uint32_t)(v));
    if (pc_sb_finish(&sb_tmp2) == 0)
    {
        tmp[0] = '\0';
    }
    value_prefix();
    put_raw(tmp);
}

void JsonWriter::boolean(bool v)
{
    value_prefix();
    put_raw(v ? "true" : "false");
}

void JsonWriter::null_value()
{
    value_prefix();
    put_raw("null");
}

void JsonWriter::raw(const char *literal)
{
    value_prefix();
    put_raw(literal);
}

void JsonWriter::kv_str(const char *k, const char *v)
{
    key(k);
    str(v);
}
void JsonWriter::kv_int(const char *k, long v)
{
    key(k);
    integer(v);
}
void JsonWriter::kv_uint(const char *k, unsigned long v)
{
    key(k);
    uinteger(v);
}
void JsonWriter::kv_bool(const char *k, bool v)
{
    key(k);
    boolean(v);
}
void JsonWriter::kv_null(const char *k)
{
    key(k);
    null_value();
}
void JsonWriter::kv_raw(const char *k, const char *literal)
{
    key(k);
    raw(literal);
}

// ---------------------------------------------------------------------------
// Reader (top-level object members)
// ---------------------------------------------------------------------------

static bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static const char *skip_ws(const char *p)
{
    while (*p && is_ws(*p))
    {
        p++;
    }
    return p;
}

// p points at the opening quote; returns the pointer just past the closing quote
// (or at the terminating NUL if unterminated). Honors backslash escapes.
static const char *skip_string(const char *p)
{
    p++; // opening quote
    while (*p)
    {
        if (*p == '\\' && p[1])
        {
            p += 2;
            continue;
        }
        if (*p == '"')
        {
            return p + 1;
        }
        p++;
    }
    return p;
}

// Skip one JSON value starting at p (ws already consumed). Returns the pointer
// just past the value.
static const char *skip_value(const char *p)
{
    if (*p == '"')
    {
        return skip_string(p);
    }
    if (*p == '{' || *p == '[')
    {
        char open = *p;
        char close = (open == '{') ? '}' : ']';
        int depth = 0;
        while (*p)
        {
            if (*p == '"')
            {
                p = skip_string(p);
                continue;
            }
            if (*p == open)
            {
                depth++;
            }
            else if (*p == close)
            {
                depth--;
                if (depth == 0)
                {
                    return p + 1;
                }
            }
            p++;
        }
        return p;
    }
    // primitive: number / true / false / null
    while (*p && *p != ',' && *p != '}' && *p != ']' && !is_ws(*p))
    {
        p++;
    }
    return p;
}

// Locate the value of a top-level @p key in object @p json. Returns a pointer to
// the first character of the value (ws-skipped), or nullptr if not found.
static const char *json_find_value(const char *json, const char *key)
{
    if (!json || !key)
    {
        return nullptr;
    }
    const char *p = skip_ws(json);
    if (*p != '{')
    {
        return nullptr;
    }
    p++; // into the object

#define PC_key_max 256 // member names the server looks up are short; bound the needle defensively
    size_t keylen = strnlen(key, PC_key_max);
    while (true)
    {
        p = skip_ws(p);
        if (*p == '}' || *p == '\0')
        {
            return nullptr;
        }
        if (*p != '"')
        {
            return nullptr; // expected a member name
        }

        const char *kstart = p + 1;
        const char *kend = skip_string(p); // just past closing quote
        size_t klen = (kend > kstart) ? (size_t)((kend - 1) - kstart) : 0;
        bool match = (klen == keylen) && (strncmp(kstart, key, klen) == 0);

        p = skip_ws(kend);
        if (*p != ':')
        {
            return nullptr;
        }
        p = skip_ws(p + 1);

        if (match)
        {
            return p;
        }

        p = skip_value(p);
        p = skip_ws(p);
        if (*p == ',')
        {
            p++;
            continue;
        }
        return nullptr; // '}' or malformed
    }
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

enum class JsonEsc
{
    LITERAL_C, // *c_out holds one char for the caller's common write path
    EMITTED,   // UTF-8 bytes already written to out; caller advances p and continues
    TRUNCATED  // sequence would overflow out_cap; caller returns
};

// Decode a \uXXXX sequence with p at the 'u'. A high surrogate (0xD800..0xDBFF) followed by a low
// surrogate combines into one code point (0x10000..0x10FFFF); an unpaired/lone surrogate becomes
// U+FFFD. On success returns the code point and leaves p on the last consumed byte. Returns -1 for
// malformed / short hex, leaving p unchanged so the caller emits a literal '?'.
// Read the four hex digits at src[0..3] into a 16-bit value; -1 if any is absent or non-hex.
static int json_hex4(const char *src)
{
    int h0 = src[0] ? hex_val(src[0]) : -1;
    int h1 = (h0 >= 0 && src[1]) ? hex_val(src[1]) : -1;
    int h2 = (h1 >= 0 && src[2]) ? hex_val(src[2]) : -1;
    int h3 = (h2 >= 0 && src[3]) ? hex_val(src[3]) : -1;
    if (h3 < 0)
    {
        return -1;
    }
    return (h0 << 12) | (h1 << 8) | (h2 << 4) | h3;
}

static long json_decode_u(const char *&p)
{
    int v = json_hex4(p + 1); // p at 'u'; the four hex digits are p[1..4]
    if (v < 0)
    {
        return -1;
    }
    unsigned cp = (unsigned)v;
    p += 4; // consume the four hex digits (p now at the last one)
    if (cp >= 0xD800 && cp <= 0xDBFF)
    {
        // A high surrogate pairs with a following \uXXXX low surrogate (0xDC00..0xDFFF).
        int lo = (p[1] == '\\' && p[2] == 'u') ? json_hex4(p + 3) : -1;
        if (lo >= 0xDC00 && lo <= 0xDFFF)
        {
            cp = 0x10000u + ((cp - 0xD800u) << 10) + ((unsigned)lo - 0xDC00u);
            p += 6; // consume the low surrogate's \uXXXX too
        }
        else
        {
            cp = 0xFFFDu; // unpaired high surrogate
        }
    }
    else if (cp >= 0xDC00 && cp <= 0xDFFF)
    {
        cp = 0xFFFDu; // lone low surrogate
    }
    return (long)cp;
}

// Encode code point cp as UTF-8 into out at *i (bounded by out_cap): <= 0x7F one byte, then 2/3/4
// bytes. Returns EMITTED on success, or TRUNCATED (writing a NUL) if the whole sequence will not fit.
static JsonEsc json_emit_utf8(unsigned cp, char *out, size_t &i, size_t out_cap)
{
    unsigned char u8[4];
    int un;
    if (cp < 0x80u)
    {
        u8[0] = (unsigned char)cp;
        un = 1;
    }
    else if (cp < 0x800u)
    {
        u8[0] = (unsigned char)(0xC0u | (cp >> 6));
        u8[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
        un = 2;
    }
    else if (cp < 0x10000u)
    {
        u8[0] = (unsigned char)(0xE0u | (cp >> 12));
        u8[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        u8[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
        un = 3;
    }
    else
    {
        u8[0] = (unsigned char)(0xF0u | (cp >> 18));
        u8[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
        u8[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        u8[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
        un = 4;
    }
    if (i + (size_t)un >= out_cap)
    {
        out[i] = '\0'; // the whole UTF-8 sequence must fit; truncate cleanly
        return JsonEsc::TRUNCATED;
    }
    for (int k = 0; k < un; k++)
    {
        out[i++] = (char)u8[k];
    }
    return JsonEsc::EMITTED;
}

// Decode one JSON string escape. On entry p points at the escape char (just past the '\'). Simple
// escapes and malformed \u yield LITERAL_C (resolved char in *c_out); a valid \uXXXX emits its UTF-8
// bytes directly (EMITTED) or reports TRUNCATED when the whole sequence will not fit. p is left on
// the last consumed byte so the caller can advance past it uniformly.
static JsonEsc json_decode_escape(const char *&p, char *out, size_t &i, size_t out_cap, char *c_out)
{
    switch (*p)
    {
    case 'n':
        *c_out = '\n';
        return JsonEsc::LITERAL_C;
    case 't':
        *c_out = '\t';
        return JsonEsc::LITERAL_C;
    case 'r':
        *c_out = '\r';
        return JsonEsc::LITERAL_C;
    case 'b':
        *c_out = '\b';
        return JsonEsc::LITERAL_C;
    case 'f':
        *c_out = '\f';
        return JsonEsc::LITERAL_C;
    case '"':
        *c_out = '"';
        return JsonEsc::LITERAL_C;
    case '\\':
        *c_out = '\\';
        return JsonEsc::LITERAL_C;
    case '/':
        *c_out = '/';
        return JsonEsc::LITERAL_C;
    case 'u':
        break; // \uXXXX handled below
    default:
        *c_out = *p;
        return JsonEsc::LITERAL_C;
    }

    // \uXXXX -> UTF-8. <= 0x7F stays one byte; 0x80..0x7FF / 0x800..0xFFFF and (via a surrogate pair)
    // 0x10000..0x10FFFF emit 2/3/4 bytes. Malformed / short hex -> '?', rescanned as literals.
    long cp = json_decode_u(p);
    if (cp < 0)
    {
        *c_out = '?';
        return JsonEsc::LITERAL_C;
    }
    return json_emit_utf8((unsigned)cp, out, i, out_cap);
}

bool json_get_str(const char *json, const char *key, char *out, size_t out_cap)
{
    if (!out || out_cap == 0)
    {
        return false;
    }
    const char *v = json_find_value(json, key);
    if (!v || *v != '"')
    {
        return false;
    }

    const char *p = v + 1;
    size_t i = 0;
    while (*p && *p != '"')
    {
        char c = *p;
        if (c == '\\' && p[1])
        {
            p++;
            JsonEsc r = json_decode_escape(p, out, i, out_cap, &c);
            if (r == JsonEsc::TRUNCATED)
            {
                return true;
            }
            if (r == JsonEsc::EMITTED)
            {
                p++; // past the last consumed hex digit
                continue;
            }
        }
        if (i + 1 < out_cap)
        {
            out[i++] = c;
        }
        else
        {
            out[i] = '\0'; // truncate to capacity
            return true;
        }
        p++;
    }
    out[i] = '\0';
    return true;
}

bool json_get_int(const char *json, const char *key, long *out)
{
    if (!out)
    {
        return false;
    }
    const char *v = json_find_value(json, key);
    if (!v || *v == '"') // must be a bare number, not a string
    {
        return false;
    }
    const char *end = nullptr;
    long val = pc_strtol(v, &end);
    if (end == v)
    {
        return false; // no digits parsed
    }
    *out = val;
    return true;
}

bool json_get_bool(const char *json, const char *key, bool *out)
{
    if (!out)
    {
        return false;
    }
    const char *v = json_find_value(json, key);
    if (!v)
    {
        return false;
    }
    if (strncmp(v, "true", 4) == 0 && (v[4] == '\0' || v[4] == ',' || v[4] == '}' || v[4] == ']' || is_ws(v[4])))
    {
        *out = true;
        return true;
    }
    if (strncmp(v, "false", 5) == 0 && (v[5] == '\0' || v[5] == ',' || v[5] == '}' || v[5] == ']' || is_ws(v[5])))
    {
        *out = false;
        return true;
    }
    return false;
}
