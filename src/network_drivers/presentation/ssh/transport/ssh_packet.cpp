// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_packet.cpp
 * @brief SSH binary packet framing, encryption, MAC, and receive reassembly.
 */

#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "crypto/aead/aesgcm.h"
#include "crypto/aead/chachapoly.h"
#include "crypto/mac/hmac_sha256.h"
#include "crypto/mac/hmac_sha512.h"
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"
#if PC_ENABLE_SSH_ZLIB
#include "network_drivers/presentation/ssh/transport/ssh_comp.h"
#include "network_drivers/presentation/ssh/transport/ssh_zlib.h" // ssh_deflate_bound
#endif
#include "server/mmgr/plaintext.h"
#include "server/mmgr/secure.h"
#include <string.h>

#ifdef ARDUINO
#include <Arduino.h> // esp_fill_random()
#else
#include <Arduino.h> // mock
#endif

// ---------------------------------------------------------------------------
// BSS allocation
// ---------------------------------------------------------------------------

SshPacketState ssh_pkt[MAX_SSH_CONNS];

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static inline uint32_t read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void write_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

// Compute the padding needed so that (5 + payload_len + padding) is a
// multiple of 16 (AES block size).  Minimum padding = 4 bytes (RFC 4253 §6).
static size_t compute_padding(size_t payload_len)
{
    size_t total = 5 + payload_len; // 4-byte length + 1-byte padding_len + payload
    size_t remainder = total % 16;
    size_t padding = (remainder == 0) ? 0 : (16 - remainder);
    if (padding < 4)
    {
        padding += 16;
    }
    return padding;
}

// Compute the MAC over 4-byte seq_no || buf using the HMAC named by mac_mode. For E&M the buf is
// the plaintext packet; for ETM it is the length field || ciphertext. Writes ssh_mac_len() bytes.
static void compute_mac_mode(uint8_t mac_mode, const uint8_t *mac_key, uint32_t seq_no, const uint8_t *buf,
                             size_t buf_len, uint8_t *mac_out)
{
    uint8_t seq_be[4];
    write_u32_be(seq_be, seq_no);
    if (mac_mode == SSH_MAC_HMAC_SHA512 || mac_mode == SSH_MAC_HMAC_SHA512_ETM)
    {
        pc_hmac_sha512_ctx ctx;
        pc_hmac_sha512_init(&ctx, mac_key, 64);
        pc_hmac_sha512_update(&ctx, seq_be, 4);
        pc_hmac_sha512_update(&ctx, buf, buf_len);
        pc_hmac_sha512_final(&ctx, mac_out);
    }
    else
    {
        pc_hmac_sha256_ctx ctx;
        pc_hmac_sha256_init(&ctx, mac_key, 32);
        pc_hmac_sha256_update(&ctx, seq_be, 4);
        pc_hmac_sha256_update(&ctx, buf, buf_len);
        pc_hmac_sha256_final(&ctx, mac_out);
    }
}

// Constant-time 32-byte comparison to prevent timing oracles on MAC verify.
static int ct_memcmp(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++)
    {
        diff |= a[i] ^ b[i];
    }
    return (int)diff;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void ssh_pkt_init(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    SshPacketState *s = &ssh_pkt[i];
    memset(s, 0, sizeof(*s)); // is_client defaults false = server role
    s->kex_active = true;
    s->enc_out = false;
    s->enc_in = false;
}

void ssh_pkt_set_client(uint8_t i)
{
    if (i < MAX_SSH_CONNS)
    {
        ssh_pkt[i].is_client = true;
    }
}

// ---------------------------------------------------------------------------
// Direction-aware key selection (RFC 4253 §7.2 names keys by direction, not role)
// ---------------------------------------------------------------------------
// A client sends c2s / receives s2c; a server is the mirror. Selecting the key set through these
// keeps one send and one receive implementation correct for both roles.

