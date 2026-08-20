// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file smb2.c
 * @brief SMB2 client wire codec implementation (see smb2.h). All fields little-endian.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SMB

#include "mmgr/protomem/protomem.h"
#include "network_drivers/application/smb/smb2/smb2.h"

#include "crypto/aead/aes128gcm/aes128gcm.h"    // Aes128Gcm keyed AEAD (SMB 3.x AES-128-GCM)
#include "crypto/aead/aesccm/aesccm.h"          // AesCcm (SMB 3.x AES-128/256-CCM)
#include "crypto/aead/aesgcm/aesgcm.h"          // AesGcm keyed AEAD (SMB 3.x AES-256-GCM)
#include "crypto/hash/sha512/sha512.h"          // Sha512 for the SMB 3.1.1 preauth-integrity chain
#include "crypto/kdf/kdf/kdf.h"                 // Kdf for SMB 3.x key derivation
#include "crypto/mac/aes_cmac/aes_cmac.h"       // AesCmac for SMB 3.x message signing
#include "crypto/mac/hmac_sha256/hmac_sha256.h" // HmacSha256 for SMB 2.x message signing
#include "mmgr/endian/endian.h"
#include "mmgr/secure/secure.h" // the per-call GCM context borrow

PROTOCORE_BEGIN_DECLS

static const uint8_t SMB2_PROTOCOL_ID[4] = {0xFE, 'S', 'M', 'B'};

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_smb2_build_header(uint8_t *restrict work);
void protocore_smb2_parse_header(uint8_t *restrict work);

void protocore_smb2_transport_frame(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = Smb2V.transport_frame_args.out;
    size_t cap = Smb2V.transport_frame_args.cap;
    const uint8_t *msg = Smb2V.transport_frame_args.msg;
    size_t msg_len = Smb2V.transport_frame_args.msg_len;

    if (!out || !msg || msg_len > 0x00FFFFFF || 4 + msg_len > cap)
    {
        Smb2V.n = 0;
        return;
    }
    out[0] = 0x00; // Direct TCP: first byte MUST be zero
    out[1] = (uint8_t)(msg_len >> 16);
    out[2] = (uint8_t)(msg_len >> 8);
    out[3] = (uint8_t)(msg_len);
    mem.cpy(out + 4, msg, msg_len);
    Smb2V.n = 4 + msg_len;
}

void protocore_smb2_transport_len(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = Smb2V.transport_len_args.buf;
    size_t len = Smb2V.transport_len_args.len;

    if (!buf || len < 4 || buf[0] != 0x00)
    {
        Smb2V.u32 = 0;
        return;
    }
    Smb2V.u32 = ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

void protocore_smb2_build_header(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Smb2V.build_header_args.buf;
    size_t cap = Smb2V.build_header_args.cap;
    Smb2Command command = Smb2V.build_header_args.command;
    uint16_t credit_request = Smb2V.build_header_args.credit_request;
    uint64_t message_id = Smb2V.build_header_args.message_id;
    uint32_t tree_id = Smb2V.build_header_args.tree_id;
    uint64_t session_id = Smb2V.build_header_args.session_id;

    if (!buf || cap < PROTOCORE_SMB2_HEADER_SIZE)
    {
        Smb2V.n = 0;
        return;
    }
    mem.set(buf, 0, PROTOCORE_SMB2_HEADER_SIZE);
    mem.cpy(buf + 0, SMB2_PROTOCOL_ID, 4); // ProtocolId
    endian.wr16le(buf + 4, 64);            // StructureSize
    // bytes 6 CreditCharge and 8 Status/ChannelSequence stay zero
    endian.wr16le(buf + 12, (uint16_t)command); // Command
    endian.wr16le(buf + 14, credit_request);    // CreditRequest
    // byte 16 Flags stays zero for a client request; byte 20 NextCommand stays zero
    endian.wr64le(buf + 24, message_id); // MessageId
    // byte 32 Reserved stays zero
    endian.wr32le(buf + 36, tree_id);    // TreeId
    endian.wr64le(buf + 40, session_id); // SessionId
    // bytes 48 through 63 Signature stay zero
    Smb2V.n = PROTOCORE_SMB2_HEADER_SIZE;
}

void protocore_smb2_parse_header(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = Smb2V.parse_header_args.buf;
    size_t len = Smb2V.parse_header_args.len;
    Smb2Header *out = Smb2V.parse_header_args.out;

    if (!buf || !out || len < PROTOCORE_SMB2_HEADER_SIZE)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    if (mem.cmp(buf, SMB2_PROTOCOL_ID, 4) != 0 || endian.rd16le(buf + 4) != 64)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    out->status = endian.rd32le(buf + 8);
    out->command = (Smb2Command)endian.rd16le(buf + 12);
    out->credit_response = endian.rd16le(buf + 14);
    out->flags = endian.rd32le(buf + 16);
    out->message_id = endian.rd64le(buf + 24);
    out->tree_id = endian.rd32le(buf + 36);
    out->session_id = endian.rd64le(buf + 40);
    Smb2V.ok = PROTO_TRUE;
}

void protocore_smb2_build_negotiate(uint8_t *restrict work)
{
    uint8_t *buf = Smb2V.build_negotiate_args.buf;
    size_t cap = Smb2V.build_negotiate_args.cap;
    const uint8_t *client_guid = Smb2V.build_negotiate_args.client_guid;
    uint16_t security_mode = Smb2V.build_negotiate_args.security_mode;

    static const Smb2Dialect dialects[] = {SMB2_DIALECT_0202, SMB2_DIALECT_0210, SMB2_DIALECT_0300, SMB2_DIALECT_0302};
    const uint16_t ndialects = (uint16_t)(sizeof(dialects) / sizeof(dialects[0]));
    const size_t total = PROTOCORE_SMB2_HEADER_SIZE + 36 + (size_t)ndialects * 2; // header + fixed body + dialects
    if (!buf || !client_guid || cap < total)
    {
        Smb2V.n = 0;
        return;
    }

    Smb2V.build_header_args.buf = buf;
    Smb2V.build_header_args.cap = cap;
    Smb2V.build_header_args.command = SMB2_NEGOTIATE;
    Smb2V.build_header_args.credit_request = 1;
    Smb2V.build_header_args.message_id = 0;
    Smb2V.build_header_args.tree_id = 0;
    Smb2V.build_header_args.session_id = 0;
    protocore_smb2_build_header(work);
    if (Smb2V.n == 0)
    {
        Smb2V.n = 0;
        return;
    }

    uint8_t *b = buf + PROTOCORE_SMB2_HEADER_SIZE; // NEGOTIATE request body
    mem.set(b, 0, 36);
    endian.wr16le(b + 0, 36);            // StructureSize
    endian.wr16le(b + 2, ndialects);     // DialectCount
    endian.wr16le(b + 4, security_mode); // SecurityMode
    // byte 6 Reserved and byte 8 Capabilities stay zero
    mem.cpy(b + 12, client_guid, 16); // ClientGuid
    // byte 28 ClientStartTime stays zero; only 3.1.1 reinterprets these 8 bytes as negotiate-context fields
    for (uint16_t i = 0; i < ndialects; i++)
    {
        endian.wr16le(b + 36 + i * 2, (uint16_t)dialects[i]);
    }
    Smb2V.n = total;
}

void protocore_smb2_parse_negotiate_response(uint8_t *restrict work)
{
    const uint8_t *msg = Smb2V.parse_negotiate_response_args.msg;
    size_t len = Smb2V.parse_negotiate_response_args.len;
    Smb2NegotiateResp *out = Smb2V.parse_negotiate_response_args.out;

    if (!msg || !out)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    Smb2Header h;
    Smb2V.parse_header_args.buf = msg;
    Smb2V.parse_header_args.len = len;
    Smb2V.parse_header_args.out = &h;
    protocore_smb2_parse_header(work);
    if (!Smb2V.ok || h.command != SMB2_NEGOTIATE)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    // The fixed response body is 64 bytes (StructureSize .. NegotiateContextOffset), Buffer follows.
    if (len < PROTOCORE_SMB2_HEADER_SIZE + 64)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *b = msg + PROTOCORE_SMB2_HEADER_SIZE;
    if (endian.rd16le(b + 0) != 65) // StructureSize
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }

    out->security_mode = endian.rd16le(b + 2);
    out->dialect = endian.rd16le(b + 4);
    mem.cpy(out->server_guid, b + 8, 16);
    out->capabilities = endian.rd32le(b + 24);
    out->max_transact = endian.rd32le(b + 28);
    out->max_read = endian.rd32le(b + 32);
    out->max_write = endian.rd32le(b + 36);

    uint16_t sec_off = endian.rd16le(b + 56); // SecurityBufferOffset - from the start of the SMB2 header (msg)
    uint16_t sec_len = endian.rd16le(b + 58); // SecurityBufferLength
    if (sec_len == 0)
    {
        out->sec_buf = NULL;
        out->sec_buf_len = 0;
        Smb2V.ok = PROTO_TRUE;
        return;
    }
    if ((size_t)sec_off + sec_len > len || sec_off < PROTOCORE_SMB2_HEADER_SIZE)
    {
        Smb2V.ok = PROTO_FALSE; // security buffer out of bounds - fail closed
        return;
    }
    out->sec_buf = msg + sec_off;
    out->sec_buf_len = sec_len;
    Smb2V.ok = PROTO_TRUE;
}

