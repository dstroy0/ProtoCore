// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file xmpp.c
 * @brief The XMPP stanza codec: the RFC 6120 sec 8 builders and the XML 1.0 sec 3.1 start-tag reads.
 *
 * Every builder starts a run, appends its literals and its escaped members through one bounded
 * cursor, and terminates it. The cursor is ns->n and the run's verdict is ns->ok, latched false the
 * first time an append will not fit, so an over-long stanza reports 0 octets instead of a truncated
 * one.
 */

#include "services/iot/xmpp/xmpp.h"

#if PROTOCORE_ENABLE_XMPP

#include "mmgr/protomem/protomem.h" // mem.cpy: a literal and an entity move whole
#include "mmgr/protostr/protostr.h" // str.len / str.starts: the bounded length and the attribute-name match

// RFC 6120 sec 8.1: the common attribute names, and RFC 6120 sec 4.7 the stream header's two.
#define PROTOCORE_XMPP_ATTR_TO "to"
#define PROTOCORE_XMPP_ATTR_FROM "from"
#define PROTOCORE_XMPP_ATTR_TYPE "type"
#define PROTOCORE_XMPP_ATTR_ID "id"

// XML 1.0 sec 3.1: what closes a start-tag (production [40]) and an empty-element tag ([44]).
#define PROTOCORE_XMPP_TAG_END ">"
#define PROTOCORE_XMPP_EMPTY_END "/>"

// XML 1.0 sec 4.6: how many predefined entities there are.
#define PROTOCORE_XMPP_ENTITY_COUNT 5

/** @brief One predefined entity of XML 1.0 sec 4.6. */
typedef struct
{
    char ch;            ///< the character the entity stands for
    const char *entity; ///< the entity reference written in its place
} XmppEntity;

// XML 1.0 sec 4.6: amp, lt, gt, apos and quot, the only entity references RFC 6120 sec 11.1 leaves
// an XMPP stream.
static const XmppEntity s_entities[PROTOCORE_XMPP_ENTITY_COUNT] = {
    {'&', "&amp;"}, {'<', "&lt;"}, {'>', "&gt;"}, {'\'', "&apos;"}, {'"', "&quot;"},
};

