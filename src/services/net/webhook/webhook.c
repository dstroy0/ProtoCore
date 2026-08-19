// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file webhook.c
 * @brief The IFTTT Maker target URI and JSON content builders, and the POST that sends them.
 *
 * The builders are pure: they write the caller's region and read nothing else. The send hands the
 * target URI (RFC 9110 sec 7.1) and the content (RFC 9110 sec 6.4) to the outbound HTTP client as a
 * POST (RFC 9110 sec 9.3.3) typed application/json (RFC 9110 sec 8.3) and reports the status code
 * that comes back (RFC 9110 sec 15.1). No IETF standard defines the webhook pattern itself.
 */

#include "services/net/webhook/webhook.h"
#include "mmgr/membuild/membuild.h" // protocore_sb: the bounded frame builder the URI is built with
#include "mmgr/protomem/protomem.h" // mem.cpy
#include "mmgr/protostr/protostr.h" // str.len: bounded length, no stdlib

#if PROTOCORE_ENABLE_WEBHOOK

#include "shared/mime/mime.h" // PROTOCORE_MIME_JSON: the Content-Type a POST sends (RFC 9110 sec 8.3)

#if PROTOCORE_ENABLE_HTTP_CLIENT
#include "services/net/http_client/http_client.h"
#endif

// The two fixed path segments of the Maker target URI, with the event and the key between them
// (RFC 3986 sec 3.3).
#define PROTOCORE_IFTTT_MAKER_ROOT "https://maker.ifttt.com/trigger/"
#define PROTOCORE_IFTTT_MAKER_KEY_SEG "/with/key/"

// The frames a trigger builds into, on its own stack.
#define PROTOCORE_WEBHOOK_URI_CAP 160u
#define PROTOCORE_WEBHOOK_CONTENT_CAP 256u

// Append the NUL-terminated s at *pos, terminator included in the bound. Leaves the region
// untouched and reports false when the whole string would not fit.
static proto_bool json_append(char *out, size_t cap, size_t *pos, const char *s)
{
    size_t n = str.len(s, cap + 1);
    if (*pos + n >= cap)
    {
        return PROTO_FALSE;
    }
    mem.cpy(out + *pos, s, n);
    *pos += n;
    out[*pos] = '\0';
    return PROTO_TRUE;
}

// Append s as the body of a JSON string, escaping quotation mark and reverse solidus with a
// preceding reverse solidus (RFC 8259 sec 7). Every other octet is copied as it stands.
static proto_bool json_append_escaped(char *out, size_t cap, size_t *pos, const char *s)
{
    for (; *s; s++)
    {
        char c = *s;
        if (c == '"' || c == '\\')
        {
            if (*pos + 2 >= cap)
            {
                return PROTO_FALSE;
            }
            out[(*pos)++] = '\\';
            out[(*pos)++] = c;
        }
        else
        {
            if (*pos + 1 >= cap)
            {
                return PROTO_FALSE;
            }
            out[(*pos)++] = c;
        }
        out[*pos] = '\0';
    }
    return PROTO_TRUE;
}

// Build "https://maker.ifttt.com/trigger/<event>/with/key/<key>" into the build region: an https
// URI (RFC 9110 sec 4.2.2) whose last two segments are the event and the key.
static void ifttt_url(uint8_t *restrict work)
{
    (void)work;
    char *out = Webhook.build.out;
    const size_t cap = Webhook.build.cap;
    const char *event = Webhook.ifttt.event;
    const char *key = Webhook.ifttt.key;

    Webhook.n = 0;
    if (!out || cap == 0 || !event || !key)
    {
        if (out && cap)
        {
            out[0] = '\0';
        }
        return;
    }
    protocore_sb sb_uri = {out, cap, 0, PROTO_TRUE};
    Sb.put(&sb_uri, PROTOCORE_IFTTT_MAKER_ROOT);
    Sb.put(&sb_uri, event);
    Sb.put(&sb_uri, PROTOCORE_IFTTT_MAKER_KEY_SEG);
    Sb.put(&sb_uri, key);
    const int w = (int)Sb.finish(&sb_uri);
    // The builder is the overflow signal: it refuses the frame rather than truncating, so there is
    // no would-be length to compare against cap.
    if (!sb_uri.ok)
    {
        out[0] = '\0';
        return;
    }
    Webhook.n = w;
}

