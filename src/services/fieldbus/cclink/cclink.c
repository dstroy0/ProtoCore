// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cclink.c
 * @brief CC-Link cyclic fieldbus frame codec (see cclink.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_CCLINK

#include "mmgr/protomem/protomem.h"
#include "services/fieldbus/cclink/cclink.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void cclink_sum(uint8_t *restrict work);

static void cclink_sum(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *bytes = Cclink.sum_args.bytes;
    size_t len = Cclink.sum_args.len;

    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
    {
        sum = (uint8_t)(sum + bytes[i]);
    }
    Cclink.value = sum;
}

static void cclink_build(uint8_t *restrict work)
{
    uint8_t station = Cclink.build_args.station;
    uint8_t command = Cclink.build_args.command;
    const uint8_t *bits = Cclink.build_args.bits;
    size_t bit_len = Cclink.build_args.bit_len;
    const uint8_t *words = Cclink.build_args.words;
    size_t word_len = Cclink.build_args.word_len;
    uint8_t *out = Cclink.build_args.out;
    size_t cap = Cclink.build_args.cap;

    if (!out || (bit_len && !bits) || (word_len && !words) || station > 63)
    {
        Cclink.n = 0;
        return;
    }
    size_t n = 2 + bit_len + word_len + 1;
    if (n > cap)
    {
        Cclink.n = 0;
        return;
    }
    size_t i = 0;
    out[i++] = station;
    out[i++] = command;
    if (bit_len)
    {
        mem.cpy(out + i, bits, bit_len);
        i += bit_len;
    }
    if (word_len)
    {
        mem.cpy(out + i, words, word_len);
        i += word_len;
    }
    Cclink.sum_args.bytes = out;
    Cclink.sum_args.len = i;
    cclink_sum(work);
    out[i] = Cclink.value; // checksum over station..last data
    i++;
    Cclink.n = i;
}

static void cclink_parse(uint8_t *restrict work)
{
    const uint8_t *frame = Cclink.parse_args.frame;
    size_t len = Cclink.parse_args.len;
    CcLinkFrame *out = Cclink.parse_args.out;

    if (!frame || !out || len < 3) // station + command + checksum
    {
        Cclink.ok = PROTO_FALSE;
        return;
    }
    size_t body = len - 1;
    Cclink.sum_args.bytes = frame;
    Cclink.sum_args.len = body;
    cclink_sum(work);
    if (Cclink.value != frame[body])
    {
        Cclink.ok = PROTO_FALSE;
        return;
    }
    out->station = frame[0];
    out->command = frame[1];
    out->payload = (body > 2) ? (frame + 2) : NULL;
    out->payload_len = body - 2;
    Cclink.ok = PROTO_TRUE;
}

static void cclink_get_bit(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *bits = Cclink.get_bit_args.bits;
    size_t bit_len = Cclink.get_bit_args.bit_len;
    size_t index = Cclink.get_bit_args.index;

    if (!bits || index / 8 >= bit_len)
    {
        Cclink.ok = PROTO_FALSE;
        return;
    }
    Cclink.ok = (bits[index / 8] >> (index % 8)) & 1u;
}

static void cclink_set_bit(uint8_t *restrict work)
{
    (void)work;
    uint8_t *bits = Cclink.set_bit_args.bits;
    size_t bit_len = Cclink.set_bit_args.bit_len;
    size_t index = Cclink.set_bit_args.index;
    proto_bool value = Cclink.set_bit_args.value;

    if (!bits || index / 8 >= bit_len)
    {
        return;
    }
    uint8_t mask = (uint8_t)(1u << (index % 8));
    if (value)
    {
        bits[index / 8] |= mask;
    }
    else
    {
        bits[index / 8] &= (uint8_t)~mask;
    }
}

static void cclink_get_word(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *words = Cclink.get_word_args.words;
    size_t word_len = Cclink.get_word_args.word_len;
    size_t index = Cclink.get_word_args.index;

    size_t off = index * 2;
    if (!words || off + 1 >= word_len)
    {
        Cclink.u16 = 0;
        return;
    }
    Cclink.u16 = (uint16_t)(words[off] | (words[off + 1] << 8)); // little-endian
}

CclinkNs Cclink = {.sum = cclink_sum,
                   .build = cclink_build,
                   .parse = cclink_parse,
                   .get_bit = cclink_get_bit,
                   .set_bit = cclink_set_bit,
                   .get_word = cclink_get_word};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CCLINK