void protocore_smb2_build_negotiate_311(uint8_t *restrict work)
{
    uint8_t *buf = Smb2V.build_negotiate_311_args.buf;
    size_t cap = Smb2V.build_negotiate_311_args.cap;
    const uint8_t *client_guid = Smb2V.build_negotiate_311_args.client_guid;
    uint16_t security_mode = Smb2V.build_negotiate_311_args.security_mode;
    const uint8_t *salt = Smb2V.build_negotiate_311_args.salt;
    size_t salt_len = Smb2V.build_negotiate_311_args.salt_len;
    const uint16_t *ciphers = Smb2V.build_negotiate_311_args.ciphers;
    size_t cipher_count = Smb2V.build_negotiate_311_args.cipher_count;

    static const Smb2Dialect dialects[] = {SMB2_DIALECT_0202, SMB2_DIALECT_0210, SMB2_DIALECT_0300, SMB2_DIALECT_0302,
                                           SMB2_DIALECT_0311};
    const uint16_t ndialects = (uint16_t)(sizeof(dialects) / sizeof(dialects[0]));
    if (!buf || !client_guid || !salt || salt_len == 0 || salt_len > 0xFFFF ||
        cipher_count > PROTOCORE_SMB2_MAX_OFFER_CIPHERS || (cipher_count > 0 && !ciphers))
    {
        Smb2V.n = 0;
        return;
    }

    // header(64) + fixed body(36) + dialects(2*n), padded to 8, then the negotiate-context list. Each
    // context is ContextType(2) + DataLength(2) + Reserved(4) + Data, 8-byte aligned (MS-SMB2 §2.2.3.1).
    const proto_bool offer_enc = cipher_count > 0;
    const size_t body_end = PROTOCORE_SMB2_HEADER_SIZE + 36 + (size_t)ndialects * 2;
    const size_t ctx_start = (body_end + 7) & ~(size_t)7; // NegotiateContextOffset (from msg start)
    const size_t preauth_data = 6 + salt_len;             // HashAlgorithmCount + SaltLength + 1 hash + Salt
    const size_t preauth_ctx = 8 + preauth_data;          // context header + data
    const size_t after_preauth = ctx_start + preauth_ctx;
    const size_t preauth_pad = ((after_preauth + 7) & ~(size_t)7) - after_preauth; // align the next context
    const size_t sign_ctx = 8 + 4; // header + SigningAlgorithmCount + 1 algorithm
    const size_t after_sign = ctx_start + preauth_ctx + preauth_pad + sign_ctx;
    // Pad after signing only when an encryption context follows (it must be 8-byte aligned).
    const size_t sign_pad = offer_enc ? (((after_sign + 7) & ~(size_t)7) - after_sign) : 0;
    const size_t enc_ctx = offer_enc ? (8 + 2 + 2 * cipher_count) : 0; // header + CipherCount + Ciphers[]
    const uint16_t ctx_count = offer_enc ? 3 : 2;
    const size_t total = ctx_start + preauth_ctx + preauth_pad + sign_ctx + sign_pad + enc_ctx;
    if (cap < total)
    {
        Smb2V.n = 0;
        return;
    }

    Smb2V.build_header_args.buf = buf;
    Smb2V.build_header_args.cap = cap;
    Smb2V.build_header_args.command = SMB2_NEGOTIATE;
    Smb2V.build_header_args.credit_request = 1;
    Smb2V.build_header_args.message_id = 0;
    Smb2V.build_header_args.tree_id = 0;
    Smb2V.build_header_args.session_id = 0;
    protocore_smb2_build_header(work);
    if (Smb2V.n == 0)
    {
        Smb2V.n = 0;
        return;
    }

    uint8_t *b = buf + PROTOCORE_SMB2_HEADER_SIZE;
    mem.set(b, 0, ctx_start - PROTOCORE_SMB2_HEADER_SIZE); // fixed body + dialects + alignment pad
    endian.wr16le(b + 0, 36);                              // StructureSize
    endian.wr16le(b + 2, ndialects);                       // DialectCount
    endian.wr16le(b + 4, security_mode);                   // SecurityMode
    endian.wr32le(b + 8, SMB2_GLOBAL_CAP_ENCRYPTION);      // Capabilities: advertise encryption support
    mem.cpy(b + 12, client_guid, 16);                      // ClientGuid
    endian.wr32le(b + 28, (uint32_t)ctx_start);            // NegotiateContextOffset (overlays ClientStartTime)
    endian.wr16le(b + 32, ctx_count);                      // NegotiateContextCount (preauth + signing [+ encryption])
    for (uint16_t i = 0; i < ndialects; i++)
    {
        endian.wr16le(b + 36 + i * 2, (uint16_t)dialects[i]);
    }

    // Context 1 - PREAUTH_INTEGRITY_CAPABILITIES (mandatory once 0x0311 is offered), §2.2.3.1.1.
    uint8_t *c = buf + ctx_start;
    endian.wr16le(c + 0, SMB2_PREAUTH_INTEGRITY_CAPABILITIES);
    endian.wr16le(c + 2, (uint16_t)preauth_data);
    endian.wr32le(c + 4, 0);                              // Reserved
    endian.wr16le(c + 8, 1);                              // HashAlgorithmCount
    endian.wr16le(c + 10, (uint16_t)salt_len);            // SaltLength
    endian.wr16le(c + 12, SMB2_PREAUTH_INTEGRITY_SHA512); // HashAlgorithms[0]
    mem.cpy(c + 14, salt, salt_len);                      // Salt

    // Context 2 - SIGNING_CAPABILITIES advertising AES-CMAC (the algorithm this client signs 3.x with;
    // the mandatory-to-implement SMB 3.x signer and the Windows default). A 3.1.1 server that accepts it
    // signs with AES-128-CMAC over the KDF-derived key; 3.0 / 3.0.2 use AES-CMAC unconditionally.
    uint8_t *c2 = c + preauth_ctx + preauth_pad;
    if (preauth_pad)
    {
        mem.set(c + preauth_ctx, 0, preauth_pad);
    }
    endian.wr16le(c2 + 0, SMB2_SIGNING_CAPABILITIES);
    endian.wr16le(c2 + 2, 4);                      // DataLength
    endian.wr32le(c2 + 4, 0);                      // Reserved
    endian.wr16le(c2 + 8, 1);                      // SigningAlgorithmCount
    endian.wr16le(c2 + 10, SMB2_SIGNING_AES_CMAC); // SigningAlgorithms[0]

    // Context 3 - ENCRYPTION_CAPABILITIES listing the offered ciphers in preference order (§2.2.3.1.2). A
    // server that accepts one may require an encrypted session/share; we then wrap messages in a
    // TRANSFORM_HEADER (protocore_smb2_encrypt). All four SMB 3.1.1 ciphers can be offered.
    if (offer_enc)
    {
        uint8_t *c3 = c2 + sign_ctx + sign_pad;
        if (sign_pad)
        {
            mem.set(c2 + sign_ctx, 0, sign_pad);
        }
        endian.wr16le(c3 + 0, SMB2_ENCRYPTION_CAPABILITIES);
        endian.wr16le(c3 + 2, (uint16_t)(2 + 2 * cipher_count)); // DataLength = CipherCount(2) + Ciphers[]
        endian.wr32le(c3 + 4, 0);                                // Reserved
        endian.wr16le(c3 + 8, (uint16_t)cipher_count);           // CipherCount
        for (size_t i = 0; i < cipher_count; i++)
        {
            endian.wr16le(c3 + 10 + i * 2, ciphers[i]); // Ciphers[i]
        }
    }
    Smb2V.n = total;
}

