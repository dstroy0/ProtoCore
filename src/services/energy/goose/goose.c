// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file goose.c
 * @brief IEC 61850 GOOSE publisher codec (see goose.h).
 */

#include "services/energy/goose/goose.h"

#if PC_ENABLE_GOOSE

// Number of octets to encode a BER definite length.
static size_t len_octets(size_t len)
{
    if (len < 0x80)
    {
        return 1;
    }
    size_t n = 0;
    for (size_t v = len; v; v >>= 8)
    {
        n++;
    }
    return 1 + n;
}

// Write a BER length at p; returns octets written.
static size_t write_len(uint8_t *p, size_t len)
{
    if (len < 0x80)
    {
        p[0] = (uint8_t)len;
        return 1;
    }
    size_t nb = 0;
    for (size_t v = len; v; v >>= 8)
    {
        nb++;
    }
    p[0] = (uint8_t)(0x80 | nb);
    for (size_t i = 0; i < nb; i++)
    {
        p[1 + i] = (uint8_t)(len >> (8 * (nb - 1 - i)));
    }
    return 1 + nb;
}

// Append a TLV. Returns false on overflow.
static proto_bool tlv(uint8_t *out, size_t cap, size_t *n, uint8_t tag, const uint8_t *val, size_t val_len)
{
    size_t need = 1 + len_octets(val_len) + val_len;
    if (*n + need > cap)
    {
        return PROTO_FALSE;
    }
    out[(*n)++] = tag;
    *n += write_len(out + *n, val_len);
    if (val_len)
    {
        memcpy(out + *n, val, val_len);
        *n += val_len;
    }
    return PROTO_TRUE;
}

// Append a BER INTEGER (unsigned value, minimal length, leading 0x00 if the MSB is set).
static proto_bool pc_ber_int(uint8_t *out, size_t cap, size_t *n, uint8_t tag, uint32_t v)
{
    uint8_t buf[5];
    size_t k = 0;
    // big-endian minimal
    if (v == 0)
    {
        buf[k++] = 0;
    }
    else
    {
        uint8_t tmp[4];
        size_t t = 0;
        for (uint32_t x = v; x; x >>= 8)
        {
            tmp[t++] = (uint8_t)x;
        }
        if (tmp[t - 1] & 0x80)
        {
            buf[k++] = 0x00; // keep it positive
        }
        for (size_t i = 0; i < t; i++)
        {
            buf[k++] = tmp[t - 1 - i];
        }
    }
    return tlv(out, cap, n, tag, buf, k);
}

static proto_bool pc_ber_str(uint8_t *out, size_t cap, size_t *n, uint8_t tag, const char *s)
{
    return tlv(out, cap, n, tag, (const uint8_t *)(s ? s : ""), s ? strnlen(s, cap + 1) : 0);
}

static proto_bool pc_ber_bool(uint8_t *out, size_t cap, size_t *n, uint8_t tag, proto_bool v)
{
    uint8_t b = v ? 0xFF : 0x00;
    return tlv(out, cap, n, tag, &b, 1);
}

// Read one definite-length BER TLV at *p within [*p, end). Advances *p past the value on success.
// Rejects truncation and indefinite / over-4-octet lengths (a GOOSE PDU never needs them).
static proto_bool ber_read(const uint8_t *end, const uint8_t **p, uint8_t *tag, const uint8_t **vptr, size_t *vlen)
{
    const uint8_t *q = *p;
    if (q + 2 > end)
    {
        return PROTO_FALSE;
    }
    uint8_t t = *q++;
    size_t len = *q++;
    if (len & 0x80)
    {
        size_t nb = len & 0x7F;
        if (nb == 0 || nb > 4 || q + nb > end)
        {
            return PROTO_FALSE;
        }
        len = 0;
        for (size_t i = 0; i < nb; i++)
        {
            len = (len << 8) | *q++;
        }
    }
    if (len > (size_t)(end - q))
    {
        return PROTO_FALSE;
    }
    *tag = t;
    *vptr = q;
    *vlen = len;
    *p = q + len;
    return PROTO_TRUE;
}

// A big-endian unsigned BER INTEGER value (a leading 0x00 sign octet folds in harmlessly).
static uint32_t ber_uint(const uint8_t *v, size_t n)
{
    uint32_t x = 0;
    for (size_t i = 0; i < n; i++)
    {
        x = (x << 8) | v[i];
    }
    return x;
}

