// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file xmpp.h
 * @brief XMPP (RFC 6120) stanza codec (PC_ENABLE_XMPP).
 *
 * XMPP (Jabber) is an XML streaming protocol: after a `<stream:stream>` open, peers exchange three
 * stanza kinds - `<message>`, `<presence>`, and `<iq>` (info/query). This is the stanza codec: builders
 * that emit correctly XML-escaped stanzas into a caller buffer (so a device can publish sensor data or
 * receive commands as an IoT XMPP client), plus minimal readers to pull the stanza element name and an
 * attribute value out of a received stanza.
 *
 * Pure text framing, zero heap, no stdlib, host-testable. TLS (`starttls`) + SASL auth ride the existing
 * client TLS path; the IoT XEPs (0323 sensor-data, 0325 control) layer their payloads inside `<iq>`.
 */

#ifndef PROTOCORE_XMPP_H
#define PROTOCORE_XMPP_H

#include "protocore_config.h"

#if PC_ENABLE_XMPP

/**
 * @brief XML-escape @p in into @p out (& < > ' " -> entities). @return bytes written (excl NUL), or 0 if
 *        it would overflow @p cap (a NUL terminator is written when there is room).
 */
size_t pc_xmpp_escape(const char *in, size_t in_len, char *out, size_t cap);

/**
 * @brief Build the initial `<stream:stream ...>` open tag (jabber:client). @return length, or 0 on overflow.
 */
size_t pc_xmpp_stream_open(const char *from, const char *to, char *out, size_t cap);

/**
 * @brief Build a `<message to=.. from=.. type=..><body>..</body></message>` stanza.
 * @param type e.g. "chat"; null omits the type attribute. from may be null (server stamps it).
 * @return length, or 0 on overflow.
 */
size_t pc_xmpp_message(const char *to, const char *from, const char *type, const char *body, char *out, size_t cap);

/** @brief Build a `<presence/>` (type null) or `<presence type=".."/>` stanza. @return length, or 0. */
size_t pc_xmpp_presence(const char *type, char *out, size_t cap);

/**
 * @brief Build an `<iq type=.. id=..>child</iq>` stanza (child is inserted verbatim; may be null/empty).
 * @return length, or 0 on overflow.
 */
size_t pc_xmpp_iq(const char *type, const char *id, const char *child_xml, char *out, size_t cap);

/**
 * @brief Read the stanza's top-level element name (message / presence / iq / ...) into @p out.
 * @return the name length, or 0 if no start tag is found.
 */
size_t pc_xmpp_stanza_name(const char *xml, size_t len, char *out, size_t cap);

/**
 * @brief Extract the value of attribute @p attr from the stanza's start tag into @p out (unescaped copy
 *        is NOT performed - the raw attribute text is returned). @return value length, or 0 if absent.
 */
size_t pc_xmpp_attr(const char *xml, size_t len, const char *attr, char *out, size_t cap);

#endif // PC_ENABLE_XMPP
#endif // PROTOCORE_XMPP_H
