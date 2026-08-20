// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file xmpp.h
 * @brief The XMPP stanza codec (RFC 6120 sec 8): four builders, an escape, and two start-tag reads.
 *
 * RFC 6120 sec 4.2 opens an XML stream by sending an initial stream header, and RFC 6120 sec 8
 * carries three stanza kinds over that stream: `<message/>` (sec 8.2.1), `<presence/>` (sec 8.2.2)
 * and `<iq/>` (sec 8.2.3). This module builds those four elements into a caller buffer and reads
 * two things back out of a received one: the start-tag Name, and one attribute value.
 *
 * RFC 6120 sec 8.1 names the common attributes a stanza carries: 'to' (sec 8.1.1), 'from'
 * (sec 8.1.2), 'id' (sec 8.1.3) and 'type' (sec 8.1.4). Each stanza kind takes its own 'type'
 * values: message takes chat, error, groupchat, headline or normal (RFC 6121 sec 5.2.2); presence
 * takes error, probe, subscribe, subscribed, unavailable, unsubscribe or unsubscribed, and its
 * absence signals available (RFC 6121 sec 4.7.1); IQ takes get, set, result or error (RFC 6120
 * sec 8.2.3).
 *
 * Character data and attribute values are written through the five predefined entities of XML 1.0
 * (W3C REC-xml Fifth Edition sec 4.6: amp, lt, gt, apos, quot). XML is a W3C Recommendation, not an
 * IETF RFC; RFC 6120 sec 11.1 is what makes those five the only entity references an XMPP stream
 * may carry.
 *
 * The builders emit the members they are given and check nothing else. RFC 6120 sec 8.2.3 makes
 * 'id' and 'type' REQUIRED on an IQ, and sec 8.4 requires one extension element on a get or a set;
 * a caller that leaves those members NULL gets a stanza without them.
 *
 * A 'to' or 'from' value is a JID (RFC 7622 sec 3.1, which obsoletes RFC 6122). The text is copied
 * through escaped, without the PRECIS preparation or the 1023-octet part limits of RFC 7622 sec 3.2
 * through sec 3.4.
 *
 * STARTTLS (RFC 6120 sec 5), SASL (RFC 6120 sec 6) and resource binding (RFC 6120 sec 7) are
 * negotiated elsewhere; this module frames text and holds no stream.
 *
 * The module exports one symbol, @ref Xmpp. Everything in xmpp.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_XMPP_H
#define PROTOCORE_XMPP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_XMPP

PROTOCORE_BEGIN_DECLS

/** @brief Where a call writes its octets. */
typedef struct
{
    char *buf;  ///< the buffer the call writes into
    size_t cap; ///< how much room it has, the NUL included
} XmppOutArgs;

/** @brief The character data an escape reads (XML 1.0 sec 2.4). */
typedef struct
{
    const char *in; ///< the octets to escape
    size_t len;     ///< how many of them
} XmppTextArgs;

/** @brief RFC 6120 sec 4.7: the addresses the initial stream header carries (sec 4.2). */
typedef struct
{
    const char *from; ///< 'from' (sec 4.7.1), the initiating entity's JID; NULL leaves it out
    const char *to;   ///< 'to' (sec 4.7.2), the domainpart the initiator expects the receiver to service
} XmppStreamArgs;

/** @brief RFC 6120 sec 8.1: the common attributes a stanza carries. A NULL member leaves one out. */
typedef struct
{
    const char *to;   ///< 'to' (sec 8.1.1), the intended recipient's JID
    const char *from; ///< 'from' (sec 8.1.2), the sending entity's JID
    const char *type; ///< 'type' (sec 8.1.4), one of the values its stanza kind defines
    const char *id;   ///< 'id' (sec 8.1.3), REQUIRED on an IQ; only the IQ builder emits it
} XmppCommonArgs;

/** @brief What a stanza carries below its start-tag. A NULL member leaves that child out. */
typedef struct
{
    const char *body;      ///< the `<body/>` character data of a message (RFC 6121 sec 5.2.3), escaped on the way out
    const char *extension; ///< the extension element of an IQ (RFC 6120 sec 8.4), copied through verbatim
} XmppChildArgs;

