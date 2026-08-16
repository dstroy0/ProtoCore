// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ikev2.c
 * @brief IKEv2 (RFC 7296): the wire codec, the key schedule, and the handshake driver - see ikev2.h.
 *
 * Each call reads its arguments off ::Ike and leaves its outcome there. The framing helpers keep
 * natural arguments where one call composes another: sa_init_build lays four payloads down, the
 * handshake driver lays whole messages down, and a shared cursor would collide.
 */

#include "services/security/ikev2/ikev2.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_IKEV2

#include "crypto/aead/aesgcm.h"           // the Encrypted payload's AEAD (RFC 5282)
#include "crypto/asymmetric/curve25519.h" // Diffie-Hellman Group Num 31 (RFC 8031)
#include "crypto/asymmetric/ecdsa.h"      // ECDSA-P256 AUTH (RFC 7427 sec 3)
#include "crypto/asymmetric/rsa.h"        // RSA AUTH verify (RFC 7296 sec 3.8)
#include "crypto/hash/sha256.h"           // the COOKIE hash (RFC 7296 sec 2.6)
#include "crypto/mac/hmac_sha256.h"       // the PRF: PRF_HMAC_SHA2_256 (RFC 4868 sec 4)
#include "mmgr/secure.h"                  // the per-call AEAD context borrow

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

// Octet layout of an SK-framed message (RFC 7296 sec 3.14 with RFC 5282 sec 3): [0,28) IKE header,
// [28,32) the Encrypted payload's generic header, [32,40) IV, then Ciphertext and its 16-octet ICV.
// The associated data is [0,32) (RFC 5282 sec 5.1).
static const size_t IKE_SK_HDR_OFF = PROTOCORE_IKE_HDR_LEN;
static const size_t IKE_SK_IV_OFF = PROTOCORE_IKE_HDR_LEN + PROTOCORE_IKE_PAYLOAD_HDR_LEN;
static const size_t IKE_SK_CT_OFF = PROTOCORE_IKE_HDR_LEN + PROTOCORE_IKE_PAYLOAD_HDR_LEN + PROTOCORE_IKE_GCM_IV_LEN;

// ---------------------------------------------------------------------------
// The handle's state
// ---------------------------------------------------------------------------

/**
 * @brief The calls that reach this handle - what IkeNs points at.
 *
 * @var IkeInternal::ns  the handle a caller sets a call's members on
 */
struct IkeInternal
{
    IkeNs *ns;
};

static struct IkeInternal s_ike = {.ns = &Ike};

// ---------------------------------------------------------------------------
// Big-endian scalars and the generic payload header (RFC 7296 sec 3.1, sec 3.2)
// ---------------------------------------------------------------------------

static inline void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static inline void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static inline uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static inline uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// Write Next Payload, a clear C bit and RESERVED, and Payload Length; false when it does not fit.
static proto_bool put_pl_hdr(uint8_t *buf, size_t cap, IkePayloadType next_payload, size_t total_len)
{
    if (total_len > 0xFFFF || cap < total_len)
    {
        return PROTO_FALSE;
    }
    buf[0] = (uint8_t)next_payload;
    buf[1] = 0x00;
    put16(buf + 2, (uint16_t)total_len);
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// IKE header (RFC 7296 sec 3.1)
// ---------------------------------------------------------------------------

static size_t ike_hdr_build(uint8_t *buf, size_t cap, const IkeHeader *h)
{
    if (!buf || !h || cap < PROTOCORE_IKE_HDR_LEN)
    {
        return 0;
    }
    mem.cpy(buf, h->init_spi, PROTOCORE_IKE_SPI_LEN);
    mem.cpy(buf + 8, h->resp_spi, PROTOCORE_IKE_SPI_LEN);
    buf[16] = (uint8_t)h->next_payload;
    buf[17] = h->version;
    buf[18] = (uint8_t)h->exchange;
    buf[19] = h->flags;
    put32(buf + 20, h->message_id);
    put32(buf + 24, h->length);
    return PROTOCORE_IKE_HDR_LEN;
}

static proto_bool ike_hdr_parse(const uint8_t *buf, size_t len, IkeHeader *out)
{
    mem.set(out, 0, sizeof(*out));
    if (!buf || len < PROTOCORE_IKE_HDR_LEN)
    {
        return PROTO_FALSE;
    }
    mem.cpy(out->init_spi, buf, PROTOCORE_IKE_SPI_LEN);
    mem.cpy(out->resp_spi, buf + 8, PROTOCORE_IKE_SPI_LEN);
    out->next_payload = (IkePayloadType)buf[16];
    out->version = buf[17];
    out->exchange = (IkeExchange)buf[18];
    out->flags = buf[19];
    out->message_id = get32(buf + 20);
    out->length = get32(buf + 24);
    return PROTO_TRUE;
}

// Length sits at octets 24..27 of the header.
static proto_bool ike_set_length(uint8_t *buf, size_t buf_cap, uint32_t total_len)
{
    if (!buf || buf_cap < PROTOCORE_IKE_HDR_LEN)
    {
        return PROTO_FALSE;
    }
    put32(buf + 24, total_len);
    return PROTO_TRUE;
}

static void hdr_build(struct IkeInternal *restrict ctx)
{
    ctx->ns->n = ike_hdr_build(ctx->ns->out.buf, ctx->ns->out.cap, &ctx->ns->hdr);
}

static void hdr_parse(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = ike_hdr_parse(ctx->ns->wire.msg, ctx->ns->wire.len, &ctx->ns->hdr);
}

static void set_length(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = ike_set_length(ctx->ns->out.buf, ctx->ns->out.cap, ctx->ns->msg.length);
}

// ---------------------------------------------------------------------------
// The payload chain (RFC 7296 sec 3.2)
// ---------------------------------------------------------------------------

static void ike_payload_iter_init(IkePayloadIter *it, IkePayloadType first_type, const uint8_t *area, size_t area_len)
{
    if (!it)
    {
        return;
    }
    it->area = area;
    it->len = area_len;
    it->off = 0;
    it->next_type = first_type;
}

// Payload Length counts the generic header, so a value under 4 or past the area is malformed.
static proto_bool ike_payload_next(IkePayloadIter *it, IkePayload *out)
{
    out->type = IKE_PL_NONE;
    out->next_payload = IKE_PL_NONE;
    out->critical = PROTO_FALSE;
    out->body = NULL;
    out->body_len = 0;
    if (!it || !it->area || it->next_type == IKE_PL_NONE)
    {
        return PROTO_FALSE;
    }
    if (it->off + PROTOCORE_IKE_PAYLOAD_HDR_LEN > it->len)
    {
        return PROTO_FALSE;
    }
    const uint8_t *p = it->area + it->off;
    uint8_t next = p[0];
    proto_bool critical = (p[1] & PROTOCORE_IKE_CRITICAL) != 0;
    uint16_t plen = get16(p + 2);
    if (plen < PROTOCORE_IKE_PAYLOAD_HDR_LEN || it->off + plen > it->len)
    {
        return PROTO_FALSE;
    }
    out->type = it->next_type;
    out->next_payload = (IkePayloadType)next;
    out->critical = critical;
    out->body = p + PROTOCORE_IKE_PAYLOAD_HDR_LEN;
    out->body_len = (size_t)plen - PROTOCORE_IKE_PAYLOAD_HDR_LEN;
    it->next_type = (IkePayloadType)next;
    it->off += plen;
    return PROTO_TRUE;
}

static void payload_iter_init(struct IkeInternal *restrict ctx)
{
    ike_payload_iter_init(ctx->ns->walk.chain, ctx->ns->walk.first_type, ctx->ns->wire.msg, ctx->ns->wire.len);
}

static void payload_next(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = ike_payload_next(ctx->ns->walk.chain, &ctx->ns->payload);
}

static void payload_build(struct IkeInternal *restrict ctx)
{
    uint8_t *buf = ctx->ns->out.buf;
    const uint8_t *body = ctx->ns->pl.data;
    size_t body_len = ctx->ns->pl.data_len;
    ctx->ns->n = 0;
    if (!buf || (body_len && !body))
    {
        return;
    }
    size_t total = PROTOCORE_IKE_PAYLOAD_HDR_LEN + body_len;
    if (total > 0xFFFF || ctx->ns->out.cap < total)
    {
        return;
    }
    buf[0] = (uint8_t)ctx->ns->pl.next_payload;
    buf[1] = ctx->ns->pl.critical ? PROTOCORE_IKE_CRITICAL : 0x00;
    put16(buf + 2, (uint16_t)total);
    if (body_len)
    {
        mem.cpy(buf + PROTOCORE_IKE_PAYLOAD_HDR_LEN, body, body_len);
    }
    ctx->ns->n = total;
}

// ---------------------------------------------------------------------------
// Typed payload builders (RFC 7296 sec 3.3 through sec 3.15)
// ---------------------------------------------------------------------------

// SA payload holding one Proposal Substructure: Last Substruc 0, its transforms with Last Substruc 3
// on all but the last, and a Key Length attribute in TV form where one is asked for (sec 3.3.5).
static size_t ike_sa_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, uint8_t proposal_num,
                           IkeProtocol protocol_id, const uint8_t *spi, uint8_t spi_size,
                           const IkeTransform *transforms, uint8_t num_transforms)
{
    if (!buf || !transforms || num_transforms == 0)
    {
        return 0;
    }
    if (spi_size && !spi)
    {
        return 0;
    }

    const size_t prop_hdr = 8; // Last Substruc, RESERVED, Proposal Length, Num, Protocol, SPI Size, Num Transforms
    size_t off = PROTOCORE_IKE_PAYLOAD_HDR_LEN + prop_hdr + spi_size;
    if (cap < off)
    {
        return 0;
    }

    size_t tstart = off;
    for (uint8_t i = 0; i < num_transforms; i++)
    {
        proto_bool has_key = transforms[i].key_length >= 0;
        size_t tlen = 8 + (has_key ? 4 : 0);
        if (off + tlen > cap)
        {
            return 0;
        }
        buf[off + 0] = (i + 1 == num_transforms) ? 0 : 3; // Last Substruc: 0 last, 3 more transforms
        buf[off + 1] = 0;
        put16(buf + off + 2, (uint16_t)tlen);
        buf[off + 4] = (uint8_t)transforms[i].type;
        buf[off + 5] = 0;
        put16(buf + off + 6, transforms[i].id);
        if (has_key)
        {
            put16(buf + off + 8, (uint16_t)(0x8000u | IKE_ATTR_KEY_LENGTH)); // AF set: TV form
            put16(buf + off + 10, (uint16_t)transforms[i].key_length);
        }
        off += tlen;
    }

    size_t prop_len = prop_hdr + spi_size + (off - tstart);
    size_t sa_total = PROTOCORE_IKE_PAYLOAD_HDR_LEN + prop_len;
    if (prop_len > 0xFFFF || sa_total > 0xFFFF)
    {
        return 0;
    }

    buf[0] = (uint8_t)next_payload;
    buf[1] = 0;
    put16(buf + 2, (uint16_t)sa_total);
    uint8_t *pr = buf + PROTOCORE_IKE_PAYLOAD_HDR_LEN;
    pr[0] = 0; // Last Substruc: the only proposal
    pr[1] = 0;
    put16(pr + 2, (uint16_t)prop_len);
    pr[4] = proposal_num;
    pr[5] = (uint8_t)protocol_id;
    pr[6] = spi_size;
    pr[7] = num_transforms;
    if (spi_size)
    {
        mem.cpy(pr + prop_hdr, spi, spi_size);
    }
    return sa_total;
}

// KE payload: Diffie-Hellman Group Num, RESERVED, then Key Exchange Data (sec 3.4).
static size_t ike_ke_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, uint16_t dh_group,
                           const uint8_t *data, size_t data_len)
{
    if (!buf || (data_len && !data))
    {
        return 0;
    }
    size_t total = PROTOCORE_IKE_PAYLOAD_HDR_LEN + 4 + data_len;
    if (!put_pl_hdr(buf, cap, next_payload, total))
    {
        return 0;
    }
    put16(buf + 4, dh_group);
    buf[6] = 0;
    buf[7] = 0;
    if (data_len)
    {
        mem.cpy(buf + 8, data, data_len);
    }
    return total;
}

// Nonce payload: the generic header then Nonce Data (sec 3.9).
static size_t ike_nonce_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, const uint8_t *nonce,
                              size_t nonce_len)
{
    if (!buf || (nonce_len && !nonce))
    {
        return 0;
    }
    size_t total = PROTOCORE_IKE_PAYLOAD_HDR_LEN + nonce_len;
    if (!put_pl_hdr(buf, cap, next_payload, total))
    {
        return 0;
    }
    if (nonce_len)
    {
        mem.cpy(buf + 4, nonce, nonce_len);
    }
    return total;
}

// ID payload: ID Type, three RESERVED octets, then Identification Data (sec 3.5).
static size_t ike_id_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, IkeIdType id_type,
                           const uint8_t *data, size_t data_len)
{
    if (!buf || (data_len && !data))
    {
        return 0;
    }
    size_t total = PROTOCORE_IKE_PAYLOAD_HDR_LEN + 4 + data_len;
    if (!put_pl_hdr(buf, cap, next_payload, total))
    {
        return 0;
    }
    buf[4] = (uint8_t)id_type;
    buf[5] = 0;
    buf[6] = 0;
    buf[7] = 0;
    if (data_len)
    {
        mem.cpy(buf + 8, data, data_len);
    }
    return total;
}

// AUTH payload: Auth Method, three RESERVED octets, then Authentication Data (sec 3.8).
static size_t ike_auth_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, IkeAuthMethod auth_method,
                             const uint8_t *data, size_t data_len)
{
    if (!buf || (data_len && !data))
    {
        return 0;
    }
    size_t total = PROTOCORE_IKE_PAYLOAD_HDR_LEN + 4 + data_len;
    if (!put_pl_hdr(buf, cap, next_payload, total))
    {
        return 0;
    }
    buf[4] = (uint8_t)auth_method;
    buf[5] = 0;
    buf[6] = 0;
    buf[7] = 0;
    if (data_len)
    {
        mem.cpy(buf + 8, data, data_len);
    }
    return total;
}

