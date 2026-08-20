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

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_FLOW_EXPORT

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "mmgr/protomem/protomem.h"
#include "services/net/flow_export/flow_export.h"

#include "mmgr/endian/endian.h"

PROTOCORE_BEGIN_DECLS

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

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define FLOW_EXPORT_OFF_CTX 0u
static_assert(FLOW_EXPORT_OFF_CTX + sizeof(struct FlowExportStorage) <= PROTOCORE_FLOW_EXPORT_BORROW,
              "PROTOCORE_FLOW_EXPORT_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define FLOW_EXPORT_CTX(w) ((struct FlowExportStorage *)(void *)((w) + FLOW_EXPORT_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_FLOW_EXPORT_BORROW persistent bytes
} FlowExportOwnCtx;
static FlowExportOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_flow_export_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_FLOW_EXPORT_BORROW).buf;
    }
    return s_own.span;
}

// Closes the Set that template_set, data_set_begin and message_finish find open, above its
// definition.
void protocore_flow_export_data_set_end(uint8_t *restrict work);

// ---------------------------------------------------------------------------
// Cursor primitives. Each latches the sticky error on overflow and writes nothing after it.
// ---------------------------------------------------------------------------

// Append @p v as two octets, most significant first.
static void put_u16(uint8_t *restrict work, uint16_t v)
{
    struct FlowExportStorage *m = FLOW_EXPORT_CTX(work);
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
static void put_u32(uint8_t *restrict work, uint32_t v)
{
    struct FlowExportStorage *m = FLOW_EXPORT_CTX(work);
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
static void put_span(uint8_t *restrict work, const uint8_t *p, size_t n)
{
    struct FlowExportStorage *m = FLOW_EXPORT_CTX(work);
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
static void put_zero(uint8_t *restrict work, size_t n)
{
    struct FlowExportStorage *m = FLOW_EXPORT_CTX(work);
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
static void patch_u16(uint8_t *restrict work, size_t off, uint16_t v)
{
    endian.wr16be(FLOW_EXPORT_CTX(work)->buf + off, v);
}

// ---------------------------------------------------------------------------
// Vendor NetFlow Version 5: a fixed 24-octet header then N fixed 48-octet records. No IETF
// specification covers this format; RFC 3954 specifies Version 9 only.
// ---------------------------------------------------------------------------

void protocore_flow_export_v5_header(uint8_t *restrict work)
{
    (void)work;
    FlowExportV.ok = PROTO_FALSE;
    FlowExportV.n = 0;
    uint8_t *buf = FlowExportV.out.buf;
    const FlowV5Header *h = FlowExportV.v5.header;
    if (!buf || !h || FlowExportV.out.cap < FLOW_V5_HEADER_SIZE)
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
    FlowExportV.n = p; // 24
    FlowExportV.ok = PROTO_TRUE;
}

void protocore_flow_export_v5_record(uint8_t *restrict work)
{
    (void)work;
    FlowExportV.ok = PROTO_FALSE;
    FlowExportV.n = 0;
    uint8_t *buf = FlowExportV.out.buf;
    const FlowV5Record *r = FlowExportV.v5.record;
    if (!buf || !r || FlowExportV.out.cap < FLOW_V5_RECORD_SIZE)
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
    FlowExportV.n = p; // 48
    FlowExportV.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// IPFIX (RFC 7011) and NetFlow v9 (RFC 3954): the template-then-data cursor.
// ---------------------------------------------------------------------------

// RFC 7011 sec 3.1: Version 0x000a, Length, Export Time, Sequence Number, Observation Domain ID.
// Length stays zero until message_finish.
void protocore_flow_export_ipfix_begin(uint8_t *restrict work)
{
    FlowExportV.ok = PROTO_FALSE;
    FlowExportV.n = 0;
    if (!FlowExportV.out.buf)
    {
        return;
    }
    struct FlowExportStorage *m = FLOW_EXPORT_CTX(work);
    m->buf = FlowExportV.out.buf;
    m->cap = FlowExportV.out.cap;
    m->pos = 0;
    m->set_start = 0;
    m->count = 0;
    m->version = FLOW_IPFIX_VERSION;
    m->error = PROTO_FALSE;
    put_u16(work, FLOW_IPFIX_VERSION);
    put_u16(work, 0);
    put_u32(work, FlowExportV.message.export_time);
    put_u32(work, FlowExportV.message.sequence_number);
    put_u32(work, FlowExportV.message.observation_domain_id);
    FlowExportV.ok = !m->error;
}

// RFC 3954 sec 5.1: Version 9, Count, sysUpTime, UNIX Secs, Sequence Number, Source ID.
// Count stays zero until message_finish.
void protocore_flow_export_v9_begin(uint8_t *restrict work)
{
    FlowExportV.ok = PROTO_FALSE;
    FlowExportV.n = 0;
    if (!FlowExportV.out.buf)
    {
        return;
    }
    struct FlowExportStorage *m = FLOW_EXPORT_CTX(work);
    m->buf = FlowExportV.out.buf;
    m->cap = FlowExportV.out.cap;
    m->pos = 0;
    m->set_start = 0;
    m->count = 0;
    m->version = FLOW_V9_VERSION;
    m->error = PROTO_FALSE;
    put_u16(work, FLOW_V9_VERSION);
    put_u16(work, 0);
    put_u32(work, FlowExportV.message.sys_uptime);
    put_u32(work, FlowExportV.message.unix_secs);
    put_u32(work, FlowExportV.message.sequence_number);
    put_u32(work, FlowExportV.message.observation_domain_id);
    FlowExportV.ok = !m->error;
}

// RFC 7011 sec 3.4.1 / RFC 3954 sec 5.2: Set ID, Set Length, Template ID, Field Count, then one
// Field Specifier per field. RFC 3954 sec 5.1 counts a Template Record toward Count.
void protocore_flow_export_template_set(uint8_t *restrict work)
{
    FlowExportV.ok = PROTO_FALSE;
    struct FlowExportStorage *m = FLOW_EXPORT_CTX(work);
    const FlowFieldSpecifier *fields = FlowExportV.tmpl.fields;
    const size_t field_count = FlowExportV.tmpl.field_count;
    if (!fields || field_count == 0)
    {
        return;
    }
    if (m->set_start)
    {
        protocore_flow_export_data_set_end(work);
    }
    size_t set_off = m->pos;
    put_u16(work, (m->version == FLOW_V9_VERSION) ? FLOW_V9_TEMPLATE_FLOWSET_ID : FLOW_IPFIX_TEMPLATE_SET_ID);
    put_u16(work, 0);
    put_u16(work, FlowExportV.template_id);
    put_u16(work, (uint16_t)field_count);
    for (size_t i = 0; i < field_count; i++)
    {
        put_u16(work, fields[i].information_element_id);
        put_u16(work, fields[i].field_length);
    }
    if (!m->error)
    {
        patch_u16(work, set_off + 2, (uint16_t)(m->pos - set_off));
    }
    m->count++;
    FlowExportV.ok = !m->error;
}

// RFC 7011 sec 3.3.2: a Data Set's Set ID is the Template ID its records match, 256 or above.
// RFC 3954 sec 5.2 reserves FlowSet IDs 0 through 255.
void protocore_flow_export_data_set_begin(uint8_t *restrict work)
{
    FlowExportV.ok = PROTO_FALSE;
    struct FlowExportStorage *m = FLOW_EXPORT_CTX(work);
    if (FlowExportV.template_id < FLOW_TEMPLATE_ID_MIN)
    {
        return;
    }
    if (m->set_start)
    {
        protocore_flow_export_data_set_end(work);
    }
    m->set_start = m->pos;
    put_u16(work, FlowExportV.template_id);
    put_u16(work, 0);
    FlowExportV.ok = !m->error;
}

// RFC 7011 sec 3.4.3: "It consists only of one or more Field Values." The caller encodes them in
// Template order; this copies them in and counts the record.
void protocore_flow_export_data_record(uint8_t *restrict work)
{
    FlowExportV.ok = PROTO_FALSE;
    struct FlowExportStorage *m = FLOW_EXPORT_CTX(work);
    if (!m->set_start || !FlowExportV.data.record || FlowExportV.data.len == 0)
    {
        return;
    }
    put_span(work, FlowExportV.data.record, FlowExportV.data.len);
    if (!m->error)
    {
        m->count++;
    }
    FlowExportV.ok = !m->error;
}

// RFC 3954 sec 5.3: "The Exporter SHOULD insert some padding bytes so that the subsequent FlowSet
// starts at a 4-byte aligned boundary", and the Length covers those octets. RFC 7011 sec 3.3.2:
// the Length is the Set Header plus all records plus the optional padding.
void protocore_flow_export_data_set_end(uint8_t *restrict work)
{
    FlowExportV.ok = PROTO_FALSE;
    struct FlowExportStorage *m = FLOW_EXPORT_CTX(work);
    if (!m->set_start)
    {
        return;
    }
    if (m->version == FLOW_V9_VERSION)
    {
        size_t set_len = m->pos - m->set_start;
        put_zero(work, (4 - (set_len & 3)) & 3);
    }
    if (!m->error)
    {
        patch_u16(work, m->set_start + 2, (uint16_t)(m->pos - m->set_start));
    }
    m->set_start = 0;
    FlowExportV.ok = !m->error;
}

// RFC 3954 sec 5.1 Count: "the sum of Options FlowSet records, Template FlowSet records, and Data
// FlowSet records". RFC 7011 sec 3.1 Length: "Total length of the IPFIX Message, measured in
// octets, including Message Header and Set(s)", a 16-bit field, so a longer message fails closed
// rather than reporting a truncated length.
void protocore_flow_export_message_finish(uint8_t *restrict work)
{
    FlowExportV.ok = PROTO_FALSE;
    FlowExportV.n = 0;
    struct FlowExportStorage *m = FLOW_EXPORT_CTX(work);
    if (m->set_start)
    {
        protocore_flow_export_data_set_end(work);
    }
    if (m->error)
    {
        return;
    }
    if (m->version == FLOW_V9_VERSION)
    {
        patch_u16(work, 2, m->count);
    }
    else
    {
        if (m->pos > FLOW_IPFIX_LENGTH_MAX)
        {
            return;
        }
        patch_u16(work, 2, (uint16_t)m->pos);
    }
    FlowExportV.n = m->pos;
    FlowExportV.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
FlowExportVars FlowExportV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FLOW_EXPORT
