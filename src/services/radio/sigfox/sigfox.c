// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sigfox.c
 * @brief Sigfox modem AT-command codec - implementation.
 *
 * `AT$SF=<hex>` sends one uplink; the modem replies "OK" on success or "ERROR". The payload
 * is hex-encoded (uppercase, two nibbles per byte) into the command.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SIGFOX

#include "services/radio/sigfox/sigfox.h"

PROTOCORE_BEGIN_DECLS

static char hex_nibble(uint8_t v)
{
    return (char)(v < 10 ? '0' + v : 'A' + (v - 10));
}

// Is the needle present in the first len bytes of haystack?
static proto_bool contains(const char *hay, uint16_t len, const char *needle)
{
    uint16_t nlen = 0;
    while (needle[nlen])
    {
        nlen++;
    }
    if (nlen == 0 || len < nlen)
    {
        return PROTO_FALSE; // (internally, both call sites below) with the literal non-empty needles "ERROR"/"OK"
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
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void sigfox_build_uplink(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *payload = Sigfox.build_uplink_args.payload;
    uint8_t len = Sigfox.build_uplink_args.len;
    char *out = Sigfox.build_uplink_args.out;
    uint16_t cap = Sigfox.build_uplink_args.cap;

    if (!out || !payload || len == 0 || len > PROTOCORE_SIGFOX_MAX_PAYLOAD)
    {
        Sigfox.value = 0;
        return;
    }
    // "AT$SF=" (6) + 2*len hex + "\r\n" (2) + NUL (1)
    uint16_t need = (uint16_t)(6 + 2 * len + 2 + 1);
    if (need > cap)
    {
        Sigfox.value = 0;
        return;
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
    Sigfox.value = p;
}

static void sigfox_parse_response(uint8_t *restrict work)
{
    (void)work;
    const char *buf = Sigfox.parse_response_args.buf;
    uint16_t len = Sigfox.parse_response_args.len;

    if (!buf || len == 0)
    {
        Sigfox.status = SIGFOX_PENDING;
        return;
    }
    if (contains(buf, len, "ERROR"))
    {
        Sigfox.status = SIGFOX_ERROR;
        return;
    }
    if (contains(buf, len, "OK"))
    {
        Sigfox.status = SIGFOX_OK;
        return;
    }
    Sigfox.status = SIGFOX_PENDING;
}

SigfoxNs Sigfox = {.build_uplink = sigfox_build_uplink, .parse_response = sigfox_parse_response};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SIGFOX
