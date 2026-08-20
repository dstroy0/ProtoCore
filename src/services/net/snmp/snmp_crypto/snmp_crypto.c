// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file snmp_crypto.c
 * @brief The USM transforms - implementation. See snmp_crypto.h.
 */

#include "services/net/snmp/snmp_crypto/snmp_crypto.h"
#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"
#include "mmgr/secure/secure.h"

#if PROTOCORE_ENABLE_SNMP_V3

#include "crypto/cipher/aes_sbox.h"
#include "crypto/hash/sha256/sha256.h"

// The scan for the password's end stops here, so a password that carries no terminator cannot be
// read past. The derivation repeats whatever it finds, so a shorter password is not rejected.
#define PROTOCORE_SNMP_USM_PASS_MAX 256

// The password-to-key input length: RFC 7860 sec 9.3 repeats the password to 1,048,576 octets.
#define PROTOCORE_SNMP_USM_EXPAND_LEN 1048576u

// ---------------------------------------------------------------------------
// Key localization (RFC 3414 sec 2.6, derivation per RFC 7860 sec 9.3)
// ---------------------------------------------------------------------------

// Ku = H( password repeated to 1,048,576 octets ), then Kul = H( Ku || snmpEngineID || Ku ), with
// H = SHA-256. An empty password yields an all-zero key and reports false.
static void localize_key(uint8_t *restrict work)
{
    (void)work;
    const char *password = SnmpCryptoV.key.password;
    uint8_t *key_out = SnmpCryptoV.key.out;
    size_t pwlen = password ? str.len(password, PROTOCORE_SNMP_USM_PASS_MAX) : 0;
    if (pwlen == 0 || key_out == NULL)
    {
        if (key_out != NULL)
        {
            mem.set(key_out, 0, SNMP_USM_KEY_LEN);
        }
        SnmpCryptoV.ok = PROTO_FALSE;
        return;
    }

    uint8_t *sha;
    sha = SnmpCryptoV.work;
    Sha256.init(sha);
    uint8_t block[64];
    size_t pw_index = 0;
    uint32_t count = 0;
    while (count < PROTOCORE_SNMP_USM_EXPAND_LEN)
    {
        for (int i = 0; i < 64; i++)
        {
            block[i] = (uint8_t)password[pw_index];
            pw_index = (pw_index + 1) % pwlen;
        }
        Sha256V.update_args.data = block;
        Sha256V.update_args.len = 64;
        Sha256.update(sha);
        count += 64;
    }
    uint8_t ku[SNMP_USM_KEY_LEN];
    Sha256V.final_args.out = ku;
    Sha256.final(sha);

    sha = SnmpCryptoV.work;
    Sha256.init(sha);
    Sha256V.update_args.data = ku;
    Sha256V.update_args.len = SNMP_USM_KEY_LEN;
    Sha256.update(sha);
    Sha256V.update_args.data = SnmpCryptoV.key.engine_id;
    Sha256V.update_args.len = SnmpCryptoV.key.engine_id_len;
    Sha256.update(sha);
    Sha256V.update_args.data = ku;
    Sha256V.update_args.len = SNMP_USM_KEY_LEN;
    Sha256.update(sha);
    Sha256V.final_args.out = key_out;
    Sha256.final(sha);

    protocore_secure_wipe(ku, sizeof(ku));
    protocore_secure_wipe(block, sizeof(block));
    SnmpCryptoV.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// AES-128 block cipher (FIPS 197) and CFB128 mode (NIST SP 800-38A), the
// usmAesCfb128Protocol transform of RFC 3826. Both helpers are pure over their
// arguments and touch no module state.
// ---------------------------------------------------------------------------

static const uint8_t kRcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

// Expand a 128-bit key into 11 round keys (44 words).
static void aes128_key_schedule(const uint8_t key[16], uint8_t rk[176])
{
    mem.cpy(rk, key, 16);
    for (int i = 4; i < 44; i++)
    {
        uint8_t t[4];
        mem.cpy(t, rk + (i - 1) * 4, 4);
        if (i % 4 == 0)
        {
            uint8_t tmp = t[0]; // RotWord
            t[0] = PROTOCORE_AES_SBOX[t[1]];
            t[1] = PROTOCORE_AES_SBOX[t[2]];
            t[2] = PROTOCORE_AES_SBOX[t[3]];
            t[3] = PROTOCORE_AES_SBOX[tmp];
            t[0] ^= kRcon[i / 4 - 1];
        }
        for (int j = 0; j < 4; j++)
        {
            rk[i * 4 + j] = rk[(i - 4) * 4 + j] ^ t[j];
        }
    }
}

// Multiply by x in GF(2^8), reducing by 0x11b.
static uint8_t xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b));
}