static inline const uint8_t *km_send_chacha(const SshKeyMat *km, bool cli)
{
    return cli ? km->chacha_key_c2s : km->chacha_key_s2c;
}
static inline const uint8_t *km_recv_chacha(const SshKeyMat *km, bool cli)
{
    return cli ? km->chacha_key_s2c : km->chacha_key_c2s;
}
static inline const uint8_t *km_send_aes_key(const SshKeyMat *km, bool cli)
{
    return cli ? km->aes_key_c2s : km->aes_key_s2c;
}
static inline uint8_t *km_send_aes_iv(SshKeyMat *km, bool cli)
{
    return cli ? km->aes_iv_c2s : km->aes_iv_s2c;
}
static inline const uint8_t *km_recv_aes_key(const SshKeyMat *km, bool cli)
{
    return cli ? km->aes_key_s2c : km->aes_key_c2s;
}
// GCM keeps a keyed context per direction rather than a raw key: the schedule is built once at install
// (ssh_dh.cpp) and reused per packet, because standing one up costs ~9,200 cycles regardless of packet
// size and would otherwise dominate small interactive traffic.
static inline pc_aesgcm_key *km_send_gcm(SshKeyMat *km, bool cli)
{
    return reinterpret_cast<pc_aesgcm_key *>(cli ? km->gcm_ctx_c2s : km->gcm_ctx_s2c);
}
static inline pc_aesgcm_key *km_recv_gcm(SshKeyMat *km, bool cli)
{
    return reinterpret_cast<pc_aesgcm_key *>(cli ? km->gcm_ctx_s2c : km->gcm_ctx_c2s);
}
static inline uint8_t *km_recv_aes_iv(SshKeyMat *km, bool cli)
{
    return cli ? km->aes_iv_s2c : km->aes_iv_c2s;
}
static inline const uint8_t *km_send_mac(const SshKeyMat *km, bool cli)
{
    return cli ? km->mac_key_c2s : km->mac_key_s2c;
}
static inline const uint8_t *km_recv_mac(const SshKeyMat *km, bool cli)
{
    return cli ? km->mac_key_s2c : km->mac_key_c2s;
}

// ---------------------------------------------------------------------------
// Send
// ---------------------------------------------------------------------------

int ssh_pkt_send(uint8_t i, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t *out_len, size_t out_cap)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshPacketState *s = &ssh_pkt[i];
    SshKeyMat *km = &ssh_keys[i];

    // Sequence overflow guard.
    if (s->seq_no_send >= SSH_SEQ_CLOSE_THRESHOLD)
    {
        return -1;
    }

#if PC_ENABLE_SSH_ZLIB
    // Compression (RFC 4253 §6.2) transforms the payload BEFORE padding/encryption, once the s2c
    // stream is active. The compressor is stateful (context takeover), so this call must be followed
    // by a full send - the same atomicity the stateful cipher below already requires. The wire buffer
    // is sized (SSH_WIRE_CAP) so the compressed payload can never overflow out_cap and desync.
    PlaintextScope comp_scope;
    if (ssh_comp_s2c_active(i))
    {
        size_t bound = ssh_deflate_bound(payload_len);
        uint8_t *cbuf = (uint8_t *)pc_plaintext_alloc(bound, 16);
        size_t clen = 0;
        if (!cbuf || ssh_comp_s2c(i, payload, payload_len, cbuf, bound, &clen) != 0)
        {
            return -1;
        }
        payload = cbuf;
        payload_len = clen;
    }
