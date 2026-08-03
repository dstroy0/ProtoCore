// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file profibus.c
 * @brief PROFIBUS-DP FDL telegram codec (see profibus.h).
 */

#include "services/fieldbus/profibus/profibus.h"

#if PC_ENABLE_PROFIBUS

#include <string.h>

uint8_t pc_pb_fcs(const uint8_t *bytes, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
    {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum;
}

size_t pc_pb_build_sd1(uint8_t da, uint8_t sa, uint8_t fc, uint8_t *out, size_t cap)
{
    if (!out || cap < 6)
    {
        return 0;
    }
    out[0] = PB_SD1;
    out[1] = da;
    out[2] = sa;
    out[3] = fc;
    uint8_t body[3] = {da, sa, fc};
    out[4] = pc_pb_fcs(body, 3);
    out[5] = PB_ED;
    return 6;
}

size_t pc_pb_build_sd2(uint8_t da, uint8_t sa, uint8_t fc, const uint8_t *data, size_t data_len, uint8_t *out,
                       size_t cap)
{
    if (!out || (data_len && !data) || data_len > 246)
    {
        return 0;
    }
    // SD2 LE LEr SD2 DA SA FC [data] FCS ED
    size_t n = 4 + 3 + data_len + 2; // (SD2,LE,LEr,SD2) + (DA,SA,FC) + data + (FCS,ED)
    if (n > cap)
    {
        return 0;
    }
    uint8_t le = (uint8_t)(3 + data_len); // length of DA+SA+FC+data
    size_t i = 0;
    out[i++] = PB_SD2;
    out[i++] = le;
    out[i++] = le; // LEr (redundant length)
    out[i++] = PB_SD2;
    out[i++] = da;
    out[i++] = sa;
    out[i++] = fc;
    if (data_len)
    {
        memcpy(out + i, data, data_len);
        i += data_len;
    }
    // FCS over DA+SA+FC+data (out[4 .. 4+le-1]).
    out[i++] = pc_pb_fcs(out + 4, le);
    out[i++] = PB_ED;
    return i;
}

size_t pc_pb_build_sd3(uint8_t da, uint8_t sa, uint8_t fc, const uint8_t *data, uint8_t *out, size_t cap)
{
    if (!out || !data || cap < 14) // SD3 DA SA FC data[8] FCS ED
    {
        return 0;
    }
    out[0] = PB_SD3;
    out[1] = da;
    out[2] = sa;
    out[3] = fc;
    memcpy(out + 4, data, 8);
    out[12] = pc_pb_fcs(out + 1, 11); // FCS over DA+SA+FC+data(8)
    out[13] = PB_ED;
    return 14;
}

// SD3 fixed-length telegram: SD3 DA SA FC data[8] FCS ED (14 octets).
static proto_bool pb_parse_sd3(const uint8_t *frame, size_t len, PbTelegram *out)
{
    if (len < 14)
    {
        return PROTO_FALSE;
    }
    if (pc_pb_fcs(frame + 1, 11) != frame[12] || frame[13] != PB_ED)
    {
        return PROTO_FALSE;
    }
    out->sd = PB_SD3;
    out->da = frame[1];
    out->sa = frame[2];
    out->fc = frame[3];
    out->data = frame + 4;
    out->data_len = 8;
    return PROTO_TRUE;
}

// SD1 no-data telegram: SD1 DA SA FC FCS ED (6 octets).
static proto_bool pb_parse_sd1(const uint8_t *frame, size_t len, PbTelegram *out)
{
    (void)len; // len >= 6 already guaranteed by pc_pb_parse
    uint8_t body[3] = {frame[1], frame[2], frame[3]};
    if (pc_pb_fcs(body, 3) != frame[4] || frame[5] != PB_ED)
    {
        return PROTO_FALSE;
    }
    out->sd = PB_SD1;
    out->da = frame[1];
    out->sa = frame[2];
    out->fc = frame[3];
    out->data = NULL;
    out->data_len = 0;
    return PROTO_TRUE;
}

// SD2 variable-length telegram: SD2 LE LEr SD2 DA SA FC [data] FCS ED.
static proto_bool pb_parse_sd2(const uint8_t *frame, size_t len, PbTelegram *out)
{
    if (len < 9)
    {
        return PROTO_FALSE;
    }
    uint8_t le = frame[1];
    if (frame[2] != le || frame[3] != PB_SD2)
    {
        return PROTO_FALSE;
    }
    if (le < 3)
    {
        return PROTO_FALSE;
    }
    size_t total = 4 + le + 2; // header(4) + le body + FCS + ED
    if (len < total)
    {
        return PROTO_FALSE;
    }
    if (pc_pb_fcs(frame + 4, le) != frame[4 + le] || frame[4 + le + 1] != PB_ED)
    {
        return PROTO_FALSE;
    }
    out->sd = PB_SD2;
    out->da = frame[4];
    out->sa = frame[5];
    out->fc = frame[6];
    size_t dl = le - 3;
    out->data = dl ? (frame + 7) : NULL;
    out->data_len = dl;
    return PROTO_TRUE;
}

proto_bool pc_pb_parse(const uint8_t *frame, size_t len, PbTelegram *out)
{
    if (!frame || !out || len < 6)
    {
        return PROTO_FALSE;
    }
    if (frame[0] == PB_SD3)
    {
        return pb_parse_sd3(frame, len, out);
    }
    if (frame[0] == PB_SD1)
    {
        return pb_parse_sd1(frame, len, out);
    }
    if (frame[0] == PB_SD2)
    {
        return pb_parse_sd2(frame, len, out);
    }
    return PROTO_FALSE;
}

#endif // PC_ENABLE_PROFIBUS
