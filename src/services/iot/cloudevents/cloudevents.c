// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

/**
 * @brief The envelope's calls - what CloudEventsNs points at.
 *
 * @var CloudEventsInternal::ns  the handle a caller sets a call's members on
 */
struct CloudEventsInternal
{
    CloudEventsNs *ns;
};

static struct CloudEventsInternal s_cloudevents = {.ns = &CloudEvents};

// An attribute is present when it is a non-empty string: every REQUIRED attribute "MUST be a
// non-empty string", and a present subject "MUST be a non-empty string" (CloudEvents 1.0.2).
static proto_bool ce_present(const char *s)
{
    return s != NULL && s[0] != '\0';
}

// Build the one JSON object of Structured Content Mode into ns->envelope, and report its octet
// count in ns->n.
static void cloudevents_build_structured(struct CloudEventsInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;

    char *out = ctx->ns->envelope.out;
    const size_t cap = ctx->ns->envelope.cap;
    if (!out || cap == 0)
    {
        return;
    }
    // id, source and type are REQUIRED (CloudEvents 1.0.2, section "REQUIRED Attributes").
    if (!ce_present(ctx->ns->attr.id) || !ce_present(ctx->ns->attr.source) || !ce_present(ctx->ns->attr.type))
    {
        return;
    }

    protocore_json_writer w = {0};
    Json.init(&w, out, cap);
    Json.begin_object(&w);
    Json.kv_str(&w, CE_ATTR_SPECVERSION, PROTOCORE_CLOUDEVENTS_SPECVERSION);
    Json.kv_str(&w, CE_ATTR_ID, ctx->ns->attr.id);
    Json.kv_str(&w, CE_ATTR_SOURCE, ctx->ns->attr.source);
    Json.kv_str(&w, CE_ATTR_TYPE, ctx->ns->attr.type);
    if (ce_present(ctx->ns->attr.subject))
    {
        Json.kv_str(&w, CE_ATTR_SUBJECT, ctx->ns->attr.subject);
    }

    // A JSON value goes out verbatim, a plain string goes out escaped, and either way the member is
    // named `data` (JSON Event Format 1.0.2 sec 3.1.1). A JSON value with no stated datacontenttype
    // takes the implied one.
    if (ce_present(ctx->ns->data.json))
    {
        const char *dct = CE_IMPLIED_DATACONTENTTYPE;
        if (ce_present(ctx->ns->attr.datacontenttype))
        {
            dct = ctx->ns->attr.datacontenttype;
        }
        Json.kv_str(&w, CE_ATTR_DATACONTENTTYPE, dct);
        Json.key(&w, CE_MEMBER_DATA);
        Json.put_raw(&w, ctx->ns->data.json);
    }
    else if (ctx->ns->data.str)
    {
        if (ce_present(ctx->ns->attr.datacontenttype))
        {
            Json.kv_str(&w, CE_ATTR_DATACONTENTTYPE, ctx->ns->attr.datacontenttype);
        }
        Json.kv_str(&w, CE_MEMBER_DATA, ctx->ns->data.str);
    }
    else if (ce_present(ctx->ns->attr.datacontenttype))
    {
        Json.kv_str(&w, CE_ATTR_DATACONTENTTYPE, ctx->ns->attr.datacontenttype);
    }

    Json.end_object(&w);
    if (!protocore_json_ok(&w))
    {
        return;
    }
    ctx->ns->n = protocore_json_length(&w);
    ctx->ns->ok = PROTO_TRUE;
}

// Take the context attributes off the message's headers, and report whether the three REQUIRED ones
// arrived. The payload is the HTTP body in this mode (HTTP Protocol Binding 1.0.2 sec 3.1.2), so the
// data members are cleared rather than filled.
static void cloudevents_read_binary(struct CloudEventsInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    ctx->ns->attr.id = NULL;
    ctx->ns->attr.source = NULL;
    ctx->ns->attr.type = NULL;
    ctx->ns->attr.subject = NULL;
    ctx->ns->attr.datacontenttype = NULL;
    ctx->ns->data.json = NULL;
    ctx->ns->data.str = NULL;
    if (!ctx->ns->msg.req)
    {
        return;
    }

    ctx->ns->attr.id = http_get_header(ctx->ns->msg.req, CE_HDR_ID);
    ctx->ns->attr.source = http_get_header(ctx->ns->msg.req, CE_HDR_SOURCE);
    ctx->ns->attr.type = http_get_header(ctx->ns->msg.req, CE_HDR_TYPE);
    ctx->ns->attr.subject = http_get_header(ctx->ns->msg.req, CE_HDR_SUBJECT);
    // datacontenttype rides in Content-Type, and a ce-datacontenttype header "MUST NOT also be
    // present in the message" (HTTP Protocol Binding 1.0.2 sec 3.1.1).
    ctx->ns->attr.datacontenttype = http_get_header(ctx->ns->msg.req, CE_HDR_CONTENT_TYPE);

    ctx->ns->ok =
        ce_present(ctx->ns->attr.id) && ce_present(ctx->ns->attr.source) && ce_present(ctx->ns->attr.type);
}

// Designated, so a member's position in the struct does not decide what it binds to.
CloudEventsNs CloudEvents = {
    .build_structured = cloudevents_build_structured,
    .read_binary = cloudevents_read_binary,
    .internal = &s_cloudevents,
};

#endif // PROTOCORE_ENABLE_CLOUDEVENTS
