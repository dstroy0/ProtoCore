// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the stateless HMAC-signed CSRF token (server/security/csrf):
// Csrf.issue builds a fresh `<nonce_hex>.<sig_hex>` token (HMAC-SHA256 over a 6-byte nonce,
// truncated + hex-encoded) and Csrf.verify recomputes that HMAC and constant-time compares
// it - both pure (no Arduino, no sockets, no heap). Also bench the shared hex codec
// (shared/hex/hex.h) the token layer builds on, since encode/decode of the nonce and
// signature bytes is on the same hot path as issue/verify. Worked example for performance_benching/device/<service>/:
// a pure protocol codec with no hardware involved, so every call here exercises the real production
// code path (contrast with performance_benching/device/ads1115, a peripheral driver where the bus transaction itself
// is stubbed). Out of scope: the hardware-RNG secret seeding a real begin() does once at boot - the
// bench seeds a fixed secret instead, exactly like test/test_csrf/test_csrf.cpp, so issue/verify are
// deterministic and comparable run to run.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/csrf -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/security/csrf/csrf.h"
#include "shared/hex/hex.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t hex_work[16]; // the borrow an entry takes; Hex never reads it

// Same fixed secret used by test/test_csrf/test_csrf.cpp - deterministic, known-good.
static const uint8_t SECRET[32] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa,
                                   0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba,
                                   0xdc, 0xfe, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

/** @brief Render @p raw as CSRF_NONCE_BYTES * 2 lowercase hex characters plus a NUL at @p out. */
static void hex_encode_nonce(const uint8_t *raw, char *out)
{
    Hex.io.in = raw;
    Hex.io.n = CSRF_NONCE_BYTES;
    Hex.io.out = out;
    Hex.args.upper = PROTO_FALSE;
    Hex.encode(hex_work);
}

/** @brief Read CSRF_NONCE_BYTES * 2 hex characters at @p text back into @p out; bytes written. */
static int32_t hex_decode_nonce(const char *text, uint8_t *out)
{
    Hex.io.text = text;
    Hex.io.n = CSRF_NONCE_BYTES * 2;
    Hex.io.bytes = out;
    Hex.io.cap = CSRF_NONCE_BYTES;
    Hex.decode(hex_work);
    return Hex.i32;
}

void dbench_run(void)
{
    Csrf.secret_args.secret = SECRET;
    Csrf.secret_args.len = sizeof(SECRET);
    Csrf.set_secret(protocore_csrf_span());

    static char token[CSRF_TOKEN_BUF];
    Csrf.issue_args.out = token;
    Csrf.issue_args.cap = sizeof(token);
    Csrf.issue(protocore_csrf_span()); // seed a valid token for the verify bench
    int tlen = Csrf.n;
    (void)tlen;

    static const uint8_t raw6[CSRF_NONCE_BYTES] = {0xde, 0xad, 0xbe, 0xef, 0x01, 0x02};
    static char hex_out[CSRF_NONCE_BYTES * 2 + 1];
    hex_encode_nonce(raw6, hex_out);
    static uint8_t bin_out[CSRF_NONCE_BYTES];

    for (;;)
    {
        DBENCH_BANNER("csrf");
        volatile int sinki = 0;
        volatile bool sinkb = false;

        Csrf.issue_args.out = token;
        Csrf.issue_args.cap = sizeof(token);
        DBENCH_OP("Csrf.issue", 20000, {
            Csrf.issue(protocore_csrf_span());
            sinki += Csrf.n;
        });
        Csrf.verify_args.token = token;
        DBENCH_OP("Csrf.verify", 20000, {
            Csrf.verify(protocore_csrf_span());
            sinkb = Csrf.valid;
        });
        DBENCH_OP("Hex.encode (6B nonce)", 50000, hex_encode_nonce(raw6, hex_out));
        DBENCH_OP("Hex.decode (6B nonce)", 50000, sinki += hex_decode_nonce(hex_out, bin_out));

        (void)sinki;
        (void)sinkb;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("csrf")
