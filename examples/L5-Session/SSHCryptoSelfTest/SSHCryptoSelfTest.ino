// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file SSHCryptoSelfTest.ino
 * @brief On-device check that the ESP32 hardware-accelerated SSH crypto is correct.
 *
 * Runs the streaming SHA-256, HMAC-SHA-256, and AES-256-CTR primitives - which
 * on ESP32 route through mbedtls/the hardware accelerator - against published
 * known-answer vectors and prints PASS/FAIL over Serial. Useful after enabling
 * the HW paths (streaming SHA via mbedtls_sha256_*, AES via
 * mbedtls_aes_crypt_ctr) to confirm the device matches the spec.
 *
 * Vectors:
 *   SHA-256("abc")                      FIPS 180-4
 *   HMAC-SHA-256 RFC 4231 test case 1   ("Hi There", key = 0x0b x20)
 *   AES-256-CTR NIST SP 800-38A F.5.5   (first block)
 *
 * Flash to the board, open Serial at 115200, expect "ALL TESTS PASSED".
 */

#include "protocore.h" // discovers the library (adds src/ to the include path) - MUST come first

#include "crypto/cipher/aes256ctr.h"
#include "crypto/hash/sha256.h"
#include "crypto/mac/hmac_sha256.h"
#include <string.h>

static bool eq(const uint8_t *a, const uint8_t *b, size_t n)
{
    return memcmp(a, b, n) == 0;
}

static void report(const char *name, bool ok, bool &all_ok)
{
    Serial.printf("  %-22s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok)
    {
        all_ok = false;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\nSSH HW crypto self-test");

    bool all_ok = true;

    // --- SHA-256("abc") streaming ---
    {
        static const uint8_t want[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                         0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                         0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
        pc_sha256_ctx c;
        uint8_t out[32];
        pc_sha256_init(&c);
        pc_sha256_update(&c, (const uint8_t *)"a", 1);
        pc_sha256_update(&c, (const uint8_t *)"bc", 2); // chunked → exercises streaming
        pc_sha256_final(&c, out);
        report("sha256 streaming", eq(out, want, 32), all_ok);
    }

    // --- HMAC-SHA-256 RFC 4231 test case 1 ---
    {
        uint8_t key[20];
        memset(key, 0x0b, sizeof(key));
        static const uint8_t want[32] = {0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf,
                                         0xce, 0xaf, 0x0b, 0xf1, 0x2b, 0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83,
                                         0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7};
        uint8_t mac[32];
        pc_hmac_sha256(key, sizeof(key), (const uint8_t *)"Hi There", 8, mac);
        report("hmac-sha256 rfc4231", eq(mac, want, 32), all_ok);
    }

    // --- AES-256-CTR NIST SP 800-38A F.5.5 (first block) ---
    {
        static const uint8_t key[32] = {0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe, 0x2b, 0x73, 0xae,
                                        0xf0, 0x85, 0x7d, 0x77, 0x81, 0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61,
                                        0x08, 0xd7, 0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4};
        static const uint8_t iv[16] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
                                       0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};
        static const uint8_t pt[16] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
                                       0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
        static const uint8_t want[16] = {0x60, 0x1e, 0xc3, 0x13, 0x77, 0x57, 0x89, 0xa5,
                                         0xb7, 0xa7, 0xf5, 0x04, 0xbb, 0xf3, 0xd2, 0x28};
        uint8_t counter[16]; // stateless API advances the counter in place, so copy the IV out of const storage
        memcpy(counter, iv, sizeof(counter));
        uint8_t out[16];
        // Key schedule and keystream ride the wiped crypto scratch; no cipher state persists on the stack.
        pc_aes256ctr_crypt(key, counter, pt, out, 16);
        report("aes256-ctr nist", eq(out, want, 16), all_ok);
    }

    Serial.println(all_ok ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
}

void loop()
{
    delay(1000);
}
