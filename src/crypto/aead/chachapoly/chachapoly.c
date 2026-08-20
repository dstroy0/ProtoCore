// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file chachapoly.c
 * @brief chacha20-poly1305@openssh.com - implementation. See chachapoly.h.
 *
 * The context is this file's. The module's own borrow holds the per-packet working set: the nonce, the
 * derived one-time Poly1305 key, the computed tag, and the decrypted length word.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_CHACHAPOLY

#include "crypto/aead/chachapoly/chachapoly.h"
#include "crypto/cipher/chacha20/chacha20.h"
#include "crypto/crypto_opt.h"
#include "crypto/ct_eq.h" // protocore_ct_eq
#include "crypto/mac/poly1305/poly1305.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The one definition, private to this TU. It sits at CHACHAPOLY_OFF_CTX in the caller's borrow, so its size
// never leaves this file and no consumer can name it.
//
// Only what is not derivable: the nonce, the derived one-time Poly1305 key, the computed tag, and the
// decrypted length word.
typedef struct
{
    uint8_t iv[8];        ///< the 8-byte ChaCha nonce
    uint8_t poly_key[32]; ///< Poly1305 key = K_main block 0
    uint8_t tag[16];      ///< the tag computed over the ciphertext
    uint8_t len[4];       ///< the decrypted length word
} ChachaPolyCtx;

// The caller's borrow, split: the per-packet working set, then the regions the nested ChaCha20 and
// Poly1305 run out of. Those two are driven through their own namespaces, so this borrow carries a
// region for each rather than naming any term of theirs.
#define CHACHAPOLY_OFF_CTX 0u
#define CHACHAPOLY_OFF_CHACHA (CHACHAPOLY_OFF_CTX + sizeof(ChachaPolyCtx))
#define CHACHAPOLY_OFF_POLY (CHACHAPOLY_OFF_CHACHA + PROTOCORE_CHACHA20_BORROW)
static_assert(CHACHAPOLY_OFF_POLY + PROTOCORE_POLY1305_BORROW <= PROTOCORE_CHACHAPOLY_BORROW,
              "PROTOCORE_CHACHAPOLY_BORROW is short of the per-packet working set and the two nested "
              "borrows - raise it in protocore_config.h, which derives PROTOCORE_SECURE_ARENA_SIZE from it");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(
    CHACHAPOLY_OFF_CTX % _Alignof(ChachaPolyCtx) == 0,
    "CHACHAPOLY_OFF_CTX is not a multiple of alignof(ChachaPolyCtx) - CHACHAPOLY_CTX() would return a misaligned "
    "pointer; pad the region ahead of it");

// The regions, at their offsets in the caller's borrow.
#define CHACHAPOLY_CTX(w) ((ChachaPolyCtx *)(void *)((w) + CHACHAPOLY_OFF_CTX))
#define CHACHAPOLY_CHACHA(w) ((w) + CHACHAPOLY_OFF_CHACHA)
#define CHACHAPOLY_POLY(w) ((w) + CHACHAPOLY_OFF_POLY)

// One keystream run through the Chacha20 namespace.
static void cp_chacha(uint8_t *restrict work, const uint8_t *key, const uint8_t iv[8], uint64_t counter,
                      const uint8_t *in, uint8_t *out, size_t len)
{
    Chacha20V.xor_args.key = key;
    Chacha20V.xor_args.iv = iv;
    Chacha20V.xor_args.counter = counter;
    Chacha20V.xor_args.in = in;
    Chacha20V.xor_args.out = out;
    Chacha20V.xor_args.len = len;
    Chacha20.xor_(CHACHAPOLY_CHACHA(work));
}

// One tag through the Poly1305 namespace.
static void cp_poly(uint8_t *restrict work, const uint8_t *poly_key, const uint8_t *msg, size_t len, uint8_t *out)
{
    Poly1305V.mac_args.key = poly_key;
    Poly1305V.mac_args.msg = msg;
    Poly1305V.mac_args.len = len;
    Poly1305V.mac_args.out = out;
    Poly1305.mac(CHACHAPOLY_POLY(work));
}

