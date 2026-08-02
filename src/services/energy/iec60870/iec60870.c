// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iec60870.c
 * @brief IEC 60870-5-101 / -104 telecontrol codec (pure, host-tested).
 */

#include "services/energy/iec60870/iec60870.h"

#if PC_ENABLE_IEC60870

#include <string.h>

// --- -104 APCI ---

size_t pc_iec104_build_i(uint8_t *buf, size_t cap, uint16_t ns, uint16_t nr, const uint8_t *asdu, size_t asdu_len)
{
    if (!buf || (asdu_len && !asdu) || asdu_len > 249) // APDU length octet maxes at 253 (= 4 + 249)
    {
        return 0;
    }
    size_t total = IEC104_APCI_LEN + asdu_len;
    if (cap < total)
    {
        return 0;
    }
    buf[0] = IEC_START_104;
    buf[1] = (uint8_t)(4 + asdu_len);      // APDU length: 4 control octets + ASDU
    buf[2] = (uint8_t)((ns << 1) & 0xFEu); // I-format: bit0 of octet 1 is 0
    buf[3] = (uint8_t)((ns >> 7) & 0xFFu);
    buf[4] = (uint8_t)((nr << 1) & 0xFEu);
    buf[5] = (uint8_t)((nr >> 7) & 0xFFu);
    if (asdu_len)
    {
        memcpy(buf + 6, asdu, asdu_len);
    }
    return total;
}

size_t pc_iec104_build_s(uint8_t *buf, size_t cap, uint16_t nr)
{
    if (!buf || cap < IEC104_APCI_LEN)
    {
        return 0;
    }
    buf[0] = IEC_START_104;
    buf[1] = 4;
    buf[2] = 0x01; // S-format
    buf[3] = 0x00;
    buf[4] = (uint8_t)((nr << 1) & 0xFEu);
    buf[5] = (uint8_t)((nr >> 7) & 0xFFu);
    return IEC104_APCI_LEN;
}

size_t pc_iec104_build_u(uint8_t *buf, size_t cap, uint8_t u_cmd)
{
    if (!buf || cap < IEC104_APCI_LEN)
    {
        return 0;
    }
    buf[0] = IEC_START_104;
    buf[1] = 4;
    buf[2] = u_cmd; // U-format: bits 0-1 are 11 in every defined command
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = 0x00;
    return IEC104_APCI_LEN;
}

proto_bool pc_iec104_parse(const uint8_t *buf, size_t len, Iec104Apci *out, size_t *consumed)
{
    if (!buf || !out || len < 2 || buf[0] != IEC_START_104)
    {
        return PROTO_FALSE;
    }
    uint8_t L = buf[1];
    if (L < 4)
    {
        return PROTO_FALSE;
    }
    size_t total = (size_t)2 + L;
    if (len < total)
    {
        return PROTO_FALSE;
    }
    uint8_t c0 = buf[2];
    out->ns = out->nr = 0;
    out->u_cmd = 0;
    out->asdu = NULL;
    out->asdu_len = 0;
    if ((c0 & 0x01u) == 0) // I-format
    {
        out->format = IEC104_I;
        out->ns = (uint16_t)((buf[2] >> 1) | ((uint16_t)buf[3] << 7));
        out->nr = (uint16_t)((buf[4] >> 1) | ((uint16_t)buf[5] << 7));
        out->asdu_len = (size_t)(L - 4);
        out->asdu = out->asdu_len ? buf + 6 : NULL;
    }
    else if ((c0 & 0x03u) == 0x01u) // S-format
    {
        out->format = IEC104_S;
        out->nr = (uint16_t)((buf[4] >> 1) | ((uint16_t)buf[5] << 7));
    }
    else // U-format (bits 0-1 == 11)
    {
        out->format = IEC104_U;
        out->u_cmd = c0;
    }
    if (consumed)
    {
        *consumed = total;
    }
    return PROTO_TRUE;
}

// --- ASDU header + IOA ---

size_t pc_iec_asdu_build_header(uint8_t *buf, size_t cap, const IecAsduHeader *h)
{
    if (!buf || !h || cap < 6)
    {
        return 0;
    }
    buf[0] = h->type_id;
    buf[1] = (uint8_t)((h->sq ? 0x80u : 0u) | (h->count & 0x7Fu)); // variable structure qualifier
    buf[2] = (uint8_t)((h->test ? 0x80u : 0u) | (h->negative ? 0x40u : 0u) | (h->cot & 0x3Fu));
    buf[3] = h->orig_addr;
    buf[4] = (uint8_t)(h->common_addr & 0xFFu); // common address, little-endian
    buf[5] = (uint8_t)((h->common_addr >> 8) & 0xFFu);
    return 6;
}

