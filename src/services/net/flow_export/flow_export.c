// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file flow_export.c
 * @brief The Exporting Process: IPFIX Messages (RFC 7011 sec 3), NetFlow v9 Export Packets
 *        (RFC 3954 sec 5), and vendor NetFlow Version 5 packets.
 *
 * The v5 writes are stateless: header and record go straight into the caller's span. The v9 and
 * IPFIX side is a cursor over one message: a begin lays the Message Header (RFC 7011 sec 3.1,
 * RFC 3954 sec 5.1) with its length or count left at zero, each Set records its own start offset
 * so its Length field is patched when the Set closes (RFC 7011 sec 3.3.2, RFC 3954 sec 5.3), and
 * message_finish patches the header. Every field is network byte order.
 */

#include "services/net/flow_export/flow_export.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_FLOW_EXPORT

#include "mmgr/endian.h"

#define FLOW_V5_VERSION 5 ///< vendor NetFlow Version 5 packet Version field

#define FLOW_V9_VERSION 9             ///< RFC 3954 sec 5.1 Version
#define FLOW_V9_TEMPLATE_FLOWSET_ID 0 ///< RFC 3954 sec 5.2 Template FlowSet ID

#define FLOW_IPFIX_VERSION 10         ///< RFC 7011 sec 3.1 Version 0x000a
#define FLOW_IPFIX_TEMPLATE_SET_ID 2  ///< RFC 7011 sec 3.3.2 Template Set ID
#define FLOW_IPFIX_LENGTH_MAX 0xFFFFu ///< RFC 7011 sec 3.1 Length is a 16-bit octet count

#define FLOW_TEMPLATE_ID_MIN 256 ///< RFC 7011 sec 3.4.1 / RFC 3954 sec 5.2: Template IDs start at 256

/**
 * @brief The message under construction: RFC 7011 sec 3 / RFC 3954 sec 5, one at a time.
 *
 * `set_start` is the offset of the open Set header, and zero means none is open: a Set never
 * starts at offset 0, the Message Header occupies it.
 */
struct FlowExportStorage
{
    uint8_t *buf;     ///< the caller's span this message is built in
    size_t cap;       ///< its octet capacity
    size_t pos;       ///< octets written so far
    size_t set_start; ///< offset of the open Set header, 0 when none is open
    uint16_t count;   ///< RFC 3954 sec 5.1 Count: Template plus Data Records in this packet
    uint8_t version;  ///< RFC 3954 sec 5.1 / RFC 7011 sec 3.1 Version: 9 or 10
    proto_bool error; ///< sticky overflow flag: every put is a no-op once it is set
};

/**
 * @brief The message state and the calls that reach it - what FlowExportNs points at.
 *
 * @var FlowExportInternal::store  the cursor over the message under construction
 * @var FlowExportInternal::ns     the handle a caller sets a call's members on
 */
struct FlowExportInternal
{
    struct FlowExportStorage *store;
    FlowExportNs *ns;
};

static struct FlowExportStorage s_message;

static struct FlowExportInternal s_flow = {.store = &s_message, .ns = &FlowExport};

// Closes the Set that template_set, data_set_begin and message_finish find open, above its
// definition.
static void data_set_end(struct FlowExportInternal *restrict ctx);

// ---------------------------------------------------------------------------
// Cursor primitives. Each latches the sticky error on overflow and writes nothing after it.
// ---------------------------------------------------------------------------

// Append @p v as two octets, most significant first.
static void put_u16(struct FlowExportInternal *restrict ctx, uint16_t v)
{
    struct FlowExportStorage *m = ctx->store;
    if (m->error)
    {
        return;
    }
    if (m->pos + 2 > m->cap)
    {
        m->error = PROTO_TRUE;
        return;
    }
    m->pos += endian.wr16be(m->buf + m->pos, v);
}

// Append @p v as four octets, most significant first.
static void put_u32(struct FlowExportInternal *restrict ctx, uint32_t v)
{
    struct FlowExportStorage *m = ctx->store;
    if (m->error)
    {
        return;
    }
    if (m->pos + 4 > m->cap)
    {
        m->error = PROTO_TRUE;
        return;
    }
    m->pos += endian.wr32be(m->buf + m->pos, v);
}

// Append @p n octets from @p p.
static void put_span(struct FlowExportInternal *restrict ctx, const uint8_t *p, size_t n)
{
    struct FlowExportStorage *m = ctx->store;
    if (m->error)
    {
        return;
    }
    if (m->pos + n > m->cap)
    {
        m->error = PROTO_TRUE;
        return;
    }
    mem.cpy(m->buf + m->pos, p, n);
    m->pos += n;
}

// Append @p n zero octets. RFC 7011 sec 3.3.1: padding octets MUST be zero.
static void put_zero(struct FlowExportInternal *restrict ctx, size_t n)
{
    struct FlowExportStorage *m = ctx->store;
    if (m->error)
    {
        return;
    }
    if (m->pos + n > m->cap)
    {
        m->error = PROTO_TRUE;
        return;
    }
    mem.set(m->buf + m->pos, 0, n);
    m->pos += n;
}