void protocore_smb2_parse_negotiate_contexts(uint8_t *restrict work)
{
    const uint8_t *msg = Smb2V.parse_negotiate_contexts_args.msg;
    size_t len = Smb2V.parse_negotiate_contexts_args.len;
    Smb2NegotiateContexts *out = Smb2V.parse_negotiate_contexts_args.out;

    if (!msg || !out)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    mem.set(out, 0, sizeof(*out));
    Smb2Header h;
    Smb2V.parse_header_args.buf = msg;
    Smb2V.parse_header_args.len = len;
    Smb2V.parse_header_args.out = &h;
    protocore_smb2_parse_header(work);
    if (!Smb2V.ok || h.command != SMB2_NEGOTIATE)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    if (len < PROTOCORE_SMB2_HEADER_SIZE + 64)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *b = msg + PROTOCORE_SMB2_HEADER_SIZE;
    if (endian.rd16le(b + 0) != 65) // StructureSize
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    uint16_t count = endian.rd16le(b + 6); // NegotiateContextCount (reserved for < 3.1.1)
    uint32_t off = endian.rd32le(b + 60);  // NegotiateContextOffset (from the SMB2 header start)
    if (count == 0 || off < PROTOCORE_SMB2_HEADER_SIZE)
    {
        Smb2V.ok = PROTO_FALSE; // not a 3.1.1 response carrying a context list
        return;
    }

    size_t p = off;
    for (uint16_t i = 0; i < count; i++)
    {
        p = (p + 7) & ~(size_t)7; // every context is 8-byte aligned
        if (p + 8 > len)
        {
            Smb2V.ok = PROTO_FALSE;
            return;
        }
        uint16_t ctype = endian.rd16le(msg + p);
        uint16_t dlen = endian.rd16le(msg + p + 2); // Reserved (4) follows at p+4
        size_t data = p + 8;
        if (data + dlen > len)
        {
            Smb2V.ok = PROTO_FALSE;
            return;
        }
        const uint8_t *d = msg + data;
        if (ctype == SMB2_PREAUTH_INTEGRITY_CAPABILITIES && dlen >= 6)
        {
            uint16_t hcount = endian.rd16le(d + 0); // HashAlgorithmCount
            uint16_t slen = endian.rd16le(d + 2);   // SaltLength
            if (hcount >= 1 && (size_t)dlen >= 4 + (size_t)hcount * 2 + slen)
            {
                out->have_preauth = PROTO_TRUE;
                out->hash_algorithm = endian.rd16le(d + 4); // HashAlgorithms[0]
                out->salt_len = slen;
                out->salt = slen ? d + 4 + (size_t)hcount * 2 : NULL;
            }
        }
        else if (ctype == SMB2_SIGNING_CAPABILITIES && dlen >= 4)
        {
            uint16_t scount = endian.rd16le(d + 0); // SigningAlgorithmCount
            if (scount >= 1 && (size_t)dlen >= 2 + (size_t)scount * 2)
            {
                out->have_signing = PROTO_TRUE;
                out->signing_algorithm = endian.rd16le(d + 2); // SigningAlgorithms[0]
            }
        }
        else if (ctype == SMB2_ENCRYPTION_CAPABILITIES && dlen >= 4)
        {
            uint16_t ccount = endian.rd16le(d + 0); // CipherCount
            if (ccount >= 1 && (size_t)dlen >= 2 + (size_t)ccount * 2)
            {
                out->have_encryption = PROTO_TRUE;
                out->cipher = endian.rd16le(d + 2); // Ciphers[0]
            }
        }
        p = data + dlen;
    }
    Smb2V.ok = PROTO_TRUE;
}

void protocore_smb2_preauth_init(uint8_t *restrict work)
{
    (void)work;
    SmbPreauth *p = Smb2V.preauth_init_args.p;

    if (p)
    {
        mem.set(p->hash, 0, sizeof(p->hash)); // the preauth hash starts as 64 zero bytes (MS-SMB2 §3.1.5.2)
    }
}

void protocore_smb2_preauth_update(uint8_t *restrict work)
{
    (void)work;
    uint8_t *crypto_work = Smb2V.preauth_update_args.crypto_work;
    SmbPreauth *p = Smb2V.preauth_update_args.p;
    const uint8_t *msg = Smb2V.preauth_update_args.msg;
    size_t len = Smb2V.preauth_update_args.len;

    if (!p || (!msg && len))
    {
        return;
    }
    // hash = SHA-512(previous hash || message); chain the current value with the next handshake message.
    // Snapshot the previous hash so the digest input never aliases its output buffer.
    uint8_t prev[PROTOCORE_SMB2_PREAUTH_HASH_LEN];
    mem.cpy(prev, p->hash, sizeof(prev));
    Sha512.init(crypto_work);
    Sha512V.update_args.data = prev;
    Sha512V.update_args.len = sizeof(prev);
    Sha512.update(crypto_work);
    Sha512V.update_args.data = msg;
    Sha512V.update_args.len = len;
    Sha512.update(crypto_work);
    Sha512V.final_args.out = p->hash;
    Sha512.final(crypto_work);
}

void protocore_smb2_build_session_setup(uint8_t *restrict work)
{
    uint8_t *buf = Smb2V.build_session_setup_args.buf;
    size_t cap = Smb2V.build_session_setup_args.cap;
    uint64_t message_id = Smb2V.build_session_setup_args.message_id;
    uint64_t session_id = Smb2V.build_session_setup_args.session_id;
    uint8_t security_mode = Smb2V.build_session_setup_args.security_mode;
    const uint8_t *sec_buf = Smb2V.build_session_setup_args.sec_buf;
    size_t sec_len = Smb2V.build_session_setup_args.sec_len;

    const size_t body = 24; // fixed SESSION_SETUP request body (§2.2.5)
    const size_t total = PROTOCORE_SMB2_HEADER_SIZE + body + sec_len;
    if (!buf || !sec_buf || sec_len == 0 || sec_len > 0xFFFF || cap < total)
    {
        Smb2V.n = 0;
        return;
    }
    Smb2V.build_header_args.buf = buf;
    Smb2V.build_header_args.cap = cap;
    Smb2V.build_header_args.command = SMB2_SESSION_SETUP;
    Smb2V.build_header_args.credit_request = 1;
    Smb2V.build_header_args.message_id = message_id;
    Smb2V.build_header_args.tree_id = 0;
    Smb2V.build_header_args.session_id = session_id;
    protocore_smb2_build_header(work);
    if (Smb2V.n == 0)
    {
        Smb2V.n = 0;
        return;
    }

    uint8_t *b = buf + PROTOCORE_SMB2_HEADER_SIZE;
    mem.set(b, 0, body);
    endian.wr16le(b + 0, 25); // StructureSize (fixed 24 + 1 for the variable buffer)
    b[2] = 0;                 // Flags (SMB2_SESSION_FLAG_BINDING only for 3.x channel binding)
    b[3] = security_mode;     // SecurityMode (one byte here)
    // byte 4 Capabilities and byte 8 Channel stay zero
    endian.wr16le(b + 12,
                  (uint16_t)(PROTOCORE_SMB2_HEADER_SIZE + body)); // SecurityBufferOffset (from the header start)
    endian.wr16le(b + 14, (uint16_t)sec_len);                     // SecurityBufferLength
    // byte 16 PreviousSessionId stays zero for a fresh session
    mem.cpy(b + body, sec_buf, sec_len);
    Smb2V.n = total;
}

void protocore_smb2_parse_session_setup_response(uint8_t *restrict work)
{
    const uint8_t *msg = Smb2V.parse_session_setup_response_args.msg;
    size_t len = Smb2V.parse_session_setup_response_args.len;
    Smb2SessionSetupResp *out = Smb2V.parse_session_setup_response_args.out;

    if (!msg || !out)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    Smb2Header h;
    Smb2V.parse_header_args.buf = msg;
    Smb2V.parse_header_args.len = len;
    Smb2V.parse_header_args.out = &h;
    protocore_smb2_parse_header(work);
    if (!Smb2V.ok || h.command != SMB2_SESSION_SETUP)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    // The fixed response body is 8 bytes (StructureSize .. SecurityBufferLength), Buffer follows.
    if (len < PROTOCORE_SMB2_HEADER_SIZE + 8)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *b = msg + PROTOCORE_SMB2_HEADER_SIZE;
    if (endian.rd16le(b + 0) != 9) // StructureSize
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }

    out->session_flags = endian.rd16le(b + 2);
    uint16_t sec_off = endian.rd16le(b + 4); // SecurityBufferOffset - from the start of the SMB2 header (msg)
    uint16_t sec_len = endian.rd16le(b + 6); // SecurityBufferLength
    if (sec_len == 0)
    {
        out->sec_buf = NULL;
        out->sec_buf_len = 0;
        Smb2V.ok = PROTO_TRUE;
        return;
    }
    if ((size_t)sec_off + sec_len > len || sec_off < PROTOCORE_SMB2_HEADER_SIZE)
    {
        Smb2V.ok = PROTO_FALSE; // security buffer out of bounds - fail closed
        return;
    }
    out->sec_buf = msg + sec_off;
    out->sec_buf_len = sec_len;
    Smb2V.ok = PROTO_TRUE;
}