#endif

    // Padding block size and base differ by mode. chacha/gcm (AEAD) and aes-ETM exclude the 4-byte
    // length from the block-alignment (it is AAD / sent in clear); plain aes-E&M includes it.
    //   chacha    : block 8,  base = padding_length + payload
    //   aes GCM   : block 16, base = padding_length + payload   (RFC 5647 sec 7.3)
    //   aes ETM   : block 16, base = padding_length + payload
    //   aes E&M / plaintext : block 16, base = length + padding_length + payload  (compute_padding)
    bool chacha = s->enc_out && km->cipher_mode == SSH_CIPHER_CHACHA20POLY1305;
    bool gcm = s->enc_out && km->cipher_mode == SSH_CIPHER_AES256GCM;
    bool etm = s->enc_out && km->cipher_mode == SSH_CIPHER_AES256CTR && ssh_mac_is_etm(km->mac_mode);
    size_t pad_len;
    size_t tag_len;
    if (chacha)
    {
        size_t base = 1 + payload_len;
        pad_len = 8 - (base % 8);
        if (pad_len < 4)
        {
            pad_len += 8;
        }
        tag_len = PC_CHACHAPOLY_TAG_LEN;
    }
    else if (gcm)
    {
        size_t base = 1 + payload_len;
        pad_len = 16 - (base % 16);
        if (pad_len < 4)
        {
            pad_len += 16;
        }
        tag_len = PC_AESGCM_TAG_LEN;
    }
    else if (etm)
    {
        size_t base = 1 + payload_len;
        pad_len = 16 - (base % 16);
        if (pad_len < 4)
        {
            pad_len += 16;
        }
        tag_len = ssh_mac_len(km->mac_mode);
    }
    else
    {
        pad_len = compute_padding(payload_len);
        tag_len = s->enc_out ? ssh_mac_len(km->mac_mode) : 0;
    }
    size_t pkt_len = 1 + payload_len + pad_len; // padding_length + payload + padding
    size_t wire_len = 4 + pkt_len + tag_len;

    if (wire_len > out_cap)
    {
        return -1;
    }

    // Assemble the plaintext packet into out[].
    write_u32_be(out, (uint32_t)pkt_len);            // packet_length
    out[4] = (uint8_t)pad_len;                       // padding_length
    memcpy(out + 5, payload, payload_len);           // payload
    esp_fill_random(out + 5 + payload_len, pad_len); // random padding

    const bool cli = s->is_client; // send direction: client uses c2s, server uses s2c
    if (chacha)
    {
        // Encrypt length (header key) + payload (main key) and append the Poly1305 tag.
        pc_chachapoly_encrypt(km_send_chacha(km, cli), s->seq_no_send, out, out, (uint32_t)pkt_len);
    }
    else if (gcm)
    {
        // aes256-gcm@openssh.com: length stays in clear (it is the AAD); seal the packet body in
        // place and append the 16-byte GCM tag. The context's invocation counter advances by one.
        // Seal in place (tag appended after the ciphertext), then advance the RFC 5647 invocation counter.
        uint8_t *iv = km_send_aes_iv(km, cli);
        pc_aesgcm_seal(km_send_gcm(km, cli), iv, out, 4, out + 4, pkt_len, out + 4, out + 4 + pkt_len);
        pc_aesgcm_iv_increment(iv);
    }
    else if (etm)
    {
        // Encrypt-then-MAC: length stays in clear; encrypt the payload, then MAC over (length||ct).
        pc_aes256ctr_crypt(km_send_aes_key(km, cli), km_send_aes_iv(km, cli), out + 4, out + 4, pkt_len);
        compute_mac_mode(km->mac_mode, km_send_mac(km, cli), s->seq_no_send, out, 4 + pkt_len, out + 4 + pkt_len);
    }
    else if (s->enc_out)
    {
        // Encrypt-and-MAC: MAC over plaintext (seq || unencrypted packet), then AES-256-CTR.
        uint8_t mac[64];
        compute_mac_mode(km->mac_mode, km_send_mac(km, cli), s->seq_no_send, out, 4 + pkt_len, mac);
        pc_aes256ctr_crypt(km_send_aes_key(km, cli), km_send_aes_iv(km, cli), out, out, 4 + pkt_len);
        memcpy(out + 4 + pkt_len, mac, tag_len);
        pc_secure_wipe(mac, sizeof(mac));
    }

    *out_len = wire_len;
    s->seq_no_send++;
    return 0;
}