// Notify payload: Protocol ID, SPI Size, Notify Message Type, the SPI, then Notification Data (sec 3.10).
static size_t ike_notify_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, IkeProtocol protocol_id,
                               const uint8_t *spi, uint8_t spi_size, uint16_t notify_type, const uint8_t *data,
                               size_t data_len)
{
    if (!buf || (spi_size && !spi) || (data_len && !data))
    {
        return 0;
    }
    size_t total = PROTOCORE_IKE_PAYLOAD_HDR_LEN + 4 + spi_size + data_len;
    if (!put_pl_hdr(buf, cap, next_payload, total))
    {
        return 0;
    }
    buf[4] = (uint8_t)protocol_id;
    buf[5] = spi_size;
    put16(buf + 6, notify_type);
    size_t off = 8;
    if (spi_size)
    {
        mem.cpy(buf + off, spi, spi_size);
        off += spi_size;
    }
    if (data_len)
    {
        mem.cpy(buf + off, data, data_len);
    }
    return total;
}

static void sa_build(struct IkeInternal *restrict ctx)
{
    ctx->ns->n = ike_sa_build(ctx->ns->out.buf, ctx->ns->out.cap, ctx->ns->pl.next_payload, ctx->ns->prop.proposal_num,
                              ctx->ns->prop.protocol_id, ctx->ns->prop.spi, ctx->ns->prop.spi_size,
                              ctx->ns->prop.transforms, ctx->ns->prop.num_transforms);
}

static void ke_build(struct IkeInternal *restrict ctx)
{
    ctx->ns->n = ike_ke_build(ctx->ns->out.buf, ctx->ns->out.cap, ctx->ns->pl.next_payload, ctx->ns->ke.dh_group,
                              ctx->ns->pl.data, ctx->ns->pl.data_len);
}

static void nonce_build(struct IkeInternal *restrict ctx)
{
    ctx->ns->n = ike_nonce_build(ctx->ns->out.buf, ctx->ns->out.cap, ctx->ns->pl.next_payload, ctx->ns->pl.data,
                                 ctx->ns->pl.data_len);
}

static void id_build(struct IkeInternal *restrict ctx)
{
    ctx->ns->n = ike_id_build(ctx->ns->out.buf, ctx->ns->out.cap, ctx->ns->pl.next_payload, ctx->ns->id.id_type,
                              ctx->ns->pl.data, ctx->ns->pl.data_len);
}

static void auth_build(struct IkeInternal *restrict ctx)
{
    ctx->ns->n = ike_auth_build(ctx->ns->out.buf, ctx->ns->out.cap, ctx->ns->pl.next_payload, ctx->ns->auth.auth_method,
                                ctx->ns->pl.data, ctx->ns->pl.data_len);
}

// CERT and CERTREQ share the layout: Cert Encoding then the data (sec 3.6, sec 3.7).
static void cert_build(struct IkeInternal *restrict ctx)
{
    uint8_t *buf = ctx->ns->out.buf;
    const uint8_t *data = ctx->ns->pl.data;
    size_t data_len = ctx->ns->pl.data_len;
    ctx->ns->n = 0;
    if (!buf || (data_len && !data))
    {
        return;
    }
    size_t total = PROTOCORE_IKE_PAYLOAD_HDR_LEN + 1 + data_len;
    if (!put_pl_hdr(buf, ctx->ns->out.cap, ctx->ns->pl.next_payload, total))
    {
        return;
    }
    buf[4] = ctx->ns->id.cert_encoding;
    if (data_len)
    {
        mem.cpy(buf + 5, data, data_len);
    }
    ctx->ns->n = total;
}

static void notify_build(struct IkeInternal *restrict ctx)
{
    ctx->ns->n = ike_notify_build(ctx->ns->out.buf, ctx->ns->out.cap, ctx->ns->pl.next_payload,
                                  ctx->ns->prop.protocol_id, ctx->ns->prop.spi, ctx->ns->prop.spi_size,
                                  ctx->ns->notify.notify_type, ctx->ns->pl.data, ctx->ns->pl.data_len);
}

// Delete payload: Protocol ID, SPI Size, Num of SPIs, then the SPI list (sec 3.11).
static void delete_build(struct IkeInternal *restrict ctx)
{
    uint8_t *buf = ctx->ns->out.buf;
    uint8_t spi_size = ctx->ns->prop.spi_size;
    uint16_t num_spis = ctx->ns->prop.num_spis;
    const uint8_t *spis = ctx->ns->pl.data;
    ctx->ns->n = 0;
    if (!buf)
    {
        return;
    }
    size_t spis_len = (size_t)spi_size * num_spis;
    if (spis_len && !spis)
    {
        return;
    }
    size_t total = PROTOCORE_IKE_PAYLOAD_HDR_LEN + 4 + spis_len;
    if (!put_pl_hdr(buf, ctx->ns->out.cap, ctx->ns->pl.next_payload, total))
    {
        return;
    }
    buf[4] = (uint8_t)ctx->ns->prop.protocol_id;
    buf[5] = spi_size;
    put16(buf + 6, num_spis);
    if (spis_len)
    {
        mem.cpy(buf + 8, spis, spis_len);
    }
    ctx->ns->n = total;
}

// TS payload: Number of TSs, three RESERVED octets, then each Traffic Selector (sec 3.13, sec 3.13.1).
static void ts_build(struct IkeInternal *restrict ctx)
{
    uint8_t *buf = ctx->ns->out.buf;
    size_t cap = ctx->ns->out.cap;
    const IkeTrafficSelector *sels = ctx->ns->ts.sels;
    uint8_t num = ctx->ns->ts.num;
    ctx->ns->n = 0;
    if (!buf || !sels || num == 0)
    {
        return;
    }
    size_t off = PROTOCORE_IKE_PAYLOAD_HDR_LEN + 4;
    if (cap < off)
    {
        return;
    }
    for (uint8_t i = 0; i < num; i++)
    {
        const IkeTrafficSelector *s = &sels[i];
        if ((s->addr_len != 4 && s->addr_len != 16) || !s->start_addr || !s->end_addr)
        {
            return;
        }
        size_t sel_len = 8 + 2 * s->addr_len; // TS Type, IP Protocol ID, Selector Length, ports, addresses
        if (off + sel_len > cap)
        {
            return;
        }
        buf[off + 0] = (uint8_t)s->ts_type;
        buf[off + 1] = s->ip_protocol;
        put16(buf + off + 2, (uint16_t)sel_len);
        put16(buf + off + 4, s->start_port);
        put16(buf + off + 6, s->end_port);
        mem.cpy(buf + off + 8, s->start_addr, s->addr_len);
        mem.cpy(buf + off + 8 + s->addr_len, s->end_addr, s->addr_len);
        off += sel_len;
    }
    if (off > 0xFFFF)
    {
        return;
    }
    buf[0] = (uint8_t)ctx->ns->pl.next_payload;
    buf[1] = 0;
    put16(buf + 2, (uint16_t)off);
    buf[4] = num;
    buf[5] = 0;
    buf[6] = 0;
    buf[7] = 0;
    ctx->ns->n = off;
}

// CP payload: CFG Type, three RESERVED octets, then each attribute as type, length, value (sec 3.15.1).
static void cp_build(struct IkeInternal *restrict ctx)
{
    uint8_t *buf = ctx->ns->out.buf;
    const IkeCfgAttr *attrs = ctx->ns->cp.attrs;
    uint8_t num_attrs = ctx->ns->cp.num_attrs;
    ctx->ns->n = 0;
    if (!buf || (num_attrs && !attrs))
    {
        return;
    }
    size_t attrs_len = 0;
    for (uint8_t i = 0; i < num_attrs; i++)
    {
        if (attrs[i].value_len && !attrs[i].value)
        {
            return;
        }
        attrs_len += 4 + attrs[i].value_len;
    }
    size_t total = PROTOCORE_IKE_PAYLOAD_HDR_LEN + 4 + attrs_len;
    if (!put_pl_hdr(buf, ctx->ns->out.cap, ctx->ns->pl.next_payload, total))
    {
        return;
    }
    buf[4] = (uint8_t)ctx->ns->cp.cfg_type;
    buf[5] = 0;
    buf[6] = 0;
    buf[7] = 0;
    size_t off = 8;
    for (uint8_t i = 0; i < num_attrs; i++)
    {
        put16(buf + off, (uint16_t)(attrs[i].type & 0x7FFF)); // the reserved high bit stays clear
        put16(buf + off + 2, attrs[i].value_len);
        off += 4;
        if (attrs[i].value_len)
        {
            mem.cpy(buf + off, attrs[i].value, attrs[i].value_len);
            off += attrs[i].value_len;
        }
    }
    ctx->ns->n = total;
}

// Encrypted payload envelope: the generic header, then IV, Ciphertext and Integrity Checksum Data
// laid end to end (sec 3.14 as RFC 5282 sec 3 rewrites it). The AEAD itself is sk_aead_seal's.
static void sk_build(struct IkeInternal *restrict ctx)
{
    uint8_t *buf = ctx->ns->out.buf;
    const uint8_t *iv = ctx->ns->sk.iv;
    size_t iv_len = ctx->ns->sk.iv_len;
    const uint8_t *ct = ctx->ns->sk.ct;
    size_t ct_len = ctx->ns->sk.ct_len;
    const uint8_t *icv = ctx->ns->sk.icv;
    size_t icv_len = ctx->ns->sk.icv_len;
    ctx->ns->n = 0;
    if (!buf || (iv_len && !iv) || (ct_len && !ct) || (icv_len && !icv))
    {
        return;
    }
    size_t total = PROTOCORE_IKE_PAYLOAD_HDR_LEN + iv_len + ct_len + icv_len;
    if (!put_pl_hdr(buf, ctx->ns->out.cap, ctx->ns->pl.next_payload, total))
    {
        return;
    }
    size_t off = PROTOCORE_IKE_PAYLOAD_HDR_LEN;
    if (iv_len)
    {
        mem.cpy(buf + off, iv, iv_len);
        off += iv_len;
    }
    if (ct_len)
    {
        mem.cpy(buf + off, ct, ct_len);
        off += ct_len;
    }
    if (icv_len)
    {
        mem.cpy(buf + off, icv, icv_len);
    }
    ctx->ns->n = total;
}

// ---------------------------------------------------------------------------
// Message fragmentation (RFC 7383)
// ---------------------------------------------------------------------------

// Encrypted Fragment payload: Fragment Number and Total Fragments, then IV, Ciphertext and ICV
// (RFC 7383 sec 2.5). Both counters are non-zero and Fragment Number is at most Total Fragments.
static void skf_build(struct IkeInternal *restrict ctx)
{
    uint8_t *buf = ctx->ns->out.buf;
    uint16_t frag_num = ctx->ns->frag.frag_num;
    uint16_t total = ctx->ns->frag.total;
    const uint8_t *iv = ctx->ns->sk.iv;
    size_t iv_len = ctx->ns->sk.iv_len;
    const uint8_t *ct = ctx->ns->sk.ct;
    size_t ct_len = ctx->ns->sk.ct_len;
    const uint8_t *icv = ctx->ns->sk.icv;
    size_t icv_len = ctx->ns->sk.icv_len;
    ctx->ns->n = 0;
    if (!buf || (iv_len && !iv) || (ct_len && !ct) || (icv_len && !icv))
    {
        return;
    }
    if (frag_num == 0 || total == 0 || frag_num > total)
    {
        return;
    }
    size_t body = PROTOCORE_IKE_PAYLOAD_HDR_LEN + 4 + iv_len + ct_len + icv_len;
    if (!put_pl_hdr(buf, ctx->ns->out.cap, ctx->ns->pl.next_payload, body))
    {
        return;
    }
    put16(buf + PROTOCORE_IKE_PAYLOAD_HDR_LEN, frag_num);
    put16(buf + PROTOCORE_IKE_PAYLOAD_HDR_LEN + 2, total);
    size_t off = PROTOCORE_IKE_PAYLOAD_HDR_LEN + 4;
    if (iv_len)
    {
        mem.cpy(buf + off, iv, iv_len);
        off += iv_len;
    }
    if (ct_len)
    {
        mem.cpy(buf + off, ct, ct_len);
        off += ct_len;
    }
    if (icv_len)
    {
        mem.cpy(buf + off, icv, icv_len);
    }
    ctx->ns->n = body;
}

static void skf_parse(struct IkeInternal *restrict ctx)
{
    const uint8_t *body = ctx->ns->wire.msg;
    size_t body_len = ctx->ns->wire.len;
    size_t iv_len = ctx->ns->sk.iv_len;
    size_t icv_len = ctx->ns->sk.icv_len;
    mem.set(&ctx->ns->sk_ref, 0, sizeof(ctx->ns->sk_ref));
    ctx->ns->ok = PROTO_FALSE;
    if (!body || body_len < 4 + iv_len + icv_len)
    {
        return;
    }
    uint16_t fn = get16(body);
    uint16_t tf = get16(body + 2);
    if (fn == 0 || tf == 0 || fn > tf) // RFC 7383 sec 2.6: both non-zero, Fragment Number at most Total
    {
        return;
    }
    ctx->ns->sk_ref.frag_num = fn;
    ctx->ns->sk_ref.total = tf;
    ctx->ns->sk_ref.iv = body + 4;
    ctx->ns->sk_ref.ct = body + 4 + iv_len;
    ctx->ns->sk_ref.ct_len = body_len - 4 - iv_len - icv_len;
    ctx->ns->sk_ref.icv = body + body_len - icv_len;
    ctx->ns->ok = PROTO_TRUE;
}

static proto_bool ike_frag_reasm_complete(const IkeFragReasm *r)
{
    return r && r->total != 0 && r->count == r->total;
}

static void frag_reasm_init(struct IkeInternal *restrict ctx)
{
    IkeFragReasm *r = ctx->ns->frag.reasm;
    if (!r)
    {
        return;
    }
    r->total = 0;
    r->count = 0;
    mem.set(r->present, 0, sizeof(r->present));
    r->pool = ctx->ns->out.buf;
    r->pool_cap = ctx->ns->out.cap;
    r->pool_used = 0;
}

// RFC 7383 sec 2.6: reject a zero counter, a Fragment Number past Total, a Total that disagrees with
// the fragments already queued, and a replay of a stored Fragment Number.
static void frag_reasm_add(struct IkeInternal *restrict ctx)
{
    IkeFragReasm *r = ctx->ns->frag.reasm;
    uint16_t frag_num = ctx->ns->frag.frag_num;
    uint16_t total = ctx->ns->frag.total;
    const uint8_t *chunk = ctx->ns->frag.chunk;
    size_t len = ctx->ns->frag.chunk_len;
    ctx->ns->ok = PROTO_FALSE;
    if (!r || !r->pool || (len && !chunk))
    {
        return;
    }
    if (frag_num == 0 || total == 0 || frag_num > total || total > PROTOCORE_IKE_FRAG_MAX)
    {
        return;
    }
    if (r->total == 0)
    {
        r->total = total;
    }
    else if (r->total != total)
    {
        return;
    }
    uint16_t idx = (uint16_t)(frag_num - 1);
    if (r->present[idx])
    {
        return;
    }
    if (len > r->pool_cap - r->pool_used)
    {
        return;
    }
    if (len)
    {
        mem.cpy(r->pool + r->pool_used, chunk, len);
    }
    r->off[idx] = r->pool_used;
    r->len[idx] = len;
    r->present[idx] = PROTO_TRUE;
    r->pool_used += len;
    r->count++;
    ctx->ns->ok = PROTO_TRUE;
}

