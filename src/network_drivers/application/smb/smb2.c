// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file smb2.c
 * @brief SMB2 client wire codec implementation (see smb2.h). All fields little-endian.
 */

#include "smb2.h"

#if PC_ENABLE_SMB

#include "crypto/aead/aes128gcm.h"  // pc_aes128gcm_* keyed AEAD (SMB 3.x AES-128-GCM)
#include "crypto/aead/aesccm.h"     // pc_aesccm_seal_tag/open_tag (SMB 3.x AES-128/256-CCM)
#include "crypto/aead/aesgcm.h"     // pc_aesgcm_* keyed AEAD (SMB 3.x AES-256-GCM)
#include "crypto/hash/sha512.h"     // pc_sha512 for the SMB 3.1.1 preauth-integrity chain
#include "crypto/kdf/kdf.h"         // pc_kdf_ctr_hmac_sha256 for SMB 3.x key derivation
#include "crypto/mac/aes_cmac.h"    // pc_aes_cmac for SMB 3.x message signing
#include "crypto/mac/hmac_sha256.h" // pc_hmac_sha256 for SMB 2.x message signing
#include "mmgr/secure.h"            // the per-call GCM context borrow
#include "shared_primitives/endian.h"

static const uint8_t SMB2_PROTOCOL_ID[4] = {0xFE, 'S', 'M', 'B'};

size_t pc_smb2_transport_frame(uint8_t *out, size_t cap, const uint8_t *msg, size_t msg_len)
{
    if (!out || !msg || msg_len > 0x00FFFFFF || 4 + msg_len > cap)
    {
        return 0;
    }
    out[0] = 0x00; // Direct TCP: first byte MUST be zero
    out[1] = (uint8_t)(msg_len >> 16);
    out[2] = (uint8_t)(msg_len >> 8);
    out[3] = (uint8_t)(msg_len);
    memcpy(out + 4, msg, msg_len);
    return 4 + msg_len;
}

