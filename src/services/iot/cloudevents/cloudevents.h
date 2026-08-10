// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cloudevents.h
 * @brief CloudEvents v1.0 (CNCF) event envelope - structured JSON build + binary
 *        (ce-* header) read. Zero-heap; builds on the JSON writer.
 *
 * CloudEvents is the application-layer metadata envelope that makes a device's
 * events interoperable with serverless / event-mesh consumers. Two content modes:
 *
 *  - **structured** - the whole event is one `application/cloudevents+json` body;
 *    build it with pc_cloudevents_build_json() into a caller buffer.
 *  - **binary** - the event attributes ride as `ce-*` HTTP headers and the payload
 *    is the body; read an inbound binary event with pc_cloudevents_from_headers().
 *
 * Emit a binary event from a handler by adding the `ce-id` / `ce-source` /
 * `ce-type` / `ce-specversion` response headers yourself (and the data as the body)
 * - no special API needed.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CLOUDEVENTS_H
#define PROTOCORE_CLOUDEVENTS_H

#include "protocore_config.h"

#if PC_ENABLE_CLOUDEVENTS

#include "network_drivers/presentation/http/http_parser/http_parser.h"

/**
 * @brief A CloudEvents v1.0 event. The three required context attributes are
 *        @ref id, @ref source and @ref type (`specversion` is always "1.0").
 *
 * All strings are referenced, not copied. Exactly one of @ref data_json (a
 * pre-formatted JSON value, emitted verbatim) or @ref data_str (a plain string,
 * JSON-escaped) may be set; leave both null for an event with no data.
 */
typedef struct
{
    const char *id;              ///< required: unique id for this event (e.g. a counter / UUID)
    const char *source;          ///< required: the producer context URI-reference (e.g. "/devices/esp32-1")
    const char *type;            ///< required: the event type (e.g. "com.example.sensor.reading")
    const char *subject;         ///< optional: subject within the source, or null
    const char *datacontenttype; ///< optional: media type of data (default "application/json" when data_json set)
    const char *data_json;       ///< optional: data as a pre-formatted JSON value (object/array/number/...)
    const char *data_str;        ///< optional: data as a plain string (emitted as a JSON string)
} CloudEvent;

/**
 * @brief Build a structured CloudEvents JSON envelope into @p buf.
 *
 * Emits `{"specversion":"1.0","id":...,"source":...,"type":...[,"subject":...]
 * [,"datacontenttype":...][,"data":...]}`. Fails (returns 0) if a required
 * attribute is missing/empty or the buffer overflows.
 *
 * @return number of bytes written (excluding the NUL), or 0 on error.
 */
size_t pc_cloudevents_build_json(char *buf, size_t cap, const CloudEvent *ce);

/**
 * @brief Read an inbound binary-mode CloudEvent from a request's `ce-*` headers.
 *
 * Fills @p out from `ce-id` / `ce-source` / `ce-type` / `ce-subject` /
 * `Content-Type` (the data is the request body). The pointers reference the
 * request's header storage (valid for the duration of the request).
 *
 * @return true if the three required attributes (id, source, type) are present.
 */
proto_bool pc_cloudevents_from_headers(const HttpReq *req, CloudEvent *out);

#endif // PC_ENABLE_CLOUDEVENTS

#endif // PROTOCORE_CLOUDEVENTS_H
