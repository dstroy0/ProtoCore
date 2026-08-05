// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ikev2.c
 * @brief IKEv2 (RFC 7296) message + payload codec (pure, host-tested).
 */

#include "services/security/ikev2/ikev2.h"

#if PC_ENABLE_IKEV2

#include "crypto/aead/aesgcm.h"           // SK-payload AEAD (AES-256-GCM-16)
#include "crypto/asymmetric/curve25519.h" // D-H group 31 (X25519, RFC 7748)
#include "crypto/asymmetric/ecdsa.h"      // ECDSA-P256 certificate AUTH
#include "crypto/asymmetric/rsa.h"        // RSA-2048 certificate AUTH verify
#include "crypto/hash/sha256.h"           // anti-DoS COOKIE hash (RFC 7296 §2.6)
#include "crypto/mac/hmac_sha256.h"       // PRF = HMAC-SHA2-256
#include "mmgr/secure.h"                  // the per-call GCM context borrow
                                          // memcpy / memset (framing is hand-rolled)

// ── big-endian scalar helpers ─────────────────────────────────────────────────────────────────
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

// Write a generic payload header (next-payload, no critical bit, 2-byte total length); false on overflow.
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

// ── IKE header ────────────────────────────────────────────────────────────────────────────────

size_t pc_ike_hdr_build(uint8_t *buf, size_t cap, const IkeHeader *h)
{
    if (!buf || !h || cap < PC_IKE_HDR_LEN)
    {
        return 0;
    }
    memcpy(buf, h->init_spi, PC_IKE_SPI_LEN);
    memcpy(buf + 8, h->resp_spi, PC_IKE_SPI_LEN);
    buf[16] = (uint8_t)h->next_payload;
    buf[17] = h->version;
    buf[18] = (uint8_t)h->exchange;
    buf[19] = h->flags;
    put32(buf + 20, h->message_id);
    put32(buf + 24, h->length);
    return PC_IKE_HDR_LEN;
}

proto_bool pc_ike_hdr_parse(const uint8_t *buf, size_t len, IkeHeader *out)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    if (!buf || len < PC_IKE_HDR_LEN)
    {
        return PROTO_FALSE;
    }
    memcpy(out->init_spi, buf, PC_IKE_SPI_LEN);
    memcpy(out->resp_spi, buf + 8, PC_IKE_SPI_LEN);
    out->next_payload = (IkePayloadType)buf[16];
    out->version = buf[17];
    out->exchange = (IkeExchange)buf[18];
    out->flags = buf[19];
    out->message_id = get32(buf + 20);
    out->length = get32(buf + 24);
    return PROTO_TRUE;
}

proto_bool pc_ike_set_length(uint8_t *buf, size_t buf_cap, uint32_t total_len)
{
    if (!buf || buf_cap < PC_IKE_HDR_LEN)
    {
        return PROTO_FALSE;
    }
    put32(buf + 24, total_len);
    return PROTO_TRUE;
}

// ── generic payload chain ─────────────────────────────────────────────────────────────────────

void pc_ike_payload_iter_init(IkePayloadIter *it, IkePayloadType first_type, const uint8_t *area, size_t area_len)
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

proto_bool pc_ike_payload_next(IkePayloadIter *it, IkePayload *out)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    out->type = IKE_PL_NONE;
    out->next_payload = IKE_PL_NONE;
    out->critical = PROTO_FALSE;
    out->body = NULL;
    out->body_len = 0;
    if (!it || !it->area || it->next_type == IKE_PL_NONE)
    {
        return PROTO_FALSE;
    }
    if (it->off + PC_IKE_PAYLOAD_HDR_LEN > it->len)
    {
        return PROTO_FALSE;
    }
    const uint8_t *p = it->area + it->off;
    uint8_t next = p[0];
    proto_bool critical = (p[1] & PC_IKE_CRITICAL) != 0;
    uint16_t plen = get16(p + 2);
    if (plen < PC_IKE_PAYLOAD_HDR_LEN || it->off + plen > it->len)
    {
        return PROTO_FALSE;
    }
    out->type = it->next_type;
    out->next_payload = (IkePayloadType)next;
    out->critical = critical;
    out->body = p + PC_IKE_PAYLOAD_HDR_LEN;
    out->body_len = (size_t)plen - PC_IKE_PAYLOAD_HDR_LEN;
    it->next_type = (IkePayloadType)next;
    it->off += plen;
    return PROTO_TRUE;
}

size_t pc_ike_payload_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, proto_bool critical,
                            const uint8_t *body, size_t body_len)
{
    if (!buf || (body_len && !body))
    {
        return 0;
    }
    size_t total = PC_IKE_PAYLOAD_HDR_LEN + body_len;
    if (total > 0xFFFF || cap < total)
    {
        return 0;
    }
    buf[0] = (uint8_t)next_payload;
    buf[1] = critical ? PC_IKE_CRITICAL : 0x00;
    put16(buf + 2, (uint16_t)total);
    if (body_len)
    {
        memcpy(buf + PC_IKE_PAYLOAD_HDR_LEN, body, body_len);
    }
    return total;
}

// ── typed payload builders ────────────────────────────────────────────────────────────────────

size_t pc_ike_sa_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, uint8_t proposal_num,
                       IkeProtocol protocol_id, const uint8_t *spi, uint8_t spi_size, const IkeTransform *transforms,
                       uint8_t num_transforms)
{
    if (!buf || !transforms || num_transforms == 0)
    {
        return 0;
    }
    if (spi_size && !spi)
    {
        return 0;
    }

    const size_t prop_hdr = 8; // last+res+len(2)+num+proto+spisize+numtrans
    size_t off = PC_IKE_PAYLOAD_HDR_LEN + prop_hdr + spi_size;
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
        buf[off + 0] = (i + 1 == num_transforms) ? 0 : 3; // 0 = last, 3 = more
        buf[off + 1] = 0;
        put16(buf + off + 2, (uint16_t)tlen);
        buf[off + 4] = (uint8_t)transforms[i].type;
        buf[off + 5] = 0;
        put16(buf + off + 6, transforms[i].id);
        if (has_key)
        {
            put16(buf + off + 8, (uint16_t)(0x8000u | IKE_ATTR_KEY_LENGTH)); // TV form, AF bit set
            put16(buf + off + 10, (uint16_t)transforms[i].key_length);
        }
        off += tlen;
    }

    size_t prop_len = prop_hdr + spi_size + (off - tstart);
    size_t sa_total = PC_IKE_PAYLOAD_HDR_LEN + prop_len;
    if (prop_len > 0xFFFF || sa_total > 0xFFFF)
    {
        return 0;
    }

    buf[0] = (uint8_t)next_payload;
    buf[1] = 0;
    put16(buf + 2, (uint16_t)sa_total);
    uint8_t *pr = buf + PC_IKE_PAYLOAD_HDR_LEN;
    pr[0] = 0; // last (only) proposal
    pr[1] = 0;
    put16(pr + 2, (uint16_t)prop_len);
    pr[4] = proposal_num;
    pr[5] = (uint8_t)protocol_id;
    pr[6] = spi_size;
    pr[7] = num_transforms;
    if (spi_size)
    {
        memcpy(pr + prop_hdr, spi, spi_size);
    }
    return sa_total;
}

size_t pc_ike_ke_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, uint16_t dh_group, const uint8_t *data,
                       size_t data_len)
{
    if (!buf || (data_len && !data))
    {
        return 0;
    }
    size_t total = PC_IKE_PAYLOAD_HDR_LEN + 4 + data_len;
    if (!put_pl_hdr(buf, cap, next_payload, total))
    {
        return 0;
    }
    put16(buf + 4, dh_group);
    buf[6] = 0;
    buf[7] = 0;
    if (data_len)
    {
        memcpy(buf + 8, data, data_len);
    }
    return total;
}

size_t pc_ike_nonce_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, const uint8_t *nonce, size_t nonce_len)
{
    if (!buf || (nonce_len && !nonce))
    {
        return 0;
    }
    size_t total = PC_IKE_PAYLOAD_HDR_LEN + nonce_len;
    if (!put_pl_hdr(buf, cap, next_payload, total))
    {
        return 0;
    }
    if (nonce_len)
    {
        memcpy(buf + 4, nonce, nonce_len);
    }
    return total;
}

size_t pc_ike_id_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, IkeIdType id_type, const uint8_t *data,
                       size_t data_len)
{
    if (!buf || (data_len && !data))
    {
        return 0;
    }
    size_t total = PC_IKE_PAYLOAD_HDR_LEN + 4 + data_len;
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
        memcpy(buf + 8, data, data_len);
    }
    return total;
}

size_t pc_ike_auth_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, IkeAuthMethod auth_method,
                         const uint8_t *data, size_t data_len)
{
    if (!buf || (data_len && !data))
    {
        return 0;
    }
    size_t total = PC_IKE_PAYLOAD_HDR_LEN + 4 + data_len;
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
        memcpy(buf + 8, data, data_len);
    }
    return total;
}

size_t pc_ike_cert_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, uint8_t cert_encoding,
                         const uint8_t *data, size_t data_len)
{
    if (!buf || (data_len && !data))
    {
        return 0;
    }
    size_t total = PC_IKE_PAYLOAD_HDR_LEN + 1 + data_len;
    if (!put_pl_hdr(buf, cap, next_payload, total))
    {
        return 0;
    }
    buf[4] = cert_encoding;
    if (data_len)
    {
        memcpy(buf + 5, data, data_len);
    }
    return total;
}

size_t pc_ike_notify_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, IkeProtocol protocol_id,
                           const uint8_t *spi, uint8_t spi_size, uint16_t notify_type, const uint8_t *data,
                           size_t data_len)
{
    if (!buf || (spi_size && !spi) || (data_len && !data))
    {
        return 0;
    }
    size_t total = PC_IKE_PAYLOAD_HDR_LEN + 4 + spi_size + data_len;
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
        memcpy(buf + off, spi, spi_size);
        off += spi_size;
    }
    if (data_len)
    {
        memcpy(buf + off, data, data_len);
    }
    return total;
}

size_t pc_ike_delete_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, IkeProtocol protocol_id,
                           uint8_t spi_size, const uint8_t *spis, uint16_t num_spis)
{
    if (!buf)
    {
        return 0;
    }
    size_t spis_len = (size_t)spi_size * num_spis;
    if (spis_len && !spis)
    {
        return 0;
    }
    size_t total = PC_IKE_PAYLOAD_HDR_LEN + 4 + spis_len;
    if (!put_pl_hdr(buf, cap, next_payload, total))
    {
        return 0;
    }
    buf[4] = (uint8_t)protocol_id;
    buf[5] = spi_size;
    put16(buf + 6, num_spis);
    if (spis_len)
    {
        memcpy(buf + 8, spis, spis_len);
    }
    return total;
}

