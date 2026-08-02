// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file webhook.c
 * @brief IFTTT URL / payload builders (pure) + fire over the http_client.
 */

#include "services/net/webhook/webhook.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_WEBHOOK

#include "shared_primitives/mime.h"

#include <stdio.h>
#include <string.h>

#if PC_ENABLE_HTTP_CLIENT
#include "services/net/http_client/http_client.h"
#endif
static proto_bool put(char *out, size_t cap, size_t *pos, const char *s)
{
    size_t n = strnlen(s, cap + 1);
    if (*pos + n >= cap)
    {
        return PROTO_FALSE;
    }
    memcpy(out + *pos, s, n);
    *pos += n;
    out[*pos] = '\0';
    return PROTO_TRUE;
}

// Append a JSON string value, escaping '"' and '\'.
static proto_bool put_escaped(char *out, size_t cap, size_t *pos, const char *s)
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

int pc_ifttt_url(const char *event, const char *key, char *out, size_t cap)
{
    if (!out || cap == 0 || !event || !key)
    {
        if (out && cap)
        {
            out[0] = '\0';
        }
        return 0;
    }
    pc_sb sb_out = {out, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb_out, "https://maker.ifttt.com/trigger/");
    pc_sb_put(&sb_out, event);
    pc_sb_put(&sb_out, "/with/key/");
    pc_sb_put(&sb_out, key);
    int w = (int)pc_sb_finish(&sb_out);
    // The builder is the overflow signal: it refuses the frame rather than truncating, so there is
    // no would-be length to compare against cap.
    if (!sb_out.ok)
    {
        out[0] = '\0';
        return 0;
    }
    return w;
}

int pc_ifttt_payload(const char *v1, const char *v2, const char *v3, char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    out[0] = '\0';
    size_t pos = 0;
    proto_bool ok = put(out, cap, &pos, "{");
    const char *names[3] = {"value1", "value2", "value3"};
    const char *vals[3] = {v1, v2, v3};
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
            ok = ok && put(out, cap, &pos, ","); // GCOVR_EXCL_BR_LINE  ok-false half is unreachable (see above)
        }
        first = PROTO_FALSE;
        ok = ok && put(out, cap, &pos, "\"");
        ok = ok && put(out, cap, &pos, names[i]);
        ok = ok && put(out, cap, &pos, "\":\"");
        ok = ok && put_escaped(out, cap, &pos, vals[i]);
        ok = ok && put(out, cap, &pos, "\"");
    }
    ok = ok && put(out, cap, &pos, "}");
    if (!ok)
    {
        out[0] = '\0';
        return 0;
    }
    return (int)pos;
}

#if PC_ENABLE_HTTP_CLIENT

int pc_webhook_post(const char *url, const char *json)
{
    if (!url || !json)
    {
        return (int)HTTP_CLIENT_ERR_URL;
    }
    HttpClientResult r;
    return http_post(url, PC_MIME_JSON, (const uint8_t *)json, strnlen(json, PC_HTTP_CLIENT_BUF_SIZE), &r);
}

#else // http_client not enabled in this build

int pc_webhook_post(const char *, const char *)
{
    return -1;
}

#endif // PC_ENABLE_HTTP_CLIENT

int pc_ifttt_trigger(const char *event, const char *key, const char *v1, const char *v2, const char *v3)
{
    char url[160];
    char body[256];
    if (pc_ifttt_url(event, key, url, sizeof(url)) == 0)
    {
        return -1;
    }
    if (pc_ifttt_payload(v1, v2, v3, body, sizeof(body)) == 0)
    {
        return -1;
    }
    return pc_webhook_post(url, body);
}

#endif // PC_ENABLE_WEBHOOK