uint32_t pc_smb2_transport_len(const uint8_t *buf, size_t len)
{
    if (!buf || len < 4 || buf[0] != 0x00)
    {
        return 0;
    }
    return ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

size_t pc_smb2_build_header(uint8_t *buf, size_t cap, Smb2Command command, uint16_t credit_request, uint64_t message_id,
                            uint32_t tree_id, uint64_t session_id)
{
    if (!buf || cap < PC_SMB2_HEADER_SIZE)
    {
        return 0;
    }
    memset(buf, 0, PC_SMB2_HEADER_SIZE);
    memcpy(buf + 0, SMB2_PROTOCOL_ID, 4); // ProtocolId
    pc_wr16le(buf + 4, 64);               // StructureSize
    // bytes 6 CreditCharge and 8 Status/ChannelSequence stay zero
    pc_wr16le(buf + 12, (uint16_t)command); // Command
    pc_wr16le(buf + 14, credit_request);    // CreditRequest
    // byte 16 Flags stays zero for a client request; byte 20 NextCommand stays zero
    pc_wr64le(buf + 24, message_id); // MessageId
    // byte 32 Reserved stays zero
    pc_wr32le(buf + 36, tree_id);    // TreeId
    pc_wr64le(buf + 40, session_id); // SessionId
    // bytes 48 through 63 Signature stay zero
    return PC_SMB2_HEADER_SIZE;
}

proto_bool pc_smb2_parse_header(const uint8_t *buf, size_t len, Smb2Header *out)
{
    if (!buf || !out || len < PC_SMB2_HEADER_SIZE)
    {
        return PROTO_FALSE;
    }
    if (memcmp(buf, SMB2_PROTOCOL_ID, 4) != 0 || pc_rd16le(buf + 4) != 64)
    {
        return PROTO_FALSE;
    }
    out->status = pc_rd32le(buf + 8);
    out->command = (Smb2Command)pc_rd16le(buf + 12);
    out->credit_response = pc_rd16le(buf + 14);
    out->flags = pc_rd32le(buf + 16);
    out->message_id = pc_rd64le(buf + 24);
    out->tree_id = pc_rd32le(buf + 36);
    out->session_id = pc_rd64le(buf + 40);
    return PROTO_TRUE;
}

size_t pc_smb2_build_negotiate(uint8_t *buf, size_t cap, const uint8_t client_guid[16], uint16_t security_mode)
{
    static const Smb2Dialect dialects[] = {SMB2_DIALECT_0202, SMB2_DIALECT_0210, SMB2_DIALECT_0300, SMB2_DIALECT_0302};
    const uint16_t ndialects = (uint16_t)(sizeof(dialects) / sizeof(dialects[0]));
    const size_t total = PC_SMB2_HEADER_SIZE + 36 + (size_t)ndialects * 2; // header + fixed body + dialects
    if (!buf || !client_guid || cap < total)
    {
        return 0;
    }

    if (pc_smb2_build_header(buf, cap, SMB2_NEGOTIATE, 1, 0, 0, 0) == 0)
    {
        return 0;
    }

    uint8_t *b = buf + PC_SMB2_HEADER_SIZE; // NEGOTIATE request body
    memset(b, 0, 36);
    pc_wr16le(b + 0, 36);            // StructureSize
    pc_wr16le(b + 2, ndialects);     // DialectCount
    pc_wr16le(b + 4, security_mode); // SecurityMode
    // byte 6 Reserved and byte 8 Capabilities stay zero
    memcpy(b + 12, client_guid, 16); // ClientGuid
    // byte 28 ClientStartTime stays zero; only 3.1.1 reinterprets these 8 bytes as negotiate-context fields
    for (uint16_t i = 0; i < ndialects; i++)
    {
        pc_wr16le(b + 36 + i * 2, (uint16_t)dialects[i]);
    }
    return total;
}

proto_bool pc_smb2_parse_negotiate_response(const uint8_t *msg, size_t len, Smb2NegotiateResp *out)
{
    if (!msg || !out)
    {
        return PROTO_FALSE;
    }
    Smb2Header h;
    if (!pc_smb2_parse_header(msg, len, &h) || h.command != SMB2_NEGOTIATE)
    {
        return PROTO_FALSE;
    }
    // The fixed response body is 64 bytes (StructureSize .. NegotiateContextOffset), Buffer follows.
    if (len < PC_SMB2_HEADER_SIZE + 64)
    {
        return PROTO_FALSE;
    }
    const uint8_t *b = msg + PC_SMB2_HEADER_SIZE;
    if (pc_rd16le(b + 0) != 65) // StructureSize
    {
        return PROTO_FALSE;
    }

    out->security_mode = pc_rd16le(b + 2);
    out->dialect = pc_rd16le(b + 4);
    memcpy(out->server_guid, b + 8, 16);
    out->capabilities = pc_rd32le(b + 24);
    out->max_transact = pc_rd32le(b + 28);
    out->max_read = pc_rd32le(b + 32);
    out->max_write = pc_rd32le(b + 36);

    uint16_t sec_off = pc_rd16le(b + 56); // SecurityBufferOffset - from the start of the SMB2 header (msg)
    uint16_t sec_len = pc_rd16le(b + 58); // SecurityBufferLength
    if (sec_len == 0)
    {
        out->sec_buf = NULL;
        out->sec_buf_len = 0;
        return PROTO_TRUE;
    }
    if ((size_t)sec_off + sec_len > len || sec_off < PC_SMB2_HEADER_SIZE)
    {
        return PROTO_FALSE; // security buffer out of bounds - fail closed
    }
    out->sec_buf = msg + sec_off;
    out->sec_buf_len = sec_len;
    return PROTO_TRUE;
}

size_t pc_smb2_build_negotiate_311(uint8_t *buf, size_t cap, const uint8_t client_guid[16], uint16_t security_mode,
                                   const uint8_t *salt, size_t salt_len, const uint16_t *ciphers, size_t cipher_count)
{
    static const Smb2Dialect dialects[] = {SMB2_DIALECT_0202, SMB2_DIALECT_0210, SMB2_DIALECT_0300, SMB2_DIALECT_0302,
                                           SMB2_DIALECT_0311};
    const uint16_t ndialects = (uint16_t)(sizeof(dialects) / sizeof(dialects[0]));
    if (!buf || !client_guid || !salt || salt_len == 0 || salt_len > 0xFFFF ||
        cipher_count > PC_SMB2_MAX_OFFER_CIPHERS || (cipher_count > 0 && !ciphers))
    {
        return 0;
    }

    // header(64) + fixed body(36) + dialects(2*n), padded to 8, then the negotiate-context list. Each
    // context is ContextType(2) + DataLength(2) + Reserved(4) + Data, 8-byte aligned (MS-SMB2 §2.2.3.1).
    const proto_bool offer_enc = cipher_count > 0;
    const size_t body_end = PC_SMB2_HEADER_SIZE + 36 + (size_t)ndialects * 2;
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
        return 0;
    }

    if (pc_smb2_build_header(buf, cap, SMB2_NEGOTIATE, 1, 0, 0, 0) == 0)
    {
        return 0;
    }

    uint8_t *b = buf + PC_SMB2_HEADER_SIZE;
    memset(b, 0, ctx_start - PC_SMB2_HEADER_SIZE); // fixed body + dialects + alignment pad
    pc_wr16le(b + 0, 36);                          // StructureSize
    pc_wr16le(b + 2, ndialects);                   // DialectCount
    pc_wr16le(b + 4, security_mode);               // SecurityMode
    pc_wr32le(b + 8, SMB2_GLOBAL_CAP_ENCRYPTION);  // Capabilities: advertise encryption support
    memcpy(b + 12, client_guid, 16);               // ClientGuid
    pc_wr32le(b + 28, (uint32_t)ctx_start);        // NegotiateContextOffset (overlays ClientStartTime)
    pc_wr16le(b + 32, ctx_count);                  // NegotiateContextCount (preauth + signing [+ encryption])
    for (uint16_t i = 0; i < ndialects; i++)
    {
        pc_wr16le(b + 36 + i * 2, (uint16_t)dialects[i]);
    }

    // Context 1 - PREAUTH_INTEGRITY_CAPABILITIES (mandatory once 0x0311 is offered), §2.2.3.1.1.
    uint8_t *c = buf + ctx_start;
    pc_wr16le(c + 0, SMB2_PREAUTH_INTEGRITY_CAPABILITIES);
    pc_wr16le(c + 2, (uint16_t)preauth_data);
    pc_wr32le(c + 4, 0);                              // Reserved
    pc_wr16le(c + 8, 1);                              // HashAlgorithmCount
    pc_wr16le(c + 10, (uint16_t)salt_len);            // SaltLength
    pc_wr16le(c + 12, SMB2_PREAUTH_INTEGRITY_SHA512); // HashAlgorithms[0]
    memcpy(c + 14, salt, salt_len);                   // Salt

    // Context 2 - SIGNING_CAPABILITIES advertising AES-CMAC (the algorithm this client signs 3.x with;
    // the mandatory-to-implement SMB 3.x signer and the Windows default). A 3.1.1 server that accepts it
    // signs with AES-128-CMAC over the KDF-derived key; 3.0 / 3.0.2 use AES-CMAC unconditionally.
    uint8_t *c2 = c + preauth_ctx + preauth_pad;
    if (preauth_pad)
    {
        memset(c + preauth_ctx, 0, preauth_pad);
    }
    pc_wr16le(c2 + 0, SMB2_SIGNING_CAPABILITIES);
    pc_wr16le(c2 + 2, 4);                      // DataLength
    pc_wr32le(c2 + 4, 0);                      // Reserved
    pc_wr16le(c2 + 8, 1);                      // SigningAlgorithmCount
    pc_wr16le(c2 + 10, SMB2_SIGNING_AES_CMAC); // SigningAlgorithms[0]

    // Context 3 - ENCRYPTION_CAPABILITIES listing the offered ciphers in preference order (§2.2.3.1.2). A
    // server that accepts one may require an encrypted session/share; we then wrap messages in a
    // TRANSFORM_HEADER (pc_smb2_encrypt). All four SMB 3.1.1 ciphers can be offered.
    if (offer_enc)
    {
        uint8_t *c3 = c2 + sign_ctx + sign_pad;
        if (sign_pad)
        {
            memset(c2 + sign_ctx, 0, sign_pad);
        }
        pc_wr16le(c3 + 0, SMB2_ENCRYPTION_CAPABILITIES);
        pc_wr16le(c3 + 2, (uint16_t)(2 + 2 * cipher_count)); // DataLength = CipherCount(2) + Ciphers[]
        pc_wr32le(c3 + 4, 0);                                // Reserved
        pc_wr16le(c3 + 8, (uint16_t)cipher_count);           // CipherCount
        for (size_t i = 0; i < cipher_count; i++)
        {
            pc_wr16le(c3 + 10 + i * 2, ciphers[i]); // Ciphers[i]
        }
    }
    return total;
}

