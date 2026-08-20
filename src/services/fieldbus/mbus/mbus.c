// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mbus.c
 * @brief Wired M-Bus (EN 13757) frame + data-record codec (pure, host-tested).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_MBUS

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/mbus/mbus.h"

PROTOCORE_BEGIN_DECLS

// The M-Bus checksum is the 8-bit arithmetic sum of the covered octets.
static uint8_t checksum(const uint8_t *p, size_t n)
{
    uint8_t s = 0;
    for (size_t i = 0; i < n; i++)
    {
        s = (uint8_t)(s + p[i]);
    }
    return s;
}

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void mbus_build_short(uint8_t *restrict work);
static void mbus_dif_data_len(uint8_t *restrict work);

static void mbus_build_ack(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Mbus.build_ack_args.buf;
    size_t cap = Mbus.build_ack_args.cap;

    if (!buf || cap < 1)
    {
        Mbus.n = 0;
        return;
    }
    buf[0] = MBUS_ACK;
    Mbus.n = 1;
}

static void mbus_build_short(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Mbus.build_short_args.buf;
    size_t cap = Mbus.build_short_args.cap;
    uint8_t c = Mbus.build_short_args.c;
    uint8_t a = Mbus.build_short_args.a;

    if (!buf || cap < 5)
    {
        Mbus.n = 0;
        return;
    }
    buf[0] = MBUS_START_SHORT;
    buf[1] = c;
    buf[2] = a;
    buf[3] = (uint8_t)(c + a); // checksum over C + A
    buf[4] = MBUS_STOP;
    Mbus.n = 5;
}

static void mbus_build_long(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Mbus.build_long_args.buf;
    size_t cap = Mbus.build_long_args.cap;
    uint8_t c = Mbus.build_long_args.c;
    uint8_t a = Mbus.build_long_args.a;
    uint8_t ci = Mbus.build_long_args.ci;
    const uint8_t *data = Mbus.build_long_args.data;
    uint8_t data_len = Mbus.build_long_args.data_len;

    if (!buf || data_len > MBUS_MAX_DATA || (data_len && !data))
    {
        Mbus.n = 0;
        return;
    }
    uint8_t L = (uint8_t)(3 + data_len); // L counts C + A + CI + user data
    size_t total = (size_t)6 + L;        // 68 L L 68 [L octets] CS 16
    if (cap < total)
    {
        Mbus.n = 0;
        return;
    }
    buf[0] = MBUS_START_LONG;
    buf[1] = L;
    buf[2] = L;
    buf[3] = MBUS_START_LONG;
    buf[4] = c;
    buf[5] = a;
    buf[6] = ci;
    if (data_len)
    {
        mem.cpy(buf + 7, data, data_len);
    }
    buf[4 + L] = checksum(buf + 4, L); // sum of C..end of user data
    buf[5 + L] = MBUS_STOP;
    Mbus.n = total;
}

static void mbus_build_snd_nke(uint8_t *restrict work)
{
    uint8_t *buf = Mbus.build_snd_nke_args.buf;
    size_t cap = Mbus.build_snd_nke_args.cap;
    uint8_t a = Mbus.build_snd_nke_args.a;

    Mbus.build_short_args.buf = buf;
    Mbus.build_short_args.cap = cap;
    Mbus.build_short_args.c = MBUS_C_SND_NKE;
    Mbus.build_short_args.a = a;
    mbus_build_short(work);
}

static void mbus_build_req_ud2(uint8_t *restrict work)
{
    uint8_t *buf = Mbus.build_req_ud2_args.buf;
    size_t cap = Mbus.build_req_ud2_args.cap;
    uint8_t a = Mbus.build_req_ud2_args.a;
    proto_bool fcb = Mbus.build_req_ud2_args.fcb;

    Mbus.build_short_args.buf = buf;
    Mbus.build_short_args.cap = cap;
    Mbus.build_short_args.c = (uint8_t)(fcb ? 0x7Bu : MBUS_C_REQ_UD2);
    Mbus.build_short_args.a = a;
    mbus_build_short(work);
}