// ---------------------------------------------------------------------------
// Receive
// ---------------------------------------------------------------------------

// Dispatch one decrypted packet payload (message-type byte + data) to @p handler, first decompressing
// it when the client-to-server compression stream is active (RFC 4253 sec 6.2). Compression only runs
// after NEWKEYS / auth success, so the pre-auth plaintext path never enters the c2s branch.
// @return 0 on success (handler invoked, or skipped for a flush-only packet), -1 on a malformed
//         compressed stream / decompression overflow (the caller must wipe + disconnect).
static int ssh_dispatch_payload(uint8_t i, const uint8_t *payload, size_t payload_len, ssh_msg_handler_t handler)
{
#if PC_ENABLE_SSH_ZLIB
    if (ssh_comp_c2s_active(i))
    {
        PlaintextScope inflate_scope;
        uint8_t *dbuf = (uint8_t *)pc_plaintext_alloc(SSH_PKT_BUF_SIZE, 16);
        size_t dlen = 0;
        if (!dbuf || ssh_comp_c2s(i, payload, payload_len, dbuf, SSH_PKT_BUF_SIZE, &dlen) != 0)
        {
            return -1; // malformed stream, or a payload that decompresses beyond the uncompressed limit
        }
        if (dlen == 0)
        {
            return 0; // the packet carried only flush bits (no message); consume it and move on
        }
        handler(i, dbuf[0], dbuf, dlen);
        return 0;
    }
#endif
    handler(i, payload[0], payload, payload_len);
    return 0;
}

// Extract one packet from the head of the RX buffer and dispatch it to @p handler. Every cipher path
// returns the same tri-state so ssh_pkt_recv's extract loop can stay flat: 1 = one packet consumed (keep
// extracting), 0 = incomplete (need more bytes, stop), -1 = fatal (the buffer is already wiped on the paths
// that require it; the caller must disconnect). One function per cipher mode keeps each within the nesting
// and cognitive-complexity budget that the single inline switch blew past.

static int ssh_recv_chachapoly(uint8_t i, SshPacketState *s, const SshKeyMat *km, ssh_msg_handler_t handler)
{
    // chacha20-poly1305@openssh.com. Keyed by the sequence number, so decrypting the
    // length is stateless/repeatable - no cipher-state peek/restore is needed.
    const uint8_t *rk = km_recv_chacha(km, s->is_client); // recv: client s2c, server c2s
    uint32_t pkt_len = pc_chachapoly_get_length(rk, s->seq_no_recv, s->rx_buf);
    if (pkt_len < 1 || pkt_len > SSH_PKT_BUF_SIZE - 4 - PC_CHACHAPOLY_TAG_LEN)
    {
        pc_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }
    size_t wire_need = 4 + pkt_len + PC_CHACHAPOLY_TAG_LEN;
    if (s->rx_len < wire_need)
    {
        return 0; // incomplete packet
    }

    const size_t scratch_sz = 4 + pkt_len; // plaintext = length(4) || (pad_len||payload||pad)
    PlaintextScope scratch_scope;
    uint8_t *scratch = (uint8_t *)pc_plaintext_alloc(scratch_sz, 16);
    if (!scratch)
    {
        pc_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }

    // Verify the Poly1305 tag over the ciphertext, then decrypt. No plaintext on failure.
    if (!pc_chachapoly_decrypt(rk, s->seq_no_recv, scratch, s->rx_buf, pkt_len))
    {
        pc_secure_wipe(scratch, scratch_sz);
        pc_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1; // caller must close connection
    }

    if (s->seq_no_recv >= SSH_SEQ_CLOSE_THRESHOLD)
    {
        pc_secure_wipe(scratch, scratch_sz);
        return -1;
    }
    s->seq_no_recv++;

    uint8_t pad_len_byte = scratch[4];
    if (pad_len_byte < 4 || pad_len_byte >= pkt_len)
    {
        pc_secure_wipe(scratch, scratch_sz);
        return -1;
    }
    size_t payload_len = pkt_len - 1 - pad_len_byte;
    if (ssh_dispatch_payload(i, scratch + 5, payload_len, handler) < 0)
    {
        pc_secure_wipe(scratch, scratch_sz);
        return -1;
    }

    size_t consumed = wire_need;
    memmove(s->rx_buf, s->rx_buf + consumed, s->rx_len - consumed);
    s->rx_len -= consumed;
    pc_secure_wipe(scratch, scratch_sz);
    return 1;
}

