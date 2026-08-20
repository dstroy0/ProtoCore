// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file profibus.c
 * @brief PROFIBUS-DP FDL telegram codec (see profibus.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_PROFIBUS

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/profibus/profibus.h"

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

// The entries this file calls before reaching their definitions.
void protocore_profibus_fcs(uint8_t *restrict work);

void protocore_profibus_fcs(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *bytes = ProfibusV.fcs_args.bytes;
    size_t len = ProfibusV.fcs_args.len;

    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
    {
        sum = (uint8_t)(sum + bytes[i]);
    }
    ProfibusV.value = sum;
}

void protocore_profibus_build_sd1(uint8_t *restrict work)
{
    uint8_t da = ProfibusV.build_sd1_args.da;
    uint8_t sa = ProfibusV.build_sd1_args.sa;
    uint8_t fc = ProfibusV.build_sd1_args.fc;
    uint8_t *out = ProfibusV.build_sd1_args.out;
    size_t cap = ProfibusV.build_sd1_args.cap;

    if (!out || cap < 6)
    {
        ProfibusV.n = 0;
        return;
    }
    out[0] = PB_SD1;
    out[1] = da;
    out[2] = sa;
    out[3] = fc;
    uint8_t body[3] = {da, sa, fc};
    ProfibusV.fcs_args.bytes = body;
    ProfibusV.fcs_args.len = 3;
    protocore_profibus_fcs(work);
    out[4] = ProfibusV.value;
    out[5] = PB_ED;
    ProfibusV.n = 6;
}

void protocore_profibus_build_sd2(uint8_t *restrict work)
{
    uint8_t da = ProfibusV.build_sd2_args.da;
    uint8_t sa = ProfibusV.build_sd2_args.sa;
    uint8_t fc = ProfibusV.build_sd2_args.fc;
    const uint8_t *data = ProfibusV.build_sd2_args.data;
    size_t data_len = ProfibusV.build_sd2_args.data_len;
    uint8_t *out = ProfibusV.build_sd2_args.out;
    size_t cap = ProfibusV.build_sd2_args.cap;

    // LE counts DA + SA + FC + the PDU and ranges 4 to 249, so the PDU is 1 to 246 octets. A
    // zero-octet data unit would make LE 3; SD1 is the format for a telegram with no data field.
    if (!out || (data_len && !data) || data_len == 0 || data_len > 246)
    {
        ProfibusV.n = 0;
        return;
    }
    // SD2 LE LEr SD2 DA SA FC [data] FCS ED
    size_t n = 4 + 3 + data_len + 2; // (SD2,LE,LEr,SD2) + (DA,SA,FC) + data + (FCS,ED)
    if (n > cap)
    {
        ProfibusV.n = 0;
        return;
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
        mem.cpy(out + i, data, data_len);
        i += data_len;
    }
    // FCS over DA+SA+FC+data (out[4 .. 4+le-1]).
    ProfibusV.fcs_args.bytes = out + 4;
    ProfibusV.fcs_args.len = le;
    protocore_profibus_fcs(work);
    out[i++] = ProfibusV.value;
    out[i++] = PB_ED;
    ProfibusV.n = i;
}

void protocore_profibus_build_sd3(uint8_t *restrict work)
{
    uint8_t da = ProfibusV.build_sd3_args.da;
    uint8_t sa = ProfibusV.build_sd3_args.sa;
    uint8_t fc = ProfibusV.build_sd3_args.fc;
    const uint8_t *data = ProfibusV.build_sd3_args.data;
    uint8_t *out = ProfibusV.build_sd3_args.out;
    size_t cap = ProfibusV.build_sd3_args.cap;

    if (!out || !data || cap < 14) // SD3 DA SA FC data[8] FCS ED
    {
        ProfibusV.n = 0;
        return;
    }
    out[0] = PB_SD3;
    out[1] = da;
    out[2] = sa;
    out[3] = fc;
    mem.cpy(out + 4, data, 8);
    ProfibusV.fcs_args.bytes = out + 1;
    ProfibusV.fcs_args.len = 11;
    protocore_profibus_fcs(work);
    out[12] = ProfibusV.value; // FCS over DA+SA+FC+data(8)
    out[13] = PB_ED;
    ProfibusV.n = 14;
}

// SD3 fixed-length telegram: SD3 DA SA FC data[8] FCS ED (14 octets).
static void pb_parse_sd3(uint8_t *restrict work)
{
    const uint8_t *frame = ProfibusV.parse_args.frame;
    size_t len = ProfibusV.parse_args.len;
    PbTelegram *out = ProfibusV.parse_args.out;

    if (len < 14)
    {
        ProfibusV.ok = PROTO_FALSE;
        return;
    }
    ProfibusV.fcs_args.bytes = frame + 1;
    ProfibusV.fcs_args.len = 11;
    protocore_profibus_fcs(work);
    if (ProfibusV.value != frame[12] || frame[13] != PB_ED)
    {
        ProfibusV.ok = PROTO_FALSE;
        return;
    }
    out->sd = PB_SD3;
    out->da = frame[1];
    out->sa = frame[2];
    out->fc = frame[3];
    out->data = frame + 4;
    out->data_len = 8;
    ProfibusV.ok = PROTO_TRUE;
}

// SD1 no-data telegram: SD1 DA SA FC FCS ED (6 octets).
static void pb_parse_sd1(uint8_t *restrict work)
{
    const uint8_t *frame = ProfibusV.parse_args.frame;
    PbTelegram *out = ProfibusV.parse_args.out;

    uint8_t body[3] = {frame[1], frame[2], frame[3]}; // len >= 6 already guaranteed by Profibus.parse
    ProfibusV.fcs_args.bytes = body;
    ProfibusV.fcs_args.len = 3;
    protocore_profibus_fcs(work);
    if (ProfibusV.value != frame[4] || frame[5] != PB_ED)
    {
        ProfibusV.ok = PROTO_FALSE;
        return;
    }
    out->sd = PB_SD1;
    out->da = frame[1];
    out->sa = frame[2];
    out->fc = frame[3];
    out->data = NULL;
    out->data_len = 0;
    ProfibusV.ok = PROTO_TRUE;
}

// SD2 variable-length telegram: SD2 LE LEr SD2 DA SA FC [data] FCS ED.
static void pb_parse_sd2(uint8_t *restrict work)
{
    const uint8_t *frame = ProfibusV.parse_args.frame;
    size_t len = ProfibusV.parse_args.len;
    PbTelegram *out = ProfibusV.parse_args.out;

    if (len < 9)
    {
        ProfibusV.ok = PROTO_FALSE;
        return;
    }
    uint8_t le = frame[1];
    if (frame[2] != le || frame[3] != PB_SD2)
    {
        ProfibusV.ok = PROTO_FALSE;
        return;
    }
    if (le < 4) // LE counts DA + SA + FC + the PDU, and the PDU is at least one octet
    {
        ProfibusV.ok = PROTO_FALSE;
        return;
    }
    size_t total = 4 + le + 2; // header(4) + le body + FCS + ED
    if (len < total)
    {
        ProfibusV.ok = PROTO_FALSE;
        return;
    }
    ProfibusV.fcs_args.bytes = frame + 4;
    ProfibusV.fcs_args.len = le;
    protocore_profibus_fcs(work);
    if (ProfibusV.value != frame[4 + le] || frame[4 + le + 1] != PB_ED)
    {
        ProfibusV.ok = PROTO_FALSE;
        return;
    }
    out->sd = PB_SD2;
    out->da = frame[4];
    out->sa = frame[5];
    out->fc = frame[6];
    size_t dl = le - 3;
    out->data = dl ? (frame + 7) : NULL;
    out->data_len = dl;
    ProfibusV.ok = PROTO_TRUE;
}

void protocore_profibus_parse(uint8_t *restrict work)
{
    const uint8_t *frame = ProfibusV.parse_args.frame;
    size_t len = ProfibusV.parse_args.len;
    const PbTelegram *out = ProfibusV.parse_args.out;

    if (!frame || !out || len < 6)
    {
        ProfibusV.ok = PROTO_FALSE;
        return;
    }
    if (frame[0] == PB_SD3)
    {
        pb_parse_sd3(work);
        return;
    }
    if (frame[0] == PB_SD1)
    {
        pb_parse_sd1(work);
        return;
    }
    if (frame[0] == PB_SD2)
    {
        pb_parse_sd2(work);
        return;
    }
    ProfibusV.ok = PROTO_FALSE;
}

/** @brief The operands and the outcome. */
ProfibusVars ProfibusV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_PROFIBUS