static void frag_reasm_complete(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = ike_frag_reasm_complete(ctx->ns->frag.reasm);
}

// Merge the staged contents 1..Total into one Encrypted payload content (RFC 7383 sec 2.6).
static void frag_reasm_assemble(struct IkeInternal *restrict ctx)
{
    const IkeFragReasm *r = ctx->ns->frag.reasm;
    uint8_t *out = ctx->ns->out.buf;
    ctx->ns->n = 0;
    if (!ike_frag_reasm_complete(r) || !out)
    {
        return;
    }
    size_t off = 0;
    for (uint16_t i = 0; i < r->total; i++)
    {
        if (off + r->len[i] > ctx->ns->out.cap)
        {
            return;
        }
        if (r->len[i])
        {
            mem.cpy(out + off, r->pool + r->off[i], r->len[i]);
        }
        off += r->len[i];
    }
    ctx->ns->n = off;
}

// ---------------------------------------------------------------------------
// Stateless COOKIE (RFC 7296 sec 2.6)
// ---------------------------------------------------------------------------

// Cookie = VersionIDofSecret | Hash(Ni | IPi | SPIi | secret), the hash SHA-256.
static size_t ike_cookie_compute(uint8_t *work, uint8_t version, const uint8_t *secret, size_t secret_len,
                                 const uint8_t *ni, size_t ni_len, const uint8_t *ipi, size_t ipi_len,
                                 const uint8_t *spii, uint8_t *out, size_t out_cap)
{
    if (!out || out_cap < PROTOCORE_IKE_COOKIE_LEN || !spii)
    {
        return 0;
    }
    if ((ni_len && !ni) || (ipi_len && !ipi) || (secret_len && !secret))
    {
        return 0;
    }
    Sha256.init(work);
    if (ni_len)
    {
        Sha256.update_args.data = ni;
        Sha256.update_args.len = ni_len;
        Sha256.update(work);
    }
    if (ipi_len)
    {
        Sha256.update_args.data = ipi;
        Sha256.update_args.len = ipi_len;
        Sha256.update(work);
    }
    Sha256.update_args.data = spii;
    Sha256.update_args.len = PROTOCORE_IKE_SPI_LEN;
    Sha256.update(work);
    if (secret_len)
    {
        Sha256.update_args.data = secret;
        Sha256.update_args.len = secret_len;
        Sha256.update(work);
    }
    out[0] = version;
    Sha256.final_args.out = out + 1;
    Sha256.final(work);
    return PROTOCORE_IKE_COOKIE_LEN;
}

static void cookie_compute(struct IkeInternal *restrict ctx)
{
    ctx->ns->n =
        ike_cookie_compute(ctx->ns->work, ctx->ns->notify.version, ctx->ns->notify.secret, ctx->ns->notify.secret_len,
                           ctx->ns->notify.ni, ctx->ns->notify.ni_len, ctx->ns->notify.ipi, ctx->ns->notify.ipi_len,
                           ctx->ns->notify.spii, ctx->ns->out.buf, ctx->ns->out.cap);
}

// The VersionIDofSecret octet names which secret to recompute with, so it comes off the cookie itself.
static void cookie_verify(struct IkeInternal *restrict ctx)
{
    const uint8_t *cookie = ctx->ns->notify.cookie;
    ctx->ns->ok = PROTO_FALSE;
    if (!cookie || ctx->ns->notify.cookie_len != PROTOCORE_IKE_COOKIE_LEN)
    {
        return;
    }
    uint8_t expect[PROTOCORE_IKE_COOKIE_LEN];
    if (ike_cookie_compute(ctx->ns->work, cookie[0], ctx->ns->notify.secret, ctx->ns->notify.secret_len,
                           ctx->ns->notify.ni, ctx->ns->notify.ni_len, ctx->ns->notify.ipi, ctx->ns->notify.ipi_len,
                           ctx->ns->notify.spii, expect, sizeof(expect)) != PROTOCORE_IKE_COOKIE_LEN)
    {
        return;
    }
    // Compare the whole cookie without an early out.
    uint8_t diff = 0;
    for (size_t i = 0; i < PROTOCORE_IKE_COOKIE_LEN; i++)
    {
        diff |= (uint8_t)(expect[i] ^ cookie[i]);
    }
    ctx->ns->ok = diff == 0;
}

// A COOKIE is a Notify with Protocol ID and SPI Size zero (RFC 7296 sec 2.6, sec 3.10).
static void cookie_notify_build(struct IkeInternal *restrict ctx)
{
    ctx->ns->n = ike_notify_build(ctx->ns->out.buf, ctx->ns->out.cap, ctx->ns->pl.next_payload, IKE_PROTO_NONE, NULL, 0,
                                  PROTOCORE_IKE_N_COOKIE, ctx->ns->notify.cookie, ctx->ns->notify.cookie_len);
}

// ---------------------------------------------------------------------------
// Typed payload parsers (each reads a payload body off wire)
// ---------------------------------------------------------------------------

static proto_bool ike_ke_parse(const uint8_t *body, size_t body_len, IkeKeRef *out)
{
    mem.set(out, 0, sizeof(*out));
    if (!body || body_len < 4) // Diffie-Hellman Group Num and RESERVED
    {
        return PROTO_FALSE;
    }
    out->dh_group = get16(body);
    out->ke_data = body + 4;
    out->ke_len = body_len - 4;
    return PROTO_TRUE;
}

static void ke_parse(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = ike_ke_parse(ctx->ns->wire.msg, ctx->ns->wire.len, &ctx->ns->ke_ref);
}

// ID Type then three RESERVED octets precede Identification Data (sec 3.5).
static void id_parse(struct IkeInternal *restrict ctx)
{
    const uint8_t *body = ctx->ns->wire.msg;
    size_t body_len = ctx->ns->wire.len;
    mem.set(&ctx->ns->id_ref, 0, sizeof(ctx->ns->id_ref));
    ctx->ns->id_ref.id_type = IKE_ID_RESERVED;
    ctx->ns->ok = PROTO_FALSE;
    if (!body || body_len < 4)
    {
        return;
    }
    ctx->ns->id_ref.id_type = (IkeIdType)body[0];
    ctx->ns->id_ref.id_data = body + 4;
    ctx->ns->id_ref.id_len = body_len - 4;
    ctx->ns->ok = PROTO_TRUE;
}

// Auth Method then three RESERVED octets precede Authentication Data (sec 3.8).
static proto_bool ike_auth_parse(const uint8_t *body, size_t body_len, IkeAuthRef *out)
{
    mem.set(out, 0, sizeof(*out));
    out->auth_method = IKE_AUTH_RESERVED;
    if (!body || body_len < 4)
    {
        return PROTO_FALSE;
    }
    out->auth_method = (IkeAuthMethod)body[0];
    out->auth_data = body + 4;
    out->auth_len = body_len - 4;
    return PROTO_TRUE;
}

static void auth_parse(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = ike_auth_parse(ctx->ns->wire.msg, ctx->ns->wire.len, &ctx->ns->auth_ref);
}

// Protocol ID, SPI Size, Notify Message Type, then the SPI and Notification Data (sec 3.10).
static void notify_parse(struct IkeInternal *restrict ctx)
{
    const uint8_t *body = ctx->ns->wire.msg;
    size_t body_len = ctx->ns->wire.len;
    mem.set(&ctx->ns->notify_ref, 0, sizeof(ctx->ns->notify_ref));
    ctx->ns->ok = PROTO_FALSE;
    if (!body || body_len < 4)
    {
        return;
    }
    uint8_t ss = body[1];
    if (body_len < (size_t)4 + ss)
    {
        return;
    }
    ctx->ns->notify_ref.protocol_id = (IkeProtocol)body[0];
    ctx->ns->notify_ref.spi_size = ss;
    ctx->ns->notify_ref.notify_type = get16(body + 2);
    ctx->ns->notify_ref.spi = ss ? body + 4 : NULL;
    ctx->ns->notify_ref.data = body + 4 + ss;
    ctx->ns->notify_ref.data_len = body_len - 4 - ss;
    ctx->ns->ok = PROTO_TRUE;
}

// Protocol ID, SPI Size, Num of SPIs, then that many SPIs (sec 3.11).
static void delete_parse(struct IkeInternal *restrict ctx)
{
    const uint8_t *body = ctx->ns->wire.msg;
    size_t body_len = ctx->ns->wire.len;
    mem.set(&ctx->ns->delete_ref, 0, sizeof(ctx->ns->delete_ref));
    ctx->ns->ok = PROTO_FALSE;
    if (!body || body_len < 4)
    {
        return;
    }
    uint8_t ss = body[1];
    uint16_t num = get16(body + 2);
    if (body_len < (size_t)4 + (size_t)ss * num)
    {
        return;
    }
    ctx->ns->delete_ref.protocol_id = (IkeProtocol)body[0];
    ctx->ns->delete_ref.spi_size = ss;
    ctx->ns->delete_ref.num_spis = num;
    ctx->ns->delete_ref.spis = (ss && num) ? body + 4 : NULL;
    ctx->ns->ok = PROTO_TRUE;
}

