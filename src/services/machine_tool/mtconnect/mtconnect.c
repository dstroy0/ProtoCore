// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mtconnect.c
 * @brief MTConnect agent response codec (see mtconnect.h).
 */

#include "services/machine_tool/mtconnect/mtconnect.h"

#if PC_ENABLE_MTCONNECT

static void put(pc_mtc_streams *s, const char *text)
{
    // null text is a no-op: harden the helper itself, not just every call site.
    if (!s->ok || !text)
    // every call site lives in this file; each passes a string literal, an
    // x ? x : "" ternary, or a local that can only ever hold a literal (wrap,
    // sub, mtc_cat_str()'s return) - never a raw possibly-null variable. Kept
    // as a defensive backstop only.
    {
        return;
    }
    size_t tl = strnlen(text, s->cap + 1);
    if (s->len + tl >= s->cap)
    {
        s->ok = PROTO_FALSE;
        return;
    }
    memcpy(s->buf + s->len, text, tl);
    s->len += tl;
}

static void put_escaped(pc_mtc_streams *s, const char *text)
{
    // null text is a no-op: harden the helper itself, not just every call site.
    if (!s->ok || !text)
    // linkage, so every call site lives in this file; each passes an
    // x ? x : "" ternary, or is reached only inside an "if (x && x[0])" guard
    // (name/units/serialNumber/toolId/deviceUuid/timestamp/limit) that already
    // proved x non-null. Kept as a defensive backstop only.
    {
        return;
    }
    for (const char *p = text; *p; p++)
    {
        const char *rep = NULL;
        switch (*p)
        {
        case '&':
            rep = "&amp;";
            break;
        case '<':
            rep = "&lt;";
            break;
        case '>':
            rep = "&gt;";
            break;
        case '"':
            rep = "&quot;";
            break;
        default:
            break;
        }
        if (rep)
        {
            put(s, rep);
        }
        else
        {
            if (s->len + 1 >= s->cap)
            {
                s->ok = PROTO_FALSE;
                return;
            }
            s->buf[s->len++] = *p;
        }
    }
}

// A minimal unsigned -> decimal directly into the stream.
static void put_u64(pc_mtc_streams *s, uint64_t v)
{
    char tmp[20];
    int n = 0;
    do
    {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v);
    char out[21];
    for (int i = 0; i < n; i++)
    {
        out[i] = tmp[n - 1 - i];
    }
    out[n] = '\0';
    put(s, out);
}

static const char *mtc_cat_str(pc_mtc_category cat)
{
    if (cat == PC_MTC_SAMPLE)
    {
        return "SAMPLE";
    }
    if (cat == PC_MTC_EVENT)
    {
        return "EVENT";
    }
    return "CONDITION";
}