proto_bool pc_smb2_parse_negotiate_contexts(const uint8_t *msg, size_t len, Smb2NegotiateContexts *out)
{
    if (!msg || !out)
    {
        return PROTO_FALSE;
    }
    memset(out, 0, sizeof(*out));
    Smb2Header h;
    if (!pc_smb2_parse_header(msg, len, &h) || h.command != SMB2_NEGOTIATE)
    {
        return PROTO_FALSE;
    }
    if (len < PC_SMB2_HEADER_SIZE + 64)
    {
        return PROTO_FALSE;
    }
    const uint8_t *b = msg + PC_SMB2_HEADER_SIZE;
    if (pc_rd16le(b + 0) != 65) // StructureSize
    {
        return PROTO_FALSE;
    }
    uint16_t count = pc_rd16le(b + 6); // NegotiateContextCount (reserved for < 3.1.1)
    uint32_t off = pc_rd32le(b + 60);  // NegotiateContextOffset (from the SMB2 header start)
    if (count == 0 || off < PC_SMB2_HEADER_SIZE)
    {
        return PROTO_FALSE; // not a 3.1.1 response carrying a context list
    }

    size_t p = off;
    for (uint16_t i = 0; i < count; i++)
    {
        p = (p + 7) & ~(size_t)7; // every context is 8-byte aligned
        if (p + 8 > len)
        {
            return PROTO_FALSE;
        }
        uint16_t ctype = pc_rd16le(msg + p);
        uint16_t dlen = pc_rd16le(msg + p + 2); // Reserved (4) follows at p+4
        size_t data = p + 8;
        if (data + dlen > len)
        {
            return PROTO_FALSE;
        }
        const uint8_t *d = msg + data;
        if (ctype == SMB2_PREAUTH_INTEGRITY_CAPABILITIES && dlen >= 6)
        {
            uint16_t hcount = pc_rd16le(d + 0); // HashAlgorithmCount
            uint16_t slen = pc_rd16le(d + 2);   // SaltLength
            if (hcount >= 1 && (size_t)dlen >= 4 + (size_t)hcount * 2 + slen)
            {
                out->have_preauth = PROTO_TRUE;
                out->hash_algorithm = pc_rd16le(d + 4); // HashAlgorithms[0]
                out->salt_len = slen;
                out->salt = slen ? d + 4 + (size_t)hcount * 2 : NULL;
            }
        }
        else if (ctype == SMB2_SIGNING_CAPABILITIES && dlen >= 4)
        {
            uint16_t scount = pc_rd16le(d + 0); // SigningAlgorithmCount
            if (scount >= 1 && (size_t)dlen >= 2 + (size_t)scount * 2)
            {
                out->have_signing = PROTO_TRUE;
                out->signing_algorithm = pc_rd16le(d + 2); // SigningAlgorithms[0]
            }
        }
        else if (ctype == SMB2_ENCRYPTION_CAPABILITIES && dlen >= 4)
        {
            uint16_t ccount = pc_rd16le(d + 0); // CipherCount
            if (ccount >= 1 && (size_t)dlen >= 2 + (size_t)ccount * 2)
            {
                out->have_encryption = PROTO_TRUE;
                out->cipher = pc_rd16le(d + 2); // Ciphers[0]
            }
        }
        p = data + dlen;
    }
    return PROTO_TRUE;
}

void pc_smb_preauth_init(SmbPreauth *p)
{
    if (p)
    {
        memset(p->hash, 0, sizeof(p->hash)); // the preauth hash starts as 64 zero bytes (MS-SMB2 §3.1.5.2)
    }
}

void pc_smb_preauth_update(SmbPreauth *p, const uint8_t *msg, size_t len)
{
    if (!p || (!msg && len))
    {
        return;
    }
    // hash = SHA-512(previous hash || message); chain the current value with the next handshake message.
    // Snapshot the previous hash so the digest input never aliases its output buffer.
    uint8_t prev[PC_SMB2_PREAUTH_HASH_LEN];
    memcpy(prev, p->hash, sizeof(prev));
    pc_sha512_ctx c;
    pc_sha512_init(&c);
    pc_sha512_update(&c, prev, sizeof(prev));
    pc_sha512_update(&c, msg, len);
    pc_sha512_final(&c, p->hash);
}

