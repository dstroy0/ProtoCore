// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mtconnect.c
 * @brief MTConnect agent response codec (see mtconnect.h).
 *
 * Serial framing. A document is written straight into the caller's buffer as the calls arrive; the
 * only thing kept across a call is where the next byte of each open run goes.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_MTCONNECT

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"
#include "server/clock/clock.h" // the always-on system clock behind creationTime
#include "services/machine_tool/mtconnect/mtconnect.h"
#include "shared/time_compat/time_compat.h" // the UTC breakdown creationTime is formatted from

PROTOCORE_BEGIN_DECLS

static uint8_t time_compat_work[16]; // the borrow an entry takes; TimeCompat never reads it

/** @brief One buffered observation (a value at a sequence number), stored in fixed fields. */
typedef struct
{
    uint64_t seq;                         ///< the sequence assigned when it was recorded
    char type[PROTOCORE_MTC_STR_MAX + 1]; ///< DataItem type element name
    char data_id[PROTOCORE_MTC_STR_MAX + 1];
    char timestamp[PROTOCORE_MTC_TS_MAX + 1];
    char value[PROTOCORE_MTC_VAL_MAX + 1];
    uint8_t cat; ///< its ::protocore_mtc_category
} MtConnectObs;

/**
 * @brief What survives one call: where the document has got to, and where each open run ends.
 *
 * A ComponentStream is an xs:sequence of Samples, Events and Condition, each maxOccurs="1", and the
 * observations arrive in whatever order the caller has them. So the three runs sit end to end inside
 * the component and each new observation is written at the end of its own run - three stamps, moved
 * on past. The container tags go round the non-empty runs when the component closes.
 */
typedef struct
{
    char *out; ///< the caller's buffer, seated when the document opened
    size_t cap;
    size_t len;        ///< bytes of the document written so far
    size_t comp_start; ///< where the open component's runs begin
    size_t run[3];     ///< one past the last byte of the Samples, Events and Condition runs
    proto_bool ok;
    proto_bool in_comp;

    uint32_t count;     ///< observations the ring holds (<= PROTOCORE_MTC_SAMPLE_BUFFER)
    uint32_t head;      ///< ring write index
    uint64_t next_seq;  ///< the sequence the next add will assign
    uint64_t first_seq; ///< the sequence of the oldest retained observation
} MtConnectCtx;

// The caller's borrow, split: the running context, then the observation ring the sample replay reads.
#define MTC_OFF_CTX 0u
#define MTC_OFF_RING (MTC_OFF_CTX + sizeof(MtConnectCtx))
static_assert(MTC_OFF_RING + (size_t)PROTOCORE_MTC_SAMPLE_BUFFER * sizeof(MtConnectObs) <= PROTOCORE_MTCONNECT_BORROW,
              "PROTOCORE_MTCONNECT_BORROW is short of the context and the observation ring - raise it in "
              "protocore_config.h, which sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(MTC_OFF_CTX % _Alignof(MtConnectCtx) == 0,
              "MTC_OFF_CTX is not a multiple of alignof(MtConnectCtx) - MTC_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");
static_assert(MTC_OFF_RING % _Alignof(MtConnectObs) == 0,
              "MTC_OFF_RING is not a multiple of alignof(MtConnectObs) - MTC_RING() would return a misaligned "
              "pointer; pad the region ahead of it");

// The regions, at their offsets in the caller's borrow.
#define MTC_CTX(w) ((MtConnectCtx *)(void *)((w) + MTC_OFF_CTX))
#define MTC_RING(w) ((MtConnectObs *)(void *)((w) + MTC_OFF_RING))

// The one owned instance, private to this TU: the pointer to the bytes this module took for itself.
static uint8_t *s_span;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_mtconnect_span(void)
{
    if (s_span == NULL)
    {
        s_span = protocore_plaintext_persist_span(PROTOCORE_MTCONNECT_BORROW).buf;
    }
    return s_span;
}

// --- the text the document is made of --------------------------------------

