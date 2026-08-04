// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wamp.c
 * @brief WAMP message builders (over JsonWriter) + positional array parser (pure, host-tested).
 */

#include "services/iot/wamp/wamp.h"

#if PC_ENABLE_WAMP

#include "network_drivers/presentation/codec/json/json.h"

// Emit a uint64 as a JSON number (JsonWriter's integer() is only platform-long wide).
static void emit_uint(pc_json_writer *w, uint64_t v)
{
    char rev[20];
    size_t r = 0;
    char tmp[21];
    size_t n = 0;
    if (v == 0)
    {
        pc_json_raw(w, "0");
        return;
    }
    while (v)
    {
        rev[r++] = (char)('0' + (int)(v % 10));
        v /= 10;
    }
    while (r)
    {
        tmp[n++] = rev[--r];
    }
    tmp[n] = '\0';
    pc_json_raw(w, tmp);
}

static size_t finish(pc_json_writer *w)
{
    return pc_json_ok(w) ? pc_json_length(w) : 0;
}

// Append the trailing Arguments / ArgumentsKw of a PUBLISH / CALL / YIELD.
static void emit_args(pc_json_writer *w, const char *args_json, const char *kwargs_json)
{
    if (!args_json && !kwargs_json)
    {
        return;
    }
    pc_json_raw(w, args_json ? args_json : "[]"); // kwargs without args still needs a positional Arguments
    if (kwargs_json)
    {
        pc_json_raw(w, kwargs_json);
    }
}

size_t pc_wamp_build_hello(char *buf, size_t cap, const char *realm, const char *details_json)
{
    if (!buf || !realm)
    {
        return 0;
    }
    pc_json_writer w = {0};
    pc_json_init(&w, buf, cap);
    pc_json_begin_array(&w);
    pc_json_int(&w, WAMP_HELLO);
    pc_json_str(&w, realm);
    pc_json_raw(&w, details_json ? details_json : "{}");
    pc_json_end_array(&w);
    return finish(&w);
}

size_t pc_wamp_build_goodbye(char *buf, size_t cap, const char *reason_uri, const char *details_json)
{
    if (!buf || !reason_uri)
    {
        return 0;
    }
    pc_json_writer w = {0};
    pc_json_init(&w, buf, cap);
    pc_json_begin_array(&w);
    pc_json_int(&w, WAMP_GOODBYE);
    pc_json_raw(&w, details_json ? details_json : "{}");
    pc_json_str(&w, reason_uri);
    pc_json_end_array(&w);
    return finish(&w);
}

size_t pc_wamp_build_subscribe(char *buf, size_t cap, uint64_t request, const char *topic, const char *options_json)
{
    if (!buf || !topic)
    {
        return 0;
    }
    pc_json_writer w = {0};
    pc_json_init(&w, buf, cap);
    pc_json_begin_array(&w);
    pc_json_int(&w, WAMP_SUBSCRIBE);
    emit_uint(&w, request);
    pc_json_raw(&w, options_json ? options_json : "{}");
    pc_json_str(&w, topic);
    pc_json_end_array(&w);
    return finish(&w);
}

size_t pc_wamp_build_unsubscribe(char *buf, size_t cap, uint64_t request, uint64_t subscription_id)
{
    if (!buf)
    {
        return 0;
    }
    pc_json_writer w = {0};
    pc_json_init(&w, buf, cap);
    pc_json_begin_array(&w);
    pc_json_int(&w, WAMP_UNSUBSCRIBE);
    emit_uint(&w, request);
    emit_uint(&w, subscription_id);
    pc_json_end_array(&w);
    return finish(&w);
}

size_t pc_wamp_build_unregister(char *buf, size_t cap, uint64_t request, uint64_t registration_id)
{
    if (!buf)
    {
        return 0;
    }
    pc_json_writer w = {0};
    pc_json_init(&w, buf, cap);
    pc_json_begin_array(&w);
    pc_json_int(&w, WAMP_UNREGISTER);
    emit_uint(&w, request);
    emit_uint(&w, registration_id);
    pc_json_end_array(&w);
    return finish(&w);
}

size_t pc_wamp_build_publish(char *buf, size_t cap, uint64_t request, const char *topic, const char *options_json,
                             const char *args_json, const char *kwargs_json)
{
    if (!buf || !topic)
    {
        return 0;
    }
    pc_json_writer w = {0};
    pc_json_init(&w, buf, cap);
    pc_json_begin_array(&w);
    pc_json_int(&w, WAMP_PUBLISH);
    emit_uint(&w, request);
    pc_json_raw(&w, options_json ? options_json : "{}");
    pc_json_str(&w, topic);
    emit_args(&w, args_json, kwargs_json);
    pc_json_end_array(&w);
    return finish(&w);
}

size_t pc_wamp_build_call(char *buf, size_t cap, uint64_t request, const char *procedure, const char *options_json,
                          const char *args_json, const char *kwargs_json)
{
    if (!buf || !procedure)
    {
        return 0;
    }
    pc_json_writer w = {0};
    pc_json_init(&w, buf, cap);
    pc_json_begin_array(&w);
    pc_json_int(&w, WAMP_CALL);
    emit_uint(&w, request);
    pc_json_raw(&w, options_json ? options_json : "{}");
    pc_json_str(&w, procedure);
    emit_args(&w, args_json, kwargs_json);
    pc_json_end_array(&w);
    return finish(&w);
}

