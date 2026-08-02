// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file openadr.cpp
 * @brief OpenADR 3.0 JSON codec (see openadr.h).
 */

#include "services/energy/openadr/openadr.h"
#include "mmgr/membuild.h" // pc_sb frame builder

#if PC_ENABLE_OPENADR

#include <string.h>

namespace
{
void put_json_str(pc_sb *b, const char *s)
{
    pc_sb_put(b, "\"");
    for (const char *p = s ? s : ""; *p; p++)
    {
        if (*p == '"' || *p == '\\')
        {
            char esc[3] = {'\\', *p, '\0'};
            pc_sb_put(b, esc);
        }
        else if (*p == '\n')
        {
            pc_sb_put(b, "\\n");
        }
        else
        {
            if (b->len + 1 >= b->cap)
            {
                b->ok = false;
                return;
            }
            b->p[b->len++] = *p;
        }
    }
    pc_sb_put(b, "\"");
}

void put_u64(pc_sb *b, uint64_t v)
{
    char tmp[21];
    int n = 0;
    do
    {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v);
    char out[22];
    for (int i = 0; i < n; i++)
    {
        out[i] = tmp[n - 1 - i];
    }
    out[n] = '\0';
    pc_sb_put(b, out);
}

// Format a double with 3 decimal places (no stdlib). Rounds to milli-units; handles the sign.
void put_double(pc_sb *b, double v)
{
    if (v < 0)
    {
        pc_sb_put(b, "-");
        v = -v;
    }
    // scale by 1000 and round.
    uint64_t scaled = (uint64_t)(v * 1000.0 + 0.5);
    uint64_t whole = scaled / 1000;
    uint32_t frac = (uint32_t)(scaled % 1000);
    put_u64(b, whole);
    pc_sb_put(b, ".");
    // three digits, zero-padded.
    char f[4] = {(char)('0' + (frac / 100) % 10), (char)('0' + (frac / 10) % 10), (char)('0' + frac % 10), '\0'};
    pc_sb_put(b, f);
}
} // namespace

size_t pc_openadr_event(const char *program_id, const char *event_name, const OpenAdrInterval *intervals, size_t count,
                        char *out, size_t cap)
{
    if (!out || (count && !intervals))
    {
        return 0;
    }
    pc_sb b = {out, cap, 0, cap > 0};
    pc_sb_put(&b, "{\"objectType\":\"EVENT\",\"programID\":");
    put_json_str(&b, program_id);
    pc_sb_put(&b, ",\"eventName\":");
    put_json_str(&b, event_name);
    pc_sb_put(&b, ",\"intervals\":[");
    for (size_t i = 0; i < count; i++)
    {
        if (i)
        {
            pc_sb_put(&b, ",");
        }
        pc_sb_put(&b, "{\"id\":");
        put_u64(&b, i);
        pc_sb_put(&b, ",\"interval\":{\"start\":");
        put_u64(&b, intervals[i].start);
        pc_sb_put(&b, ",\"duration\":");
        put_u64(&b, intervals[i].duration);
        pc_sb_put(&b, "},\"payloads\":[{\"type\":");
        put_json_str(&b, intervals[i].type);
        pc_sb_put(&b, ",\"values\":[");
        put_double(&b, intervals[i].value);
        pc_sb_put(&b, "]}]}");
    }
    pc_sb_put(&b, "]}");
    return pc_sb_finish(&b);
}

size_t pc_openadr_report(const char *program_id, const char *event_id, const char *resource_name, double value,
                         uint32_t timestamp, char *out, size_t cap)
{
    if (!out)
    {
        return 0;
    }
    pc_sb b2 = {out, cap, 0, cap > 0};
    pc_sb_put(&b2, "{\"objectType\":\"REPORT\",\"programID\":");
    put_json_str(&b2, program_id);
    pc_sb_put(&b2, ",\"eventID\":");
    put_json_str(&b2, event_id);
    pc_sb_put(&b2, ",\"resources\":[{\"resourceName\":");
    put_json_str(&b2, resource_name);
    pc_sb_put(&b2, ",\"intervals\":[{\"interval\":{\"start\":");
    put_u64(&b2, timestamp);
    pc_sb_put(&b2, "},\"payloads\":[{\"type\":\"READING\",\"values\":[");
    put_double(&b2, value);
    pc_sb_put(&b2, "]}]}]}]}");
    return pc_sb_finish(&b2);
}

#endif // PC_ENABLE_OPENADR