/** @brief The received stanza a read walks, and the attribute name it looks up. */
typedef struct
{
    const char *xml;  ///< the received octets
    size_t len;       ///< how many of them
    const char *attr; ///< the attribute name to find in the start-tag (XML 1.0 sec 3.1)
} XmppStanzaArgs;

/**
 * @brief The XMPP stanza codec.
 *
 * A caller sets the members a call takes, invokes it through ::Xmpp, and reads the outcome off the
 * same handle.
 *
 * No slot member: every call works on the caller's octets and holds no stream, so no call names a
 * row.
 *
 * @var XmppNs::out          where the call writes its octets
 * @var XmppNs::text         the character data an escape reads
 * @var XmppNs::stream       the addresses the initial stream header carries (RFC 6120 sec 4.7)
 * @var XmppNs::common       the common attributes a stanza carries (RFC 6120 sec 8.1)
 * @var XmppNs::child        the `<body/>` or the extension element a stanza carries
 * @var XmppNs::stanza       the received stanza a read walks, and the attribute it names
 * @var XmppNs::ok           a call's true/false outcome
 * @var XmppNs::n            the octets written into @c out, excluding the NUL; 0 when the call failed
 * @var XmppNs::escape       write @c text through the five predefined entities (XML 1.0 sec 4.6)
 * @var XmppNs::stream_open  build the initial stream header from @c stream (RFC 6120 sec 4.2)
 * @var XmppNs::message      build a `<message/>` from @c common to, from and type, and @c child body
 *                           (RFC 6120 sec 8.2.1, RFC 6121 sec 5.2)
 * @var XmppNs::presence     build a `<presence/>` from @c common type (RFC 6120 sec 8.2.2, RFC 6121
 *                           sec 4.7)
 * @var XmppNs::iq           build an `<iq/>` from @c common type and id, and @c child extension
 *                           (RFC 6120 sec 8.2.3, sec 8.4)
 * @var XmppNs::stanza_name  read the start-tag Name of @c stanza, the element's type (XML 1.0 sec 3.1)
 * @var XmppNs::attr         read the attribute value @c stanza names out of its start-tag, as the raw
 *                           octets between the quotes with no entity expanded (XML 1.0 sec 3.1)
 */
typedef struct
{
    XmppOutArgs out;       ///< where the octets land
    XmppTextArgs text;     ///< what an escape reads
    XmppStreamArgs stream; ///< what the initial stream header addresses
    XmppCommonArgs common; ///< what a stanza's start-tag says
    XmppChildArgs child;   ///< what a stanza carries below it
    XmppStanzaArgs stanza; ///< what a read walks
    proto_bool ok;
    size_t n;
} XmppVars;

/** @brief The operands and the outcome. */
extern XmppVars XmppV;

/** @brief The entries. */
typedef struct
{
    void (*const escape)(uint8_t *restrict work);
    void (*const stream_open)(uint8_t *restrict work);
    void (*const message)(uint8_t *restrict work);
    void (*const presence)(uint8_t *restrict work);
    void (*const iq)(uint8_t *restrict work);
    void (*const stanza_name)(uint8_t *restrict work);
    void (*const attr)(uint8_t *restrict work);
} XmppNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in XmppV or a region of the borrow at a fixed offset.
void protocore_xmpp_escape(uint8_t *restrict work);
void protocore_xmpp_stream_open(uint8_t *restrict work);
void protocore_xmpp_message(uint8_t *restrict work);
void protocore_xmpp_presence(uint8_t *restrict work);
void protocore_xmpp_iq(uint8_t *restrict work);
void protocore_xmpp_stanza_name(uint8_t *restrict work);
void protocore_xmpp_attr(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Xmpp.escape(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const XmppNs Xmpp __attribute__((unused)) = {
    .escape = protocore_xmpp_escape,
    .stream_open = protocore_xmpp_stream_open,
    .message = protocore_xmpp_message,
    .presence = protocore_xmpp_presence,
    .iq = protocore_xmpp_iq,
    .stanza_name = protocore_xmpp_stanza_name,
    .attr = protocore_xmpp_attr,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_XMPP

#endif // PROTOCORE_XMPP_H
