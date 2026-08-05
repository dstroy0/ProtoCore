// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file json.c
 * @brief Implementation of the zero-heap JSON writer and top-level reader.
 */

#include "json.h"
#include "mmgr/membuild.h"         // pc_sb frame builder
#include "shared_primitives/hex.h" // PC_HEX_LOWER - the shared digit table
#include "mmgr/protostr.h"
#include <stdio.h>

// ---------------------------------------------------------------------------
// pc_json_writer
// ---------------------------------------------------------------------------

void pc_json_init(pc_json_writer *w, char *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->ok = (buf != NULL && cap >= 1) ? PROTO_TRUE : PROTO_FALSE;
    w->after_key = PROTO_FALSE;
    w->depth = 0;
    for (size_t i = 0; i < JSON_MAX_DEPTH; i++)
    {
        w->need_comma[i] = PROTO_FALSE;
    }
    if (w->ok)
    {
        w->buf[0] = '\0';
    }
}

static void json_put(pc_json_writer *w, char c)
{
    if (!w->ok)
    {
        return;
    }
    if (w->len + 1 >= w->cap) // leave room for the NUL
    {
        w->ok = PROTO_FALSE;
        return;
    }
    w->buf[w->len++] = c;
    w->buf[w->len] = '\0';
}

static void json_put_raw(pc_json_writer *w, const char *s)
{
    if (!s)
    {
        return;
    }
    for (; *s; s++)
    {
        json_put(w, *s);
    }
}

static void json_put_escaped(pc_json_writer *w, const char *s)
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
            json_put(w, '\\');
            json_put(w, '"');
            break;
        case '\\':
            json_put(w, '\\');
            json_put(w, '\\');
            break;
        case '\n':
            json_put(w, '\\');
            json_put(w, 'n');
            break;
        case '\r':
            json_put(w, '\\');
            json_put(w, 'r');
            break;
        case '\t':
            json_put(w, '\\');
            json_put(w, 't');
            break;
        case '\b':
            json_put(w, '\\');
            json_put(w, 'b');
            break;
        case '\f':
            json_put(w, '\\');
            json_put(w, 'f');
            break;
        default:
            if (c < 0x20)
            {
                // Control char -> \u00XX
                json_put(w, '\\');
                json_put(w, 'u');
                json_put(w, '0');
                json_put(w, '0');
                json_put(w, PC_HEX_LOWER[(c >> 4) & 0x0f]);
                json_put(w, PC_HEX_LOWER[c & 0x0f]);
            }
            else
            {
                json_put(w, (char)c);
            }
            break;
        }
    }
}

static void json_value_prefix(pc_json_writer *w)
{
    if (w->after_key)
    {
        w->after_key = PROTO_FALSE; // a value right after pc_json_key(w): the comma was its own
        return;
    }
    if (w->depth > 0)
    {
        size_t lvl = (size_t)(w->depth - 1);
        if (w->need_comma[lvl])
        {
            json_put(w, ',');
        }
        w->need_comma[lvl] = PROTO_TRUE;
    }
}

static void json_push(pc_json_writer *w, char open)
{
    json_value_prefix(w);
    json_put(w, open);
    if (w->depth < JSON_MAX_DEPTH)
    {
        w->need_comma[w->depth] = PROTO_FALSE;
        w->depth++;
    }
    else
    {
        w->ok = PROTO_FALSE; // nesting too deep
    }
}

static void json_pop(pc_json_writer *w, char close)
{
    json_put(w, close);
    if (w->depth > 0)
    {
        w->depth--;
    }
    else
    {
        w->ok = PROTO_FALSE; // unbalanced close
    }
}

void pc_json_begin_object(pc_json_writer *w)
{
    json_push(w, '{');
}
void pc_json_end_object(pc_json_writer *w)
{
    json_pop(w, '}');
}
void pc_json_begin_array(pc_json_writer *w)
{
    json_push(w, '[');
}
void pc_json_end_array(pc_json_writer *w)
{
    json_pop(w, ']');
}

void pc_json_key(pc_json_writer *w, const char *k)
{
    json_value_prefix(w);
    json_put(w, '"');
    json_put_escaped(w, k);
    json_put(w, '"');
    json_put(w, ':');
    w->after_key = PROTO_TRUE; // suppress the following value's own comma
}

