// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cloudevents.cpp
 * @brief CloudEvents v1.0 structured-JSON build + binary-header read.
 */

#include "services/iot/cloudevents/cloudevents.h"

#if PC_ENABLE_CLOUDEVENTS

#include "network_drivers/presentation/codec/json/json.h"
#include <string.h>

static bool ce_present(const char *s)
{
    return s != nullptr && s[0] != '\0';
}

// CloudEvents 1.0 JSON attribute names emitted in more than one branch below; named once so the
// (typo-prone) spec keys stay identical across the has-data / no-data paths. (Flash-resident.)
static const char *const CE_KEY_DATACONTENTTYPE = "datacontenttype";
static const char *const CE_KEY_DATA = "data";

size_t pc_cloudevents_build_json(char *buf, size_t cap, const CloudEvent *ce)
{
    if (!buf || cap == 0 || !ce)
    {
        return 0;
    }
    // The three context attributes id/source/type are REQUIRED (CloudEvents 1.0).
    if (!ce_present(ce->id) || !ce_present(ce->source) || !ce_present(ce->type))
    {
        return 0;
    }

    JsonWriter w(buf, cap);
    w.begin_object();
    w.kv_str("specversion", "1.0");
    w.kv_str("id", ce->id);
    w.kv_str("source", ce->source);
    w.kv_str("type", ce->type);
    if (ce_present(ce->subject))
    {
        w.kv_str("subject", ce->subject);
    }

    // data: a pre-formatted JSON value (verbatim) or a plain string (escaped).
    if (ce->data_json && ce->data_json[0] != '\0')
    {
        w.kv_str(CE_KEY_DATACONTENTTYPE, ce_present(ce->datacontenttype) ? ce->datacontenttype : "application/json");
        w.key(CE_KEY_DATA);
        w.raw(ce->data_json);
    }
    else if (ce->data_str)
    {
        if (ce_present(ce->datacontenttype))
        {
            w.kv_str(CE_KEY_DATACONTENTTYPE, ce->datacontenttype);
        }
        w.kv_str(CE_KEY_DATA, ce->data_str);
    }
    else if (ce_present(ce->datacontenttype))
    {
        w.kv_str(CE_KEY_DATACONTENTTYPE, ce->datacontenttype);
    }

    w.end_object();
    return w.ok() ? strnlen(buf, cap) : 0;
}

bool pc_cloudevents_from_headers(const HttpReq *req, CloudEvent *out)
{
    if (!req || !out)
    {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->id = http_get_header(req, "ce-id");
    out->source = http_get_header(req, "ce-source");
    out->type = http_get_header(req, "ce-type");
    out->subject = http_get_header(req, "ce-subject");
    out->datacontenttype = http_get_header(req, "Content-Type");
    return ce_present(out->id) && ce_present(out->source) && ce_present(out->type);
}

#endif // PC_ENABLE_CLOUDEVENTS
