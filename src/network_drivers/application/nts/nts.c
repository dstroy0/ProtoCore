// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file nts.c
 * @brief Network Time Security wire codec (see nts.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_NTS

#include "mmgr/protomem/protomem.h"
#include "network_drivers/application/nts/nts.h"

PROTOCORE_BEGIN_DECLS

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

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_nts_ef(uint8_t *restrict work);
void protocore_nts_ke_record(uint8_t *restrict work);

void protocore_nts_ke_record(uint8_t *restrict work)
{
    (void)work;
    proto_bool critical = NtsV.ke_record_args.critical;
    uint16_t type = NtsV.ke_record_args.type;
    const uint8_t *body = NtsV.ke_record_args.body;
    size_t body_len = NtsV.ke_record_args.body_len;
    uint8_t *out = NtsV.ke_record_args.out;
    size_t cap = NtsV.ke_record_args.cap;

    if (!out || (body_len && !body) || body_len > 0xFFFF)
    {
        NtsV.n = 0;
        return;
    }
    size_t n = 4 + body_len;
    if (n > cap)
    {
        NtsV.n = 0;
        return;
    }
    put_u16(out, (uint16_t)((type & 0x7FFF) | (critical ? NTS_KE_CRITICAL : 0)));
    put_u16(out + 2, (uint16_t)body_len);
    if (body_len)
    {
        mem.cpy(out + 4, body, body_len);
    }
    NtsV.n = n;
}

void protocore_nts_ke_request(uint8_t *restrict work)
{
    uint8_t *out = NtsV.ke_request_args.out;
    size_t cap = NtsV.ke_request_args.cap;

    uint8_t proto[2];
    put_u16(proto, NTS_NEXT_PROTO_NTPV4);
    uint8_t aead[2];
    put_u16(aead, NTS_AEAD_AES_SIV_CMAC_256);

    size_t n = 0;
    size_t r;
    NtsV.ke_record_args.critical = PROTO_TRUE;
    NtsV.ke_record_args.type = NTS_KE_NEXT_PROTOCOL;
    NtsV.ke_record_args.body = proto;
    NtsV.ke_record_args.body_len = 2;
    NtsV.ke_record_args.out = out + n;
    NtsV.ke_record_args.cap = cap - n;
    protocore_nts_ke_record(work);
    r = NtsV.n;
    if (!r)
    {
        NtsV.n = 0;
        return;
    }
    n += r;
    NtsV.ke_record_args.critical = PROTO_TRUE;
    NtsV.ke_record_args.type = NTS_KE_AEAD_ALGORITHM;
    NtsV.ke_record_args.body = aead;
    NtsV.ke_record_args.body_len = 2;
    NtsV.ke_record_args.out = out + n;
    NtsV.ke_record_args.cap = cap - n;
    protocore_nts_ke_record(work);
    r = NtsV.n;
    if (!r)
    {
        NtsV.n = 0;
        return;
    }
    n += r;
    NtsV.ke_record_args.critical = PROTO_TRUE;
    NtsV.ke_record_args.type = NTS_KE_END_OF_MESSAGE;
    NtsV.ke_record_args.body = NULL;
    NtsV.ke_record_args.body_len = 0;
    NtsV.ke_record_args.out = out + n;
    NtsV.ke_record_args.cap = cap - n;
    protocore_nts_ke_record(work);
    r = NtsV.n;
    if (!r)
    {
        NtsV.n = 0;
        return;
    }
    n += r;
    NtsV.n = n;
}

void protocore_nts_ke_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = NtsV.ke_parse_args.buf;
    size_t len = NtsV.ke_parse_args.len;
    protocore_nts_ke_cb cb = NtsV.ke_parse_args.cb;
    void *arg = NtsV.ke_parse_args.arg;

    size_t off = 0;
    while (off + 4 <= len)
    {
        uint16_t tf = get_u16(buf + off);
        uint16_t blen = get_u16(buf + off + 2);
        proto_bool critical = (tf & NTS_KE_CRITICAL) != 0;
        uint16_t type = (uint16_t)(tf & 0x7FFF);
        if (off + 4 + blen > len)
        {
            NtsV.ok = PROTO_FALSE; // truncated body
            return;
        }
        if (cb)
        {
            cb(critical, type, blen ? (buf + off + 4) : NULL, blen, arg);
        }
        off += 4 + blen;
        if (type == NTS_KE_END_OF_MESSAGE)
        {
            // RFC 8915 sec 4.1.1: End of Message is the final record, so octets past it are malformed.
            NtsV.ok = off == len ? PROTO_TRUE : PROTO_FALSE;
            return;
        }
    }
    NtsV.ok = PROTO_FALSE; // no End-of-Message record
}

void protocore_nts_ef(uint8_t *restrict work)
{
    (void)work;
    uint16_t field_type = NtsV.ef_args.field_type;
    const uint8_t *value = NtsV.ef_args.value;
    size_t value_len = NtsV.ef_args.value_len;
    uint8_t *out = NtsV.ef_args.out;
    size_t cap = NtsV.ef_args.cap;

    if (!out || (value_len && !value))
    {
        NtsV.n = 0;
        return;
    }
    // RFC 7822: Length = type + length + value + padding, a multiple of 4 (min 4).
    size_t total = 4 + value_len;
    size_t padded = (total + 3) & ~(size_t)3;
    if (padded > 0xFFFF || padded > cap)
    {
        NtsV.n = 0;
        return;
    }
    put_u16(out, field_type);
    put_u16(out + 2, (uint16_t)padded);
    if (value_len)
    {
        mem.cpy(out + 4, value, value_len);
    }
    for (size_t i = total; i < padded; i++)
    {
        out[i] = 0; // zero padding
    }
    NtsV.n = padded;
}

void protocore_nts_ef_unique_id(uint8_t *restrict work)
{
    const uint8_t *nonce = NtsV.ef_unique_id_args.nonce;
    size_t nonce_len = NtsV.ef_unique_id_args.nonce_len;
    uint8_t *out = NtsV.ef_unique_id_args.out;
    size_t cap = NtsV.ef_unique_id_args.cap;

    NtsV.ef_args.field_type = NTS_EF_UNIQUE_IDENTIFIER;
    NtsV.ef_args.value = nonce;
    NtsV.ef_args.value_len = nonce_len;
    NtsV.ef_args.out = out;
    NtsV.ef_args.cap = cap;
    protocore_nts_ef(work);
}

void protocore_nts_ef_cookie(uint8_t *restrict work)
{
    const uint8_t *cookie = NtsV.ef_cookie_args.cookie;
    size_t cookie_len = NtsV.ef_cookie_args.cookie_len;
    uint8_t *out = NtsV.ef_cookie_args.out;
    size_t cap = NtsV.ef_cookie_args.cap;

    NtsV.ef_args.field_type = NTS_EF_COOKIE;
    NtsV.ef_args.value = cookie;
    NtsV.ef_args.value_len = cookie_len;
    NtsV.ef_args.out = out;
    NtsV.ef_args.cap = cap;
    protocore_nts_ef(work);
}

/** @brief The operands and the outcome. */
NtsVars NtsV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_NTS