// Append @p text at the end of the document.
static void put(uint8_t *restrict work, const char *text)
{
    MtConnectCtx *c = MTC_CTX(work);
    if (!c->ok || !text)
    {
        return;
    }
    size_t tl = str.len(text, c->cap + 1);
    if (c->len + tl >= c->cap)
    {
        c->ok = PROTO_FALSE;
        return;
    }
    mem.cpy(c->out + c->len, text, tl);
    c->len += tl;
}

// Append @p text with the five XML metacharacters replaced by their entities (XML 1.0 sec 2.4).
static void put_escaped(uint8_t *restrict work, const char *text)
{
    MtConnectCtx *c = MTC_CTX(work);
    if (!c->ok || !text)
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
            put(work, rep);
            continue;
        }
        if (c->len + 1 >= c->cap)
        {
            c->ok = PROTO_FALSE;
            return;
        }
        c->out[c->len++] = *p;
    }
}

// A minimal unsigned -> decimal directly into the document.
static void put_u64(uint8_t *restrict work, uint64_t v)
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
    put(work, out);
}

// Open a run of @p n bytes at @p at, moving what follows out of the way. The runs after it slide by
// the same amount, which is what keeps the three stamps pointing at their own ends.
static char *open_gap(uint8_t *restrict work, size_t at, size_t n)
{
    MtConnectCtx *c = MTC_CTX(work);
    if (!c->ok)
    {
        return NULL;
    }
    if (c->len + n >= c->cap)
    {
        c->ok = PROTO_FALSE;
        return NULL;
    }
    mem.move(c->out + at + n, c->out + at, c->len - at);
    c->len += n;
    return c->out + at;
}

// Put the literal @p text at @p at rather than at the end.
static void put_at(uint8_t *restrict work, size_t at, const char *text)
{
    size_t n = str.len(text, MTC_CTX(work)->cap + 1);
    char *gap = open_gap(work, at, n);
    if (gap)
    {
        mem.cpy(gap, text, n);
    }
}

// Reverse @p n bytes in place - the half of a rotation that needs no buffer.
static void reverse(char *p, size_t n)
{
    for (size_t i = 0; i + 1 < n; i++, n--)
    {
        const char t = p[i];
        p[i] = p[n - 1];
        p[n - 1] = t;
    }
}

static const char *mtc_cat_str(protocore_mtc_category cat)
{
    if (cat == PROTOCORE_MTC_SAMPLE)
    {
        return "SAMPLE";
    }
    if (cat == PROTOCORE_MTC_EVENT)
    {
        return "EVENT";
    }
    return "CONDITION";
}

// --- the Header every document opens with ----------------------------------