void protocore_smb2_build_tree_connect(uint8_t *restrict work)
{
    uint8_t *buf = Smb2V.build_tree_connect_args.buf;
    size_t cap = Smb2V.build_tree_connect_args.cap;
    uint64_t message_id = Smb2V.build_tree_connect_args.message_id;
    uint64_t session_id = Smb2V.build_tree_connect_args.session_id;
    const uint8_t *path_utf16 = Smb2V.build_tree_connect_args.path_utf16;
    size_t path_len = Smb2V.build_tree_connect_args.path_len;

    const size_t body = 8; // fixed TREE_CONNECT request body (§2.2.9)
    const size_t total = PROTOCORE_SMB2_HEADER_SIZE + body + path_len;
    if (!buf || !path_utf16 || path_len == 0 || path_len > 0xFFFF || cap < total)
    {
        Smb2V.n = 0;
        return;
    }
    Smb2V.build_header_args.buf = buf;
    Smb2V.build_header_args.cap = cap;
    Smb2V.build_header_args.command = SMB2_TREE_CONNECT;
    Smb2V.build_header_args.credit_request = 1;
    Smb2V.build_header_args.message_id = message_id;
    Smb2V.build_header_args.tree_id = 0;
    Smb2V.build_header_args.session_id = session_id;
    protocore_smb2_build_header(work);
    if (Smb2V.n == 0)
    {
        Smb2V.n = 0;
        return;
    }

    uint8_t *b = buf + PROTOCORE_SMB2_HEADER_SIZE;
    mem.set(b, 0, body);
    endian.wr16le(b + 0, 9); // StructureSize
    // byte 2 Flags/Reserved stays zero
    endian.wr16le(b + 4, (uint16_t)(PROTOCORE_SMB2_HEADER_SIZE + body)); // PathOffset (from the header start) = 72
    endian.wr16le(b + 6, (uint16_t)path_len);                            // PathLength
    mem.cpy(b + body, path_utf16, path_len);                             // the \\server\share path (UTF-16LE)
    Smb2V.n = total;
}

void protocore_smb2_parse_tree_connect_response(uint8_t *restrict work)
{
    const uint8_t *msg = Smb2V.parse_tree_connect_response_args.msg;
    size_t len = Smb2V.parse_tree_connect_response_args.len;
    Smb2TreeConnectResp *out = Smb2V.parse_tree_connect_response_args.out;

    if (!msg || !out)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    Smb2Header h;
    Smb2V.parse_header_args.buf = msg;
    Smb2V.parse_header_args.len = len;
    Smb2V.parse_header_args.out = &h;
    protocore_smb2_parse_header(work);
    if (!Smb2V.ok || h.command != SMB2_TREE_CONNECT)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    if (len < PROTOCORE_SMB2_HEADER_SIZE + 16) // fixed 16-byte body, no variable buffer
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *b = msg + PROTOCORE_SMB2_HEADER_SIZE;
    if (endian.rd16le(b + 0) != 16) // StructureSize
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    out->share_type = b[2];
    out->share_flags = endian.rd32le(b + 4);
    out->capabilities = endian.rd32le(b + 8);
    out->maximal_access = endian.rd32le(b + 12);
    Smb2V.ok = PROTO_TRUE;
}

void protocore_smb2_build_create(uint8_t *restrict work)
{
    uint8_t *buf = Smb2V.build_create_args.buf;
    size_t cap = Smb2V.build_create_args.cap;
    uint64_t message_id = Smb2V.build_create_args.message_id;
    uint64_t session_id = Smb2V.build_create_args.session_id;
    uint32_t tree_id = Smb2V.build_create_args.tree_id;
    uint32_t desired_access = Smb2V.build_create_args.desired_access;
    uint32_t share_access = Smb2V.build_create_args.share_access;
    uint32_t create_disposition = Smb2V.build_create_args.create_disposition;
    uint32_t create_options = Smb2V.build_create_args.create_options;
    const uint8_t *name_utf16 = Smb2V.build_create_args.name_utf16;
    size_t name_len = Smb2V.build_create_args.name_len;

    const size_t body = 56; // fixed CREATE request body (§2.2.13)
    const size_t total = PROTOCORE_SMB2_HEADER_SIZE + body + name_len;
    if (!buf || !name_utf16 || name_len == 0 || name_len > 0xFFFF || cap < total)
    {
        Smb2V.n = 0;
        return;
    }
    Smb2V.build_header_args.buf = buf;
    Smb2V.build_header_args.cap = cap;
    Smb2V.build_header_args.command = SMB2_CREATE;
    Smb2V.build_header_args.credit_request = 1;
    Smb2V.build_header_args.message_id = message_id;
    Smb2V.build_header_args.tree_id = tree_id;
    Smb2V.build_header_args.session_id = session_id;
    protocore_smb2_build_header(work);
    if (Smb2V.n == 0)
    {
        Smb2V.n = 0;
        return;
    }

    uint8_t *b = buf + PROTOCORE_SMB2_HEADER_SIZE;
    mem.set(b, 0, body);
    endian.wr16le(b + 0, 57); // StructureSize (fixed 56 + 1 for the variable buffer)
    // byte 2 SecurityFlags and byte 3 RequestedOplockLevel stay zero (SMB2_OPLOCK_LEVEL_NONE)
    endian.wr32le(b + 4, 2); // ImpersonationLevel = Impersonation
    // byte 8 SmbCreateFlags and byte 16 Reserved stay zero
    endian.wr32le(b + 24, desired_access);                                // DesiredAccess
    endian.wr32le(b + 28, 0);                                             // FileAttributes = 0
    endian.wr32le(b + 32, share_access);                                  // ShareAccess
    endian.wr32le(b + 36, create_disposition);                            // CreateDisposition
    endian.wr32le(b + 40, create_options);                                // CreateOptions
    endian.wr16le(b + 44, (uint16_t)(PROTOCORE_SMB2_HEADER_SIZE + body)); // NameOffset (from the header start) = 120
    endian.wr16le(b + 46, (uint16_t)name_len);                            // NameLength
    // bytes 48 CreateContextsOffset and 52 CreateContextsLength stay zero
    mem.cpy(b + body, name_utf16, name_len);
    Smb2V.n = total;
}

void protocore_smb2_parse_create_response(uint8_t *restrict work)
{
    const uint8_t *msg = Smb2V.parse_create_response_args.msg;
    size_t len = Smb2V.parse_create_response_args.len;
    Smb2CreateResp *out = Smb2V.parse_create_response_args.out;

    if (!msg || !out)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    Smb2Header h;
    Smb2V.parse_header_args.buf = msg;
    Smb2V.parse_header_args.len = len;
    Smb2V.parse_header_args.out = &h;
    protocore_smb2_parse_header(work);
    if (!Smb2V.ok || h.command != SMB2_CREATE)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    if (len < PROTOCORE_SMB2_HEADER_SIZE + 88) // fixed 88-byte body (StructureSize .. CreateContextsLength)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *b = msg + PROTOCORE_SMB2_HEADER_SIZE;
    if (endian.rd16le(b + 0) != 89) // StructureSize
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    out->create_action = endian.rd32le(b + 4);
    out->end_of_file = endian.rd64le(b + 48);
    out->file_attributes = endian.rd32le(b + 56);
    mem.cpy(out->file_id, b + 64, 16); // FileId (persistent 8 + volatile 8)
    Smb2V.ok = PROTO_TRUE;
}