size_t pc_ike_ts_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, const IkeTrafficSelector *sels,
                       uint8_t num)
{
    if (!buf || !sels || num == 0)
    {
        return 0;
    }
    size_t off = PC_IKE_PAYLOAD_HDR_LEN + 4; // generic hdr + num(1) + reserved(3)
    if (cap < off)
    {
        return 0;
    }
    for (uint8_t i = 0; i < num; i++)
    {
        const IkeTrafficSelector *s = &sels[i];
        if ((s->addr_len != 4 && s->addr_len != 16) || !s->start_addr || !s->end_addr)
        {
            return 0;
        }
        size_t sel_len = 8 + 2 * s->addr_len;
        if (off + sel_len > cap)
        {
            return 0;
        }
        buf[off + 0] = (uint8_t)s->ts_type;
        buf[off + 1] = s->ip_protocol;
        put16(buf + off + 2, (uint16_t)sel_len);
        put16(buf + off + 4, s->start_port);
        put16(buf + off + 6, s->end_port);
        memcpy(buf + off + 8, s->start_addr, s->addr_len);
        memcpy(buf + off + 8 + s->addr_len, s->end_addr, s->addr_len);
        off += sel_len;
    }
    if (off > 0xFFFF)
    {
        return 0;
    }
    buf[0] = (uint8_t)next_payload;
    buf[1] = 0;
    put16(buf + 2, (uint16_t)off);
    buf[4] = num;
    buf[5] = 0;
    buf[6] = 0;
    buf[7] = 0;
    return off;
}

size_t pc_ike_cp_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, IkeCfgType cfg_type,
                       const IkeCfgAttr *attrs, uint8_t num_attrs)
{
    if (!buf || (num_attrs && !attrs))
    {
        return 0;
    }
    size_t attrs_len = 0;
    for (uint8_t i = 0; i < num_attrs; i++)
    {
        if (attrs[i].value_len && !attrs[i].value)
        {
            return 0;
        }
        attrs_len += 4 + attrs[i].value_len; // type(2) + length(2) + value
    }
    size_t total = PC_IKE_PAYLOAD_HDR_LEN + 4 + attrs_len; // generic hdr + CFG Type(1) + reserved(3)
    if (!put_pl_hdr(buf, cap, next_payload, total))
    {
        return 0;
    }
    buf[4] = (uint8_t)cfg_type;
    buf[5] = 0;
    buf[6] = 0;
    buf[7] = 0;
    size_t off = 8;
    for (uint8_t i = 0; i < num_attrs; i++)
    {
        put16(buf + off, (uint16_t)(attrs[i].type & 0x7FFF)); // reserved high bit clear
        put16(buf + off + 2, attrs[i].value_len);
        off += 4;
        if (attrs[i].value_len)
        {
            memcpy(buf + off, attrs[i].value, attrs[i].value_len);
            off += attrs[i].value_len;
        }
    }
    return total;
}

size_t pc_ike_sk_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, const uint8_t *iv, size_t iv_len,
                       const uint8_t *ciphertext, size_t ct_len, const uint8_t *icv, size_t icv_len)
{
    if (!buf || (iv_len && !iv) || (ct_len && !ciphertext) || (icv_len && !icv))
    {
        return 0;
    }
    size_t total = PC_IKE_PAYLOAD_HDR_LEN + iv_len + ct_len + icv_len;
    if (!put_pl_hdr(buf, cap, next_payload, total))
    {
        return 0;
    }
    size_t off = PC_IKE_PAYLOAD_HDR_LEN;
    if (iv_len)
    {
        memcpy(buf + off, iv, iv_len);
        off += iv_len;
    }
    if (ct_len)
    {
        memcpy(buf + off, ciphertext, ct_len);
        off += ct_len;
    }
    if (icv_len)
    {
        memcpy(buf + off, icv, icv_len);
    }
    return total;
}

// ── message fragmentation (RFC 7383) ──────────────────────────────────────────────────────────

size_t pc_ike_skf_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, uint16_t frag_num, uint16_t total,
                        const uint8_t *iv, size_t iv_len, const uint8_t *ciphertext, size_t ct_len, const uint8_t *icv,
                        size_t icv_len)
{
    if (!buf || (iv_len && !iv) || (ct_len && !ciphertext) || (icv_len && !icv))
    {
        return 0;
    }
    if (frag_num == 0 || total == 0 || frag_num > total) // RFC 7383 §2.5: 1 <= Fragment Number <= Total
    {
        return 0;
    }
    size_t body = PC_IKE_PAYLOAD_HDR_LEN + 4 + iv_len + ct_len + icv_len; // + Frag Number(2) + Total(2)
    if (!put_pl_hdr(buf, cap, next_payload, body))
    {
        return 0;
    }
    put16(buf + PC_IKE_PAYLOAD_HDR_LEN, frag_num);
    put16(buf + PC_IKE_PAYLOAD_HDR_LEN + 2, total);
    size_t off = PC_IKE_PAYLOAD_HDR_LEN + 4;
    if (iv_len)
    {
        memcpy(buf + off, iv, iv_len);
        off += iv_len;
    }
    if (ct_len)
    {
        memcpy(buf + off, ciphertext, ct_len);
        off += ct_len;
    }
    if (icv_len)
    {
        memcpy(buf + off, icv, icv_len);
    }
    return body;
}

proto_bool pc_ike_skf_parse(const uint8_t *body, size_t body_len, uint16_t *frag_num, uint16_t *total, size_t iv_len,
                            size_t icv_len, const uint8_t **iv, const uint8_t **ct, size_t *ct_len, const uint8_t **icv)
{
    if (frag_num)
    {
        *frag_num = 0;
    }
    if (total)
    {
        *total = 0;
    }
    if (iv)
    {
        *iv = NULL;
    }
    if (ct)
    {
        *ct = NULL;
    }
    if (ct_len)
    {
        *ct_len = 0;
    }
    if (icv)
    {
        *icv = NULL;
    }
    // Body = Frag Number(2) | Total(2) | IV | ciphertext | ICV; the ciphertext must be non-negative.
    if (!body || body_len < 4 + iv_len + icv_len)
    {
        return PROTO_FALSE;
    }
    uint16_t fn = get16(body);
    uint16_t tf = get16(body + 2);
    if (fn == 0 || tf == 0 || fn > tf) // a nonsensical fragment counter
    {
        return PROTO_FALSE;
    }
    if (frag_num)
    {
        *frag_num = fn;
    }
    if (total)
    {
        *total = tf;
    }
    if (iv)
    {
        *iv = body + 4;
    }
    if (ct)
    {
        *ct = body + 4 + iv_len;
    }
    if (ct_len)
    {
        *ct_len = body_len - 4 - iv_len - icv_len;
    }
    if (icv)
    {
        *icv = body + body_len - icv_len;
    }
    return PROTO_TRUE;
}

void pc_ike_frag_reasm_init(IkeFragReasm *r, uint8_t *pool, size_t pool_cap)
{
    if (!r)
    {
        return;
    }
    r->total = 0;
    r->count = 0;
    memset(r->present, 0, sizeof(r->present));
    r->pool = pool;
    r->pool_cap = pool_cap;
    r->pool_used = 0;
}

proto_bool pc_ike_frag_reasm_add(IkeFragReasm *r, uint16_t frag_num, uint16_t total, const uint8_t *chunk, size_t len)
{
    if (!r || !r->pool || (len && !chunk))
    {
        return PROTO_FALSE;
    }
    if (frag_num == 0 || total == 0 || frag_num > total || total > PC_IKE_FRAG_MAX)
    {
        return PROTO_FALSE;
    }
    if (r->total == 0)
    {
        r->total = total;
    }
    else if (r->total != total) // every fragment must agree on Total
    {
        return PROTO_FALSE;
    }
    uint16_t idx = (uint16_t)(frag_num - 1);
    if (r->present[idx]) // a duplicate fragment is rejected, not merged
    {
        return PROTO_FALSE;
    }
    if (len > r->pool_cap - r->pool_used) // pool overflow (also covers pool_used == cap)
    {
        return PROTO_FALSE;
    }
    if (len)
    {
        memcpy(r->pool + r->pool_used, chunk, len);
    }
    r->off[idx] = r->pool_used;
    r->len[idx] = len;
    r->present[idx] = PROTO_TRUE;
    r->pool_used += len;
    r->count++;
    return PROTO_TRUE;
}

proto_bool pc_ike_frag_reasm_complete(const IkeFragReasm *r)
{
    return r && r->total != 0 && r->count == r->total;
}

size_t pc_ike_frag_reasm_assemble(const IkeFragReasm *r, uint8_t *out, size_t out_cap)
{
    if (!pc_ike_frag_reasm_complete(r) || !out)
    {
        return 0;
    }
    size_t off = 0;
    for (uint16_t i = 0; i < r->total; i++) // concatenate fragments 1..Total in order
    {
        if (off + r->len[i] > out_cap)
        {
            return 0;
        }
        if (r->len[i])
        {
            memcpy(out + off, r->pool + r->off[i], r->len[i]);
        }
        off += r->len[i];
    }
    return off;
}

// ── anti-DoS COOKIE (RFC 7296 §2.6) ───────────────────────────────────────────────────────────

size_t pc_ike_cookie_compute(uint8_t version, const uint8_t *secret, size_t secret_len, const uint8_t *ni,
                             size_t ni_len, const uint8_t *ipi, size_t ipi_len, const uint8_t spii[PC_IKE_SPI_LEN],
                             uint8_t *out, size_t out_cap)
{
    if (!out || out_cap < PC_IKE_COOKIE_LEN || !spii)
    {
        return 0;
    }
    if ((ni_len && !ni) || (ipi_len && !ipi) || (secret_len && !secret))
    {
        return 0;
    }
    // Cookie = version | SHA-256( Ni | IPi | SPIi | secret ).
    pc_sha256_ctx ctx;
    pc_sha256_init(&ctx);
    if (ni_len)
    {
        pc_sha256_update(&ctx, ni, ni_len);
    }
    if (ipi_len)
    {
        pc_sha256_update(&ctx, ipi, ipi_len);
    }
    pc_sha256_update(&ctx, spii, PC_IKE_SPI_LEN);
    if (secret_len)
    {
        pc_sha256_update(&ctx, secret, secret_len);
    }
    out[0] = version;
    pc_sha256_final(&ctx, out + 1);
    return PC_IKE_COOKIE_LEN;
}

proto_bool pc_ike_cookie_verify(const uint8_t *cookie, size_t cookie_len, const uint8_t *secret, size_t secret_len,
                                const uint8_t *ni, size_t ni_len, const uint8_t *ipi, size_t ipi_len,
                                const uint8_t spii[PC_IKE_SPI_LEN])
{
    if (!cookie || cookie_len != PC_IKE_COOKIE_LEN)
    {
        return PROTO_FALSE;
    }
    uint8_t expect[PC_IKE_COOKIE_LEN];
    if (pc_ike_cookie_compute(cookie[0], secret, secret_len, ni, ni_len, ipi, ipi_len, spii, expect, sizeof(expect)) !=
        PC_IKE_COOKIE_LEN)
    {
        return PROTO_FALSE;
    }
    // Constant-time compare over the whole cookie (version tag + digest); no early-out.
    uint8_t diff = 0;
    for (size_t i = 0; i < PC_IKE_COOKIE_LEN; i++)
    {
        diff |= (uint8_t)(expect[i] ^ cookie[i]);
    }
    return diff == 0;
}

