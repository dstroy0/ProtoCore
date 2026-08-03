// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_dtls_record.cpp
 * @brief DTLS 1.3 record layer (RFC 9147 §4). See pc_dtls_record.h.
 */

#include "network_drivers/presentation/security/dtls/dtls_record.h"

#if PC_ENABLE_DTLS

#include "crypto/aead/aes128gcm.h"
#include "mmgr/secure.h" // the secure pool: header-protection key schedule
#include "network_drivers/tls/tls13_kdf.h"
#include <string.h>

// Unified-header first-byte fixed pattern and flag bits (RFC 9147 §4, Figure 3): 0 0 1 C S L E E.
static const uint8_t DTLS_UH_FIXED = 0x20; // 001x xxxx
static const uint8_t DTLS_UH_FIXED_MASK = 0xE0;
static const uint8_t DTLS_UH_CID = 0x10;    // C: connection id present
static const uint8_t DTLS_UH_SEQ16 = 0x08;  // S: 16-bit (vs 8-bit) sequence number
static const uint8_t DTLS_UH_LENGTH = 0x04; // L: length present
static const uint8_t DTLS_UH_EPOCH_MASK = 0x03;

// Build the AEAD nonce: the 64-bit sequence number, right-aligned in the 12-byte IV, XOR the IV
// (RFC 9147 §4.2.2 / RFC 8446 §5.3; the epoch is NOT mixed in). Same construction as QUIC.
static void build_nonce(const uint8_t iv[12], uint64_t seq, uint8_t nonce[12])
{
    memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++)
    {
        nonce[11 - i] ^= (uint8_t)(seq >> (8 * i));
    }
}

// Reconstruct the full sequence number from its truncated on-wire bits (RFC 9147 §4.2.2, using the
// RFC 9000 Appendix A.3 packet-number decoding: the candidate closest to `expected`).
static uint64_t seq_decode(uint64_t expected, uint64_t truncated, unsigned bits)
{
    // GCOVR_EXCL_START  unreachable: seq_decode's only caller (below) always passes bits = seq_len * 8
    // where seq_len is 1 or 2 (the unified header's S bit selects an 8- or 16-bit sequence number), so
    // bits is always 8 or 16 - never 0 and never >= 64. Kept as a defensive guard against a future caller.
    if (bits == 0 || bits >= 64)
    {
        return truncated;
    }
    // GCOVR_EXCL_STOP
    uint64_t win = (uint64_t)1 << bits;
    uint64_t hwin = win >> 1;
    uint64_t mask = win - 1;
    uint64_t candidate = (expected & ~mask) | (truncated & mask);
    if (candidate + hwin <= expected && candidate + win > candidate)
    {
        return candidate + win;
    }
    if (candidate > expected + hwin && candidate >= win)
    {
        return candidate - win;
    }
    return candidate;
}

void pc_dtls_record_keys_derive(DtlsRecordKeys *out, DtlsCipher cipher, uint16_t epoch, const uint8_t secret[32])
{
    out->cipher = cipher;
    out->epoch = epoch;
    // AEAD_AES_128_GCM: 16-byte key, 12-byte IV, 16-byte sequence-number key. The DTLS 1.3 variant
    // carries the "dtls13" HKDF-Expand-Label prefix (RFC 9147 §4.2.3 / §5.9).
    // The key becomes a keyed context here and the raw bytes are wiped; nothing downstream wants them.
    // The pool cannot come up short here: PC_SECURE_ARENA_SIZE is the SUM of every PC_WORK_*, a
    // strict upper bound rather than a deepest-nest estimate, so a borrow inside that budget always
    // succeeds. The borrow also wipes on release, on every exit path.
    size_t mark = pc_secure_mark();
    uint8_t *k = pc_secure_span(PC_AES128GCM_KEY_LEN, 8).buf;
    pc_tls13_kdf_expand_label(&DTLS13_KDF, secret, "key", k, PC_AES128GCM_KEY_LEN);
    pc_aes128gcm_key_init(out->gcm, k);
    pc_tls13_kdf_expand_label(&DTLS13_KDF, secret, "iv", out->iv, sizeof(out->iv));
    uint8_t *snk = pc_secure_span(PC_AES128GCM_KEY_LEN, 8).buf;
    pc_tls13_kdf_expand_label(&DTLS13_KDF, secret, "sn", snk, PC_AES128GCM_KEY_LEN);
    pc_aes128_init((pc_aes128 *)(out->sn_key), snk);
    pc_secure_release(mark);
}