static int ssh_recv_aesgcm(uint8_t i, SshPacketState *s, SshKeyMat *km, ssh_msg_handler_t handler)
{
    // aes256-gcm@openssh.com (RFC 5647): the 4-byte packet_length is sent in the clear and is
    // the AEAD's additional authenticated data; the 16-byte GCM tag is verified over
    // (length || ciphertext) BEFORE any plaintext is produced.
    uint32_t pkt_len = read_u32_be(s->rx_buf);
    // The encrypted portion (pkt_len) must be a positive whole number of AES blocks.
    if (pkt_len < 1 || pkt_len > SSH_PKT_BUF_SIZE - 4 - PC_AESGCM_TAG_LEN || (pkt_len % 16) != 0)
    {
        pc_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }
    size_t wire_need = 4 + pkt_len + PC_AESGCM_TAG_LEN;
    if (s->rx_len < wire_need)
    {
        return 0; // incomplete packet
    }

    const size_t scratch_sz = pkt_len; // plaintext = padding_length || payload || padding
    PlaintextScope scratch_scope;
    uint8_t *scratch = (uint8_t *)pc_plaintext_alloc(scratch_sz, 16);
    if (!scratch)
    {
        pc_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }

    // Verify the GCM tag over (length || ciphertext), then decrypt. No plaintext on failure.
    uint8_t *iv = km_recv_aes_iv(km, s->is_client);
    if (!pc_aesgcm_open(km_recv_gcm(km, s->is_client), iv, s->rx_buf, 4, s->rx_buf + 4, pkt_len,
                        s->rx_buf + 4 + pkt_len, scratch))
    {
        pc_secure_wipe(scratch, scratch_sz);
        pc_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1; // caller must close connection
    }
    pc_aesgcm_iv_increment(iv); // tag verified: advance the RFC 5647 invocation counter (recv success)

    if (s->seq_no_recv >= SSH_SEQ_CLOSE_THRESHOLD)
    {
        pc_secure_wipe(scratch, scratch_sz);
        return -1;
    }
    s->seq_no_recv++;

    uint8_t pad_len_byte = scratch[0];
    if (pad_len_byte < 4 || pad_len_byte >= pkt_len)
    {
        pc_secure_wipe(scratch, scratch_sz);
        return -1;
    }
    size_t payload_len = pkt_len - 1 - pad_len_byte;
    if (ssh_dispatch_payload(i, scratch + 1, payload_len, handler) < 0)
    {
        pc_secure_wipe(scratch, scratch_sz);
        return -1;
    }

    size_t consumed = wire_need;
    memmove(s->rx_buf, s->rx_buf + consumed, s->rx_len - consumed);
    s->rx_len -= consumed;
    pc_secure_wipe(scratch, scratch_sz);
    return 1;
}

