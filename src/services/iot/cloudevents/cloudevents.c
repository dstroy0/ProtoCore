// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cloudevents.c
 * @brief The CloudEvents 1.0.2 envelope: the Structured Content Mode object and the Binary Content
 *        Mode header read.
 *
 * build_structured drives the JSON writer over the caller's buffer, emitting the REQUIRED attributes
 * in order and then whichever optional ones are present. read_binary looks up one header per
 * context attribute and reports whether the three REQUIRED ones arrived.
 */

#include "services/iot/cloudevents/cloudevents.h"

#if PROTOCORE_ENABLE_CLOUDEVENTS

#include "network_drivers/presentation/codec/json/json.h" // Json: the bounded writer an envelope is built with

// The JSON member names an envelope carries (JSON Event Format 1.0.2 sec 3), spelled once so the
// has-data and no-data paths emit the same keys.
#define CE_ATTR_SPECVERSION "specversion"
#define CE_ATTR_ID "id"
#define CE_ATTR_SOURCE "source"
#define CE_ATTR_TYPE "type"
#define CE_ATTR_SUBJECT "subject"
#define CE_ATTR_DATACONTENTTYPE "datacontenttype"
#define CE_MEMBER_DATA "data"

// The headers a context attribute maps to in Binary Content Mode: the `ce-` prefixed name
// (HTTP Protocol Binding 1.0.2 sec 3.1.3.1), and `Content-Type` for datacontenttype (sec 3.1.1).
#define CE_HDR_ID "ce-id"
#define CE_HDR_SOURCE "ce-source"
#define CE_HDR_TYPE "ce-type"
#define CE_HDR_SUBJECT "ce-subject"
#define CE_HDR_CONTENT_TYPE "Content-Type"

// The datacontenttype a JSON-format event with none is equivalent to (CloudEvents 1.0.2, section
// "datacontenttype").
#define CE_IMPLIED_DATACONTENTTYPE "application/json"

// An attribute is present when it is a non-empty string: every REQUIRED attribute "MUST be a
// non-empty string", and a present subject "MUST be a non-empty string" (CloudEvents 1.0.2).
static proto_bool ce_present(const char *s)
{
    return s != NULL && s[0] != '\0';
}

// Build the one JSON object of Structured Content Mode into ns->envelope, and report its octet
// count in ns->n.
static void cloudevents_build_structured(uint8_t *restrict work)
{
    (void)work;
    CloudEvents.ok = PROTO_FALSE;
    CloudEvents.n = 0;

    char *out = CloudEvents.envelope.out;
    const size_t cap = CloudEvents.envelope.cap;
    if (!out || cap == 0)
    {
        return;
    }
    // id, source and type are REQUIRED (CloudEvents 1.0.2, section "REQUIRED Attributes").
    if (!ce_present(CloudEvents.attr.id) || !ce_present(CloudEvents.attr.source) || !ce_present(CloudEvents.attr.type))
    {
        return;
    }

    protocore_json_writer w = {0};
    Json.init(&w, out, cap);
    Json.begin_object(&w);
    Json.kv_str(&w, CE_ATTR_SPECVERSION, PROTOCORE_CLOUDEVENTS_SPECVERSION);
    Json.kv_str(&w, CE_ATTR_ID, CloudEvents.attr.id);
    Json.kv_str(&w, CE_ATTR_SOURCE, CloudEvents.attr.source);
    Json.kv_str(&w, CE_ATTR_TYPE, CloudEvents.attr.type);
    if (ce_present(CloudEvents.attr.subject))
    {
        Json.kv_str(&w, CE_ATTR_SUBJECT, CloudEvents.attr.subject);
    }

    // A JSON value goes out verbatim, a plain string goes out escaped, and either way the member is
    // named `data` (JSON Event Format 1.0.2 sec 3.1.1). A JSON value with no stated datacontenttype
    // takes the implied one.
    if (ce_present(CloudEvents.data.json))
    {
        const char *dct = CE_IMPLIED_DATACONTENTTYPE;
        if (ce_present(CloudEvents.attr.datacontenttype))
        {
            dct = CloudEvents.attr.datacontenttype;
        }
        Json.kv_str(&w, CE_ATTR_DATACONTENTTYPE, dct);
        Json.key(&w, CE_MEMBER_DATA);
        Json.put_raw(&w, CloudEvents.data.json);
    }
    else if (CloudEvents.data.str)
    {
        if (ce_present(CloudEvents.attr.datacontenttype))
        {
            Json.kv_str(&w, CE_ATTR_DATACONTENTTYPE, CloudEvents.attr.datacontenttype);
        }
        Json.kv_str(&w, CE_MEMBER_DATA, CloudEvents.data.str);
    }
    else if (ce_present(CloudEvents.attr.datacontenttype))
    {
        Json.kv_str(&w, CE_ATTR_DATACONTENTTYPE, CloudEvents.attr.datacontenttype);
    }

    Json.end_object(&w);
    if (!protocore_json_ok(&w))
    {
        return;
    }
    CloudEvents.n = protocore_json_length(&w);
    CloudEvents.ok = PROTO_TRUE;
}

// Take the context attributes off the message's headers, and report whether the three REQUIRED ones
// arrived. The payload is the HTTP body in this mode (HTTP Protocol Binding 1.0.2 sec 3.1.2), so the
// data members are cleared rather than filled.
static void cloudevents_read_binary(uint8_t *restrict work)
{
    (void)work;
    CloudEvents.ok = PROTO_FALSE;
    CloudEvents.n = 0;
    CloudEvents.attr.id = NULL;
    CloudEvents.attr.source = NULL;
    CloudEvents.attr.type = NULL;
    CloudEvents.attr.subject = NULL;
    CloudEvents.attr.datacontenttype = NULL;
    CloudEvents.data.json = NULL;
    CloudEvents.data.str = NULL;
    if (!CloudEvents.msg.req)
    {
        return;
    }

    CloudEvents.attr.id = http_get_header(CloudEvents.msg.req, CE_HDR_ID);
    CloudEvents.attr.source = http_get_header(CloudEvents.msg.req, CE_HDR_SOURCE);
    CloudEvents.attr.type = http_get_header(CloudEvents.msg.req, CE_HDR_TYPE);
    CloudEvents.attr.subject = http_get_header(CloudEvents.msg.req, CE_HDR_SUBJECT);
    // datacontenttype rides in Content-Type, and a ce-datacontenttype header "MUST NOT also be
    // present in the message" (HTTP Protocol Binding 1.0.2 sec 3.1.1).
    CloudEvents.attr.datacontenttype = http_get_header(CloudEvents.msg.req, CE_HDR_CONTENT_TYPE);

    CloudEvents.ok =
        ce_present(CloudEvents.attr.id) && ce_present(CloudEvents.attr.source) && ce_present(CloudEvents.attr.type);
}

// Designated, so a member's position in the struct does not decide what it binds to.
CloudEventsNs CloudEvents = {.build_structured = cloudevents_build_structured, .read_binary = cloudevents_read_binary};

#endif // PROTOCORE_ENABLE_CLOUDEVENTS