// ---------------------------------------------------------------------------
// DTLSPlaintext
// ---------------------------------------------------------------------------

size_t pc_dtls_plaintext_build(uint8_t content_type, uint16_t epoch, uint64_t seq, const uint8_t *fragment,
                               size_t frag_len, uint8_t *out, size_t out_cap)
{
    size_t total = PC_DTLS_PLAINTEXT_HDR_LEN + frag_len;
    if (total > out_cap || frag_len > 0xFFFF)
    {
        return 0;
    }
    out[0] = content_type;
    out[1] = (uint8_t)(PC_DTLS_LEGACY_VERSION >> 8);
    out[2] = (uint8_t)PC_DTLS_LEGACY_VERSION;
    out[3] = (uint8_t)(epoch >> 8);
    out[4] = (uint8_t)epoch;
    out[5] = (uint8_t)(seq >> 40); // 48-bit sequence number, big-endian
    out[6] = (uint8_t)(seq >> 32);
    out[7] = (uint8_t)(seq >> 24);
    out[8] = (uint8_t)(seq >> 16);
    out[9] = (uint8_t)(seq >> 8);
    out[10] = (uint8_t)seq;
    out[11] = (uint8_t)(frag_len >> 8);
    out[12] = (uint8_t)frag_len;
    if (frag_len)
    {
        memcpy(out + PC_DTLS_PLAINTEXT_HDR_LEN, fragment, frag_len);
    }
    return total;
}

size_t pc_dtls_plaintext_parse(const uint8_t *rec, size_t rec_len, DtlsPlaintext *out)
{
    if (rec_len < PC_DTLS_PLAINTEXT_HDR_LEN)
    {
        return 0;
    }
    if (rec[1] != (uint8_t)(PC_DTLS_LEGACY_VERSION >> 8) || rec[2] != (uint8_t)PC_DTLS_LEGACY_VERSION)
    {
        return 0; // wrong legacy_version
    }
    out->content_type = rec[0];
    out->epoch = (uint16_t)(((uint16_t)rec[3] << 8) | rec[4]);
    out->seq = ((uint64_t)rec[5] << 40) | ((uint64_t)rec[6] << 32) | ((uint64_t)rec[7] << 24) |
               ((uint64_t)rec[8] << 16) | ((uint64_t)rec[9] << 8) | (uint64_t)rec[10];
    size_t length = ((size_t)rec[11] << 8) | rec[12];
    if (PC_DTLS_PLAINTEXT_HDR_LEN + length > rec_len)
    {
        return 0;
    }
    out->fragment = rec + PC_DTLS_PLAINTEXT_HDR_LEN;
    out->frag_len = length;
    return PC_DTLS_PLAINTEXT_HDR_LEN + length;
}

// ---------------------------------------------------------------------------
// DTLSCiphertext
// ---------------------------------------------------------------------------