size_t pc_ike_cookie_notify_build(uint8_t *buf, size_t cap, IkePayloadType next_payload, const uint8_t *cookie,
                                  size_t cookie_len)
{
    return pc_ike_notify_build(buf, cap, next_payload, IKE_PROTO_NONE, NULL, 0, PC_IKE_N_COOKIE, cookie, cookie_len);
}

// ── typed payload parsers ─────────────────────────────────────────────────────────────────────

proto_bool pc_ike_ke_parse(const uint8_t *body, size_t body_len, uint16_t *dh_group, const uint8_t **data,
                           size_t *data_len)
{
    if (dh_group)
    {
        *dh_group = 0;
    }
    if (data)
    {
        *data = NULL;
    }
    if (data_len)
    {
        *data_len = 0;
    }
    if (!body || body_len < 4)
    {
        return PROTO_FALSE;
    }
    if (dh_group)
    {
        *dh_group = get16(body);
    }
    if (data)
    {
        *data = body + 4;
    }
    if (data_len)
    {
        *data_len = body_len - 4;
    }
    return PROTO_TRUE;
}

proto_bool pc_ike_id_parse(const uint8_t *body, size_t body_len, IkeIdType *id_type, const uint8_t **data,
                           size_t *data_len)
{
    if (id_type)
    {
        *id_type = IKE_ID_RESERVED;
    }
    if (data)
    {
        *data = NULL;
    }
    if (data_len)
    {
        *data_len = 0;
    }
    if (!body || body_len < 4)
    {
        return PROTO_FALSE;
    }
    if (id_type)
    {
        *id_type = (IkeIdType)body[0];
    }
    if (data)
    {
        *data = body + 4;
    }
    if (data_len)
    {
        *data_len = body_len - 4;
    }
    return PROTO_TRUE;
}

proto_bool pc_ike_auth_parse(const uint8_t *body, size_t body_len, IkeAuthMethod *auth_method, const uint8_t **data,
                             size_t *data_len)
{
    if (auth_method)
    {
        *auth_method = IKE_AUTH_RESERVED;
    }
    if (data)
    {
        *data = NULL;
    }
    if (data_len)
    {
        *data_len = 0;
    }
    if (!body || body_len < 4)
    {
        return PROTO_FALSE;
    }
    if (auth_method)
    {
        *auth_method = (IkeAuthMethod)body[0];
    }
    if (data)
    {
        *data = body + 4;
    }
    if (data_len)
    {
        *data_len = body_len - 4;
    }
    return PROTO_TRUE;
}

proto_bool pc_ike_notify_parse(const uint8_t *body, size_t body_len, IkeProtocol *protocol_id, uint16_t *notify_type,
                               const uint8_t **spi, uint8_t *spi_size, const uint8_t **data, size_t *data_len)
{
    if (protocol_id)
    {
        *protocol_id = IKE_PROTO_NONE;
    }
    if (notify_type)
    {
        *notify_type = 0;
    }
    if (spi)
    {
        *spi = NULL;
    }
    if (spi_size)
    {
        *spi_size = 0;
    }
    if (data)
    {
        *data = NULL;
    }
    if (data_len)
    {
        *data_len = 0;
    }
    if (!body || body_len < 4)
    {
        return PROTO_FALSE;
    }
    uint8_t ss = body[1];
    if (body_len < (size_t)4 + ss)
    {
        return PROTO_FALSE;
    }
    if (protocol_id)
    {
        *protocol_id = (IkeProtocol)body[0];
    }
    if (spi_size)
    {
        *spi_size = ss;
    }
    if (notify_type)
    {
        *notify_type = get16(body + 2);
    }
    if (spi)
    {
        *spi = ss ? body + 4 : NULL;
    }
    if (data)
    {
        *data = body + 4 + ss;
    }
    if (data_len)
    {
        *data_len = body_len - 4 - ss;
    }
    return PROTO_TRUE;
}

proto_bool pc_ike_delete_parse(const uint8_t *body, size_t body_len, IkeProtocol *protocol_id, uint8_t *spi_size,
                               uint16_t *num_spis, const uint8_t **spis)
{
    if (protocol_id)
    {
        *protocol_id = IKE_PROTO_NONE;
    }
    if (spi_size)
    {
        *spi_size = 0;
    }
    if (num_spis)
    {
        *num_spis = 0;
    }
    if (spis)
    {
        *spis = NULL;
    }
    if (!body || body_len < 4)
    {
        return PROTO_FALSE;
    }
    uint8_t ss = body[1];
    uint16_t num = get16(body + 2);
    if (body_len < (size_t)4 + (size_t)ss * num)
    {
        return PROTO_FALSE;
    }
    if (protocol_id)
    {
        *protocol_id = (IkeProtocol)body[0];
    }
    if (spi_size)
    {
        *spi_size = ss;
    }
    if (num_spis)
    {
        *num_spis = num;
    }
    if (spis)
    {
        *spis = (ss && num) ? body + 4 : NULL;
    }
    return PROTO_TRUE;
}

proto_bool pc_ike_sk_parse(const uint8_t *body, size_t body_len, size_t iv_len, size_t icv_len, const uint8_t **iv,
                           const uint8_t **ciphertext, size_t *ct_len, const uint8_t **icv)
{
    if (iv)
    {
        *iv = NULL;
    }
    if (ciphertext)
    {
        *ciphertext = NULL;
    }
    if (ct_len)
    {
        *ct_len = 0;
    }
    if (icv)
    {
        *icv = NULL;
    }
    if (!body || body_len < iv_len + icv_len)
    {
        return PROTO_FALSE;
    }
    if (iv)
    {
        *iv = iv_len ? body : NULL;
    }
    if (ciphertext)
    {
        *ciphertext = body + iv_len;
    }
    if (ct_len)
    {
        *ct_len = body_len - iv_len - icv_len;
    }
    if (icv)
    {
        *icv = icv_len ? body + body_len - icv_len : NULL;
    }
    return PROTO_TRUE;
}

// ── SA / proposal / transform parsing ─────────────────────────────────────────────────────────

proto_bool pc_ike_sa_first_proposal(const uint8_t *body, size_t body_len, IkeProposalRef *out)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
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
    out->last = (body[0] == 0); // 0 = last, 2 = more proposals follow
    out->proposal_num = body[4];
    out->protocol_id = (IkeProtocol)body[5];
    out->spi_size = ss;
    out->num_transforms = body[7];
    out->spi = ss ? body + 8 : NULL;
    out->transforms = body + 8 + ss;
    out->transforms_len = (size_t)plen - 8 - ss;
    return PROTO_TRUE;
}

void pc_ike_transform_iter_init(IkeTransformIter *it, const IkeProposalRef *p)
{
    if (!it)
    {
        return;
    }
    it->area = p ? p->transforms : NULL;
    it->len = p ? p->transforms_len : 0;
    it->off = 0;
}

proto_bool pc_ike_transform_next(IkeTransformIter *it, IkeTransformRef *out)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    out->type = IKE_TRANSFORM_ENCR;
    out->id = 0;
    out->key_length = -1;
    out->last = PROTO_TRUE;
    if (!it || !it->area || it->off + 8 > it->len)
    {
        return PROTO_FALSE;
    }
    const uint8_t *t = it->area + it->off;
    uint16_t tlen = get16(t + 2);
    if (tlen < 8 || it->off + tlen > it->len)
    {
        return PROTO_FALSE;
    }
    out->last = (t[0] == 0); // 0 = last, 3 = more
    out->type = (IkeTransformType)t[4];
    out->id = get16(t + 6);

    // Walk the transform attributes (RFC 7296 3.3.5): a 2-byte AF|type; if AF (0x8000) set it is TV
    // (a 2-byte value follows), else TLV (a 2-byte length then that many value bytes).
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
    return PROTO_TRUE;
}

// ── traffic selector parsing ──────────────────────────────────────────────────────────────────

uint8_t pc_ike_ts_count(const uint8_t *body, size_t body_len)
{
    if (!body || body_len < 4)
    {
        return 0;
    }
    return body[0];
}

proto_bool pc_ike_ts_get(const uint8_t *body, size_t body_len, uint8_t index, IkeTrafficSelector *out)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    if (!body || body_len < 4)
    {
        return PROTO_FALSE;
    }
    uint8_t num = body[0];
    if (index >= num)
    {
        return PROTO_FALSE;
    }
    size_t off = 4;
    // the loop never runs to completion: index < num is enforced above, so iteration `index` either
    // rejects a malformed selector or returns the match
    for (uint8_t i = 0; i < num; i++)
    {
        if (off + 8 > body_len)
        {
            return PROTO_FALSE;
        }
        uint16_t sel_len = get16(body + off + 2);
        if (sel_len < 8 || off + sel_len > body_len || ((sel_len - 8) % 2) != 0)
        {
            return PROTO_FALSE;
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
            return PROTO_TRUE;
        }
        off += sel_len;
    }
    return PROTO_FALSE;
}

proto_bool pc_ike_cp_parse(const uint8_t *body, size_t body_len, IkeCfgType *cfg_type, const uint8_t **attrs,
                           size_t *attrs_len)
{
    if (cfg_type)
    {
        *cfg_type = IKE_CFG_REQUEST;
    }
    if (attrs)
    {
        *attrs = NULL;
    }
    if (attrs_len)
    {
        *attrs_len = 0;
    }
    if (!body || body_len < 4) // CFG Type(1) + reserved(3)
    {
        return PROTO_FALSE;
    }
    if (cfg_type)
    {
        *cfg_type = (IkeCfgType)body[0];
    }
    if (attrs)
    {
        *attrs = body + 4;
    }
    if (attrs_len)
    {
        *attrs_len = body_len - 4;
    }
    return PROTO_TRUE;
}

void pc_ike_cp_attr_iter_init(IkeCfgAttrIter *it, const uint8_t *attrs, size_t attrs_len)
{
    if (!it)
    {
        return;
    }
    it->area = attrs;
    it->len = attrs_len;
    it->off = 0;
}

proto_bool pc_ike_cp_attr_next(IkeCfgAttrIter *it, IkeCfgAttr *out)
{
    if (!it || !out || !it->area)
    {
        return PROTO_FALSE;
    }
    if (it->off + 4 > it->len) // no room for another attribute header
    {
        return PROTO_FALSE;
    }
    const uint8_t *p = it->area + it->off;
    uint16_t vlen = get16(p + 2);
    if (it->off + 4 + vlen > it->len) // truncated value
    {
        return PROTO_FALSE;
    }
    out->type = get16(p) & 0x7FFF; // mask off the reserved high bit
    out->value_len = vlen;
    out->value = vlen ? (p + 4) : NULL;
    it->off += 4 + vlen;
    return PROTO_TRUE;
}

// ── tier 2: SKEYSEED / SK_* key derivation (RFC 7296 §2.13-2.14) ───────────────────────────────

