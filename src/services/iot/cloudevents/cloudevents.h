// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cloudevents.h
 * @brief The CloudEvents envelope: the structured-mode JSON build and the binary-mode header read.
 *
 * CloudEvents is a CNCF specification, not an IETF one. Everything here follows CloudEvents
 * Version 1.0.2 and its two companion documents: the JSON Event Format for CloudEvents Version 1.0.2
 * and the HTTP Protocol Binding for CloudEvents Version 1.0.2.
 *
 * The core specification, section "Context Attributes", splits the attributes an event carries into
 * "REQUIRED Attributes" - `id`, `source`, `specversion`, `type` - and "OPTIONAL Attributes" -
 * `datacontenttype`, `dataschema`, `subject`, `time`. Section "Event Data" gives the payload, which
 * "will be encapsulated within `data`". `specversion` is written for the caller: the section by that
 * name states a producer "MUST use a value of `1.0` when referring to this version of the
 * specification", so every envelope built here carries ::PROTOCORE_CLOUDEVENTS_SPECVERSION.
 *
 * The HTTP Protocol Binding sec 1.3 names the content modes, and this module covers two of the
 * three:
 *
 *  - **Structured Content Mode** (sec 3.2): the whole event is one JSON object in the message body,
 *    and sec 3.2.1 requires the `Content-Type` to be the event format's media type, which the JSON
 *    Event Format sec 3 fixes at ::PROTOCORE_CLOUDEVENTS_MEDIA_TYPE. @ref CloudEventsNs::build_structured
 *    writes that object into a caller buffer.
 *  - **Binary Content Mode** (sec 3.1): sec 3.1.3.1 maps every context attribute to an HTTP header
 *    "with the same name as the attribute name but prefixed with `ce-`", sec 3.1.1 carries
 *    `datacontenttype` in `Content-Type` instead, and sec 3.1.2 makes the message body the `data`
 *    byte-sequence. @ref CloudEventsNs::read_binary takes an inbound message's attributes off those
 *    headers.
 *
 * Batched Content Mode (sec 3.3) and its `application/cloudevents-batch+json` media type are not
 * built here.
 *
 * Emitting a binary-mode event from a handler is the response headers plus the body, so no call
 * covers it: add `ce-id`, `ce-source`, `ce-type` and `ce-specversion`, and write the data as the
 * body.
 *
 * Every string is referenced, never copied, so what a caller sets has to outlive the call that
 * reads it.
 *
 * The module exports one symbol, @ref CloudEvents. Everything in cloudevents.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CLOUDEVENTS_H
#define PROTOCORE_CLOUDEVENTS_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_CLOUDEVENTS

PROTOCORE_BEGIN_DECLS

#include "network_drivers/presentation/http/http_parser/http_parser.h" // HttpReq: the message a binary read parses

/** @brief The `specversion` every envelope carries (CloudEvents 1.0.2, section "specversion"). */
#define PROTOCORE_CLOUDEVENTS_SPECVERSION "1.0"

/**
 * @brief The media type a structured-mode message is carried as (JSON Event Format 1.0.2 sec 3).
 *
 * What the HTTP Protocol Binding 1.0.2 sec 3.2.1 requires the `Content-Type` to be set to.
 */
#define PROTOCORE_CLOUDEVENTS_MEDIA_TYPE "application/cloudevents+json"

/**
 * @brief The context attributes one event carries (CloudEvents 1.0.2, section "Context Attributes").
 *
 * A build reads these; a binary-mode read writes them. `specversion` is not among them: the module
 * writes ::PROTOCORE_CLOUDEVENTS_SPECVERSION itself.
 */
typedef struct
{
    const char *id;              ///< REQUIRED `id`: non-empty, unique within the producer's scope
    const char *source;          ///< REQUIRED `source`: non-empty URI-reference naming the producer context
    const char *type;            ///< REQUIRED `type`: non-empty, reverse-DNS prefixed by convention
    const char *subject;         ///< OPTIONAL `subject`: non-empty when present; NULL or "" omits it
    const char *datacontenttype; ///< OPTIONAL `datacontenttype`: an RFC 2046 media type; NULL or "" omits it
} CloudEventAttrArgs;