// creationTime as xs:dateTime. The system clock is always running, so the instant is always there;
// it counts from the epoch until a wall clock is set, which is what an agent with no time source has.
static void put_creation_time(uint8_t *restrict work)
{
    Clock.millis(Clock.internal);
    struct tm tmv;
    TimeCompat.args.epoch = (time_t)(Clock.ms / 1000u);
    TimeCompat.args.out = &tmv;
    TimeCompat.gmtime(time_compat_work);
    put(work, "\" creationTime=\"");
    if (TimeCompat.tm_out == NULL)
    {
        put(work, "1970-01-01T00:00:00Z");
        return;
    }
    const uint32_t f[6] = {(uint32_t)(tmv.tm_year + 1900), (uint32_t)(tmv.tm_mon + 1), (uint32_t)tmv.tm_mday,
                           (uint32_t)tmv.tm_hour,          (uint32_t)tmv.tm_min,       (uint32_t)tmv.tm_sec};
    const uint8_t width[6] = {4, 2, 2, 2, 2, 2};
    const char sep[6] = {'-', '-', 'T', ':', ':', 'Z'};
    char ts[21];
    size_t k = 0;
    for (int i = 0; i < 6; i++)
    {
        uint32_t v = f[i];
        for (int d = width[i] - 1; d >= 0; d--)
        {
            ts[k + (size_t)d] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        k += width[i];
        ts[k++] = sep[i];
    }
    ts[k] = '\0';
    put(work, ts);
}

// version, instanceId, creationTime and sender are required of every 1.4 Header, so they are written
// from one place and a document cannot be short of one.
static void put_header_common(uint8_t *restrict work)
{
    put(work, "<Header instanceId=\"");
    put_u64(work, MtConnect.doc.instance_id);
    put(work, "\" version=\"1.4");
    put_creation_time(work);
    put(work, "\" sender=\"");
    put_escaped(work, MtConnect.doc.sender);
}

// The retained observation window the stream and error Headers carry.
static void put_window(uint8_t *restrict work, uint64_t first, uint64_t last, uint32_t size)
{
    put(work, "\" bufferSize=\"");
    put_u64(work, (uint64_t)size);
    put(work, "\" firstSequence=\"");
    put_u64(work, first);
    put(work, "\" lastSequence=\"");
    put_u64(work, last);
}

// Seat the caller's buffer and open the XML declaration and root element.
static void doc_open(uint8_t *restrict work, const char *root)
{
    MtConnectCtx *c = MTC_CTX(work);
    c->out = MtConnect.doc.out;
    c->cap = MtConnect.doc.cap;
    c->len = 0;
    c->ok = (c->out != NULL && c->cap > 0);
    c->in_comp = PROTO_FALSE;
    put(work, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    put(work, "<");
    put(work, root);
    put(work, " xmlns=\"urn:mtconnect.org:");
    put(work, root);
    put(work, ":1.4\">");
}

// Close the root element, terminate, and report the length.
static void doc_close(uint8_t *restrict work, const char *tail)
{
    MtConnectCtx *c = MTC_CTX(work);
    put(work, tail);
    MtConnect.ok = c->ok;
    if (!c->ok)
    {
        MtConnect.n = 0;
        return;
    }
    c->out[c->len] = '\0';
    MtConnect.n = c->len;
}

// --- streams (current / sample) --------------------------------------------

// Close the open component: the three runs sit end to end already, so the container tags go round
// the non-empty ones, back to front so the stamps in front of each insertion stay put.
static void close_component(uint8_t *restrict work)
{
    MtConnectCtx *c = MTC_CTX(work);
    if (!c->in_comp)
    {
        return;
    }
    c->in_comp = PROTO_FALSE;
    static const char *const OPEN[3] = {"<Samples>", "<Events>", "<Condition>"};
    static const char *const CLOSE[3] = {"</Samples>", "</Events>", "</Condition>"};
    for (int i = 2; i >= 0; i--)
    {
        const size_t start = (i == 0) ? c->comp_start : c->run[i - 1];
        if (c->run[i] == start)
        {
            continue; // no observation of that category: maxOccurs=1 does not mean minOccurs=1
        }
        put_at(work, c->run[i], CLOSE[i]);
        put_at(work, start, OPEN[i]);
    }
    put(work, "</ComponentStream>");
}

static void streams_begin(uint8_t *restrict work)
{
    doc_open(work, "MTConnectStreams");
    put_header_common(work);
    put(work, "\" nextSequence=\"");
    put_u64(work, MtConnect.streams.next_seq);
    put_window(work, MtConnect.window.first_seq, MtConnect.window.last_seq,
               MtConnect.window.buffer_size ? MtConnect.window.buffer_size : PROTOCORE_MTC_SAMPLE_BUFFER);
    put(work, "\"/>");
    // DeviceStreamType marks name AND uuid required; ComponentStreamType marks component AND
    // componentId. Both are the caller's - they are what the probe response published.
    put(work, "<Streams><DeviceStream name=\"");
    put_escaped(work, MtConnect.streams.device_name);
    put(work, "\" uuid=\"");
    put_escaped(work, MtConnect.streams.device_uuid);
    put(work, "\">");
    MtConnect.ok = MTC_CTX(work)->ok;
}

static void streams_add(uint8_t *restrict work)
{
    MtConnectCtx *c = MTC_CTX(work);
    if (!c->ok)
    {
        return;
    }
    if (!c->in_comp)
    {
        put(work, "<ComponentStream component=\"");
        put_escaped(work, MtConnect.streams.component ? MtConnect.streams.component : "Device");
        put(work, "\" componentId=\"");
        put_escaped(work, MtConnect.streams.component_id);
        put(work, "\">");
        c->in_comp = PROTO_TRUE;
        c->comp_start = c->len;
        c->run[0] = c->len;
        c->run[1] = c->len;
        c->run[2] = c->len;
    }

    // Written at the end of its own run, so the three stay in the order the sequence states. The
    // element is composed at the document end and then moved into place, which keeps one code path
    // for the text and one memmove for the placement.
    const size_t tail = c->len;
    const protocore_mtc_category cat = MtConnect.obs.cat;
    if (cat == PROTOCORE_MTC_CONDITION)
    {
        // <Condition><Normal type="TYPE" dataItemId="ID" sequence="SEQ" timestamp="TS"/></Condition>
        put(work, "<");
        put(work, MtConnect.obs.value ? MtConnect.obs.value : "Normal");
        put(work, " type=\"");
        put_escaped(work, MtConnect.obs.type);
        put(work, "\" dataItemId=\"");
        put_escaped(work, MtConnect.obs.data_id);
        put(work, "\" sequence=\"");
        put_u64(work, MtConnect.obs.seq);
        put(work, "\" timestamp=\"");
        put_escaped(work, MtConnect.obs.timestamp);
        put(work, "\"/>");
    }
    else
    {
        put(work, "<");
        put(work, MtConnect.obs.type ? MtConnect.obs.type : "");
        put(work, " dataItemId=\"");
        put_escaped(work, MtConnect.obs.data_id);
        put(work, "\" sequence=\"");
        put_u64(work, MtConnect.obs.seq);
        put(work, "\" timestamp=\"");
        put_escaped(work, MtConnect.obs.timestamp);
        put(work, "\">");
        put_escaped(work, MtConnect.obs.value);
        put(work, "</");
        put(work, MtConnect.obs.type ? MtConnect.obs.type : "");
        put(work, ">");
    }
    if (!c->ok)
    {
        return;
    }
    const size_t n = c->len - tail;
    const int slot = (cat == PROTOCORE_MTC_SAMPLE) ? 0 : ((cat == PROTOCORE_MTC_EVENT) ? 1 : 2);
    if (c->run[slot] != tail)
    {
        // The element was composed at the document end and belongs at the end of its own run, with
        // the later runs after it: AB -> BA over [run, len). Three reversals do that in place, so
        // moving an element costs no buffer of its own however long it is.
        reverse(c->out + c->run[slot], tail - c->run[slot]);
        reverse(c->out + tail, n);
        reverse(c->out + c->run[slot], c->len - c->run[slot]);
    }
    for (int i = slot; i < 3; i++)
    {
        c->run[i] += n;
    }
    MtConnect.ok = c->ok;
}

static void streams_end(uint8_t *restrict work)
{
    close_component(work);
    doc_close(work, "</DeviceStream></Streams></MTConnectStreams>");
}

// --- error -----------------------------------------------------------------

static void error(uint8_t *restrict work)
{
    doc_open(work, "MTConnectError");
    put_header_common(work);
    put_window(work, 0, 0, PROTOCORE_MTC_SAMPLE_BUFFER);
    put(work, "\"/>");
    put(work, "<Errors><Error errorCode=\"");
    put_escaped(work, MtConnect.err.error_code);
    put(work, "\">");
    put_escaped(work, MtConnect.err.message);
    doc_close(work, "</Error></Errors></MTConnectError>");
}

// --- probe (MTConnectDevices) ----------------------------------------------

static void devices_begin(uint8_t *restrict work)
{
    doc_open(work, "MTConnectDevices");
    put_header_common(work);
    put_window(work, 0, 0, PROTOCORE_MTC_SAMPLE_BUFFER);
    // DevicesType's Header adds the asset counters: the agent's capacity and how much is in use,
    // neither of which is a property of the device being described.
    put(work, "\" assetBufferSize=\"");
    put_u64(work, (uint64_t)MtConnect.assets.asset_buffer_size);
    put(work, "\" assetCount=\"");
    put_u64(work, (uint64_t)MtConnect.assets.asset_count);
    put(work, "\"/>");
    put(work, "<Devices><Device id=\"");
    put_escaped(work, MtConnect.device.device_id);
    put(work, "\" name=\"");
    put_escaped(work, MtConnect.device.device_name);
    put(work, "\" uuid=\"");
    put_escaped(work, MtConnect.device.uuid);
    put(work, "\"><DataItems>");
    MtConnect.ok = MTC_CTX(work)->ok;
}

static void devices_add(uint8_t *restrict work)
{
    put(work, "<DataItem category=\"");
    put(work, mtc_cat_str(MtConnect.item.cat));
    put(work, "\" id=\"");
    put_escaped(work, MtConnect.item.id);
    put(work, "\" type=\"");
    put_escaped(work, MtConnect.item.type);
    put(work, "\"");
    if (MtConnect.item.name && MtConnect.item.name[0])
    {
        put(work, " name=\"");
        put_escaped(work, MtConnect.item.name);
        put(work, "\"");
    }
    if (MtConnect.item.units && MtConnect.item.units[0])
    {
        put(work, " units=\"");
        put_escaped(work, MtConnect.item.units);
        put(work, "\"");
    }
    put(work, "/>");
    MtConnect.ok = MTC_CTX(work)->ok;
}

static void devices_end(uint8_t *restrict work)
{
    doc_close(work, "</DataItems></Device></Devices></MTConnectDevices>");
}

// --- asset (MTConnectAssets) -----------------------------------------------

static void assets_begin(uint8_t *restrict work)
{
    doc_open(work, "MTConnectAssets");
    put_header_common(work);
    put(work, "\" assetBufferSize=\"");
    put_u64(work, (uint64_t)MtConnect.assets.asset_buffer_size);
    put(work, "\" assetCount=\"");
    put_u64(work, (uint64_t)MtConnect.assets.asset_count);
    put(work, "\"/>");
    put(work, "<Assets>");
    MtConnect.ok = MTC_CTX(work)->ok;
}

// One optional attribute, written only when the caller supplied it.
static void put_opt_attr(uint8_t *restrict work, const char *name, const char *value)
{
    if (!value || !value[0])
    {
        return;
    }
    put(work, " ");
    put(work, name);
    put(work, "=\"");
    put_escaped(work, value);
    put(work, "\"");
}

static void tool_begin(uint8_t *restrict work)
{
    put(work, "<CuttingTool assetId=\"");
    put_escaped(work, MtConnect.tool.asset_id);
    put(work, "\"");
    put_opt_attr(work, "serialNumber", MtConnect.tool.serial_number);
    put_opt_attr(work, "toolId", MtConnect.tool.tool_id);
    put_opt_attr(work, "deviceUuid", MtConnect.tool.device_uuid);
    put_opt_attr(work, "timestamp", MtConnect.tool.timestamp);
    // CuttingToolLifeCycleType opens with CutterStatus at minOccurs=1, ahead of ToolLife in the same
    // sequence, so it is written here rather than left to the caller to remember.
    put(work, "><CuttingToolLifeCycle><CutterStatus><Status>");
    put_escaped(work, MtConnect.tool.cutter_status);
    put(work, "</Status></CutterStatus>");
    MtConnect.ok = MTC_CTX(work)->ok;
}

static void tool_life(uint8_t *restrict work)
{
    // LifeType marks type, countDirection, initial and limit required: without initial and limit the
    // count says neither where the life started nor where it ends.
    put(work, "<ToolLife type=\"");
    put_escaped(work, MtConnect.life.type);
    put(work, "\" countDirection=\"");
    put_escaped(work, MtConnect.life.count_direction);
    put(work, "\" initial=\"");
    put_escaped(work, MtConnect.life.initial);
    put(work, "\" limit=\"");
    put_escaped(work, MtConnect.life.limit);
    put(work, "\">");
    put_escaped(work, MtConnect.life.value);
    put(work, "</ToolLife>");
    MtConnect.ok = MTC_CTX(work)->ok;
}

static void tool_end(uint8_t *restrict work)
{
    put(work, "</CuttingToolLifeCycle></CuttingTool>");
    MtConnect.ok = MTC_CTX(work)->ok;
}

static void assets_end(uint8_t *restrict work)
{
    doc_close(work, "</Assets></MTConnectAssets>");
}

// --- the observation ring the sample replay reads --------------------------

// Copy @p src into @p dst, stopping one short of @p cap, and terminate.
static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    if (src)
    {
        while (src[i] && i + 1 < cap)
        {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static void ring_init(uint8_t *restrict work)
{
    MtConnectCtx *c = MTC_CTX(work);
    c->count = 0;
    c->head = 0;
    c->next_seq = MtConnect.streams.next_seq ? MtConnect.streams.next_seq : 1u;
    c->first_seq = c->next_seq; // empty: first == next, so lastSequence sits just below first
    MtConnect.ok = PROTO_TRUE;
}

static void ring_add(uint8_t *restrict work)
{
    MtConnectCtx *c = MTC_CTX(work);
    MtConnectObs *o = &MTC_RING(work)[c->head];
    o->cat = (uint8_t)MtConnect.obs.cat;
    o->seq = c->next_seq;
    copy_str(o->type, sizeof(o->type), MtConnect.obs.type);
    copy_str(o->data_id, sizeof(o->data_id), MtConnect.obs.data_id);
    copy_str(o->timestamp, sizeof(o->timestamp), MtConnect.obs.timestamp);
    copy_str(o->value, sizeof(o->value), MtConnect.obs.value);
    c->head = (c->head + 1) % PROTOCORE_MTC_SAMPLE_BUFFER;
    if (c->count < PROTOCORE_MTC_SAMPLE_BUFFER)
    {
        c->count++;
    }
    else
    {
        c->first_seq++; // full: the oldest was overwritten, so the window slides forward
    }
    MtConnect.seq = c->next_seq++;
    MtConnect.ok = PROTO_TRUE;
}

static void ring_query(uint8_t *restrict work)
{
    MtConnectCtx *c = MTC_CTX(work);
    const uint64_t first = c->first_seq;
    const uint64_t next = c->next_seq; // one past the newest retained observation
    const uint64_t last = next - 1u;   // newest sequence; when empty this is first-1
    const uint64_t from = MtConnect.query.from;
    const uint64_t start = from < first ? first : from; // a stale `from` catches up from the oldest

    const uint32_t avail = (start < next) ? (uint32_t)(next - start) : 0u;
    const uint32_t to_emit = (MtConnect.query.count < avail) ? MtConnect.query.count : avail;
    // Resume point: past the last one returned, or nextSequence when nothing was in range.
    const uint64_t next_report = (start >= next) ? next : start + to_emit;

    // The oldest retained observation sits `count` slots behind head; observation first+k is at
    // (oldest + k) around the ring. Read out before the document is opened, because opening it
    // rewrites the context these come from.
    const uint32_t oldest = (c->head + PROTOCORE_MTC_SAMPLE_BUFFER - c->count) % PROTOCORE_MTC_SAMPLE_BUFFER;
    const uint32_t base = (uint32_t)(start - first);
    const MtConnectObs *ring = MTC_RING(work);

    MtConnect.streams.next_seq = next_report;
    MtConnect.window.first_seq = first;
    MtConnect.window.last_seq = last;
    MtConnect.window.buffer_size = PROTOCORE_MTC_SAMPLE_BUFFER;
    streams_begin(work);

    for (uint32_t e = 0; e < to_emit; e++)
    {
        const MtConnectObs *o = &ring[(oldest + base + e) % PROTOCORE_MTC_SAMPLE_BUFFER];
        MtConnect.obs.cat = (protocore_mtc_category)o->cat;
        MtConnect.obs.type = o->type;
        MtConnect.obs.data_id = o->data_id;
        MtConnect.obs.seq = o->seq;
        MtConnect.obs.timestamp = o->timestamp;
        MtConnect.obs.value = o->value;
        streams_add(work);
    }
    streams_end(work);
}

MtConnectNs MtConnect = {
    .streams_begin = streams_begin,
    .streams_add = streams_add,
    .streams_end = streams_end,
    .error = error,
    .devices_begin = devices_begin,
    .devices_add = devices_add,
    .devices_end = devices_end,
    .assets_begin = assets_begin,
    .tool_begin = tool_begin,
    .tool_life = tool_life,
    .tool_end = tool_end,
    .assets_end = assets_end,
    .ring_init = ring_init,
    .ring_add = ring_add,
    .ring_query = ring_query,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MTCONNECT