size_t pc_goose_pdu(const pc_goose *g, uint8_t *out, size_t cap)
{
    if (!g || !out)
    {
        return 0;
    }
    const size_t RESERVE = 4; // 0x61 tag + up to 3 length octets
    if (cap < RESERVE)
    {
        return 0;
    }

    size_t n = RESERVE; // build the content after the reserved header area
    static const uint8_t ZT[8] = {0};
    proto_bool ok =
        pc_ber_str(out, cap, &n, 0x80, g->gocb_ref) && pc_ber_int(out, cap, &n, 0x81, g->time_allowed_to_live) &&
        pc_ber_str(out, cap, &n, 0x82, g->dat_set) && pc_ber_str(out, cap, &n, 0x83, g->go_id) &&
        tlv(out, cap, &n, 0x84, g->t ? g->t : ZT, 8) && pc_ber_int(out, cap, &n, 0x85, g->st_num) &&
        pc_ber_int(out, cap, &n, 0x86, g->sq_num) && pc_ber_bool(out, cap, &n, 0x87, g->simulation) &&
        pc_ber_int(out, cap, &n, 0x88, g->conf_rev) && pc_ber_bool(out, cap, &n, 0x89, g->nds_com) &&
        pc_ber_int(out, cap, &n, 0x8A, g->num_entries) && tlv(out, cap, &n, 0xAB, g->all_data, g->all_data_len);
    if (!ok)
    {
        return 0;
    }

    size_t content_len = n - RESERVE;
    size_t hdr = 1 + len_octets(content_len);
    // Move the content down so the 0x61 tag + length sit immediately before it (no gap).
    memmove(out + hdr, out + RESERVE, content_len);
    out[0] = 0x61;
    write_len(out + 1, content_len);
    return hdr + content_len;
}

size_t pc_goose_frame(const uint8_t *dst, const uint8_t *src, uint16_t appid, const pc_goose *g, uint8_t *out,
                      size_t cap)
{
    if (!dst || !src || !g || !out || cap < 22)
    {
        return 0;
    }
    // Ethernet II header (ethertype 0x88B8 = GOOSE).
    memcpy(out, dst, 6);
    memcpy(out + 6, src, 6);
    out[12] = 0x88;
    out[13] = 0xB8;
    // GOOSE header: APPID(2), length(2, filled below), reserved1(2), reserved2(2).
    out[14] = (uint8_t)(appid >> 8);
    out[15] = (uint8_t)appid;
    out[18] = 0;
    out[19] = 0;
    out[20] = 0;
    out[21] = 0;

    size_t pdu = pc_goose_pdu(g, out + 22, cap - 22);
    if (!pdu)
    {
        return 0;
    }
    uint16_t goose_len = (uint16_t)(8 + pdu); // GOOSE header (8) + APDU
    out[16] = (uint8_t)(goose_len >> 8);
    out[17] = (uint8_t)goose_len;
    return 22 + pdu;
}

proto_bool pc_goose_parse_frame(const uint8_t *buf, size_t len, pc_goose_rx *out)
{
    if (!buf || !out || len < 24) // 14 Ethernet + 8 GOOSE header + a minimal PDU
    {
        return PROTO_FALSE;
    }
    if (buf[12] != 0x88 || buf[13] != 0xB8) // GOOSE ethertype
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    out->appid = (uint16_t)((buf[14] << 8) | buf[15]);

    const uint8_t *end = buf + len;
    const uint8_t *p = buf + 22; // the IECGoosePdu begins after the 8-octet GOOSE header
    uint8_t tag = 0;
    const uint8_t *v = NULL;
    size_t vlen = 0;
    if (!ber_read(end, &p, &tag, &v, &vlen) || tag != 0x61) // the outer IECGoosePdu SEQUENCE
    {
        return PROTO_FALSE;
    }

    const uint8_t *inner_end = v + vlen;
    const uint8_t *ip = v;
    while (ip < inner_end)
    {
        if (!ber_read(inner_end, &ip, &tag, &v, &vlen))
        {
            return PROTO_FALSE;
        }
        switch (tag)
        {
        case 0x80:
            out->gocb_ref = (const char *)v;
            out->gocb_ref_len = vlen;
            break;
        case 0x81:
            out->time_allowed_to_live = ber_uint(v, vlen);
            break;
        case 0x82:
            out->dat_set = (const char *)v;
            out->dat_set_len = vlen;
            break;
        case 0x83:
            out->go_id = (const char *)v;
            out->go_id_len = vlen;
            break;
        case 0x84:
            if (vlen == 8)
            {
                out->t = v;
            }
            break;
        case 0x85:
            out->st_num = ber_uint(v, vlen);
            break;
        case 0x86:
            out->sq_num = ber_uint(v, vlen);
            break;
        case 0x87:
            out->simulation = (vlen >= 1 && v[0] != 0);
            break;
        case 0x88:
            out->conf_rev = ber_uint(v, vlen);
            break;
        case 0x89:
            out->nds_com = (vlen >= 1 && v[0] != 0);
            break;
        case 0x8A:
            out->num_entries = ber_uint(v, vlen);
            break;
        case 0xAB:
            out->all_data = v;
            out->all_data_len = vlen;
            break;
        default:
            break; // unknown / future tags are skipped
        }
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_GOOSE