proto_bool pc_ike_prf_plus(const uint8_t *key, size_t key_len, const uint8_t *seed, size_t seed_len, uint8_t *out,
                           size_t out_len)
{
    if (!key || !seed || !out || out_len == 0)
    {
        return PROTO_FALSE;
    }
    // prf+ chains at most 255 blocks: the counter i is a single octet 0x01..0xFF (RFC 7296 §2.13).
    if (out_len > (size_t)255 * PC_HMAC_SHA256_LEN)
    {
        return PROTO_FALSE;
    }

    uint8_t t[PC_HMAC_SHA256_LEN];
    size_t t_len = 0; // T0 is empty (omitted from the first block's input)
    size_t produced = 0;
    uint8_t counter = 0;
    while (produced < out_len)
    {
        counter++; // 0x01 for T1, 0x02 for T2, ... (bounded by the out_len guard above)
        pc_hmac_sha256_ctx ctx;
        pc_hmac_sha256_init(&ctx, key, key_len);
        if (t_len)
        {
            pc_hmac_sha256_update(&ctx, t, t_len); // Ti-1
        }
        pc_hmac_sha256_update(&ctx, seed, seed_len);
        pc_hmac_sha256_update(&ctx, &counter, 1);
        pc_hmac_sha256_final(&ctx, t);
        t_len = PC_HMAC_SHA256_LEN;

        size_t take = out_len - produced;
        if (take > PC_HMAC_SHA256_LEN)
        {
            take = PC_HMAC_SHA256_LEN;
        }
        memcpy(out + produced, t, take);
        produced += take;
    }
    return PROTO_TRUE;
}

// Build S = Ni | Nr | SPIi | SPIr into @p s (caller-sized); returns its length, or 0 on a bad nonce length.
static size_t build_ni_nr_spi(uint8_t *s, const uint8_t *ni, size_t ni_len, const uint8_t *nr, size_t nr_len,
                              const uint8_t *spi_i, const uint8_t *spi_r)
{
    if (ni_len == 0 || ni_len > PC_IKE_NONCE_MAX || nr_len == 0 || nr_len > PC_IKE_NONCE_MAX)
    {
        return 0;
    }
    size_t nlen = ni_len + nr_len;
    memcpy(s, ni, ni_len);
    memcpy(s + ni_len, nr, nr_len);
    memcpy(s + nlen, spi_i, PC_IKE_SPI_LEN);
    memcpy(s + nlen + PC_IKE_SPI_LEN, spi_r, PC_IKE_SPI_LEN);
    return nlen + 2 * PC_IKE_SPI_LEN;
}

// Given SKEYSEED and S = Ni|Nr|SPIi|SPIr, run prf+ and split it into the seven SK_* keys (shared by the
// initial and the rekey schedules - only how SKEYSEED is computed differs).
static proto_bool sk_split_from_skeyseed(const uint8_t skeyseed[PC_IKE_PRF_LEN], const uint8_t *s, size_t s_len,
                                         const IkeKeyLengths *lens, IkeKeyMaterial *out)
{
    // sk_a MAY be 0 (an AEAD cipher carries its own integrity, so there is no separate SK_ai/SK_ar);
    // the others must be present.
    if (lens->sk_d == 0 || lens->sk_d > PC_IKE_SK_MAX || lens->sk_a > PC_IKE_SK_MAX || lens->sk_e == 0 ||
        lens->sk_e > PC_IKE_SK_MAX || lens->sk_p == 0 || lens->sk_p > PC_IKE_SK_MAX)
    {
        return PROTO_FALSE;
    }
    size_t total = lens->sk_d + 2 * lens->sk_a + 2 * lens->sk_e + 2 * lens->sk_p;
    uint8_t ks[7 * PC_IKE_SK_MAX];
    if (!pc_ike_prf_plus(skeyseed, PC_IKE_PRF_LEN, s, s_len, ks, total))
    {
        return PROTO_FALSE;
    }

    size_t o = 0;
    memcpy(out->sk_d, ks + o, lens->sk_d);
    o += lens->sk_d;
    memcpy(out->sk_ai, ks + o, lens->sk_a);
    o += lens->sk_a;
    memcpy(out->sk_ar, ks + o, lens->sk_a);
    o += lens->sk_a;
    memcpy(out->sk_ei, ks + o, lens->sk_e);
    o += lens->sk_e;
    memcpy(out->sk_er, ks + o, lens->sk_e);
    o += lens->sk_e;
    memcpy(out->sk_pi, ks + o, lens->sk_p);
    o += lens->sk_p;
    memcpy(out->sk_pr, ks + o, lens->sk_p);
    out->sk_d_len = lens->sk_d;
    out->sk_a_len = lens->sk_a;
    out->sk_e_len = lens->sk_e;
    out->sk_p_len = lens->sk_p;
    return PROTO_TRUE;
}

proto_bool pc_ike_derive_keys(const uint8_t *dh_secret, size_t dh_len, const uint8_t *ni, size_t ni_len,
                              const uint8_t *nr, size_t nr_len, const uint8_t *spi_i, const uint8_t *spi_r,
                              const IkeKeyLengths *lens, IkeKeyMaterial *out)
{
    if (!dh_secret || !ni || !nr || !spi_i || !spi_r || !lens || !out)
    {
        return PROTO_FALSE;
    }
    // S = Ni | Nr | SPIi | SPIr; its Ni|Nr prefix is also the SKEYSEED HMAC key, so one buffer serves both.
    uint8_t s[2 * PC_IKE_NONCE_MAX + 2 * PC_IKE_SPI_LEN];
    size_t s_len = build_ni_nr_spi(s, ni, ni_len, nr, nr_len, spi_i, spi_r);
    if (s_len == 0)
    {
        return PROTO_FALSE;
    }
    // SKEYSEED = prf(Ni | Nr, g^ir). HMAC pre-hashes a key longer than the 64-byte block (RFC 2104).
    uint8_t skeyseed[PC_IKE_PRF_LEN];
    pc_hmac_sha256(s, ni_len + nr_len, dh_secret, dh_len, skeyseed);
    return sk_split_from_skeyseed(skeyseed, s, s_len, lens, out);
}

proto_bool pc_ike_rekey_derive_keys(const uint8_t *sk_d_old, size_t sk_d_old_len, const uint8_t *dh_secret,
                                    size_t dh_len, const uint8_t *ni, size_t ni_len, const uint8_t *nr, size_t nr_len,
                                    const uint8_t *spi_i, const uint8_t *spi_r, const IkeKeyLengths *lens,
                                    IkeKeyMaterial *out)
{
    if (!sk_d_old || !dh_secret || !ni || !nr || !spi_i || !spi_r || !lens || !out)
    {
        return PROTO_FALSE;
    }
    if (dh_len == 0 || dh_len > PC_IKE_X25519_LEN || ni_len > PC_IKE_NONCE_MAX || nr_len > PC_IKE_NONCE_MAX)
    {
        return PROTO_FALSE;
    }

    // Rekey SKEYSEED = prf(SK_d(old), g^ir(new) | Ni | Nr)  (RFC 7296 §2.18) - the OLD SK_d is the key.
    uint8_t seed[PC_IKE_X25519_LEN + 2 * PC_IKE_NONCE_MAX];
    size_t sl = 0;
    memcpy(seed, dh_secret, dh_len);
    sl = dh_len;
    memcpy(seed + sl, ni, ni_len);
    sl += ni_len;
    memcpy(seed + sl, nr, nr_len);
    sl += nr_len;
    uint8_t skeyseed[PC_IKE_PRF_LEN];
    pc_hmac_sha256(sk_d_old, sk_d_old_len, seed, sl, skeyseed);

    // Then the identical prf+(SKEYSEED, Ni | Nr | SPIi | SPIr) split with the NEW SPIs.
    uint8_t s[2 * PC_IKE_NONCE_MAX + 2 * PC_IKE_SPI_LEN];
    size_t s_len = build_ni_nr_spi(s, ni, ni_len, nr, nr_len, spi_i, spi_r);
    if (s_len == 0)
    {
        return PROTO_FALSE;
    }
    return sk_split_from_skeyseed(skeyseed, s, s_len, lens, out);
}

// ── tier 2: SK-payload AEAD (AES-256-GCM-16, RFC 5282) ─────────────────────────────────────────

// Build the 12-byte GCM nonce = salt(4) || explicit IV(8) (RFC 5282 §4).
static void ike_gcm_nonce(uint8_t nonce[PC_AESGCM_IV_LEN], const uint8_t *salt, const uint8_t *iv)
{
    memcpy(nonce, salt, PC_IKE_GCM_SALT_LEN);
    memcpy(nonce + PC_IKE_GCM_SALT_LEN, iv, PC_IKE_GCM_IV_LEN);
}

proto_bool pc_ike_sk_aead_seal(const uint8_t key[PC_IKE_AEAD_KEY_LEN], const uint8_t salt[PC_IKE_GCM_SALT_LEN],
                               const uint8_t iv[PC_IKE_GCM_IV_LEN], const uint8_t *aad, size_t aad_len,
                               const uint8_t *pt, size_t pt_len, uint8_t *out)
{
    if (!key || !salt || !iv || !out || (pt_len && !pt) || (aad_len && !aad))
    {
        return PROTO_FALSE;
    }
    uint8_t nonce[PC_AESGCM_IV_LEN];
    ike_gcm_nonce(nonce, salt, iv);
    // IKEv2 chooses a fresh explicit IV per message, so the GCM context is single-use here (the SSH
    // invocation-counter model does not apply): init, seal once, wipe.
    {
        // Per-call context: this path is not hot enough to justify a per-session one. The cost is the
        // ~9,200-cycle lifecycle documented in aesgcm.h - hoist the context into session state if it
        // ever shows up in a profile.
        size_t mark = pc_secure_mark();
        struct pc_aesgcm_key *gcm = pc_aesgcm_key_init(pc_secure_span(PC_WORK_AESGCM, 8).buf, key);
        pc_aesgcm_seal(gcm, nonce, aad, aad_len, pt, pt_len, out, out + pt_len); // out = ct || 16-byte tag
        pc_aesgcm_key_wipe(gcm);
        pc_secure_release(mark);
    }
    return PROTO_TRUE;
}

proto_bool pc_ike_sk_aead_open(const uint8_t key[PC_IKE_AEAD_KEY_LEN], const uint8_t salt[PC_IKE_GCM_SALT_LEN],
                               const uint8_t iv[PC_IKE_GCM_IV_LEN], const uint8_t *aad, size_t aad_len,
                               const uint8_t *ct, size_t ct_len, const uint8_t tag[PC_IKE_AEAD_ICV_LEN], uint8_t *out)
{
    if (!key || !salt || !iv || !tag || !out || (ct_len && !ct) || (aad_len && !aad))
    {
        return PROTO_FALSE;
    }
    uint8_t nonce[PC_AESGCM_IV_LEN];
    ike_gcm_nonce(nonce, salt, iv);
    proto_bool ok = PROTO_FALSE;
    {
        // Per-call context: this path is not hot enough to justify a per-session one. The cost is the
        // ~9,200-cycle lifecycle documented in aesgcm.h - hoist the context into session state if it
        // ever shows up in a profile.
        size_t mark = pc_secure_mark();
        struct pc_aesgcm_key *gcm = pc_aesgcm_key_init(pc_secure_span(PC_WORK_AESGCM, 8).buf, key);
        ok = pc_aesgcm_open(gcm, nonce, aad, aad_len, ct, ct_len, tag, out);
        pc_aesgcm_key_wipe(gcm);
        pc_secure_release(mark);
    }
    return ok;
}

// ── tier 2: Diffie-Hellman shared secret (RFC 7296 §2.7) ───────────────────────────────────────