// The Encrypted payload body carves into IV, Ciphertext and ICV by the negotiated lengths (sec 3.14).
static void sk_parse(struct IkeInternal *restrict ctx)
{
    const uint8_t *body = ctx->ns->wire.msg;
    size_t body_len = ctx->ns->wire.len;
    size_t iv_len = ctx->ns->sk.iv_len;
    size_t icv_len = ctx->ns->sk.icv_len;
    mem.set(&ctx->ns->sk_ref, 0, sizeof(ctx->ns->sk_ref));
    ctx->ns->ok = PROTO_FALSE;
    if (!body || body_len < iv_len + icv_len)
    {
        return;
    }
    ctx->ns->sk_ref.iv = iv_len ? body : NULL;
    ctx->ns->sk_ref.ct = body + iv_len;
    ctx->ns->sk_ref.ct_len = body_len - iv_len - icv_len;
    ctx->ns->sk_ref.icv = icv_len ? body + body_len - icv_len : NULL;
    ctx->ns->ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Proposal and Transform Substructures (RFC 7296 sec 3.3.1, sec 3.3.2)
// ---------------------------------------------------------------------------

// Proposal Length bounds the substructure and must cover its header and SPI.
static proto_bool ike_sa_first_proposal(const uint8_t *body, size_t body_len, IkeProposalRef *out)
{
    mem.set(out, 0, sizeof(*out));
    if (!body || body_len < 8)
    {
        return PROTO_FALSE;
    }
    uint16_t plen = get16(body + 2);
    uint8_t ss = body[6];
    if (plen < 8 || (size_t)plen > body_len || (size_t)8 + ss > plen)
    {
        return PROTO_FALSE;
    }
    out->last = (body[0] == 0); // Last Substruc: 0 last, 2 more proposals
    out->proposal_num = body[4];
    out->protocol_id = (IkeProtocol)body[5];
    out->spi_size = ss;
    out->num_transforms = body[7];
    out->spi = ss ? body + 8 : NULL;
    out->transforms = body + 8 + ss;
    out->transforms_len = (size_t)plen - 8 - ss;
    return PROTO_TRUE;
}

static void sa_first_proposal(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = ike_sa_first_proposal(ctx->ns->wire.msg, ctx->ns->wire.len, &ctx->ns->proposal);
}

static void transform_iter_init(struct IkeInternal *restrict ctx)
{
    IkeTransformIter *it = ctx->ns->walk.transforms;
    const IkeProposalRef *p = ctx->ns->walk.proposal;
    if (!it)
    {
        return;
    }
    it->area = p ? p->transforms : NULL;
    it->len = p ? p->transforms_len : 0;
    it->off = 0;
}

// Transform Length bounds the substructure; its attributes are TV when the AF bit is set and TLV
// otherwise, and only Key Length is decoded (sec 3.3.5).
static void transform_next(struct IkeInternal *restrict ctx)
{
    IkeTransformIter *it = ctx->ns->walk.transforms;
    IkeTransformRef *out = &ctx->ns->transform;
    out->type = IKE_TRANSFORM_ENCR;
    out->id = 0;
    out->key_length = -1;
    out->last = PROTO_TRUE;
    ctx->ns->ok = PROTO_FALSE;
    if (!it || !it->area || it->off + 8 > it->len)
    {
        return;
    }
    const uint8_t *t = it->area + it->off;
    uint16_t tlen = get16(t + 2);
    if (tlen < 8 || it->off + tlen > it->len)
    {
        return;
    }
    out->last = (t[0] == 0); // Last Substruc: 0 last, 3 more transforms
    out->type = (IkeTransformType)t[4];
    out->id = get16(t + 6);

    size_t ao = 8;
    while (ao + 4 <= tlen)
    {
        uint16_t af_type = get16(t + ao);
        uint16_t atype = af_type & 0x7FFF;
        if (af_type & 0x8000)
        {
            if (atype == IKE_ATTR_KEY_LENGTH)
            {
                out->key_length = get16(t + ao + 2);
            }
            ao += 4;
        }
        else
        {
            uint16_t alen = get16(t + ao + 2);
            ao += (size_t)4 + alen;
        }
    }
    it->off += tlen;
    ctx->ns->ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Traffic Selectors and Configuration attributes (RFC 7296 sec 3.13, sec 3.15)
// ---------------------------------------------------------------------------

static void ts_count(struct IkeInternal *restrict ctx)
{
    const uint8_t *body = ctx->ns->wire.msg;
    ctx->ns->u8 = (!body || ctx->ns->wire.len < 4) ? 0 : body[0];
}

// Selector Length bounds each selector; its address halves are equal, so the remainder after the
// 8-octet head is even (sec 3.13.1).
static void ts_get(struct IkeInternal *restrict ctx)
{
    const uint8_t *body = ctx->ns->wire.msg;
    size_t body_len = ctx->ns->wire.len;
    uint8_t index = ctx->ns->ts.index;
    IkeTrafficSelector *out = &ctx->ns->sel;
    mem.set(out, 0, sizeof(*out));
    ctx->ns->ok = PROTO_FALSE;
    if (!body || body_len < 4)
    {
        return;
    }
    uint8_t num = body[0];
    if (index >= num)
    {
        return;
    }
    size_t off = 4;
    // Iteration `index` either rejects a malformed selector or returns the match, so the loop never
    // runs to completion.
    for (uint8_t i = 0; i < num; i++)
    {
        if (off + 8 > body_len)
        {
            return;
        }
        uint16_t sel_len = get16(body + off + 2);
        if (sel_len < 8 || off + sel_len > body_len || ((sel_len - 8) % 2) != 0)
        {
            return;
        }
        if (i == index)
        {
            size_t addr_len = (size_t)(sel_len - 8) / 2;
            out->ts_type = (IkeTsType)body[off];
            out->ip_protocol = body[off + 1];
            out->start_port = get16(body + off + 4);
            out->end_port = get16(body + off + 6);
            out->start_addr = body + off + 8;
            out->end_addr = body + off + 8 + addr_len;
            out->addr_len = addr_len;
            ctx->ns->ok = PROTO_TRUE;
            return;
        }
        off += sel_len;
    }
}

// CFG Type then three RESERVED octets precede the attribute area (sec 3.15).
static void cp_parse(struct IkeInternal *restrict ctx)
{
    const uint8_t *body = ctx->ns->wire.msg;
    size_t body_len = ctx->ns->wire.len;
    mem.set(&ctx->ns->cp_ref, 0, sizeof(ctx->ns->cp_ref));
    ctx->ns->cp_ref.cfg_type = IKE_CFG_REQUEST;
    ctx->ns->ok = PROTO_FALSE;
    if (!body || body_len < 4)
    {
        return;
    }
    ctx->ns->cp_ref.cfg_type = (IkeCfgType)body[0];
    ctx->ns->cp_ref.attrs = body + 4;
    ctx->ns->cp_ref.attrs_len = body_len - 4;
    ctx->ns->ok = PROTO_TRUE;
}

static void cp_attr_iter_init(struct IkeInternal *restrict ctx)
{
    IkeCfgAttrIter *it = ctx->ns->walk.attrs;
    if (!it)
    {
        return;
    }
    it->area = ctx->ns->wire.msg;
    it->len = ctx->ns->wire.len;
    it->off = 0;
}

// Each attribute is a 15-bit type, a 2-octet length, and that many value octets (sec 3.15.1).
static void cp_attr_next(struct IkeInternal *restrict ctx)
{
    IkeCfgAttrIter *it = ctx->ns->walk.attrs;
    IkeCfgAttr *out = &ctx->ns->attr;
    ctx->ns->ok = PROTO_FALSE;
    if (!it || !it->area)
    {
        return;
    }
    if (it->off + 4 > it->len)
    {
        return;
    }
    const uint8_t *p = it->area + it->off;
    uint16_t vlen = get16(p + 2);
    if (it->off + 4 + vlen > it->len)
    {
        return;
    }
    out->type = get16(p) & 0x7FFF; // the reserved high bit is masked off
    out->value_len = vlen;
    out->value = vlen ? (p + 4) : NULL;
    it->off += 4 + vlen;
    ctx->ns->ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// prf+ and the SK_* schedule (RFC 7296 sec 2.13, sec 2.14, sec 2.17, sec 2.18)
// ---------------------------------------------------------------------------

// prf+(K,S) = T1 | T2 | ... where T1 = prf(K, S | 0x01) and Ti = prf(K, Ti-1 | S | i).
static proto_bool ike_prf_plus(uint8_t *work, const uint8_t *key, size_t key_len, const uint8_t *seed, size_t seed_len,
                               uint8_t *out, size_t out_len)
{
    if (!key || !seed || !out || out_len == 0)
    {
        return PROTO_FALSE;
    }
    // The counter is a single octet, so prf+ is not defined beyond 255 blocks (sec 2.13).
    if (out_len > (size_t)255 * PROTOCORE_HMAC_SHA256_LEN)
    {
        return PROTO_FALSE;
    }

    uint8_t t[PROTOCORE_HMAC_SHA256_LEN];
    size_t t_len = 0; // T0 is empty, so the first block omits it
    size_t produced = 0;
    uint8_t counter = 0;
    while (produced < out_len)
    {
        counter++;
        HmacSha256.key_args.key = key;
        HmacSha256.key_args.key_len = key_len;
        HmacSha256.init(work);
        if (t_len)
        {
            HmacSha256.update_args.data = t;
            HmacSha256.update_args.len = t_len;
            HmacSha256.update(work);
        }
        HmacSha256.update_args.data = seed;
        HmacSha256.update_args.len = seed_len;
        HmacSha256.update(work);
        HmacSha256.update_args.data = &counter;
        HmacSha256.update_args.len = 1;
        HmacSha256.update(work);
        HmacSha256.final_args.out = t;
        HmacSha256.final(work);
        t_len = PROTOCORE_HMAC_SHA256_LEN;

        size_t take = out_len - produced;
        if (take > PROTOCORE_HMAC_SHA256_LEN)
        {
            take = PROTOCORE_HMAC_SHA256_LEN;
        }
        mem.cpy(out + produced, t, take);
        produced += take;
    }
    return PROTO_TRUE;
}

static void prf_plus(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = ike_prf_plus(ctx->ns->work, ctx->ns->keymat.prf_key, ctx->ns->keymat.prf_key_len,
                               ctx->ns->keymat.seed, ctx->ns->keymat.seed_len, ctx->ns->out.buf, ctx->ns->out.cap);
}

// Build S = Ni | Nr | SPIi | SPIr; returns its length, or 0 on a nonce length out of range.
static size_t build_ni_nr_spi(uint8_t *s, const uint8_t *ni, size_t ni_len, const uint8_t *nr, size_t nr_len,
                              const uint8_t *spi_i, const uint8_t *spi_r)
{
    if (ni_len == 0 || ni_len > PROTOCORE_IKE_NONCE_MAX || nr_len == 0 || nr_len > PROTOCORE_IKE_NONCE_MAX)
    {
        return 0;
    }
    size_t nlen = ni_len + nr_len;
    mem.cpy(s, ni, ni_len);
    mem.cpy(s + ni_len, nr, nr_len);
    mem.cpy(s + nlen, spi_i, PROTOCORE_IKE_SPI_LEN);
    mem.cpy(s + nlen + PROTOCORE_IKE_SPI_LEN, spi_r, PROTOCORE_IKE_SPI_LEN);
    return nlen + 2 * PROTOCORE_IKE_SPI_LEN;
}

// {SK_d | SK_ai | SK_ar | SK_ei | SK_er | SK_pi | SK_pr} = prf+(SKEYSEED, S), taken in that order.
// The initial and the rekey schedules differ only in how SKEYSEED was computed.
static proto_bool sk_split_from_skeyseed(uint8_t *work, const uint8_t skeyseed[PROTOCORE_IKE_PRF_LEN], const uint8_t *s,
                                         size_t s_len, const IkeKeyLengths *lens, IkeKeyMaterial *out)
{
    // An AEAD cipher makes SK_ai and SK_ar zero octets (RFC 5282 sec 7.1); the rest are present.
    if (lens->sk_d == 0 || lens->sk_d > PROTOCORE_IKE_SK_MAX || lens->sk_a > PROTOCORE_IKE_SK_MAX || lens->sk_e == 0 ||
        lens->sk_e > PROTOCORE_IKE_SK_MAX || lens->sk_p == 0 || lens->sk_p > PROTOCORE_IKE_SK_MAX)
    {
        return PROTO_FALSE;
    }
    size_t total = lens->sk_d + 2 * lens->sk_a + 2 * lens->sk_e + 2 * lens->sk_p;
    uint8_t ks[7 * PROTOCORE_IKE_SK_MAX];
    if (!ike_prf_plus(work, skeyseed, PROTOCORE_IKE_PRF_LEN, s, s_len, ks, total))
    {
        return PROTO_FALSE;
    }

    size_t o = 0;
    mem.cpy(out->sk_d, ks + o, lens->sk_d);
    o += lens->sk_d;
    mem.cpy(out->sk_ai, ks + o, lens->sk_a);
    o += lens->sk_a;
    mem.cpy(out->sk_ar, ks + o, lens->sk_a);
    o += lens->sk_a;
    mem.cpy(out->sk_ei, ks + o, lens->sk_e);
    o += lens->sk_e;
    mem.cpy(out->sk_er, ks + o, lens->sk_e);
    o += lens->sk_e;
    mem.cpy(out->sk_pi, ks + o, lens->sk_p);
    o += lens->sk_p;
    mem.cpy(out->sk_pr, ks + o, lens->sk_p);
    out->sk_d_len = lens->sk_d;
    out->sk_a_len = lens->sk_a;
    out->sk_e_len = lens->sk_e;
    out->sk_p_len = lens->sk_p;
    return PROTO_TRUE;
}

// SKEYSEED = prf(Ni | Nr, g^ir) (sec 2.14).
static proto_bool ike_derive_keys(uint8_t *work, const uint8_t *dh_secret, size_t dh_len, const uint8_t *ni,
                                  size_t ni_len, const uint8_t *nr, size_t nr_len, const uint8_t *spi_i,
                                  const uint8_t *spi_r, const IkeKeyLengths *lens, IkeKeyMaterial *out)
{
    if (!dh_secret || !ni || !nr || !spi_i || !spi_r || !lens || !out)
    {
        return PROTO_FALSE;
    }
    // The Ni | Nr prefix of S is also the SKEYSEED key, so one buffer serves both.
    uint8_t s[2 * PROTOCORE_IKE_NONCE_MAX + 2 * PROTOCORE_IKE_SPI_LEN];
    size_t s_len = build_ni_nr_spi(s, ni, ni_len, nr, nr_len, spi_i, spi_r);
    if (s_len == 0)
    {
        return PROTO_FALSE;
    }
    uint8_t skeyseed[PROTOCORE_IKE_PRF_LEN];
    HmacSha256.mac_args.key = s;
    HmacSha256.mac_args.key_len = ni_len + nr_len;
    HmacSha256.mac_args.data = dh_secret;
    HmacSha256.mac_args.len = dh_len;
    HmacSha256.mac_args.out = skeyseed;
    HmacSha256.mac(work);
    return sk_split_from_skeyseed(work, skeyseed, s, s_len, lens, out);
}

static void derive_keys(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok =
        ike_derive_keys(ctx->ns->work, ctx->ns->keymat.dh_secret, ctx->ns->keymat.dh_len, ctx->ns->keymat.ni,
                        ctx->ns->keymat.ni_len, ctx->ns->keymat.nr, ctx->ns->keymat.nr_len, ctx->ns->keymat.spi_i,
                        ctx->ns->keymat.spi_r, ctx->ns->keymat.lens, ctx->ns->keymat.keys);
}

// SKEYSEED = prf(SK_d (old), g^ir (new) | Ni | Nr), then the sec 2.14 split with the new SPIs (sec 2.18).
static void rekey_derive_keys(struct IkeInternal *restrict ctx)
{
    const uint8_t *sk_d_old = ctx->ns->keymat.sk_d;
    const uint8_t *dh_secret = ctx->ns->keymat.dh_secret;
    size_t dh_len = ctx->ns->keymat.dh_len;
    const uint8_t *ni = ctx->ns->keymat.ni;
    size_t ni_len = ctx->ns->keymat.ni_len;
    const uint8_t *nr = ctx->ns->keymat.nr;
    size_t nr_len = ctx->ns->keymat.nr_len;
    ctx->ns->ok = PROTO_FALSE;
    if (!sk_d_old || !dh_secret || !ni || !nr || !ctx->ns->keymat.spi_i || !ctx->ns->keymat.spi_r ||
        !ctx->ns->keymat.lens || !ctx->ns->keymat.keys)
    {
        return;
    }
    if (dh_len == 0 || dh_len > PROTOCORE_IKE_X25519_LEN || ni_len > PROTOCORE_IKE_NONCE_MAX ||
        nr_len > PROTOCORE_IKE_NONCE_MAX)
    {
        return;
    }

    uint8_t seed[PROTOCORE_IKE_X25519_LEN + 2 * PROTOCORE_IKE_NONCE_MAX];
    size_t sl = dh_len;
    mem.cpy(seed, dh_secret, dh_len);
    mem.cpy(seed + sl, ni, ni_len);
    sl += ni_len;
    mem.cpy(seed + sl, nr, nr_len);
    sl += nr_len;
    uint8_t skeyseed[PROTOCORE_IKE_PRF_LEN];
    HmacSha256.mac_args.key = sk_d_old;
    HmacSha256.mac_args.key_len = ctx->ns->keymat.sk_d_len;
    HmacSha256.mac_args.data = seed;
    HmacSha256.mac_args.len = sl;
    HmacSha256.mac_args.out = skeyseed;
    HmacSha256.mac(ctx->ns->work);

    uint8_t s[2 * PROTOCORE_IKE_NONCE_MAX + 2 * PROTOCORE_IKE_SPI_LEN];
    size_t s_len = build_ni_nr_spi(s, ni, ni_len, nr, nr_len, ctx->ns->keymat.spi_i, ctx->ns->keymat.spi_r);
    if (s_len == 0)
    {
        return;
    }
    ctx->ns->ok = sk_split_from_skeyseed(ctx->ns->work, skeyseed, s, s_len, ctx->ns->keymat.lens, ctx->ns->keymat.keys);
}

// KEYMAT = prf+(SK_d, Ni | Nr), or prf+(SK_d, g^ir (new) | Ni | Nr) when the exchange carried KE (sec 2.17).
static void child_keymat(struct IkeInternal *restrict ctx)
{
    const uint8_t *sk_d = ctx->ns->keymat.sk_d;
    const uint8_t *dh_secret = ctx->ns->keymat.dh_secret;
    size_t dh_len = ctx->ns->keymat.dh_len;
    const uint8_t *ni = ctx->ns->keymat.ni;
    size_t ni_len = ctx->ns->keymat.ni_len;
    const uint8_t *nr = ctx->ns->keymat.nr;
    size_t nr_len = ctx->ns->keymat.nr_len;
    ctx->ns->ok = PROTO_FALSE;
    if (!sk_d || !ni || !nr || !ctx->ns->out.buf || ctx->ns->out.cap == 0)
    {
        return;
    }
    if (ni_len > PROTOCORE_IKE_NONCE_MAX || nr_len > PROTOCORE_IKE_NONCE_MAX || dh_len > PROTOCORE_IKE_X25519_LEN)
    {
        return;
    }

    uint8_t seed[PROTOCORE_IKE_X25519_LEN + 2 * PROTOCORE_IKE_NONCE_MAX];
    size_t o = 0;
    if (dh_secret && dh_len)
    {
        mem.cpy(seed, dh_secret, dh_len);
        o = dh_len;
    }
    mem.cpy(seed + o, ni, ni_len);
    o += ni_len;
    mem.cpy(seed + o, nr, nr_len);
    o += nr_len;
    ctx->ns->ok =
        ike_prf_plus(ctx->ns->work, sk_d, ctx->ns->keymat.sk_d_len, seed, o, ctx->ns->out.buf, ctx->ns->out.cap);
}

// SK_d, SK_pi and SK_pr take the PRF's preferred key length (sec 2.13); the cipher key takes the Key
// Length attribute, plus the 4-octet salt for AES-GCM (RFC 5282 sec 7.1).
static proto_bool ike_suite_keylengths(const IkeSuite *suite, IkeKeyLengths *out)
{
    if (!suite || !out)
    {
        return PROTO_FALSE;
    }
    if (suite->prf != IKE_PRF_HMAC_SHA2_256) // the only PRF this schedule implements
    {
        return PROTO_FALSE;
    }

    out->sk_d = PROTOCORE_IKE_PRF_LEN;
    out->sk_p = PROTOCORE_IKE_PRF_LEN;

    if (suite->integ == 0)
    {
        out->sk_a = 0;
    }
    else if (suite->integ == IKE_INTEG_HMAC_SHA2_256_128)
    {
        out->sk_a = 32; // the hash output length (RFC 4868 sec 2.1.1)
    }
    else
    {
        return PROTO_FALSE;
    }

    if (suite->encr_keylen <= 0 || (suite->encr_keylen % 8) != 0)
    {
        return PROTO_FALSE;
    }
    size_t ek = (size_t)(suite->encr_keylen / 8);
    if (suite->encr == IKE_ENCR_AES_GCM_16)
    {
        ek += PROTOCORE_IKE_GCM_SALT_LEN;
    }
    if (ek == 0 || ek > PROTOCORE_IKE_SK_MAX)
    {
        return PROTO_FALSE;
    }
    out->sk_e = ek;
    return PROTO_TRUE;
}

static void suite_keylengths(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = ctx->ns->keymat.lens != NULL && ike_suite_keylengths(ctx->ns->keymat.suite, ctx->ns->keymat.lens);
}

// ---------------------------------------------------------------------------
// The Encrypted payload's AEAD (RFC 5282)
// ---------------------------------------------------------------------------

// The nonce is the salt concatenated with the IV, in that order (RFC 5282 sec 4).
static void ike_gcm_nonce(uint8_t nonce[PROTOCORE_AESGCM_IV_LEN], const uint8_t *salt, const uint8_t *iv)
{
    mem.cpy(nonce, salt, PROTOCORE_IKE_GCM_SALT_LEN);
    mem.cpy(nonce + PROTOCORE_IKE_GCM_SALT_LEN, iv, PROTOCORE_IKE_GCM_IV_LEN);
}

// The Ciphertext field is the ciphertext followed by the Authentication Tag (RFC 5282 sec 3.2).
static proto_bool ike_sk_aead_seal(const uint8_t *key, const uint8_t *salt, const uint8_t *iv, const uint8_t *aad,
                                   size_t aad_len, const uint8_t *pt, size_t pt_len, uint8_t *out)
{
    if (!key || !salt || !iv || !out || (pt_len && !pt) || (aad_len && !aad))
    {
        return PROTO_FALSE;
    }
    uint8_t nonce[PROTOCORE_AESGCM_IV_LEN];
    ike_gcm_nonce(nonce, salt, iv);
    // A fresh IV per message makes the context single use: init, seal once, wipe.
    {
        size_t mark = protocore_secure_mark();
        uint8_t *gcm = protocore_secure_span(PROTOCORE_AESGCM_BORROW, 8).buf;
        AesGcm.key_args.key = key;
        AesGcm.key_init(gcm);
        AesGcm.seal_args.nonce = nonce;
        AesGcm.seal_args.aad = aad;
        AesGcm.seal_args.aad_len = aad_len;
        AesGcm.seal_args.pt = pt;
        AesGcm.seal_args.pt_len = pt_len;
        AesGcm.seal_args.ct_out = out;
        AesGcm.seal_args.tag_out = out + pt_len;
        AesGcm.seal(gcm);
        AesGcm.key_wipe(gcm);
        protocore_secure_release(mark);
    }
    return PROTO_TRUE;
}

static proto_bool ike_sk_aead_open(const uint8_t *key, const uint8_t *salt, const uint8_t *iv, const uint8_t *aad,
                                   size_t aad_len, const uint8_t *ct, size_t ct_len, const uint8_t *icv, uint8_t *out)
{
    if (!key || !salt || !iv || !icv || !out || (ct_len && !ct) || (aad_len && !aad))
    {
        return PROTO_FALSE;
    }
    uint8_t nonce[PROTOCORE_AESGCM_IV_LEN];
    ike_gcm_nonce(nonce, salt, iv);
    proto_bool ok = PROTO_FALSE;
    {
        size_t mark = protocore_secure_mark();
        uint8_t *gcm = protocore_secure_span(PROTOCORE_AESGCM_BORROW, 8).buf;
        AesGcm.key_args.key = key;
        AesGcm.key_init(gcm);
        AesGcm.open_args.nonce = nonce;
        AesGcm.open_args.aad = aad;
        AesGcm.open_args.aad_len = aad_len;
        AesGcm.open_args.ct = ct;
        AesGcm.open_args.ct_len = ct_len;
        AesGcm.open_args.tag = icv;
        AesGcm.open_args.out = out;
        AesGcm.open(gcm);
        ok = AesGcm.ok;
        AesGcm.key_wipe(gcm);
        protocore_secure_release(mark);
    }
    return ok;
}

static void sk_aead_seal(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = ike_sk_aead_seal(ctx->ns->sk.key, ctx->ns->sk.salt, ctx->ns->sk.iv, ctx->ns->sk.aad,
                                   ctx->ns->sk.aad_len, ctx->ns->sk.pt, ctx->ns->sk.pt_len, ctx->ns->out.buf);
}

static void sk_aead_open(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok =
        ike_sk_aead_open(ctx->ns->sk.key, ctx->ns->sk.salt, ctx->ns->sk.iv, ctx->ns->sk.aad, ctx->ns->sk.aad_len,
                         ctx->ns->sk.ct, ctx->ns->sk.ct_len, ctx->ns->sk.icv, ctx->ns->out.buf);
}

// ---------------------------------------------------------------------------
// The Diffie-Hellman exchange (RFC 7296 sec 3.4, RFC 8031)
// ---------------------------------------------------------------------------

// One scalar multiplication, out of a borrow taken and released per call. The entries below take
// only their operands, so the bytes the curve runs in are this file's.
static proto_bool ike_x25519(proto_bool base, const uint8_t *scalar, const uint8_t *point, uint8_t *out)
{
    const size_t mark = protocore_secure_mark();
    protocore_span w = protocore_secure_span(PROTOCORE_CURVE25519_BORROW, 8);
    if (!span.ok(w))
    {
        protocore_secure_release(mark);
        return PROTO_FALSE;
    }
    if (base)
    {
        Curve25519.x25519_base_args.scalar = scalar;
        Curve25519.x25519_base_args.out = out;
        Curve25519.x25519_base(w.buf);
    }
    else
    {
        Curve25519.x25519_args.scalar = scalar;
        Curve25519.x25519_args.point = point;
        Curve25519.x25519_args.out = out;
        Curve25519.x25519(w.buf);
    }
    const proto_bool ok = Curve25519.ok;
    protocore_secure_release(mark);
    return ok;
}

// Group 31: the Key Exchange Data is X25519(private, base), 32 octets (RFC 8031 sec 3.1).
static void dh_public(struct IkeInternal *restrict ctx)
{
    const uint8_t *our_priv = ctx->ns->ke.our_priv;
    uint8_t *out = ctx->ns->out.buf;
    ctx->ns->n = 0;
    if (!our_priv || !out)
    {
        return;
    }
    if (ctx->ns->ke.dh_group == IKE_DH_CURVE25519)
    {
        if (ctx->ns->ke.our_priv_len != PROTOCORE_IKE_X25519_LEN || ctx->ns->out.cap < PROTOCORE_IKE_X25519_LEN)
        {
            return;
        }
        if (!ike_x25519(PROTO_TRUE, our_priv, NULL, out))
        {
            return;
        }
        ctx->ns->n = PROTOCORE_IKE_X25519_LEN;
    }
    // Groups 19 and 14 are a later increment.
}

// Group 31: g^ir is X25519(private, the peer's Key Exchange Data), used directly (RFC 8031 sec 2).
static size_t ike_dh_compute(uint16_t group, const uint8_t *our_priv, size_t priv_len, const uint8_t *peer_pub,
                             size_t pub_len, uint8_t *out, size_t out_cap)
{
    if (!our_priv || !peer_pub || !out)
    {
        return 0;
    }
    if (group == IKE_DH_CURVE25519)
    {
        if (priv_len != PROTOCORE_IKE_X25519_LEN || pub_len != PROTOCORE_IKE_X25519_LEN ||
            out_cap < PROTOCORE_IKE_X25519_LEN)
        {
            return 0;
        }
        if (!ike_x25519(PROTO_FALSE, our_priv, peer_pub, out))
        {
            return 0;
        }
        return PROTOCORE_IKE_X25519_LEN;
    }
    return 0;
}

static void dh_compute(struct IkeInternal *restrict ctx)
{
    ctx->ns->n = ike_dh_compute(ctx->ns->ke.dh_group, ctx->ns->ke.our_priv, ctx->ns->ke.our_priv_len,
                                ctx->ns->ke.peer_pub, ctx->ns->ke.peer_pub_len, ctx->ns->out.buf, ctx->ns->out.cap);
}

// ---------------------------------------------------------------------------
// Authentication of the IKE SA (RFC 7296 sec 2.15, RFC 7427)
// ---------------------------------------------------------------------------

// AUTH = prf( prf(Shared Secret, "Key Pad for IKEv2"), SignedOctets ) where SignedOctets is
// RealMessage | NonceData | MACedID and MACedID = prf(SK_p, RestOfIDPayload).
static proto_bool ike_auth_psk(uint8_t *work, const uint8_t *psk, size_t psk_len, const uint8_t *real_msg,
                               size_t real_len, const uint8_t *peer_nonce, size_t nonce_len, const uint8_t *sk_p,
                               size_t sk_p_len, const uint8_t *id_body, size_t id_body_len, uint8_t *out)
{
    if (!psk || !real_msg || !peer_nonce || !sk_p || !id_body || !out)
    {
        return PROTO_FALSE;
    }

    uint8_t macid[PROTOCORE_IKE_AUTH_LEN];
    HmacSha256.mac_args.key = sk_p;
    HmacSha256.mac_args.key_len = sk_p_len;
    HmacSha256.mac_args.data = id_body;
    HmacSha256.mac_args.len = id_body_len;
    HmacSha256.mac_args.out = macid;
    HmacSha256.mac(work);

    uint8_t keypad[PROTOCORE_IKE_AUTH_LEN];
    static const char pad[] = PROTOCORE_IKE_PSK_PAD; // 17 characters, the NUL is not sent
    HmacSha256.mac_args.key = psk;
    HmacSha256.mac_args.key_len = psk_len;
    HmacSha256.mac_args.data = (const uint8_t *)pad;
    HmacSha256.mac_args.len = sizeof(pad) - 1;
    HmacSha256.mac_args.out = keypad;
    HmacSha256.mac(work);

    // Streamed, so RealMessage is never copied again.
    HmacSha256.key_args.key = keypad;
    HmacSha256.key_args.key_len = sizeof(keypad);
    HmacSha256.init(work);
    HmacSha256.update_args.data = real_msg;
    HmacSha256.update_args.len = real_len;
    HmacSha256.update(work);
    HmacSha256.update_args.data = peer_nonce;
    HmacSha256.update_args.len = nonce_len;
    HmacSha256.update(work);
    HmacSha256.update_args.data = macid;
    HmacSha256.update_args.len = sizeof(macid);
    HmacSha256.update(work);
    HmacSha256.final_args.out = out;
    HmacSha256.final(work);
    return PROTO_TRUE;
}

static void auth_psk(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok =
        ctx->ns->out.cap >= PROTOCORE_IKE_AUTH_LEN &&
        ike_auth_psk(ctx->ns->work, ctx->ns->auth.psk, ctx->ns->auth.psk_len, ctx->ns->auth.real_msg,
                     ctx->ns->auth.real_len, ctx->ns->auth.peer_nonce, ctx->ns->auth.peer_nonce_len, ctx->ns->auth.sk_p,
                     ctx->ns->auth.sk_p_len, ctx->ns->id.id_body, ctx->ns->id.id_body_len, ctx->ns->out.buf);
}

// SignedOctets = RealMessage | NonceData | MACedID, assembled whole because a signer hashes it whole.
static size_t ike_signed_octets(uint8_t *work, uint8_t *scratch, size_t cap, const uint8_t *real, size_t real_len,
                                const uint8_t *nonce, size_t nonce_len, const uint8_t *sk_p, size_t sk_p_len,
                                const uint8_t *id_body, size_t id_body_len)
{
    if (!scratch || !real || !nonce || !sk_p || !id_body)
    {
        return 0;
    }
    size_t total = real_len + nonce_len + PROTOCORE_IKE_AUTH_LEN;
    if (total > cap)
    {
        return 0;
    }
    mem.cpy(scratch, real, real_len);
    mem.cpy(scratch + real_len, nonce, nonce_len);
    HmacSha256.mac_args.key = sk_p;
    HmacSha256.mac_args.key_len = sk_p_len;
    HmacSha256.mac_args.data = id_body;
    HmacSha256.mac_args.len = id_body_len;
    HmacSha256.mac_args.out = scratch + real_len + nonce_len;
    HmacSha256.mac(work);
    return total;
}

static void signed_octets(struct IkeInternal *restrict ctx)
{
    ctx->ns->n =
        ike_signed_octets(ctx->ns->work, ctx->ns->auth.scratch, ctx->ns->auth.scratch_cap, ctx->ns->auth.real_msg,
                          ctx->ns->auth.real_len, ctx->ns->auth.peer_nonce, ctx->ns->auth.peer_nonce_len,
                          ctx->ns->auth.sk_p, ctx->ns->auth.sk_p_len, ctx->ns->id.id_body, ctx->ns->id.id_body_len);
}

// The signer hashes the assembled octets with SHA-256 itself (RFC 7427 sec 3).
static void auth_sign_ecdsa_p256(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->out.buf || ctx->ns->out.cap < PROTOCORE_IKE_ECDSA_P256_SIG_LEN || !ctx->ns->auth.priv)
    {
        return;
    }
    size_t n =
        ike_signed_octets(ctx->ns->work, ctx->ns->auth.scratch, ctx->ns->auth.scratch_cap, ctx->ns->auth.real_msg,
                          ctx->ns->auth.real_len, ctx->ns->auth.peer_nonce, ctx->ns->auth.peer_nonce_len,
                          ctx->ns->auth.sk_p, ctx->ns->auth.sk_p_len, ctx->ns->id.id_body, ctx->ns->id.id_body_len);
    if (n == 0)
    {
        return;
    }
    Ecdsa.sign_args.msg = ctx->ns->auth.scratch;
    Ecdsa.sign_args.mlen = n;
    Ecdsa.sign_args.priv = ctx->ns->auth.priv;
    Ecdsa.sign_args.sig = ctx->ns->out.buf;
    Ecdsa.sign(ctx->ns->work);
    ctx->ns->ok = Ecdsa.ok;
}

static void auth_verify_ecdsa_p256(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->auth.pub || !ctx->ns->auth.sig)
    {
        return;
    }
    size_t n =
        ike_signed_octets(ctx->ns->work, ctx->ns->auth.scratch, ctx->ns->auth.scratch_cap, ctx->ns->auth.real_msg,
                          ctx->ns->auth.real_len, ctx->ns->auth.peer_nonce, ctx->ns->auth.peer_nonce_len,
                          ctx->ns->auth.sk_p, ctx->ns->auth.sk_p_len, ctx->ns->id.id_body, ctx->ns->id.id_body_len);
    if (n == 0)
    {
        return;
    }
    Ecdsa.verify_args.pub = ctx->ns->auth.pub;
    Ecdsa.verify_args.msg = ctx->ns->auth.scratch;
    Ecdsa.verify_args.mlen = n;
    Ecdsa.verify_args.sig = ctx->ns->auth.sig;
    Ecdsa.verify(ctx->ns->work);
    ctx->ns->ok = Ecdsa.ok;
}

// Auth Method 1, RSA Digital Signature: RSASSA-PKCS1-v1_5 over the same octets (RFC 7296 sec 3.8).
static void auth_verify_rsa_sha256(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->auth.rsa_n || !ctx->ns->auth.rsa_e || !ctx->ns->auth.sig)
    {
        return;
    }
    size_t n =
        ike_signed_octets(ctx->ns->work, ctx->ns->auth.scratch, ctx->ns->auth.scratch_cap, ctx->ns->auth.real_msg,
                          ctx->ns->auth.real_len, ctx->ns->auth.peer_nonce, ctx->ns->auth.peer_nonce_len,
                          ctx->ns->auth.sk_p, ctx->ns->auth.sk_p_len, ctx->ns->id.id_body, ctx->ns->id.id_body_len);
    if (n == 0)
    {
        return;
    }
    Rsa.verify_args.n = ctx->ns->auth.rsa_n;
    Rsa.verify_args.e = ctx->ns->auth.rsa_e;
    Rsa.verify_args.msg = ctx->ns->auth.scratch;
    Rsa.verify_args.msg_len = n;
    Rsa.verify_args.sig = ctx->ns->auth.sig;
    Rsa.verify_args.sig_len = ctx->ns->auth.sig_len;
    Rsa.verify_args.hash = PROTOCORE_RSA_HASH_SHA256;
    Rsa.verify(ctx->ns->work);
    ctx->ns->ok = Rsa.ok;
}