void protocore_smb2_build_close(uint8_t *restrict work)
{
    uint8_t *buf = Smb2V.build_close_args.buf;
    size_t cap = Smb2V.build_close_args.cap;
    uint64_t message_id = Smb2V.build_close_args.message_id;
    uint64_t session_id = Smb2V.build_close_args.session_id;
    uint32_t tree_id = Smb2V.build_close_args.tree_id;
    const uint8_t *file_id = Smb2V.build_close_args.file_id;

    const size_t body = 24; // fixed CLOSE request body (§2.2.15), no variable buffer
    const size_t total = PROTOCORE_SMB2_HEADER_SIZE + body;
    if (!buf || !file_id || cap < total)
    {
        Smb2V.n = 0;
        return;
    }
    Smb2V.build_header_args.buf = buf;
    Smb2V.build_header_args.cap = cap;
    Smb2V.build_header_args.command = SMB2_CLOSE;
    Smb2V.build_header_args.credit_request = 1;
    Smb2V.build_header_args.message_id = message_id;
    Smb2V.build_header_args.tree_id = tree_id;
    Smb2V.build_header_args.session_id = session_id;
    protocore_smb2_build_header(work);
    if (Smb2V.n == 0)
    {
        Smb2V.n = 0;
        return;
    }

    uint8_t *b = buf + PROTOCORE_SMB2_HEADER_SIZE;
    mem.set(b, 0, body);
    endian.wr16le(b + 0, 24); // StructureSize
    // byte 2 Flags stays zero (no POSTQUERY_ATTRIB); byte 4 Reserved stays zero
    mem.cpy(b + 8, file_id, 16); // FileId
    Smb2V.n = total;
}

void protocore_smb2_parse_close_response(uint8_t *restrict work)
{
    const uint8_t *msg = Smb2V.parse_close_response_args.msg;
    size_t len = Smb2V.parse_close_response_args.len;
    Smb2CloseResp *out = Smb2V.parse_close_response_args.out;

    if (!msg || !out)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    Smb2Header h;
    Smb2V.parse_header_args.buf = msg;
    Smb2V.parse_header_args.len = len;
    Smb2V.parse_header_args.out = &h;
    protocore_smb2_parse_header(work);
    if (!Smb2V.ok || h.command != SMB2_CLOSE)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    if (len < PROTOCORE_SMB2_HEADER_SIZE + 60) // fixed 60-byte body
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *b = msg + PROTOCORE_SMB2_HEADER_SIZE;
    if (endian.rd16le(b + 0) != 60) // StructureSize
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    out->end_of_file = endian.rd64le(b + 48);
    out->file_attributes = endian.rd32le(b + 56);
    Smb2V.ok = PROTO_TRUE;
}

void protocore_smb2_build_read(uint8_t *restrict work)
{
    uint8_t *buf = Smb2V.build_read_args.buf;
    size_t cap = Smb2V.build_read_args.cap;
    uint64_t message_id = Smb2V.build_read_args.message_id;
    uint64_t session_id = Smb2V.build_read_args.session_id;
    uint32_t tree_id = Smb2V.build_read_args.tree_id;
    const uint8_t *file_id = Smb2V.build_read_args.file_id;
    uint32_t length = Smb2V.build_read_args.length;
    uint64_t offset = Smb2V.build_read_args.offset;

    const size_t body = 48;                                     // fixed READ request body (§2.2.19)
    const size_t total = PROTOCORE_SMB2_HEADER_SIZE + body + 1; // + a 1-byte buffer (StructureSize 49 convention)
    if (!buf || !file_id || cap < total)
    {
        Smb2V.n = 0;
        return;
    }
    Smb2V.build_header_args.buf = buf;
    Smb2V.build_header_args.cap = cap;
    Smb2V.build_header_args.command = SMB2_READ;
    Smb2V.build_header_args.credit_request = 1;
    Smb2V.build_header_args.message_id = message_id;
    Smb2V.build_header_args.tree_id = tree_id;
    Smb2V.build_header_args.session_id = session_id;
    protocore_smb2_build_header(work);
    if (Smb2V.n == 0)
    {
        Smb2V.n = 0;
        return;
    }

    uint8_t *b = buf + PROTOCORE_SMB2_HEADER_SIZE;
    mem.set(b, 0, body + 1);
    endian.wr16le(b + 0, 49); // StructureSize
    b[2] = (uint8_t)(PROTOCORE_SMB2_HEADER_SIZE +
                     16); // Padding: requested data offset in the response (header + 16-byte body)
    // byte 3 Flags stays zero
    endian.wr32le(b + 4, length); // Length
    endian.wr64le(b + 8, offset); // Offset
    mem.cpy(b + 16, file_id, 16); // FileId
    endian.wr32le(b + 32, 1);     // MinimumCount = 1 (fail if the server returns nothing)
    // bytes 36 Channel, 40 RemainingBytes and 44/46 ReadChannelInfoOffset/Length stay zero
    // the one-byte Buffer at b+48 stays zero (already zeroed)
    Smb2V.n = total;
}

void protocore_smb2_parse_read_response(uint8_t *restrict work)
{
    const uint8_t *msg = Smb2V.parse_read_response_args.msg;
    size_t len = Smb2V.parse_read_response_args.len;
    Smb2ReadResp *out = Smb2V.parse_read_response_args.out;

    if (!msg || !out)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    Smb2Header h;
    Smb2V.parse_header_args.buf = msg;
    Smb2V.parse_header_args.len = len;
    Smb2V.parse_header_args.out = &h;
    protocore_smb2_parse_header(work);
    if (!Smb2V.ok || h.command != SMB2_READ)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    if (len < PROTOCORE_SMB2_HEADER_SIZE + 16) // fixed 16-byte body (StructureSize .. Reserved2), Buffer follows
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *b = msg + PROTOCORE_SMB2_HEADER_SIZE;
    if (endian.rd16le(b + 0) != 17) // StructureSize
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }

    uint8_t data_off = b[2];                  // DataOffset - from the start of the SMB2 header (msg)
    uint32_t data_len = endian.rd32le(b + 4); // DataLength
    if (data_len == 0)
    {
        out->data = NULL;
        out->data_len = 0;
        Smb2V.ok = PROTO_TRUE;
        return;
    }
    if (data_off < PROTOCORE_SMB2_HEADER_SIZE || (size_t)data_off + data_len > len)
    {
        Smb2V.ok = PROTO_FALSE; // data out of bounds - fail closed
        return;
    }
    out->data = msg + data_off;
    out->data_len = data_len;
    Smb2V.ok = PROTO_TRUE;
}

void protocore_smb2_build_write(uint8_t *restrict work)
{
    uint8_t *buf = Smb2V.build_write_args.buf;
    size_t cap = Smb2V.build_write_args.cap;
    uint64_t message_id = Smb2V.build_write_args.message_id;
    uint64_t session_id = Smb2V.build_write_args.session_id;
    uint32_t tree_id = Smb2V.build_write_args.tree_id;
    const uint8_t *file_id = Smb2V.build_write_args.file_id;
    const uint8_t *data = Smb2V.build_write_args.data;
    size_t data_len = Smb2V.build_write_args.data_len;
    uint64_t offset = Smb2V.build_write_args.offset;

    const size_t body = 48; // fixed WRITE request body (§2.2.21)
    const size_t total = PROTOCORE_SMB2_HEADER_SIZE + body + data_len;
    if (!buf || !file_id || !data || data_len == 0 || data_len > 0xFFFFFFFF || cap < total)
    {
        Smb2V.n = 0;
        return;
    }
    Smb2V.build_header_args.buf = buf;
    Smb2V.build_header_args.cap = cap;
    Smb2V.build_header_args.command = SMB2_WRITE;
    Smb2V.build_header_args.credit_request = 1;
    Smb2V.build_header_args.message_id = message_id;
    Smb2V.build_header_args.tree_id = tree_id;
    Smb2V.build_header_args.session_id = session_id;
    protocore_smb2_build_header(work);
    if (Smb2V.n == 0)
    {
        Smb2V.n = 0;
        return;
    }

    uint8_t *b = buf + PROTOCORE_SMB2_HEADER_SIZE;
    mem.set(b, 0, body);
    endian.wr16le(b + 0, 49);                                            // StructureSize
    endian.wr16le(b + 2, (uint16_t)(PROTOCORE_SMB2_HEADER_SIZE + body)); // DataOffset (from the header start) = 112
    endian.wr32le(b + 4, (uint32_t)data_len);                            // Length
    endian.wr64le(b + 8, offset);                                        // Offset
    mem.cpy(b + 16, file_id, 16);                                        // FileId
    // bytes 32 Channel, 36 RemainingBytes, 40/42 WriteChannelInfoOffset/Length and 44 Flags stay zero
    mem.cpy(b + body, data, data_len); // the data to write
    Smb2V.n = total;
}