size_t pc_ike_dh_public(uint16_t group, const uint8_t *our_priv, size_t priv_len, uint8_t *out, size_t out_cap)
{
    if (!our_priv || !out)
    {
        return 0;
    }
    if (group == IKE_DH_CURVE25519) // RFC 7748 X25519
    {
        if (priv_len != PC_IKE_X25519_LEN || out_cap < PC_IKE_X25519_LEN)
        {
            return 0;
        }
        pc_x25519_base(out, our_priv); // out = our_priv * G
        return PC_IKE_X25519_LEN;
    }
    return 0; // groups 19 (P-256) / 14 (MODP-2048) are a later increment
}

size_t pc_ike_dh_compute(uint16_t group, const uint8_t *our_priv, size_t priv_len, const uint8_t *peer_pub,
                         size_t pub_len, uint8_t *out, size_t out_cap)
{
    if (!our_priv || !peer_pub || !out)
    {
        return 0;
    }
    if (group == IKE_DH_CURVE25519)
    {
        if (priv_len != PC_IKE_X25519_LEN || pub_len != PC_IKE_X25519_LEN || out_cap < PC_IKE_X25519_LEN)
        {
            return 0;
        }
        pc_x25519(out, our_priv, peer_pub); // out = our_priv * peer_pub
        return PC_IKE_X25519_LEN;
    }
    return 0;
}

// ── tier 2: IKE_AUTH pre-shared-key authentication (RFC 7296 §2.15) ─────────────────────────────

proto_bool pc_ike_auth_psk(const uint8_t *psk, size_t psk_len, const uint8_t *real_msg, size_t real_len,
                           const uint8_t *peer_nonce, size_t nonce_len, const uint8_t *sk_p, size_t sk_p_len,
                           const uint8_t *id_body, size_t id_body_len, uint8_t out[PC_IKE_AUTH_LEN])
{
    if (!psk || !real_msg || !peer_nonce || !sk_p || !id_body || !out)
    {
        return PROTO_FALSE;
    }

    // MACedID = prf(SK_p, RestOfIDPayload).
    uint8_t macid[PC_IKE_AUTH_LEN];
    pc_hmac_sha256(sk_p, sk_p_len, id_body, id_body_len, macid);

    // keypad = prf(PSK, "Key Pad for IKEv2") - the inner PRF that turns the shared key into a fixed key.
    uint8_t keypad[PC_IKE_AUTH_LEN];
    static const char pad[] = PC_IKE_PSK_PAD; // 17 octets, no NUL sent
    pc_hmac_sha256(psk, psk_len, (const uint8_t *)pad, sizeof(pad) - 1, keypad);

    // AUTH = prf(keypad, RealMessage | Nonce | MACedID). Streamed so RealMessage is never re-buffered.
    pc_hmac_sha256_ctx ctx;
    pc_hmac_sha256_init(&ctx, keypad, sizeof(keypad));
    pc_hmac_sha256_update(&ctx, real_msg, real_len);
    pc_hmac_sha256_update(&ctx, peer_nonce, nonce_len);
    pc_hmac_sha256_update(&ctx, macid, sizeof(macid));
    pc_hmac_sha256_final(&ctx, out);
    return PROTO_TRUE;
}

// ── tier 2: IKE_SA_INIT message assembly (RFC 7296 §1.2) ───────────────────────────────────────

size_t pc_ike_sa_init_build(uint8_t *buf, size_t cap, const uint8_t init_spi[PC_IKE_SPI_LEN],
                            const uint8_t resp_spi[PC_IKE_SPI_LEN], uint32_t msg_id, proto_bool is_response,
                            uint8_t proposal_num, const IkeTransform *transforms, uint8_t num_transforms,
                            uint16_t dh_group, const uint8_t *ke_data, size_t ke_len, const uint8_t *nonce,
                            size_t nonce_len)
{
    if (!buf || !init_spi || !resp_spi || !transforms || num_transforms == 0 || (ke_len && !ke_data) ||
        (nonce_len && !nonce))
    {
        return 0;
    }

    IkeHeader h;
    memcpy(h.init_spi, init_spi, PC_IKE_SPI_LEN);
    memcpy(h.resp_spi, resp_spi, PC_IKE_SPI_LEN);
    h.next_payload = IKE_PL_SA;
    h.version = PC_IKE_VERSION;
    h.exchange = IKE_SA_INIT;
    h.flags = is_response ? PC_IKE_FLAG_RESPONSE : PC_IKE_FLAG_INITIATOR;
    h.message_id = msg_id;
    h.length = 0; // patched below

    size_t off = pc_ike_hdr_build(buf, cap, &h);
    if (off == 0)
    {
        return 0;
    }
    // SA -> KE.  One proposal, IKE protocol, no SPI in an IKE_SA_INIT SA payload.
    size_t n = pc_ike_sa_build(buf + off, cap - off, IKE_PL_KE, proposal_num, IKE_PROTO_IKE, NULL, 0, transforms,
                               num_transforms);
    if (n == 0)
    {
        return 0;
    }
    off += n;
    // KE -> Nonce.
    n = pc_ike_ke_build(buf + off, cap - off, IKE_PL_NONCE, dh_group, ke_data, ke_len);
    if (n == 0)
    {
        return 0;
    }
    off += n;
    // Nonce -> end of chain.
    n = pc_ike_nonce_build(buf + off, cap - off, IKE_PL_NONE, nonce, nonce_len);
    if (n == 0)
    {
        return 0;
    }
    off += n;

    pc_ike_set_length(buf, cap, (uint32_t)off);
    return off;
}

proto_bool pc_ike_sa_init_parse(const uint8_t *msg, size_t len, IkeSaInitMsg *out)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));

    IkeHeader h;
    if (!pc_ike_hdr_parse(msg, len, &h))
    {
        return PROTO_FALSE;
    }
    if (h.exchange != IKE_SA_INIT)
    {
        return PROTO_FALSE;
    }
    if (h.length < PC_IKE_HDR_LEN || h.length > len) // a truncated / lying Length fails closed
    {
        return PROTO_FALSE;
    }

    memcpy(out->init_spi, h.init_spi, PC_IKE_SPI_LEN);
    memcpy(out->resp_spi, h.resp_spi, PC_IKE_SPI_LEN);
    out->is_response = (h.flags & PC_IKE_FLAG_RESPONSE) != 0;

    IkePayloadIter it;
    pc_ike_payload_iter_init(&it, h.next_payload, msg + PC_IKE_HDR_LEN, h.length - PC_IKE_HDR_LEN);
    IkePayload pl;
    proto_bool have_sa = PROTO_FALSE, have_ke = PROTO_FALSE, have_nonce = PROTO_FALSE;
    while (pc_ike_payload_next(&it, &pl))
    {
        if (pl.type == IKE_PL_SA && !have_sa)
        {
            have_sa = pc_ike_sa_first_proposal(pl.body, pl.body_len, &out->proposal);
        }
        else if (pl.type == IKE_PL_KE && !have_ke)
        {
            have_ke = pc_ike_ke_parse(pl.body, pl.body_len, &out->dh_group, &out->ke_data, &out->ke_len);
        }
        else if (pl.type == IKE_PL_NONCE && !have_nonce)
        {
            out->nonce = pl.body; // a Nonce payload body is the raw nonce data
            out->nonce_len = pl.body_len;
            have_nonce = PROTO_TRUE;
        }
    }
    return have_sa && have_ke && have_nonce;
}

// ── tier 2: IKE_AUTH encrypted-message assembly (RFC 7296 §3.14, RFC 5282) ─────────────────────

// Byte layout of an SK message: [0,28) IKE header | [28,32) SK generic header | [32,40) IV |
// [40, 40+ct) ciphertext | [.., +16) ICV.  The AAD is [0,32) (header through the SK generic header).
static const size_t IKE_SK_HDR_OFF = PC_IKE_HDR_LEN;                         // 28
static const size_t IKE_SK_IV_OFF = PC_IKE_HDR_LEN + PC_IKE_PAYLOAD_HDR_LEN; // 32 (also the AAD length)
static const size_t IKE_SK_CT_OFF = IKE_SK_IV_OFF + PC_IKE_GCM_IV_LEN;       // 40

// The generic SK-encrypted message builder: HDR(@p exchange, @p flags) | SK{ IV | AEAD(inner | Pad) | ICV }.
// pc_ike_auth_msg_build and the INFORMATIONAL builder are thin wrappers that fix the exchange + flags.
static size_t sk_message_build(uint8_t *buf, size_t cap, const uint8_t *init_spi, const uint8_t *resp_spi,
                               uint32_t msg_id, IkeExchange exchange, uint8_t flags, IkePayloadType first_inner_type,
                               const uint8_t *inner, size_t inner_len, const uint8_t *key, const uint8_t *salt,
                               const uint8_t *iv)
{
    if (!buf || !init_spi || !resp_spi || !key || !salt || !iv || (inner_len && !inner))
    {
        return 0;
    }

    size_t pt_len = inner_len + 1; // inner payloads + a 1-byte Pad Length (zero padding)
    size_t sk_len = PC_IKE_PAYLOAD_HDR_LEN + PC_IKE_GCM_IV_LEN + pt_len + PC_IKE_AEAD_ICV_LEN; // SK payload
    size_t total = PC_IKE_HDR_LEN + sk_len;
    if (total > 0xFFFF || cap < total)
    {
        return 0;
    }

    IkeHeader h;
    memcpy(h.init_spi, init_spi, PC_IKE_SPI_LEN);
    memcpy(h.resp_spi, resp_spi, PC_IKE_SPI_LEN);
    h.next_payload = IKE_PL_SK;
    h.version = PC_IKE_VERSION;
    h.exchange = exchange;
    h.flags = flags;
    h.message_id = msg_id;
    h.length = (uint32_t)total;
    if (pc_ike_hdr_build(buf, cap, &h) == 0)
    {
        return 0;
    }

    // SK generic header (next = first inner payload type).
    buf[IKE_SK_HDR_OFF + 0] = (uint8_t)first_inner_type;
    buf[IKE_SK_HDR_OFF + 1] = 0;
    put16(buf + IKE_SK_HDR_OFF + 2, (uint16_t)sk_len);

    // IV, then the plaintext (inner | Pad Length = 0) built in place at the ciphertext offset.
    memcpy(buf + IKE_SK_IV_OFF, iv, PC_IKE_GCM_IV_LEN);
    if (inner_len)
    {
        memcpy(buf + IKE_SK_CT_OFF, inner, inner_len);
    }
    buf[IKE_SK_CT_OFF + inner_len] = 0x00; // Pad Length (0 padding bytes)

    // Encrypt in place: AAD = [0, 32), plaintext -> ciphertext || ICV at IKE_SK_CT_OFF.
    pc_ike_sk_aead_seal(key, salt, iv, buf, IKE_SK_IV_OFF, buf + IKE_SK_CT_OFF, pt_len, buf + IKE_SK_CT_OFF);
    return total;
}

