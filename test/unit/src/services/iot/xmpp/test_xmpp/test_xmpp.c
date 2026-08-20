// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the XMPP stanza codec (services/iot/xmpp/xmpp.h).
//
// The load-bearing case is test_predefined_entities: XML 1.0 sec 4.6 publishes exactly five
// predefined entities and their expansions, and RFC 6120 sec 11.1 makes those five the only entity
// references an XMPP stream may carry. An unescaped '<' inside a body ends the stanza early on the
// peer, so reproducing all five is what makes this codec safe to feed user text to. The stream
// header case pins the three fixed values RFC 6120 requires on it; the stanza cases pin the element
// names of sec 8.2 and the common attribute names of sec 8.1.

#include "services/iot/xmpp/xmpp.h"
#include <string.h>

#include <unity.h>

static uint8_t xmpp_work[16]; // the borrow an entry takes; Xmpp never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static char g_buf[512];

// Clear every member a build reads, so each case only carries what it sets.
static void reset(void)
{
    XmppV.out.buf = g_buf;
    XmppV.out.cap = sizeof(g_buf);
    XmppV.text.in = NULL;
    XmppV.text.len = 0;
    XmppV.stream.from = NULL;
    XmppV.stream.to = NULL;
    XmppV.common.to = NULL;
    XmppV.common.from = NULL;
    XmppV.common.type = NULL;
    XmppV.common.id = NULL;
    XmppV.child.body = NULL;
    XmppV.child.extension = NULL;
}

static void read_stanza(const char *xml, const char *attr)
{
    XmppV.out.buf = g_buf;
    XmppV.out.cap = sizeof(g_buf);
    XmppV.stanza.xml = xml;
    XmppV.stanza.len = xml ? strlen(xml) : 0;
    XmppV.stanza.attr = attr;
}

// XML 1.0 (Fifth Edition) sec 4.6 defines five predefined entities: amp, lt, gt, apos and quot,
// standing for '&', '<', '>', '\'' and '"'. RFC 6120 sec 11.1 bars every other entity reference
// from an XMPP stream, so these five are the whole escape.
void test_predefined_entities(void)
{
    static const char IN[] = "a<b>c&d'e\"f";
    reset();
    XmppV.text.in = IN;
    XmppV.text.len = sizeof(IN) - 1;
    Xmpp.escape(xmpp_work);

    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING("a&lt;b&gt;c&amp;d&apos;e&quot;f", g_buf);
    TEST_ASSERT_EQUAL_UINT(strlen(g_buf), XmppV.n);
}

// Text carrying none of the five passes through unchanged, octet for octet.
void test_escape_leaves_ordinary_text_alone(void)
{
    static const char IN[] = "Wherefore art thou, Romeo?";
    reset();
    XmppV.text.in = IN;
    XmppV.text.len = sizeof(IN) - 1;
    Xmpp.escape(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING(IN, g_buf);

    // An empty run is a valid escape of nothing.
    reset();
    XmppV.text.in = IN;
    XmppV.text.len = 0;
    Xmpp.escape(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, XmppV.n);
    TEST_ASSERT_EQUAL_STRING("", g_buf);
}

// RFC 6120 sec 4.2 opens a stream with an initial stream header. Whatever the attribute order, the
// header must carry the content namespace 'jabber:client' (sec 4.8.3), the stream namespace
// 'http://etherx.jabber.org/streams' (sec 4.8.2) and version '1.0' (sec 4.7.5), with the 'from'
// (sec 4.7.1) and 'to' (sec 4.7.2) the caller gave it.
void test_stream_header(void)
{
    reset();
    XmppV.stream.from = "juliet@im.example.com";
    XmppV.stream.to = "im.example.com";
    Xmpp.stream_open(xmpp_work);

    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING("<?xml version='1.0'?><stream:stream"
                             " from=\"juliet@im.example.com\""
                             " to=\"im.example.com\""
                             " xmlns='jabber:client'"
                             " xmlns:stream='http://etherx.jabber.org/streams'"
                             " version='1.0'>",
                             g_buf);

    // An address the caller left unset leaves its attribute out entirely.
    reset();
    XmppV.stream.to = "im.example.com";
    Xmpp.stream_open(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_NULL(strstr(g_buf, "from="));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, " to=\"im.example.com\""));
}