static void mbus_build_req_ud1(uint8_t *restrict work)
{
    uint8_t *buf = Mbus.build_req_ud1_args.buf;
    size_t cap = Mbus.build_req_ud1_args.cap;
    uint8_t a = Mbus.build_req_ud1_args.a;
    proto_bool fcb = Mbus.build_req_ud1_args.fcb;

    Mbus.build_short_args.buf = buf;
    Mbus.build_short_args.cap = cap;
    Mbus.build_short_args.c = (uint8_t)(fcb ? 0x7Au : MBUS_C_REQ_UD1);
    Mbus.build_short_args.a = a;
    mbus_build_short(work);
}

static void mbus_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = Mbus.parse_args.buf;
    size_t len = Mbus.parse_args.len;
    MbusFrame *out = Mbus.parse_args.out;
    size_t *consumed = Mbus.parse_args.consumed;

    if (!buf || !out || len < 1)
    {
        Mbus.ok = PROTO_FALSE;
        return;
    }
    out->type = MBUS_FRAME_NONE;
    out->c = out->a = out->ci = 0;
    out->data = NULL;
    out->data_len = 0;

    if (buf[0] == MBUS_ACK)
    {
        out->type = MBUS_FRAME_ACK;
        if (consumed)
        {
            *consumed = 1;
        }
        Mbus.ok = PROTO_TRUE;
        return;
    }
    if (buf[0] == MBUS_START_SHORT)
    {
        if (len < 5 || buf[4] != MBUS_STOP)
        {
            Mbus.ok = PROTO_FALSE;
            return;
        }
        if (buf[3] != (uint8_t)(buf[1] + buf[2]))
        {
            Mbus.ok = PROTO_FALSE;
            return;
        }
        out->type = MBUS_FRAME_SHORT;
        out->c = buf[1];
        out->a = buf[2];
        if (consumed)
        {
            *consumed = 5;
        }
        Mbus.ok = PROTO_TRUE;
        return;
    }
    if (buf[0] == MBUS_START_LONG)
    {
        if (len < 4)
        {
            Mbus.ok = PROTO_FALSE;
            return;
        }
        uint8_t L = buf[1];
        if (L < 3 || buf[2] != L || buf[3] != MBUS_START_LONG)
        {
            Mbus.ok = PROTO_FALSE;
            return;
        }
        size_t total = (size_t)6 + L;
        if (len < total || buf[5 + L] != MBUS_STOP)
        {
            Mbus.ok = PROTO_FALSE;
            return;
        }
        if (checksum(buf + 4, L) != buf[4 + L])
        {
            Mbus.ok = PROTO_FALSE;
            return;
        }
        out->type = MBUS_FRAME_LONG;
        out->c = buf[4];
        out->a = buf[5];
        out->ci = buf[6];
        out->data_len = (uint8_t)(L - 3);
        out->data = out->data_len ? buf + 7 : NULL;
        if (consumed)
        {
            *consumed = total;
        }
        Mbus.ok = PROTO_TRUE;
        return;
    }
    Mbus.ok = PROTO_FALSE;
}

static void mbus_dif_data_len(uint8_t *restrict work)
{
    (void)work;
    uint8_t coding = Mbus.dif_data_len_args.coding;

    switch ((MbusDifCoding)(coding & 0x0Fu))
    {
    case MBUS_DIF_NONE:
        Mbus.value = 0;
        return;
    case MBUS_DIF_INT8:
        Mbus.value = 1;
        return;
    case MBUS_DIF_INT16:
        Mbus.value = 2;
        return;
    case MBUS_DIF_INT24:
        Mbus.value = 3;
        return;
    case MBUS_DIF_INT32:
        Mbus.value = 4;
        return;
    case MBUS_DIF_REAL32:
        Mbus.value = 4;
        return;
    case MBUS_DIF_INT48:
        Mbus.value = 6;
        return;
    case MBUS_DIF_INT64:
        Mbus.value = 8;
        return;
    case MBUS_DIF_READOUT:
        Mbus.value = 0;
        return;
    case MBUS_DIF_BCD2:
        Mbus.value = 1;
        return;
    case MBUS_DIF_BCD4:
        Mbus.value = 2;
        return;
    case MBUS_DIF_BCD6:
        Mbus.value = 3;
        return;
    case MBUS_DIF_BCD8:
        Mbus.value = 4;
        return;
    case MBUS_DIF_VARIABLE:
        Mbus.value = 0; // length carried in the LVAR octet
        return;
    case MBUS_DIF_BCD12:
        Mbus.value = 6;
        return;
    default: // MBUS_DIF_SPECIAL
        Mbus.value = 0;
        return;
    }
}