// XML 1.0 sec 2.3 production [3]: S is one or more of space, tab, carriage return and line feed.
static proto_bool xml_space(char c)
{
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
    {
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// The entity reference XML 1.0 sec 4.6 gives for c, or NULL when c stands as itself.
static const char *entity_of(char c)
{
    for (size_t i = 0; i < PROTOCORE_XMPP_ENTITY_COUNT; i++)
    {
        if (s_entities[i].ch == c)
        {
            return s_entities[i].entity;
        }
    }
    return NULL;
}

// Open a run at the head of the caller's buffer. A missing buffer or no room fails it here.
static void start(uint8_t *restrict work)
{
    (void)work;
    Xmpp.n = 0;
    Xmpp.ok = PROTO_TRUE;
    if (Xmpp.out.buf == NULL || Xmpp.out.cap == 0)
    {
        Xmpp.ok = PROTO_FALSE;
    }
}

// Terminate the run, or report nothing written.
static void finish(uint8_t *restrict work)
{
    (void)work;
    if (!Xmpp.ok)
    {
        Xmpp.n = 0;
        return;
    }
    Xmpp.out.buf[Xmpp.n] = '\0';
}

// Append one octet, keeping room for the terminator.
static void put_char(uint8_t *restrict work, char c)
{
    if (!Xmpp.ok)
    {
        return;
    }
    if (Xmpp.n + 1 >= Xmpp.out.cap)
    {
        Xmpp.ok = PROTO_FALSE;
        return;
    }
    Xmpp.out.buf[Xmpp.n] = c;
    Xmpp.n++;
}

// Append a NUL-terminated run, keeping room for the terminator.
static void put(uint8_t *restrict work, const char *s)
{
    if (!Xmpp.ok)
    {
        return;
    }
    const size_t cap = Xmpp.out.cap;
    const size_t sl = str.len(s, cap);
    if (Xmpp.n + sl >= cap)
    {
        Xmpp.ok = PROTO_FALSE;
        return;
    }
    mem.cpy(Xmpp.out.buf + Xmpp.n, s, sl);
    Xmpp.n += sl;
}

// Append len octets of s, each character carrying an entity written as that entity instead.
static void put_escaped(uint8_t *restrict work, const char *s, size_t len)
{
    if (!Xmpp.ok || s == NULL)
    {
        return;
    }
    for (size_t i = 0; i < len; i++)
    {
        const char *rep = entity_of(s[i]);
        if (rep == NULL)
        {
            put_char(work, s[i]);
        }
        else
        {
            put(work, rep);
        }
    }
}

// Append one attribute specification, `S Name Eq AttValue` (XML 1.0 sec 3.1 production [41]), with
// the value escaped and double quotes as its delimiter. A NULL value leaves the attribute out.
static void put_attr(uint8_t *restrict work, const char *name, const char *value)
{
    if (value == NULL)
    {
        return;
    }
    put_char(work, ' ');
    put(work, name);
    put_char(work, '=');
    put_char(work, '"');
    put_escaped(work, value, str.len(value, Xmpp.out.cap));
    put_char(work, '"');
}

// Write text.in into out with the XML 1.0 sec 4.6 entities substituted.
static void xmpp_escape(uint8_t *restrict work)
{
    start(work);
    if (Xmpp.text.in == NULL)
    {
        Xmpp.ok = PROTO_FALSE;
    }
    put_escaped(work, Xmpp.text.in, Xmpp.text.len);
    finish(work);
}

// Build the initial stream header (RFC 6120 sec 4.2), preceded by the XML declaration RFC 6120
// sec 11.5 asks for, with 'jabber:client' as the content namespace (sec 4.8.3) and version '1.0'
// (sec 4.7.5).
static void xmpp_stream_open(uint8_t *restrict work)
{
    start(work);
    put(work, "<?xml version='1.0'?><stream:stream");
    put_attr(work, PROTOCORE_XMPP_ATTR_FROM, Xmpp.stream.from);
    put_attr(work, PROTOCORE_XMPP_ATTR_TO, Xmpp.stream.to);
    put(work, " xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams' version='1.0'>");
    finish(work);
}

// Build a `<message/>` (RFC 6120 sec 8.2.1) carrying the `<body/>` of RFC 6121 sec 5.2.3.
static void xmpp_message(uint8_t *restrict work)
{
    start(work);
    put(work, "<message");
    put_attr(work, PROTOCORE_XMPP_ATTR_TO, Xmpp.common.to);
    put_attr(work, PROTOCORE_XMPP_ATTR_FROM, Xmpp.common.from);
    put_attr(work, PROTOCORE_XMPP_ATTR_TYPE, Xmpp.common.type);
    put(work, PROTOCORE_XMPP_TAG_END);
    if (Xmpp.child.body != NULL)
    {
        put(work, "<body>");
        put_escaped(work, Xmpp.child.body, str.len(Xmpp.child.body, Xmpp.out.cap));
        put(work, "</body>");
    }
    put(work, "</message>");
    finish(work);
}

// Build a `<presence/>` (RFC 6120 sec 8.2.2) as an empty-element tag. A NULL type signals available
// (RFC 6121 sec 4.7.1).
static void xmpp_presence(uint8_t *restrict work)
{
    start(work);
    put(work, "<presence");
    put_attr(work, PROTOCORE_XMPP_ATTR_TYPE, Xmpp.common.type);
    put(work, PROTOCORE_XMPP_EMPTY_END);
    finish(work);
}

// Build an `<iq/>` (RFC 6120 sec 8.2.3) around the extension element of sec 8.4, which is already
// XML and goes in as it stands.
static void xmpp_iq(uint8_t *restrict work)
{
    start(work);
    put(work, "<iq");
    put_attr(work, PROTOCORE_XMPP_ATTR_TYPE, Xmpp.common.type);
    put_attr(work, PROTOCORE_XMPP_ATTR_ID, Xmpp.common.id);
    put(work, PROTOCORE_XMPP_TAG_END);
    if (Xmpp.child.extension != NULL)
    {
        put(work, Xmpp.child.extension);
    }
    put(work, "</iq>");
    finish(work);
}

// Read the Name of the first start-tag in stanza.xml, the element's type (XML 1.0 sec 3.1).
static void xmpp_stanza_name(uint8_t *restrict work)
{
    start(work);
    const char *xml = Xmpp.stanza.xml;
    const size_t len = Xmpp.stanza.len;
    if (xml == NULL)
    {
        Xmpp.ok = PROTO_FALSE;
    }
    if (!Xmpp.ok)
    {
        finish(work);
        return;
    }

    // A '<' opens a start-tag only when what follows is a Name: '<?' opens a processing instruction
    // (sec 2.6), '<!' a comment or a declaration (sec 2.5, sec 2.8), and '</' an end-tag (sec 3.1).
    size_t i = 0;
    proto_bool found = PROTO_FALSE;
    while (i + 1 < len && !found)
    {
        if (xml[i] == '<' && xml[i + 1] != '?' && xml[i + 1] != '!' && xml[i + 1] != '/')
        {
            found = PROTO_TRUE;
        }
        else
        {
            i++;
        }
    }
    if (!found)
    {
        Xmpp.ok = PROTO_FALSE;
        finish(work);
        return;
    }

    // Production [40] and [44]: the Name runs from just past '<' to the S, the '/' or the '>' that
    // ends it.
    size_t j = i + 1;
    while (j < len && !xml_space(xml[j]) && xml[j] != '>' && xml[j] != '/')
    {
        put_char(work, xml[j]);
        j++;
    }
    finish(work);
}

// Read the attribute value stanza.attr names out of the start-tag of stanza.xml, as the raw octets
// between the delimiters (XML 1.0 sec 3.1).
static void xmpp_attr(uint8_t *restrict work)
{
    start(work);
    const char *xml = Xmpp.stanza.xml;
    const size_t len = Xmpp.stanza.len;
    const char *name = Xmpp.stanza.attr;
    if (xml == NULL || name == NULL)
    {
        Xmpp.ok = PROTO_FALSE;
    }
    if (!Xmpp.ok)
    {
        finish(work);
        return;
    }
    const size_t nl = str.len(name, len);

    // An attribute specification belongs to the start-tag, which ends at its '>'.
    size_t end = 0;
    while (end < len && xml[end] != '>')
    {
        end++;
    }

    // Production [40] puts S in front of every attribute specification and production [41] puts Eq
    // straight after its Name, so a name that is only the tail of another one never matches.
    size_t i = 0;
    proto_bool found = PROTO_FALSE;
    while (i + nl + 2 < end && !found)
    {
        proto_bool sep = PROTO_FALSE;
        if (i == 0)
        {
            sep = PROTO_TRUE;
        }
        else if (xml_space(xml[i - 1]))
        {
            sep = PROTO_TRUE;
        }
        if (sep && str.starts(xml + i, name, nl + 1, PROTO_FALSE) && xml[i + nl] == '=')
        {
            found = PROTO_TRUE;
        }
        else
        {
            i++;
        }
    }
    if (!found)
    {
        Xmpp.ok = PROTO_FALSE;
        finish(work);
        return;
    }

    // Production [10] AttValue: the value is delimited by a pair of '"' or a pair of '\''.
    const char q = xml[i + nl + 1];
    if (q != '"' && q != '\'')
    {
        Xmpp.ok = PROTO_FALSE;
        finish(work);
        return;
    }
    size_t j = i + nl + 2;
    while (j < end && xml[j] != q)
    {
        put_char(work, xml[j]);
        j++;
    }
    finish(work);
}

// Designated, so a member's position in the struct does not decide what it binds to.
XmppNs Xmpp = {.escape = xmpp_escape,
               .stream_open = xmpp_stream_open,
               .message = xmpp_message,
               .presence = xmpp_presence,
               .iq = xmpp_iq,
               .stanza_name = xmpp_stanza_name,
               .attr = xmpp_attr};

#endif // PROTOCORE_ENABLE_XMPP
