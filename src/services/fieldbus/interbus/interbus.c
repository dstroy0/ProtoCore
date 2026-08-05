// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file interbus.c
 * @brief INTERBUS summation-frame codec (see interbus.h).
 */

#include "services/fieldbus/interbus/interbus.h"

#if PC_ENABLE_INTERBUS

#include "shared_primitives/crc.h" // PC_CRC16_IBM_3740

uint16_t pc_interbus_fcs(const uint8_t *bytes, size_t len)
{
    // CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, xorout 0 - cataloged as CRC-16/IBM-3740.
    return (uint16_t)pc_crc(&PC_CRC16_IBM_3740, bytes, len);
}

size_t pc_interbus_build(const uint16_t *words, size_t word_count, uint8_t *out, size_t cap)
{
    if (!out || (word_count && !words))
    {
        return 0;
    }
    size_t n = 2 + word_count * 2 + 2; // loopback + words + FCS
    if (n > cap)
    {
        return 0;
    }
    size_t i = 0;
    out[i++] = (uint8_t)(PC_INTERBUS_LOOPBACK >> 8);
    out[i++] = (uint8_t)PC_INTERBUS_LOOPBACK;
    for (size_t w = 0; w < word_count; w++)
    {
        out[i++] = (uint8_t)(words[w] >> 8); // big-endian
        out[i++] = (uint8_t)words[w];
    }
    uint16_t crc = pc_interbus_fcs(out, i); // FCS over loopback + words
    out[i++] = (uint8_t)(crc >> 8);
    out[i++] = (uint8_t)crc;
    return i;
}

proto_bool pc_interbus_parse(const uint8_t *frame, size_t len, uint16_t *out_words, size_t max_words, size_t *out_count)
{
    if (!frame || !out_words || !out_count || len < 4) // loopback + FCS minimum
    {
        return PROTO_FALSE;
    }
    if (((frame[0] << 8) | frame[1]) != PC_INTERBUS_LOOPBACK)
    {
        return PROTO_FALSE;
    }
    if ((len - 4) % 2 != 0) // the words region must be whole 16-bit words
    {
        return PROTO_FALSE;
    }
    size_t word_count = (len - 4) / 2;
    if (word_count > max_words)
    {
        return PROTO_FALSE;
    }
    uint16_t want = pc_interbus_fcs(frame, len - 2);
    uint16_t got = (uint16_t)((frame[len - 2] << 8) | frame[len - 1]);
    if (want != got)
    {
        return PROTO_FALSE;
    }
    for (size_t w = 0; w < word_count; w++)
    {
        out_words[w] = (uint16_t)((frame[2 + w * 2] << 8) | frame[2 + w * 2 + 1]);
    }
    *out_count = word_count;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_INTERBUS
