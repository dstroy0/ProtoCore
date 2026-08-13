// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snmp_ber.c
 * @brief The SNMP serialization (RFC 3417 sec 8, over ITU-T X.690) - implementation. See snmp_ber.h.
 */

#include "services/net/snmp/snmp_ber.h"

#if PROTOCORE_ENABLE_SNMP

/**
 * @brief The codec's calls - what SnmpBerNs points at.
 *
 * No storage member: both cursors are the caller's, so the module keeps nothing between calls.
 *
 * @var SnmpBerInternal::ns  the handle a caller sets a call's members on
 */
struct SnmpBerInternal
{
    SnmpBerNs *ns;
};

static struct SnmpBerInternal s_ber = {.ns = &SnmpBer};

// ---------------------------------------------------------------------------
// Encoder primitives. Each runs over the caller's cursor and touches no module
// state, so each takes that cursor rather than the handle.
// ---------------------------------------------------------------------------

static void enc_byte(BerEnc *e, uint8_t b)
{
    if (!e->ok)
    {
        return;
    }
    if (e->len >= e->cap)
    {
        e->ok = PROTO_FALSE;
        return;
    }
    e->buf[e->len++] = b;
}

static void enc_bytes(BerEnc *e, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        enc_byte(e, p[i]);
    }
}

// Definite length octets (RFC 3417 sec 8 item 1): short form under 128, else long form with a
// count byte and the length big-endian.
static void enc_len(BerEnc *e, size_t length)
{
    if (length < 0x80)
    {
        enc_byte(e, (uint8_t)length);
        return;
    }
    uint8_t tmp[4];
    int k = 0;
    size_t v = length;
    while (v)
    {
        tmp[k++] = (uint8_t)(v & 0xFF);
        v >>= 8;
    }
    enc_byte(e, (uint8_t)(0x80 | k));
    for (int i = k - 1; i >= 0; i--)
    {
        enc_byte(e, tmp[i]);
    }
}

// One primitive TLV: identifier octet, definite length, value.
static void enc_tlv(BerEnc *e, uint8_t tag, const uint8_t *val, size_t n)
{
    enc_byte(e, tag);
    enc_len(e, n);
    enc_bytes(e, val, n);
}

// One subidentifier in base 128, big-endian, the high bit set on every octet but the last, into
// tmp[*t] bounded by cap. An overrun latches the cursor as full.
static void oid_emit_arc(uint8_t *tmp, size_t cap, size_t *t, uint32_t v, BerEnc *e)
{
    uint8_t b[5];
    int k = 0;
    do
    {
        b[k++] = (uint8_t)(v & 0x7F);
        v >>= 7;
    } while (v);
    for (int i = k - 1; i >= 0; i--)
    {
        uint8_t byte = b[i];
        if (i != 0)
        {
            byte |= 0x80;
        }
        if (*t < cap)
        {
            tmp[(*t)++] = byte;
        }
        else
        {
            e->ok = PROTO_FALSE;
        }
    }
}

// ---------------------------------------------------------------------------
// Encoder calls
// ---------------------------------------------------------------------------

static void enc_init(struct SnmpBerInternal *restrict ctx)
{
    BerEnc *e = ctx->ns->enc;
    e->buf = ctx->ns->buf.out;
    e->cap = ctx->ns->buf.cap;
    e->len = 0;
    e->ok = (ctx->ns->buf.out != NULL && ctx->ns->buf.cap > 0);
    ctx->ns->ok = e->ok;
}

// INTEGER in two's complement, minimal: drop leading 0x00 and 0xFF octets that the next octet's
// sign bit already carries.
static void put_integer(struct SnmpBerInternal *restrict ctx)
{
    BerEnc *e = ctx->ns->enc;
    uint8_t tmp[8];
    int k = 0;
    long val = ctx->ns->tlv.ival;
    do
    {
        tmp[k++] = (uint8_t)(val & 0xFF);
        val >>= 8;
    } while (!((val == 0 && !(tmp[k - 1] & 0x80)) || (val == -1 && (tmp[k - 1] & 0x80))) && k < (int)sizeof(tmp));

    enc_byte(e, (uint8_t)SNMP_TAG_BER_INTEGER);
    enc_len(e, (size_t)k);
    for (int i = k - 1; i >= 0; i--)
    {
        enc_byte(e, tmp[i]);
    }
    ctx->ns->ok = e->ok;
}