proto_bool pc_iec_asdu_parse_header(const uint8_t *buf, size_t len, IecAsduHeader *out, size_t *consumed)
{
    if (!buf || !out || len < 6)
    {
        return PROTO_FALSE;
    }
    out->type_id = buf[0];
    out->sq = (buf[1] & 0x80u) != 0;
    out->count = (uint8_t)(buf[1] & 0x7Fu);
    out->test = (buf[2] & 0x80u) != 0;
    out->negative = (buf[2] & 0x40u) != 0;
    out->cot = (uint8_t)(buf[2] & 0x3Fu);
    out->orig_addr = buf[3];
    out->common_addr = (uint16_t)(buf[4] | ((uint16_t)buf[5] << 8));
    if (consumed)
    {
        *consumed = 6;
    }
    return PROTO_TRUE;
}

size_t pc_iec_put_ioa(uint8_t *buf, size_t cap, uint32_t ioa)
{
    if (!buf || cap < 3)
    {
        return 0;
    }
    buf[0] = (uint8_t)(ioa & 0xFFu);
    buf[1] = (uint8_t)((ioa >> 8) & 0xFFu);
    buf[2] = (uint8_t)((ioa >> 16) & 0xFFu);
    return 3;
}

uint32_t pc_iec_get_ioa(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

// --- typed information objects ---

size_t pc_iec_io_build_sp(uint8_t *buf, size_t cap, uint32_t ioa, proto_bool on, uint8_t quality)
{
    if (!buf || cap < 4)
    {
        return 0;
    }
    pc_iec_put_ioa(buf, cap, ioa);
    buf[3] = (uint8_t)((on ? 0x01u : 0u) | (quality & 0xF0u)); // SIQ: SPI (bit 0) + quality (bits 4..7)
    return 4;
}

proto_bool pc_iec_io_parse_sp(const uint8_t *buf, size_t len, uint32_t *ioa, proto_bool *on, uint8_t *quality)
{
    if (!buf || len < 4)
    {
        return PROTO_FALSE;
    }
    if (ioa)
    {
        *ioa = pc_iec_get_ioa(buf);
    }
    if (on)
    {
        *on = (buf[3] & 0x01u) != 0;
    }
    if (quality)
    {
        *quality = (uint8_t)(buf[3] & 0xF0u);
    }
    return PROTO_TRUE;
}

size_t pc_iec_io_build_float(uint8_t *buf, size_t cap, uint32_t ioa, float value, uint8_t qds)
{
    if (!buf || cap < 8)
    {
        return 0;
    }
    pc_iec_put_ioa(buf, cap, ioa);
    uint32_t bits;
    memcpy(&bits, &value, 4); // the IEEE-754 bit pattern, written little-endian (endian-safe)
    buf[3] = (uint8_t)bits;
    buf[4] = (uint8_t)(bits >> 8);
    buf[5] = (uint8_t)(bits >> 16);
    buf[6] = (uint8_t)(bits >> 24);
    buf[7] = qds;
    return 8;
}

proto_bool pc_iec_io_parse_float(const uint8_t *buf, size_t len, uint32_t *ioa, float *value, uint8_t *qds)
{
    if (!buf || len < 8)
    {
        return PROTO_FALSE;
    }
    if (ioa)
    {
        *ioa = pc_iec_get_ioa(buf);
    }
    if (value)
    {
        uint32_t bits =
            (uint32_t)buf[3] | ((uint32_t)buf[4] << 8) | ((uint32_t)buf[5] << 16) | ((uint32_t)buf[6] << 24);
        memcpy(value, &bits, 4);
    }
    if (qds)
    {
        *qds = buf[7];
    }
    return PROTO_TRUE;
}

size_t pc_iec_io_build_scaled(uint8_t *buf, size_t cap, uint32_t ioa, int16_t value, uint8_t qds)
{
    if (!buf || cap < 6)
    {
        return 0;
    }
    pc_iec_put_ioa(buf, cap, ioa);
    uint16_t u = (uint16_t)value; // two's-complement wire form, little-endian
    buf[3] = (uint8_t)u;
    buf[4] = (uint8_t)(u >> 8);
    buf[5] = qds;
    return 6;
}

proto_bool pc_iec_io_parse_scaled(const uint8_t *buf, size_t len, uint32_t *ioa, int16_t *value, uint8_t *qds)
{
    if (!buf || len < 6)
    {
        return PROTO_FALSE;
    }
    if (ioa)
    {
        *ioa = pc_iec_get_ioa(buf);
    }
    if (value)
    {
        *value = (int16_t)((uint16_t)buf[3] | ((uint16_t)buf[4] << 8));
    }
    if (qds)
    {
        *qds = buf[5];
    }
    return PROTO_TRUE;
}

size_t pc_iec_io_build_normalized(uint8_t *buf, size_t cap, uint32_t ioa, float value, uint8_t qds)
{
    if (!buf || cap < 6)
    {
        return 0;
    }
    pc_iec_put_ioa(buf, cap, ioa);
    int32_t nva = (int32_t)(value * 32768.0f); // normalized fraction -> signed 16-bit, clamped to the field
    if (nva > 32767)
    {
        nva = 32767;
    }
    if (nva < -32768)
    {
        nva = -32768;
    }
    uint16_t u = (uint16_t)(int16_t)nva;
    buf[3] = (uint8_t)u;
    buf[4] = (uint8_t)(u >> 8);
    buf[5] = qds;
    return 6;
}

proto_bool pc_iec_io_parse_normalized(const uint8_t *buf, size_t len, uint32_t *ioa, float *value, uint8_t *qds)
{
    if (!buf || len < 6)
    {
        return PROTO_FALSE;
    }
    if (ioa)
    {
        *ioa = pc_iec_get_ioa(buf);
    }
    if (value)
    {
        int16_t nva = (int16_t)((uint16_t)buf[3] | ((uint16_t)buf[4] << 8));
        *value = (float)nva / 32768.0f;
    }
    if (qds)
    {
        *qds = buf[5];
    }
    return PROTO_TRUE;
}

size_t pc_iec_io_build_counter(uint8_t *buf, size_t cap, uint32_t ioa, int32_t value, uint8_t seq)
{
    if (!buf || cap < 8)
    {
        return 0;
    }
    pc_iec_put_ioa(buf, cap, ioa);
    uint32_t u = (uint32_t)value; // two's-complement wire form, little-endian
    buf[3] = (uint8_t)u;
    buf[4] = (uint8_t)(u >> 8);
    buf[5] = (uint8_t)(u >> 16);
    buf[6] = (uint8_t)(u >> 24);
    buf[7] = seq; // sequence notation: SQ (bits 0..4) + CY / CA / IV
    return 8;
}

proto_bool pc_iec_io_parse_counter(const uint8_t *buf, size_t len, uint32_t *ioa, int32_t *value, uint8_t *seq)
{
    if (!buf || len < 8)
    {
        return PROTO_FALSE;
    }
    if (ioa)
    {
        *ioa = pc_iec_get_ioa(buf);
    }
    if (value)
    {
        *value =
            (int32_t)((uint32_t)buf[3] | ((uint32_t)buf[4] << 8) | ((uint32_t)buf[5] << 16) | ((uint32_t)buf[6] << 24));
    }
    if (seq)
    {
        *seq = buf[7];
    }
    return PROTO_TRUE;
}

size_t pc_iec_io_build_sc(uint8_t *buf, size_t cap, uint32_t ioa, proto_bool on, proto_bool select)
{
    if (!buf || cap < 4)
    {
        return 0;
    }
    pc_iec_put_ioa(buf, cap, ioa);
    buf[3] = (uint8_t)((on ? IEC_SCO_ON : 0u) | (select ? IEC_SCO_SE : 0u)); // SCO: SCS (bit 0) + S/E (bit 7)
    return 4;
}

proto_bool pc_iec_io_parse_sc(const uint8_t *buf, size_t len, uint32_t *ioa, proto_bool *on, proto_bool *select)
{
    if (!buf || len < 4)
    {
        return PROTO_FALSE;
    }
    if (ioa)
    {
        *ioa = pc_iec_get_ioa(buf);
    }
    if (on)
    {
        *on = (buf[3] & IEC_SCO_ON) != 0;
    }
    if (select)
    {
        *select = (buf[3] & IEC_SCO_SE) != 0;
    }
    return PROTO_TRUE;
}

size_t pc_iec_io_build_dp(uint8_t *buf, size_t cap, uint32_t ioa, uint8_t dpi, uint8_t quality)
{
    if (!buf || cap < 4)
    {
        return 0;
    }
    pc_iec_put_ioa(buf, cap, ioa);
    buf[3] = (uint8_t)((dpi & IEC_DP_MASK) | (quality & 0xF0u)); // DIQ: DPI (bits 0..1) + quality (bits 4..7)
    return 4;
}

proto_bool pc_iec_io_parse_dp(const uint8_t *buf, size_t len, uint32_t *ioa, uint8_t *dpi, uint8_t *quality)
{
    if (!buf || len < 4)
    {
        return PROTO_FALSE;
    }
    if (ioa)
    {
        *ioa = pc_iec_get_ioa(buf);
    }
    if (dpi)
    {
        *dpi = (uint8_t)(buf[3] & IEC_DP_MASK);
    }
    if (quality)
    {
        *quality = (uint8_t)(buf[3] & 0xF0u);
    }
    return PROTO_TRUE;
}

size_t pc_iec_io_build_dc(uint8_t *buf, size_t cap, uint32_t ioa, uint8_t dcs, uint8_t qu, proto_bool select)
{
    if (!buf || cap < 4)
    {
        return 0;
    }
    pc_iec_put_ioa(buf, cap, ioa);
    // DCO: DCS (bits 0..1) + QU (bits 2..6) + S/E (bit 7).
    buf[3] = (uint8_t)((dcs & IEC_DP_MASK) | ((qu & IEC_DCO_QU_MASK) << IEC_DCO_QU_SHIFT) | (select ? IEC_DCO_SE : 0u));
    return 4;
}

proto_bool pc_iec_io_parse_dc(const uint8_t *buf, size_t len, uint32_t *ioa, uint8_t *dcs, uint8_t *qu,
                              proto_bool *select)
{
    if (!buf || len < 4)
    {
        return PROTO_FALSE;
    }
    if (ioa)
    {
        *ioa = pc_iec_get_ioa(buf);
    }
    if (dcs)
    {
        *dcs = (uint8_t)(buf[3] & IEC_DP_MASK);
    }
    if (qu)
    {
        *qu = (uint8_t)((buf[3] >> IEC_DCO_QU_SHIFT) & IEC_DCO_QU_MASK);
    }
    if (select)
    {
        *select = (buf[3] & IEC_DCO_SE) != 0;
    }
    return PROTO_TRUE;
}

// --- -101 FT1.2 link frames ---

static uint8_t sum8(const uint8_t *p, size_t n)
{
    uint8_t s = 0;
    for (size_t i = 0; i < n; i++)
    {
        s = (uint8_t)(s + p[i]);
    }
    return s;
}

size_t pc_iec101_build_fixed(uint8_t *buf, size_t cap, uint8_t control, uint8_t addr)
{
    if (!buf || cap < 5)
    {
        return 0;
    }
    buf[0] = IEC_START_FIXED;
    buf[1] = control;
    buf[2] = addr;
    buf[3] = (uint8_t)(control + addr); // checksum over control + address
    buf[4] = IEC_STOP;
    return 5;
}

size_t pc_iec101_build_variable(uint8_t *buf, size_t cap, uint8_t control, uint8_t addr, const uint8_t *asdu,
                                uint8_t asdu_len)
{
    if (!buf || (asdu_len && !asdu) || asdu_len > 253)
    {
        return 0;
    }
    uint8_t L = (uint8_t)(2 + asdu_len); // L counts control + address + ASDU
    size_t total = (size_t)6 + L;
    if (cap < total)
    {
        return 0;
    }
    buf[0] = IEC_START_104; // 0x68
    buf[1] = L;
    buf[2] = L;
    buf[3] = IEC_START_104;
    buf[4] = control;
    buf[5] = addr;
    if (asdu_len)
    {
        memcpy(buf + 6, asdu, asdu_len);
    }
    buf[4 + L] = sum8(buf + 4, L); // checksum over control..end of ASDU
    buf[5 + L] = IEC_STOP;
    return total;
}

proto_bool pc_iec101_parse(const uint8_t *buf, size_t len, Iec101Frame *out, size_t *consumed)
{
    if (!buf || !out || len < 1)
    {
        return PROTO_FALSE;
    }
    out->fixed = PROTO_FALSE;
    out->control = out->addr = 0;
    out->asdu = NULL;
    out->asdu_len = 0;

    if (buf[0] == IEC_START_FIXED)
    {
        if (len < 5 || buf[4] != IEC_STOP || buf[3] != (uint8_t)(buf[1] + buf[2]))
        {
            return PROTO_FALSE;
        }
        out->fixed = PROTO_TRUE;
        out->control = buf[1];
        out->addr = buf[2];
        if (consumed)
        {
            *consumed = 5;
        }
        return PROTO_TRUE;
    }
    if (buf[0] == IEC_START_104)
    {
        if (len < 4)
        {
            return PROTO_FALSE;
        }
        uint8_t L = buf[1];
        if (L < 2 || buf[2] != L || buf[3] != IEC_START_104)
        {
            return PROTO_FALSE;
        }
        size_t total = (size_t)6 + L;
        if (len < total || buf[5 + L] != IEC_STOP || sum8(buf + 4, L) != buf[4 + L])
        {
            return PROTO_FALSE;
        }
        out->control = buf[4];
        out->addr = buf[5];
        out->asdu_len = (uint8_t)(L - 2);
        out->asdu = out->asdu_len ? buf + 6 : NULL;
        if (consumed)
        {
            *consumed = total;
        }
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

#endif // PC_ENABLE_IEC60870
