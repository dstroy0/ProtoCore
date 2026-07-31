// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cclink.cpp
 * @brief CC-Link cyclic fieldbus frame codec (see cclink.h).
 */

#include "services/fieldbus/cclink/cclink.h"

#if PC_ENABLE_CCLINK

#include <string.h>

uint8_t pc_cclink_sum(const uint8_t *bytes, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
    {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum;
}

size_t pc_cclink_build(uint8_t station, uint8_t command, const uint8_t *bits, size_t bit_len, const uint8_t *words,
                       size_t word_len, uint8_t *out, size_t cap)
{
    if (!out || (bit_len && !bits) || (word_len && !words) || station > 63)
    {
        return 0;
    }
    size_t n = 2 + bit_len + word_len + 1;
    if (n > cap)
    {
        return 0;
    }
    size_t i = 0;
    out[i++] = station;
    out[i++] = command;
    if (bit_len)
    {
        memcpy(out + i, bits, bit_len);
        i += bit_len;
    }
    if (word_len)
    {
        memcpy(out + i, words, word_len);
        i += word_len;
    }
    out[i] = pc_cclink_sum(out, i); // checksum over station..last data
    i++;
    return i;
}

bool pc_cclink_parse(const uint8_t *frame, size_t len, CcLinkFrame *out)
{
    if (!frame || !out || len < 3) // station + command + checksum
    {
        return false;
    }
    size_t body = len - 1;
    if (pc_cclink_sum(frame, body) != frame[body])
    {
        return false;
    }
    out->station = frame[0];
    out->command = frame[1];
    out->payload = (body > 2) ? (frame + 2) : nullptr;
    out->payload_len = body - 2;
    return true;
}

bool pc_cclink_get_bit(const uint8_t *bits, size_t bit_len, size_t index)
{
    if (!bits || index / 8 >= bit_len)
    {
        return false;
    }
    return (bits[index / 8] >> (index % 8)) & 1u;
}

void pc_cclink_set_bit(uint8_t *bits, size_t bit_len, size_t index, bool value)
{
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

uint16_t pc_cclink_get_word(const uint8_t *words, size_t word_len, size_t index)
{
    size_t off = index * 2;
    if (!words || off + 1 >= word_len)
    {
        return 0;
    }
    return (uint16_t)(words[off] | (words[off + 1] << 8)); // little-endian
}

#endif // PC_ENABLE_CCLINK