static int ssh_recv_ctr_etm(uint8_t i, SshPacketState *s, SshKeyMat *km, ssh_msg_handler_t handler)
{
    // aes256-ctr + encrypt-then-MAC: the 4-byte packet_length is sent in the clear, and the
    // MAC is verified over (length || ciphertext) BEFORE anything is decrypted.
    uint32_t pkt_len = read_u32_be(s->rx_buf);
    size_t mac_tag = ssh_mac_len(km->mac_mode);
    // The encrypted portion (pkt_len) must be a positive whole number of AES blocks.
    if (pkt_len < 1 || pkt_len > SSH_PKT_BUF_SIZE - 4 || (pkt_len % 16) != 0)
    {
        pc_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }
    size_t wire_need = 4 + pkt_len + mac_tag;
    if (s->rx_len < wire_need)
    {
        return 0; // incomplete packet
    }

    uint8_t expected_mac[64];
    compute_mac_mode(km->mac_mode, km_recv_mac(km, s->is_client), s->seq_no_recv, s->rx_buf, 4 + pkt_len, expected_mac);
    if (ct_memcmp(expected_mac, s->rx_buf + 4 + pkt_len, mac_tag) != 0)
    {
        pc_secure_wipe(expected_mac, sizeof(expected_mac));
        pc_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1; // caller must close connection
    }
    pc_secure_wipe(expected_mac, sizeof(expected_mac));

    if (s->seq_no_recv >= SSH_SEQ_CLOSE_THRESHOLD)
    {
        return -1;
    }
    s->seq_no_recv++;

    // MAC verified -> decrypt the payload (advances c2s_ctx by exactly pkt_len/16 blocks).
    const size_t scratch_sz = SSH_PKT_BUF_SIZE;
    PlaintextScope scratch_scope;
    uint8_t *scratch = (uint8_t *)pc_plaintext_alloc(scratch_sz, 16);
    if (!scratch)
    {
        pc_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }
    memcpy(scratch, s->rx_buf + 4, pkt_len);
    pc_aes256ctr_crypt(km_recv_aes_key(km, s->is_client), km_recv_aes_iv(km, s->is_client), scratch, scratch, pkt_len);

    // scratch = padding_length || payload || padding.
    uint8_t pad_len_byte = scratch[0];
    if (pad_len_byte < 4 || pad_len_byte >= pkt_len)
    {
        pc_secure_wipe(scratch, scratch_sz);
        return -1;
    }
    size_t payload_len = pkt_len - 1 - pad_len_byte;
    if (ssh_dispatch_payload(i, scratch + 1, payload_len, handler) < 0)
    {
        pc_secure_wipe(scratch, scratch_sz);
        return -1;
    }

    size_t consumed = wire_need;
    memmove(s->rx_buf, s->rx_buf + consumed, s->rx_len - consumed);
    s->rx_len -= consumed;
    pc_secure_wipe(scratch, scratch_sz);
    return 1;
}