void protocore_smb2_parse_write_response(uint8_t *restrict work)
{
    const uint8_t *msg = Smb2V.parse_write_response_args.msg;
    size_t len = Smb2V.parse_write_response_args.len;
    Smb2WriteResp *out = Smb2V.parse_write_response_args.out;

    if (!msg || !out)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    Smb2Header h;
    Smb2V.parse_header_args.buf = msg;
    Smb2V.parse_header_args.len = len;
    Smb2V.parse_header_args.out = &h;
    protocore_smb2_parse_header(work);
    if (!Smb2V.ok || h.command != SMB2_WRITE)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    if (len < PROTOCORE_SMB2_HEADER_SIZE + 16) // fixed 16-byte body
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    const uint8_t *b = msg + PROTOCORE_SMB2_HEADER_SIZE;
    if (endian.rd16le(b + 0) != 17) // StructureSize
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    out->count = endian.rd32le(b + 4); // Count (bytes written)
    Smb2V.ok = PROTO_TRUE;
}

// --- Message signing (MS-SMB2 §3.1.4.1 / §3.1.5.1) -------------------------
// The SMB2 sync header places the Flags field at offset 16 (LE u32) and the 16-byte Signature at
// offset 48. The MAC covers the whole message with the Signature zeroed. SMB 2.x uses HMAC-SHA256 (the
// on-wire Signature is its first 16 octets); SMB 3.x uses AES-128-CMAC (its full 16-octet tag). The
// FLAGS_SIGNED / zero-Signature / MAC / write-back framing is identical - only the MAC differs.
#define PROTOCORE_SMB2_FLAGS_OFF 16
#define PROTOCORE_SMB2_SIGNATURE_OFF 48
#define PROTOCORE_SMB2_SIGNATURE_LEN 16

// A MAC that writes its first 16 octets into out16 (HMAC-SHA256 truncated, or AES-CMAC whole tag).
typedef void (*Smb2MacFn)(uint8_t *crypto_work, const uint8_t key[16], const uint8_t *msg, size_t len,
                          uint8_t out16[16]);

static void mac_hmac_sha256(uint8_t *crypto_work, const uint8_t key[16], const uint8_t *msg, size_t len,
                            uint8_t out16[16])
{
    uint8_t mac[32];
    HmacSha256V.mac_args.key = key;
    HmacSha256V.mac_args.key_len = 16;
    HmacSha256V.mac_args.data = msg;
    HmacSha256V.mac_args.len = len;
    HmacSha256V.mac_args.out = mac;
    HmacSha256.mac(crypto_work);
    mem.cpy(out16, mac, 16); // Signature = first 16 octets of the HMAC
}

static void mac_aes_cmac(uint8_t *crypto_work, const uint8_t key[16], const uint8_t *msg, size_t len, uint8_t out16[16])
{
    AesCmacV.mac_args.key = key;
    AesCmacV.mac_args.msg = msg;
    AesCmacV.mac_args.msg_len = len;
    AesCmacV.mac_args.out = out16;
    AesCmac.mac(crypto_work); // the whole 16-octet CMAC tag
}

static void smb2_sign_framed(uint8_t *crypto_work, const uint8_t key[16], uint8_t *msg, size_t msg_len, Smb2MacFn mac)
{
    if (!key || !msg || msg_len < PROTOCORE_SMB2_HEADER_SIZE)
    {
        return;
    }
    endian.wr32le(msg + PROTOCORE_SMB2_FLAGS_OFF, endian.rd32le(msg + PROTOCORE_SMB2_FLAGS_OFF) | SMB2_FLAGS_SIGNED);
    mem.set(msg + PROTOCORE_SMB2_SIGNATURE_OFF, 0, PROTOCORE_SMB2_SIGNATURE_LEN); // zero the Signature before the MAC
    uint8_t tag[PROTOCORE_SMB2_SIGNATURE_LEN];
    mac(crypto_work, key, msg, msg_len, tag);
    mem.cpy(msg + PROTOCORE_SMB2_SIGNATURE_OFF, tag, PROTOCORE_SMB2_SIGNATURE_LEN);
}

static proto_bool smb2_verify_framed(uint8_t *crypto_work, const uint8_t key[16], uint8_t *msg, size_t msg_len,
                                     Smb2MacFn mac)
{
    if (!key || !msg || msg_len < PROTOCORE_SMB2_HEADER_SIZE)
    {
        return PROTO_FALSE;
    }
    uint8_t received[PROTOCORE_SMB2_SIGNATURE_LEN];
    mem.cpy(received, msg + PROTOCORE_SMB2_SIGNATURE_OFF, PROTOCORE_SMB2_SIGNATURE_LEN);
    mem.set(msg + PROTOCORE_SMB2_SIGNATURE_OFF, 0, PROTOCORE_SMB2_SIGNATURE_LEN);
    uint8_t tag[PROTOCORE_SMB2_SIGNATURE_LEN];
    mac(crypto_work, key, msg, msg_len, tag);
    mem.cpy(msg + PROTOCORE_SMB2_SIGNATURE_OFF, received,
            PROTOCORE_SMB2_SIGNATURE_LEN); // restore; the message is unchanged
    uint8_t diff = 0;
    for (size_t i = 0; i < PROTOCORE_SMB2_SIGNATURE_LEN; i++)
    {
        diff |= (uint8_t)(tag[i] ^ received[i]); // constant-time compare (no early exit)
    }
    return diff == 0;
}

void protocore_smb2_sign(uint8_t *restrict work)
{
    (void)work;
    uint8_t *crypto_work = Smb2V.sign_args.crypto_work;
    const uint8_t *key = Smb2V.sign_args.key;
    uint8_t *msg = Smb2V.sign_args.msg;
    size_t msg_len = Smb2V.sign_args.msg_len;

    smb2_sign_framed(crypto_work, key, msg, msg_len, mac_hmac_sha256);
}

void protocore_smb2_verify(uint8_t *restrict work)
{
    (void)work;
    uint8_t *crypto_work = Smb2V.verify_args.crypto_work;
    const uint8_t *key = Smb2V.verify_args.key;
    uint8_t *msg = Smb2V.verify_args.msg;
    size_t msg_len = Smb2V.verify_args.msg_len;

    Smb2V.ok = smb2_verify_framed(crypto_work, key, msg, msg_len, mac_hmac_sha256);
}

void protocore_smb2_sign_cmac(uint8_t *restrict work)
{
    (void)work;
    uint8_t *crypto_work = Smb2V.sign_cmac_args.crypto_work;
    const uint8_t *key = Smb2V.sign_cmac_args.key;
    uint8_t *msg = Smb2V.sign_cmac_args.msg;
    size_t msg_len = Smb2V.sign_cmac_args.msg_len;

    smb2_sign_framed(crypto_work, key, msg, msg_len, mac_aes_cmac);
}

void protocore_smb2_verify_cmac(uint8_t *restrict work)
{
    (void)work;
    uint8_t *crypto_work = Smb2V.verify_cmac_args.crypto_work;
    const uint8_t *key = Smb2V.verify_cmac_args.key;
    uint8_t *msg = Smb2V.verify_cmac_args.msg;
    size_t msg_len = Smb2V.verify_cmac_args.msg_len;

    Smb2V.ok = smb2_verify_framed(crypto_work, key, msg, msg_len, mac_aes_cmac);
}