// ---------------------------------------------------------------------------
// IKE_SA_INIT message assembly (RFC 7296 sec 1.2)
// ---------------------------------------------------------------------------

// HDR, SA, KE, Nonce with the chain's Next Payload fields and the header Length all set.
static size_t ike_sa_init_build(uint8_t *buf, size_t cap, const uint8_t *init_spi, const uint8_t *resp_spi,
                                uint32_t msg_id, proto_bool is_response, uint8_t proposal_num,
                                const IkeTransform *transforms, uint8_t num_transforms, uint16_t dh_group,
                                const uint8_t *ke_data, size_t ke_len, const uint8_t *nonce, size_t nonce_len)
{
    if (!buf || !init_spi || !resp_spi || !transforms || num_transforms == 0 || (ke_len && !ke_data) ||
        (nonce_len && !nonce))
    {
        return 0;
    }

    IkeHeader h;
    mem.cpy(h.init_spi, init_spi, PROTOCORE_IKE_SPI_LEN);
    mem.cpy(h.resp_spi, resp_spi, PROTOCORE_IKE_SPI_LEN);
    h.next_payload = IKE_PL_SA;
    h.version = PROTOCORE_IKE_VERSION;
    h.exchange = IKE_SA_INIT;
    h.flags = is_response ? PROTOCORE_IKE_FLAG_RESPONSE : PROTOCORE_IKE_FLAG_INITIATOR;
    h.message_id = msg_id;
    h.length = 0; // patched once the payloads are down

    size_t off = ike_hdr_build(buf, cap, &h);
    if (off == 0)
    {
        return 0;
    }
    // SPI Size is zero in an initial IKE SA negotiation: the SPI is in the header (sec 3.3.1).
    size_t n =
        ike_sa_build(buf + off, cap - off, IKE_PL_KE, proposal_num, IKE_PROTO_IKE, NULL, 0, transforms, num_transforms);
    if (n == 0)
    {
        return 0;
    }
    off += n;
    n = ike_ke_build(buf + off, cap - off, IKE_PL_NONCE, dh_group, ke_data, ke_len);
    if (n == 0)
    {
        return 0;
    }
    off += n;
    n = ike_nonce_build(buf + off, cap - off, IKE_PL_NONE, nonce, nonce_len);
    if (n == 0)
    {
        return 0;
    }
    off += n;

    ike_set_length(buf, cap, (uint32_t)off);
    return off;
}