static int ssh_recv_ctr_emac(uint8_t i, SshPacketState *s, SshKeyMat *km, ssh_msg_handler_t handler)
{
    // aes256-ctr + encrypt-and-MAC. We need the first cipher block (16 bytes) for the length.
    if (s->rx_len < 16)
    {
        return 0; // wait for more data
    }

    // --- Peek packet_length WITHOUT advancing the cipher ---
    // Decrypt only the 4-byte length prefix against the current counter block; the counter is not advanced
    // and no cipher state touches the stack (all working memory stays in the shared crypto scratch).
    const uint8_t *rk = km_recv_aes_key(km, s->is_client); // recv: client s2c, server c2s
    uint8_t *rctr = km_recv_aes_iv(km, s->is_client);
    uint32_t pkt_len = pc_aes256ctr_get_length(rk, rctr, s->rx_buf);

    // Validate length.  The encrypted portion (4 + pkt_len) must be a
    // whole number of AES blocks (RFC 4253 §6 padding guarantees this).
    size_t enc_len = 4 + pkt_len;
    if (pkt_len < 1 || pkt_len > SSH_PKT_BUF_SIZE - 4 || (enc_len % 16) != 0)
    {
        pc_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }

    size_t mac_tag = ssh_mac_len(km->mac_mode);
    size_t wire_need = enc_len + mac_tag;
    if (s->rx_len < wire_need)
    {
        return 0; // incomplete packet; cipher state already restored
    }

    // Borrow this packet's plaintext scratch from the shared arena. The
    // scope guard reclaims it on every exit path, so multiple packets in
    // one call reuse the same space instead of accumulating; an exhausted
    // arena fails closed (discard + disconnect).
    const size_t scratch_sz = SSH_PKT_BUF_SIZE + 64;
    PlaintextScope scratch_scope;
    uint8_t *scratch = (uint8_t *)pc_plaintext_alloc(scratch_sz, 16);
    if (!scratch)
    {
        pc_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }

    // Full packet present.  Decrypt EXACTLY the encrypted portion,
    // which advances the recv counter by exactly enc_len/16 blocks and
    // leaves it aligned on the next packet boundary.
    memcpy(scratch, s->rx_buf, enc_len);
    pc_aes256ctr_crypt(rk, rctr, scratch, scratch, enc_len);

    // Verify MAC over seq_no || plaintext(scratch[0..enc_len)).
    const uint8_t *rx_mac = s->rx_buf + enc_len; // MAC is sent in clear
    uint8_t expected_mac[64];
    compute_mac_mode(km->mac_mode, km_recv_mac(km, s->is_client), s->seq_no_recv, scratch, enc_len, expected_mac);

    if (ct_memcmp(expected_mac, rx_mac, mac_tag) != 0)
    {
        // MAC failure: zero everything and disconnect.
        pc_secure_wipe(scratch, scratch_sz);
        pc_secure_wipe(expected_mac, sizeof(expected_mac));
        pc_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1; // caller must close connection
    }
    pc_secure_wipe(expected_mac, sizeof(expected_mac));

    // MAC verified.  Sequence overflow guard.
    if (s->seq_no_recv >= SSH_SEQ_CLOSE_THRESHOLD)
    {
        pc_secure_wipe(scratch, scratch_sz);
        return -1;
    }
    s->seq_no_recv++;

    // Extract payload: scratch[5 .. 5 + payload_len - 1]
    uint8_t pad_len_byte = scratch[4];
    // RFC 4253 6: there MUST be at least 4 bytes of padding, and it cannot
    // exceed the packet (which would underflow payload_len).
    if (pad_len_byte < 4 || pad_len_byte >= pkt_len)
    {
        pc_secure_wipe(scratch, scratch_sz);
        return -1;
    }
    size_t payload_len = pkt_len - 1 - pad_len_byte;
    if (ssh_dispatch_payload(i, scratch + 5, payload_len, handler) < 0)
    {
        pc_secure_wipe(scratch, scratch_sz);
        return -1;
    }

    // Consume from rx_buf.
    size_t consumed = wire_need;
    memmove(s->rx_buf, s->rx_buf + consumed, s->rx_len - consumed);
    s->rx_len -= consumed;
    pc_secure_wipe(scratch, scratch_sz);
    return 1;
}

static int ssh_recv_plain(uint8_t i, SshPacketState *s, const SshKeyMat *km, ssh_msg_handler_t handler)
{
    (void)km; // no keys before NEWKEYS
    // Unencrypted path (during initial handshake / before NEWKEYS).
    uint32_t pkt_len = read_u32_be(s->rx_buf);
    if (pkt_len < 1 || pkt_len > SSH_PKT_BUF_SIZE - 4)
    {
        pc_secure_wipe(s->rx_buf, s->rx_len);
        s->rx_len = 0;
        return -1;
    }
    size_t wire_need = 4 + pkt_len; // no MAC before NEWKEYS
    if (s->rx_len < wire_need)
    {
        return 0;
    }

    if (s->seq_no_recv >= SSH_SEQ_CLOSE_THRESHOLD)
    {
        return -1;
    }
    s->seq_no_recv++;

    uint8_t pad_len_byte = s->rx_buf[4];
    if (pad_len_byte >= pkt_len)
    {
        return -1;
    }
    size_t payload_len = pkt_len - 1 - pad_len_byte;
    if (ssh_dispatch_payload(i, s->rx_buf + 5, payload_len, handler) < 0)
    {
        return -1;
    }

    size_t consumed = wire_need;
    memmove(s->rx_buf, s->rx_buf + consumed, s->rx_len - consumed);
    s->rx_len -= consumed;
    return 1;
}