size_t pc_ike_auth_msg_build(uint8_t *buf, size_t cap, const uint8_t init_spi[PC_IKE_SPI_LEN],
                             const uint8_t resp_spi[PC_IKE_SPI_LEN], uint32_t msg_id, proto_bool is_response,
                             IkePayloadType first_inner_type, const uint8_t *inner, size_t inner_len,
                             const uint8_t key[PC_IKE_AEAD_KEY_LEN], const uint8_t salt[PC_IKE_GCM_SALT_LEN],
                             const uint8_t iv[PC_IKE_GCM_IV_LEN])
{
    // In IKE_AUTH the initiator always sends the request and the responder the response, so the flag is
    // exactly INITIATOR-for-request / RESPONSE-for-response.
    uint8_t flags = is_response ? PC_IKE_FLAG_RESPONSE : PC_IKE_FLAG_INITIATOR;
    return sk_message_build(buf, cap, init_spi, resp_spi, msg_id, IKE_AUTH, flags, first_inner_type, inner, inner_len,
                            key, salt, iv);
}

proto_bool pc_ike_auth_msg_open(uint8_t *msg, size_t len, const uint8_t key[PC_IKE_AEAD_KEY_LEN],
                                const uint8_t salt[PC_IKE_GCM_SALT_LEN], IkePayloadType *first_inner_type,
                                const uint8_t **inner_out, size_t *inner_len_out)
{
    if (!msg || !key || !salt || !inner_out || !inner_len_out)
    {
        return PROTO_FALSE;
    }
    if (first_inner_type)
    {
        *first_inner_type = IKE_PL_NONE;
    }
    *inner_out = NULL;
    *inner_len_out = 0;

    IkeHeader h;
    if (!pc_ike_hdr_parse(msg, len, &h))
    {
        return PROTO_FALSE;
    }
    if (h.next_payload != IKE_PL_SK) // this helper handles the all-encrypted (IKE_AUTH) shape
    {
        return PROTO_FALSE;
    }
    if (h.length < IKE_SK_CT_OFF || h.length > len)
    {
        return PROTO_FALSE;
    }

    // SK payload starts right after the header; its own length bounds the body.
    size_t sk_len = ((size_t)msg[IKE_SK_HDR_OFF + 2] << 8) | msg[IKE_SK_HDR_OFF + 3];
    if (sk_len < PC_IKE_PAYLOAD_HDR_LEN + PC_IKE_GCM_IV_LEN + 1 + PC_IKE_AEAD_ICV_LEN ||
        IKE_SK_HDR_OFF + sk_len > h.length)
    {
        return PROTO_FALSE;
    }

    const uint8_t *ivp = msg + IKE_SK_IV_OFF;
    uint8_t *ct = msg + IKE_SK_CT_OFF;
    size_t ct_len = sk_len - PC_IKE_PAYLOAD_HDR_LEN - PC_IKE_GCM_IV_LEN - PC_IKE_AEAD_ICV_LEN;
    const uint8_t *tag = ct + ct_len;

    // Verify + decrypt in place (AAD = header through the SK generic header).
    if (!pc_ike_sk_aead_open(key, salt, ivp, msg, IKE_SK_IV_OFF, ct, ct_len, tag, ct))
    {
        return PROTO_FALSE;
    }

    // Strip the RFC 7296 §3.14 trailer: last plaintext byte is Pad Length, preceded by that many pad bytes.
    uint8_t pad_len = ct[ct_len - 1];
    if ((size_t)pad_len + 1 > ct_len) // padding + the pad-length byte cannot exceed the plaintext
    {
        return PROTO_FALSE;
    }
    if (first_inner_type)
    {
        *first_inner_type = (IkePayloadType)msg[IKE_SK_HDR_OFF];
    }
    *inner_out = ct;
    *inner_len_out = ct_len - 1 - pad_len;
    return PROTO_TRUE;
}

// ── tier 2: IKE_AUTH ECDSA-P256 (certificate) authentication (RFC 7296 §2.15, RFC 7427) ─────────

size_t pc_ike_signed_octets(uint8_t *scratch, size_t cap, const uint8_t *real, size_t real_len, const uint8_t *nonce,
                            size_t nonce_len, const uint8_t *sk_p, size_t sk_p_len, const uint8_t *id_body,
                            size_t id_body_len)
{
    if (!scratch || !real || !nonce || !sk_p || !id_body)
    {
        return 0;
    }
    size_t total = real_len + nonce_len + PC_IKE_AUTH_LEN; // RealMessage | Nonce | prf(SK_p, id)(=32)
    if (total > cap)
    {
        return 0;
    }
    memcpy(scratch, real, real_len);
    memcpy(scratch + real_len, nonce, nonce_len);
    pc_hmac_sha256(sk_p, sk_p_len, id_body, id_body_len, scratch + real_len + nonce_len); // MACedID
    return total;
}

proto_bool pc_ike_auth_sign_ecdsa_p256(uint8_t sig[PC_IKE_ECDSA_P256_SIG_LEN],
                                       const uint8_t priv[PC_IKE_ECDSA_P256_PRIV_LEN], uint8_t *scratch,
                                       size_t scratch_cap, const uint8_t *real, size_t real_len, const uint8_t *nonce,
                                       size_t nonce_len, const uint8_t *sk_p, size_t sk_p_len, const uint8_t *id_body,
                                       size_t id_body_len)
{
    if (!sig || !priv)
    {
        return PROTO_FALSE;
    }
    size_t n = pc_ike_signed_octets(scratch, scratch_cap, real, real_len, nonce, nonce_len, sk_p, sk_p_len, id_body,
                                    id_body_len);
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    return pc_ecdsa_p256_sign(sig, scratch, n, priv); // hashes the octets with SHA-256 internally
}

proto_bool pc_ike_auth_verify_ecdsa_p256(const uint8_t pub[PC_IKE_ECDSA_P256_PUB_LEN],
                                         const uint8_t sig[PC_IKE_ECDSA_P256_SIG_LEN], uint8_t *scratch,
                                         size_t scratch_cap, const uint8_t *real, size_t real_len, const uint8_t *nonce,
                                         size_t nonce_len, const uint8_t *sk_p, size_t sk_p_len, const uint8_t *id_body,
                                         size_t id_body_len)
{
    if (!pub || !sig)
    {
        return PROTO_FALSE;
    }
    size_t n = pc_ike_signed_octets(scratch, scratch_cap, real, real_len, nonce, nonce_len, sk_p, sk_p_len, id_body,
                                    id_body_len);
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    return pc_ecdsa_p256_verify(pub, scratch, n, sig);
}

// ── tier 2: IKE SA context + key material from a completed IKE_SA_INIT ──────────────────────────

proto_bool pc_ike_suite_keylengths(const IkeSuite *suite, IkeKeyLengths *out)
{
    if (!suite || !out)
    {
        return PROTO_FALSE;
    }
    if (suite->prf != IKE_PRF_HMAC_SHA2_256) // the only PRF the key schedule implements
    {
        return PROTO_FALSE;
    }

    out->sk_d = PC_IKE_PRF_LEN; // the PRF key length
    out->sk_p = PC_IKE_PRF_LEN;

    // Integrity: an AEAD cipher (integ == 0) has no separate SK_ai/SK_ar; HMAC-SHA2-256-128 uses a 32-byte key.
    if (suite->integ == 0)
    {
        out->sk_a = 0;
    }
    else if (suite->integ == IKE_INTEG_HMAC_SHA2_256_128)
    {
        out->sk_a = 32;
    }
    else
    {
        return PROTO_FALSE;
    }

    // Encryption: the key in bytes, plus a 4-byte salt for AES-GCM (RFC 5282).
    if (suite->encr_keylen <= 0 || (suite->encr_keylen % 8) != 0)
    {
        return PROTO_FALSE;
    }
    size_t ek = (size_t)(suite->encr_keylen / 8);
    if (suite->encr == IKE_ENCR_AES_GCM_16)
    {
        ek += PC_IKE_GCM_SALT_LEN;
    }
    if (ek == 0 || ek > PC_IKE_SK_MAX)
    {
        return PROTO_FALSE;
    }
    out->sk_e = ek;
    return PROTO_TRUE;
}

proto_bool pc_ike_sa_keys_from_init(IkeSa *sa, const uint8_t *our_dh_priv, size_t our_dh_priv_len,
                                    const uint8_t *peer_ke, size_t peer_ke_len, const uint8_t *ni, size_t ni_len,
                                    const uint8_t *nr, size_t nr_len)
{
    if (!sa || !our_dh_priv || !peer_ke || !ni || !nr)
    {
        return PROTO_FALSE;
    }
    IkeKeyLengths lens;
    if (!pc_ike_suite_keylengths(&sa->suite, &lens))
    {
        return PROTO_FALSE;
    }

    uint8_t shared[PC_IKE_X25519_LEN]; // the only supported group (31) yields a 32-byte secret
    size_t sh =
        pc_ike_dh_compute(sa->suite.dh, our_dh_priv, our_dh_priv_len, peer_ke, peer_ke_len, shared, sizeof(shared));
    if (sh == 0)
    {
        return PROTO_FALSE;
    }
    // SKEYSEED + SK_* are order-independent in the SPIs/nonces, so both peers derive identical keys.
    return pc_ike_derive_keys(shared, sh, ni, ni_len, nr, nr_len, sa->init_spi, sa->resp_spi, &lens, &sa->keys);
}

// ── tier 2: initiator IKE_SA_INIT handshake driver ─────────────────────────────────────────────

size_t pc_ike_initiator_start(IkeHandshake *hs, const uint8_t our_spi[PC_IKE_SPI_LEN],
                              const uint8_t our_dh_priv[PC_IKE_X25519_LEN], const uint8_t our_dh_pub[PC_IKE_X25519_LEN],
                              const uint8_t *our_nonce, size_t nonce_len, const IkeSuite *suite,
                              const IkeTransform *transforms, uint8_t num_transforms, uint8_t *out, size_t out_cap)
{
    if (!hs || !our_spi || !our_dh_priv || !our_dh_pub || !our_nonce || !suite || !transforms || num_transforms == 0)
    {
        return 0;
    }
    if (nonce_len == 0 || nonce_len > PC_IKE_NONCE_MAX)
    {
        return 0;
    }

    memset(hs, 0, sizeof(*hs));
    memcpy(hs->sa.init_spi, our_spi, PC_IKE_SPI_LEN); // resp_spi stays 0 until the response
    hs->sa.is_initiator = PROTO_TRUE;
    hs->sa.suite = *suite;
    memcpy(hs->our_dh_priv, our_dh_priv, PC_IKE_X25519_LEN);
    memcpy(hs->our_nonce, our_nonce, nonce_len);
    hs->our_nonce_len = (uint16_t)nonce_len;

    uint8_t zero_spi[PC_IKE_SPI_LEN] = {0};
    size_t n = pc_ike_sa_init_build(out, out_cap, our_spi, zero_spi, 0, /*is_response=*/PROTO_FALSE, 1, transforms,
                                    num_transforms, suite->dh, our_dh_pub, PC_IKE_X25519_LEN, our_nonce, nonce_len);
    if (n == 0 || n > PC_IKE_MSG_MAX) // RealMessage1 must fit the stored copy (needed for the AUTH octets)
    {
        hs->state = IKE_ST_FAILED;
        return 0;
    }
    memcpy(hs->init_msg, out, n);
    hs->init_msg_len = (uint16_t)n;
    hs->state = IKE_ST_SA_INIT_SENT;
    return n;
}