static void sa_init_build(struct IkeInternal *restrict ctx)
{
    ctx->ns->n = ike_sa_init_build(ctx->ns->out.buf, ctx->ns->out.cap, ctx->ns->msg.init_spi, ctx->ns->msg.resp_spi,
                                   ctx->ns->msg.message_id, ctx->ns->msg.is_response, ctx->ns->prop.proposal_num,
                                   ctx->ns->prop.transforms, ctx->ns->prop.num_transforms, ctx->ns->ke.dh_group,
                                   ctx->ns->ke.our_pub, ctx->ns->ke.our_pub_len, ctx->ns->sess.our_nonce,
                                   ctx->ns->sess.our_nonce_len);
}

// A Length that lies about the message fails closed.
static proto_bool ike_sa_init_parse(const uint8_t *msg, size_t len, IkeSaInitMsg *out)
{
    mem.set(out, 0, sizeof(*out));

    IkeHeader h;
    if (!ike_hdr_parse(msg, len, &h))
    {
        return PROTO_FALSE;
    }
    if (h.exchange != IKE_SA_INIT)
    {
        return PROTO_FALSE;
    }
    if (h.length < PROTOCORE_IKE_HDR_LEN || h.length > len)
    {
        return PROTO_FALSE;
    }

    mem.cpy(out->init_spi, h.init_spi, PROTOCORE_IKE_SPI_LEN);
    mem.cpy(out->resp_spi, h.resp_spi, PROTOCORE_IKE_SPI_LEN);
    out->is_response = (h.flags & PROTOCORE_IKE_FLAG_RESPONSE) != 0;

    IkePayloadIter it;
    ike_payload_iter_init(&it, h.next_payload, msg + PROTOCORE_IKE_HDR_LEN, h.length - PROTOCORE_IKE_HDR_LEN);
    IkePayload pl;
    proto_bool have_sa = PROTO_FALSE, have_ke = PROTO_FALSE, have_nonce = PROTO_FALSE;
    while (ike_payload_next(&it, &pl))
    {
        if (pl.type == IKE_PL_SA && !have_sa)
        {
            have_sa = ike_sa_first_proposal(pl.body, pl.body_len, &out->proposal);
        }
        else if (pl.type == IKE_PL_KE && !have_ke)
        {
            IkeKeRef ke;
            have_ke = ike_ke_parse(pl.body, pl.body_len, &ke);
            out->dh_group = ke.dh_group;
            out->ke_data = ke.ke_data;
            out->ke_len = ke.ke_len;
        }
        else if (pl.type == IKE_PL_NONCE && !have_nonce)
        {
            out->nonce = pl.body; // a Nonce payload body is Nonce Data
            out->nonce_len = pl.body_len;
            have_nonce = PROTO_TRUE;
        }
    }
    return have_sa && have_ke && have_nonce;
}

static void sa_init_parse(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = ike_sa_init_parse(ctx->ns->wire.msg, ctx->ns->wire.len, &ctx->ns->sa_init);
}

// ---------------------------------------------------------------------------
// SK-protected message assembly (RFC 7296 sec 3.14, RFC 5282)
// ---------------------------------------------------------------------------

// HDR | SK{ IV | AEAD(inner | Pad Length) | ICV }. The plaintext pads to nothing, so the trailer is
// the single Pad Length octet 0x00 (sec 3.14). The associated data is the header through the
// Encrypted payload's own header (RFC 5282 sec 5.1).
static size_t ike_sk_message_build(uint8_t *buf, size_t cap, const uint8_t *init_spi, const uint8_t *resp_spi,
                                   uint32_t msg_id, IkeExchange exchange, uint8_t flags,
                                   IkePayloadType first_inner_type, const uint8_t *inner, size_t inner_len,
                                   const uint8_t *key, const uint8_t *salt, const uint8_t *iv)
{
    if (!buf || !init_spi || !resp_spi || !key || !salt || !iv || (inner_len && !inner))
    {
        return 0;
    }

    size_t pt_len = inner_len + 1;
    size_t sk_len = PROTOCORE_IKE_PAYLOAD_HDR_LEN + PROTOCORE_IKE_GCM_IV_LEN + pt_len + PROTOCORE_IKE_AEAD_ICV_LEN;
    size_t total = PROTOCORE_IKE_HDR_LEN + sk_len;
    if (total > 0xFFFF || cap < total)
    {
        return 0;
    }

    IkeHeader h;
    mem.cpy(h.init_spi, init_spi, PROTOCORE_IKE_SPI_LEN);
    mem.cpy(h.resp_spi, resp_spi, PROTOCORE_IKE_SPI_LEN);
    h.next_payload = IKE_PL_SK;
    h.version = PROTOCORE_IKE_VERSION;
    h.exchange = exchange;
    h.flags = flags;
    h.message_id = msg_id;
    h.length = (uint32_t)total;
    if (ike_hdr_build(buf, cap, &h) == 0)
    {
        return 0;
    }

    // The Encrypted payload's Next Payload is the type of the first embedded payload (sec 3.14).
    buf[IKE_SK_HDR_OFF + 0] = (uint8_t)first_inner_type;
    buf[IKE_SK_HDR_OFF + 1] = 0;
    put16(buf + IKE_SK_HDR_OFF + 2, (uint16_t)sk_len);

    mem.cpy(buf + IKE_SK_IV_OFF, iv, PROTOCORE_IKE_GCM_IV_LEN);
    if (inner_len)
    {
        mem.cpy(buf + IKE_SK_CT_OFF, inner, inner_len);
    }
    buf[IKE_SK_CT_OFF + inner_len] = 0x00; // Pad Length: no padding octets

    ike_sk_aead_seal(key, salt, iv, buf, IKE_SK_IV_OFF, buf + IKE_SK_CT_OFF, pt_len, buf + IKE_SK_CT_OFF);
    return total;
}

// In IKE_AUTH the initiator sends the request and the responder the response, so the flag is exactly
// I for a request and R for a response.
static size_t ike_auth_msg_build(uint8_t *buf, size_t cap, const uint8_t *init_spi, const uint8_t *resp_spi,
                                 uint32_t msg_id, proto_bool is_response, IkePayloadType first_inner_type,
                                 const uint8_t *inner, size_t inner_len, const uint8_t *key, const uint8_t *salt,
                                 const uint8_t *iv)
{
    uint8_t flags = is_response ? PROTOCORE_IKE_FLAG_RESPONSE : PROTOCORE_IKE_FLAG_INITIATOR;
    return ike_sk_message_build(buf, cap, init_spi, resp_spi, msg_id, IKE_AUTH, flags, first_inner_type, inner,
                                inner_len, key, salt, iv);
}

static void auth_msg_build(struct IkeInternal *restrict ctx)
{
    ctx->ns->n = ike_auth_msg_build(ctx->ns->out.buf, ctx->ns->out.cap, ctx->ns->msg.init_spi, ctx->ns->msg.resp_spi,
                                    ctx->ns->msg.message_id, ctx->ns->msg.is_response, ctx->ns->msg.first_inner_type,
                                    ctx->ns->msg.inner, ctx->ns->msg.inner_len, ctx->ns->sk.key, ctx->ns->sk.salt,
                                    ctx->ns->sk.iv);
}

// Verify the ICV, decrypt in place, then strip Padding and Pad Length (sec 3.14).
static proto_bool ike_auth_msg_open(uint8_t *msg, size_t len, const uint8_t *key, const uint8_t *salt, IkeInnerRef *out)
{
    mem.set(out, 0, sizeof(*out));
    if (!msg || !key || !salt)
    {
        return PROTO_FALSE;
    }

    IkeHeader h;
    if (!ike_hdr_parse(msg, len, &h))
    {
        return PROTO_FALSE;
    }
    if (h.next_payload != IKE_PL_SK) // an Encrypted payload is the whole message body here
    {
        return PROTO_FALSE;
    }
    if (h.length < IKE_SK_CT_OFF || h.length > len)
    {
        return PROTO_FALSE;
    }

    // The Encrypted payload's own Payload Length bounds its body.
    size_t sk_len = ((size_t)msg[IKE_SK_HDR_OFF + 2] << 8) | msg[IKE_SK_HDR_OFF + 3];
    if (sk_len < PROTOCORE_IKE_PAYLOAD_HDR_LEN + PROTOCORE_IKE_GCM_IV_LEN + 1 + PROTOCORE_IKE_AEAD_ICV_LEN ||
        IKE_SK_HDR_OFF + sk_len > h.length)
    {
        return PROTO_FALSE;
    }

    const uint8_t *ivp = msg + IKE_SK_IV_OFF;
    uint8_t *ct = msg + IKE_SK_CT_OFF;
    size_t ct_len = sk_len - PROTOCORE_IKE_PAYLOAD_HDR_LEN - PROTOCORE_IKE_GCM_IV_LEN - PROTOCORE_IKE_AEAD_ICV_LEN;
    const uint8_t *icv = ct + ct_len;

    if (!ike_sk_aead_open(key, salt, ivp, msg, IKE_SK_IV_OFF, ct, ct_len, icv, ct))
    {
        return PROTO_FALSE;
    }

    uint8_t pad_len = ct[ct_len - 1];
    if ((size_t)pad_len + 1 > ct_len) // Padding plus Pad Length cannot exceed the plaintext
    {
        return PROTO_FALSE;
    }
    out->first_inner_type = (IkePayloadType)msg[IKE_SK_HDR_OFF];
    out->inner = ct;
    out->inner_len = ct_len - 1 - pad_len;
    return PROTO_TRUE;
}