void protocore_smb2_derive_signing_key(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *session_key = Smb2V.derive_signing_key_args.session_key;
    uint16_t dialect = Smb2V.derive_signing_key_args.dialect;
    const uint8_t *preauth = Smb2V.derive_signing_key_args.preauth;
    uint8_t *out_key = Smb2V.derive_signing_key_args.out_key;

    if (!session_key || !out_key)
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }

    // Assemble the SP800-108 fixed input: `Label || 0x00 || Context || [L]`. The KDF prepends the 32-bit
    // counter. Each label literal carries its own trailing NUL (sizeof includes it), which IS the Label;
    // the explicit 0x00 after it is the KDF's Label||Context separator - so the label is followed by two
    // NULs on the wire, matching Windows / Samba / impacket (verified vs impacket KDF_CounterMode).
    uint8_t fixed[96];
    size_t n = 0;
    if (dialect == (uint16_t)SMB2_DIALECT_0311)
    {
        if (!preauth)
        {
            Smb2V.ok = PROTO_FALSE;
            return;
        }
        static const char label[] = "SMBSigningKey"; // sizeof == 14 ("SMBSigningKey\0")
        mem.cpy(fixed + n, label, sizeof(label));
        n += sizeof(label);
        fixed[n++] = 0x00;               // Label||0x00 separator
        mem.cpy(fixed + n, preauth, 64); // Context = the 64-byte preauth-integrity hash
        n += 64;
    }
    else
    {
        static const char label[] = "SMB2AESCMAC"; // sizeof == 12 ("SMB2AESCMAC\0")
        static const char context[] = "SmbSign";   // sizeof == 8  ("SmbSign\0")
        mem.cpy(fixed + n, label, sizeof(label));
        n += sizeof(label);
        fixed[n++] = 0x00; // separator
        mem.cpy(fixed + n, context, sizeof(context));
        n += sizeof(context);
    }
    // [L] = the derived-key length in bits (128), 32-bit big-endian.
    fixed[n++] = 0x00;
    fixed[n++] = 0x00;
    fixed[n++] = 0x00;
    fixed[n++] = 0x80;
    size_t mark = protocore_secure_mark();
    uint8_t *w = protocore_secure_span(PROTOCORE_KDF_BORROW, 8).buf;
    KdfV.ctr_args.ki = session_key;
    KdfV.ctr_args.ki_len = 16;
    KdfV.ctr_args.fixed = fixed;
    KdfV.ctr_args.fixed_len = n;
    KdfV.ctr_args.out = out_key;
    KdfV.ctr_args.out_len = 16;
    Kdf.ctr_hmac_sha256(w);
    const proto_bool derived = KdfV.ok;
    protocore_secure_release(mark);
    Smb2V.ok = derived;
}

// One SMB 3.x cipher key (MS-SMB2 §3.1.4.2), same SP800-108 construction as the signing key. @p c2s picks the
// client->server (encrypt) label/context vs server->client (decrypt). 3.1.1 keys the context on the preauth
// hash; 3.0/3.0.2 use the fixed "ServerIn "/"ServerOut" contexts (label "SMB2AESCCM" even when the cipher is
// GCM, per the spec).
static proto_bool smb3_derive_cipher_key(const uint8_t session_key[16], uint16_t dialect, const uint8_t *preauth,
                                         proto_bool c2s, size_t key_len, uint8_t *out_key)
{
    uint8_t fixed[96];
    size_t n = 0;
    if (dialect == (uint16_t)SMB2_DIALECT_0311)
    {
        if (!preauth)
        {
            return PROTO_FALSE;
        }
        static const char c2s_label[] = "SMBC2SCipherKey"; // sizeof == 16 (15 chars + NUL)
        static const char s2c_label[] = "SMBS2CCipherKey";
        mem.cpy(fixed + n, c2s ? c2s_label : s2c_label, sizeof(c2s_label));
        n += sizeof(c2s_label);
        fixed[n++] = 0x00;               // Label||0x00 separator
        mem.cpy(fixed + n, preauth, 64); // Context = the 64-byte preauth-integrity hash
        n += 64;
    }
    else
    {
        static const char label[] = "SMB2AESCCM";  // sizeof == 11
        static const char ctx_in[] = "ServerIn ";  // sizeof == 10 (note the trailing space, MS-SMB2 §3.1.4.2)
        static const char ctx_out[] = "ServerOut"; // sizeof == 10
        mem.cpy(fixed + n, label, sizeof(label));
        n += sizeof(label);
        fixed[n++] = 0x00;
        mem.cpy(fixed + n, c2s ? ctx_in : ctx_out, sizeof(ctx_in));
        n += sizeof(ctx_in);
    }
    const uint32_t l_bits = (uint32_t)(key_len * 8); // [L] = key length in bits, 32-bit big-endian
    fixed[n++] = (uint8_t)((l_bits >> 24) & 0xff);
    fixed[n++] = (uint8_t)((l_bits >> 16) & 0xff);
    fixed[n++] = (uint8_t)((l_bits >> 8) & 0xff);
    fixed[n++] = (uint8_t)(l_bits & 0xff);
    size_t mark = protocore_secure_mark();
    uint8_t *w = protocore_secure_span(PROTOCORE_KDF_BORROW, 8).buf;
    KdfV.ctr_args.ki = session_key;
    KdfV.ctr_args.ki_len = 16;
    KdfV.ctr_args.fixed = fixed;
    KdfV.ctr_args.fixed_len = n;
    KdfV.ctr_args.out = out_key;
    KdfV.ctr_args.out_len = key_len;
    Kdf.ctr_hmac_sha256(w);
    const proto_bool derived = KdfV.ok;
    protocore_secure_release(mark);
    return derived;
}

void protocore_smb2_derive_encryption_keys(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *session_key = Smb2V.derive_encryption_keys_args.session_key;
    uint16_t dialect = Smb2V.derive_encryption_keys_args.dialect;
    const uint8_t *preauth = Smb2V.derive_encryption_keys_args.preauth;
    size_t key_len = Smb2V.derive_encryption_keys_args.key_len;
    uint8_t *out_c2s = Smb2V.derive_encryption_keys_args.out_c2s;
    uint8_t *out_s2c = Smb2V.derive_encryption_keys_args.out_s2c;

    if (!session_key || !out_c2s || !out_s2c || (key_len != 16 && key_len != 32))
    {
        Smb2V.ok = PROTO_FALSE;
        return;
    }
    Smb2V.ok = smb3_derive_cipher_key(session_key, dialect, preauth, PROTO_TRUE, key_len, out_c2s) &&
               smb3_derive_cipher_key(session_key, dialect, preauth, PROTO_FALSE, key_len, out_s2c);
}

