// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file wamp.c
 * @brief The WAMP message builders, over the Json writer, and the positional element reader.
 *
 * A build opens a JSON list on the caller's buffer, emits the message type code (WAMP sec 3.3),
 * then the elements that message's layout names, and reports the byte count. A read scans one
 * received list element by element and slices, converts, or copies the one at the named position.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_WAMP

#include "services/iot/wamp/wamp.h"

#include "network_drivers/presentation/codec/json/json.h" // Json: the bounded writer a build emits through

static uint8_t json_work[16]; // the borrow an entry takes; Json never reads it

// Emit a uint64 as a JSON number: digits generated low end first, then reversed into the writer.
static void emit_uint(protocore_json_writer *w, uint64_t v)
{
    char rev[20];
    size_t r = 0;
    char tmp[21];
    size_t n = 0;
    if (v == 0)
    {
        JsonV.put_raw_args.w = w;
        JsonV.put_raw_args.literal = "0";
        Json.put_raw(json_work);
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
    JsonV.put_raw_args.w = w;
    JsonV.put_raw_args.literal = tmp;
    Json.put_raw(json_work);
}

// Bind the writer to ns->out and open the list with its message type code (WAMP sec 3.3).
static void begin_msg(uint8_t *restrict work, protocore_json_writer *w, int code)
{
    JsonV.init_args.w = w;
    JsonV.init_args.buf = WampV.out.buf;
    JsonV.init_args.cap = WampV.out.cap;
    Json.init(work);
    JsonV.begin_array_args.w = w;
    Json.begin_array(work);
    JsonV.put_int_args.w = w;
    JsonV.put_int_args.v = code;
    Json.put_int(work);
}

// Close out a build: the byte count in ns->n, 0 unless the writer stayed inside the buffer.
static void finish(uint8_t *restrict work, protocore_json_writer *w)
{
    JsonV.end_array_args.w = w;
    Json.end_array(work);
    WampV.ok = protocore_json_ok(w);
    WampV.n = WampV.ok ? protocore_json_length(w) : 0;
}

// Append the trailing Arguments|list and ArgumentsKw|dict, both left off when neither is set.
// ArgumentsKw sits one position past Arguments, so a keyword-only payload emits `[]` to hold it.
static void emit_args(uint8_t *restrict work, protocore_json_writer *w)
{
    const char *args = WampV.payload.arguments;
    const char *kwargs = WampV.payload.arguments_kw;
    if (!args && !kwargs)
    {
        return;
    }
    JsonV.put_raw_args.w = w;
    JsonV.put_raw_args.literal = args ? args : "[]";
    Json.put_raw(work);
    if (kwargs)
    {
        JsonV.put_raw_args.w = w;
        JsonV.put_raw_args.literal = kwargs;
        Json.put_raw(work);
    }
}

// ---- builders ----

// [HELLO, Realm|uri, Details|dict] (WAMP sec 3.4.1.1).
void protocore_wamp_build_hello(uint8_t *restrict work)
{
    WampV.ok = PROTO_FALSE;
    WampV.n = 0;
    if (!WampV.out.buf || !WampV.uri.realm)
    {
        return;
    }
    protocore_json_writer w = {0};
    begin_msg(work, &w, WAMP_HELLO);
    JsonV.put_str_args.w = &w;
    JsonV.put_str_args.v = WampV.uri.realm;
    Json.put_str(work);
    JsonV.put_raw_args.w = &w;
    JsonV.put_raw_args.literal = WampV.payload.details ? WampV.payload.details : "{}";
    Json.put_raw(work);
    finish(work, &w);
}

// [GOODBYE, Details|dict, Reason|uri] (WAMP sec 3.4.1.4).
void protocore_wamp_build_goodbye(uint8_t *restrict work)
{
    WampV.ok = PROTO_FALSE;
    WampV.n = 0;
    if (!WampV.out.buf || !WampV.uri.reason)
    {
        return;
    }
    protocore_json_writer w = {0};
    begin_msg(work, &w, WAMP_GOODBYE);
    JsonV.put_raw_args.w = &w;
    JsonV.put_raw_args.literal = WampV.payload.details ? WampV.payload.details : "{}";
    Json.put_raw(work);
    JsonV.put_str_args.w = &w;
    JsonV.put_str_args.v = WampV.uri.reason;
    Json.put_str(work);
    finish(work, &w);
}

// [SUBSCRIBE, Request|id, Options|dict, Topic|uri] (WAMP sec 3.4.2.3).
void protocore_wamp_build_subscribe(uint8_t *restrict work)
{
    WampV.ok = PROTO_FALSE;
    WampV.n = 0;
    if (!WampV.out.buf || !WampV.uri.topic)
    {
        return;
    }
    protocore_json_writer w = {0};
    begin_msg(work, &w, WAMP_SUBSCRIBE);
    emit_uint(&w, WampV.id.request);
    JsonV.put_raw_args.w = &w;
    JsonV.put_raw_args.literal = WampV.payload.options ? WampV.payload.options : "{}";
    Json.put_raw(work);
    JsonV.put_str_args.w = &w;
    JsonV.put_str_args.v = WampV.uri.topic;
    Json.put_str(work);
    finish(work, &w);
}

// [UNSUBSCRIBE, Request|id, SUBSCRIBED.Subscription|id] (WAMP sec 3.4.2.5).
void protocore_wamp_build_unsubscribe(uint8_t *restrict work)
{
    WampV.ok = PROTO_FALSE;
    WampV.n = 0;
    if (!WampV.out.buf)
    {
        return;
    }
    protocore_json_writer w = {0};
    begin_msg(work, &w, WAMP_UNSUBSCRIBE);
    emit_uint(&w, WampV.id.request);
    emit_uint(&w, WampV.id.subscription);
    finish(work, &w);
}

// [PUBLISH, Request|id, Options|dict, Topic|uri] and its payload tail (WAMP sec 3.4.2.1).
void protocore_wamp_build_publish(uint8_t *restrict work)
{
    WampV.ok = PROTO_FALSE;
    WampV.n = 0;
    if (!WampV.out.buf || !WampV.uri.topic)
    {
        return;
    }
    protocore_json_writer w = {0};
    begin_msg(work, &w, WAMP_PUBLISH);
    emit_uint(&w, WampV.id.request);
    JsonV.put_raw_args.w = &w;
    JsonV.put_raw_args.literal = WampV.payload.options ? WampV.payload.options : "{}";
    Json.put_raw(work);
    JsonV.put_str_args.w = &w;
    JsonV.put_str_args.v = WampV.uri.topic;
    Json.put_str(work);
    emit_args(work, &w);
    finish(work, &w);
}

// [CALL, Request|id, Options|dict, Procedure|uri] and its payload tail (WAMP sec 3.4.3.1).
void protocore_wamp_build_call(uint8_t *restrict work)
{
    WampV.ok = PROTO_FALSE;
    WampV.n = 0;
    if (!WampV.out.buf || !WampV.uri.procedure)
    {
        return;
    }
    protocore_json_writer w = {0};
    begin_msg(work, &w, WAMP_CALL);
    emit_uint(&w, WampV.id.request);
    JsonV.put_raw_args.w = &w;
    JsonV.put_raw_args.literal = WampV.payload.options ? WampV.payload.options : "{}";
    Json.put_raw(work);
    JsonV.put_str_args.w = &w;
    JsonV.put_str_args.v = WampV.uri.procedure;
    Json.put_str(work);
    emit_args(work, &w);
    finish(work, &w);
}

// [REGISTER, Request|id, Options|dict, Procedure|uri] (WAMP sec 3.4.3.3).
void protocore_wamp_build_register(uint8_t *restrict work)
{
    WampV.ok = PROTO_FALSE;
    WampV.n = 0;
    if (!WampV.out.buf || !WampV.uri.procedure)
    {
        return;
    }
    protocore_json_writer w = {0};
    begin_msg(work, &w, WAMP_REGISTER);
    emit_uint(&w, WampV.id.request);
    JsonV.put_raw_args.w = &w;
    JsonV.put_raw_args.literal = WampV.payload.options ? WampV.payload.options : "{}";
    Json.put_raw(work);
    JsonV.put_str_args.w = &w;
    JsonV.put_str_args.v = WampV.uri.procedure;
    Json.put_str(work);
    finish(work, &w);
}

// [UNREGISTER, Request|id, REGISTERED.Registration|id] (WAMP sec 3.4.3.5).
void protocore_wamp_build_unregister(uint8_t *restrict work)
{
    WampV.ok = PROTO_FALSE;
    WampV.n = 0;
    if (!WampV.out.buf)
    {
        return;
    }
    protocore_json_writer w = {0};
    begin_msg(work, &w, WAMP_UNREGISTER);
    emit_uint(&w, WampV.id.request);
    emit_uint(&w, WampV.id.registration);
    finish(work, &w);
}

// [YIELD, INVOCATION.Request|id, Options|dict] and its payload tail (WAMP sec 3.4.3.8).
void protocore_wamp_build_yield(uint8_t *restrict work)
{
    WampV.ok = PROTO_FALSE;
    WampV.n = 0;
    if (!WampV.out.buf)
    {
        return;
    }
    protocore_json_writer w = {0};
    begin_msg(work, &w, WAMP_YIELD);
    emit_uint(&w, WampV.id.request);
    JsonV.put_raw_args.w = &w;
    JsonV.put_raw_args.literal = WampV.payload.options ? WampV.payload.options : "{}";
    Json.put_raw(work);
    emit_args(work, &w);
    finish(work, &w);
}

// ---- positional reader ----

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

// Slice the raw element at ns->parse.index out of the received list (WAMP sec 3.3) into ns->text
// and ns->n.
void protocore_wamp_element(uint8_t *restrict work)
{
    (void)work;
    WampV.ok = PROTO_FALSE;
    WampV.text = NULL;
    WampV.n = 0;
    const char *msg = WampV.parse.msg;
    if (!msg)
    {
        return;
    }
    size_t i = skip_ws(msg, 0);
    if (msg[i] != '[')
    {
        return;
    }
    i++;
    for (size_t idx = 0;; idx++)
    {
        i = skip_ws(msg, i);
        if (msg[i] == ']' || msg[i] == '\0')
        {
            return; // ran out before reaching the index
        }
        size_t s = i;
        size_t e = scan_value(msg, i);
        if (!e)
        {
            return;
        }
        if (idx == WampV.parse.index)
        {
            WampV.text = msg + s;
            WampV.n = e - s;
            WampV.ok = PROTO_TRUE;
            return;
        }
        i = skip_ws(msg, e);
        if (msg[i] != ',')
        {
            return; // ']' (the index is past the end), NUL, or malformed
        }
        i++;
    }
}

// Read the element at ns->parse.index as an id, decimal digits only (WAMP sec 2.1.2), into ns->u64.
void protocore_wamp_get_id(uint8_t *restrict work)
{
    WampV.u64 = 0;
    protocore_wamp_element(work);
    // n == 0 is defensive only: scan_value() either fails (0, rejected inside wamp_element) or returns
    // an index strictly past where it started, so a sliced element is never empty.
    if (!WampV.ok || WampV.n == 0)
    {
        WampV.ok = PROTO_FALSE;
        return;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < WampV.n; i++)
    {
        const char c = WampV.text[i];
        if (c < '0' || c > '9')
        {
            WampV.ok = PROTO_FALSE;
            return;
        }
        v = v * 10 + (uint64_t)(c - '0');
    }
    WampV.u64 = v;
}

// Read the message type code, element 0 of the list (WAMP sec 3.5), into ns->i32.
void protocore_wamp_get_type(uint8_t *restrict work)
{
    WampV.i32 = 0;
    WampV.parse.index = 0;
    protocore_wamp_get_id(work);
    if (WampV.ok)
    {
        WampV.i32 = (int32_t)WampV.u64;
    }
}

// Copy the URI element at ns->parse.index into ns->parse.uri_out, the quotes stripped. WAMP sec
// 2.1.1 bars whitespace and `#` from URI components, so the copy carries no escape to undo.
void protocore_wamp_get_uri(uint8_t *restrict work)
{
    char *out = WampV.parse.uri_out;
    const size_t cap = WampV.parse.uri_cap;
    if (!out || cap == 0)
    {
        WampV.ok = PROTO_FALSE;
        return;
    }
    protocore_wamp_element(work);
    if (!WampV.ok)
    {
        return;
    }
    WampV.ok = PROTO_FALSE;
    const char *s = WampV.text;
    const size_t n = WampV.n;
    // The trailing-quote arm is defensive only: an element that starts with '"' was scanned by
    // scan_string(), which returns the index just past the CLOSING quote or fails outright, so
    // s[n-1] is always '"' once s[0] is.
    if (n < 2 || s[0] != '"' || s[n - 1] != '"')
    {
        return;
    }
    const size_t body = n - 2;
    if (body + 1 > cap) // room for the NUL
    {
        return;
    }
    for (size_t i = 0; i < body; i++)
    {
        out[i] = s[i + 1];
    }
    out[body] = '\0';
    WampV.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
WampVars WampV;

#endif // PROTOCORE_ENABLE_WAMP