size_t pc_dtls_ciphertext_protect(DtlsRecordKeys &keys, uint64_t seq, uint8_t content_type, const uint8_t *plaintext,
                                  size_t pt_len, uint8_t *out, size_t out_cap, const uint8_t *cid, size_t cid_len)
{
    if (keys.cipher != DTLS_CIPHER_AES_128_GCM_SHA256)
    {
        return 0;
    }
    if (cid_len > PC_DTLS_CID_MAX || (cid_len && !cid))
    {
        return 0;
    }
    // Unified header: [C] connection id, S=1 (16-bit seq), L=1 (length). hdr = byte0 || [cid] || seq16 ||
    // length16. The CID (RFC 9146 / RFC 9147 §9) sits between the first byte and the sequence number.
    const size_t hdr_len = 1 + cid_len + 2 + 2;
    size_t inner_len = pt_len + 1;                // DTLSInnerPlaintext = plaintext || content_type
    size_t enc_len = inner_len + PC_DTLS_TAG_LEN; // AEAD ciphertext || tag
    size_t total = hdr_len + enc_len;
    if (total > out_cap)
    {
        return 0;
    }

    uint8_t flags = (uint8_t)(DTLS_UH_FIXED | DTLS_UH_SEQ16 | DTLS_UH_LENGTH | (keys.epoch & DTLS_UH_EPOCH_MASK));
    if (cid_len)
    {
        flags |= DTLS_UH_CID;
    }
    out[0] = flags;
    if (cid_len)
    {
        memcpy(out + 1, cid, cid_len);
    }
    size_t seq_off = 1 + cid_len;
    out[seq_off] = (uint8_t)(seq >> 8); // plaintext sequence number (this header form is the AEAD AAD)
    out[seq_off + 1] = (uint8_t)seq;
    out[seq_off + 2] = (uint8_t)(enc_len >> 8);
    out[seq_off + 3] = (uint8_t)enc_len;

    // Assemble the inner plaintext where it will be sealed (seal permits out == pt).
    memcpy(out + hdr_len, plaintext, pt_len);
    out[hdr_len + pt_len] = content_type;

    uint8_t nonce[12];
    build_nonce(keys.iv, seq, nonce);
    // AAD = the whole unified header (including any connection id) carrying the plaintext sequence
    // number (before §4.2.3 encryption).
    pc_aes128gcm_seal((pc_aes128gcm_key *)(keys.gcm), nonce, out, hdr_len, out + hdr_len, inner_len, out + hdr_len,
                      out + hdr_len + inner_len);

    // Encrypt the sequence number (RFC 9147 §4.2.3): mask = AES-ECB(sn_key, ciphertext[0..15]).
    // enc_len = inner_len + 16 >= 17, so the 16-byte sample is always available.
    // The sequence-number context is already keyed and lives in the key material; rebuilding it here
    // costs ~556 cycles per record plus a pool borrow and wipe, independent of record size.
    uint8_t mask[16];
    pc_aes128_encrypt_block((pc_aes128 *)(keys.sn_key), out + hdr_len, mask);
    out[seq_off] ^= mask[0];
    out[seq_off + 1] ^= mask[1];
    return total;
}