size_t pc_smb2_build_session_setup(uint8_t *buf, size_t cap, uint64_t message_id, uint64_t session_id,
                                   uint8_t security_mode, const uint8_t *sec_buf, size_t sec_len)
{
    const size_t body = 24; // fixed SESSION_SETUP request body (§2.2.5)
    const size_t total = PC_SMB2_HEADER_SIZE + body + sec_len;
    if (!buf || !sec_buf || sec_len == 0 || sec_len > 0xFFFF || cap < total)
    {
        return 0;
    }
    if (pc_smb2_build_header(buf, cap, SMB2_SESSION_SETUP, 1, message_id, 0, session_id) == 0)
    {
        return 0;
    }

    uint8_t *b = buf + PC_SMB2_HEADER_SIZE;
    memset(b, 0, body);
    pc_wr16le(b + 0, 25); // StructureSize (fixed 24 + 1 for the variable buffer)
    b[2] = 0;             // Flags (SMB2_SESSION_FLAG_BINDING only for 3.x channel binding)
    b[3] = security_mode; // SecurityMode (one byte here)
    // byte 4 Capabilities and byte 8 Channel stay zero
    pc_wr16le(b + 12, (uint16_t)(PC_SMB2_HEADER_SIZE + body)); // SecurityBufferOffset (from the header start)
    pc_wr16le(b + 14, (uint16_t)sec_len);                      // SecurityBufferLength
    // byte 16 PreviousSessionId stays zero for a fresh session
    memcpy(b + body, sec_buf, sec_len);
    return total;
}

proto_bool pc_smb2_parse_session_setup_response(const uint8_t *msg, size_t len, Smb2SessionSetupResp *out)
{
    if (!msg || !out)
    {
        return PROTO_FALSE;
    }
    Smb2Header h;
    if (!pc_smb2_parse_header(msg, len, &h) || h.command != SMB2_SESSION_SETUP)
    {
        return PROTO_FALSE;
    }
    // The fixed response body is 8 bytes (StructureSize .. SecurityBufferLength), Buffer follows.
    if (len < PC_SMB2_HEADER_SIZE + 8)
    {
        return PROTO_FALSE;
    }
    const uint8_t *b = msg + PC_SMB2_HEADER_SIZE;
    if (pc_rd16le(b + 0) != 9) // StructureSize
    {
        return PROTO_FALSE;
    }

    out->session_flags = pc_rd16le(b + 2);
    uint16_t sec_off = pc_rd16le(b + 4); // SecurityBufferOffset - from the start of the SMB2 header (msg)
    uint16_t sec_len = pc_rd16le(b + 6); // SecurityBufferLength
    if (sec_len == 0)
    {
        out->sec_buf = NULL;
        out->sec_buf_len = 0;
        return PROTO_TRUE;
    }
    if ((size_t)sec_off + sec_len > len || sec_off < PC_SMB2_HEADER_SIZE)
    {
        return PROTO_FALSE; // security buffer out of bounds - fail closed
    }
    out->sec_buf = msg + sec_off;
    out->sec_buf_len = sec_len;
    return PROTO_TRUE;
}

size_t pc_smb2_build_tree_connect(uint8_t *buf, size_t cap, uint64_t message_id, uint64_t session_id,
                                  const uint8_t *path_utf16, size_t path_len)
{
    const size_t body = 8; // fixed TREE_CONNECT request body (§2.2.9)
    const size_t total = PC_SMB2_HEADER_SIZE + body + path_len;
    if (!buf || !path_utf16 || path_len == 0 || path_len > 0xFFFF || cap < total)
    {
        return 0;
    }
    if (pc_smb2_build_header(buf, cap, SMB2_TREE_CONNECT, 1, message_id, 0, session_id) == 0)
    {
        return 0;
    }

    uint8_t *b = buf + PC_SMB2_HEADER_SIZE;
    memset(b, 0, body);
    pc_wr16le(b + 0, 9); // StructureSize
    // byte 2 Flags/Reserved stays zero
    pc_wr16le(b + 4, (uint16_t)(PC_SMB2_HEADER_SIZE + body)); // PathOffset (from the header start) = 72
    pc_wr16le(b + 6, (uint16_t)path_len);                     // PathLength
    memcpy(b + body, path_utf16, path_len);                   // the \\server\share path (UTF-16LE)
    return total;
}

proto_bool pc_smb2_parse_tree_connect_response(const uint8_t *msg, size_t len, Smb2TreeConnectResp *out)
{
    if (!msg || !out)
    {
        return PROTO_FALSE;
    }
    Smb2Header h;
    if (!pc_smb2_parse_header(msg, len, &h) || h.command != SMB2_TREE_CONNECT)
    {
        return PROTO_FALSE;
    }
    if (len < PC_SMB2_HEADER_SIZE + 16) // fixed 16-byte body, no variable buffer
    {
        return PROTO_FALSE;
    }
    const uint8_t *b = msg + PC_SMB2_HEADER_SIZE;
    if (pc_rd16le(b + 0) != 16) // StructureSize
    {
        return PROTO_FALSE;
    }
    out->share_type = b[2];
    out->share_flags = pc_rd32le(b + 4);
    out->capabilities = pc_rd32le(b + 8);
    out->maximal_access = pc_rd32le(b + 12);
    return PROTO_TRUE;
}

size_t pc_smb2_build_create(uint8_t *buf, size_t cap, uint64_t message_id, uint64_t session_id, uint32_t tree_id,
                            uint32_t desired_access, uint32_t share_access, uint32_t create_disposition,
                            uint32_t create_options, const uint8_t *name_utf16, size_t name_len)
{
    const size_t body = 56; // fixed CREATE request body (§2.2.13)
    const size_t total = PC_SMB2_HEADER_SIZE + body + name_len;
    if (!buf || !name_utf16 || name_len == 0 || name_len > 0xFFFF || cap < total)
    {
        return 0;
    }
    if (pc_smb2_build_header(buf, cap, SMB2_CREATE, 1, message_id, tree_id, session_id) == 0)
    {
        return 0;
    }

    uint8_t *b = buf + PC_SMB2_HEADER_SIZE;
    memset(b, 0, body);
    pc_wr16le(b + 0, 57); // StructureSize (fixed 56 + 1 for the variable buffer)
    // byte 2 SecurityFlags and byte 3 RequestedOplockLevel stay zero (SMB2_OPLOCK_LEVEL_NONE)
    pc_wr32le(b + 4, 2); // ImpersonationLevel = Impersonation
    // byte 8 SmbCreateFlags and byte 16 Reserved stay zero
    pc_wr32le(b + 24, desired_access);                         // DesiredAccess
    pc_wr32le(b + 28, 0);                                      // FileAttributes = 0
    pc_wr32le(b + 32, share_access);                           // ShareAccess
    pc_wr32le(b + 36, create_disposition);                     // CreateDisposition
    pc_wr32le(b + 40, create_options);                         // CreateOptions
    pc_wr16le(b + 44, (uint16_t)(PC_SMB2_HEADER_SIZE + body)); // NameOffset (from the header start) = 120
    pc_wr16le(b + 46, (uint16_t)name_len);                     // NameLength
    // bytes 48 CreateContextsOffset and 52 CreateContextsLength stay zero
    memcpy(b + body, name_utf16, name_len);
    return total;
}

