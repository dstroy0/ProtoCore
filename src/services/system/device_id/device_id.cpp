// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file device_id.cpp
 * @brief Stable MAC-derived device UUID - implementation. See device_id.h.
 */

#include "device_id.h"
#include "shared_primitives/hex.h"

#if PC_ENABLE_DEVICE_ID

#include "crypto/hash/sha1.h"

#if PROTOCORE_HOT
#include <esp_mac.h> // esp_read_mac()
#endif

// RFC 4122 DNS namespace UUID (6ba7b810-9dad-11d1-80b4-00c04fd430c8).
static const uint8_t NS_DNS[16] = {0x6b, 0xa7, 0xb8, 0x10, 0x9d, 0xad, 0x11, 0xd1,
                                   0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8};

void pc_uuid_from_mac(const uint8_t mac[6], char out[PC_UUID_STR_LEN])
{
    // UUIDv5 name = lowercase MAC hex (12 chars, no separators).
    uint8_t input[16 + 12];
    for (int i = 0; i < 16; i++)
    {
        input[i] = NS_DNS[i];
    }
    for (int i = 0; i < 6; i++)
    {
        input[16 + i * 2] = (uint8_t)pc_hex_digit((uint8_t)(mac[i] >> 4), PROTO_FALSE);
        input[16 + i * 2 + 1] = (uint8_t)pc_hex_digit((uint8_t)(mac[i] & 0x0F), PROTO_FALSE);
    }

    uint8_t h[PC_SHA1_DIGEST_LEN];
    pc_sha1(input, sizeof(input), h);
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
            out[oi++] = pc_hex_digit((uint8_t)(h[hi] >> 4), PROTO_FALSE);
            out[oi++] = pc_hex_digit((uint8_t)(h[hi] & 0x0F), PROTO_FALSE);
            hi++;
        }
    }
    out[oi] = '\0';
}

#if PROTOCORE_HOT
void pc_device_uuid(char out[PC_UUID_STR_LEN])
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA); // stable factory MAC
    pc_uuid_from_mac(mac, out);
}
#endif

#endif // PC_ENABLE_DEVICE_ID