// The 8-byte ChaCha nonce is the sequence number as a big-endian uint64 (POKE_U64 in OpenSSH); a
// 32-bit SSH seqnr leaves the high 4 bytes zero.
static void seq_nonce(uint32_t seqnr, uint8_t iv[8])
{
    iv[0] = 0;
    iv[1] = 0;
    iv[2] = 0;
    iv[3] = 0;
    iv[4] = (uint8_t)(seqnr >> 24);
    iv[5] = (uint8_t)(seqnr >> 16);
    iv[6] = (uint8_t)(seqnr >> 8);
    iv[7] = (uint8_t)seqnr;
}

// --- the entries -----------------------------------------------------------

void protocore_chacha_poly_get_length(uint8_t *restrict work)
{
    ChachaPolyV.ok = PROTO_FALSE;
    if (!ChachaPolyV.length_args.key || !ChachaPolyV.length_args.enc_len)
    {
        return;
    }
    ChachaPolyCtx *ctx = CHACHAPOLY_CTX(work);
    const uint8_t *key = ChachaPolyV.length_args.key;
    seq_nonce(ChachaPolyV.length_args.seqnr, ctx->iv);
    // header key, counter 0
    cp_chacha(work, key + 32, ctx->iv, 0, ChachaPolyV.length_args.enc_len, ctx->len, 4);
    ChachaPolyV.length =
        ((uint32_t)ctx->len[0] << 24) | ((uint32_t)ctx->len[1] << 16) | ((uint32_t)ctx->len[2] << 8) | ctx->len[3];
    ChachaPolyV.ok = PROTO_TRUE;
}

void protocore_chacha_poly_encrypt(uint8_t *restrict work)
{
    ChachaPolyV.ok = PROTO_FALSE;
    if (!ChachaPolyV.encrypt_args.key || !ChachaPolyV.encrypt_args.src || !ChachaPolyV.encrypt_args.dest)
    {
        return;
    }
    ChachaPolyCtx *ctx = CHACHAPOLY_CTX(work);
    const uint8_t *key = ChachaPolyV.encrypt_args.key;
    const uint8_t *src = ChachaPolyV.encrypt_args.src;
    uint8_t *dest = ChachaPolyV.encrypt_args.dest;
    const uint32_t payload_len = ChachaPolyV.encrypt_args.payload_len;
    seq_nonce(ChachaPolyV.encrypt_args.seqnr, ctx->iv);
    cp_chacha(work, key, ctx->iv, 0, NULL, ctx->poly_key, 32);        // Poly1305 key = K_main block 0
    cp_chacha(work, key + 32, ctx->iv, 0, src, dest, 4);              // length field: K_header, counter 0
    cp_chacha(work, key, ctx->iv, 1, src + 4, dest + 4, payload_len); // payload: K_main, counter 1
    cp_poly(work, ctx->poly_key, dest, 4 + payload_len, dest + 4 + payload_len);
    ChachaPolyV.ok = PROTO_TRUE;
}

void protocore_chacha_poly_decrypt(uint8_t *restrict work)
{
    ChachaPolyV.ok = PROTO_FALSE;
    if (!ChachaPolyV.decrypt_args.key || !ChachaPolyV.decrypt_args.src || !ChachaPolyV.decrypt_args.dest)
    {
        return;
    }
    ChachaPolyCtx *ctx = CHACHAPOLY_CTX(work);
    const uint8_t *key = ChachaPolyV.decrypt_args.key;
    const uint8_t *src = ChachaPolyV.decrypt_args.src;
    uint8_t *dest = ChachaPolyV.decrypt_args.dest;
    const uint32_t payload_len = ChachaPolyV.decrypt_args.payload_len;
    seq_nonce(ChachaPolyV.decrypt_args.seqnr, ctx->iv);
    cp_chacha(work, key, ctx->iv, 0, NULL, ctx->poly_key, 32);
    cp_poly(work, ctx->poly_key, src, 4 + payload_len, ctx->tag); // MAC over the ciphertext (length || payload)
    if (!protocore_ct_eq(ctx->tag, src + 4 + payload_len, 16))
    {
        return; // authentication failed - produce no plaintext
    }
    cp_chacha(work, key + 32, ctx->iv, 0, src, dest, 4);
    cp_chacha(work, key, ctx->iv, 1, src + 4, dest + 4, payload_len);
    ChachaPolyV.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
ChachaPolyVars ChachaPolyV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CHACHAPOLY