// Overwrite the two octets at @p off with @p v. Only reached with off + 2 <= pos.
static void patch_u16(struct FlowExportInternal *restrict ctx, size_t off, uint16_t v)
{
    endian.wr16be(ctx->store->buf + off, v);
}

// ---------------------------------------------------------------------------
// Vendor NetFlow Version 5: a fixed 24-octet header then N fixed 48-octet records. No IETF
// specification covers this format; RFC 3954 specifies Version 9 only.
// ---------------------------------------------------------------------------

static void v5_header(struct FlowExportInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    uint8_t *buf = ctx->ns->out.buf;
    const FlowV5Header *h = ctx->ns->v5.header;
    if (!buf || !h || ctx->ns->out.cap < FLOW_V5_HEADER_SIZE)
    {
        return;
    }
    size_t p = 0;
    p += endian.wr16be(buf + p, FLOW_V5_VERSION);
    p += endian.wr16be(buf + p, h->count);
    p += endian.wr32be(buf + p, h->sys_uptime);
    p += endian.wr32be(buf + p, h->unix_secs);
    p += endian.wr32be(buf + p, h->unix_nsecs);
    p += endian.wr32be(buf + p, h->flow_sequence);
    buf[p++] = h->engine_type;
    buf[p++] = h->engine_id;
    p += endian.wr16be(buf + p, h->sampling_interval);
    ctx->ns->n = p; // 24
    ctx->ns->ok = PROTO_TRUE;
}

