// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha3.h
 * @brief Keccak-f[1600] sponge: SHA3-256, SHA3-512, SHAKE128, SHAKE256 (FIPS 202).
 *
 * The symmetric primitives ML-KEM (FIPS 203) is built on: G = SHA3-512, H = SHA3-256, the matrix
 * XOF = SHAKE128, and the noise PRF = SHAKE256. Zero-heap, endian-independent (the sponge state is
 * addressed as a little-endian byte string regardless of host byte order), no external dependency.
 *
 * One-shot helpers cover fixed-length digests and arbitrary SHAKE output. For an incremental XOF
 * (ML-KEM samples the public matrix by squeezing three bytes at a time) absorb once with
 * pc_shake128_absorb() then pull with pc_keccak_squeeze() as many times as needed.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SHA3_H
#define PROTOCORE_SHA3_H

#include "protocore_config.h"

#if PC_ENABLE_PQC_KEX

/// Sponge rates (block size in octets = 1600/8 - 2*capacity/8) for the modes we use.
#define KECCAK_RATE_SHA3_256 136
#define KECCAK_RATE_SHA3_512 72
#define KECCAK_RATE_SHAKE128 168
#define KECCAK_RATE_SHAKE256 136

/// A Keccak sponge in the squeeze phase; `out_pos` is how many octets of the current block are spent.
typedef struct
{
    uint64_t st[25];
    uint32_t rate;
    uint32_t out_pos;
} KeccakCtx;

/// Absorb the whole message with domain-separation byte @p domain (0x06 SHA3, 0x1F SHAKE) and pad,
/// leaving @p c ready to squeeze. Handles any input length (multi-block).
void pc_keccak_absorb(KeccakCtx *c, uint32_t rate, const uint8_t *in, size_t inlen, uint8_t domain);

/// Squeeze @p outlen octets, permuting between blocks. May be called repeatedly for XOF use.
void pc_keccak_squeeze(KeccakCtx *c, uint8_t *out, size_t outlen);

/// SHA3-256 one-shot: 32-octet digest of @p in.
void sha3_256(uint8_t out[32], const uint8_t *in, size_t inlen);

/// SHA3-512 one-shot: 64-octet digest of @p in.
void sha3_512(uint8_t out[64], const uint8_t *in, size_t inlen);

/// SHAKE128 one-shot: @p outlen octets from @p in.
void shake128(uint8_t *out, size_t outlen, const uint8_t *in, size_t inlen);

/// SHAKE256 one-shot: @p outlen octets from @p in.
void shake256(uint8_t *out, size_t outlen, const uint8_t *in, size_t inlen);

/// Begin an incremental SHAKE128 XOF over @p in; pull output with pc_keccak_squeeze(@p c, ...).
void pc_shake128_absorb(KeccakCtx *c, const uint8_t *in, size_t inlen);

#endif // PC_ENABLE_PQC_KEX

#endif // PROTOCORE_SHA3_H
