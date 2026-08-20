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
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from

#if PROTOCORE_ENABLE_LWM2M

#include "mmgr/protomem/protomem.h" // mem.cpy: the Value octets a write copies, and the Float bit pattern
#include "mmgr/protostr/protostr.h" // str.len: the bounded String measure

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

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define LWM2M_TLV_OFF_CTX 0u
static_assert(LWM2M_TLV_OFF_CTX + sizeof(struct Lwm2mTlvStorage) <= PROTOCORE_LWM2M_TLV_BORROW,
              "PROTOCORE_LWM2M_TLV_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define LWM2M_TLV_CTX(w) ((struct Lwm2mTlvStorage *)(void *)((w) + LWM2M_TLV_OFF_CTX))

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

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_LWM2M_TLV_BORROW persistent bytes
} Lwm2mTlvOwnCtx;
static Lwm2mTlvOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_lwm2m_tlv_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_LWM2M_TLV_BORROW).buf;
    }
    return s_own.span;
}

// Bind the sink buffer and clear the cursor. A sink with no buffer starts poisoned, so every later
// write and the finish fail closed.
static void tlv_open(uint8_t *restrict work)
{
    LWM2M_TLV_CTX(work)->w.buf = Lwm2mTlv.sink.buf;
    LWM2M_TLV_CTX(work)->w.cap = Lwm2mTlv.sink.cap;
    LWM2M_TLV_CTX(work)->w.pos = 0;
    LWM2M_TLV_CTX(work)->w.overflow = (Lwm2mTlv.sink.buf == NULL);
    Lwm2mTlv.ok = !LWM2M_TLV_CTX(work)->w.overflow;
}

// Emit one entry: the Type byte, the Identifier field, the Length field the Value's size calls for,
// and the Value (LwM2M Core sec 7.4.5 Table 7.4.5.-1).
static void tlv_write(uint8_t *restrict work)
{
    Lwm2mTlvWriteCursor *w = &LWM2M_TLV_CTX(work)->w;
    const uint8_t *value = Lwm2mTlv.val.opaque;
    const size_t value_len = Lwm2mTlv.val.len;
    Lwm2mTlv.ok = PROTO_FALSE;
    if (value_len && !value)
    {
        return;
    }

    uint8_t type = (uint8_t)(Lwm2mTlv.hdr.id_type & LWM2M_TLV_IDTYPE_MASK);
    proto_bool id16 = Lwm2mTlv.hdr.id > 0xFF; // past 255 the Identifier field is 16 bits
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
        w->buf[w->pos++] = (uint8_t)(Lwm2mTlv.hdr.id >> 8);
    }
    w->buf[w->pos++] = (uint8_t)Lwm2mTlv.hdr.id;
    store_be(w->buf + w->pos, (uint64_t)value_len, lenbytes);
    w->pos += lenbytes;
    if (value_len)
    {
        mem.cpy(w->buf + w->pos, value, value_len);
        w->pos += value_len;
    }
    Lwm2mTlv.ok = PROTO_TRUE;
}

// Stage the Integer in its shortest width and emit it.
static void tlv_write_integer(uint8_t *restrict work)
{
    const size_t n = integer_octets(Lwm2mTlv.val.integer_value);
    store_be(LWM2M_TLV_CTX(work)->w.scalar, (uint64_t)Lwm2mTlv.val.integer_value, n);
    Lwm2mTlv.val.opaque = LWM2M_TLV_CTX(work)->w.scalar;
    Lwm2mTlv.val.len = n;
    tlv_write(work);
}

// Stage the Boolean as one octet and emit it: the Length of a Boolean is always 1.
static void tlv_write_boolean(uint8_t *restrict work)
{
    LWM2M_TLV_CTX(work)->w.scalar[0] = Lwm2mTlv.val.boolean_value ? 1 : 0;
    Lwm2mTlv.val.opaque = LWM2M_TLV_CTX(work)->w.scalar;
    Lwm2mTlv.val.len = 1;
    tlv_write(work);
}