static void v5_record(struct FlowExportInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    uint8_t *buf = ctx->ns->out.buf;
    const FlowV5Record *r = ctx->ns->v5.record;
    if (!buf || !r || ctx->ns->out.cap < FLOW_V5_RECORD_SIZE)
    {
        return;
    }
    size_t p = 0;
    p += endian.wr32be(buf + p, r->src_addr);
    p += endian.wr32be(buf + p, r->dst_addr);
    p += endian.wr32be(buf + p, r->next_hop);
    p += endian.wr16be(buf + p, r->input);
    p += endian.wr16be(buf + p, r->output);
    p += endian.wr32be(buf + p, r->d_pkts);
    p += endian.wr32be(buf + p, r->d_octets);
    p += endian.wr32be(buf + p, r->first);
    p += endian.wr32be(buf + p, r->last);
    p += endian.wr16be(buf + p, r->src_port);
    p += endian.wr16be(buf + p, r->dst_port);
    buf[p++] = 0; // pad1
    buf[p++] = r->tcp_flags;
    buf[p++] = r->prot;
    buf[p++] = r->tos;
    p += endian.wr16be(buf + p, r->src_as);
    p += endian.wr16be(buf + p, r->dst_as);
    buf[p++] = r->src_mask;
    buf[p++] = r->dst_mask;
    buf[p++] = 0; // pad2, two octets
    buf[p++] = 0;
    ctx->ns->n = p; // 48
    ctx->ns->ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// IPFIX (RFC 7011) and NetFlow v9 (RFC 3954): the template-then-data cursor.
// ---------------------------------------------------------------------------

// RFC 7011 sec 3.1: Version 0x000a, Length, Export Time, Sequence Number, Observation Domain ID.
// Length stays zero until message_finish.
static void ipfix_begin(struct FlowExportInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    if (!ctx->ns->out.buf)
    {
        return;
    }
    struct FlowExportStorage *m = ctx->store;
    m->buf = ctx->ns->out.buf;
    m->cap = ctx->ns->out.cap;
    m->pos = 0;
    m->set_start = 0;
    m->count = 0;
    m->version = FLOW_IPFIX_VERSION;
    m->error = PROTO_FALSE;
    put_u16(ctx, FLOW_IPFIX_VERSION);
    put_u16(ctx, 0);
    put_u32(ctx, ctx->ns->message.export_time);
    put_u32(ctx, ctx->ns->message.sequence_number);
    put_u32(ctx, ctx->ns->message.observation_domain_id);
    ctx->ns->ok = !m->error;
}

// RFC 3954 sec 5.1: Version 9, Count, sysUpTime, UNIX Secs, Sequence Number, Source ID.
// Count stays zero until message_finish.
static void v9_begin(struct FlowExportInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    if (!ctx->ns->out.buf)
    {
        return;
    }
    struct FlowExportStorage *m = ctx->store;
    m->buf = ctx->ns->out.buf;
    m->cap = ctx->ns->out.cap;
    m->pos = 0;
    m->set_start = 0;
    m->count = 0;
    m->version = FLOW_V9_VERSION;
    m->error = PROTO_FALSE;
    put_u16(ctx, FLOW_V9_VERSION);
    put_u16(ctx, 0);
    put_u32(ctx, ctx->ns->message.sys_uptime);
    put_u32(ctx, ctx->ns->message.unix_secs);
    put_u32(ctx, ctx->ns->message.sequence_number);
    put_u32(ctx, ctx->ns->message.observation_domain_id);
    ctx->ns->ok = !m->error;
}

// RFC 7011 sec 3.4.1 / RFC 3954 sec 5.2: Set ID, Set Length, Template ID, Field Count, then one
// Field Specifier per field. RFC 3954 sec 5.1 counts a Template Record toward Count.
static void template_set(struct FlowExportInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    struct FlowExportStorage *m = ctx->store;
    const FlowFieldSpecifier *fields = ctx->ns->tmpl.fields;
    const size_t field_count = ctx->ns->tmpl.field_count;
    if (!fields || field_count == 0)
    {
        return;
    }
    if (m->set_start)
    {
        data_set_end(ctx);
    }
    size_t set_off = m->pos;
    put_u16(ctx, (m->version == FLOW_V9_VERSION) ? FLOW_V9_TEMPLATE_FLOWSET_ID : FLOW_IPFIX_TEMPLATE_SET_ID);
    put_u16(ctx, 0);
    put_u16(ctx, ctx->ns->template_id);
    put_u16(ctx, (uint16_t)field_count);
    for (size_t i = 0; i < field_count; i++)
    {
        put_u16(ctx, fields[i].information_element_id);
        put_u16(ctx, fields[i].field_length);
    }
    if (!m->error)
    {
        patch_u16(ctx, set_off + 2, (uint16_t)(m->pos - set_off));
    }
    m->count++;
    ctx->ns->ok = !m->error;
}

// RFC 7011 sec 3.3.2: a Data Set's Set ID is the Template ID its records match, 256 or above.
// RFC 3954 sec 5.2 reserves FlowSet IDs 0 through 255.
static void data_set_begin(struct FlowExportInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    struct FlowExportStorage *m = ctx->store;
    if (ctx->ns->template_id < FLOW_TEMPLATE_ID_MIN)
    {
        return;
    }
    if (m->set_start)
    {
        data_set_end(ctx);
    }
    m->set_start = m->pos;
    put_u16(ctx, ctx->ns->template_id);
    put_u16(ctx, 0);
    ctx->ns->ok = !m->error;
}

// RFC 7011 sec 3.4.3: "It consists only of one or more Field Values." The caller encodes them in
// Template order; this copies them in and counts the record.
static void data_record(struct FlowExportInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    struct FlowExportStorage *m = ctx->store;
    if (!m->set_start || !ctx->ns->data.record || ctx->ns->data.len == 0)
    {
        return;
    }
    put_span(ctx, ctx->ns->data.record, ctx->ns->data.len);
    if (!m->error)
    {
        m->count++;
    }
    ctx->ns->ok = !m->error;
}

// RFC 3954 sec 5.3: "The Exporter SHOULD insert some padding bytes so that the subsequent FlowSet
// starts at a 4-byte aligned boundary", and the Length covers those octets. RFC 7011 sec 3.3.2:
// the Length is the Set Header plus all records plus the optional padding.
static void data_set_end(struct FlowExportInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    struct FlowExportStorage *m = ctx->store;
    if (!m->set_start)
    {
        return;
    }
    if (m->version == FLOW_V9_VERSION)
    {
        size_t set_len = m->pos - m->set_start;
        put_zero(ctx, (4 - (set_len & 3)) & 3);
    }
    if (!m->error)
    {
        patch_u16(ctx, m->set_start + 2, (uint16_t)(m->pos - m->set_start));
    }
    m->set_start = 0;
    ctx->ns->ok = !m->error;
}

// RFC 3954 sec 5.1 Count: "the sum of Options FlowSet records, Template FlowSet records, and Data
// FlowSet records". RFC 7011 sec 3.1 Length: "Total length of the IPFIX Message, measured in
// octets, including Message Header and Set(s)", a 16-bit field, so a longer message fails closed
// rather than reporting a truncated length.
static void message_finish(struct FlowExportInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    struct FlowExportStorage *m = ctx->store;
    if (m->set_start)
    {
        data_set_end(ctx);
    }
    if (m->error)
    {
        return;
    }
    if (m->version == FLOW_V9_VERSION)
    {
        patch_u16(ctx, 2, m->count);
    }
    else
    {
        if (m->pos > FLOW_IPFIX_LENGTH_MAX)
        {
            return;
        }
        patch_u16(ctx, 2, (uint16_t)m->pos);
    }
    ctx->ns->n = m->pos;
    ctx->ns->ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
FlowExportNs FlowExport = {.v5_header = v5_header,
                           .v5_record = v5_record,
                           .ipfix_begin = ipfix_begin,
                           .v9_begin = v9_begin,
                           .template_set = template_set,
                           .data_set_begin = data_set_begin,
                           .data_record = data_record,
                           .data_set_end = data_set_end,
                           .message_finish = message_finish,
                           .internal = &s_flow};

#endif // PROTOCORE_ENABLE_FLOW_EXPORT
