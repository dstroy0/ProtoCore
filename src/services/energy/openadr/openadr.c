// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file openadr.c
 * @brief OpenADR 3.0 JSON codec (see openadr.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_OPENADR

#include "services/energy/openadr/openadr.h"
#include "verba_scribo/verba_scribo.h" // verba_*: the text and number writers the builders chain

PROTOCORE_BEGIN_DECLS

static size_t put_u64(char *out, size_t cap, size_t at, uint64_t v)
{
    char tmp[21];
    int n = 0;
    do
    {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v);
    char digits[22];
    for (int i = 0; i < n; i++)
    {
        digits[i] = tmp[n - 1 - i];
    }
    digits[n] = '\0';
    return EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = digits);
}

// Format a double with 3 decimal places (no stdlib). Rounds to milli-units; handles the sign.
static size_t put_double(char *out, size_t cap, size_t at, double v)
{
    if (v < 0)
    {
        at = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = "-");
        v = -v;
    }
    // scale by 1000 and round.
    uint64_t scaled = (uint64_t)(v * 1000.0 + 0.5);
    uint64_t whole = scaled / 1000;
    uint32_t frac = (uint32_t)(scaled % 1000);
    at = put_u64(out, cap, at, whole);
    at = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = ".");
    // three digits, zero-padded.
    char f[4] = {(char)('0' + (frac / 100) % 10), (char)('0' + (frac / 10) % 10), (char)('0' + frac % 10), '\0'};
    return EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = at, .text = f);
}

size_t protocore_openadr_event(const char *program_id, const char *event_name, const OpenAdrInterval *intervals,
                               size_t count, char *out, size_t cap)
{
    if (!out || (count && !intervals))
    {
        return 0;
    }
    size_t b = 0;
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b,
                   .text = "{\"objectType\":\"EVENT\",\"programID\":");
    b = EMBED_CALL(verba_textus.json, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = program_id);
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = ",\"eventName\":");
    b = EMBED_CALL(verba_textus.json, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = event_name);
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = ",\"intervals\":[");
    for (size_t i = 0; i < count; i++)
    {
        if (i)
        {
            b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = ",");
        }
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "{\"id\":");
        b = put_u64(out, cap, b, i);
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b,
                       .text = ",\"interval\":{\"start\":");
        b = put_u64(out, cap, b, intervals[i].start);
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = ",\"duration\":");
        b = put_u64(out, cap, b, intervals[i].duration);
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b,
                       .text = "},\"payloads\":[{\"type\":");
        b = EMBED_CALL(verba_textus.json, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = intervals[i].type);
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = ",\"values\":[");
        b = put_double(out, cap, b, intervals[i].value);
        b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "]}]}");
    }
    b = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b, .text = "]}");
    return EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = out, .cap = cap, .at = b);
}

size_t protocore_openadr_report(const char *program_id, const char *event_id, const char *resource_name, double value,
                                uint32_t timestamp, char *out, size_t cap)
{
    if (!out)
    {
        return 0;
    }
    size_t b2 = 0;
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2,
                    .text = "{\"objectType\":\"REPORT\",\"programID\":");
    b2 = EMBED_CALL(verba_textus.json, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = program_id);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = ",\"eventID\":");
    b2 = EMBED_CALL(verba_textus.json, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = event_id);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2,
                    .text = ",\"resources\":[{\"resourceName\":");
    b2 = EMBED_CALL(verba_textus.json, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = resource_name);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2,
                    .text = ",\"intervals\":[{\"interval\":{\"start\":");
    b2 = put_u64(out, cap, b2, timestamp);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2,
                    .text = "},\"payloads\":[{\"type\":\"READING\",\"values\":[");
    b2 = put_double(out, cap, b2, value);
    b2 = EMBED_CALL(verba_textus.put, VerbaTextusCfg, .out = out, .cap = cap, .at = b2, .text = "]}]}]}]}");
    return EMBED_CALL(verba_finis.finish, VerbaFinisCfg, .out = out, .cap = cap, .at = b2);
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_OPENADR