void pc_json_str(pc_json_writer *w, const char *v)
{
    json_value_prefix(w);
    json_put(w, '"');
    json_put_escaped(w, v);
    json_put(w, '"');
}

void pc_json_int(pc_json_writer *w, long v)
{
    char tmp[24];
    pc_sb sb_tmp = {tmp, sizeof(tmp), 0, PROTO_TRUE};
    pc_sb_i64(&sb_tmp, (int64_t)(v));
    if (pc_sb_finish(&sb_tmp) == 0)
    {
        tmp[0] = '\0';
    }
    json_value_prefix(w);
    json_put_raw(w, tmp);
}

void pc_json_uint(pc_json_writer *w, unsigned long v)
{
    char tmp[24];
    pc_sb sb_tmp2 = {tmp, sizeof(tmp), 0, PROTO_TRUE};
    pc_sb_u32(&sb_tmp2, (uint32_t)(v));
    if (pc_sb_finish(&sb_tmp2) == 0)
    {
        tmp[0] = '\0';
    }
    json_value_prefix(w);
    json_put_raw(w, tmp);
}

void pc_json_bool(pc_json_writer *w, proto_bool v)
{
    json_value_prefix(w);
    json_put_raw(w, v ? "true" : "false");
}

void pc_json_null(pc_json_writer *w)
{
    json_value_prefix(w);
    json_put_raw(w, "null");
}

void pc_json_raw(pc_json_writer *w, const char *literal)
{
    json_value_prefix(w);
    json_put_raw(w, literal);
}

void pc_json_kv_str(pc_json_writer *w, const char *k, const char *v)
{
    pc_json_key(w, k);
    pc_json_str(w, v);
}
void pc_json_kv_int(pc_json_writer *w, const char *k, long v)
{
    pc_json_key(w, k);
    pc_json_int(w, v);
}
void pc_json_kv_uint(pc_json_writer *w, const char *k, unsigned long v)
{
    pc_json_key(w, k);
    pc_json_uint(w, v);
}
void pc_json_kv_bool(pc_json_writer *w, const char *k, proto_bool v)
{
    pc_json_key(w, k);
    pc_json_bool(w, v);
}
void pc_json_kv_null(pc_json_writer *w, const char *k)
{
    pc_json_key(w, k);
    pc_json_null(w);
}
void pc_json_kv_raw(pc_json_writer *w, const char *k, const char *literal)
{
    pc_json_key(w, k);
    pc_json_raw(w, literal);
}

// ---------------------------------------------------------------------------
// Reader (top-level object members)
// ---------------------------------------------------------------------------

