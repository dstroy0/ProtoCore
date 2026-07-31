// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file chachapoly.cpp
 * @brief chacha20-poly1305@openssh.com - implementation. See pc_chachapoly.h.
 */

#include "crypto/aead/chachapoly.h"
#include "crypto/cipher/chacha20.h"
#include "crypto/crypto_opt.h"
#include "crypto/crypto_scratch.h" // pc_ct_eq
#include "crypto/mac/poly1305.h"
#include "server/mmgr/secure.h" // the secure pool: AEAD working state, wiped on release
#include <string.h>
PC_CRYPTO_HOT

namespace
{
// The 8-byte ChaCha nonce is the sequence number as a big-endian uint64 (POKE_U64 in OpenSSH); a
// 32-bit SSH seqnr leaves the high 4 bytes zero.
void seq_nonce(uint32_t seqnr, uint8_t iv[8])
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

// chacha20-poly1305 per-op working set (nonce, the derived one-time Poly1305 key, the computed tag, and the
// decrypted length word) in the shared crypto scratch at the base span - this is a top-level op, so it shares
// the base with the other one-at-a-time top-level ops; its nested chacha20/poly1305 use their own regions
// above the base. Wiped per op so the Poly1305 key never lingers.
struct ChachapolyWork
{
    uint8_t iv[8];
    uint8_t poly_key[32];
    uint8_t tag[16];
    uint8_t len[4];
};
static_assert(sizeof(ChachapolyWork) <= PC_WORK_CHACHAPOLY,
              "ChachapolyWork outgrew PC_WORK_CHACHAPOLY - raise it in protocore_config.h, which derives "
              "PC_SECURE_ARENA_SIZE from it");

} // namespace

uint32_t pc_chachapoly_get_length(const uint8_t key[PC_CHACHAPOLY_KEY_LEN], uint32_t seqnr,
                                  const uint8_t enc_len[PC_CHACHAPOLY_AAD_LEN])
{
    SecureBorrow ws_b(sizeof(ChachapolyWork), alignof(ChachapolyWork));
    const pc_span &ws = ws_b.span();
    if (!pc_span_ok(ws))
    {
        return 0; // pool exhausted: a zero length is rejected by every caller
    }
    ChachapolyWork *w = reinterpret_cast<ChachapolyWork *>(ws.buf);
    seq_nonce(seqnr, w->iv);
    pc_chacha20_xor(key + 32, w->iv, 0, enc_len, w->len, 4); // header key, counter 0
    uint32_t n = ((uint32_t)w->len[0] << 24) | ((uint32_t)w->len[1] << 16) | ((uint32_t)w->len[2] << 8) | w->len[3];
    return n;
}

void pc_chachapoly_encrypt(const uint8_t key[PC_CHACHAPOLY_KEY_LEN], uint32_t seqnr, uint8_t *dest, const uint8_t *src,
                           uint32_t payload_len)
{
    SecureBorrow ws_b(sizeof(ChachapolyWork), alignof(ChachapolyWork));
    const pc_span &ws = ws_b.span();
    if (!pc_span_ok(ws))
    {
        return; // pool exhausted: emit nothing rather than an unauthenticated frame
    }
    ChachapolyWork *w = reinterpret_cast<ChachapolyWork *>(ws.buf);
    seq_nonce(seqnr, w->iv);
    pc_chacha20_xor(key, w->iv, 0, nullptr, w->poly_key, 32);       // Poly1305 key = K_main block 0
    pc_chacha20_xor(key + 32, w->iv, 0, src, dest, 4);              // length field: K_header, counter 0
    pc_chacha20_xor(key, w->iv, 1, src + 4, dest + 4, payload_len); // payload: K_main, counter 1
    pc_poly1305(dest + 4 + payload_len, dest, 4 + payload_len, w->poly_key);
}

bool pc_chachapoly_decrypt(const uint8_t key[PC_CHACHAPOLY_KEY_LEN], uint32_t seqnr, uint8_t *dest, const uint8_t *src,
                           uint32_t payload_len)
{
    SecureBorrow ws_b(sizeof(ChachapolyWork), alignof(ChachapolyWork));
    const pc_span &ws = ws_b.span();
    if (!pc_span_ok(ws))
    {
        return false; // pool exhausted: fail closed
    }
    ChachapolyWork *w = reinterpret_cast<ChachapolyWork *>(ws.buf);
    seq_nonce(seqnr, w->iv);
    pc_chacha20_xor(key, w->iv, 0, nullptr, w->poly_key, 32);
    pc_poly1305(w->tag, src, 4 + payload_len, w->poly_key); // MAC over the ciphertext (length || payload)
    if (!pc_ct_eq(w->tag, src + 4 + payload_len, 16))
    {
        return false; // authentication failed - produce no plaintext
    }
    pc_chacha20_xor(key + 32, w->iv, 0, src, dest, 4);
    pc_chacha20_xor(key, w->iv, 1, src + 4, dest + 4, payload_len);
    return true;
}