// A non-negative application-type value (RFC 2578 sec 7.1.6 through 7.1.8): big-endian, minimal,
// with a leading 0x00 when the top bit would otherwise read as a sign.
static void put_uint(struct SnmpBerInternal *restrict ctx)
{
    BerEnc *e = ctx->ns->enc;
    uint8_t tmp[5];
    int k = 0;
    uint32_t val = ctx->ns->tlv.uval;
    do
    {
        tmp[k++] = (uint8_t)(val & 0xFF);
        val >>= 8;
        // val is 32 bits wide, so it reaches 0 by the fourth shift; k caps the tmp[] write.
    } while (val && k < 4);
    if (tmp[k - 1] & 0x80)
    {
        tmp[k++] = 0x00;
    }

    enc_byte(e, ctx->ns->tlv.tag);
    enc_len(e, (size_t)k);
    for (int i = k - 1; i >= 0; i--)
    {
        enc_byte(e, tmp[i]);
    }
    ctx->ns->ok = e->ok;
}

static void put_octet_string(struct SnmpBerInternal *restrict ctx)
{
    enc_tlv(ctx->ns->enc, ctx->ns->tlv.tag, ctx->ns->tlv.bytes, ctx->ns->tlv.len);
    ctx->ns->ok = ctx->ns->enc->ok;
}

static void put_null(struct SnmpBerInternal *restrict ctx)
{
    BerEnc *e = ctx->ns->enc;
    enc_byte(e, (uint8_t)SNMP_TAG_BER_NULL);
    enc_byte(e, 0x00);
    ctx->ns->ok = e->ok;
}

