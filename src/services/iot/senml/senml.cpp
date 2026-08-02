// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file senml.cpp
 * @brief SenML pack builders: JSON, plus any binary codec via pc_codec (pure, host-tested).
 */

#include "services/iot/senml/senml.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_SENML

#include "network_drivers/presentation/codec/cbor/cbor.h"
#include "network_drivers/presentation/codec/codec.h"
#include "network_drivers/presentation/codec/json/json.h"
#include <stdio.h> // snprintf for the JSON number formatting

// SenML-CBOR integer labels (RFC 8428).
#define SENML_LBL_BN (-2)
#define SENML_LBL_BT (-3)
#define SENML_LBL_N 0
#define SENML_LBL_U 1
#define SENML_LBL_V 2
#define SENML_LBL_VS 3
#define SENML_LBL_VB 4
#define SENML_LBL_T 6

// True when @p d is an integer that fits in int64 (so it can be emitted losslessly as one).
static proto_bool is_integral(double d)
{
    return d >= -9.2e18 && d <= 9.2e18 && d == (double)(int64_t)d;
}

// Emit a SenML number into a JSON value position: an integer when integral (keeps timestamp
// precision), otherwise a %g float.
static void json_num(JsonWriter &w, double d)
{
    char tmp[32];
    if (is_integral(d))
    {
        pc_sb sb_tmp = {tmp, sizeof(tmp), 0, PROTO_TRUE};
        pc_sb_i64(&sb_tmp, (int64_t)((long long)d));
        if (pc_sb_finish(&sb_tmp) == 0)
        {
            tmp[0] = '\0';
        }
    }
    else
    {
        pc_sb sb_tmp2 = {tmp, sizeof(tmp), 0, PROTO_TRUE};
        pc_sb_g(&sb_tmp2, (double)(d), 6);
        if (pc_sb_finish(&sb_tmp2) == 0)
        {
            tmp[0] = '\0';
        }
    }
    w.raw(tmp);
}

size_t pc_senml_json_build(char *buf, size_t cap, const SenmlRecord *records, size_t count)
{
    if (!buf || (count && !records))
    {
        return 0;
    }
    JsonWriter w(buf, cap);
    w.begin_array();
    for (size_t i = 0; i < count; i++)
    {
        const SenmlRecord &r = records[i];
        w.begin_object();
        if (r.base_name)
        {
            w.kv_str("bn", r.base_name);
        }
        if (r.has_base_time)
        {
            w.key("bt");
            json_num(w, r.base_time);
        }
        if (r.name)
        {
            w.kv_str("n", r.name);
        }
        if (r.unit)
        {
            w.kv_str("u", r.unit);
        }
        // Every SenmlValueKind enumerator has a case below, so the default edge the compiler
        // emits for the uint8_t-backed enum is unreachable for any value the API admits.
        switch (r.value_kind) // GCOVR_EXCL_LINE  exhaustive enum switch; the default edge is dead
        {
        case SENML_V_FLOAT:
            w.key("v");
            json_num(w, r.value);
            break;
        case SENML_V_STRING:
            if (r.value_str)
            {
                w.kv_str("vs", r.value_str);
            }
            break;
        case SENML_V_BOOL:
            w.kv_bool("vb", r.value_bool);
            break;
        case SENML_V_NONE:
            break;
        }
        if (r.has_time)
        {
            w.key("t");
            json_num(w, r.time);
        }
        w.end_object();
    }
    w.end_array();
    return w.ok() ? w.length() : 0;
}

// Emit a SenML number: an integer when integral (keeps timestamp precision), else a float.
static void codec_num(const pc_codec *c, pc_span *w, double d)
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

static size_t record_fields(const SenmlRecord &r)
{
    size_t n = 0;
    if (r.base_name)
    {
        n++;
    }
    if (r.has_base_time)
    {
        n++;
    }
    if (r.name)
    {
        n++;
    }
    if (r.unit)
    {
        n++;
    }
    if (r.value_kind != SENML_V_NONE && !(r.value_kind == SENML_V_STRING && !r.value_str))
    {
        n++;
    }
    if (r.has_time)
    {
        n++;
    }
    return n;
}

