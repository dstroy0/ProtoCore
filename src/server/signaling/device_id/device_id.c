// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file device_id.c
 * @brief Stable MAC-derived device UUID - implementation. See device_id.h.
 */

#include "server/signaling/device_id/device_id.h"
#include "shared/hex/hex.h"

static uint8_t hex_work[16]; // the borrow an entry takes; Hex never reads it

#if PROTOCORE_ENABLE_DEVICE_ID

#include "crypto/hash/sha1/sha1.h"
#include "mmgr/secure/secure.h" // the pool the digest borrow comes from
#include "mmgr/span/span.h"     // protocore_span, span.ok

// RFC 4122 DNS namespace UUID (6ba7b810-9dad-11d1-80b4-00c04fd430c8).
static const uint8_t NS_DNS[16] = {0x6b, 0xa7, 0xb8, 0x10, 0x9d, 0xad, 0x11, 0xd1,
                                   0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8};

// The lowercase hex character for one nibble.
static char hex_digit(uint8_t nibble)
{
    Hex.args.nibble = nibble;
    Hex.args.upper = PROTO_FALSE;
    Hex.digit(hex_work);
    return Hex.ch;
}

static void devid_from_mac(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *mac = DeviceId.args.mac;
    char *out = DeviceId.args.out;

    // UUIDv5 name = lowercase MAC hex (12 chars, no separators).
    uint8_t input[16 + 12];
    for (int i = 0; i < 16; i++)
    {
        input[i] = NS_DNS[i];
    }
    for (int i = 0; i < 6; i++)
    {
        input[16 + i * 2] = (uint8_t)hex_digit((uint8_t)(mac[i] >> 4));
        input[16 + i * 2 + 1] = (uint8_t)hex_digit((uint8_t)(mac[i] & 0x0F));
    }

    uint8_t h[PROTOCORE_SHA1_DIGEST_LEN];
    const size_t mark = protocore_secure_mark();
    protocore_span w = protocore_secure_span(PROTOCORE_SHA1_BORROW, 8);
    if (!span.ok(w))
    {
        protocore_secure_release(mark);
        out[0] = '\0';
        return;
    }
    Sha1V.hash_args.data = input;
    Sha1V.hash_args.len = sizeof(input);
    Sha1V.hash_args.out = h;
    Sha1.hash(w.buf);
    protocore_secure_release(mark);
    h[6] = (uint8_t)((h[6] & 0x0F) | 0x50); // version 5
    h[8] = (uint8_t)((h[8] & 0x3F) | 0x80); // RFC 4122 variant

    // Format as 8-4-4-4-12 (16 bytes -> 32 hex + 4 dashes).
    static const int groups[5] = {4, 2, 2, 2, 6};
    int hi = 0;
    int oi = 0;
    for (int g = 0; g < 5; g++)
    {
        if (g)
        {
            out[oi++] = '-';
        }
        for (int b = 0; b < groups[g]; b++)
        {
            out[oi++] = hex_digit((uint8_t)(h[hi] >> 4));
            out[oi++] = hex_digit((uint8_t)(h[hi] & 0x0F));
            hi++;
        }
    }
    out[oi] = '\0';
}

#if PROTOCORE_HAS_VENDOR_MAC
static void devid_uuid(uint8_t *restrict work)
{
    uint8_t mac[6] = {0};
    (void)protocore_platform_mac_read(mac); // the stable factory address; leaves zeros when it has none
    DeviceId.args.mac = mac;
    devid_from_mac(work);
}
#endif

DeviceIdNs DeviceId = {
    .from_mac = devid_from_mac,
#if PROTOCORE_HAS_VENDOR_MAC
    .uuid = devid_uuid,
#endif
};

#endif // PROTOCORE_ENABLE_DEVICE_ID