proto_bool pc_smb2_parse_create_response(const uint8_t *msg, size_t len, Smb2CreateResp *out)
{
    if (!msg || !out)
    {
        return PROTO_FALSE;
    }
    Smb2Header h;
    if (!pc_smb2_parse_header(msg, len, &h) || h.command != SMB2_CREATE)
    {
        return PROTO_FALSE;
    }
    if (len < PC_SMB2_HEADER_SIZE + 88) // fixed 88-byte body (StructureSize .. CreateContextsLength)
    {
        return PROTO_FALSE;
    }
    const uint8_t *b = msg + PC_SMB2_HEADER_SIZE;
    if (pc_rd16le(b + 0) != 89) // StructureSize
    {
        return PROTO_FALSE;
    }
    out->create_action = pc_rd32le(b + 4);
    out->end_of_file = pc_rd64le(b + 48);
    out->file_attributes = pc_rd32le(b + 56);
    memcpy(out->file_id, b + 64, 16); // FileId (persistent 8 + volatile 8)
    return PROTO_TRUE;
}

size_t pc_smb2_build_close(uint8_t *buf, size_t cap, uint64_t message_id, uint64_t session_id, uint32_t tree_id,
                           const uint8_t file_id[16])
{
    const size_t body = 24; // fixed CLOSE request body (§2.2.15), no variable buffer
    const size_t total = PC_SMB2_HEADER_SIZE + body;
    if (!buf || !file_id || cap < total)
    {
        return 0;
    }
    if (pc_smb2_build_header(buf, cap, SMB2_CLOSE, 1, message_id, tree_id, session_id) == 0)
    {
        return 0;
    }

    uint8_t *b = buf + PC_SMB2_HEADER_SIZE;
    memset(b, 0, body);
    pc_wr16le(b + 0, 24); // StructureSize
    // byte 2 Flags stays zero (no POSTQUERY_ATTRIB); byte 4 Reserved stays zero
    memcpy(b + 8, file_id, 16); // FileId
    return total;
}

proto_bool pc_smb2_parse_close_response(const uint8_t *msg, size_t len, Smb2CloseResp *out)
{
    if (!msg || !out)
    {
        return PROTO_FALSE;
    }
    Smb2Header h;
    if (!pc_smb2_parse_header(msg, len, &h) || h.command != SMB2_CLOSE)
    {
        return PROTO_FALSE;
    }
    if (len < PC_SMB2_HEADER_SIZE + 60) // fixed 60-byte body
    {
        return PROTO_FALSE;
    }
    const uint8_t *b = msg + PC_SMB2_HEADER_SIZE;
    if (pc_rd16le(b + 0) != 60) // StructureSize
    {
        return PROTO_FALSE;
    }
    out->end_of_file = pc_rd64le(b + 48);
    out->file_attributes = pc_rd32le(b + 56);
    return PROTO_TRUE;
}

size_t pc_smb2_build_read(uint8_t *buf, size_t cap, uint64_t message_id, uint64_t session_id, uint32_t tree_id,
                          const uint8_t file_id[16], uint32_t length, uint64_t offset)
{
    const size_t body = 48;                              // fixed READ request body (§2.2.19)
    const size_t total = PC_SMB2_HEADER_SIZE + body + 1; // + a 1-byte buffer (StructureSize 49 convention)
    if (!buf || !file_id || cap < total)
    {
        return 0;
    }
    if (pc_smb2_build_header(buf, cap, SMB2_READ, 1, message_id, tree_id, session_id) == 0)
    {
        return 0;
    }

    uint8_t *b = buf + PC_SMB2_HEADER_SIZE;
    memset(b, 0, body + 1);
    pc_wr16le(b + 0, 49); // StructureSize
    b[2] =
        (uint8_t)(PC_SMB2_HEADER_SIZE + 16); // Padding: requested data offset in the response (header + 16-byte body)
    // byte 3 Flags stays zero
    pc_wr32le(b + 4, length);    // Length
    pc_wr64le(b + 8, offset);    // Offset
    memcpy(b + 16, file_id, 16); // FileId
    pc_wr32le(b + 32, 1);        // MinimumCount = 1 (fail if the server returns nothing)
    // bytes 36 Channel, 40 RemainingBytes and 44/46 ReadChannelInfoOffset/Length stay zero
    // the one-byte Buffer at b+48 stays zero (already zeroed)
    return total;
}

proto_bool pc_smb2_parse_read_response(const uint8_t *msg, size_t len, Smb2ReadResp *out)
{
    if (!msg || !out)
    {
        return PROTO_FALSE;
    }
    Smb2Header h;
    if (!pc_smb2_parse_header(msg, len, &h) || h.command != SMB2_READ)
    {
        return PROTO_FALSE;
    }
    if (len < PC_SMB2_HEADER_SIZE + 16) // fixed 16-byte body (StructureSize .. Reserved2), Buffer follows
    {
        return PROTO_FALSE;
    }
    const uint8_t *b = msg + PC_SMB2_HEADER_SIZE;
    if (pc_rd16le(b + 0) != 17) // StructureSize
    {
        return PROTO_FALSE;
    }

    uint8_t data_off = b[2];              // DataOffset - from the start of the SMB2 header (msg)
    uint32_t data_len = pc_rd32le(b + 4); // DataLength
    if (data_len == 0)
    {
        out->data = NULL;
        out->data_len = 0;
        return PROTO_TRUE;
    }
    if (data_off < PC_SMB2_HEADER_SIZE || (size_t)data_off + data_len > len)
    {
        return PROTO_FALSE; // data out of bounds - fail closed
    }
    out->data = msg + data_off;
    out->data_len = data_len;
    return PROTO_TRUE;
}