size_t pc_senml_build(const pc_codec *c, uint8_t *buf, size_t cap, const SenmlRecord *records, size_t count)
{
    if (!c || !buf || (count && !records))
    {
        return 0;
    }
    pc_span w{pc_span_from(buf, cap)};
    c->put_array(&w, count);
    for (size_t i = 0; i < count; i++)
    {
        const SenmlRecord &r = records[i];
        c->put_map(&w, record_fields(r));
        if (r.base_name)
        {
            c->put_label(&w, "bn", SENML_LBL_BN);
            c->put_str(&w, r.base_name);
        }
        if (r.has_base_time)
        {
            c->put_label(&w, "bt", SENML_LBL_BT);
            codec_num(c, &w, r.base_time);
        }
        if (r.name)
        {
            c->put_label(&w, "n", SENML_LBL_N);
            c->put_str(&w, r.name);
        }
        if (r.unit)
        {
            c->put_label(&w, "u", SENML_LBL_U);
            c->put_str(&w, r.unit);
        }
        // Exhaustive over SenmlValueKind, as in the JSON builder above: the compiler's default
        // edge for the uint8_t-backed enum is unreachable, and record_fields() above is written
        // against the same four kinds so the declared field count always matches what is emitted.
        switch (r.value_kind) // GCOVR_EXCL_LINE  exhaustive enum switch; the default edge is dead
        {
        case SENML_V_FLOAT:
            c->put_label(&w, "v", SENML_LBL_V);
            codec_num(c, &w, r.value);
            break;
        case SENML_V_STRING:
            if (r.value_str)
            {
                c->put_label(&w, "vs", SENML_LBL_VS);
                c->put_str(&w, r.value_str);
            }
            break;
        case SENML_V_BOOL:
            c->put_label(&w, "vb", SENML_LBL_VB);
            c->put_bool(&w, r.value_bool);
            break;
        case SENML_V_NONE:
            break;
        }
        if (r.has_time)
        {
            c->put_label(&w, "t", SENML_LBL_T);
            codec_num(c, &w, r.time);
        }
    }
    return pc_span_ok(w) ? pc_span_len(w) : 0;
}

// --- resolution (RFC 8428 §4.6) ---

size_t pc_senml_resolve(const SenmlRecord *in, size_t n, SenmlResolved *out, size_t max)
{
    if (!in || !out)
    {
        return 0;
    }
    const char *base_name = NULL; // the active base name (bn), carried forward
    proto_bool base_time_set = PROTO_FALSE;
    double base_time = 0.0; // the active base time (bt)

    size_t count = n < max ? n : max;
    for (size_t i = 0; i < count; i++)
    {
        const SenmlRecord *r = &in[i];
        if (r->base_name) // a base field becomes active for this record and the ones after it
        {
            base_name = r->base_name;
        }
        if (r->has_base_time)
        {
            base_time = r->base_time;
            base_time_set = PROTO_TRUE;
        }

        SenmlResolved *o = &out[i];
        // Resolved name = active base name + record name (either part may be absent).
        pc_sb sb_name = {o->name, sizeof(o->name), 0, PROTO_TRUE};
        pc_sb_put(&sb_name, base_name ? base_name : "");
        pc_sb_put(&sb_name, r->name ? r->name : "");
        int w = (int)pc_sb_finish(&sb_name);
        if (!sb_name.ok)
        {
            o->name[0] = '\0'; // GCOVR_EXCL_LINE  snprintf on "%s%s" cannot encode-error
        }

        // Resolved time = base time + record time (each defaults to 0); absent only if neither is present.
        o->has_time = base_time_set || r->has_time;
        o->time = (base_time_set ? base_time : 0.0) + (r->has_time ? r->time : 0.0);

        o->unit = r->unit;
        o->value_kind = r->value_kind;
        o->value = r->value;
        o->value_str = r->value_str;
        o->value_bool = r->value_bool;
    }
    return count;
}

#endif // PC_ENABLE_SENML