proto_bool pc_ike_initiator_on_sa_init(IkeHandshake *hs, const uint8_t *resp, size_t resp_len)
{
    if (!hs || !resp || hs->state != IKE_ST_SA_INIT_SENT)
    {
        return PROTO_FALSE;
    }

    IkeSaInitMsg m;
    if (!pc_ike_sa_init_parse(resp, resp_len, &m))
    {
        hs->state = IKE_ST_FAILED;
        return PROTO_FALSE;
    }
    // It must be a RESPONSE echoing our initiator SPI, offering the group we proposed.
    if (!m.is_response || memcmp(m.init_spi, hs->sa.init_spi, PC_IKE_SPI_LEN) != 0 || m.dh_group != hs->sa.suite.dh)
    {
        hs->state = IKE_ST_FAILED;
        return PROTO_FALSE;
    }
    memcpy(hs->sa.resp_spi, m.resp_spi, PC_IKE_SPI_LEN);

    // Capture Nr for the IKE_AUTH octets (must fit our bounded store).
    if (m.nonce_len == 0 || m.nonce_len > PC_IKE_NONCE_MAX)
    {
        hs->state = IKE_ST_FAILED;
        return PROTO_FALSE;
    }
    memcpy(hs->peer_nonce, m.nonce, m.nonce_len);
    hs->peer_nonce_len = (uint16_t)m.nonce_len;

    // Stash RealMessage2 (the responder's IKE_SA_INIT) - the responder's AUTH later signs over it.
    if (resp_len > PC_IKE_MSG_MAX)
    {
        hs->state = IKE_ST_FAILED;
        return PROTO_FALSE;
    }
    memcpy(hs->resp_msg, resp, resp_len);
    hs->resp_msg_len = (uint16_t)resp_len;

    // Derive the SA keys: for the initiator, Ni = ours, Nr = the responder's.
    if (!pc_ike_sa_keys_from_init(&hs->sa, hs->our_dh_priv, PC_IKE_X25519_LEN, m.ke_data, m.ke_len, hs->our_nonce,
                                  hs->our_nonce_len, m.nonce, m.nonce_len))
    {
        hs->state = IKE_ST_FAILED;
        return PROTO_FALSE;
    }
    hs->state = IKE_ST_SA_INIT_DONE;
    return PROTO_TRUE;
}

size_t pc_ike_initiator_build_auth_psk(IkeHandshake *hs, IkeIdType idi_type, const uint8_t *idi_data, size_t idi_len,
                                       const uint8_t *psk, size_t psk_len, const uint8_t iv[PC_IKE_GCM_IV_LEN],
                                       uint8_t *out, size_t out_cap)
{
    if (!hs || !idi_data || !psk || !iv || !out)
    {
        return 0;
    }
    if (hs->state != IKE_ST_SA_INIT_DONE)
    {
        return 0;
    }

    // Build the inner chain IDi(next=AUTH) | AUTH(next=PC_NONE) into a scratch buffer.
    uint8_t inner[PC_IKE_MSG_MAX];
    size_t idn = pc_ike_id_build(inner, sizeof(inner), IKE_PL_AUTH, idi_type, idi_data, idi_len);
    if (idn == 0)
    {
        return 0;
    }
    // AUTH signs the IDi payload BODY (RestOfIDPayload = the payload minus its 4-byte generic header).
    const uint8_t *idi_body = inner + PC_IKE_PAYLOAD_HDR_LEN;
    size_t idi_body_len = idn - PC_IKE_PAYLOAD_HDR_LEN;
    uint8_t auth[PC_IKE_AUTH_LEN];
    if (!pc_ike_auth_psk(psk, psk_len, hs->init_msg, hs->init_msg_len, hs->peer_nonce, hs->peer_nonce_len,
                         hs->sa.keys.sk_pi, hs->sa.keys.sk_p_len, idi_body, idi_body_len, auth))
    {
        return 0;
    }
    size_t an = pc_ike_auth_build(inner + idn, sizeof(inner) - idn, IKE_PL_NONE, IKE_AUTH_PSK, auth, sizeof(auth));
    if (an == 0)
    {
        return 0;
    }

    // Wrap IDi | AUTH in the SK envelope: SK_ei = the 32-byte AES key || its 4-byte GCM salt (RFC 5282).
    size_t n = pc_ike_auth_msg_build(out, out_cap, hs->sa.init_spi, hs->sa.resp_spi, 1, /*is_response=*/PROTO_FALSE,
                                     IKE_PL_IDI, inner, idn + an, hs->sa.keys.sk_ei,
                                     hs->sa.keys.sk_ei + PC_IKE_AEAD_KEY_LEN, iv);
    if (n == 0)
    {
        return 0;
    }
    hs->state = IKE_ST_AUTH_SENT;
    return n;
}

proto_bool pc_ike_initiator_on_auth_psk(IkeHandshake *hs, const uint8_t *resp, size_t resp_len, const uint8_t *psk,
                                        size_t psk_len)
{
    if (!hs || !resp || !psk || hs->state != IKE_ST_AUTH_SENT)
    {
        return PROTO_FALSE;
    }
    if (resp_len == 0 || resp_len > PC_IKE_MSG_MAX)
    {
        hs->state = IKE_ST_FAILED;
        return PROTO_FALSE;
    }

    // Decrypt SK{ IDr | AUTH } into a scratch copy (open mutates the buffer) with SK_er (responder->initiator).
    uint8_t work[PC_IKE_MSG_MAX];
    memcpy(work, resp, resp_len);
    IkePayloadType first = IKE_PL_NONE;
    const uint8_t *inner = NULL;
    size_t inner_len = 0;
    if (!pc_ike_auth_msg_open(work, resp_len, hs->sa.keys.sk_er, hs->sa.keys.sk_er + PC_IKE_AEAD_KEY_LEN, &first,
                              &inner, &inner_len))
    {
        hs->state = IKE_ST_FAILED;
        return PROTO_FALSE;
    }

    // Locate the IDr + AUTH payloads in the decrypted inner chain.
    IkePayloadIter it;
    pc_ike_payload_iter_init(&it, first, inner, inner_len);
    IkePayload pl;
    const uint8_t *idr_body = NULL, *auth_body = NULL;
    size_t idr_body_len = 0, auth_body_len = 0;
    while (pc_ike_payload_next(&it, &pl))
    {
        if (pl.type == IKE_PL_IDR && !idr_body)
        {
            idr_body = pl.body;
            idr_body_len = pl.body_len;
        }
        else if (pl.type == IKE_PL_AUTH && !auth_body)
        {
            auth_body = pl.body;
            auth_body_len = pl.body_len;
        }
    }
    IkeAuthMethod method = IKE_AUTH_RESERVED;
    const uint8_t *authdata = NULL;
    size_t authdata_len = 0;
    if (!idr_body || !auth_body || !pc_ike_auth_parse(auth_body, auth_body_len, &method, &authdata, &authdata_len) ||
        method != IKE_AUTH_PSK || authdata_len != PC_IKE_AUTH_LEN)
    {
        hs->state = IKE_ST_FAILED;
        return PROTO_FALSE;
    }

    // Recompute the responder's AUTH = prf(prf(PSK,pad), RealMessage2 | Ni | prf(SK_pr, IDr')).
    uint8_t expect[PC_IKE_AUTH_LEN];
    if (!pc_ike_auth_psk(psk, psk_len, hs->resp_msg, hs->resp_msg_len, hs->our_nonce, hs->our_nonce_len,
                         hs->sa.keys.sk_pr, hs->sa.keys.sk_p_len, idr_body, idr_body_len, expect))
    {
        hs->state = IKE_ST_FAILED;
        return PROTO_FALSE;
    }
    // Constant-time compare (no early-out on the identity proof).
    uint8_t diff = 0;
    for (size_t i = 0; i < PC_IKE_AUTH_LEN; i++)
    {
        diff |= (uint8_t)(expect[i] ^ authdata[i]);
    }
    if (diff != 0)
    {
        hs->state = IKE_ST_FAILED;
        return PROTO_FALSE;
    }

    hs->state = IKE_ST_ESTABLISHED;
    return PROTO_TRUE;
}

// ── responder IKE_SA_INIT handshake driver ─────────────────────────────────────────────────────

size_t pc_ike_responder_on_sa_init(IkeHandshake *hs, const uint8_t *req, size_t req_len,
                                   const uint8_t our_spi[PC_IKE_SPI_LEN], const uint8_t our_dh_priv[PC_IKE_X25519_LEN],
                                   const uint8_t our_dh_pub[PC_IKE_X25519_LEN], const uint8_t *our_nonce,
                                   size_t nonce_len, const IkeSuite *suite, const IkeTransform *transforms,
                                   uint8_t num_transforms, uint8_t *out, size_t out_cap)
{
    if (!hs || !req || !our_spi || !our_dh_priv || !our_dh_pub || !our_nonce || !suite || !transforms ||
        num_transforms == 0)
    {
        return 0;
    }
    if (nonce_len == 0 || nonce_len > PC_IKE_NONCE_MAX || req_len > PC_IKE_MSG_MAX)
    {
        return 0;
    }

    IkeSaInitMsg m;
    if (!pc_ike_sa_init_parse(req, req_len, &m))
    {
        return 0;
    }
    // Must be a REQUEST (not a response) offering the group we accept; the nonce must fit our store.
    if (m.is_response || m.dh_group != suite->dh || m.nonce_len == 0 || m.nonce_len > PC_IKE_NONCE_MAX)
    {
        return 0;
    }

    memset(hs, 0, sizeof(*hs));
    hs->sa.is_initiator = PROTO_FALSE;
    hs->sa.suite = *suite;
    memcpy(hs->sa.init_spi, m.init_spi, PC_IKE_SPI_LEN); // the initiator's SPI, echoed
    memcpy(hs->sa.resp_spi, our_spi, PC_IKE_SPI_LEN);    // our chosen responder SPI
    memcpy(hs->our_dh_priv, our_dh_priv, PC_IKE_X25519_LEN);
    memcpy(hs->our_nonce, our_nonce, nonce_len); // Nr
    hs->our_nonce_len = (uint16_t)nonce_len;
    memcpy(hs->peer_nonce, m.nonce, m.nonce_len); // Ni
    hs->peer_nonce_len = (uint16_t)m.nonce_len;
    memcpy(hs->init_msg, req, req_len); // RealMessage1 = the request
    hs->init_msg_len = (uint16_t)req_len;

    // Emit the IKE_SA_INIT response (echo the initiator SPI, add ours, our KE + nonce).
    size_t n = pc_ike_sa_init_build(out, out_cap, m.init_spi, our_spi, 0, /*is_response=*/PROTO_TRUE, 1, transforms,
                                    num_transforms, suite->dh, our_dh_pub, PC_IKE_X25519_LEN, our_nonce, nonce_len);
    if (n == 0 || n > PC_IKE_MSG_MAX)
    {
        hs->state = IKE_ST_FAILED;
        return 0;
    }
    memcpy(hs->resp_msg, out, n); // RealMessage2 = our response
    hs->resp_msg_len = (uint16_t)n;

    // Derive the SA keys: Ni = the initiator's nonce (peer), Nr = ours.
    if (!pc_ike_sa_keys_from_init(&hs->sa, our_dh_priv, PC_IKE_X25519_LEN, m.ke_data, m.ke_len, hs->peer_nonce,
                                  hs->peer_nonce_len, hs->our_nonce, hs->our_nonce_len))
    {
        hs->state = IKE_ST_FAILED;
        return 0;
    }
    hs->state = IKE_ST_SA_INIT_DONE;
    return n;
}

