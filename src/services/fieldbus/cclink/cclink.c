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

void protocore_cclink_sum(uint8_t *restrict work);

void protocore_cclink_sum(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *bytes = CclinkV.sum_args.bytes;
    size_t len = CclinkV.sum_args.len;

    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
    {
        sum = (uint8_t)(sum + bytes[i]);
    }
    CclinkV.value = sum;
}

void protocore_cclink_build(uint8_t *restrict work)
{
    uint8_t station = CclinkV.build_args.station;
    uint8_t command = CclinkV.build_args.command;
    const uint8_t *bits = CclinkV.build_args.bits;
    size_t bit_len = CclinkV.build_args.bit_len;
    const uint8_t *words = CclinkV.build_args.words;
    size_t word_len = CclinkV.build_args.word_len;
    uint8_t *out = CclinkV.build_args.out;
    size_t cap = CclinkV.build_args.cap;

    if (!out || (bit_len && !bits) || (word_len && !words) || station > 63)
    {
        CclinkV.n = 0;
        return;
    }
    size_t n = 2 + bit_len + word_len + 1;
    if (n > cap)
    {
        CclinkV.n = 0;
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
    CclinkV.sum_args.bytes = out;
    CclinkV.sum_args.len = i;
    protocore_cclink_sum(work);
    out[i] = CclinkV.value; // checksum over station..last data
    i++;
    CclinkV.n = i;
}

void protocore_cclink_parse(uint8_t *restrict work)
{
    const uint8_t *frame = CclinkV.parse_args.frame;
    size_t len = CclinkV.parse_args.len;
    CcLinkFrame *out = CclinkV.parse_args.out;

    if (!frame || !out || len < 3) // station + command + checksum
    {
        CclinkV.ok = PROTO_FALSE;
        return;
    }
    size_t body = len - 1;
    CclinkV.sum_args.bytes = frame;
    CclinkV.sum_args.len = body;
    protocore_cclink_sum(work);
    if (CclinkV.value != frame[body])
    {
        CclinkV.ok = PROTO_FALSE;
        return;
    }
    out->station = frame[0];
    out->command = frame[1];
    out->payload = (body > 2) ? (frame + 2) : NULL;
    out->payload_len = body - 2;
    CclinkV.ok = PROTO_TRUE;
}

void protocore_cclink_get_bit(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *bits = CclinkV.get_bit_args.bits;
    size_t bit_len = CclinkV.get_bit_args.bit_len;
    size_t index = CclinkV.get_bit_args.index;

    if (!bits || index / 8 >= bit_len)
    {
        CclinkV.ok = PROTO_FALSE;
        return;
    }
    CclinkV.ok = (bits[index / 8] >> (index % 8)) & 1u;
}

void protocore_cclink_set_bit(uint8_t *restrict work)
{
    (void)work;
    uint8_t *bits = CclinkV.set_bit_args.bits;
    size_t bit_len = CclinkV.set_bit_args.bit_len;
    size_t index = CclinkV.set_bit_args.index;
    proto_bool value = CclinkV.set_bit_args.value;

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

void protocore_cclink_get_word(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *words = CclinkV.get_word_args.words;
    size_t word_len = CclinkV.get_word_args.word_len;
    size_t index = CclinkV.get_word_args.index;

    size_t off = index * 2;
    if (!words || off + 1 >= word_len)
    {
        CclinkV.u16 = 0;
        return;
    }
    CclinkV.u16 = (uint16_t)(words[off] | (words[off + 1] << 8)); // little-endian
}

/** @brief The operands and the outcome. */
CclinkVars CclinkV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CCLINK