static void aes128_encrypt_block(const uint8_t rk[176], const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16];
    for (int i = 0; i < 16; i++)
    {
        s[i] = in[i] ^ rk[i];
    }

    for (int round = 1; round <= 10; round++)
    {
        // SubBytes
        for (int i = 0; i < 16; i++)
        {
            s[i] = PROTOCORE_AES_SBOX[s[i]];
        }

        // ShiftRows (state is column-major: s[r + 4c])
        uint8_t t[16];
        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                t[r + 4 * c] = s[r + 4 * ((c + r) % 4)];
            }
        }
        mem.cpy(s, t, 16);

        // MixColumns, skipped in the final round
        if (round != 10)
        {
            for (int c = 0; c < 4; c++)
            {
                uint8_t *col = s + 4 * c;
                uint8_t a0 = col[0];
                uint8_t a1 = col[1];
                uint8_t a2 = col[2];
                uint8_t a3 = col[3];
                col[0] = (uint8_t)(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
                col[1] = (uint8_t)(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
                col[2] = (uint8_t)(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
                col[3] = (uint8_t)((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
            }
        }

        // AddRoundKey
        for (int i = 0; i < 16; i++)
        {
            s[i] ^= rk[round * 16 + i];
        }
    }
    mem.cpy(out, s, 16);
}

// CFB128: each block of input is XORed with the cipher applied to the feedback register, and the
// ciphertext block becomes the next feedback. A trailing partial block takes as many keystream
// octets as it has and ends the walk.
static void aes_cfb128(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *in = SnmpCryptoV.priv.in;
    uint8_t *out = SnmpCryptoV.priv.out;
    const size_t len = SnmpCryptoV.priv.len;
    const proto_bool encrypt = SnmpCryptoV.priv.encrypt;
    if (SnmpCryptoV.priv.key == NULL || SnmpCryptoV.priv.iv == NULL || in == NULL || out == NULL)
    {
        SnmpCryptoV.ok = PROTO_FALSE;
        return;
    }

    uint8_t rk[176];
    aes128_key_schedule(SnmpCryptoV.priv.key, rk);
    uint8_t fb[16];
    mem.cpy(fb, SnmpCryptoV.priv.iv, 16);
    uint8_t ks[16];

    size_t off = 0;
    while (off < len)
    {
        aes128_encrypt_block(rk, fb, ks);
        size_t bl = len - off;
        if (bl > 16)
        {
            bl = 16;
        }
        // The ciphertext block is captured before the XOR writes over it, so out may be in.
        uint8_t cipher[16];
        if (encrypt)
        {
            for (size_t j = 0; j < bl; j++)
            {
                out[off + j] = in[off + j] ^ ks[j];
                cipher[j] = out[off + j];
            }
        }
        else
        {
            for (size_t j = 0; j < bl; j++)
            {
                cipher[j] = in[off + j];
                out[off + j] = in[off + j] ^ ks[j];
            }
        }
        if (bl == 16)
        {
            mem.cpy(fb, cipher, 16);
        }
        off += bl;
    }

    protocore_secure_wipe(rk, sizeof(rk));
    protocore_secure_wipe(ks, sizeof(ks));
    protocore_secure_wipe(fb, sizeof(fb));
    SnmpCryptoV.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
SnmpCryptoVars SnmpCryptoV;

#endif // PROTOCORE_ENABLE_SNMP_V3
