// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file senml.c
 * @brief The SenML Pack walk (RFC 8428): once for the JSON representation, once for any binary
 *        codec, plus the Record resolver.
 *
 * Each build walks the Record array and emits one label / value pair per field present. The JSON
 * build writes the labels as member names (sec 5); the binary build hands both spellings to the
 * codec, which writes the sec 6 Table 4 integer map key. The resolve carries the Base Name and Base
 * Time forward and folds them into every Record (sec 4.6).
 */

#include "services/iot/senml/senml.h"

#if PROTOCORE_ENABLE_SENML

#include "mmgr/membuild.h" // Sb: the Number rendering, and the resolved-Name concatenation
#include "mmgr/span.h"     // protocore_span: the region a binary build writes into
#include "network_drivers/presentation/codec/json/json.h"

// RFC 8428 sec 6 Table 4: the integer map keys the binary representation uses for the labels. Base
// fields take negative labels, the rest non-negative.
#define SENML_LABEL_BASE_NAME (-2)
#define SENML_LABEL_BASE_TIME (-3)
#define SENML_LABEL_NAME 0
#define SENML_LABEL_UNIT 1
#define SENML_LABEL_VALUE 2
#define SENML_LABEL_STRING_VALUE 3
#define SENML_LABEL_BOOLEAN_VALUE 4
#define SENML_LABEL_TIME 6

// Significant digits a non-integral Number is rendered with in the JSON representation.
#define SENML_JSON_NUMBER_DIGITS 6

// Longest rendering of one JSON Number, the NUL included.
#define SENML_JSON_NUMBER_MAX 32

/**
 * @brief The builder's calls - what SenmlNs points at.
 *
 * No storage member: every call writes into a region the caller bound, so the module holds nothing
 * of its own.
 *
 * @var SenmlInternal::ns  the handle a caller sets a call's members on
 */
struct SenmlInternal
{
    SenmlNs *ns;
};

static struct SenmlInternal s_senml = {.ns = &Senml};

// True when d is an integer inside the int64 range, so it converts to one losslessly.
static proto_bool is_integral(double d)
{
    return d >= -9.2e18 && d <= 9.2e18 && d == (double)(int64_t)d;
}

// Render a Number into a JSON value position: an integer when integral, else a floating-point form
// of SENML_JSON_NUMBER_DIGITS significant digits.
static void json_number(protocore_json_writer *w, double d)
{
    char tmp[SENML_JSON_NUMBER_MAX];
    protocore_sb sb = {tmp, sizeof(tmp), 0, PROTO_TRUE};
    if (is_integral(d))
    {
        Sb.i64(&sb, (int64_t)d);
    }
    else
    {
        Sb.g(&sb, d, SENML_JSON_NUMBER_DIGITS);
    }
    if (Sb.finish(&sb) == 0)
    {
        tmp[0] = '\0';
    }
    Json.put_raw(w, tmp);
}

// Encode a Number through the codec: an integer when integral, else a floating-point item.
static void codec_number(const protocore_codec *c, protocore_span *w, double d)
{
    if (is_integral(d))
    {
        c->put_int(w, (int64_t)d);
    }
    else
    {
        c->put_float(w, (float)d);
    }
}

// How many label / value pairs one Record emits, which is the map length the binary build declares.
static size_t record_fields(const SenmlRecord *r)
{
    size_t n = 0;
    if (r->base_name)
    {
        n++;
    }
    if (r->has_base_time)
    {
        n++;
    }
    if (r->name)
    {
        n++;
    }
    if (r->unit)
    {
        n++;
    }
    if (r->value_kind != SENML_VALUE_NONE && !(r->value_kind == SENML_VALUE_STRING && !r->string_value))
    {
        n++;
    }
    if (r->has_time)
    {
        n++;
    }
    return n;
}

// Build the JSON representation of ns->pack into ns->json (RFC 8428 sec 5): an array with one
// object per Record, the labels as member names. Reports the length in ns->n.
static void senml_json_build(struct SenmlInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    const SenmlRecord *records = ctx->ns->pack.records;
    const size_t count = ctx->ns->pack.count;
    if (!ctx->ns->json.buf || (count && !records))
    {
        return;
    }
    protocore_json_writer w = {0};
    Json.init(&w, ctx->ns->json.buf, ctx->ns->json.cap);
    Json.begin_array(&w);
    for (size_t i = 0; i < count; i++)
    {
        const SenmlRecord *r = &records[i];
        Json.begin_object(&w);
        if (r->base_name)
        {
            Json.kv_str(&w, "bn", r->base_name);
        }
        if (r->has_base_time)
        {
            Json.key(&w, "bt");
            json_number(&w, r->base_time);
        }
        if (r->name)
        {
            Json.kv_str(&w, "n", r->name);
        }
        if (r->unit)
        {
            Json.kv_str(&w, "u", r->unit);
        }
        // Every SenmlValueKind enumerator has a case below, so the default edge the compiler emits
        // for the uint8_t-backed enum is unreachable for any value the API admits.
        switch (r->value_kind)
        {
        case SENML_VALUE_NUMBER:
            Json.key(&w, "v");
            json_number(&w, r->value);
            break;
        case SENML_VALUE_STRING:
            if (r->string_value)
            {
                Json.kv_str(&w, "vs", r->string_value);
            }
            break;
        case SENML_VALUE_BOOLEAN:
            Json.kv_bool(&w, "vb", r->boolean_value);
            break;
        case SENML_VALUE_NONE:
            break;
        }
        if (r->has_time)
        {
            Json.key(&w, "t");
            json_number(&w, r->time);
        }
        Json.end_object(&w);
    }
    Json.end_array(&w);
    if (!protocore_json_ok(&w))
    {
        return;
    }
    ctx->ns->n = protocore_json_length(&w);
    ctx->ns->ok = PROTO_TRUE;
}