// OBJECT IDENTIFIER: the first two subidentifiers combine as 40 * arc0 + arc1, the rest follow
// one base-128 group each.
static void put_oid(struct SnmpBerInternal *restrict ctx)
{
    BerEnc *e = ctx->ns->enc;
    const uint32_t *arcs = ctx->ns->tlv.arcs;
    const size_t n = ctx->ns->tlv.arc_count;
    if (n < 2)
    {
        e->ok = PROTO_FALSE;
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    uint8_t tmp[SNMP_MAX_OID_LEN * 5];
    size_t t = 0;
    oid_emit_arc(tmp, sizeof(tmp), &t, 40u * arcs[0] + arcs[1], e);
    for (size_t i = 2; i < n; i++)
    {
        oid_emit_arc(tmp, sizeof(tmp), &t, arcs[i], e);
    }
    enc_tlv(e, (uint8_t)SNMP_TAG_BER_OID, tmp, t);
    ctx->ns->ok = e->ok;
}

static void put_tlv(struct SnmpBerInternal *restrict ctx)
{
    enc_tlv(ctx->ns->enc, ctx->ns->tlv.tag, ctx->ns->tlv.bytes, ctx->ns->tlv.len);
    ctx->ns->ok = ctx->ns->enc->ok;
}

static void put_raw(struct SnmpBerInternal *restrict ctx)
{
    enc_bytes(ctx->ns->enc, ctx->ns->tlv.bytes, ctx->ns->tlv.len);
    ctx->ns->ok = ctx->ns->enc->ok;
}

// Open a constructed type: identifier octet, then a definite-long length of two octets reserved at
// tlv.token. RFC 3417 sec 8 item 1 permits more length octets than the minimum, so the reservation
// is a valid encoding whatever the content turns out to measure.
static void seq_begin(struct SnmpBerInternal *restrict ctx)
{
    BerEnc *e = ctx->ns->enc;
    enc_byte(e, ctx->ns->tlv.tag);
    ctx->ns->tlv.token = e->len;
    enc_byte(e, 0x82);
    enc_byte(e, 0x00);
    enc_byte(e, 0x00);
    ctx->ns->ok = e->ok;
}

static void seq_end(struct SnmpBerInternal *restrict ctx)
{
    BerEnc *e = ctx->ns->enc;
    const size_t token = ctx->ns->tlv.token;
    if (!e->ok)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    size_t content = e->len - (token + 3);
    if (content > 0xFFFF)
    {
        e->ok = PROTO_FALSE;
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    e->buf[token] = 0x82;
    e->buf[token + 1] = (uint8_t)((content >> 8) & 0xFF);
    e->buf[token + 2] = (uint8_t)(content & 0xFF);
    ctx->ns->ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

static void dec_init(struct SnmpBerInternal *restrict ctx)
{
    BerDec *d = ctx->ns->dec;
    d->buf = ctx->ns->buf.in;
    d->len = ctx->ns->buf.cap;
    d->pos = 0;
    d->ok = (ctx->ns->buf.in != NULL);
    ctx->ns->ok = d->ok;
}

// Identifier octet then definite length, leaving the cursor at the value. Runs over the caller's
// cursor and touches no module state, so the three reads below share it directly.
static proto_bool dec_header(BerDec *d, uint8_t *tag, size_t *length)
{
    if (!d->ok || d->pos >= d->len)
    {
        d->ok = PROTO_FALSE;
        return PROTO_FALSE;
    }
    *tag = d->buf[d->pos++];

    if (d->pos >= d->len)
    {
        d->ok = PROTO_FALSE;
        return PROTO_FALSE;
    }
    uint8_t l0 = d->buf[d->pos++];
    size_t length_val;
    if (l0 < 0x80)
    {
        length_val = l0;
    }
    else
    {
        int k = l0 & 0x7F;
        if (k == 0 || k > 4 || d->pos + (size_t)k > d->len)
        {
            d->ok = PROTO_FALSE; // indefinite form (k == 0) is prohibited by RFC 3417 sec 8 item 1
            return PROTO_FALSE;
        }
        length_val = 0;
        for (int i = 0; i < k; i++)
        {
            length_val = (length_val << 8) | d->buf[d->pos++];
        }
    }
    // Compare against the octets left rather than d->pos + length_val: a four-octet long-form
    // length reaches 0xFFFFFFFF, and that sum wraps on a 32-bit target and slips under d->len.
    // d->pos <= d->len holds here, bounded by the count-byte check above.
    if (length_val > d->len - d->pos)
    {
        d->ok = PROTO_FALSE;
        return PROTO_FALSE;
    }
    *length = length_val;
    return PROTO_TRUE;
}

static void read_header(struct SnmpBerInternal *restrict ctx)
{
    uint8_t tag = 0;
    size_t length = 0;
    ctx->ns->ok = dec_header(ctx->ns->dec, &tag, &length);
    ctx->ns->tag = tag;
    ctx->ns->vlen = length;
}

// INTEGER, sign-extended from the first octet. The accumulator is unsigned because shifting a
// negative signed value left is undefined; the final cast yields the two's-complement value.
static void read_integer(struct SnmpBerInternal *restrict ctx)
{
    BerDec *d = ctx->ns->dec;
    uint8_t tag;
    size_t len;
    if (!dec_header(d, &tag, &len) || tag != (uint8_t)SNMP_TAG_BER_INTEGER || len == 0 || len > 8)
    {
        d->ok = PROTO_FALSE;
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    uint64_t uv = (d->buf[d->pos] & 0x80) ? ~(uint64_t)0 : 0;
    for (size_t i = 0; i < len; i++)
    {
        uv = (uv << 8) | d->buf[d->pos + i];
    }
    d->pos += len;
    ctx->ns->ival = (long)uv;
    ctx->ns->ok = PROTO_TRUE;
}

// OBJECT IDENTIFIER: each subidentifier is base 128 with the high bit as a continuation flag. The
// first subidentifier is itself multi-octet capable and encodes 40 * arc0 + arc1, where arc0 is
// 0 through 2 and the remainder is arc1, which may exceed 39.
static void read_oid(struct SnmpBerInternal *restrict ctx)
{
    BerDec *d = ctx->ns->dec;
    uint32_t *arcs = ctx->ns->read.arc_out;
    const size_t max = ctx->ns->read.arc_cap;
    uint8_t tag;
    size_t len;
    if (!dec_header(d, &tag, &len) || tag != (uint8_t)SNMP_TAG_BER_OID || len == 0 || max < 2)
    {
        d->ok = PROTO_FALSE;
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    const uint8_t *p = d->buf + d->pos;
    size_t count = 0;
    uint32_t acc = 0;
    proto_bool first_done = PROTO_FALSE;
    for (size_t i = 0; i < len; i++)
    {
        acc = (acc << 7) | (uint32_t)(p[i] & 0x7F);
        if (p[i] & 0x80)
        {
            continue;
        }
        if (!first_done)
        {
            uint32_t arc0 = acc / 40u;
            if (arc0 > 2u)
            {
                arc0 = 2u;
            }
            arcs[count++] = arc0; // count is 0 here and max is at least 2
            arcs[count++] = acc - 40u * arc0;
            first_done = PROTO_TRUE;
        }
        else
        {
            if (count >= max)
            {
                d->ok = PROTO_FALSE;
                ctx->ns->ok = PROTO_FALSE;
                return;
            }
            arcs[count++] = acc;
        }
        acc = 0;
    }
    d->pos += len;
    ctx->ns->n = count;
    ctx->ns->ok = PROTO_TRUE;
}

static void skip(struct SnmpBerInternal *restrict ctx)
{
    BerDec *d = ctx->ns->dec;
    const size_t length = ctx->ns->read.skip;
    if (!d->ok || d->pos > d->len || length > d->len - d->pos) // wrap-safe, as in dec_header
    {
        d->ok = PROTO_FALSE;
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    d->pos += length;
    ctx->ns->ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
SnmpBerNs SnmpBer = {.enc_init = enc_init,
                     .put_integer = put_integer,
                     .put_uint = put_uint,
                     .put_octet_string = put_octet_string,
                     .put_null = put_null,
                     .put_oid = put_oid,
                     .put_tlv = put_tlv,
                     .put_raw = put_raw,
                     .seq_begin = seq_begin,
                     .seq_end = seq_end,
                     .dec_init = dec_init,
                     .read_header = read_header,
                     .read_integer = read_integer,
                     .read_oid = read_oid,
                     .skip = skip,
                     .internal = &s_ber};

#endif // PROTOCORE_ENABLE_SNMP
