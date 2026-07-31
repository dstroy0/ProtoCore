// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Test-only throwaway key material. A functional test that needs "a host key"
// (as opposed to a fixed known-answer vector) should not depend on one embedded
// private key: generate a fresh one each run so no test silently relies on a
// specific key, and so no private bytes live in the repo.
//
// This borrows the discipline from fuzzing rather than fixed-fixture unit tests:
// the seed is fresh-random per run BUT is logged and can be pinned via the
// PC_TEST_KEY_SEED environment variable (64 hex chars), so any failure
// reproduces exactly - which keeps a randomized input honest in a suite that is
// otherwise deterministic. Known-answer vectors (test_crypto_kat, RFC vectors)
// stay fixed and must never use this.

#ifndef PROTOCORE_TEST_THROWAWAY_KEY_H
#define PROTOCORE_TEST_THROWAWAY_KEY_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

// Fill out[32] with a throwaway Ed25519 seed. If PC_TEST_KEY_SEED holds 64 hex
// chars it is used verbatim (reproduce a run); otherwise a fresh random seed is
// drawn. Either way the seed is logged with the exact re-pin command.
static inline void throwaway_ed25519_seed(uint8_t out[32])
{
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return -1;
    };

    const char *pin = getenv("PC_TEST_KEY_SEED");
    bool pinned = false;
    if (pin && strlen(pin) >= 64)
    {
        uint8_t tmp[32]; // parse into a scratch buffer; only commit to out[] once fully valid
        pinned = true;
        for (int i = 0; i < 32 && pinned; i++)
        {
            int hi = nib(pin[2 * i]), lo = nib(pin[2 * i + 1]);
            if (hi < 0 || lo < 0)
            {
                pinned = false; // not valid hex -> fall back to random (out[] never saw pin bytes)
            }
            else
            {
                tmp[i] = (uint8_t)((hi << 4) | lo);
            }
        }
        if (pinned)
        {
            memcpy(out, tmp, 32);
        }
    }
    if (pinned)
    {
        // Reproduced from the caller-supplied seed - it is already known to them, so do not echo it
        // back (echoing getenv-sourced bytes to stdout is what a taint scan flags, and adds nothing).
        printf("[throwaway-key] ed25519 seed pinned via PC_TEST_KEY_SEED\n");
        return;
    }

    std::random_device rd; // OS entropy on the host
    for (int i = 0; i < 32; i++)
    {
        out[i] = (uint8_t)(rd() & 0xff);
    }

    char hex[65];
    static const char *H = "0123456789abcdef";
    for (int i = 0; i < 32; i++)
    {
        hex[2 * i] = H[out[i] >> 4];
        hex[2 * i + 1] = H[out[i] & 0x0f];
    }
    hex[64] = '\0';
    printf("[throwaway-key] ed25519 seed=%s (re-pin: PC_TEST_KEY_SEED=%s)\n", hex, hex);
}

#endif // PROTOCORE_TEST_THROWAWAY_KEY_H