/**
 * @brief The payload, in the two shapes a JSON serializer takes it in (JSON Event Format 1.0.2 sec 3.1.1).
 *
 * At most one is read, @c json first. Both NULL is an event with no `data` (CloudEvents 1.0.2
 * section "Event Data": OPTIONAL).
 */
typedef struct
{
    const char *json; ///< `data` as a pre-formatted JSON value, emitted verbatim
    const char *str;  ///< `data` as a plain string, emitted as a JSON string with its escapes
} CloudEventDataArgs;

/** @brief Where a structured-mode message body lands (HTTP Protocol Binding 1.0.2 sec 3.2). */
typedef struct
{
    char *out;  ///< the buffer the JSON object is written into
    size_t cap; ///< octets that buffer holds, the NUL included
} CloudEventEnvelopeArgs;

/** @brief The inbound message a binary-mode read parses (HTTP Protocol Binding 1.0.2 sec 3.1). */
typedef struct
{
    const HttpReq *req; ///< the parsed request whose `ce-` prefixed headers carry the attributes
} CloudEventMessageArgs;

/**
 * @brief The CloudEvents envelope: structured-mode build, binary-mode read.
 *
 * A caller sets the members a call takes, invokes it through ::CloudEvents, and reads the outcome
 * off the same handle.
 *
 * @ref CloudEventsNs::attr is both directions: a build reads the attributes a caller set, and a read
 * writes the attributes it found on the message. A read clears @ref CloudEventsNs::data, because
 * binary mode puts the payload in the HTTP body (sec 3.1.2), not in an attribute.
 *
 * No slot member: one event is built or read at a time, so no call names a row.
 *
 * No storage member: every octet a call touches belongs to the caller or to the request, so nothing
 * survives a call.
 *
 * @var CloudEventsNs::attr             the context attributes: a build's input, a read's output
 * @var CloudEventsNs::data             the payload a build serializes under `data`
 * @var CloudEventsNs::envelope         where a structured-mode build writes its JSON object
 * @var CloudEventsNs::msg              the message a binary-mode read takes its attributes off
 * @var CloudEventsNs::ok               a build wrote the whole object, or a read found all three REQUIRED attributes
 * @var CloudEventsNs::n                octets a build wrote, excluding the NUL; 0 when it wrote none
 * @var CloudEventsNs::build_structured build the one JSON object of Structured Content Mode into @c envelope
 * @var CloudEventsNs::read_binary      take @c attr off the `ce-` prefixed headers of Binary Content Mode
 *
 * build_structured emits
 * `{"specversion":"1.0","id":...,"source":...,"type":...[,"subject":...][,"datacontenttype":...][,"data":...]}`.
 * It reports 0 when a REQUIRED attribute is absent or empty, when @c envelope names no buffer, or
 * when the object does not fit @c envelope.cap.
 *
 * read_binary points @c attr at the request's own header storage, so the attributes live exactly as
 * long as the request does. `datacontenttype` comes off `Content-Type`, which sec 3.1.1 makes the
 * only place it may ride.
 */
typedef struct
{
    CloudEventAttrArgs attr;         ///< what an event says about itself
    CloudEventDataArgs data;         ///< what it carries
    CloudEventEnvelopeArgs envelope; ///< where a structured build writes
    CloudEventMessageArgs msg;       ///< what a binary read parses

    proto_bool ok;
    size_t n;

    void (*const build_structured)(uint8_t *restrict work);
    void (*const read_binary)(uint8_t *restrict work);
} CloudEventsNs;

/** @brief The one symbol this module exports. */
extern CloudEventsNs CloudEvents;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CLOUDEVENTS

#endif // PROTOCORE_CLOUDEVENTS_H