// Constant-time 32-byte equality (no early-out on an identity proof).
static proto_bool ike_ct_eq32(const uint8_t *a, const uint8_t *b)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < PC_IKE_AUTH_LEN; i++)
    {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

// Find the IDi/IDr and AUTH payloads in a decrypted inner chain; returns false if either is missing.
static proto_bool ike_find_id_auth(IkePayloadType first, const uint8_t *inner, size_t inner_len, IkePayloadType id_type,
                                   const uint8_t **id_body, size_t *id_body_len, const uint8_t **authdata,
                                   size_t *authdata_len)
{
    IkePayloadIter it;
    pc_ike_payload_iter_init(&it, first, inner, inner_len);
    IkePayload pl;
    const uint8_t *auth_body = NULL;
    size_t auth_body_len = 0;
    *id_body = NULL;
    *id_body_len = 0;
    while (pc_ike_payload_next(&it, &pl))
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
    IkeAuthMethod method = IKE_AUTH_RESERVED;
    if (!*id_body || !auth_body || !pc_ike_auth_parse(auth_body, auth_body_len, &method, authdata, authdata_len) ||
        method != IKE_AUTH_PSK || *authdata_len != PC_IKE_AUTH_LEN)
    {
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

size_t pc_ike_responder_on_auth_psk(IkeHandshake *hs, const uint8_t *req, size_t req_len, const uint8_t *psk,
                                    size_t psk_len, IkeIdType idr_type, const uint8_t *idr_data, size_t idr_len,
                                    const uint8_t iv[PC_IKE_GCM_IV_LEN], uint8_t *out, size_t out_cap)
{
    if (!hs || !req || !psk || !idr_data || !iv || !out)
    {
        return 0;
    }
    if (hs->state != IKE_ST_SA_INIT_DONE || hs->sa.is_initiator)
    {
        return 0;
    }
    if (req_len == 0 || req_len > PC_IKE_MSG_MAX)
    {
        hs->state = IKE_ST_FAILED;
        return 0;
    }

    // Decrypt the initiator's SK{ IDi | AUTH } with SK_ei (initiator->responder).
    uint8_t work[PC_IKE_MSG_MAX];
    memcpy(work, req, req_len);
    IkePayloadType first = IKE_PL_NONE;
    const uint8_t *inner = NULL;
    size_t inner_len = 0;
    const uint8_t *idi_body = NULL, *authdata = NULL;
    size_t idi_body_len = 0, authdata_len = 0;
    if (!pc_ike_auth_msg_open(work, req_len, hs->sa.keys.sk_ei, hs->sa.keys.sk_ei + PC_IKE_AEAD_KEY_LEN, &first, &inner,
                              &inner_len) ||
        !ike_find_id_auth(first, inner, inner_len, IKE_PL_IDI, &idi_body, &idi_body_len, &authdata, &authdata_len))
    {
        hs->state = IKE_ST_FAILED;
        return 0;
    }

    // Verify the initiator's AUTH over RealMessage1 | Nr(our_nonce) | prf(SK_pi, IDi').
    uint8_t expect[PC_IKE_AUTH_LEN];
    if (!pc_ike_auth_psk(psk, psk_len, hs->init_msg, hs->init_msg_len, hs->our_nonce, hs->our_nonce_len,
                         hs->sa.keys.sk_pi, hs->sa.keys.sk_p_len, idi_body, idi_body_len, expect) ||
        !ike_ct_eq32(expect, authdata))
    {
        hs->state = IKE_ST_FAILED;
        return 0;
    }

    // Build our IDr | AUTH: AUTH over RealMessage2 | Ni(peer_nonce) | prf(SK_pr, IDr').
    uint8_t rinner[PC_IKE_MSG_MAX];
    size_t ridn = pc_ike_id_build(rinner, sizeof(rinner), IKE_PL_AUTH, idr_type, idr_data, idr_len);
    if (ridn == 0)
    {
        hs->state = IKE_ST_FAILED;
        return 0;
    }
    uint8_t rauth[PC_IKE_AUTH_LEN];
    if (!pc_ike_auth_psk(psk, psk_len, hs->resp_msg, hs->resp_msg_len, hs->peer_nonce, hs->peer_nonce_len,
                         hs->sa.keys.sk_pr, hs->sa.keys.sk_p_len, rinner + PC_IKE_PAYLOAD_HDR_LEN,
                         ridn - PC_IKE_PAYLOAD_HDR_LEN, rauth))
    {
        hs->state = IKE_ST_FAILED;
        return 0;
    }
    size_t ran =
        pc_ike_auth_build(rinner + ridn, sizeof(rinner) - ridn, IKE_PL_NONE, IKE_AUTH_PSK, rauth, sizeof(rauth));
    if (ran == 0)
    {
        hs->state = IKE_ST_FAILED;
        return 0;
    }
    // Wrap IDr | AUTH in SK{} keyed by SK_er (responder->initiator).
    size_t n =
        pc_ike_auth_msg_build(out, out_cap, hs->sa.init_spi, hs->sa.resp_spi, 1, /*is_response=*/PROTO_TRUE, IKE_PL_IDR,
                              rinner, ridn + ran, hs->sa.keys.sk_er, hs->sa.keys.sk_er + PC_IKE_AEAD_KEY_LEN, iv);
    if (n == 0)
    {
        hs->state = IKE_ST_FAILED;
        return 0;
    }
    hs->state = IKE_ST_ESTABLISHED;
    return n;
}

// ── tier 2: post-auth exchanges (INFORMATIONAL / CREATE_CHILD_SA) over an established SA ─────────

// Build an SK-encrypted message we are SENDING over @p sa: egress-keyed (SK_ei from the original
// initiator, SK_er from the responder), the flags carrying INITIATOR/RESPONSE independently.
static size_t sk_send_build(const IkeSa *sa, proto_bool is_response, uint32_t msg_id, IkeExchange exchange,
                            IkePayloadType first_inner_type, const uint8_t *inner, size_t inner_len, const uint8_t *iv,
                            uint8_t *out, size_t out_cap)
{
    if (!sa || !iv || !out)
    {
        return 0;
    }
    const uint8_t *key = sa->is_initiator ? sa->keys.sk_ei : sa->keys.sk_er;
    uint8_t flags =
        (uint8_t)((sa->is_initiator ? PC_IKE_FLAG_INITIATOR : 0) | (is_response ? PC_IKE_FLAG_RESPONSE : 0));
    return sk_message_build(out, out_cap, sa->init_spi, sa->resp_spi, msg_id, exchange, flags, first_inner_type, inner,
                            inner_len, key, key + PC_IKE_AEAD_KEY_LEN, iv);
}

size_t pc_ike_informational_build(const IkeSa *sa, proto_bool is_response, uint32_t msg_id,
                                  IkePayloadType first_inner_type, const uint8_t *inner, size_t inner_len,
                                  const uint8_t iv[PC_IKE_GCM_IV_LEN], uint8_t *out, size_t out_cap)
{
    return sk_send_build(sa, is_response, msg_id, IKE_INFORMATIONAL, first_inner_type, inner, inner_len, iv, out,
                         out_cap);
}

size_t pc_ike_create_child_sa_build(const IkeSa *sa, proto_bool is_response, uint32_t msg_id,
                                    IkePayloadType first_inner_type, const uint8_t *inner, size_t inner_len,
                                    const uint8_t iv[PC_IKE_GCM_IV_LEN], uint8_t *out, size_t out_cap)
{
    return sk_send_build(sa, is_response, msg_id, IKE_CREATE_CHILD_SA, first_inner_type, inner, inner_len, iv, out,
                         out_cap);
}

proto_bool pc_ike_child_keymat(const uint8_t *sk_d, size_t sk_d_len, const uint8_t *dh_secret, size_t dh_len,
                               const uint8_t *ni, size_t ni_len, const uint8_t *nr, size_t nr_len, uint8_t *out,
                               size_t out_len)
{
    if (!sk_d || !ni || !nr || !out || out_len == 0)
    {
        return PROTO_FALSE;
    }
    if (ni_len > PC_IKE_NONCE_MAX || nr_len > PC_IKE_NONCE_MAX || dh_len > PC_IKE_X25519_LEN)
    {
        return PROTO_FALSE;
    }

    // KEYMAT = prf+(SK_d, [g^ir |] Ni | Nr) (RFC 7296 §2.17); g^ir is present only for a PFS rekey.
    uint8_t seed[PC_IKE_X25519_LEN + 2 * PC_IKE_NONCE_MAX];
    size_t o = 0;
    if (dh_secret && dh_len)
    {
        memcpy(seed, dh_secret, dh_len);
        o = dh_len;
    }
    memcpy(seed + o, ni, ni_len);
    o += ni_len;
    memcpy(seed + o, nr, nr_len);
    o += nr_len;
    return pc_ike_prf_plus(sk_d, sk_d_len, seed, o, out, out_len);
}

proto_bool pc_ike_informational_open(const IkeSa *sa, uint8_t *msg, size_t len, IkePayloadType *first_inner_type,
                                     const uint8_t **inner_out, size_t *inner_len_out)
{
    if (!sa)
    {
        return PROTO_FALSE;
    }
    // Ingress direction is the peer's egress: SK_er if the peer is the responder (we are the initiator), else SK_ei.
    const uint8_t *key = sa->is_initiator ? sa->keys.sk_er : sa->keys.sk_ei;
    return pc_ike_auth_msg_open(msg, len, key, key + PC_IKE_AEAD_KEY_LEN, first_inner_type, inner_out, inner_len_out);
}

// ── tier 2: IKE_AUTH RSA-2048 (certificate) verify (RFC 7296 §2.15, RFC 7427) ───────────────────

proto_bool pc_ike_auth_verify_rsa_sha256(const uint8_t *n_be, const uint8_t *e_be4, const uint8_t *sig, size_t sig_len,
                                         uint8_t *scratch, size_t scratch_cap, const uint8_t *real, size_t real_len,
                                         const uint8_t *nonce, size_t nonce_len, const uint8_t *sk_p, size_t sk_p_len,
                                         const uint8_t *id_body, size_t id_body_len)
{
    if (!n_be || !e_be4 || !sig)
    {
        return PROTO_FALSE;
    }
    size_t n = pc_ike_signed_octets(scratch, scratch_cap, real, real_len, nonce, nonce_len, sk_p, sk_p_len, id_body,
                                    id_body_len);
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    // The device signs with its own ECDSA key; this verifies a PEER whose CERT is RSA-2048 (SHA-256).
    return pc_rsa_verify(n_be, e_be4, scratch, n, sig, sig_len, PC_RSA_HASH_SHA256) == 0;
}

#endif // PC_ENABLE_IKEV2