void protocore_smb2_encrypt(uint8_t *restrict work)
{
    (void)work;
    uint16_t cipher = Smb2V.encrypt_args.cipher;
    const uint8_t *key = Smb2V.encrypt_args.key;
    const uint8_t *nonce = Smb2V.encrypt_args.nonce;
    uint64_t session_id = Smb2V.encrypt_args.session_id;
    const uint8_t *msg = Smb2V.encrypt_args.msg;
    size_t msg_len = Smb2V.encrypt_args.msg_len;
    uint8_t *out = Smb2V.encrypt_args.out;
    size_t out_cap = Smb2V.encrypt_args.out_cap;

    const size_t key_len = protocore_smb2_cipher_key_len(cipher);
    const size_t nonce_len = protocore_smb2_cipher_nonce_len(cipher);
    if (!key || !nonce || !msg || !out || key_len == 0 || nonce_len == 0 ||
        out_cap < PROTOCORE_SMB2_TRANSFORM_HDR_LEN + msg_len)
    {
        Smb2V.n = 0;
        return;
    }

    // TRANSFORM_HEADER (MS-SMB2 §2.2.41). Signature (the AEAD tag) is filled after sealing; it and the
    // ProtocolId are outside the AAD, which is the header from the Nonce field to its end (offsets 20..51).
    mem.set(out, 0, PROTOCORE_SMB2_TRANSFORM_HDR_LEN);
    endian.wr32le(out + 0, PROTOCORE_SMB2_TRANSFORM_PROTOCOL_ID); // ProtocolId 0xFD 'S' 'M' 'B'
    mem.cpy(out + 20, nonce, PROTOCORE_SMB2_NONCE_FIELD_LEN);     // Nonce field (16 bytes; AEAD uses the leading bytes)
    endian.wr32le(out + 36, (uint32_t)msg_len);                   // OriginalMessageSize
    endian.wr16le(out + 40, 0);                                   // Reserved
    endian.wr16le(out + 42, 0x0001);                              // Flags = Encrypted (3.1.1)
    endian.wr64le(out + 44, session_id);                          // SessionId

    uint8_t *ct = out + PROTOCORE_SMB2_TRANSFORM_HDR_LEN;
    const uint8_t *aad = out + 20; // 32 bytes: Nonce field .. SessionId
    uint8_t tag[16];
    proto_bool ok = PROTO_FALSE;
    switch (cipher)
    {
    case SMB2_ENCRYPTION_AES128_GCM: {
        // Per-call context: same reasoning as the AES-256 branch below - not a hot enough path to justify
        // a per-session one, and the lifecycle cost is at least visible here.
        size_t mark = protocore_secure_mark();
        uint8_t *g = protocore_secure_span(PROTOCORE_AES128GCM_BORROW, 8).buf;
        Aes128GcmV.key_args.key = key;
        Aes128Gcm.key_init(g);
        Aes128GcmV.seal_args.nonce = out + 20;
        Aes128GcmV.seal_args.aad = aad;
        Aes128GcmV.seal_args.aad_len = 32;
        Aes128GcmV.seal_args.pt = msg;
        Aes128GcmV.seal_args.pt_len = msg_len;
        Aes128GcmV.seal_args.ct_out = ct;
        Aes128GcmV.seal_args.tag_out = tag;
        Aes128Gcm.seal(g);
        Aes128Gcm.key_wipe(g);
        protocore_secure_release(mark);
    }
        ok = PROTO_TRUE;
        break;
    case SMB2_ENCRYPTION_AES256_GCM: {
        // Per-call context: this path is not hot enough to justify a per-session one. The cost is the
        // ~9,200-cycle lifecycle documented in aesgcm.h - hoist the context into session state if it
        // ever shows up in a profile.
        size_t mark = protocore_secure_mark();
        uint8_t *gcm = protocore_secure_span(PROTOCORE_AESGCM_BORROW, 8).buf;
        AesGcmV.key_args.key = key;
        AesGcm.key_init(gcm);
        AesGcmV.seal_args.nonce = out + 20;
        AesGcmV.seal_args.aad = aad;
        AesGcmV.seal_args.aad_len = 32;
        AesGcmV.seal_args.pt = msg;
        AesGcmV.seal_args.pt_len = msg_len;
        AesGcmV.seal_args.ct_out = ct;
        AesGcmV.seal_args.tag_out = tag;
        AesGcm.seal(gcm);
        AesGcm.key_wipe(gcm);
        protocore_secure_release(mark);
    }
        ok = PROTO_TRUE;
        break;
    case SMB2_ENCRYPTION_AES128_CCM:
    case SMB2_ENCRYPTION_AES256_CCM: {
        size_t mark = protocore_secure_mark();
        uint8_t *c = protocore_secure_span(PROTOCORE_AESCCM_BORROW, 8).buf;
        AesCcmV.seal_args.key = key;
        AesCcmV.seal_args.key_len = key_len;
        AesCcmV.seal_args.nonce = out + 20;
        AesCcmV.seal_args.nonce_len = nonce_len;
        AesCcmV.seal_args.aad = aad;
        AesCcmV.seal_args.aad_len = 32;
        AesCcmV.seal_args.pt = msg;
        AesCcmV.seal_args.pt_len = msg_len;
        AesCcmV.seal_args.ct_out = ct;
        AesCcmV.seal_args.tag_out = tag;
        AesCcm.seal(c);
        ok = AesCcmV.ok;
        protocore_secure_release(mark);
    }
    break;
    default:
        Smb2V.n = 0;
        return;
    }
    if (!ok)
    {
        Smb2V.n = 0;
        return;
    }
    mem.cpy(out + 4, tag, 16); // Signature = the 16-byte AEAD tag
    Smb2V.n = PROTOCORE_SMB2_TRANSFORM_HDR_LEN + msg_len;
}

void protocore_smb2_decrypt(uint8_t *restrict work)
{
    (void)work;
    uint16_t cipher = Smb2V.decrypt_args.cipher;
    const uint8_t *key = Smb2V.decrypt_args.key;
    const uint8_t *in = Smb2V.decrypt_args.in;
    size_t in_len = Smb2V.decrypt_args.in_len;
    uint8_t *out = Smb2V.decrypt_args.out;
    size_t out_cap = Smb2V.decrypt_args.out_cap;

    const size_t key_len = protocore_smb2_cipher_key_len(cipher);
    const size_t nonce_len = protocore_smb2_cipher_nonce_len(cipher);
    if (!key || !in || !out || key_len == 0 || nonce_len == 0 || in_len < PROTOCORE_SMB2_TRANSFORM_HDR_LEN)
    {
        Smb2V.n = 0;
        return;
    }
    if (endian.rd32le(in + 0) != PROTOCORE_SMB2_TRANSFORM_PROTOCOL_ID)
    {
        Smb2V.n = 0;
        return;
    }
    size_t ct_len = in_len - PROTOCORE_SMB2_TRANSFORM_HDR_LEN;
    if (endian.rd32le(in + 36) != (uint32_t)ct_len || out_cap < ct_len) // OriginalMessageSize must match
    {
        Smb2V.n = 0;
        return;
    }

    // AAD = header[20..51] (32 bytes), tag = Signature (in+4), ciphertext = in+52; the AEAD nonce is the leading
    // bytes of the AAD (the Nonce field). Snapshot the AAD and tag before decrypting: callers decrypt in place
    // (out aliases in), and the CCM open writes the recovered plaintext over the header before it reads the AAD
    // and tag to compute/verify the MAC - so reading them from @p in after decryption would see clobbered bytes.
    // Copying the 48 header bytes makes in-place decryption safe for every cipher. Fails closed on a bad tag.
    uint8_t aad[32];
    uint8_t tag[16];
    mem.cpy(aad, in + 20, 32);
    mem.cpy(tag, in + 4, 16);
    const uint8_t *ct = in + PROTOCORE_SMB2_TRANSFORM_HDR_LEN;
    proto_bool ok = PROTO_FALSE;
    switch (cipher)
    {
    case SMB2_ENCRYPTION_AES128_GCM: {
        // Per-call context: same reasoning as the AES-256 branch below - not a hot enough path to justify
        // a per-session one, and the lifecycle cost is at least visible here.
        size_t mark = protocore_secure_mark();
        uint8_t *g = protocore_secure_span(PROTOCORE_AES128GCM_BORROW, 8).buf;
        Aes128GcmV.key_args.key = key;
        Aes128Gcm.key_init(g);
        Aes128GcmV.open_args.nonce = aad;
        Aes128GcmV.open_args.aad = aad;
        Aes128GcmV.open_args.aad_len = 32;
        Aes128GcmV.open_args.ct = ct;
        Aes128GcmV.open_args.ct_len = ct_len;
        Aes128GcmV.open_args.tag = tag;
        Aes128GcmV.open_args.out = out;
        Aes128Gcm.open(g);
        ok = Aes128GcmV.ok;
        Aes128Gcm.key_wipe(g);
        protocore_secure_release(mark);
    }
    break;
    case SMB2_ENCRYPTION_AES256_GCM: {
        // Per-call context: this path is not hot enough to justify a per-session one. The cost is the
        // ~9,200-cycle lifecycle documented in aesgcm.h - hoist the context into session state if it
        // ever shows up in a profile.
        size_t mark = protocore_secure_mark();
        uint8_t *gcm = protocore_secure_span(PROTOCORE_AESGCM_BORROW, 8).buf;
        AesGcmV.key_args.key = key;
        AesGcm.key_init(gcm);
        AesGcmV.open_args.nonce = aad;
        AesGcmV.open_args.aad = aad;
        AesGcmV.open_args.aad_len = 32;
        AesGcmV.open_args.ct = ct;
        AesGcmV.open_args.ct_len = ct_len;
        AesGcmV.open_args.tag = tag;
        AesGcmV.open_args.out = out;
        AesGcm.open(gcm);
        ok = AesGcmV.ok;
        AesGcm.key_wipe(gcm);
        protocore_secure_release(mark);
    }
    break;
    case SMB2_ENCRYPTION_AES128_CCM:
    case SMB2_ENCRYPTION_AES256_CCM: {
        size_t mark = protocore_secure_mark();
        uint8_t *c = protocore_secure_span(PROTOCORE_AESCCM_BORROW, 8).buf;
        AesCcmV.open_args.key = key;
        AesCcmV.open_args.key_len = key_len;
        AesCcmV.open_args.nonce = aad;
        AesCcmV.open_args.nonce_len = nonce_len;
        AesCcmV.open_args.aad = aad;
        AesCcmV.open_args.aad_len = 32;
        AesCcmV.open_args.ct = ct;
        AesCcmV.open_args.ct_len = ct_len;
        AesCcmV.open_args.tag = tag;
        AesCcmV.open_args.out = out;
        AesCcm.open(c);
        ok = AesCcmV.ok;
        protocore_secure_release(mark);
    }
    break;
    default:
        Smb2V.n = 0;
        return;
    }
    Smb2V.n = ok ? ct_len : 0;
}

/** @brief The operands and the outcome. */
Smb2Vars Smb2V;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SMB
