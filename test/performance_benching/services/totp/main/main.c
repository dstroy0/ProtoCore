// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

static uint8_t totp_work[16]; // the borrow an entry takes; Totp never reads it

/** @brief HOTP(K,C) over @p k for counter @p c, @p digit digits wide. */
static uint32_t hotp_of(const uint8_t *k, size_t keylen, uint64_t c, uint8_t digit)
{
    TotpV.k = k;
    TotpV.keylen = keylen;
    TotpV.digit = digit;
    TotpV.step.counter = c;
    Totp.hotp(totp_work);
    return TotpV.u32;
}

/** @brief HOTP(K,T) over @p k at Unix time @p t, time step @p x, @p digit digits wide. */
static uint32_t totp_of(const uint8_t *k, size_t keylen, uint64_t t, uint32_t x, uint8_t digit)
{
    TotpV.k = k;
    TotpV.keylen = keylen;
    TotpV.digit = digit;
    TotpV.step.unix_time = t;
    TotpV.step.t0 = 0;
    TotpV.step.x = x;
    Totp.totp(totp_work);
    return TotpV.u32;
}

/** @brief Decode the base32 text @p b32 into @p out; the K bytes written, -1 on a refusal. */
static int32_t base32_of(const char *b32, uint8_t *out, size_t cap)
{
    TotpV.secret.b32 = b32;
    TotpV.secret.out = out;
    TotpV.secret.cap = cap;
    Totp.base32_decode(totp_work);
    return TotpV.i32;
}

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
        DBENCH_OP("Totp.hotp (HMAC-SHA1)", 20000, sink += hotp_of(SECRET, sizeof(SECRET), 1234ull, 6));
        DBENCH_OP("Totp.totp (RFC 6238)", 20000, sink += totp_of(SECRET, sizeof(SECRET), 1234567890ull, 30, 8));
        static uint8_t dec[20];
        DBENCH_OP("Totp.base32_decode", 100000, sink += (uint32_t)base32_of(B32, dec, sizeof(dec)));
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("totp")
