// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file atc.c
 * @brief ATC field-I/O interop snapshot (see atc.h).
 */

#include "services/machine_tool/atc/atc.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_ATC

static void put_json_str(pc_sb *b, const char *s)
{
    pc_sb_put(b, "\"");
    for (const char *p = s ? s : ""; *p; p++)
    {
        if (*p == '"' || *p == '\\')
        {
            char esc[3] = {'\\', *p, '\0'};
            pc_sb_put(b, esc);
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
    pc_sb_put(b, "\"");
}

static void put_u8(pc_sb *b, uint8_t v)
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
    pc_sb_put(b, o);
}

// Append the points of one direction (outputs or inputs) as a JSON array.
static void put_array(pc_sb *b, const AtcFieldIo *io, proto_bool outputs)
{
    pc_sb_put(b, "[");
    proto_bool first = PROTO_TRUE;
    for (size_t i = 0; i < io->count; i++)
    {
        if (io->points[i].is_output != outputs)
        {
            continue;
        }
        if (!first)
        {
            pc_sb_put(b, ",");
        }
        first = PROTO_FALSE;
        pc_sb_put(b, "{\"name\":");
        put_json_str(b, io->points[i].name);
        pc_sb_put(b, ",\"value\":");
        put_u8(b, io->points[i].value);
        pc_sb_put(b, "}");
    }
    pc_sb_put(b, "]");
}

size_t pc_atc_snapshot_json(const AtcFieldIo *io, char *out, size_t cap)
{
    if (!io || !out || (io->count && !io->points))
    {
        return 0;
    }
    pc_sb b = {out, cap, 0, cap > 0};
    pc_sb_put(&b, "{\"inputs\":");
    put_array(&b, io, PROTO_FALSE);
    pc_sb_put(&b, ",\"outputs\":");
    put_array(&b, io, PROTO_TRUE);
    pc_sb_put(&b, "}");
    if (!b.ok)
    {
        return 0;
    }
    out[b.len] = '\0';
    return b.len;
}

proto_bool pc_atc_set_output(AtcFieldIo *io, const char *name, uint8_t value)
{
    if (!io || !name || !io->points)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < io->count; i++)
    {
        if (io->points[i].is_output && io->points[i].name && strcmp(io->points[i].name, name) == 0)
        {
            io->points[i].value = value;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

uint8_t pc_atc_get(const AtcFieldIo *io, const char *name, proto_bool *found)
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
        if (io->points[i].name && strcmp(io->points[i].name, name) == 0)
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

#endif // PC_ENABLE_ATC
