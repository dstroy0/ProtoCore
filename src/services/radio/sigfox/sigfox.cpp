// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sigfox.cpp
 * @brief Sigfox modem AT-command codec - implementation.
 *
 * `AT$SF=<hex>` sends one uplink; the modem replies "OK" on success or "ERROR". The payload
 * is hex-encoded (uppercase, two nibbles per byte) into the command.
 */

#include "services/radio/sigfox/sigfox.h"

#if PC_ENABLE_SIGFOX

namespace
{
char hex_nibble(uint8_t v)
{
    return (char)(v < 10 ? '0' + v : 'A' + (v - 10));
}

// Is the needle present in the first len bytes of haystack?
bool contains(const char *hay, uint16_t len, const char *needle)
{
    uint16_t nlen = 0;
    while (needle[nlen])
    {
        nlen++;
    }
    if (nlen == 0 || len < nlen) // GCOVR_EXCL_LINE  nlen==0 is unreachable: contains() is only ever called
    {
        return false; // (internally, both call sites below) with the literal non-empty needles "ERROR"/"OK"
    }
    for (uint16_t i = 0; i + nlen <= len; i++)
    {
        uint16_t j = 0;
        while (j < nlen && hay[i + j] == needle[j])
        {
            j++;
        }
        if (j == nlen)
        {
            return true;
        }
    }
    return false;
}
} // namespace

uint16_t pc_sigfox_build_uplink(const uint8_t *payload, uint8_t len, char *out, uint16_t cap)
{
    if (!out || !payload || len == 0 || len > PC_SIGFOX_MAX_PAYLOAD)
    {
        return 0;
    }
    // "AT$SF=" (6) + 2*len hex + "\r\n" (2) + NUL (1)
    uint16_t need = (uint16_t)(6 + 2 * len + 2 + 1);
    if (need > cap)
    {
        return 0;
    }
    const char *pfx = "AT$SF=";
    uint16_t p = 0;
    for (const char *s = pfx; *s; s++)
    {
        out[p++] = *s;
    }
    for (uint8_t i = 0; i < len; i++)
    {
        out[p++] = hex_nibble((uint8_t)(payload[i] >> 4));
        out[p++] = hex_nibble((uint8_t)(payload[i] & 0x0F));
    }
    out[p++] = '\r';
    out[p++] = '\n';
    out[p] = '\0';
    return p;
}

pc_sigfox_result pc_sigfox_parse_response(const char *buf, uint16_t len)
{
    if (!buf || len == 0)
    {
        return pc_sigfox_result::SIGFOX_PENDING;
    }
    if (contains(buf, len, "ERROR"))
    {
        return pc_sigfox_result::SIGFOX_ERROR;
    }
    if (contains(buf, len, "OK"))
    {
        return pc_sigfox_result::SIGFOX_OK;
    }
    return pc_sigfox_result::SIGFOX_PENDING;
}

#endif // PC_ENABLE_SIGFOX