proto_bool pc_dtls_ciphertext_unprotect(DtlsRecordKeys &keys, uint64_t next_seq, const uint8_t *rec, size_t rec_len,
                                        uint8_t *out, size_t out_cap, DtlsCiphertext *info, const uint8_t *expected_cid,
                                        size_t expected_cid_len)
{
    if (keys.cipher != DTLS_CIPHER_AES_128_GCM_SHA256 || rec_len < 1)
    {
        return PROTO_FALSE;
    }
    if (expected_cid_len > PC_DTLS_CID_MAX)
    {
        return PROTO_FALSE;
    }
    uint8_t b0 = rec[0];
    if ((b0 & DTLS_UH_FIXED_MASK) != DTLS_UH_FIXED)
    {
        return PROTO_FALSE; // top 3 bits must be 001
    }
    if ((b0 & DTLS_UH_EPOCH_MASK) != (keys.epoch & DTLS_UH_EPOCH_MASK))
    {
        return PROTO_FALSE; // wrong epoch keys for this record
    }

    size_t off = 1;
    if (b0 & DTLS_UH_CID)
    {
        // A connection-id record: a CID must have been negotiated for this direction, and it must be
        // ours (the CID is not length-prefixed on the wire - its length is known only from negotiation).
        if (expected_cid_len == 0 || off + expected_cid_len > rec_len ||
            memcmp(rec + off, expected_cid, expected_cid_len) != 0)
        {
            return PROTO_FALSE;
        }
        off += expected_cid_len;
    }
    else if (expected_cid_len != 0)
    {
        return PROTO_FALSE; // a CID was negotiated for this direction but the record carries none
    }

    size_t seq_len = (b0 & DTLS_UH_SEQ16) ? 2 : 1;
    if (off + seq_len > rec_len)
    {
        return PROTO_FALSE;
    }
    size_t seq_off = off;
    off += seq_len;

    size_t enc_len;
    if (b0 & DTLS_UH_LENGTH)
    {
        if (off + 2 > rec_len)
        {
            return PROTO_FALSE;
        }
        enc_len = ((size_t)rec[off] << 8) | rec[off + 1];
        off += 2;
    }
    else
    {
        enc_len = rec_len - off; // to end of datagram
    }
    if (off + enc_len > rec_len || enc_len < 16 || enc_len < PC_DTLS_TAG_LEN + 1)
    {
        return PROTO_FALSE; // need >= 16 bytes for the SN sample and >= tag + one inner byte
    }

    const uint8_t *enc = rec + off;
    size_t hdr_len = off; // unified header length (including any connection id)

    // Copy the header so we can write the decrypted sequence number into the AEAD AAD form.
    uint8_t hdr[1 + PC_DTLS_CID_MAX + 4];
    memcpy(hdr, rec, hdr_len);

    // Decrypt the sequence number (RFC 9147 §4.2.3).
    // The sequence-number context is already keyed and lives in the key material; rebuilding it here
    // costs ~556 cycles per record plus a pool borrow and wipe, independent of record size.
    uint8_t mask[16];
    pc_aes128_encrypt_block((pc_aes128 *)(keys.sn_key), enc, mask);
    uint64_t trunc = 0;
    for (size_t i = 0; i < seq_len; i++)
    {
        hdr[seq_off + i] ^= mask[i];
        trunc = (trunc << 8) | hdr[seq_off + i];
    }
    uint64_t full_seq = seq_decode(next_seq, trunc, (unsigned)(seq_len * 8));

    // Reject a record too short to hold a tag BEFORE subtracting. enc_len comes off the wire, and the
    // detached-tag api no longer range-checks a combined length on the caller's behalf, so this would
    // otherwise wrap to a huge size_t. The `> out_cap` test below happens to catch the wrapped value,
    // but that is an accident of a downstream comparison, not a check.
    if (enc_len < PC_DTLS_TAG_LEN)
    {
        return PROTO_FALSE;
    }
    size_t inner_len = enc_len - PC_DTLS_TAG_LEN; // == the ciphertext length the AEAD wants
    if (inner_len > out_cap)
    {
        return PROTO_FALSE;
    }

    uint8_t nonce[12];
    build_nonce(keys.iv, full_seq, nonce);
    const size_t pt_len = inner_len;
    if (!pc_aes128gcm_open((pc_aes128gcm_key *)(keys.gcm), nonce, hdr, hdr_len, enc, pt_len, enc + pt_len, out))
    {
        return PROTO_FALSE;
    }

    // Strip zero padding: the last non-zero byte of the inner plaintext is the content type (RFC 8446 §5.2).
    size_t n = inner_len;
    while (n > 0 && out[n - 1] == 0)
    {
        n--;
    }
    if (n == 0)
    {
        return PROTO_FALSE; // no content type -> invalid record
    }
    info->content_type = out[n - 1];
    info->pt_len = n - 1;
    info->seq = full_seq;
    info->epoch = keys.epoch;
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Anti-replay sliding window (RFC 9147 §4.5.1)
// ---------------------------------------------------------------------------

void pc_dtls_replay_init(DtlsReplayWindow *w)
{
    w->highest = 0;
    w->bitmap = 0;
    w->seeded = PROTO_FALSE;
}

proto_bool pc_dtls_replay_check(const DtlsReplayWindow *w, uint64_t seq)
{
    if (!w->seeded || seq > w->highest)
    {
        return PROTO_TRUE; // first record, or ahead of the window
    }
    uint64_t diff = w->highest - seq;
    if (diff >= 64)
    {
        return PROTO_FALSE; // older than the window
    }
    return ((w->bitmap >> diff) & 1u) == 0; // set bit => already seen (replay)
}

void pc_dtls_replay_mark(DtlsReplayWindow *w, uint64_t seq)
{
    if (!w->seeded)
    {
        w->seeded = PROTO_TRUE;
        w->highest = seq;
        w->bitmap = 1; // bit 0 = highest
        return;
    }
    if (seq > w->highest)
    {
        uint64_t shift = seq - w->highest;
        w->bitmap = (shift >= 64) ? 1u : ((w->bitmap << shift) | 1u);
        w->highest = seq;
        return;
    }
    uint64_t diff = w->highest - seq;
    if (diff < 64)
    {
        w->bitmap |= ((uint64_t)1 << diff);
    }
}

#endif // PC_ENABLE_DTLS