static void mbus_record_next(uint8_t *restrict work)
{
    const uint8_t *body = Mbus.record_next_args.body;
    size_t len = Mbus.record_next_args.len;
    size_t *pos = Mbus.record_next_args.pos;
    MbusRecord *out = Mbus.record_next_args.out;

    if (!body || !pos || !out || *pos >= len)
    {
        Mbus.ok = PROTO_FALSE;
        return;
    }
    size_t p = *pos;
    uint8_t dif = body[p++];
    uint8_t coding = (uint8_t)(dif & 0x0Fu);

    // Skip the DIFE extension chain (each DIFE's high bit flags another).
    uint8_t ext = dif;
    while (ext & 0x80u)
    {
        if (p >= len)
        {
            Mbus.ok = PROTO_FALSE;
            return;
        }
        ext = body[p++];
    }

    out->dif = dif;
    out->coding = coding;
    out->vif = 0;
    out->data = NULL;
    out->data_len = 0;

    if (coding == (uint8_t)MBUS_DIF_SPECIAL) // manufacturer-specific / idle: no VIF, no fixed data
    {
        *pos = p;
        Mbus.ok = PROTO_TRUE;
        return;
    }

    // VIF (mandatory) + its VIFE extension chain.
    if (p >= len)
    {
        Mbus.ok = PROTO_FALSE;
        return;
    }
    out->vif = body[p++];
    ext = out->vif;
    while (ext & 0x80u)
    {
        if (p >= len)
        {
            Mbus.ok = PROTO_FALSE;
            return;
        }
        ext = body[p++];
    }

    uint8_t dlen;
    if (coding == (uint8_t)MBUS_DIF_VARIABLE)
    {
        if (p >= len)
        {
            Mbus.ok = PROTO_FALSE;
            return;
        }
        uint8_t lvar = body[p++];
        if (lvar > 0xBFu)
        {
            Mbus.ok = PROTO_FALSE; // only the LVAR raw/ASCII form (0x00..0xBF) is supported
            return;
        }
        dlen = lvar;
    }
    else
    {
        Mbus.dif_data_len_args.coding = coding;
        mbus_dif_data_len(work);
        dlen = Mbus.value;
    }

    if (p + dlen > len)
    {
        Mbus.ok = PROTO_FALSE;
        return;
    }
    out->data = dlen ? body + p : NULL;
    out->data_len = dlen;
    *pos = p + dlen;
    Mbus.ok = PROTO_TRUE;
}

// --- record value + unit decoding ---

// Decode a little-endian signed integer of @p n (1..8) octets (M-Bus INT8..INT64), sign-extended.
static proto_bool mbus_decode_int(const uint8_t *d, uint8_t n, int64_t *out)
{
    if (n == 0 || n > 8)
    {
        return PROTO_FALSE;
    }
    uint64_t u = 0;
    for (int i = (int)n - 1; i >= 0; i--)
    {
        u = (u << 8) | d[i]; // little-endian
    }
    if (n < 8 && (d[n - 1] & 0x80u))
    {
        u |= ~(uint64_t)0 << (n * 8); // sign-extend the negative
    }
    *out = (int64_t)u;
    return PROTO_TRUE;
}

