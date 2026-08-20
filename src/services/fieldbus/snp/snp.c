// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snp.c
 * @brief GE Fanuc SNP serial frame codec (see snp.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SNP

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/snp/snp.h"

PROTOCORE_BEGIN_DECLS

// GFK-0582D p. 7-62: seed zero, then per byte XOR into the accumulator and rotate it left one bit
// with the top bit wrapping into the bottom.
// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void snp_bcc(uint8_t *restrict work);

static void snp_bcc(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *bytes = Snp.bcc_args.bytes;
    size_t len = Snp.bcc_args.len;

    uint8_t bcc = 0;
    for (size_t i = 0; i < len; i++)
    {
        bcc = (uint8_t)(bcc ^ bytes[i]);
        bcc = (uint8_t)((bcc << 1) | (bcc >> 7));
    }
    Snp.value = bcc;
}

static void snp_build(uint8_t *restrict work)
{
    uint8_t control = Snp.build_args.control;
    const uint8_t *data = Snp.build_args.data;
    size_t data_len = Snp.build_args.data_len;
    uint8_t *out = Snp.build_args.out;
    size_t cap = Snp.build_args.cap;

    if (!out || (data_len && !data) || data_len > 255)
    {
        Snp.n = 0;
        return;
    }
    size_t n = 2 + data_len + 1; // control + length + data + BCC
    if (n > cap)
    {
        Snp.n = 0;
        return;
    }
    out[0] = control;
    out[1] = (uint8_t)data_len;
    if (data_len)
    {
        mem.cpy(out + 2, data, data_len);
    }
    Snp.bcc_args.bytes = out;
    Snp.bcc_args.len = 2 + data_len;
    snp_bcc(work);
    out[2 + data_len] = Snp.value; // BCC over control..last data
    Snp.n = n;
}

static void snp_parse(uint8_t *restrict work)
{
    const uint8_t *frame = Snp.parse_args.frame;
    size_t len = Snp.parse_args.len;
    SnpFrame *out = Snp.parse_args.out;

    if (!frame || !out || len < 3) // control + length + BCC
    {
        Snp.ok = PROTO_FALSE;
        return;
    }
    uint8_t data_len = frame[1];
    size_t expect = 2 + (size_t)data_len + 1;
    if (len < expect)
    {
        Snp.ok = PROTO_FALSE;
        return;
    }
    Snp.bcc_args.bytes = frame;
    Snp.bcc_args.len = 2 + data_len;
    snp_bcc(work);
    if (Snp.value != frame[2 + data_len])
    {
        Snp.ok = PROTO_FALSE;
        return;
    }
    out->control = frame[0];
    out->data = data_len ? (frame + 2) : NULL;
    out->data_len = data_len;
    Snp.ok = PROTO_TRUE;
}

SnpNs Snp = {.bcc = snp_bcc, .build = snp_build, .parse = snp_parse};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SNP