// Measure the String to its NUL within the sink's capacity and emit its octets. A string that long
// cannot fit beside a Type byte and an Identifier, so the write poisons the cursor.
static void tlv_write_string(uint8_t *restrict work)
{
    if (!Lwm2mTlv.val.string_value)
    {
        Lwm2mTlv.ok = PROTO_FALSE;
        return;
    }
    Lwm2mTlv.val.opaque = (const uint8_t *)Lwm2mTlv.val.string_value;
    Lwm2mTlv.val.len = str.len(Lwm2mTlv.val.string_value, LWM2M_TLV_CTX(work)->w.cap);
    tlv_write(work);
}

// Stage the Float as binary64 in network byte order and emit it.
static void tlv_write_float(uint8_t *restrict work)
{
    uint64_t bits;
    double v = Lwm2mTlv.val.float_value;
    mem.cpy(&bits, &v, 8);
    store_be(LWM2M_TLV_CTX(work)->w.scalar, bits, 8);
    Lwm2mTlv.val.opaque = LWM2M_TLV_CTX(work)->w.scalar;
    Lwm2mTlv.val.len = 8;
    tlv_write(work);
}

// Count the octets emitted. A poisoned cursor reports 0, so a truncated payload never leaves.
static void tlv_finish(uint8_t *restrict work)
{
    Lwm2mTlv.ok = !LWM2M_TLV_CTX(work)->w.overflow;
    Lwm2mTlv.n = LWM2M_TLV_CTX(work)->w.overflow ? 0 : LWM2M_TLV_CTX(work)->w.pos;
}

// Bind the source buffer and put the reader cursor at its first entry.
static void tlv_parse(uint8_t *restrict work)
{
    LWM2M_TLV_CTX(work)->r.buf = Lwm2mTlv.source.buf;
    LWM2M_TLV_CTX(work)->r.len = Lwm2mTlv.source.len;
    LWM2M_TLV_CTX(work)->r.pos = 0;
    Lwm2mTlv.ok = (Lwm2mTlv.source.buf != NULL);
}

// Decode the entry at the cursor into hdr and val, and step the cursor past its Value. False at the
// end of the source or on an entry the source cuts short.
static void tlv_next(uint8_t *restrict work)
{
    const Lwm2mTlvReadCursor *r = &LWM2M_TLV_CTX(work)->r;
    const uint8_t *buf = r->buf;
    const size_t len = r->len;
    Lwm2mTlv.ok = PROTO_FALSE;
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

    Lwm2mTlv.hdr.id_type = (Lwm2mTlvIdType)(type & LWM2M_TLV_IDTYPE_MASK);
    Lwm2mTlv.hdr.id = id;
    Lwm2mTlv.val.opaque = buf + p;
    Lwm2mTlv.val.len = value_len;
    LWM2M_TLV_CTX(work)->r.pos = p + value_len;
    Lwm2mTlv.ok = PROTO_TRUE;
}

// Read a Value as an Integer: 1, 2, 4 or 8 octets, network byte order, two's complement.
static void tlv_value_integer(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *value = Lwm2mTlv.val.opaque;
    const size_t len = Lwm2mTlv.val.len;
    Lwm2mTlv.ok = PROTO_FALSE;
    if (!value || (len != 1 && len != 2 && len != 4 && len != 8))
    {
        return;
    }
    int64_t v = (value[0] & 0x80) ? -1 : 0; // sign-extend from the most significant bit
    for (size_t i = 0; i < len; i++)
    {
        v = (int64_t)(((uint64_t)v << 8) | value[i]);
    }
    Lwm2mTlv.val.integer_value = v;
    Lwm2mTlv.ok = PROTO_TRUE;
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
                       .value_integer = tlv_value_integer};

#endif // PROTOCORE_ENABLE_LWM2M