// Build ns->pack into ns->binary through its codec: an array of Records, each a map whose keys the
// codec writes as the RFC 8428 sec 6 Table 4 integers. Reports the length in ns->n.
static void senml_binary_build(struct SenmlInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    const protocore_codec *c = ctx->ns->binary.codec;
    const SenmlRecord *records = ctx->ns->pack.records;
    const size_t count = ctx->ns->pack.count;
    if (!c || !ctx->ns->binary.buf || (count && !records))
    {
        return;
    }
    protocore_span w = span.from(ctx->ns->binary.buf, ctx->ns->binary.cap);
    c->put_array(&w, count);
    for (size_t i = 0; i < count; i++)
    {
        const SenmlRecord *r = &records[i];
        c->put_map(&w, record_fields(r));
        if (r->base_name)
        {
            c->put_label(&w, "bn", SENML_LABEL_BASE_NAME);
            c->put_str(&w, r->base_name);
        }
        if (r->has_base_time)
        {
            c->put_label(&w, "bt", SENML_LABEL_BASE_TIME);
            codec_number(c, &w, r->base_time);
        }
        if (r->name)
        {
            c->put_label(&w, "n", SENML_LABEL_NAME);
            c->put_str(&w, r->name);
        }
        if (r->unit)
        {
            c->put_label(&w, "u", SENML_LABEL_UNIT);
            c->put_str(&w, r->unit);
        }
        // Exhaustive over SenmlValueKind, as in the JSON build above, and record_fields() counts
        // the same four kinds, so the declared map length always matches what is emitted.
        switch (r->value_kind)
        {
        case SENML_VALUE_NUMBER:
            c->put_label(&w, "v", SENML_LABEL_VALUE);
            codec_number(c, &w, r->value);
            break;
        case SENML_VALUE_STRING:
            if (r->string_value)
            {
                c->put_label(&w, "vs", SENML_LABEL_STRING_VALUE);
                c->put_str(&w, r->string_value);
            }
            break;
        case SENML_VALUE_BOOLEAN:
            c->put_label(&w, "vb", SENML_LABEL_BOOLEAN_VALUE);
            c->put_bool(&w, r->boolean_value);
            break;
        case SENML_VALUE_NONE:
            break;
        }
        if (r->has_time)
        {
            c->put_label(&w, "t", SENML_LABEL_TIME);
            codec_number(c, &w, r->time);
        }
    }
    if (!span.ok(w))
    {
        return;
    }
    ctx->ns->n = span.len(w);
    ctx->ns->ok = PROTO_TRUE;
}

// Resolve ns->pack into ns->resolved (RFC 8428 sec 4.6). A Base Name or Base Time becomes active
// for the Record carrying it and every Record after it, until a later one overrides it: each output
// Name is the active Base Name concatenated with the Name, and each output Time is the active Base
// Time added to the Time. Reports the Record count in ns->n.
static void senml_resolve(struct SenmlInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    const SenmlRecord *in = ctx->ns->pack.records;
    SenmlResolved *out = ctx->ns->resolved.out;
    if (!in || !out)
    {
        return;
    }
    const char *base_name = NULL;
    proto_bool base_time_set = PROTO_FALSE;
    double base_time = 0.0;

    const size_t count = ctx->ns->pack.count < ctx->ns->resolved.max ? ctx->ns->pack.count : ctx->ns->resolved.max;
    for (size_t i = 0; i < count; i++)
    {
        const SenmlRecord *r = &in[i];
        if (r->base_name)
        {
            base_name = r->base_name;
        }
        if (r->has_base_time)
        {
            base_time = r->base_time;
            base_time_set = PROTO_TRUE;
        }

        SenmlResolved *o = &out[i];
        protocore_sb sb_name = {o->name, sizeof(o->name), 0, PROTO_TRUE};
        Sb.put(&sb_name, base_name ? base_name : "");
        Sb.put(&sb_name, r->name ? r->name : "");
        (void)Sb.finish(&sb_name);
        if (!sb_name.ok)
        {
            o->name[0] = '\0';
        }

        o->has_time = base_time_set || r->has_time;
        o->time = (base_time_set ? base_time : 0.0) + (r->has_time ? r->time : 0.0);

        o->unit = r->unit;
        o->value_kind = r->value_kind;
        o->value = r->value;
        o->string_value = r->string_value;
        o->boolean_value = r->boolean_value;
    }
    ctx->ns->n = count;
    ctx->ns->ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
SenmlNs Senml = {
    .json_build = senml_json_build, .binary_build = senml_binary_build, .resolve = senml_resolve, .internal = &s_senml};

#endif // PROTOCORE_ENABLE_SENML