size_t pc_smb2_build_write(uint8_t *buf, size_t cap, uint64_t message_id, uint64_t session_id, uint32_t tree_id,
                           const uint8_t file_id[16], const uint8_t *data, size_t data_len, uint64_t offset)
{
    const size_t body = 48; // fixed WRITE request body (§2.2.21)
    const size_t total = PC_SMB2_HEADER_SIZE + body + data_len;
    if (!buf || !file_id || !data || data_len == 0 || data_len > 0xFFFFFFFF || cap < total)
    {
        return 0;
    }
    if (pc_smb2_build_header(buf, cap, SMB2_WRITE, 1, message_id, tree_id, session_id) == 0)
    {
        return 0;
    }

    uint8_t *b = buf + PC_SMB2_HEADER_SIZE;
    memset(b, 0, body);
    pc_wr16le(b + 0, 49);                                     // StructureSize
    pc_wr16le(b + 2, (uint16_t)(PC_SMB2_HEADER_SIZE + body)); // DataOffset (from the header start) = 112
    pc_wr32le(b + 4, (uint32_t)data_len);                     // Length
    pc_wr64le(b + 8, offset);                                 // Offset
    memcpy(b + 16, file_id, 16);                              // FileId
    // bytes 32 Channel, 36 RemainingBytes, 40/42 WriteChannelInfoOffset/Length and 44 Flags stay zero
    memcpy(b + body, data, data_len); // the data to write
    return total;
}

proto_bool pc_smb2_parse_write_response(const uint8_t *msg, size_t len, Smb2WriteResp *out)
{
    if (!msg || !out)
    {
        return PROTO_FALSE;
    }
    Smb2Header h;
    if (!pc_smb2_parse_header(msg, len, &h) || h.command != SMB2_WRITE)
    {
        return PROTO_FALSE;
    }
    if (len < PC_SMB2_HEADER_SIZE + 16) // fixed 16-byte body
    {
        return PROTO_FALSE;
    }
    const uint8_t *b = msg + PC_SMB2_HEADER_SIZE;
    if (pc_rd16le(b + 0) != 17) // StructureSize
    {
        return PROTO_FALSE;
    }
    out->count = pc_rd32le(b + 4); // Count (bytes written)
    return PROTO_TRUE;
}

// --- Message signing (MS-SMB2 §3.1.4.1 / §3.1.5.1) -------------------------
// The SMB2 sync header places the Flags field at offset 16 (LE u32) and the 16-byte Signature at
// offset 48. The MAC covers the whole message with the Signature zeroed. SMB 2.x uses HMAC-SHA256 (the
// on-wire Signature is its first 16 octets); SMB 3.x uses AES-128-CMAC (its full 16-octet tag). The
// FLAGS_SIGNED / zero-Signature / MAC / write-back framing is identical - only the MAC differs.
#define PC_SMB2_FLAGS_OFF 16
#define PC_SMB2_SIGNATURE_OFF 48
#define PC_SMB2_SIGNATURE_LEN 16

// A MAC that writes its first 16 octets into out16 (HMAC-SHA256 truncated, or AES-CMAC whole tag).
typedef void (*Smb2MacFn)(const uint8_t key[16], const uint8_t *msg, size_t len, uint8_t out16[16]);

static void mac_hmac_sha256(const uint8_t key[16], const uint8_t *msg, size_t len, uint8_t out16[16])
{
    uint8_t mac[32];
    pc_hmac_sha256(key, 16, msg, len, mac);
    memcpy(out16, mac, 16); // Signature = first 16 octets of the HMAC
}

static void mac_aes_cmac(const uint8_t key[16], const uint8_t *msg, size_t len, uint8_t out16[16])
{
    pc_aes_cmac(key, msg, len, out16); // the whole 16-octet CMAC tag
}

static void smb2_sign_framed(const uint8_t key[16], uint8_t *msg, size_t msg_len, Smb2MacFn mac)
{
    if (!key || !msg || msg_len < PC_SMB2_HEADER_SIZE)
    {
        return;
    }
    pc_wr32le(msg + PC_SMB2_FLAGS_OFF, pc_rd32le(msg + PC_SMB2_FLAGS_OFF) | SMB2_FLAGS_SIGNED);
    memset(msg + PC_SMB2_SIGNATURE_OFF, 0, PC_SMB2_SIGNATURE_LEN); // zero the Signature before the MAC
    uint8_t tag[PC_SMB2_SIGNATURE_LEN];
    mac(key, msg, msg_len, tag);
    memcpy(msg + PC_SMB2_SIGNATURE_OFF, tag, PC_SMB2_SIGNATURE_LEN);
}

static proto_bool smb2_verify_framed(const uint8_t key[16], uint8_t *msg, size_t msg_len, Smb2MacFn mac)
{
    if (!key || !msg || msg_len < PC_SMB2_HEADER_SIZE)
    {
        return PROTO_FALSE;
    }
    uint8_t received[PC_SMB2_SIGNATURE_LEN];
    memcpy(received, msg + PC_SMB2_SIGNATURE_OFF, PC_SMB2_SIGNATURE_LEN);
    memset(msg + PC_SMB2_SIGNATURE_OFF, 0, PC_SMB2_SIGNATURE_LEN);
    uint8_t tag[PC_SMB2_SIGNATURE_LEN];
    mac(key, msg, msg_len, tag);
    memcpy(msg + PC_SMB2_SIGNATURE_OFF, received, PC_SMB2_SIGNATURE_LEN); // restore; the message is unchanged
    uint8_t diff = 0;
    for (size_t i = 0; i < PC_SMB2_SIGNATURE_LEN; i++)
    {
        diff |= (uint8_t)(tag[i] ^ received[i]); // constant-time compare (no early exit)
    }
    return diff == 0;
}