void pc_mtc_streams_begin(pc_mtc_streams *s, char *buf, size_t cap, uint64_t instance_id, uint64_t next_seq,
                          const char *device_name)
{
    s->buf = buf;
    s->cap = cap;
    s->len = 0;
    s->ok = (buf != NULL && cap > 0);
    s->in_comp = PROTO_FALSE;
    put(s, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    put(s, "<MTConnectStreams xmlns=\"urn:mtconnect.org:MTConnectStreams:1.4\">");
    put(s, "<Header instanceId=\"");
    put_u64(s, instance_id);
    put(s, "\" version=\"1.4\" nextSequence=\"");
    put_u64(s, next_seq);
    put(s, "\"/>");
    put(s, "<Streams><DeviceStream name=\"");
    put_escaped(s, device_name ? device_name : "");
    put(s, "\">");
}

void pc_mtc_streams_add(pc_mtc_streams *s, pc_mtc_category cat, const char *type, const char *data_id, uint64_t seq,
                        const char *timestamp, const char *value)
{
    if (!s->ok)
    {
        return;
    }
    if (!s->in_comp)
    {
        put(s, "<ComponentStream component=\"Device\">");
        s->in_comp = PROTO_TRUE;
    }
    const char *wrap = "Condition";
    if (cat == PC_MTC_SAMPLE)
    {
        wrap = "Samples";
    }
    else if (cat == PC_MTC_EVENT)
    {
        wrap = "Events";
    }
    put(s, "<");
    put(s, wrap);
    put(s, ">");
    if (cat == PC_MTC_CONDITION)
    {
        // <Condition><Normal type="TYPE" dataItemId="ID" sequence="SEQ" timestamp="TS"/></Condition>
        const char *sub = value ? value : "Normal"; // Normal / Warning / Fault / Unavailable
        put(s, "<");
        put(s, sub);
        put(s, " type=\"");
        put_escaped(s, type ? type : "");
        put(s, "\" dataItemId=\"");
        put_escaped(s, data_id ? data_id : "");
        put(s, "\" sequence=\"");
        put_u64(s, seq);
        put(s, "\" timestamp=\"");
        put_escaped(s, timestamp ? timestamp : "");
        put(s, "\"/>");
    }
    else
    {
        // <Type dataItemId="ID" sequence="SEQ" timestamp="TS">VALUE</Type>
        put(s, "<");
        put(s, type ? type : "");
        put(s, " dataItemId=\"");
        put_escaped(s, data_id ? data_id : "");
        put(s, "\" sequence=\"");
        put_u64(s, seq);
        put(s, "\" timestamp=\"");
        put_escaped(s, timestamp ? timestamp : "");
        put(s, "\">");
        put_escaped(s, value ? value : "");
        put(s, "</");
        put(s, type ? type : "");
        put(s, ">");
    }
    put(s, "</");
    put(s, wrap);
    put(s, ">");
}

size_t pc_mtc_streams_end(pc_mtc_streams *s)
{
    if (s->in_comp)
    {
        put(s, "</ComponentStream>");
    }
    put(s, "</DeviceStream></Streams></MTConnectStreams>");
    if (!s->ok)
    {
        return 0;
    }
    s->buf[s->len] = '\0';
    return s->len;
}

size_t pc_mtc_error(uint64_t instance_id, const char *error_code, const char *message, char *out, size_t cap)
{
    pc_mtc_streams s;
    s.buf = out;
    s.cap = cap;
    s.len = 0;
    s.ok = (out != NULL && cap > 0);
    s.in_comp = PROTO_FALSE;
    put(&s, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    put(&s, "<MTConnectError xmlns=\"urn:mtconnect.org:MTConnectError:1.4\">");
    put(&s, "<Header instanceId=\"");
    put_u64(&s, instance_id);
    put(&s, "\" version=\"1.4\"/>");
    put(&s, "<Errors><Error errorCode=\"");
    put_escaped(&s, error_code ? error_code : "");
    put(&s, "\">");
    put_escaped(&s, message ? message : "");
    put(&s, "</Error></Errors></MTConnectError>");
    // Null-check `out` directly (not just s.ok, which already implies it) so the NUL terminator
    // write is provably safe to the analyzer as well as at runtime.
    if (!s.ok || out == NULL)
    // encodes out!=nullptr (s.ok assignment above), so when s.ok is
    // true out cannot be null.
    {
        return 0;
    }
    out[s.len] = '\0';
    return s.len;
}

// --- probe (MTConnectDevices): the device model a client discovers before streaming ---

void pc_mtc_devices_begin(pc_mtc_streams *s, char *buf, size_t cap, uint64_t instance_id, const char *device_id,
                          const char *device_name, const char *uuid)
{
    s->buf = buf;
    s->cap = cap;
    s->len = 0;
    s->ok = (buf != NULL && cap > 0);
    s->in_comp = PROTO_FALSE;
    put(s, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    put(s, "<MTConnectDevices xmlns=\"urn:mtconnect.org:MTConnectDevices:1.4\">");
    put(s, "<Header instanceId=\"");
    put_u64(s, instance_id);
    put(s, "\" version=\"1.4\"/>");
    put(s, "<Devices><Device id=\"");
    put_escaped(s, device_id ? device_id : "");
    put(s, "\" name=\"");
    put_escaped(s, device_name ? device_name : "");
    put(s, "\" uuid=\"");
    put_escaped(s, uuid ? uuid : "");
    put(s, "\"><DataItems>");
}

void pc_mtc_devices_add_item(pc_mtc_streams *s, pc_mtc_category cat, const char *id, const char *type, const char *name,
                             const char *units)
{
    if (!s->ok)
    {
        return;
    }
    put(s, "<DataItem category=\"");
    put(s, mtc_cat_str(cat));
    put(s, "\" id=\"");
    put_escaped(s, id ? id : "");
    put(s, "\" type=\"");
    put_escaped(s, type ? type : "");
    put(s, "\"");
    if (name && name[0])
    {
        put(s, " name=\"");
        put_escaped(s, name);
        put(s, "\"");
    }
    if (units && units[0])
    {
        put(s, " units=\"");
        put_escaped(s, units);
        put(s, "\"");
    }
    put(s, "/>");
}

size_t pc_mtc_devices_end(pc_mtc_streams *s)
{
    put(s, "</DataItems></Device></Devices></MTConnectDevices>");
    if (!s->ok)
    {
        return 0;
    }
    s->buf[s->len] = '\0';
    return s->len;
}

// --- asset (MTConnectAssets): the tool/fixture inventory a client reads by GET /asset ---

void pc_mtc_assets_begin(pc_mtc_streams *s, char *buf, size_t cap, uint64_t instance_id, uint32_t asset_count,
                         uint32_t asset_buffer_size)
{
    s->buf = buf;
    s->cap = cap;
    s->len = 0;
    s->ok = (buf != NULL && cap > 0);
    s->in_comp = PROTO_FALSE;
    put(s, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    put(s, "<MTConnectAssets xmlns=\"urn:mtconnect.org:MTConnectAssets:1.4\">");
    put(s, "<Header instanceId=\"");
    put_u64(s, instance_id);
    put(s, "\" version=\"1.4\" assetBufferSize=\"");
    put_u64(s, asset_buffer_size);
    put(s, "\" assetCount=\"");
    put_u64(s, asset_count);
    put(s, "\"/>");
    put(s, "<Assets>");
}

void pc_mtc_assets_cutting_tool_begin(pc_mtc_streams *s, const char *asset_id, const char *serial_number,
                                      const char *tool_id, const char *device_uuid, const char *timestamp)
{
    put(s, "<CuttingTool assetId=\"");
    put_escaped(s, asset_id ? asset_id : "");
    put(s, "\"");
    if (serial_number && serial_number[0])
    {
        put(s, " serialNumber=\"");
        put_escaped(s, serial_number);
        put(s, "\"");
    }
    if (tool_id && tool_id[0])
    {
        put(s, " toolId=\"");
        put_escaped(s, tool_id);
        put(s, "\"");
    }
    if (device_uuid && device_uuid[0])
    {
        put(s, " deviceUuid=\"");
        put_escaped(s, device_uuid);
        put(s, "\"");
    }
    if (timestamp && timestamp[0])
    {
        put(s, " timestamp=\"");
        put_escaped(s, timestamp);
        put(s, "\"");
    }
    put(s, "><CuttingToolLifeCycle>");
}

void pc_mtc_assets_tool_life(pc_mtc_streams *s, const char *type, const char *count_direction, const char *limit,
                             const char *value)
{
    // <ToolLife type="MINUTES" countDirection="UP" limit="100">42</ToolLife>
    put(s, "<ToolLife type=\"");
    put_escaped(s, type ? type : "");
    put(s, "\" countDirection=\"");
    put_escaped(s, count_direction ? count_direction : "");
    put(s, "\"");
    if (limit && limit[0])
    {
        put(s, " limit=\"");
        put_escaped(s, limit);
        put(s, "\"");
    }
    put(s, ">");
    put_escaped(s, value ? value : "");
    put(s, "</ToolLife>");
}

void pc_mtc_assets_cutting_tool_end(pc_mtc_streams *s)
{
    put(s, "</CuttingToolLifeCycle></CuttingTool>");
}

size_t pc_mtc_assets_end(pc_mtc_streams *s)
{
    put(s, "</Assets></MTConnectAssets>");
    if (!s->ok)
    {
        return 0;
    }
    s->buf[s->len] = '\0';
    return s->len;
}

// --- sample sequence cursor: a rolling observation buffer for the `sample` from/count long-poll ---

// Bounded, always-NUL-terminated copy into a fixed field.
static void mtc_copy_str(char *dst, size_t cap, const char *src)
{
    // cap == 0 is unreachable: every call site passes sizeof() of a fixed pc_mtc_observation field
    // (PC_MTC_STR_MAX/TS_MAX/PC_VAL_MAX + 1, all compile-time non-zero constants in protocore_config.h),
    // never a literal 0. Kept as a defensive backstop against a future zero-sized field.
    if (cap == 0)
    {
        return;
    }
    size_t i = 0;
    if (src)
    {
        for (; i < cap - 1 && src[i] != '\0'; i++) // range check first (short-circuits the src read)
        {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

// Open an MTConnectStreams document with the full sample-cursor header (buffer/first/last/next seq).
static void mtc_streams_begin_windowed(pc_mtc_streams *s, char *buf, size_t cap, uint64_t instance_id,
                                       uint64_t first_seq, uint64_t last_seq, uint64_t next_seq, uint32_t buffer_size,
                                       const char *device_name)
{
    s->buf = buf;
    s->cap = cap;
    s->len = 0;
    s->ok = (buf != NULL && cap > 0);
    s->in_comp = PROTO_FALSE;
    put(s, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    put(s, "<MTConnectStreams xmlns=\"urn:mtconnect.org:MTConnectStreams:1.4\">");
    put(s, "<Header instanceId=\"");
    put_u64(s, instance_id);
    put(s, "\" version=\"1.4\" bufferSize=\"");
    put_u64(s, buffer_size);
    put(s, "\" firstSequence=\"");
    put_u64(s, first_seq);
    put(s, "\" lastSequence=\"");
    put_u64(s, last_seq);
    put(s, "\" nextSequence=\"");
    put_u64(s, next_seq);
    put(s, "\"/>");
    put(s, "<Streams><DeviceStream name=\"");
    put_escaped(s, device_name ? device_name : "");
    put(s, "\">");
}

void pc_mtc_sample_buffer_init(pc_mtc_sample_buffer *b, uint64_t start_seq)
{
    b->count = 0;
    b->head = 0;
    b->next_seq = start_seq ? start_seq : 1;
    b->first_seq = b->next_seq; // empty: first == next (lastSequence = next-1 sits just below first)
}

uint64_t pc_mtc_sample_buffer_add(pc_mtc_sample_buffer *b, pc_mtc_category cat, const char *type, const char *data_id,
                                  const char *timestamp, const char *value)
{
    pc_mtc_observation *o = &b->obs[b->head];
    o->cat = cat;
    o->seq = b->next_seq;
    mtc_copy_str(o->type, sizeof(o->type), type);
    mtc_copy_str(o->data_id, sizeof(o->data_id), data_id);
    mtc_copy_str(o->timestamp, sizeof(o->timestamp), timestamp);
    mtc_copy_str(o->value, sizeof(o->value), value);
    b->head = (b->head + 1) % PC_MTC_SAMPLE_BUFFER;
    if (b->count < PC_MTC_SAMPLE_BUFFER)
    {
        b->count++;
    }
    else
    {
        b->first_seq++; // ring full: the oldest was overwritten, so the window slides forward
    }
    return b->next_seq++;
}

size_t pc_mtc_sample_query(const pc_mtc_sample_buffer *b, char *buf, size_t cap, uint64_t instance_id,
                           const char *device_name, uint64_t from, uint32_t count)
{
    uint64_t first = b->first_seq;
    uint64_t next = b->next_seq;                  // one past the newest retained observation
    uint64_t last = next - 1;                     // newest sequence (next-1); when empty this is first-1
    uint64_t start = from < first ? first : from; // a stale `from` catches up from the oldest kept

    uint32_t avail = (start < next) ? (uint32_t)(next - start) : 0;
    uint32_t to_emit = (count < avail) ? count : avail;
    // Resume point: past the last one returned, or the buffer's nextSequence when nothing was in range.
    uint64_t next_report = (start >= next) ? next : start + to_emit;

    pc_mtc_streams s;
    mtc_streams_begin_windowed(&s, buf, cap, instance_id, first, last, next_report, PC_MTC_SAMPLE_BUFFER, device_name);

    // The oldest retained observation sits `count` slots behind head; observation `first + k` is at
    // (oldest_idx + k) around the ring.
    uint32_t oldest_idx = (b->head + PC_MTC_SAMPLE_BUFFER - b->count) % PC_MTC_SAMPLE_BUFFER;
    for (uint32_t e = 0; e < to_emit; e++)
    {
        uint32_t k = (uint32_t)((start - first) + e);
        const pc_mtc_observation *o = &b->obs[(oldest_idx + k) % PC_MTC_SAMPLE_BUFFER];
        pc_mtc_streams_add(&s, o->cat, o->type, o->data_id, o->seq, o->timestamp, o->value);
    }
    return pc_mtc_streams_end(&s);
}

#endif // PC_ENABLE_MTCONNECT
