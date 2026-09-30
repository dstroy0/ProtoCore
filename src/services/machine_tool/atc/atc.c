// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file atc.c
 * @brief ATC field-I/O interop snapshot (see atc.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_ATC

#include "cellularum_laboro/cellularum_laboro.h" // cellul.eq: the FIO point name lookup
#include "services/machine_tool/atc/atc.h"
#include "verba_scribo/verba_scribo.h" // verba_*: the text and number writers the builders chain

PROTOCORE_BEGIN_DECLS

static size_t put_json_str(char *out, size_t cap, size_t at, const char *s)
{
    at = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = "\"");
    for (const char *p = s ? s : ""; *p; p++)
    {
        if (*p == '"' || *p == '\\')
        {
            char esc[3] = {'\\', *p, '\0'};
            at = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = esc);
        }
        else
        {
            at = EMBED_CALL(verba_littera.ch, VerbaLitteraCfg, .out = out, .cap = cap, .at = at, .ch = *p);
        }
    }
    return EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = "\"");
}

static size_t put_u8(char *out, size_t cap, size_t at, uint8_t v)
{
    char t[4];
    int n = 0;
    do
    {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    char o[4];
    for (int i = 0; i < n; i++)
    {
        o[i] = t[n - 1 - i];
    }
    o[n] = '\0';
    return EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = o);
}

// Append the points of one direction (outputs or inputs) as a JSON array.
static size_t put_array(char *out, size_t cap, size_t at, const AtcFieldIo *io, proto_bool outputs)
{
    at = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = "[");
    proto_bool first = PROTO_TRUE;
    for (size_t i = 0; i < io->count; i++)
    {
        if (io->points[i].is_output != outputs)
        {
            continue;
        }
        if (!first)
        {
            at = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = ",");
        }
        first = PROTO_FALSE;
        at = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = "{\"name\":");
        at = put_json_str(out, cap, at, io->points[i].name);
        at = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = ",\"value\":");
        at = put_u8(out, cap, at, io->points[i].value);
        at = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = "}");
    }
    return EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = "]");
}

size_t protocore_atc_snapshot_json(const AtcFieldIo *io, char *out, size_t cap)
{
    if (!io || !out || (io->count && !io->points))
    {
        return 0;
    }
    size_t b = 0;
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "{\"inputs\":");
    b = put_array(out, cap, b, io, PROTO_FALSE);
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = ",\"outputs\":");
    b = put_array(out, cap, b, io, PROTO_TRUE);
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "}");
    if (!EMBED_CALL(verba_finis.ok, VerbaFinisCfg, .cap = cap, .at = b))
    {
        return 0;
    }
    out[b] = '\0';
    return b;
}

proto_bool protocore_atc_set_output(AtcFieldIo *io, const char *name, uint8_t value)
{
    if (!io || !name || !io->points)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < io->count; i++)
    {
        if (io->points[i].is_output && io->points[i].name &&
            EMBED_CALL(cellul.eq, CatenaFinitaCfg, .src = io->points[i].name, .other = name, .cap = MAX_KEY_LEN,
                       .ci = PROTO_FALSE))
        {
            io->points[i].value = value;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

uint8_t protocore_atc_get(const AtcFieldIo *io, const char *name, proto_bool *found)
{
    if (found)
    {
        *found = PROTO_FALSE;
    }
    if (!io || !name || !io->points)
    {
        return 0;
    }
    for (size_t i = 0; i < io->count; i++)
    {
        if (io->points[i].name && EMBED_CALL(cellul.eq, CatenaFinitaCfg, .src = io->points[i].name, .other = name,
                                             .cap = MAX_KEY_LEN, .ci = PROTO_FALSE))
        {
            if (found)
            {
                *found = PROTO_TRUE;
            }
            return io->points[i].value;
        }
    }
    return 0;
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ATC