// RFC 6120 sec 8.2.1 `<message/>` with the sec 8.1 common attributes, carrying the `<body/>` of
// RFC 6121 sec 5.2.3. 'chat' is one of the message types RFC 6121 sec 5.2.2 defines.
void test_message_stanza(void)
{
    reset();
    XmppV.common.to = "romeo@example.net";
    XmppV.common.from = "juliet@im.example.com/balcony";
    XmppV.common.type = "chat";
    XmppV.child.body = "Wherefore art thou, Romeo?";
    Xmpp.message(xmpp_work);

    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING("<message to=\"romeo@example.net\""
                             " from=\"juliet@im.example.com/balcony\""
                             " type=\"chat\">"
                             "<body>Wherefore art thou, Romeo?</body>"
                             "</message>",
                             g_buf);

    // No body: the element still closes, with nothing between the tags.
    reset();
    XmppV.common.to = "romeo@example.net";
    Xmpp.message(xmpp_work);
    TEST_ASSERT_EQUAL_STRING("<message to=\"romeo@example.net\"></message>", g_buf);
}

// The body is character data, so the five entities apply to it: a '<' in a body must not be able to
// close the stanza on the peer.
void test_message_body_is_escaped(void)
{
    reset();
    XmppV.common.to = "romeo@example.net";
    XmppV.child.body = "5 < 6 & \"quoted\"";
    Xmpp.message(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING("<message to=\"romeo@example.net\">"
                             "<body>5 &lt; 6 &amp; &quot;quoted&quot;</body>"
                             "</message>",
                             g_buf);
}

// An attribute value is escaped the same way (XML 1.0 production [10] AttValue), so a quote in a
// value cannot close the attribute early.
void test_attribute_values_are_escaped(void)
{
    reset();
    XmppV.common.to = "a&b";
    XmppV.common.type = "a\"b";
    Xmpp.message(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING("<message to=\"a&amp;b\" type=\"a&quot;b\"></message>", g_buf);
}

// RFC 6120 sec 8.2.2 `<presence/>`, written as the empty-element tag of XML 1.0 production [44].
// RFC 6121 sec 4.7.1: the absence of 'type' signals available.
void test_presence_stanza(void)
{
    reset();
    Xmpp.presence(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING("<presence/>", g_buf);

    reset();
    XmppV.common.type = "unavailable";
    Xmpp.presence(xmpp_work);
    TEST_ASSERT_EQUAL_STRING("<presence type=\"unavailable\"/>", g_buf);
}

// RFC 6120 sec 8.2.3 `<iq/>`: 'id' and 'type' are REQUIRED, and sec 8.4 puts one extension element
// inside a get or a set. The extension is already XML and goes in verbatim, not escaped.
void test_iq_stanza(void)
{
    reset();
    XmppV.common.type = "get";
    XmppV.common.id = "info1";
    XmppV.child.extension = "<query xmlns='http://jabber.org/protocol/disco#info'/>";
    Xmpp.iq(xmpp_work);

    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING("<iq type=\"get\" id=\"info1\">"
                             "<query xmlns='http://jabber.org/protocol/disco#info'/>"
                             "</iq>",
                             g_buf);

    // A result with no payload is the same element with nothing inside it.
    reset();
    XmppV.common.type = "result";
    XmppV.common.id = "info1";
    Xmpp.iq(xmpp_work);
    TEST_ASSERT_EQUAL_STRING("<iq type=\"result\" id=\"info1\"></iq>", g_buf);
}

// XML 1.0 sec 3.1 production [40]: the Name follows '<' and ends at the first S, '/' or '>'. That
// Name is the stanza's kind.
void test_stanza_name(void)
{
    read_stanza("<message to='romeo@example.net' type='chat'><body>hi</body></message>", NULL);
    Xmpp.stanza_name(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING("message", g_buf);

    read_stanza("<presence/>", NULL);
    Xmpp.stanza_name(xmpp_work);
    TEST_ASSERT_EQUAL_STRING("presence", g_buf);

    read_stanza("<iq type='result' id='1'/>", NULL);
    Xmpp.stanza_name(xmpp_work);
    TEST_ASSERT_EQUAL_STRING("iq", g_buf);
}

// '<?' opens a processing instruction (XML 1.0 sec 2.6), '<!' a comment (sec 2.5), '</' an end-tag
// (sec 3.1). None of them is a start-tag, so the read walks past them to the first one that is.
void test_stanza_name_skips_non_start_tags(void)
{
    read_stanza("<?xml version='1.0'?><stream:stream xmlns='jabber:client'>", NULL);
    Xmpp.stanza_name(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING("stream:stream", g_buf);

    read_stanza("<!-- a comment --><iq type='get' id='1'/>", NULL);
    Xmpp.stanza_name(xmpp_work);
    TEST_ASSERT_EQUAL_STRING("iq", g_buf);

    // Nothing but an end-tag: there is no start-tag to name.
    read_stanza("</message>", NULL);
    Xmpp.stanza_name(xmpp_work);
    TEST_ASSERT_FALSE(XmppV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, XmppV.n);
}

// XML 1.0 production [41] Attribute: `Name Eq AttValue`, and production [10] delimits the value
// with a pair of '"' or a pair of '\''. Both spellings read back the same value.
void test_attribute_read(void)
{
    read_stanza("<message to='romeo@example.net' type='chat'>", "type");
    Xmpp.attr(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING("chat", g_buf);

    read_stanza("<message to=\"romeo@example.net\" type=\"chat\">", "to");
    Xmpp.attr(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING("romeo@example.net", g_buf);

    // An empty value is a value.
    read_stanza("<message id=''>", "id");
    Xmpp.attr(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, XmppV.n);
}

// Production [40] puts S in front of every attribute specification, so a name that is only the tail
// of a longer one is not that attribute.
void test_attribute_name_must_start_an_attribute(void)
{
    read_stanza("<message xto='wrong' to='right'>", "to");
    Xmpp.attr(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING("right", g_buf);
}

// The value is the raw octets between the delimiters: an entity reference in it is left for the
// caller to expand, not silently turned back into its character.
void test_attribute_value_is_raw(void)
{
    read_stanza("<message to='a&amp;b'>", "to");
    Xmpp.attr(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);
    TEST_ASSERT_EQUAL_STRING("a&amp;b", g_buf);
}

// An attribute specification belongs to a start-tag, which ends at its '>'. One inside a child
// element is not an attribute of the stanza.
void test_attribute_read_stops_at_the_start_tag(void)
{
    read_stanza("<message id='1'><body to='not-mine'>hi</body></message>", "to");
    Xmpp.attr(xmpp_work);
    TEST_ASSERT_FALSE(XmppV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, XmppV.n);
}

// A name that is not in the start-tag, a value with no delimiter, and a missing argument are each
// reported rather than guessed at.
void test_attribute_read_refusals(void)
{
    read_stanza("<message to='a'>", "type");
    Xmpp.attr(xmpp_work);
    TEST_ASSERT_FALSE(XmppV.ok);

    read_stanza("<message to=a>", "to");
    Xmpp.attr(xmpp_work);
    TEST_ASSERT_FALSE(XmppV.ok);

    read_stanza(NULL, "to");
    Xmpp.attr(xmpp_work);
    TEST_ASSERT_FALSE(XmppV.ok);

    read_stanza("<message to='a'>", NULL);
    Xmpp.attr(xmpp_work);
    TEST_ASSERT_FALSE(XmppV.ok);
}

// A buffer that cannot hold the whole stanza reports nothing written: half a start-tag is not XML,
// and the peer would close the stream on it.
void test_build_refuses_a_short_buffer(void)
{
    char small[8];
    reset();
    XmppV.out.buf = small;
    XmppV.out.cap = sizeof(small);
    XmppV.common.to = "romeo@example.net";
    Xmpp.message(xmpp_work);
    TEST_ASSERT_FALSE(XmppV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, XmppV.n);

    reset();
    XmppV.out.buf = NULL;
    XmppV.out.cap = 0;
    Xmpp.presence(xmpp_work);
    TEST_ASSERT_FALSE(XmppV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, XmppV.n);
}

// An escape with no source is reported, not written through.
void test_escape_refuses_a_null_source(void)
{
    reset();
    XmppV.text.in = NULL;
    XmppV.text.len = 4;
    Xmpp.escape(xmpp_work);
    TEST_ASSERT_FALSE(XmppV.ok);
    TEST_ASSERT_EQUAL_UINT(0u, XmppV.n);
}

// A built stanza reads back through the two readers: its element name and each attribute it carries.
void test_build_then_read_round_trip(void)
{
    reset();
    XmppV.common.to = "romeo@example.net";
    XmppV.common.from = "juliet@im.example.com/balcony";
    XmppV.common.type = "chat";
    Xmpp.message(xmpp_work);
    TEST_ASSERT_TRUE(XmppV.ok);

    static char stanza[256];
    memcpy(stanza, g_buf, XmppV.n + 1);

    read_stanza(stanza, NULL);
    Xmpp.stanza_name(xmpp_work);
    TEST_ASSERT_EQUAL_STRING("message", g_buf);

    read_stanza(stanza, "to");
    Xmpp.attr(xmpp_work);
    TEST_ASSERT_EQUAL_STRING("romeo@example.net", g_buf);

    read_stanza(stanza, "from");
    Xmpp.attr(xmpp_work);
    TEST_ASSERT_EQUAL_STRING("juliet@im.example.com/balcony", g_buf);

    read_stanza(stanza, "type");
    Xmpp.attr(xmpp_work);
    TEST_ASSERT_EQUAL_STRING("chat", g_buf);
}
