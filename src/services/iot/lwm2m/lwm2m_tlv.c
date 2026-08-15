// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lwm2m_tlv.c
 * @brief The OMA LwM2M TLV writer and reader: the sec 7.4.5 Type byte, and the Appendix C Values.
 *
 * A write lays down `Type Identifier [Length] Value` with the Identifier and Length fields in
 * network byte order. A read takes those same fields off the head of the reader cursor and points
 * @c val.opaque into the source buffer.
 */

#include "services/iot/lwm2m/lwm2m_tlv.h"

#if PROTOCORE_ENABLE_LWM2M

#include "mmgr/protomem.h" // mem.cpy: the Value octets a write copies, and the Float bit pattern
#include "mmgr/protostr.h" // str.len: the bounded String measure

// The writer cursor: the caller buffer, how far into it the last write reached, and the poison an
// entry that did not fit leaves behind. scalar holds the octets a typed write stages.
typedef struct
{
    uint8_t *buf;
    size_t cap;
    size_t pos;
    proto_bool overflow;
    uint8_t scalar[8];
} Lwm2mTlvWriteCursor;

// The reader cursor: the caller buffer and the offset of the next entry in it.
typedef struct
{
    const uint8_t *buf;
    size_t len;
    size_t pos;
} Lwm2mTlvReadCursor;

/**
 * @brief The codec's compile-time storage: the two cursors, and the staging octets between them.
 *
 * All of it BSS, so a payload costs no heap.
 */
struct Lwm2mTlvStorage
{
    Lwm2mTlvWriteCursor w; ///< where the next entry is emitted
    Lwm2mTlvReadCursor r;  ///< where the next entry is decoded from
};

/**
 * @brief The codec's cursors and the calls that reach them - what Lwm2mTlvNs points at.
 *
 * @var Lwm2mTlvInternal::store  the writer cursor and the reader cursor
 * @var Lwm2mTlvInternal::ns     the handle a caller sets a call's members on
 */
struct Lwm2mTlvInternal
{
    struct Lwm2mTlvStorage *store;
    Lwm2mTlvNs *ns;
};

static struct Lwm2mTlvStorage s_store;

static struct Lwm2mTlvInternal s_lwm2m_tlv = {.store = &s_store, .ns = &Lwm2mTlv};

// The shortest Integer width holding v: 1, 2, 4 or 8 octets (LwM2M Core Appendix C Table C.-2).
static size_t integer_octets(int64_t v)
{
    if (v >= -128 && v <= 127)
    {
        return 1;
    }
    if (v >= -32768 && v <= 32767)
    {
        return 2;
    }
    if (v >= -2147483648LL && v <= 2147483647LL)
    {
        return 4;
    }
    return 8;
}

// Lay the low n octets of bits down most significant first: network byte order.
static void store_be(uint8_t *dst, uint64_t bits, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        dst[i] = (uint8_t)(bits >> (8 * (n - 1 - i)));
    }
}

// Bind the sink buffer and clear the cursor. A sink with no buffer starts poisoned, so every later
// write and the finish fail closed.
static void tlv_open(struct Lwm2mTlvInternal *restrict ctx)
{
    ctx->store->w.buf = ctx->ns->sink.buf;
    ctx->store->w.cap = ctx->ns->sink.cap;
    ctx->store->w.pos = 0;
    ctx->store->w.overflow = (ctx->ns->sink.buf == NULL);
    ctx->ns->ok = !ctx->store->w.overflow;
}