static proto_bool is_ws(char c)
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
// the first character of the value (ws-skipped), or NULL if not found.
static const char *json_find_value(const char *json, const char *key)
{
    if (!json || !key)
    {
        return NULL;
    }
    const char *p = skip_ws(json);
    if (*p != '{')
    {
        return NULL;
    }
    p++; // into the object

#define PC_key_max 256 // member names the server looks up are short; bound the needle defensively
    size_t keylen = strnlen(key, PC_key_max);
    while (PROTO_TRUE)
    {
        p = skip_ws(p);
        if (*p == '}' || *p == '\0')
        {
            return NULL;
        }
        if (*p != '"')
        {
            return NULL; // expected a member name
        }

        const char *kstart = p + 1;
        const char *kend = skip_string(p); // just past closing quote
        size_t klen = (kend > kstart) ? (size_t)((kend - 1) - kstart) : 0;
        proto_bool match = (klen == keylen) && (strncmp(kstart, key, klen) == 0);

        p = skip_ws(kend);
        if (*p != ':')
        {
            return NULL;
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
        return NULL; // '}' or malformed
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

typedef enum PROTO_ENUM_PACKED
{
    JSON_ESC_LITERAL_C, // *c_out holds one char for the caller's common write path
    JSON_ESC_EMITTED,   // UTF-8 bytes already written to out; caller advances p and continues
    JSON_ESC_TRUNCATED  // sequence would overflow out_cap; caller returns
} JsonEsc;

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

static long json_decode_u(const char **pp)
{
    const char *p = *pp;      // advanced locally, published back to the caller on the success path
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
    *pp = p;
    return (long)cp;
}

// Encode code point cp as UTF-8 into out at *i (bounded by out_cap): <= 0x7F one byte, then 2/3/4
// bytes. Returns EMITTED on success, or TRUNCATED (writing a NUL) if the whole sequence will not fit.
static JsonEsc json_emit_utf8(unsigned cp, char *out, size_t *i, size_t out_cap)
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
    if (*i + (size_t)un >= out_cap)
    {
        out[*i] = '\0'; // the whole UTF-8 sequence must fit; truncate cleanly
        return JSON_ESC_TRUNCATED;
    }
    for (int k = 0; k < un; k++)
    {
        out[(*i)++] = (char)u8[k];
    }
    return JSON_ESC_EMITTED;
}

// Decode one JSON string escape. On entry p points at the escape char (just past the '\'). Simple
// escapes and malformed \u yield LITERAL_C (resolved char in *c_out); a valid \uXXXX emits its UTF-8
// bytes directly (EMITTED) or reports TRUNCATED when the whole sequence will not fit. p is left on
// the last consumed byte so the caller can advance past it uniformly.
static JsonEsc json_decode_escape(const char **p, char *out, size_t *i, size_t out_cap, char *c_out)
{
    switch (**p)
    {
    case 'n':
        *c_out = '\n';
        return JSON_ESC_LITERAL_C;
    case 't':
        *c_out = '\t';
        return JSON_ESC_LITERAL_C;
    case 'r':
        *c_out = '\r';
        return JSON_ESC_LITERAL_C;
    case 'b':
        *c_out = '\b';
        return JSON_ESC_LITERAL_C;
    case 'f':
        *c_out = '\f';
        return JSON_ESC_LITERAL_C;
    case '"':
        *c_out = '"';
        return JSON_ESC_LITERAL_C;
    case '\\':
        *c_out = '\\';
        return JSON_ESC_LITERAL_C;
    case '/':
        *c_out = '/';
        return JSON_ESC_LITERAL_C;
    case 'u':
        break; // \uXXXX handled below
    default:
        *c_out = **p;
        return JSON_ESC_LITERAL_C;
    }

    // \uXXXX -> UTF-8. <= 0x7F stays one byte; 0x80..0x7FF / 0x800..0xFFFF and (via a surrogate pair)
    // 0x10000..0x10FFFF emit 2/3/4 bytes. Malformed / short hex -> '?', rescanned as literals.
    long cp = json_decode_u(p);
    if (cp < 0)
    {
        *c_out = '?';
        return JSON_ESC_LITERAL_C;
    }
    return json_emit_utf8((unsigned)cp, out, i, out_cap);
}

proto_bool json_get_str(const char *json, const char *key, char *out, size_t out_cap)
{
    if (!out || out_cap == 0)
    {
        return PROTO_FALSE;
    }
    const char *v = json_find_value(json, key);
    if (!v || *v != '"')
    {
        return PROTO_FALSE;
    }

    const char *p = v + 1;
    size_t i = 0;
    while (*p && *p != '"')
    {
        char c = *p;
        if (c == '\\' && p[1])
        {
            p++;
            JsonEsc r = json_decode_escape(&p, out, &i, out_cap, &c);
            if (r == JSON_ESC_TRUNCATED)
            {
                return PROTO_TRUE;
            }
            if (r == JSON_ESC_EMITTED)
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
            return PROTO_TRUE;
        }
        p++;
    }
    out[i] = '\0';
    return PROTO_TRUE;
}

proto_bool json_get_int(const char *json, const char *key, long *out)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    const char *v = json_find_value(json, key);
    if (!v || *v == '"') // must be a bare number, not a string
    {
        return PROTO_FALSE;
    }
    const char *end = NULL;
    long val = str.to_long(v, &end);
    if (end == v)
    {
        return PROTO_FALSE; // no digits parsed
    }
    *out = val;
    return PROTO_TRUE;
}

proto_bool json_get_bool(const char *json, const char *key, proto_bool *out)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    const char *v = json_find_value(json, key);
    if (!v)
    {
        return PROTO_FALSE;
    }
    if (strncmp(v, "true", 4) == 0 && (v[4] == '\0' || v[4] == ',' || v[4] == '}' || v[4] == ']' || is_ws(v[4])))
    {
        *out = PROTO_TRUE;
        return PROTO_TRUE;
    }
    if (strncmp(v, "false", 5) == 0 && (v[5] == '\0' || v[5] == ',' || v[5] == '}' || v[5] == ']' || is_ws(v[5])))
    {
        *out = PROTO_FALSE;
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}
