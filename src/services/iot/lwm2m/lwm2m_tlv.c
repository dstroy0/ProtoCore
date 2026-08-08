// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_lwm2m_tlv.c
 * @brief OMA LwM2M TLV writer + reader (pure, host-tested).
 */

#include "services/iot/lwm2m/lwm2m_tlv.h"

#if PC_ENABLE_LWM2M

void pc_lwm2m_tlv_init(Lwm2mTlvWriter *w, uint8_t *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->pos = 0;
    w->error = PROTO_FALSE;
}

proto_bool pc_lwm2m_tlv_write(Lwm2mTlvWriter *w, uint8_t id_type, uint16_t id, const uint8_t *value, size_t value_len)
{
    if (!w || (value_len && !value))
    {
        return PROTO_FALSE;
    }
    uint8_t type = (uint8_t)(id_type & LWM2M_TLV_IDKIND_MASK);
    proto_bool id16 = id > 0xFF; // a 16-bit identifier is needed past 255
    if (id16)
    {
        type |= LWM2M_TLV_ID16_FLAG;
    }

    size_t lenbytes;
    if (value_len <= LWM2M_TLV_INLINE_LEN_MASK) // 0..7 fits inline (length-type 0)
    {
        lenbytes = 0;
        type |= (uint8_t)value_len;
    }
    else if (value_len <= 0xFF) // 8-bit length field (length-type 1)
    {
        lenbytes = 1;
        type |= (uint8_t)(1 << LWM2M_TLV_LENTYPE_SHIFT);
    }
    else if (value_len <= 0xFFFF) // 16-bit length field (length-type 2)
    {
        lenbytes = 2;
        type |= (uint8_t)(2 << LWM2M_TLV_LENTYPE_SHIFT);
    }
    else if (value_len <= 0xFFFFFF) // 24-bit length field (length-type 3)
    {
        lenbytes = 3;
        type |= (uint8_t)(3 << LWM2M_TLV_LENTYPE_SHIFT);
    }
    else
    {
        return PROTO_FALSE;
    }

    size_t need = 1 + (id16 ? 2 : 1) + lenbytes + value_len;
    if (w->error || w->pos + need > w->cap)
    {
        w->error = PROTO_TRUE;
        return PROTO_FALSE;
    }

    w->buf[w->pos++] = type;
    if (id16)
    {
        w->buf[w->pos++] = (uint8_t)(id >> 8);
    }
    w->buf[w->pos++] = (uint8_t)id;
    for (size_t i = 0; i < lenbytes; i++)
    {
        w->buf[w->pos++] = (uint8_t)(value_len >> (8 * (lenbytes - 1 - i)));
    }
    if (value_len)
    {
        memcpy(w->buf + w->pos, value, value_len);
        w->pos += value_len;
    }
    return PROTO_TRUE;
}

proto_bool pc_lwm2m_tlv_write_int(Lwm2mTlvWriter *w, uint16_t id, int64_t v)
{
    size_t n;
    if (v >= -128 && v <= 127)
    {
        n = 1;
    }
    else if (v >= -32768 && v <= 32767)
    {
        n = 2;
    }
    else if (v >= -2147483648LL && v <= 2147483647LL)
    {
        n = 4;
    }
    else
    {
        n = 8;
    }
    uint8_t b[8];
    for (size_t i = 0; i < n; i++)
    {
        b[i] = (uint8_t)((uint64_t)v >> (8 * (n - 1 - i)));
    }
    return pc_lwm2m_tlv_write(w, LWM2M_TLV_RESOURCE, id, b, n);
}

proto_bool pc_lwm2m_tlv_write_bool(Lwm2mTlvWriter *w, uint16_t id, proto_bool v)
{
    uint8_t b = v ? 1 : 0;
    return pc_lwm2m_tlv_write(w, LWM2M_TLV_RESOURCE, id, &b, 1);
}

proto_bool pc_lwm2m_tlv_write_string(Lwm2mTlvWriter *w, uint16_t id, const char *s)
{
    if (!s)
    {
        return PROTO_FALSE;
    }
    return pc_lwm2m_tlv_write(w, LWM2M_TLV_RESOURCE, id, (const uint8_t *)s, strnlen(s, w->cap + 1));
}

proto_bool pc_lwm2m_tlv_write_float(Lwm2mTlvWriter *w, uint16_t id, double v)
{
    uint64_t bits;
    memcpy(&bits, &v, 8);
    uint8_t b[8];
    for (size_t i = 0; i < 8; i++)
    {
        b[i] = (uint8_t)(bits >> (8 * (7 - i))); // big-endian
    }
    return pc_lwm2m_tlv_write(w, LWM2M_TLV_RESOURCE, id, b, 8);
}

size_t pc_lwm2m_tlv_finish(Lwm2mTlvWriter *w)
{
    return w->error ? 0 : w->pos;
}

proto_bool pc_lwm2m_tlv_read(const uint8_t *buf, size_t len, size_t *pos, Lwm2mTlv *out)
{
    if (!buf || !pos || !out || *pos >= len)
    {
        return PROTO_FALSE;
    }
    size_t p = *pos;
    uint8_t type = buf[p++];
    proto_bool id16 = (type & LWM2M_TLV_ID16_FLAG) != 0;
    if (p + (id16 ? 2u : 1u) > len)
    {
        return PROTO_FALSE;
    }
    uint16_t id = buf[p++];
    if (id16)
    {
        id = (uint16_t)((id << 8) | buf[p++]);
    }

    uint8_t lentype = (uint8_t)((type >> LWM2M_TLV_LENTYPE_SHIFT) & LWM2M_TLV_LENTYPE_MASK);
    size_t vlen;
    if (lentype == 0)
    {
        vlen = type & LWM2M_TLV_INLINE_LEN_MASK;
    }
    else
    {
        if (p + lentype > len)
        {
            return PROTO_FALSE;
        }
        vlen = 0;
        for (uint8_t i = 0; i < lentype; i++)
        {
            vlen = (vlen << 8) | buf[p++];
        }
    }
    if (p + vlen > len)
    {
        return PROTO_FALSE;
    }

    out->id_type = (uint8_t)(type & LWM2M_TLV_IDKIND_MASK);
    out->id = id;
    out->value = buf + p;
    out->value_len = vlen;
    *pos = p + vlen;
    return PROTO_TRUE;
}

proto_bool pc_lwm2m_tlv_value_int(const uint8_t *value, size_t len, int64_t *out)
{
    if (!value || (len != 1 && len != 2 && len != 4 && len != 8))
    {
        return PROTO_FALSE;
    }
    int64_t r = (value[0] & 0x80) ? -1 : 0; // sign-extend from the MSB
    for (size_t i = 0; i < len; i++)
    {
        r = (int64_t)(((uint64_t)r << 8) | value[i]);
    }
    if (out)
    {
        *out = r;
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_LWM2M