void pc_smb2_sign(const uint8_t key[16], uint8_t *msg, size_t msg_len)
{
    smb2_sign_framed(key, msg, msg_len, mac_hmac_sha256);
}

proto_bool pc_smb2_verify(const uint8_t key[16], uint8_t *msg, size_t msg_len)
{
    return smb2_verify_framed(key, msg, msg_len, mac_hmac_sha256);
}

void pc_smb2_sign_cmac(const uint8_t key[16], uint8_t *msg, size_t msg_len)
{
    smb2_sign_framed(key, msg, msg_len, mac_aes_cmac);
}

proto_bool pc_smb2_verify_cmac(const uint8_t key[16], uint8_t *msg, size_t msg_len)
{
    return smb2_verify_framed(key, msg, msg_len, mac_aes_cmac);
}

proto_bool pc_smb3_derive_signing_key(const uint8_t session_key[16], uint16_t dialect, const uint8_t *preauth,
                                      uint8_t out_key[16])
{
    if (!session_key || !out_key)
    {
        return PROTO_FALSE;
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
            return PROTO_FALSE;
        }
        static const char label[] = "SMBSigningKey"; // sizeof == 14 ("SMBSigningKey\0")
        memcpy(fixed + n, label, sizeof(label));
        n += sizeof(label);
        fixed[n++] = 0x00;              // Label||0x00 separator
        memcpy(fixed + n, preauth, 64); // Context = the 64-byte preauth-integrity hash
        n += 64;
    }
    else
    {
        static const char label[] = "SMB2AESCMAC"; // sizeof == 12 ("SMB2AESCMAC\0")
        static const char context[] = "SmbSign";   // sizeof == 8  ("SmbSign\0")
        memcpy(fixed + n, label, sizeof(label));
        n += sizeof(label);
        fixed[n++] = 0x00; // separator
        memcpy(fixed + n, context, sizeof(context));
        n += sizeof(context);
    }
    // [L] = the derived-key length in bits (128), 32-bit big-endian.
    fixed[n++] = 0x00;
    fixed[n++] = 0x00;
    fixed[n++] = 0x00;
    fixed[n++] = 0x80;
    return pc_kdf_ctr_hmac_sha256(session_key, 16, fixed, n, out_key, 16);
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
        memcpy(fixed + n, c2s ? c2s_label : s2c_label, sizeof(c2s_label));
        n += sizeof(c2s_label);
        fixed[n++] = 0x00;              // Label||0x00 separator
        memcpy(fixed + n, preauth, 64); // Context = the 64-byte preauth-integrity hash
        n += 64;
    }
    else
    {
        static const char label[] = "SMB2AESCCM";  // sizeof == 11
        static const char ctx_in[] = "ServerIn ";  // sizeof == 10 (note the trailing space, MS-SMB2 §3.1.4.2)
        static const char ctx_out[] = "ServerOut"; // sizeof == 10
        memcpy(fixed + n, label, sizeof(label));
        n += sizeof(label);
        fixed[n++] = 0x00;
        memcpy(fixed + n, c2s ? ctx_in : ctx_out, sizeof(ctx_in));
        n += sizeof(ctx_in);
    }
    const uint32_t l_bits = (uint32_t)(key_len * 8); // [L] = key length in bits, 32-bit big-endian
    fixed[n++] = (uint8_t)((l_bits >> 24) & 0xff);
    fixed[n++] = (uint8_t)((l_bits >> 16) & 0xff);
    fixed[n++] = (uint8_t)((l_bits >> 8) & 0xff);
    fixed[n++] = (uint8_t)(l_bits & 0xff);
    return pc_kdf_ctr_hmac_sha256(session_key, 16, fixed, n, out_key, key_len);
}

proto_bool pc_smb3_derive_encryption_keys(const uint8_t session_key[16], uint16_t dialect, const uint8_t *preauth,
                                          size_t key_len, uint8_t *out_c2s, uint8_t *out_s2c)
{
    if (!session_key || !out_c2s || !out_s2c || (key_len != 16 && key_len != 32))
    {
        return PROTO_FALSE;
    }
    return smb3_derive_cipher_key(session_key, dialect, preauth, PROTO_TRUE, key_len, out_c2s) &&
           smb3_derive_cipher_key(session_key, dialect, preauth, PROTO_FALSE, key_len, out_s2c);
}