static void auth_msg_open(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok =
        ike_auth_msg_open(ctx->ns->sk.msg, ctx->ns->sk.msg_len, ctx->ns->sk.key, ctx->ns->sk.salt, &ctx->ns->opened);
}

// ---------------------------------------------------------------------------
// The IKE SA's key material (RFC 7296 sec 2.14)
// ---------------------------------------------------------------------------

// Both peers feed the schedule the same SPIs and nonces, so both derive the same SK_* keys.
static proto_bool ike_sa_keys_from_init(IkeSa *sa, const uint8_t *our_dh_priv, size_t our_dh_priv_len,
                                        const uint8_t *peer_ke, size_t peer_ke_len, const uint8_t *ni, size_t ni_len,
                                        const uint8_t *nr, size_t nr_len)
{
    if (!sa || !our_dh_priv || !peer_ke || !ni || !nr)
    {
        return PROTO_FALSE;
    }
    IkeKeyLengths lens;
    if (!ike_suite_keylengths(&sa->suite, &lens))
    {
        return PROTO_FALSE;
    }

    uint8_t shared[PROTOCORE_IKE_X25519_LEN]; // group 31 yields a 32-octet secret
    size_t sh =
        ike_dh_compute(sa->suite.dh, our_dh_priv, our_dh_priv_len, peer_ke, peer_ke_len, shared, sizeof(shared));
    if (sh == 0)
    {
        return PROTO_FALSE;
    }
    return ike_derive_keys(sa->work, shared, sh, ni, ni_len, nr, nr_len, sa->init_spi, sa->resp_spi, &lens, &sa->keys);
}

static void sa_keys_from_init(struct IkeInternal *restrict ctx)
{
    ctx->ns->ok = ike_sa_keys_from_init(ctx->ns->sess.sa, ctx->ns->ke.our_priv, ctx->ns->ke.our_priv_len,
                                        ctx->ns->ke.peer_pub, ctx->ns->ke.peer_pub_len, ctx->ns->keymat.ni,
                                        ctx->ns->keymat.ni_len, ctx->ns->keymat.nr, ctx->ns->keymat.nr_len);
}

// ---------------------------------------------------------------------------
// The initiator's driver (RFC 7296 sec 1.2)
// ---------------------------------------------------------------------------

// The Responder's SPI is zero in the first message of an initial exchange (sec 2.6), and the
// Message ID of the IKE_SA_INIT pair is zero (sec 2.2).
static void initiator_start(struct IkeInternal *restrict ctx)
{
    IkeHandshake *hs = ctx->ns->sess.hs;
    const uint8_t *our_spi = ctx->ns->sess.our_spi;
    const uint8_t *our_dh_priv = ctx->ns->ke.our_priv;
    const uint8_t *our_dh_pub = ctx->ns->ke.our_pub;
    const uint8_t *our_nonce = ctx->ns->sess.our_nonce;
    size_t nonce_len = ctx->ns->sess.our_nonce_len;
    const IkeSuite *suite = ctx->ns->keymat.suite;
    ctx->ns->n = 0;
    if (!hs || !our_spi || !our_dh_priv || !our_dh_pub || !our_nonce || !suite || !ctx->ns->prop.transforms ||
        ctx->ns->prop.num_transforms == 0)
    {
        return;
    }
    if (nonce_len == 0 || nonce_len > PROTOCORE_IKE_NONCE_MAX)
    {
        return;
    }

    mem.set(hs, 0, sizeof(*hs));
    mem.cpy(hs->sa.init_spi, our_spi, PROTOCORE_IKE_SPI_LEN);
    hs->sa.is_initiator = PROTO_TRUE;
    hs->sa.suite = *suite;
    mem.cpy(hs->our_dh_priv, our_dh_priv, PROTOCORE_IKE_X25519_LEN);
    mem.cpy(hs->our_nonce, our_nonce, nonce_len);
    hs->our_nonce_len = (uint16_t)nonce_len;

    uint8_t zero_spi[PROTOCORE_IKE_SPI_LEN] = {0};
    size_t n = ike_sa_init_build(ctx->ns->out.buf, ctx->ns->out.cap, our_spi, zero_spi, 0, PROTO_FALSE, 1,
                                 ctx->ns->prop.transforms, ctx->ns->prop.num_transforms, suite->dh, our_dh_pub,
                                 PROTOCORE_IKE_X25519_LEN, our_nonce, nonce_len);
    if (n == 0 || n > PROTOCORE_IKE_MSG_MAX) // RealMessage1 has to fit the stored copy (sec 2.15)
    {
        hs->state = IKE_ST_FAILED;
        return;
    }
    mem.cpy(hs->init_msg, ctx->ns->out.buf, n);
    hs->init_msg_len = (uint16_t)n;
    hs->state = IKE_ST_SA_INIT_SENT;
    ctx->ns->n = n;
}

// The response must carry the R flag, echo our Initiator's SPI, and offer the group we proposed.
static void initiator_on_sa_init(struct IkeInternal *restrict ctx)
{
    IkeHandshake *hs = ctx->ns->sess.hs;
    const uint8_t *resp = ctx->ns->wire.msg;
    size_t resp_len = ctx->ns->wire.len;
    ctx->ns->ok = PROTO_FALSE;
    if (!hs || !resp || hs->state != IKE_ST_SA_INIT_SENT)
    {
        return;
    }

    IkeSaInitMsg m;
    if (!ike_sa_init_parse(resp, resp_len, &m))
    {
        hs->state = IKE_ST_FAILED;
        return;
    }
    if (!m.is_response || mem.cmp(m.init_spi, hs->sa.init_spi, PROTOCORE_IKE_SPI_LEN) != 0 ||
        m.dh_group != hs->sa.suite.dh)
    {
        hs->state = IKE_ST_FAILED;
        return;
    }
    mem.cpy(hs->sa.resp_spi, m.resp_spi, PROTOCORE_IKE_SPI_LEN);

    // Nr is kept: the responder's AUTH is verified over it later (sec 2.15).
    if (m.nonce_len == 0 || m.nonce_len > PROTOCORE_IKE_NONCE_MAX)
    {
        hs->state = IKE_ST_FAILED;
        return;
    }
    mem.cpy(hs->peer_nonce, m.nonce, m.nonce_len);
    hs->peer_nonce_len = (uint16_t)m.nonce_len;

    // RealMessage2 is the octets the responder signs (sec 2.15).
    if (resp_len > PROTOCORE_IKE_MSG_MAX)
    {
        hs->state = IKE_ST_FAILED;
        return;
    }
    mem.cpy(hs->resp_msg, resp, resp_len);
    hs->resp_msg_len = (uint16_t)resp_len;

    // For the initiator Ni is ours and Nr is the responder's.
    if (!ike_sa_keys_from_init(&hs->sa, hs->our_dh_priv, PROTOCORE_IKE_X25519_LEN, m.ke_data, m.ke_len, hs->our_nonce,
                               hs->our_nonce_len, m.nonce, m.nonce_len))
    {
        hs->state = IKE_ST_FAILED;
        return;
    }
    hs->state = IKE_ST_SA_INIT_DONE;
    ctx->ns->ok = PROTO_TRUE;
}

