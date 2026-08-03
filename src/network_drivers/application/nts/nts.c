// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nts.c
 * @brief Network Time Security wire codec (see nts.h).
 */

#include "network_drivers/application/nts/nts.h"

#if PC_ENABLE_NTS

#include <string.h>

const char NTS_EXPORTER_LABEL[] = "EXPORTER-network-time-security";

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

size_t pc_nts_ke_record(proto_bool critical, uint16_t type, const uint8_t *body, size_t body_len, uint8_t *out,
                        size_t cap)
{
    if (!out || (body_len && !body) || body_len > 0xFFFF)
    {
        return 0;
    }
    size_t n = 4 + body_len;
    if (n > cap)
    {
        return 0;
    }
    put_u16(out, (uint16_t)((type & 0x7FFF) | (critical ? NTS_KE_CRITICAL : 0)));
    put_u16(out + 2, (uint16_t)body_len);
    if (body_len)
    {
        memcpy(out + 4, body, body_len);
    }
    return n;
}

size_t pc_nts_ke_request(uint8_t *out, size_t cap)
{
    uint8_t proto[2];
    put_u16(proto, NTS_NEXT_PROTO_NTPV4);
    uint8_t aead[2];
    put_u16(aead, NTS_AEAD_AES_SIV_CMAC_256);

    size_t n = 0;
    size_t r;
    r = pc_nts_ke_record(PROTO_TRUE, NTS_KE_NEXT_PROTOCOL, proto, 2, out + n, cap - n);
    if (!r)
    {
        return 0;
    }
    n += r;
    r = pc_nts_ke_record(PROTO_TRUE, NTS_KE_AEAD_ALGORITHM, aead, 2, out + n, cap - n);
    if (!r)
    {
        return 0;
    }
    n += r;
    r = pc_nts_ke_record(PROTO_TRUE, NTS_KE_END_OF_MESSAGE, NULL, 0, out + n, cap - n);
    if (!r)
    {
        return 0;
    }
    n += r;
    return n;
}

proto_bool pc_nts_ke_parse(const uint8_t *buf, size_t len, pc_nts_ke_cb cb, void *arg)
{
    size_t off = 0;
    proto_bool saw_end = PROTO_FALSE;
    while (off + 4 <= len)
    {
        uint16_t tf = get_u16(buf + off);
        uint16_t blen = get_u16(buf + off + 2);
        proto_bool critical = (tf & NTS_KE_CRITICAL) != 0;
        uint16_t type = (uint16_t)(tf & 0x7FFF);
        if (off + 4 + blen > len)
        {
            return PROTO_FALSE; // truncated body
        }
        if (cb)
        {
            cb(critical, type, blen ? (buf + off + 4) : NULL, blen, arg);
        }
        off += 4 + blen;
        if (type == NTS_KE_END_OF_MESSAGE)
        {
            saw_end = PROTO_TRUE;
            break;
        }
    }
    return saw_end; // a well-formed stream is terminated by an End-of-Message record
}

size_t pc_nts_ef(uint16_t field_type, const uint8_t *value, size_t value_len, uint8_t *out, size_t cap)
{
    if (!out || (value_len && !value))
    {
        return 0;
    }
    // RFC 7822: Length = type + length + value + padding, a multiple of 4 (min 4).
    size_t total = 4 + value_len;
    size_t padded = (total + 3) & ~(size_t)3;
    if (padded > 0xFFFF || padded > cap)
    {
        return 0;
    }
    put_u16(out, field_type);
    put_u16(out + 2, (uint16_t)padded);
    if (value_len)
    {
        memcpy(out + 4, value, value_len);
    }
    for (size_t i = total; i < padded; i++)
    {
        out[i] = 0; // zero padding
    }
    return padded;
}

size_t pc_nts_ef_unique_id(const uint8_t *nonce, size_t nonce_len, uint8_t *out, size_t cap)
{
    return pc_nts_ef(NTS_EF_UNIQUE_IDENTIFIER, nonce, nonce_len, out, cap);
}

size_t pc_nts_ef_cookie(const uint8_t *cookie, size_t cookie_len, uint8_t *out, size_t cap)
{
    return pc_nts_ef(NTS_EF_COOKIE, cookie, cookie_len, out, cap);
}

#endif // PC_ENABLE_NTS