size_t pc_smb2_encrypt(uint16_t cipher, const uint8_t *key, const uint8_t nonce[PC_SMB2_NONCE_FIELD_LEN],
                       uint64_t session_id, const uint8_t *msg, size_t msg_len, uint8_t *out, size_t out_cap)
{
    const size_t key_len = pc_smb2_cipher_key_len(cipher);
    const size_t nonce_len = pc_smb2_cipher_nonce_len(cipher);
    if (!key || !nonce || !msg || !out || key_len == 0 || nonce_len == 0 ||
        out_cap < PC_SMB2_TRANSFORM_HDR_LEN + msg_len)
    {
        return 0;
    }

    // TRANSFORM_HEADER (MS-SMB2 §2.2.41). Signature (the AEAD tag) is filled after sealing; it and the
    // ProtocolId are outside the AAD, which is the header from the Nonce field to its end (offsets 20..51).
    memset(out, 0, PC_SMB2_TRANSFORM_HDR_LEN);
    pc_wr32le(out + 0, PC_SMB2_TRANSFORM_PROTOCOL_ID); // ProtocolId 0xFD 'S' 'M' 'B'
    memcpy(out + 20, nonce, PC_SMB2_NONCE_FIELD_LEN);  // Nonce field (16 bytes; AEAD uses the leading bytes)
    pc_wr32le(out + 36, (uint32_t)msg_len);            // OriginalMessageSize
    pc_wr16le(out + 40, 0);                            // Reserved
    pc_wr16le(out + 42, 0x0001);                       // Flags = Encrypted (3.1.1)
    pc_wr64le(out + 44, session_id);                   // SessionId

    uint8_t *ct = out + PC_SMB2_TRANSFORM_HDR_LEN;
    const uint8_t *aad = out + 20; // 32 bytes: Nonce field .. SessionId
    uint8_t tag[16];
    proto_bool ok = PROTO_FALSE;
    switch (cipher)
    {
    case SMB2_ENCRYPTION_AES128_GCM: {
        // Per-call context: same reasoning as the AES-256 branch below - not a hot enough path to justify
        // a per-session one, and the lifecycle cost is at least visible here.
        size_t mark = pc_secure_mark();
        struct pc_aes128gcm_key *g = pc_aes128gcm_key_init(pc_secure_span(PC_WORK_AES128GCM, 8).buf, key);
        pc_aes128gcm_seal(g, out + 20, aad, 32, msg, msg_len, ct, tag);
        pc_aes128gcm_key_wipe(g);
        pc_secure_release(mark);
    }
        ok = PROTO_TRUE;
        break;
    case SMB2_ENCRYPTION_AES256_GCM: {
        // Per-call context: this path is not hot enough to justify a per-session one. The cost is the
        // ~9,200-cycle lifecycle documented in aesgcm.h - hoist the context into session state if it
        // ever shows up in a profile.
        size_t mark = pc_secure_mark();
        struct pc_aesgcm_key *gcm = pc_aesgcm_key_init(pc_secure_span(PC_WORK_AESGCM, 8).buf, key);
        pc_aesgcm_seal(gcm, out + 20, aad, 32, msg, msg_len, ct, tag);
        pc_aesgcm_key_wipe(gcm);
        pc_secure_release(mark);
    }
        ok = PROTO_TRUE;
        break;
    case SMB2_ENCRYPTION_AES128_CCM:
    case SMB2_ENCRYPTION_AES256_CCM:
        ok = pc_aesccm_seal_tag(key, key_len, out + 20, nonce_len, aad, 32, msg, msg_len, ct, tag);
        break;
    default:
        return 0;
    }
    if (!ok)
    {
        return 0;
    }
    memcpy(out + 4, tag, 16); // Signature = the 16-byte AEAD tag
    return PC_SMB2_TRANSFORM_HDR_LEN + msg_len;
}

size_t pc_smb2_decrypt(uint16_t cipher, const uint8_t *key, const uint8_t *in, size_t in_len, uint8_t *out,
                       size_t out_cap)
{
    const size_t key_len = pc_smb2_cipher_key_len(cipher);
    const size_t nonce_len = pc_smb2_cipher_nonce_len(cipher);
    if (!key || !in || !out || key_len == 0 || nonce_len == 0 || in_len < PC_SMB2_TRANSFORM_HDR_LEN)
    {
        return 0;
    }
    if (pc_rd32le(in + 0) != PC_SMB2_TRANSFORM_PROTOCOL_ID)
    {
        return 0;
    }
    size_t ct_len = in_len - PC_SMB2_TRANSFORM_HDR_LEN;
    if (pc_rd32le(in + 36) != (uint32_t)ct_len || out_cap < ct_len) // OriginalMessageSize must match
    {
        return 0;
    }

    // AAD = header[20..51] (32 bytes), tag = Signature (in+4), ciphertext = in+52; the AEAD nonce is the leading
    // bytes of the AAD (the Nonce field). Snapshot the AAD and tag before decrypting: callers decrypt in place
    // (out aliases in), and the CCM open writes the recovered plaintext over the header before it reads the AAD
    // and tag to compute/verify the MAC - so reading them from @p in after decryption would see clobbered bytes.
    // Copying the 48 header bytes makes in-place decryption safe for every cipher. Fails closed on a bad tag.
    uint8_t aad[32];
    uint8_t tag[16];
    memcpy(aad, in + 20, 32);
    memcpy(tag, in + 4, 16);
    const uint8_t *ct = in + PC_SMB2_TRANSFORM_HDR_LEN;
    proto_bool ok = PROTO_FALSE;
    switch (cipher)
    {
    case SMB2_ENCRYPTION_AES128_GCM: {
        // Per-call context: same reasoning as the AES-256 branch below - not a hot enough path to justify
        // a per-session one, and the lifecycle cost is at least visible here.
        size_t mark = pc_secure_mark();
        struct pc_aes128gcm_key *g = pc_aes128gcm_key_init(pc_secure_span(PC_WORK_AES128GCM, 8).buf, key);
        ok = pc_aes128gcm_open(g, aad, aad, 32, ct, ct_len, tag, out);
        pc_aes128gcm_key_wipe(g);
        pc_secure_release(mark);
    }
    break;
    case SMB2_ENCRYPTION_AES256_GCM: {
        // Per-call context: this path is not hot enough to justify a per-session one. The cost is the
        // ~9,200-cycle lifecycle documented in aesgcm.h - hoist the context into session state if it
        // ever shows up in a profile.
        size_t mark = pc_secure_mark();
        struct pc_aesgcm_key *gcm = pc_aesgcm_key_init(pc_secure_span(PC_WORK_AESGCM, 8).buf, key);
        ok = pc_aesgcm_open(gcm, aad, aad, 32, ct, ct_len, tag, out);
        pc_aesgcm_key_wipe(gcm);
        pc_secure_release(mark);
    }
    break;
    case SMB2_ENCRYPTION_AES128_CCM:
    case SMB2_ENCRYPTION_AES256_CCM:
        ok = pc_aesccm_open_tag(key, key_len, aad, nonce_len, aad, 32, ct, ct_len, tag, out);
        break;
    default:
        return 0;
    }
    return ok ? ct_len : 0;
}

#endif // PC_ENABLE_SMB
