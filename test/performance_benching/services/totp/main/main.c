// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the TOTP/HOTP codec (services/security/totp): RFC 4226 HOTP and
// RFC 6238 TOTP (both HMAC-SHA1 based) plus base32 secret decode. Pure crypto; no clock or network.
// Iteration counts are lower than a plain codec because each op runs a full HMAC-SHA1.
//
// Build/flash:  idf.py -C test/performance_benching/totp -t upload --upload-port COM7
#include "device_bench.h"
#include "services/security/totp/totp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dbench_run(void)
{
    // RFC 6238 SHA-1 secret: ASCII "12345678901234567890" (20 bytes).
    static const uint8_t SECRET[20] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
                                       '1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
    static const char B32[] = "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ";

    for (;;)
    {
        DBENCH_BANNER("totp");
        volatile uint32_t sink = 0;
        DBENCH_OP("protocore_hotp (HMAC-SHA1)", 20000, sink += protocore_hotp(SECRET, sizeof(SECRET), 1234ull, 6));
        DBENCH_OP("protocore_totp (RFC 6238)", 20000,
                  sink += protocore_totp(SECRET, sizeof(SECRET), 1234567890ull, 30, 8));
        static uint8_t dec[20];
        DBENCH_OP("protocore_base32_decode", 100000, sink += protocore_base32_decode(B32, dec, sizeof(dec)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("totp")