// Decode a packed-BCD value of @p n (1..6) octets (M-Bus BCD2..BCD12); a 0xF top nibble marks negative.
static proto_bool mbus_decode_bcd(const uint8_t *d, uint8_t n, int64_t *out)
{
    if (n == 0 || n > 6)
    {
        return PROTO_FALSE;
    }
    proto_bool neg = (uint8_t)(d[n - 1] >> 4) == 0x0Fu; // a 0xF top nibble marks a negative value
    int64_t v = 0;
    int64_t mult = 1;
    for (uint8_t i = 0; i < n; i++)
    {
        uint8_t lo = (uint8_t)(d[i] & 0x0Fu);
        uint8_t hi = (uint8_t)(d[i] >> 4);
        if (i == (uint8_t)(n - 1) && neg)
        {
            hi = 0;
        }
        if (lo > 9 || hi > 9) // an invalid BCD nibble
        {
            return PROTO_FALSE;
        }
        v += (int64_t)(hi * 10 + lo) * mult;
        mult *= 100;
    }
    *out = neg ? -v : v;
    return PROTO_TRUE;
}

static void mbus_record_value_int(uint8_t *restrict work)
{
    (void)work;
    const MbusRecord *r = Mbus.record_value_int_args.r;
    int64_t *out = Mbus.record_value_int_args.out;

    if (!r || !out || !r->data)
    {
        Mbus.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *d = r->data;
    uint8_t n = r->data_len;
    switch ((MbusDifCoding)r->coding)
    {
    case MBUS_DIF_INT8:
    case MBUS_DIF_INT16:
    case MBUS_DIF_INT24:
    case MBUS_DIF_INT32:
    case MBUS_DIF_INT48:
    case MBUS_DIF_INT64:
        Mbus.ok = mbus_decode_int(d, n, out);
        return;
    case MBUS_DIF_BCD2:
    case MBUS_DIF_BCD4:
    case MBUS_DIF_BCD6:
    case MBUS_DIF_BCD8:
    case MBUS_DIF_BCD12:
        Mbus.ok = mbus_decode_bcd(d, n, out);
        return;
    default:
        Mbus.ok = PROTO_FALSE; // real / variable / no-data codings are not integers
        return;
    }
}

static void mbus_record_value_real(uint8_t *restrict work)
{
    (void)work;
    const MbusRecord *r = Mbus.record_value_real_args.r;
    float *out = Mbus.record_value_real_args.out;

    if (!r || !out || !r->data || (MbusDifCoding)r->coding != MBUS_DIF_REAL32 || r->data_len < 4)
    {
        Mbus.ok = PROTO_FALSE;
        return;
    }
    uint32_t bits = (uint32_t)r->data[0] | ((uint32_t)r->data[1] << 8) | ((uint32_t)r->data[2] << 16) |
                    ((uint32_t)r->data[3] << 24);
    mem.cpy(out, &bits, 4);
    Mbus.ok = PROTO_TRUE;
}

static void mbus_vif_decode(uint8_t *restrict work)
{
    (void)work;
    uint8_t vif = Mbus.vif_decode_args.vif;
    MbusUnit *unit = Mbus.vif_decode_args.unit;
    int8_t *exp10 = Mbus.vif_decode_args.exp10;

    MbusUnit u = MBUS_UNIT_UNKNOWN;
    int8_t e = 0;
    uint8_t v = (uint8_t)(vif & 0x7Fu); // ignore the VIF extension bit
    if (v <= 0x07)
    {
        u = MBUS_UNIT_WH;
        e = (int8_t)((v & 7) - 3); // energy 10^(nnn-3) Wh
    }
    else if (v <= 0x0F)
    {
        u = MBUS_UNIT_J;
        e = (int8_t)(v & 7); // energy 10^(nnn) J
    }
    else if (v <= 0x17)
    {
        u = MBUS_UNIT_M3;
        e = (int8_t)((v & 7) - 6); // volume 10^(nnn-6) m3
    }
    else if (v <= 0x1F)
    {
        u = MBUS_UNIT_KG;
        e = (int8_t)((v & 7) - 3); // mass 10^(nnn-3) kg
    }
    else if (v >= 0x28 && v <= 0x2F)
    {
        u = MBUS_UNIT_W;
        e = (int8_t)((v & 7) - 3); // power 10^(nnn-3) W
    }
    else if (v >= 0x30 && v <= 0x37)
    {
        u = MBUS_UNIT_J_PER_H;
        e = (int8_t)(v & 7); // power 10^(nnn) J/h
    }
    else if (v >= 0x38 && v <= 0x3F)
    {
        u = MBUS_UNIT_M3_PER_H;
        e = (int8_t)((v & 7) - 6); // volume flow 10^(nnn-6) m3/h
    }
    else if ((v >= 0x58 && v <= 0x5F) || (v >= 0x64 && v <= 0x67))
    {
        // Flow/return (0x58..0x5F) and external (0x64..0x67) temperatures are both degrees Celsius at
        // the same 10^(nn-3) scale, so they share one branch (the VIF only distinguishes them semantically).
        u = MBUS_UNIT_CELSIUS;
        e = (int8_t)((v & 3) - 3);
    }
    else if (v >= 0x60 && v <= 0x63)
    {
        u = MBUS_UNIT_K;
        e = (int8_t)((v & 3) - 3); // temperature difference 10^(nn-3) K
    }
    else if (v >= 0x68 && v <= 0x6B)
    {
        u = MBUS_UNIT_BAR;
        e = (int8_t)((v & 3) - 3); // pressure 10^(nn-3) bar
    }
    if (unit)
    {
        *unit = u;
    }
    if (exp10)
    {
        *exp10 = e;
    }
    Mbus.ok = u != MBUS_UNIT_UNKNOWN;
}

static void mbus_parse_var_header(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *body = Mbus.parse_var_header_args.body;
    size_t len = Mbus.parse_var_header_args.len;
    MbusVarHeader *out = Mbus.parse_var_header_args.out;

    if (!body || !out || len < MBUS_VAR_HEADER_LEN)
    {
        Mbus.ok = PROTO_FALSE;
        return;
    }
    // Identification number: 4 octets of BCD, least-significant octet first.
    uint32_t id = 0;
    for (int i = 3; i >= 0; i--)
    {
        uint8_t hi = (uint8_t)(body[i] >> 4);
        uint8_t lo = (uint8_t)(body[i] & 0x0Fu);
        if (hi > 9 || lo > 9) // the identification number must be valid BCD
        {
            Mbus.ok = PROTO_FALSE;
            return;
        }
        id = id * 100u + (uint32_t)(hi * 10u + lo);
    }
    out->id = id;
    // Manufacturer: a 15-bit value packing three letters, each (n + 64) => 'A'..'Z' (EN 13757-3 §6.3.1).
    uint16_t man = (uint16_t)(body[4] | (body[5] << 8));
    out->manufacturer_raw = man;
    out->manufacturer[0] = (char)(((man >> 10) & 0x1Fu) + 64u);
    out->manufacturer[1] = (char)(((man >> 5) & 0x1Fu) + 64u);
    out->manufacturer[2] = (char)((man & 0x1Fu) + 64u);
    out->manufacturer[3] = '\0';
    out->version = body[6];
    out->medium = body[7];
    out->access_no = body[8];
    out->status = body[9];
    out->signature = (uint16_t)(body[10] | (body[11] << 8));
    Mbus.ok = PROTO_TRUE;
}

MbusNs Mbus = {.build_ack = mbus_build_ack,
               .build_short = mbus_build_short,
               .build_long = mbus_build_long,
               .build_snd_nke = mbus_build_snd_nke,
               .build_req_ud2 = mbus_build_req_ud2,
               .build_req_ud1 = mbus_build_req_ud1,
               .parse = mbus_parse,
               .dif_data_len = mbus_dif_data_len,
               .record_next = mbus_record_next,
               .record_value_int = mbus_record_value_int,
               .record_value_real = mbus_record_value_real,
               .vif_decode = mbus_vif_decode,
               .parse_var_header = mbus_parse_var_header};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MBUS
