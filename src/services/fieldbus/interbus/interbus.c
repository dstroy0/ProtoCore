// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file interbus.c
 * @brief INTERBUS summation-frame codec (see interbus.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t crc_work[16]; // the borrow an entry takes; Crc never reads it

#if PROTOCORE_ENABLE_INTERBUS

#include "services/fieldbus/interbus/interbus.h"

#include "shared/crc/crc.h" // PROTOCORE_CRC16_IBM_3740

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void interbus_fcs(uint8_t *restrict work);

static void interbus_fcs(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *bytes = Interbus.fcs_args.bytes;
    size_t len = Interbus.fcs_args.len;

    // CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, xorout 0 - cataloged as CRC-16/IBM-3740.
    Crc.args.params = &PROTOCORE_CRC16_IBM_3740;
    Crc.args.data = bytes;
    Crc.args.len = len;
    Crc.compute(crc_work);
    Interbus.value = (uint16_t)Crc.value;
}

static void interbus_build(uint8_t *restrict work)
{
    const uint16_t *words = Interbus.build_args.words;
    size_t word_count = Interbus.build_args.word_count;
    uint8_t *out = Interbus.build_args.out;
    size_t cap = Interbus.build_args.cap;

    if (!out || (word_count && !words))
    {
        Interbus.n = 0;
        return;
    }
    size_t n = 2 + word_count * 2 + 2; // loopback + words + FCS
    if (n > cap)
    {
        Interbus.n = 0;
        return;
    }
    size_t i = 0;
    out[i++] = (uint8_t)(PROTOCORE_INTERBUS_LOOPBACK >> 8);
    out[i++] = (uint8_t)PROTOCORE_INTERBUS_LOOPBACK;
    for (size_t w = 0; w < word_count; w++)
    {
        out[i++] = (uint8_t)(words[w] >> 8); // big-endian
        out[i++] = (uint8_t)words[w];
    }
    Interbus.fcs_args.bytes = out;
    Interbus.fcs_args.len = i;
    interbus_fcs(work);
    uint16_t crc = Interbus.value; // FCS over loopback + words
    out[i++] = (uint8_t)(crc >> 8);
    out[i++] = (uint8_t)crc;
    Interbus.n = i;
}

static void interbus_parse(uint8_t *restrict work)
{
    const uint8_t *frame = Interbus.parse_args.frame;
    size_t len = Interbus.parse_args.len;
    uint16_t *out_words = Interbus.parse_args.out_words;
    size_t max_words = Interbus.parse_args.max_words;
    size_t *out_count = Interbus.parse_args.out_count;

    if (!frame || !out_words || !out_count || len < 4) // loopback + FCS minimum
    {
        Interbus.ok = PROTO_FALSE;
        return;
    }
    if (((frame[0] << 8) | frame[1]) != PROTOCORE_INTERBUS_LOOPBACK)
    {
        Interbus.ok = PROTO_FALSE;
        return;
    }
    if ((len - 4) % 2 != 0) // the words region must be whole 16-bit words
    {
        Interbus.ok = PROTO_FALSE;
        return;
    }
    size_t word_count = (len - 4) / 2;
    if (word_count > max_words)
    {
        Interbus.ok = PROTO_FALSE;
        return;
    }
    Interbus.fcs_args.bytes = frame;
    Interbus.fcs_args.len = len - 2;
    interbus_fcs(work);
    uint16_t want = Interbus.value;
    uint16_t got = (uint16_t)((frame[len - 2] << 8) | frame[len - 1]);
    if (want != got)
    {
        Interbus.ok = PROTO_FALSE;
        return;
    }
    for (size_t w = 0; w < word_count; w++)
    {
        out_words[w] = (uint16_t)((frame[2 + w * 2] << 8) | frame[2 + w * 2 + 1]);
    }
    *out_count = word_count;
    Interbus.ok = PROTO_TRUE;
}

InterbusNs Interbus = {.fcs = interbus_fcs, .build = interbus_build, .parse = interbus_parse};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_INTERBUS
