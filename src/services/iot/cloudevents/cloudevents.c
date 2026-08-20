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

static uint8_t json_work[16]; // the borrow an entry takes; Json never reads it

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
void protocore_cloud_events_build_structured(uint8_t *restrict work)
{
    (void)work;
    CloudEventsV.ok = PROTO_FALSE;
    CloudEventsV.n = 0;

    char *out = CloudEventsV.envelope.out;
    const size_t cap = CloudEventsV.envelope.cap;
    if (!out || cap == 0)
    {
        return;
    }
    // id, source and type are REQUIRED (CloudEvents 1.0.2, section "REQUIRED Attributes").
    if (!ce_present(CloudEventsV.attr.id) || !ce_present(CloudEventsV.attr.source) ||
        !ce_present(CloudEventsV.attr.type))
    {
        return;
    }

    protocore_json_writer w = {0};
    Json.init_args.w = &w;
    Json.init_args.buf = out;
    Json.init_args.cap = cap;
    Json.init(json_work);
    Json.begin_object_args.w = &w;
    Json.begin_object(json_work);
    Json.kv_str_args.w = &w;
    Json.kv_str_args.k = CE_ATTR_SPECVERSION;
    Json.kv_str_args.v = PROTOCORE_CLOUDEVENTS_SPECVERSION;
    Json.kv_str(json_work);
    Json.kv_str_args.w = &w;
    Json.kv_str_args.k = CE_ATTR_ID;
    Json.kv_str_args.v = CloudEventsV.attr.id;
    Json.kv_str(json_work);
    Json.kv_str_args.w = &w;
    Json.kv_str_args.k = CE_ATTR_SOURCE;
    Json.kv_str_args.v = CloudEventsV.attr.source;
    Json.kv_str(json_work);
    Json.kv_str_args.w = &w;
    Json.kv_str_args.k = CE_ATTR_TYPE;
    Json.kv_str_args.v = CloudEventsV.attr.type;
    Json.kv_str(json_work);
    if (ce_present(CloudEventsV.attr.subject))
    {
        Json.kv_str_args.w = &w;
        Json.kv_str_args.k = CE_ATTR_SUBJECT;
        Json.kv_str_args.v = CloudEventsV.attr.subject;
        Json.kv_str(json_work);
    }

    // A JSON value goes out verbatim, a plain string goes out escaped, and either way the member is
    // named `data` (JSON Event Format 1.0.2 sec 3.1.1). A JSON value with no stated datacontenttype
    // takes the implied one.
    if (ce_present(CloudEventsV.data.json))
    {
        const char *dct = CE_IMPLIED_DATACONTENTTYPE;
        if (ce_present(CloudEventsV.attr.datacontenttype))
        {
            dct = CloudEventsV.attr.datacontenttype;
        }
        Json.kv_str_args.w = &w;
        Json.kv_str_args.k = CE_ATTR_DATACONTENTTYPE;
        Json.kv_str_args.v = dct;
        Json.kv_str(json_work);
        Json.key_args.w = &w;
        Json.key_args.k = CE_MEMBER_DATA;
        Json.key(json_work);
        Json.put_raw_args.w = &w;
        Json.put_raw_args.literal = CloudEventsV.data.json;
        Json.put_raw(json_work);
    }
    else if (CloudEventsV.data.str)
    {
        if (ce_present(CloudEventsV.attr.datacontenttype))
        {
            Json.kv_str_args.w = &w;
            Json.kv_str_args.k = CE_ATTR_DATACONTENTTYPE;
            Json.kv_str_args.v = CloudEventsV.attr.datacontenttype;
            Json.kv_str(json_work);
        }
        Json.kv_str_args.w = &w;
        Json.kv_str_args.k = CE_MEMBER_DATA;
        Json.kv_str_args.v = CloudEventsV.data.str;
        Json.kv_str(json_work);
    }
    else if (ce_present(CloudEventsV.attr.datacontenttype))
    {
        Json.kv_str_args.w = &w;
        Json.kv_str_args.k = CE_ATTR_DATACONTENTTYPE;
        Json.kv_str_args.v = CloudEventsV.attr.datacontenttype;
        Json.kv_str(json_work);
    }

    Json.end_object_args.w = &w;
    Json.end_object(json_work);
    if (!protocore_json_ok(&w))
    {
        return;
    }
    CloudEventsV.n = protocore_json_length(&w);
    CloudEventsV.ok = PROTO_TRUE;
}

// Take the context attributes off the message's headers, and report whether the three REQUIRED ones
// arrived. The payload is the HTTP body in this mode (HTTP Protocol Binding 1.0.2 sec 3.1.2), so the
// data members are cleared rather than filled.
void protocore_cloud_events_read_binary(uint8_t *restrict work)
{
    (void)work;
    CloudEventsV.ok = PROTO_FALSE;
    CloudEventsV.n = 0;
    CloudEventsV.attr.id = NULL;
    CloudEventsV.attr.source = NULL;
    CloudEventsV.attr.type = NULL;
    CloudEventsV.attr.subject = NULL;
    CloudEventsV.attr.datacontenttype = NULL;
    CloudEventsV.data.json = NULL;
    CloudEventsV.data.str = NULL;
    if (!CloudEventsV.msg.req)
    {
        return;
    }

    HttpParserV.get_header_args.req = CloudEventsV.msg.req;
    HttpParserV.get_header_args.key = CE_HDR_ID;
    HttpParser.get_header(protocore_http_parser_span());
    CloudEventsV.attr.id = HttpParserV.text;
    HttpParserV.get_header_args.req = CloudEventsV.msg.req;
    HttpParserV.get_header_args.key = CE_HDR_SOURCE;
    HttpParser.get_header(protocore_http_parser_span());
    CloudEventsV.attr.source = HttpParserV.text;
    HttpParserV.get_header_args.req = CloudEventsV.msg.req;
    HttpParserV.get_header_args.key = CE_HDR_TYPE;
    HttpParser.get_header(protocore_http_parser_span());
    CloudEventsV.attr.type = HttpParserV.text;
    HttpParserV.get_header_args.req = CloudEventsV.msg.req;
    HttpParserV.get_header_args.key = CE_HDR_SUBJECT;
    HttpParser.get_header(protocore_http_parser_span());
    CloudEventsV.attr.subject = HttpParserV.text;
    // datacontenttype rides in Content-Type, and a ce-datacontenttype header "MUST NOT also be
    // present in the message" (HTTP Protocol Binding 1.0.2 sec 3.1.1).
    HttpParserV.get_header_args.req = CloudEventsV.msg.req;
    HttpParserV.get_header_args.key = CE_HDR_CONTENT_TYPE;
    HttpParser.get_header(protocore_http_parser_span());
    CloudEventsV.attr.datacontenttype = HttpParserV.text;

    CloudEventsV.ok =
        ce_present(CloudEventsV.attr.id) && ce_present(CloudEventsV.attr.source) && ce_present(CloudEventsV.attr.type);
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
CloudEventsVars CloudEventsV;

#endif // PROTOCORE_ENABLE_CLOUDEVENTS