// Compare an AUTH value without an early out.
static proto_bool ike_ct_eq32(const uint8_t *a, const uint8_t *b)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < PROTOCORE_IKE_AUTH_LEN; i++)
    {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

// Find the ID payload the caller names and the AUTH payload in a decrypted chain, and require the
// Auth Method to be Shared Key Message Integrity Code with a full-length MAC (sec 2.15, sec 3.8).
static proto_bool ike_find_id_auth(IkePayloadType first, const uint8_t *inner, size_t inner_len, IkePayloadType id_type,
                                   const uint8_t **id_body, size_t *id_body_len, const uint8_t **authdata,
                                   size_t *authdata_len)
{
    IkePayloadIter it;
    ike_payload_iter_init(&it, first, inner, inner_len);
    IkePayload pl;
    const uint8_t *auth_body = NULL;
    size_t auth_body_len = 0;
    *id_body = NULL;
    *id_body_len = 0;
    while (ike_payload_next(&it, &pl))
    {
        if (pl.type == id_type && !*id_body)
        {
            *id_body = pl.body;
            *id_body_len = pl.body_len;
        }
        else if (pl.type == IKE_PL_AUTH && !auth_body)
        {
            auth_body = pl.body;
            auth_body_len = pl.body_len;
        }
    }
    IkeAuthRef a;
    if (!*id_body || !auth_body || !ike_auth_parse(auth_body, auth_body_len, &a) || a.auth_method != IKE_AUTH_PSK ||
        a.auth_len != PROTOCORE_IKE_AUTH_LEN)
    {
        return PROTO_FALSE;
    }
    *authdata = a.auth_data;
    *authdata_len = a.auth_len;
    return PROTO_TRUE;
}

// SK{ IDi, AUTH } keyed by SK_ei, whose 4-octet tail is the salt (RFC 5282 sec 7.1). The AUTH covers
// RealMessage1 | NonceRData | prf(SK_pi, RestOfInitIDPayload) (sec 2.15).
static void initiator_build_auth_psk(struct IkeInternal *restrict ctx)
{
    IkeHandshake *hs = ctx->ns->sess.hs;
    const uint8_t *idi_data = ctx->ns->pl.data;
    size_t idi_len = ctx->ns->pl.data_len;
    const uint8_t *psk = ctx->ns->auth.psk;
    const uint8_t *iv = ctx->ns->sk.iv;
    ctx->ns->n = 0;
    if (!hs || !idi_data || !psk || !iv || !ctx->ns->out.buf)
    {
        return;
    }
    if (hs->state != IKE_ST_SA_INIT_DONE)
    {
        return;
    }

    uint8_t inner[PROTOCORE_IKE_MSG_MAX];
    size_t idn = ike_id_build(inner, sizeof(inner), IKE_PL_AUTH, ctx->ns->id.id_type, idi_data, idi_len);
    if (idn == 0)
    {
        return;
    }
    // RestOfInitIDPayload is the ID payload minus its generic header (sec 2.15).
    const uint8_t *idi_body = inner + PROTOCORE_IKE_PAYLOAD_HDR_LEN;
    size_t idi_body_len = idn - PROTOCORE_IKE_PAYLOAD_HDR_LEN;
    uint8_t auth[PROTOCORE_IKE_AUTH_LEN];
    if (!ike_auth_psk(hs->sa.work, psk, ctx->ns->auth.psk_len, hs->init_msg, hs->init_msg_len, hs->peer_nonce,
                      hs->peer_nonce_len, hs->sa.keys.sk_pi, hs->sa.keys.sk_p_len, idi_body, idi_body_len, auth))
    {
        return;
    }
    size_t an = ike_auth_build(inner + idn, sizeof(inner) - idn, IKE_PL_NONE, IKE_AUTH_PSK, auth, sizeof(auth));
    if (an == 0)
    {
        return;
    }

    size_t n = ike_auth_msg_build(ctx->ns->out.buf, ctx->ns->out.cap, hs->sa.init_spi, hs->sa.resp_spi, 1, PROTO_FALSE,
                                  IKE_PL_IDI, inner, idn + an, hs->sa.keys.sk_ei,
                                  hs->sa.keys.sk_ei + PROTOCORE_IKE_AEAD_KEY_LEN, iv);
    if (n == 0)
    {
        return;
    }
    hs->state = IKE_ST_AUTH_SENT;
    ctx->ns->n = n;
}

// The responder's AUTH covers RealMessage2 | NonceIData | prf(SK_pr, RestOfRespIDPayload) (sec 2.15).
static void initiator_on_auth_psk(struct IkeInternal *restrict ctx)
{
    IkeHandshake *hs = ctx->ns->sess.hs;
    const uint8_t *resp = ctx->ns->wire.msg;
    size_t resp_len = ctx->ns->wire.len;
    const uint8_t *psk = ctx->ns->auth.psk;
    ctx->ns->ok = PROTO_FALSE;
    if (!hs || !resp || !psk || hs->state != IKE_ST_AUTH_SENT)
    {
        return;
    }
    if (resp_len == 0 || resp_len > PROTOCORE_IKE_MSG_MAX)
    {
        hs->state = IKE_ST_FAILED;
        return;
    }

    // An open decrypts in place, so the received message is copied first. SK_er carries
    // responder to initiator traffic (sec 2.14).
    uint8_t buf[PROTOCORE_IKE_MSG_MAX];
    mem.cpy(buf, resp, resp_len);
    IkeInnerRef opened;
    const uint8_t *idr_body = NULL, *authdata = NULL;
    size_t idr_body_len = 0, authdata_len = 0;
    if (!ike_auth_msg_open(buf, resp_len, hs->sa.keys.sk_er, hs->sa.keys.sk_er + PROTOCORE_IKE_AEAD_KEY_LEN, &opened) ||
        !ike_find_id_auth(opened.first_inner_type, opened.inner, opened.inner_len, IKE_PL_IDR, &idr_body, &idr_body_len,
                          &authdata, &authdata_len))
    {
        hs->state = IKE_ST_FAILED;
        return;
    }

    uint8_t expect[PROTOCORE_IKE_AUTH_LEN];
    if (!ike_auth_psk(hs->sa.work, psk, ctx->ns->auth.psk_len, hs->resp_msg, hs->resp_msg_len, hs->our_nonce,
                      hs->our_nonce_len, hs->sa.keys.sk_pr, hs->sa.keys.sk_p_len, idr_body, idr_body_len, expect) ||
        !ike_ct_eq32(expect, authdata))
    {
        hs->state = IKE_ST_FAILED;
        return;
    }

    hs->state = IKE_ST_ESTABLISHED;
    ctx->ns->ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// The responder's driver (RFC 7296 sec 1.2)
// ---------------------------------------------------------------------------

// The handshake's fields keep their role-neutral meaning: init_msg is RealMessage1, the request;
// resp_msg is RealMessage2, our response; our_nonce is Nr and peer_nonce is Ni.
static void responder_on_sa_init(struct IkeInternal *restrict ctx)
{
    IkeHandshake *hs = ctx->ns->sess.hs;
    const uint8_t *req = ctx->ns->wire.msg;
    size_t req_len = ctx->ns->wire.len;
    const uint8_t *our_spi = ctx->ns->sess.our_spi;
    const uint8_t *our_dh_priv = ctx->ns->ke.our_priv;
    const uint8_t *our_dh_pub = ctx->ns->ke.our_pub;
    const uint8_t *our_nonce = ctx->ns->sess.our_nonce;
    size_t nonce_len = ctx->ns->sess.our_nonce_len;
    const IkeSuite *suite = ctx->ns->keymat.suite;
    ctx->ns->n = 0;
    if (!hs || !req || !our_spi || !our_dh_priv || !our_dh_pub || !our_nonce || !suite || !ctx->ns->prop.transforms ||
        ctx->ns->prop.num_transforms == 0)
    {
        return;
    }
    if (nonce_len == 0 || nonce_len > PROTOCORE_IKE_NONCE_MAX || req_len > PROTOCORE_IKE_MSG_MAX)
    {
        return;
    }

    IkeSaInitMsg m;
    if (!ike_sa_init_parse(req, req_len, &m))
    {
        return;
    }
    // A request has the R flag clear and must offer the group we accept (sec 3.4).
    if (m.is_response || m.dh_group != suite->dh || m.nonce_len == 0 || m.nonce_len > PROTOCORE_IKE_NONCE_MAX)
    {
        return;
    }

    mem.set(hs, 0, sizeof(*hs));
    hs->sa.is_initiator = PROTO_FALSE;
    hs->sa.suite = *suite;
    mem.cpy(hs->sa.init_spi, m.init_spi, PROTOCORE_IKE_SPI_LEN); // the initiator's SPI, echoed
    mem.cpy(hs->sa.resp_spi, our_spi, PROTOCORE_IKE_SPI_LEN);
    mem.cpy(hs->our_dh_priv, our_dh_priv, PROTOCORE_IKE_X25519_LEN);
    mem.cpy(hs->our_nonce, our_nonce, nonce_len);
    hs->our_nonce_len = (uint16_t)nonce_len;
    mem.cpy(hs->peer_nonce, m.nonce, m.nonce_len);
    hs->peer_nonce_len = (uint16_t)m.nonce_len;
    mem.cpy(hs->init_msg, req, req_len);
    hs->init_msg_len = (uint16_t)req_len;

    size_t n = ike_sa_init_build(ctx->ns->out.buf, ctx->ns->out.cap, m.init_spi, our_spi, 0, PROTO_TRUE, 1,
                                 ctx->ns->prop.transforms, ctx->ns->prop.num_transforms, suite->dh, our_dh_pub,
                                 PROTOCORE_IKE_X25519_LEN, our_nonce, nonce_len);
    if (n == 0 || n > PROTOCORE_IKE_MSG_MAX)
    {
        hs->state = IKE_ST_FAILED;
        return;
    }
    mem.cpy(hs->resp_msg, ctx->ns->out.buf, n);
    hs->resp_msg_len = (uint16_t)n;

    // For the responder Ni is the peer's and Nr is ours.
    if (!ike_sa_keys_from_init(&hs->sa, our_dh_priv, PROTOCORE_IKE_X25519_LEN, m.ke_data, m.ke_len, hs->peer_nonce,
                               hs->peer_nonce_len, hs->our_nonce, hs->our_nonce_len))
    {
        hs->state = IKE_ST_FAILED;
        return;
    }
    hs->state = IKE_ST_SA_INIT_DONE;
    ctx->ns->n = n;
}

// Verify SK{ IDi, AUTH } keyed by SK_ei, then emit SK{ IDr, AUTH } keyed by SK_er (sec 2.14, 2.15).
static void responder_on_auth_psk(struct IkeInternal *restrict ctx)
{
    IkeHandshake *hs = ctx->ns->sess.hs;
    const uint8_t *req = ctx->ns->wire.msg;
    size_t req_len = ctx->ns->wire.len;
    const uint8_t *psk = ctx->ns->auth.psk;
    size_t psk_len = ctx->ns->auth.psk_len;
    const uint8_t *idr_data = ctx->ns->pl.data;
    size_t idr_len = ctx->ns->pl.data_len;
    const uint8_t *iv = ctx->ns->sk.iv;
    ctx->ns->n = 0;
    if (!hs || !req || !psk || !idr_data || !iv || !ctx->ns->out.buf)
    {
        return;
    }
    if (hs->state != IKE_ST_SA_INIT_DONE || hs->sa.is_initiator)
    {
        return;
    }
    if (req_len == 0 || req_len > PROTOCORE_IKE_MSG_MAX)
    {
        hs->state = IKE_ST_FAILED;
        return;
    }

    uint8_t buf[PROTOCORE_IKE_MSG_MAX];
    mem.cpy(buf, req, req_len);
    IkeInnerRef opened;
    const uint8_t *idi_body = NULL, *authdata = NULL;
    size_t idi_body_len = 0, authdata_len = 0;
    if (!ike_auth_msg_open(buf, req_len, hs->sa.keys.sk_ei, hs->sa.keys.sk_ei + PROTOCORE_IKE_AEAD_KEY_LEN, &opened) ||
        !ike_find_id_auth(opened.first_inner_type, opened.inner, opened.inner_len, IKE_PL_IDI, &idi_body, &idi_body_len,
                          &authdata, &authdata_len))
    {
        hs->state = IKE_ST_FAILED;
        return;
    }

    // The initiator's AUTH covers RealMessage1 | NonceRData | prf(SK_pi, RestOfInitIDPayload).
    uint8_t expect[PROTOCORE_IKE_AUTH_LEN];
    if (!ike_auth_psk(hs->sa.work, psk, psk_len, hs->init_msg, hs->init_msg_len, hs->our_nonce, hs->our_nonce_len,
                      hs->sa.keys.sk_pi, hs->sa.keys.sk_p_len, idi_body, idi_body_len, expect) ||
        !ike_ct_eq32(expect, authdata))
    {
        hs->state = IKE_ST_FAILED;
        return;
    }

    // Ours covers RealMessage2 | NonceIData | prf(SK_pr, RestOfRespIDPayload).
    uint8_t rinner[PROTOCORE_IKE_MSG_MAX];
    size_t ridn = ike_id_build(rinner, sizeof(rinner), IKE_PL_AUTH, ctx->ns->id.id_type, idr_data, idr_len);
    if (ridn == 0)
    {
        hs->state = IKE_ST_FAILED;
        return;
    }
    uint8_t rauth[PROTOCORE_IKE_AUTH_LEN];
    if (!ike_auth_psk(hs->sa.work, psk, psk_len, hs->resp_msg, hs->resp_msg_len, hs->peer_nonce, hs->peer_nonce_len,
                      hs->sa.keys.sk_pr, hs->sa.keys.sk_p_len, rinner + PROTOCORE_IKE_PAYLOAD_HDR_LEN,
                      ridn - PROTOCORE_IKE_PAYLOAD_HDR_LEN, rauth))
    {
        hs->state = IKE_ST_FAILED;
        return;
    }
    size_t ran = ike_auth_build(rinner + ridn, sizeof(rinner) - ridn, IKE_PL_NONE, IKE_AUTH_PSK, rauth, sizeof(rauth));
    if (ran == 0)
    {
        hs->state = IKE_ST_FAILED;
        return;
    }
    size_t n = ike_auth_msg_build(ctx->ns->out.buf, ctx->ns->out.cap, hs->sa.init_spi, hs->sa.resp_spi, 1, PROTO_TRUE,
                                  IKE_PL_IDR, rinner, ridn + ran, hs->sa.keys.sk_er,
                                  hs->sa.keys.sk_er + PROTOCORE_IKE_AEAD_KEY_LEN, iv);
    if (n == 0)
    {
        hs->state = IKE_ST_FAILED;
        return;
    }
    hs->state = IKE_ST_ESTABLISHED;
    ctx->ns->n = n;
}

// ---------------------------------------------------------------------------
// Exchanges over an established SA (RFC 7296 sec 1.3, sec 1.4)
// ---------------------------------------------------------------------------

// Our egress key is SK_ei when we are the original initiator and SK_er otherwise (sec 2.14). The I
// flag follows that role and the R flag follows the message (sec 3.1).
static size_t ike_sk_send_build(const IkeSa *sa, proto_bool is_response, uint32_t msg_id, IkeExchange exchange,
                                IkePayloadType first_inner_type, const uint8_t *inner, size_t inner_len,
                                const uint8_t *iv, uint8_t *out, size_t out_cap)
{
    if (!sa || !iv || !out)
    {
        return 0;
    }
    const uint8_t *key = sa->is_initiator ? sa->keys.sk_ei : sa->keys.sk_er;
    uint8_t flags = (uint8_t)((sa->is_initiator ? PROTOCORE_IKE_FLAG_INITIATOR : 0) |
                              (is_response ? PROTOCORE_IKE_FLAG_RESPONSE : 0));
    return ike_sk_message_build(out, out_cap, sa->init_spi, sa->resp_spi, msg_id, exchange, flags, first_inner_type,
                                inner, inner_len, key, key + PROTOCORE_IKE_AEAD_KEY_LEN, iv);
}

// An INFORMATIONAL request with an empty Encrypted payload is a liveness check (sec 1.4).
static void informational_build(struct IkeInternal *restrict ctx)
{
    ctx->ns->n = ike_sk_send_build(ctx->ns->sess.sa, ctx->ns->msg.is_response, ctx->ns->msg.message_id,
                                   IKE_INFORMATIONAL, ctx->ns->msg.first_inner_type, ctx->ns->msg.inner,
                                   ctx->ns->msg.inner_len, ctx->ns->sk.iv, ctx->ns->out.buf, ctx->ns->out.cap);
}

static void create_child_sa_build(struct IkeInternal *restrict ctx)
{
    ctx->ns->n = ike_sk_send_build(ctx->ns->sess.sa, ctx->ns->msg.is_response, ctx->ns->msg.message_id,
                                   IKE_CREATE_CHILD_SA, ctx->ns->msg.first_inner_type, ctx->ns->msg.inner,
                                   ctx->ns->msg.inner_len, ctx->ns->sk.iv, ctx->ns->out.buf, ctx->ns->out.cap);
}

// The ingress key is the peer's egress key: SK_er when the peer is the responder, SK_ei otherwise.
static void informational_open(struct IkeInternal *restrict ctx)
{
    const IkeSa *sa = ctx->ns->sess.sa;
    ctx->ns->ok = PROTO_FALSE;
    if (!sa)
    {
        return;
    }
    const uint8_t *key = sa->is_initiator ? sa->keys.sk_er : sa->keys.sk_ei;
    ctx->ns->ok = ike_auth_msg_open(ctx->ns->sk.msg, ctx->ns->sk.msg_len, key, key + PROTOCORE_IKE_AEAD_KEY_LEN,
                                    &ctx->ns->opened);
}

// Designated, so a member's position in the struct does not decide what it binds to.
IkeNs Ike = {.hdr_build = hdr_build,
             .hdr_parse = hdr_parse,
             .set_length = set_length,
             .payload_iter_init = payload_iter_init,
             .payload_next = payload_next,
             .payload_build = payload_build,
             .sa_build = sa_build,
             .ke_build = ke_build,
             .nonce_build = nonce_build,
             .id_build = id_build,
             .auth_build = auth_build,
             .cert_build = cert_build,
             .notify_build = notify_build,
             .delete_build = delete_build,
             .ts_build = ts_build,
             .cp_build = cp_build,
             .sk_build = sk_build,
             .skf_build = skf_build,
             .skf_parse = skf_parse,
             .frag_reasm_init = frag_reasm_init,
             .frag_reasm_add = frag_reasm_add,
             .frag_reasm_complete = frag_reasm_complete,
             .frag_reasm_assemble = frag_reasm_assemble,
             .cookie_compute = cookie_compute,
             .cookie_verify = cookie_verify,
             .cookie_notify_build = cookie_notify_build,
             .ke_parse = ke_parse,
             .id_parse = id_parse,
             .auth_parse = auth_parse,
             .notify_parse = notify_parse,
             .delete_parse = delete_parse,
             .sk_parse = sk_parse,
             .sa_first_proposal = sa_first_proposal,
             .transform_iter_init = transform_iter_init,
             .transform_next = transform_next,
             .ts_count = ts_count,
             .ts_get = ts_get,
             .cp_parse = cp_parse,
             .cp_attr_iter_init = cp_attr_iter_init,
             .cp_attr_next = cp_attr_next,
             .prf_plus = prf_plus,
             .derive_keys = derive_keys,
             .rekey_derive_keys = rekey_derive_keys,
             .child_keymat = child_keymat,
             .suite_keylengths = suite_keylengths,
             .sa_keys_from_init = sa_keys_from_init,
             .sk_aead_seal = sk_aead_seal,
             .sk_aead_open = sk_aead_open,
             .dh_public = dh_public,
             .dh_compute = dh_compute,
             .auth_psk = auth_psk,
             .signed_octets = signed_octets,
             .auth_sign_ecdsa_p256 = auth_sign_ecdsa_p256,
             .auth_verify_ecdsa_p256 = auth_verify_ecdsa_p256,
             .auth_verify_rsa_sha256 = auth_verify_rsa_sha256,
             .sa_init_build = sa_init_build,
             .sa_init_parse = sa_init_parse,
             .auth_msg_build = auth_msg_build,
             .auth_msg_open = auth_msg_open,
             .initiator_start = initiator_start,
             .initiator_on_sa_init = initiator_on_sa_init,
             .initiator_build_auth_psk = initiator_build_auth_psk,
             .initiator_on_auth_psk = initiator_on_auth_psk,
             .responder_on_sa_init = responder_on_sa_init,
             .responder_on_auth_psk = responder_on_auth_psk,
             .informational_build = informational_build,
             .informational_open = informational_open,
             .create_child_sa_build = create_child_sa_build,
             .internal = &s_ike};

#endif // PROTOCORE_ENABLE_IKEV2