size_t pc_wamp_build_register(char *buf, size_t cap, uint64_t request, const char *procedure, const char *options_json)
{
    if (!buf || !procedure)
    {
        return 0;
    }
    pc_json_writer w = {0};
    pc_json_init(&w, buf, cap);
    pc_json_begin_array(&w);
    pc_json_int(&w, WAMP_REGISTER);
    emit_uint(&w, request);
    pc_json_raw(&w, options_json ? options_json : "{}");
    pc_json_str(&w, procedure);
    pc_json_end_array(&w);
    return finish(&w);
}

size_t pc_wamp_build_yield(char *buf, size_t cap, uint64_t request, const char *options_json, const char *args_json,
                           const char *kwargs_json)
{
    if (!buf)
    {
        return 0;
    }
    pc_json_writer w = {0};
    pc_json_init(&w, buf, cap);
    pc_json_begin_array(&w);
    pc_json_int(&w, WAMP_YIELD);
    emit_uint(&w, request);
    pc_json_raw(&w, options_json ? options_json : "{}");
    emit_args(&w, args_json, kwargs_json);
    pc_json_end_array(&w);
    return finish(&w);
}

// ---- positional parser ----

static size_t skip_ws(const char *s, size_t i)
{
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')
    {
        i++;
    }
    return i;
}

// Scan a JSON string at s[i]=='"'; return the index past the closing quote, or 0 on error.
static size_t scan_string(const char *s, size_t i)
{
    i++; // past the opening quote
    while (s[i])
    {
        if (s[i] == '\\')
        {
            if (!s[i + 1])
            {
                return 0;
            }
            i += 2;
            continue;
        }
        if (s[i] == '"')
        {
            return i + 1;
        }
        i++;
    }
    return 0;
}

// Scan one JSON value at s[i] (no leading ws); return the index just past it, or 0 on error.
static size_t scan_value(const char *s, size_t i)
{
    if (s[i] == '"')
    {
        return scan_string(s, i);
    }
    if (s[i] == '{' || s[i] == '[')
    {
        char open = s[i], close = (open == '{') ? '}' : ']';
        int depth = 0;
        while (s[i])
        {
            if (s[i] == '"')
            {
                size_t e = scan_string(s, i);
                if (!e)
                {
                    return 0;
                }
                i = e;
                continue;
            }
            if (s[i] == open)
            {
                depth++;
            }
            else if (s[i] == close)
            {
                depth--;
                if (depth == 0)
                {
                    return i + 1;
                }
            }
            i++;
        }
        return 0;
    }
    // bare token: number / true / false / null
    size_t start = i;
    while (s[i] && s[i] != ',' && s[i] != ']' && s[i] != '}' && s[i] != ' ' && s[i] != '\t' && s[i] != '\n' &&
           s[i] != '\r')
    {
        i++;
    }
    return i > start ? i : 0;
}

proto_bool pc_wamp_element(const char *msg, size_t index, const char **start, size_t *len)
{
    if (!msg)
    {
        return PROTO_FALSE;
    }
    size_t i = skip_ws(msg, 0);
    if (msg[i] != '[')
    {
        return PROTO_FALSE;
    }
    i++;
    for (size_t idx = 0;; idx++)
    {
        i = skip_ws(msg, i);
        if (msg[i] == ']' || msg[i] == '\0')
        {
            return PROTO_FALSE; // ran out before reaching index
        }
        size_t s = i;
        size_t e = scan_value(msg, i);
        if (!e)
        {
            return PROTO_FALSE;
        }
        if (idx == index)
        {
            if (start)
            {
                *start = msg + s;
            }
            if (len)
            {
                *len = e - s;
            }
            return PROTO_TRUE;
        }
        i = skip_ws(msg, e);
        if (msg[i] == ',')
        {
            i++;
        }
        else
        {
            return PROTO_FALSE; // ']' (index past end), NUL, or malformed
        }
    }
}

proto_bool pc_wamp_get_uint(const char *msg, size_t index, uint64_t *out)
{
    const char *s;
    size_t n;
    // n == 0 is defensive only: scan_value() either fails (0, rejected inside pc_wamp_element) or
    // returns an index strictly past where it started, so a returned element is never empty.
    if (!pc_wamp_element(msg, index, &s, &n) || n == 0) // GCOVR_EXCL_LINE  n==0 arm unreachable (see above)
    {
        return PROTO_FALSE;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (s[i] < '0' || s[i] > '9')
        {
            return PROTO_FALSE;
        }
        v = v * 10 + (uint64_t)(s[i] - '0');
    }
    if (out)
    {
        *out = v;
    }
    return PROTO_TRUE;
}

proto_bool pc_wamp_get_type(const char *msg, int *out)
{
    uint64_t v;
    if (!pc_wamp_get_uint(msg, 0, &v))
    {
        return PROTO_FALSE;
    }
    if (out)
    {
        *out = (int)v;
    }
    return PROTO_TRUE;
}

proto_bool pc_wamp_get_uri(const char *msg, size_t index, char *out, size_t out_cap)
{
    const char *s;
    size_t n;
    if (!out || out_cap == 0 || !pc_wamp_element(msg, index, &s, &n))
    {
        return PROTO_FALSE;
    }
    // The trailing-quote arm is defensive only: an element that starts with '"' was scanned by
    // scan_string(), which returns the index just past the CLOSING quote or fails outright, so
    // s[n-1] is always '"' once s[0] is.
    if (n < 2 || s[0] != '"' || s[n - 1] != '"') // GCOVR_EXCL_LINE  s[n-1] arm unreachable (see above)
    {
        return PROTO_FALSE;
    }
    size_t body = n - 2;
    if (body + 1 > out_cap) // need room for the NUL
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < body; i++)
    {
        out[i] = s[i + 1];
    }
    out[body] = '\0';
    return PROTO_TRUE;
}

#endif // PC_ENABLE_WAMP
