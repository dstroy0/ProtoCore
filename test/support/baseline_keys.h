// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A concrete, committed set of baseline Ed25519 host-key seeds. Functional SSH
// tests run these fixed keys for deterministic regression AND, alongside them, a
// fresh throwaway key (throwaway_key.h) for per-run variation - fixed baseline
// plus fuzz-style coverage, the way a crypto suite pairs KAT vectors with
// property tests.
//
// These are intentionally PUBLIC test fixtures (the RFC 8032 sec 7.1 test seeds),
// not secrets: they authenticate nothing and exist only to pin behavior. Do not
// use them, or any committed key, in a real deployment.
//
// The RSA-2048 baseline is the committed host key in
// test/fixtures/ssh_test_host_key/ssh_test_host_key.h, restated as
// PC_SSH_BASELINE_KEY_* in the build-time header tools/crypto/gen_ssh_test_keys.py
// writes beside it.

#ifndef PROTOCORE_TEST_BASELINE_KEYS_H
#define PROTOCORE_TEST_BASELINE_KEYS_H

#include <stdint.h>

// RFC 8032 sec 7.1 Test 1 and Test 3 private-key seeds.
static const uint8_t BASELINE_ED25519_SEEDS[][32] = {
    {0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4,
     0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60},
    {0x4c, 0xcd, 0x08, 0x9b, 0x28, 0xff, 0x96, 0xda, 0x9d, 0xb6, 0xc3, 0x46, 0xec, 0x11, 0x4e, 0x0f,
     0x5b, 0x8a, 0x31, 0x9f, 0x35, 0xab, 0xa6, 0x24, 0xda, 0x8c, 0xf6, 0xed, 0x4f, 0xb8, 0xa6, 0xfb},
};
#define BASELINE_ED25519_COUNT (sizeof(BASELINE_ED25519_SEEDS) / sizeof(BASELINE_ED25519_SEEDS[0]))

#endif // PROTOCORE_TEST_BASELINE_KEYS_H