// Emit one entry: the Type byte, the Identifier field, the Length field the Value's size calls for,
// and the Value (LwM2M Core sec 7.4.5 Table 7.4.5.-1).
static void tlv_write(struct Lwm2mTlvInternal *restrict ctx)
{
    Lwm2mTlvWriteCursor *w = &ctx->store->w;
    const uint8_t *value = ctx->ns->val.opaque;
    const size_t value_len = ctx->ns->val.len;
    ctx->ns->ok = PROTO_FALSE;
    if (value_len && !value)
    {
        return;
    }

    uint8_t type = (uint8_t)(ctx->ns->hdr.id_type & LWM2M_TLV_IDTYPE_MASK);
    proto_bool id16 = ctx->ns->hdr.id > 0xFF; // past 255 the Identifier field is 16 bits
    if (id16)
    {
        type |= LWM2M_TLV_ID16_FLAG;
    }

    size_t lenbytes;
    if (value_len <= LWM2M_TLV_INLINE_LEN_MASK) // 0..7 rides in bits 2-0 (type of Length 00)
    {
        lenbytes = 0;
        type |= (uint8_t)value_len;
    }
    else if (value_len <= 0xFF) // 8-bit Length field (type of Length 01)
    {
        lenbytes = 1;
        type |= (uint8_t)(1 << LWM2M_TLV_LENTYPE_SHIFT);
    }
    else if (value_len <= 0xFFFF) // 16-bit Length field (type of Length 10)
    {
        lenbytes = 2;
        type |= (uint8_t)(2 << LWM2M_TLV_LENTYPE_SHIFT);
    }
    else if (value_len <= 0xFFFFFF) // 24-bit Length field (type of Length 11)
    {
        lenbytes = 3;
        type |= (uint8_t)(3 << LWM2M_TLV_LENTYPE_SHIFT);
    }
    else
    {
        return;
    }

    size_t need = 1 + (id16 ? 2 : 1) + lenbytes + value_len;
    if (w->overflow || w->pos + need > w->cap)
    {
        w->overflow = PROTO_TRUE;
        return;
    }

    w->buf[w->pos++] = type;
    if (id16)
    {
        w->buf[w->pos++] = (uint8_t)(ctx->ns->hdr.id >> 8);
    }
    w->buf[w->pos++] = (uint8_t)ctx->ns->hdr.id;
    store_be(w->buf + w->pos, (uint64_t)value_len, lenbytes);
    w->pos += lenbytes;
    if (value_len)
    {
        mem.cpy(w->buf + w->pos, value, value_len);
        w->pos += value_len;
    }
    ctx->ns->ok = PROTO_TRUE;
}

// Stage the Integer in its shortest width and emit it.
static void tlv_write_integer(struct Lwm2mTlvInternal *restrict ctx)
{
    const size_t n = integer_octets(ctx->ns->val.integer_value);
    store_be(ctx->store->w.scalar, (uint64_t)ctx->ns->val.integer_value, n);
    ctx->ns->val.opaque = ctx->store->w.scalar;
    ctx->ns->val.len = n;
    tlv_write(ctx);
}

// Stage the Boolean as one octet and emit it: the Length of a Boolean is always 1.
static void tlv_write_boolean(struct Lwm2mTlvInternal *restrict ctx)
{
    ctx->store->w.scalar[0] = ctx->ns->val.boolean_value ? 1 : 0;
    ctx->ns->val.opaque = ctx->store->w.scalar;
    ctx->ns->val.len = 1;
    tlv_write(ctx);
}

// Measure the String to its NUL within the sink's capacity and emit its octets. A string that long
// cannot fit beside a Type byte and an Identifier, so the write poisons the cursor.
static void tlv_write_string(struct Lwm2mTlvInternal *restrict ctx)
{
    if (!ctx->ns->val.string_value)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    ctx->ns->val.opaque = (const uint8_t *)ctx->ns->val.string_value;
    ctx->ns->val.len = str.len(ctx->ns->val.string_value, ctx->store->w.cap);
    tlv_write(ctx);
}

// Stage the Float as binary64 in network byte order and emit it.
static void tlv_write_float(struct Lwm2mTlvInternal *restrict ctx)
{
    uint64_t bits;
    double v = ctx->ns->val.float_value;
    mem.cpy(&bits, &v, 8);
    store_be(ctx->store->w.scalar, bits, 8);
    ctx->ns->val.opaque = ctx->store->w.scalar;
    ctx->ns->val.len = 8;
    tlv_write(ctx);
}