int ssh_pkt_recv(uint8_t i, const uint8_t *data, size_t len, ssh_msg_handler_t handler)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }
    SshPacketState *s = &ssh_pkt[i];
    SshKeyMat *km = &ssh_keys[i];

    // Consume the input incrementally: append as much as fits the (single-packet) receive buffer, extract
    // every complete packet to drain it, then append more. So one TCP read carrying several pipelined packets
    // - e.g. a large SFTP write fragmented into back-to-back CHANNEL_DATA messages - is processed instead of
    // being rejected when the read exceeds SSH_PKT_BUF_SIZE.
    while (len > 0)
    {
        size_t space = SSH_PKT_BUF_SIZE - s->rx_len;
        if (space == 0)
        {
            // The buffer is full yet no complete packet could be extracted -> a single packet larger than the
            // buffer. Discard and disconnect.
            pc_secure_wipe(s->rx_buf, s->rx_len);
            s->rx_len = 0;
            return -1;
        }
        size_t take = len < space ? len : space;
        memcpy(s->rx_buf + s->rx_len, data, take);
        s->rx_len += take;
        data += take;
        len -= take;

        // Extract complete packets.
        while (s->rx_len >= 4)
        {
            int r = 0;
            if (s->enc_in && km->cipher_mode == SSH_CIPHER_CHACHA20POLY1305)
            {
                r = ssh_recv_chachapoly(i, s, km, handler);
            }
            else if (s->enc_in && km->cipher_mode == SSH_CIPHER_AES256GCM)
            {
                r = ssh_recv_aesgcm(i, s, km, handler);
            }
            else if (s->enc_in && ssh_mac_is_etm(km->mac_mode))
            {
                r = ssh_recv_ctr_etm(i, s, km, handler);
            }
            else if (s->enc_in)
            {
                r = ssh_recv_ctr_emac(i, s, km, handler);
            }
            else
            {
                r = ssh_recv_plain(i, s, km, handler);
            }

            if (r < 0)
            {
                return -1;
            }
            if (r == 0)
            {
                break; // incomplete packet - append more input and retry
            }
        } // extract-complete-packets loop
    } // incremental-append loop

    return 0;
}

// ---------------------------------------------------------------------------
// Disconnect
// ---------------------------------------------------------------------------

int ssh_pkt_disconnect(uint8_t i, uint32_t reason_code, uint8_t *out, size_t *out_len, size_t out_cap)
{
    if (i >= MAX_SSH_CONNS)
    {
        return -1;
    }

    // Build SSH_MSG_DISCONNECT payload (RFC 4253 §11.1):
    //   byte    SSH_MSG_DISCONNECT
    //   uint32  reason code
    //   string  description (empty)
    //   string  language tag (empty)
    uint8_t payload[13];
    payload[0] = SSH_MSG_DISCONNECT;
    payload[1] = (uint8_t)(reason_code >> 24);
    payload[2] = (uint8_t)(reason_code >> 16);
    payload[3] = (uint8_t)(reason_code >> 8);
    payload[4] = (uint8_t)(reason_code);
    payload[5] = 0;
    payload[6] = 0;
    payload[7] = 0;
    payload[8] = 0; // empty description
    payload[9] = 0;
    payload[10] = 0;
    payload[11] = 0;
    payload[12] = 0; // empty language

    int rc = ssh_pkt_send(i, payload, sizeof(payload), out, out_len, out_cap);

    // Zero packet state and key material regardless of send success.
    ssh_pkt_init(i);
    ssh_keymat_wipe(i);
    ssh_dh_wipe(i);

    return rc;
}
