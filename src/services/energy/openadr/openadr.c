// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file openadr.c
 * @brief OpenADR 3.0 JSON codec (see openadr.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_OPENADR

#include "mmgr/membuild/membuild.h" // protocore_sb frame builder
#include "services/energy/openadr/openadr.h"

static void put_u64(protocore_sb *b, uint64_t v)
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
    Sb.put(b, out);
}

// Format a double with 3 decimal places (no stdlib). Rounds to milli-units; handles the sign.
static void put_double(protocore_sb *b, double v)
{
    if (v < 0)
    {
        Sb.put(b, "-");
        v = -v;
    }
    // scale by 1000 and round.
    uint64_t scaled = (uint64_t)(v * 1000.0 + 0.5);
    uint64_t whole = scaled / 1000;
    uint32_t frac = (uint32_t)(scaled % 1000);
    put_u64(b, whole);
    Sb.put(b, ".");
    // three digits, zero-padded.
    char f[4] = {(char)('0' + (frac / 100) % 10), (char)('0' + (frac / 10) % 10), (char)('0' + frac % 10), '\0'};
    Sb.put(b, f);
}

size_t protocore_openadr_event(const char *program_id, const char *event_name, const OpenAdrInterval *intervals,
                               size_t count, char *out, size_t cap)
{
    if (!out || (count && !intervals))
    {
        return 0;
    }
    protocore_sb b = {out, cap, 0, cap > 0};
    Sb.put(&b, "{\"objectType\":\"EVENT\",\"programID\":");
    Sb.json(&b, program_id);
    Sb.put(&b, ",\"eventName\":");
    Sb.json(&b, event_name);
    Sb.put(&b, ",\"intervals\":[");
    for (size_t i = 0; i < count; i++)
    {
        if (i)
        {
            Sb.put(&b, ",");
        }
        Sb.put(&b, "{\"id\":");
        put_u64(&b, i);
        Sb.put(&b, ",\"interval\":{\"start\":");
        put_u64(&b, intervals[i].start);
        Sb.put(&b, ",\"duration\":");
        put_u64(&b, intervals[i].duration);
        Sb.put(&b, "},\"payloads\":[{\"type\":");
        Sb.json(&b, intervals[i].type);
        Sb.put(&b, ",\"values\":[");
        put_double(&b, intervals[i].value);
        Sb.put(&b, "]}]}");
    }
    Sb.put(&b, "]}");
    return Sb.finish(&b);
}

size_t protocore_openadr_report(const char *program_id, const char *event_id, const char *resource_name, double value,
                                uint32_t timestamp, char *out, size_t cap)
{
    if (!out)
    {
        return 0;
    }
    protocore_sb b2 = {out, cap, 0, cap > 0};
    Sb.put(&b2, "{\"objectType\":\"REPORT\",\"programID\":");
    Sb.json(&b2, program_id);
    Sb.put(&b2, ",\"eventID\":");
    Sb.json(&b2, event_id);
    Sb.put(&b2, ",\"resources\":[{\"resourceName\":");
    Sb.json(&b2, resource_name);
    Sb.put(&b2, ",\"intervals\":[{\"interval\":{\"start\":");
    put_u64(&b2, timestamp);
    Sb.put(&b2, "},\"payloads\":[{\"type\":\"READING\",\"values\":[");
    put_double(&b2, value);
    Sb.put(&b2, "]}]}]}]}");
    return Sb.finish(&b2);
}

#endif // PROTOCORE_ENABLE_OPENADR