// Build the object {"value1":..,"value2":..,"value3":..} into the build region: begin-object,
// members separated by a value-separator, end-object (RFC 8259 sec 4). A NULL value omits its
// member, so three NULLs yield {}.
static void ifttt_payload(uint8_t *restrict work)
{
    (void)work;
    char *out = Webhook.build.out;
    const size_t cap = Webhook.build.cap;
    const char *const names[3] = {"value1", "value2", "value3"};
    const char *const vals[3] = {Webhook.ifttt.value1, Webhook.ifttt.value2, Webhook.ifttt.value3};

    Webhook.n = 0;
    if (!out || cap == 0)
    {
        return;
    }
    out[0] = '\0';
    size_t pos = 0;
    proto_bool ok = json_append(out, cap, &pos, "{");
    proto_bool first = PROTO_TRUE;
    for (int i = 0; i < 3 && ok; i++)
    {
        if (!vals[i])
        {
            continue;
        }
        // ok is always true on entry here: the loop condition (`&& ok`) was just re-checked to
        // reach this iteration, and nothing between loop entry and this statement can clear it.
        if (!first)
        {
            ok = ok && json_append(out, cap, &pos, ",");
        }
        first = PROTO_FALSE;
        ok = ok && json_append(out, cap, &pos, "\"");
        ok = ok && json_append(out, cap, &pos, names[i]);
        ok = ok && json_append(out, cap, &pos, "\":\"");
        ok = ok && json_append_escaped(out, cap, &pos, vals[i]);
        ok = ok && json_append(out, cap, &pos, "\"");
    }
    ok = ok && json_append(out, cap, &pos, "}");
    if (!ok)
    {
        out[0] = '\0';
        return;
    }
    Webhook.n = (int)pos;
}

#if PROTOCORE_ENABLE_HTTP_CLIENT

// POST the content to the target URI (RFC 9110 sec 9.3.3), typed application/json
// (RFC 9110 sec 8.3), its length measured for the Content-Length the client sends
// (RFC 9110 sec 8.6). Reports the status code (RFC 9110 sec 15.1) or a negative transport error.
static void post(uint8_t *restrict work)
{
    (void)work;
    const char *target_uri = Webhook.request.target_uri;
    const char *content = Webhook.request.content;
    if (!target_uri || !content)
    {
        Webhook.i32 = (int)HTTP_CLIENT_ERR_URL;
        return;
    }
    HttpClientResult r;
    Webhook.i32 = http_post(target_uri, PROTOCORE_MIME_JSON, (const uint8_t *)content,
                            str.len(content, PROTOCORE_HTTP_CLIENT_BUF_SIZE), &r);
}

#else // no outbound HTTP client in this build

// Nothing can be sent, so every POST reports -1 and no request is formed.
static void post(uint8_t *restrict work)
{
    (void)work;
    Webhook.i32 = -1;
}

#endif // PROTOCORE_ENABLE_HTTP_CLIENT

// Build the target URI and the object into this call's own frames, then POST them. A build that
// does not fit reports -1 and sends nothing. The frames die at return, so the handle stops naming
// them before the call ends.
static void ifttt_trigger(uint8_t *restrict work)
{
    char uri[PROTOCORE_WEBHOOK_URI_CAP];
    char content[PROTOCORE_WEBHOOK_CONTENT_CAP];

    Webhook.build.out = uri;
    Webhook.build.cap = sizeof(uri);
    ifttt_url(work);
    if (Webhook.n == 0)
    {
        Webhook.build.out = NULL;
        Webhook.i32 = -1;
        return;
    }
    Webhook.build.out = content;
    Webhook.build.cap = sizeof(content);
    ifttt_payload(work);
    if (Webhook.n == 0)
    {
        Webhook.build.out = NULL;
        Webhook.i32 = -1;
        return;
    }
    Webhook.request.target_uri = uri;
    Webhook.request.content = content;
    post(work);
    Webhook.request.target_uri = NULL;
    Webhook.request.content = NULL;
    Webhook.build.out = NULL;
}

// Designated, so a member's position in the struct does not decide what it binds to.
WebhookNs Webhook = {
    .ifttt_url = ifttt_url, .ifttt_payload = ifttt_payload, .post = post, .ifttt_trigger = ifttt_trigger};

#endif // PROTOCORE_ENABLE_WEBHOOK
