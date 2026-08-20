// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file atc.c
 * @brief ATC field-I/O interop snapshot (see atc.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_ATC

#include "mmgr/membuild/membuild.h" // protocore_sb frame builder
#include "mmgr/protostr/protostr.h" // str.eq: the FIO point name lookup
#include "services/machine_tool/atc/atc.h"

static void put_json_str(protocore_sb *b, const char *s)
{
    Sb.put(b, "\"");
    for (const char *p = s ? s : ""; *p; p++)
    {
        if (*p == '"' || *p == '\\')
        {
            char esc[3] = {'\\', *p, '\0'};
            Sb.put(b, esc);
        }
        else
        {
            if (b->len + 1 >= b->cap)
            {
                b->ok = PROTO_FALSE;
                return;
            }
            b->p[b->len++] = *p;
        }
    }
    Sb.put(b, "\"");
}

static void put_u8(protocore_sb *b, uint8_t v)
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
    Sb.put(b, o);
}

// Append the points of one direction (outputs or inputs) as a JSON array.
static void put_array(protocore_sb *b, const AtcFieldIo *io, proto_bool outputs)
{
    Sb.put(b, "[");
    proto_bool first = PROTO_TRUE;
    for (size_t i = 0; i < io->count; i++)
    {
        if (io->points[i].is_output != outputs)
        {
            continue;
        }
        if (!first)
        {
            Sb.put(b, ",");
        }
        first = PROTO_FALSE;
        Sb.put(b, "{\"name\":");
        put_json_str(b, io->points[i].name);
        Sb.put(b, ",\"value\":");
        put_u8(b, io->points[i].value);
        Sb.put(b, "}");
    }
    Sb.put(b, "]");
}

size_t protocore_atc_snapshot_json(const AtcFieldIo *io, char *out, size_t cap)
{
    if (!io || !out || (io->count && !io->points))
    {
        return 0;
    }
    protocore_sb b = {out, cap, 0, cap > 0};
    Sb.put(&b, "{\"inputs\":");
    put_array(&b, io, PROTO_FALSE);
    Sb.put(&b, ",\"outputs\":");
    put_array(&b, io, PROTO_TRUE);
    Sb.put(&b, "}");
    if (!b.ok)
    {
        return 0;
    }
    out[b.len] = '\0';
    return b.len;
}

proto_bool protocore_atc_set_output(AtcFieldIo *io, const char *name, uint8_t value)
{
    if (!io || !name || !io->points)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < io->count; i++)
    {
        if (io->points[i].is_output && io->points[i].name && str.eq(io->points[i].name, name, MAX_KEY_LEN, PROTO_FALSE))
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
        if (io->points[i].name && str.eq(io->points[i].name, name, MAX_KEY_LEN, PROTO_FALSE))
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

#endif // PROTOCORE_ENABLE_ATC
