// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file xmpp.cpp
 * @brief XMPP stanza codec (see xmpp.h).
 */

#include "services/iot/xmpp/xmpp.h"

#if PC_ENABLE_XMPP

#include <string.h>

namespace
{
// Append a literal to out[*n], bounded. Returns false on overflow.
bool put(char *out, size_t cap, size_t *n, const char *s)
{
    size_t sl = strnlen(s, cap + 1);
    if (*n + sl >= cap)
    {
        return false;
    }
    memcpy(out + *n, s, sl);
    *n += sl;
    return true;
}

// Append s, XML-escaped, into out[*n]. Returns false on overflow.
bool put_escaped(char *out, size_t cap, size_t *n, const char *s)
{
    for (; *s; s++)
    {
        const char *rep = nullptr;
        switch (*s)
        {
        case '&':
            rep = "&amp;";
            break;
        case '<':
            rep = "&lt;";
            break;
        case '>':
            rep = "&gt;";
            break;
        case '\'':
            rep = "&apos;";
            break;
        case '"':
            rep = "&quot;";
            break;
        default:
            break;
        }
        if (rep)
        {
            if (!put(out, cap, n, rep))
            {
                return false;
            }
        }
        else
        {
            if (*n + 1 >= cap)
            {
                return false;
            }
            out[(*n)++] = *s;
        }
    }
    return true;
}

// Append `attr="value"` (value escaped) when value is non-null.
bool put_attr(char *out, size_t cap, size_t *n, const char *attr, const char *value)
{
    if (!value)
    {
        return true;
    }
    return put(out, cap, n, " ") && put(out, cap, n, attr) && put(out, cap, n, "=\"") &&
           put_escaped(out, cap, n, value) && put(out, cap, n, "\"");
}

size_t finish(char *out, size_t n, bool ok)
{
    if (!ok)
    {
        return 0;
    }
    out[n] = '\0';
    return n;
}
} // namespace

size_t pc_xmpp_escape(const char *in, size_t in_len, char *out, size_t cap)
{
    if (!in || !out)
    {
        return 0;
    }
    size_t n = 0;
    for (size_t i = 0; i < in_len; i++)
    {
        char one[2] = {in[i], '\0'};
        if (!put_escaped(out, cap, &n, one))
        {
            return 0;
        }
    }
    return finish(out, n, true);
}

size_t pc_xmpp_stream_open(const char *from, const char *to, char *out, size_t cap)
{
    size_t n = 0;
    bool ok =
        put(out, cap, &n, "<?xml version='1.0'?><stream:stream") && put_attr(out, cap, &n, "from", from) &&
        put_attr(out, cap, &n, "to", to) &&
        put(out, cap, &n, " xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams' version='1.0'>");
    return finish(out, n, ok);
}

size_t pc_xmpp_message(const char *to, const char *from, const char *type, const char *body, char *out, size_t cap)
{
    size_t n = 0;
    bool ok = put(out, cap, &n, "<message") && put_attr(out, cap, &n, "to", to) &&
              put_attr(out, cap, &n, "from", from) && put_attr(out, cap, &n, "type", type) && put(out, cap, &n, ">");
    if (ok && body)
    {
        ok = put(out, cap, &n, "<body>") && put_escaped(out, cap, &n, body) && put(out, cap, &n, "</body>");
    }
    ok = ok && put(out, cap, &n, "</message>");
    return finish(out, n, ok);
}

size_t pc_xmpp_presence(const char *type, char *out, size_t cap)
{
    size_t n = 0;
    bool ok = put(out, cap, &n, "<presence") && put_attr(out, cap, &n, "type", type) && put(out, cap, &n, "/>");
    return finish(out, n, ok);
}

size_t pc_xmpp_iq(const char *type, const char *id, const char *child_xml, char *out, size_t cap)
{
    size_t n = 0;
    bool ok = put(out, cap, &n, "<iq") && put_attr(out, cap, &n, "type", type) && put_attr(out, cap, &n, "id", id) &&
              put(out, cap, &n, ">");
    if (ok && child_xml)
    {
        ok = put(out, cap, &n, child_xml);
    }
    ok = ok && put(out, cap, &n, "</iq>");
    return finish(out, n, ok);
}

size_t pc_xmpp_stanza_name(const char *xml, size_t len, char *out, size_t cap)
{
    if (!xml || !out || cap == 0)
    {
        return 0;
    }
    for (size_t i = 0; i + 1 < len; i++)
    {
        if (xml[i] != '<')
        {
            continue;
        }
        char c = xml[i + 1];
        if (c == '?' || c == '!' || c == '/') // skip declaration / comment / close tag
        {
            continue;
        }
        size_t j = i + 1, k = 0;
        while (j < len && xml[j] != ' ' && xml[j] != '>' && xml[j] != '/' && xml[j] != '\t' && xml[j] != '\n')
        {
            if (k + 1 >= cap)
            {
                return 0;
            }
            out[k++] = xml[j++];
        }
        out[k] = '\0';
        return k;
    }
    return 0;
}

size_t pc_xmpp_attr(const char *xml, size_t len, const char *attr, char *out, size_t cap)
{
    if (!xml || !attr || !out || cap == 0)
    {
        return 0;
    }
    size_t al = strnlen(attr, len + 1);
    // Search only within the first start tag (up to the first '>').
    size_t end = 0;
    while (end < len && xml[end] != '>')
    {
        end++;
    }
    for (size_t i = 0; i + al + 2 < end; i++)
    {
        // match  <space>attr=  (leading space avoids a substring hit inside another attr name)
        if (!((i == 0 || xml[i - 1] == ' ') && strncmp(xml + i, attr, al) == 0 && xml[i + al] == '='))
        {
            continue;
        }
        char q = xml[i + al + 1];
        if (q != '"' && q != '\'')
        {
            return 0;
        }
        size_t j = i + al + 2, k = 0;
        while (j < end && xml[j] != q)
        {
            if (k + 1 >= cap)
            {
                return 0;
            }
            out[k++] = xml[j++];
        }
        out[k] = '\0';
        return k;
    }
    return 0;
}

#endif // PC_ENABLE_XMPP