// Count the octets emitted. A poisoned cursor reports 0, so a truncated payload never leaves.
static void tlv_finish(struct Lwm2mTlvInternal *restrict ctx)
{
    ctx->ns->ok = !ctx->store->w.overflow;
    ctx->ns->n = ctx->store->w.overflow ? 0 : ctx->store->w.pos;
}

// Bind the source buffer and put the reader cursor at its first entry.
static void tlv_parse(struct Lwm2mTlvInternal *restrict ctx)
{
    ctx->store->r.buf = ctx->ns->source.buf;
    ctx->store->r.len = ctx->ns->source.len;
    ctx->store->r.pos = 0;
    ctx->ns->ok = (ctx->ns->source.buf != NULL);
}

// Decode the entry at the cursor into hdr and val, and step the cursor past its Value. False at the
// end of the source or on an entry the source cuts short.
static void tlv_next(struct Lwm2mTlvInternal *restrict ctx)
{
    const Lwm2mTlvReadCursor *r = &ctx->store->r;
    const uint8_t *buf = r->buf;
    const size_t len = r->len;
    ctx->ns->ok = PROTO_FALSE;
    if (!buf || r->pos >= len)
    {
        return;
    }

    size_t p = r->pos;
    uint8_t type = buf[p++];
    proto_bool id16 = (type & LWM2M_TLV_ID16_FLAG) != 0;
    if (p + (id16 ? 2u : 1u) > len)
    {
        return;
    }
    uint16_t id = buf[p++];
    if (id16)
    {
        id = (uint16_t)((id << 8) | buf[p++]);
    }

    uint8_t lentype = (uint8_t)((type >> LWM2M_TLV_LENTYPE_SHIFT) & LWM2M_TLV_LENTYPE_MASK);
    size_t value_len;
    if (lentype == 0)
    {
        value_len = type & LWM2M_TLV_INLINE_LEN_MASK; // no Length field: bits 2-0 are the Length
    }
    else
    {
        if (p + lentype > len)
        {
            return;
        }
        value_len = 0; // lentype is the Length field's width in octets, most significant first
        for (uint8_t i = 0; i < lentype; i++)
        {
            value_len = (value_len << 8) | buf[p++];
        }
    }
    if (p + value_len > len)
    {
        return;
    }

    ctx->ns->hdr.id_type = (Lwm2mTlvIdType)(type & LWM2M_TLV_IDTYPE_MASK);
    ctx->ns->hdr.id = id;
    ctx->ns->val.opaque = buf + p;
    ctx->ns->val.len = value_len;
    ctx->store->r.pos = p + value_len;
    ctx->ns->ok = PROTO_TRUE;
}

// Read a Value as an Integer: 1, 2, 4 or 8 octets, network byte order, two's complement.
static void tlv_value_integer(struct Lwm2mTlvInternal *restrict ctx)
{
    const uint8_t *value = ctx->ns->val.opaque;
    const size_t len = ctx->ns->val.len;
    ctx->ns->ok = PROTO_FALSE;
    if (!value || (len != 1 && len != 2 && len != 4 && len != 8))
    {
        return;
    }
    int64_t v = (value[0] & 0x80) ? -1 : 0; // sign-extend from the most significant bit
    for (size_t i = 0; i < len; i++)
    {
        v = (int64_t)(((uint64_t)v << 8) | value[i]);
    }
    ctx->ns->val.integer_value = v;
    ctx->ns->ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
Lwm2mTlvNs Lwm2mTlv = {.open = tlv_open,
                       .write = tlv_write,
                       .write_integer = tlv_write_integer,
                       .write_boolean = tlv_write_boolean,
                       .write_string = tlv_write_string,
                       .write_float = tlv_write_float,
                       .finish = tlv_finish,
                       .parse = tlv_parse,
                       .next = tlv_next,
                       .value_integer = tlv_value_integer,
                       .internal = &s_lwm2m_tlv};

#endif // PROTOCORE_ENABLE_LWM2M
