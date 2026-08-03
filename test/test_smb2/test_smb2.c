// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SMB2 client wire codec (network_drivers/application/smb, MS-SMB2): the Direct-TCP transport
// frame, the 64-byte sync header (build/parse), the NEGOTIATE exchange, and the SESSION_SETUP
// request/response framing - including a full auth round routed through the framing (SPNEGO +
// NTLMSSP). All fields little-endian. Pure host tests against the MS-SMB2 field layout.

#include "crypto/aead/aesccm.h" // KAT the AES-CCM primitive directly (SMB 3.x AES-128/256-CCM)
#include "crypto/aead/aesgcm.h" // KAT the detached AES-256-GCM primitive
#include "network_drivers/application/smb/ntlmssp.h"
#include "network_drivers/application/smb/smb2.h"
#include "network_drivers/application/smb/spnego.h"
#include <string.h>
#include <unity.h>

// The keyed api needs a context, not a key. These are one-shot vectors, so one scratch context is
// enough - rebuilt per call, and released first so a backend that attaches vendor resources to a
// context (ESP's mbedtls does) does not leak one per vector.
static uint8_t g_gcm_ws[PC_WORK_AESGCM] __attribute__((aligned(8)));
static proto_bool g_gcm_live = PROTO_FALSE;
static struct pc_aesgcm_key *gcm_key(const uint8_t *key)
{
    if (g_gcm_live)
    {
        pc_aesgcm_key_wipe((struct pc_aesgcm_key *)(g_gcm_ws));
    }
    g_gcm_live = PROTO_TRUE;
    return pc_aesgcm_key_init(g_gcm_ws, key);
}

void setUp()
{
}
void tearDown()
{
}

// little-endian writers (mirror the codec's internal ones) for building response vectors
static void w16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void w32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static void w64(uint8_t *p, uint64_t v)
{
    w32(p, (uint32_t)v);
    w32(p + 4, (uint32_t)(v >> 32));
}
static uint16_t r16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t r32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t r64(const uint8_t *p)
{
    return (uint64_t)r32(p) | ((uint64_t)r32(p + 4) << 32);
}

void test_transport_frame()
{
    const uint8_t msg[] = {1, 2, 3, 4, 5};
    uint8_t out[16];
    size_t n = pc_smb2_transport_frame(out, sizeof(out), msg, sizeof(msg));
    TEST_ASSERT_EQUAL_size_t(4 + 5, n);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[0]); // Direct-TCP: leading zero
    TEST_ASSERT_EQUAL_HEX8(0x00, out[1]); // 24-bit big-endian length = 5
    TEST_ASSERT_EQUAL_HEX8(0x00, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x05, out[3]);
    TEST_ASSERT_EQUAL_MEMORY(msg, out + 4, 5);
    TEST_ASSERT_EQUAL_UINT32(5, pc_smb2_transport_len(out, n));
    // fail closed: too small, and a non-zero leading byte
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_transport_frame(out, 3, msg, sizeof(msg)));
    uint8_t bad[4] = {0x01, 0, 0, 5};
    TEST_ASSERT_EQUAL_UINT32(0, pc_smb2_transport_len(bad, 4));
}

void test_build_and_parse_header()
{
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_size_t(64, pc_smb2_build_header(buf, sizeof(buf), SMB2_TREE_CONNECT, 8, 0x1122334455667788ULL,
                                                      0xABCD, 0x99AABBCCDDEEFF00ULL));
    // ProtocolId + StructureSize + Command at their offsets
    const uint8_t pid[4] = {0xFE, 'S', 'M', 'B'};
    TEST_ASSERT_EQUAL_MEMORY(pid, buf, 4);
    TEST_ASSERT_EQUAL_UINT16(64, r16(buf + 4));
    TEST_ASSERT_EQUAL_UINT16(SMB2_TREE_CONNECT, r16(buf + 12));

    Smb2Header h;
    TEST_ASSERT_TRUE(pc_smb2_parse_header(buf, sizeof(buf), &h));
    TEST_ASSERT_EQUAL_UINT16(SMB2_TREE_CONNECT, h.command);
    TEST_ASSERT_EQUAL_HEX64(0x1122334455667788ULL, h.message_id);
    TEST_ASSERT_EQUAL_HEX32(0xABCD, h.tree_id);
    TEST_ASSERT_EQUAL_HEX64(0x99AABBCCDDEEFF00ULL, h.session_id);
}

void test_parse_header_rejects()
{
    uint8_t buf[64];
    pc_smb2_build_header(buf, sizeof(buf), SMB2_NEGOTIATE, 1, 0, 0, 0);
    Smb2Header h;
    TEST_ASSERT_FALSE(pc_smb2_parse_header(buf, 63, &h)); // too short
    uint8_t b2[64];
    memcpy(b2, buf, 64);
    b2[0] = 0x00; // bad ProtocolId
    TEST_ASSERT_FALSE(pc_smb2_parse_header(b2, 64, &h));
    memcpy(b2, buf, 64);
    w16(b2 + 4, 63); // bad StructureSize
    TEST_ASSERT_FALSE(pc_smb2_parse_header(b2, 64, &h));
}

void test_build_negotiate()
{
    uint8_t gid[16];
    for (int i = 0; i < 16; i++)
    {
        gid[i] = (uint8_t)(0x10 + i);
    }
    uint8_t buf[160];
    size_t n = pc_smb2_build_negotiate(buf, sizeof(buf), gid, SMB2_NEGOTIATE_SIGNING_ENABLED);
    TEST_ASSERT_EQUAL_size_t(64 + 36 + 8, n); // header + fixed body + 4 dialects

    Smb2Header h;
    TEST_ASSERT_TRUE(pc_smb2_parse_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT16(SMB2_NEGOTIATE, h.command);

    const uint8_t *b = buf + 64;              // NEGOTIATE request body
    TEST_ASSERT_EQUAL_UINT16(36, r16(b + 0)); // StructureSize
    TEST_ASSERT_EQUAL_UINT16(4, r16(b + 2));  // DialectCount
    TEST_ASSERT_EQUAL_UINT16(SMB2_NEGOTIATE_SIGNING_ENABLED, r16(b + 4));
    TEST_ASSERT_EQUAL_MEMORY(gid, b + 12, 16); // ClientGuid
    TEST_ASSERT_EQUAL_UINT16(SMB2_DIALECT_0202, r16(b + 36));
    TEST_ASSERT_EQUAL_UINT16(SMB2_DIALECT_0210, r16(b + 38));
    TEST_ASSERT_EQUAL_UINT16(SMB2_DIALECT_0300, r16(b + 40));
    TEST_ASSERT_EQUAL_UINT16(SMB2_DIALECT_0302, r16(b + 42));
    // overflow fails closed
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_negotiate(buf, 100, gid, 0));
}

// Build a well-formed NEGOTIATE response message into m; returns its length.
static size_t build_neg_resp(uint8_t *m, Smb2Dialect dialect, const uint8_t *sec, uint16_t sec_len)
{
    pc_smb2_build_header(m, 256, SMB2_NEGOTIATE, 1, 5, 0, 0);
    m[16] |= 0x01; // SMB2_FLAGS_SERVER_TO_REDIR
    uint8_t *b = m + 64;
    memset(b, 0, 64);
    w16(b + 0, 65);                              // StructureSize
    w16(b + 2, SMB2_NEGOTIATE_SIGNING_REQUIRED); // SecurityMode
    w16(b + 4, (uint16_t)dialect);               // DialectRevision
    for (int i = 0; i < 16; i++)
    {
        b[8 + i] = (uint8_t)(0xA0 + i); // ServerGuid
    }
    w32(b + 24, 0);          // Capabilities
    w32(b + 28, 0x00100000); // MaxTransactSize
    w32(b + 32, 0x00080000); // MaxReadSize
    w32(b + 36, 0x00040000); // MaxWriteSize
    size_t total = 128;      // header + 64-byte fixed body
    uint16_t off = 0;
    if (sec_len)
    {
        off = 128;
        memcpy(m + off, sec, sec_len);
        total = (size_t)off + sec_len;
    }
    w16(b + 56, off);     // SecurityBufferOffset
    w16(b + 58, sec_len); // SecurityBufferLength
    return total;
}

void test_parse_negotiate_response()
{
    const uint8_t token[] = {0x60, 0x28, 0x06, 0x06, 'S', 'P', 'N', 'E'}; // a fake SPNEGO-ish blob
    uint8_t m[256];
    size_t n = build_neg_resp(m, SMB2_DIALECT_0300, token, sizeof(token));

    Smb2NegotiateResp r;
    TEST_ASSERT_TRUE(pc_smb2_parse_negotiate_response(m, n, &r));
    TEST_ASSERT_EQUAL_UINT16(SMB2_DIALECT_0300, r.dialect);
    TEST_ASSERT_EQUAL_UINT16(SMB2_NEGOTIATE_SIGNING_REQUIRED, r.security_mode);
    TEST_ASSERT_EQUAL_UINT32(0x00080000, r.max_read);
    TEST_ASSERT_EQUAL_UINT32(0x00040000, r.max_write);
    TEST_ASSERT_EQUAL_HEX8(0xA0, r.server_guid[0]);
    TEST_ASSERT_EQUAL_UINT16(sizeof(token), r.sec_buf_len);
    TEST_ASSERT_EQUAL_MEMORY(token, r.sec_buf, sizeof(token));

    // an empty security buffer -> NULL, still valid
    n = build_neg_resp(m, SMB2_DIALECT_0210, NULL, 0);
    TEST_ASSERT_TRUE(pc_smb2_parse_negotiate_response(m, n, &r));
    TEST_ASSERT_NULL(r.sec_buf);
    TEST_ASSERT_EQUAL_UINT16(0, r.sec_buf_len);
}

void test_parse_negotiate_response_rejects()
{
    const uint8_t token[] = {1, 2, 3, 4};
    uint8_t m[256];
    size_t n = build_neg_resp(m, SMB2_DIALECT_0202, token, sizeof(token));
    Smb2NegotiateResp r;

    uint8_t bad[256];
    memcpy(bad, m, n);
    w16(bad + 64, 64); // wrong StructureSize (must be 65)
    TEST_ASSERT_FALSE(pc_smb2_parse_negotiate_response(bad, n, &r));

    memcpy(bad, m, n);
    w16(bad + 12, (uint16_t)SMB2_READ); // wrong command
    TEST_ASSERT_FALSE(pc_smb2_parse_negotiate_response(bad, n, &r));

    memcpy(bad, m, n);
    w16(bad + 64 + 58, 5000); // SecurityBufferLength past the message
    TEST_ASSERT_FALSE(pc_smb2_parse_negotiate_response(bad, n, &r));

    TEST_ASSERT_FALSE(pc_smb2_parse_negotiate_response(m, 100, &r)); // truncated before the body
}

// ---- SMB 3.1.1 negotiate contexts (MS-SMB2 §2.2.3 / §2.2.3.1 / §2.2.4) ----

void test_build_negotiate_311()
{
    uint8_t gid[16];
    for (int i = 0; i < 16; i++)
    {
        gid[i] = (uint8_t)(0x20 + i);
    }
    uint8_t salt[32];
    for (int i = 0; i < 32; i++)
    {
        salt[i] = (uint8_t)(0xA0 + i);
    }
    // The four SMB 3.1.1 ciphers, in the client's default preference order.
    const uint16_t offer[4] = {SMB2_ENCRYPTION_AES128_GCM, SMB2_ENCRYPTION_AES256_GCM, SMB2_ENCRYPTION_AES128_CCM,
                               SMB2_ENCRYPTION_AES256_CCM};
    uint8_t buf[256];
    size_t n = pc_smb2_build_negotiate_311(buf, sizeof(buf), gid, SMB2_NEGOTIATE_SIGNING_ENABLED, salt, sizeof(salt),
                                           offer, 4);
    // header(64)+body(36)+5 dialects(10) -> pad to 112; preauth ctx(46) -> pad to 160; signing ctx(12) -> pad
    // to 176; encryption ctx(8 + 2 + 4*2 = 18) = 194
    TEST_ASSERT_EQUAL_size_t(194, n);

    Smb2Header h;
    TEST_ASSERT_TRUE(pc_smb2_parse_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT16(SMB2_NEGOTIATE, h.command);

    const uint8_t *b = buf + 64;
    TEST_ASSERT_EQUAL_UINT16(36, r16(b + 0)); // StructureSize
    TEST_ASSERT_EQUAL_UINT16(5, r16(b + 2));  // DialectCount now includes 3.1.1
    TEST_ASSERT_EQUAL_MEMORY(gid, b + 12, 16);
    TEST_ASSERT_EQUAL_UINT16(SMB2_DIALECT_0311, r16(b + 44)); // 5th dialect
    TEST_ASSERT_EQUAL_UINT16(3, r16(b + 32));                 // NegotiateContextCount
    uint32_t ctx_off = r32(b + 28);                           // NegotiateContextOffset
    TEST_ASSERT_EQUAL_UINT32(112, ctx_off);
    TEST_ASSERT_EQUAL_UINT32(0, ctx_off % 8); // 8-byte aligned

    const uint8_t *c = buf + ctx_off; // PREAUTH_INTEGRITY_CAPABILITIES
    TEST_ASSERT_EQUAL_UINT16(SMB2_PREAUTH_INTEGRITY_CAPABILITIES, r16(c + 0));
    TEST_ASSERT_EQUAL_UINT16(6 + 32, r16(c + 2)); // DataLength
    TEST_ASSERT_EQUAL_UINT16(1, r16(c + 8));      // HashAlgorithmCount
    TEST_ASSERT_EQUAL_UINT16(32, r16(c + 10));    // SaltLength
    TEST_ASSERT_EQUAL_UINT16(SMB2_PREAUTH_INTEGRITY_SHA512, r16(c + 12));
    TEST_ASSERT_EQUAL_MEMORY(salt, c + 14, 32);

    const uint8_t *c2 = buf + 160; // SIGNING_CAPABILITIES, aligned after the preauth context
    TEST_ASSERT_EQUAL_UINT16(SMB2_SIGNING_CAPABILITIES, r16(c2 + 0));
    TEST_ASSERT_EQUAL_UINT16(4, r16(c2 + 2)); // DataLength
    TEST_ASSERT_EQUAL_UINT16(1, r16(c2 + 8)); // SigningAlgorithmCount
    TEST_ASSERT_EQUAL_UINT16(SMB2_SIGNING_AES_CMAC, r16(c2 + 10));

    const uint8_t *c3 = buf + 176; // ENCRYPTION_CAPABILITIES, aligned after the signing context
    TEST_ASSERT_EQUAL_UINT16(SMB2_ENCRYPTION_CAPABILITIES, r16(c3 + 0));
    TEST_ASSERT_EQUAL_UINT16(10, r16(c3 + 2));                          // DataLength = CipherCount(2) + 4 ciphers(8)
    TEST_ASSERT_EQUAL_UINT16(4, r16(c3 + 8));                           // CipherCount = all four ciphers
    TEST_ASSERT_EQUAL_UINT16(SMB2_ENCRYPTION_AES128_GCM, r16(c3 + 10)); // Ciphers[0]
    TEST_ASSERT_EQUAL_UINT16(SMB2_ENCRYPTION_AES256_GCM, r16(c3 + 12)); // Ciphers[1]
    TEST_ASSERT_EQUAL_UINT16(SMB2_ENCRYPTION_AES128_CCM, r16(c3 + 14)); // Ciphers[2]
    TEST_ASSERT_EQUAL_UINT16(SMB2_ENCRYPTION_AES256_CCM, r16(c3 + 16)); // Ciphers[3]

    // With no ciphers offered the encryption context is omitted (NegotiateContextCount = 2, total 172).
    size_t n0 =
        pc_smb2_build_negotiate_311(buf, sizeof(buf), gid, SMB2_NEGOTIATE_SIGNING_ENABLED, salt, sizeof(salt), NULL, 0);
    TEST_ASSERT_EQUAL_size_t(172, n0);
    TEST_ASSERT_EQUAL_UINT16(2, r16(buf + 64 + 32)); // NegotiateContextCount

    // overflow + bad-arg fail closed
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_negotiate_311(buf, 100, gid, 0, salt, sizeof(salt), offer, 4));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_negotiate_311(buf, sizeof(buf), gid, 0, NULL, 32, offer, 4));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_negotiate_311(buf, sizeof(buf), gid, 0, salt, 0, offer, 4));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_negotiate_311(buf, sizeof(buf), gid, 0, salt, 32, NULL, 4));  // null list
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_negotiate_311(buf, sizeof(buf), gid, 0, salt, 32, offer, 5)); // too many
}

// A NEGOTIATE response (dialect 3.1.1) carrying preauth-integrity + encryption + signing contexts.
static size_t build_neg_resp_311(uint8_t *m)
{
    pc_smb2_build_header(m, 512, SMB2_NEGOTIATE, 1, 5, 0, 0);
    uint8_t *b = m + 64;
    memset(b, 0, 64);
    w16(b + 0, 65); // StructureSize
    w16(b + 2, SMB2_NEGOTIATE_SIGNING_REQUIRED);
    w16(b + 4, (uint16_t)SMB2_DIALECT_0311); // DialectRevision
    w16(b + 56, 0);                          // SecurityBufferOffset (none here)
    w16(b + 58, 0);                          // SecurityBufferLength
    const uint32_t ctx = 128;                // right after the 64-byte fixed body, 8-aligned
    w16(b + 6, 3);                           // NegotiateContextCount
    w32(b + 60, ctx);                        // NegotiateContextOffset

    uint8_t *p = m + ctx; // 1) PREAUTH_INTEGRITY: SHA-512 + a 16-byte salt
    w16(p + 0, SMB2_PREAUTH_INTEGRITY_CAPABILITIES);
    w16(p + 2, 6 + 16);
    w32(p + 4, 0);
    w16(p + 8, 1);
    w16(p + 10, 16);
    w16(p + 12, SMB2_PREAUTH_INTEGRITY_SHA512);
    for (int i = 0; i < 16; i++)
    {
        p[14 + i] = (uint8_t)(0x50 + i);
    }

    size_t o = (ctx + 30 + 7) & ~(size_t)7; // 128+30=158 -> 160
    p = m + o;                              // 2) ENCRYPTION: AES-128-GCM
    w16(p + 0, SMB2_ENCRYPTION_CAPABILITIES);
    w16(p + 2, 4);
    w32(p + 4, 0);
    w16(p + 8, 1);
    w16(p + 10, SMB2_ENCRYPTION_AES128_GCM);

    o = (o + 12 + 7) & ~(size_t)7; // 160+12=172 -> 176
    p = m + o;                     // 3) SIGNING: HMAC-SHA256
    w16(p + 0, SMB2_SIGNING_CAPABILITIES);
    w16(p + 2, 4);
    w32(p + 4, 0);
    w16(p + 8, 1);
    w16(p + 10, SMB2_SIGNING_HMAC_SHA256);
    return o + 12; // 176 + 12 = 188
}

void test_parse_negotiate_contexts()
{
    uint8_t m[512];
    size_t n = build_neg_resp_311(m);
    Smb2NegotiateContexts c;
    TEST_ASSERT_TRUE(pc_smb2_parse_negotiate_contexts(m, n, &c));
    TEST_ASSERT_TRUE(c.have_preauth);
    TEST_ASSERT_EQUAL_UINT16(SMB2_PREAUTH_INTEGRITY_SHA512, c.hash_algorithm);
    TEST_ASSERT_EQUAL_UINT16(16, c.salt_len);
    TEST_ASSERT_NOT_NULL(c.salt);
    TEST_ASSERT_EQUAL_HEX8(0x50, c.salt[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5F, c.salt[15]);
    TEST_ASSERT_TRUE(c.have_encryption);
    TEST_ASSERT_EQUAL_UINT16(SMB2_ENCRYPTION_AES128_GCM, c.cipher);
    TEST_ASSERT_TRUE(c.have_signing);
    TEST_ASSERT_EQUAL_UINT16(SMB2_SIGNING_HMAC_SHA256, c.signing_algorithm);
}

void test_parse_negotiate_contexts_rejects()
{
    uint8_t m[512];
    size_t n = build_neg_resp_311(m);
    Smb2NegotiateContexts c;
    uint8_t bad[512];

    memcpy(bad, m, n);
    w16(bad + 64 + 6, 0); // NegotiateContextCount 0 -> not a context-bearing response
    TEST_ASSERT_FALSE(pc_smb2_parse_negotiate_contexts(bad, n, &c));

    memcpy(bad, m, n);
    w16(bad + 128 + 2, 5000); // a context DataLength that runs past the message
    TEST_ASSERT_FALSE(pc_smb2_parse_negotiate_contexts(bad, n, &c));

    memcpy(bad, m, n);
    w32(bad + 64 + 60, 10); // NegotiateContextOffset below the header
    TEST_ASSERT_FALSE(pc_smb2_parse_negotiate_contexts(bad, n, &c));

    TEST_ASSERT_FALSE(pc_smb2_parse_negotiate_contexts(m, 100, &c)); // truncated before the body
}

// The SMB 3.1.1 preauth-integrity hash chain (MS-SMB2 §3.1.5.2): SHA-512 folded over each handshake
// message, seeded with 64 zero bytes. Reference computed with Python hashlib (independent SHA-512).
void test_preauth_hash_chain()
{
    SmbPreauth p;
    pc_smb_preauth_init(&p);
    const uint8_t zero[64] = {0};
    TEST_ASSERT_EQUAL_MEMORY(zero, p.hash, 64); // the initial value is 64 zero bytes

    uint8_t m1[40];
    for (int i = 0; i < 40; i++)
    {
        m1[i] = (uint8_t)(0x10 + i);
    }
    uint8_t m2[72];
    for (int i = 0; i < 72; i++)
    {
        m2[i] = (uint8_t)(0x80 + i);
    }
    pc_smb_preauth_update(&p, m1, sizeof(m1)); // fold in a stand-in NEGOTIATE, then a SESSION_SETUP
    pc_smb_preauth_update(&p, m2, sizeof(m2));
    const uint8_t expected_preauth[64] = {0x0a, 0xa8, 0x6d, 0xd5, 0xf7, 0x6b, 0x17, 0xb2, 0x92, 0xb7, 0xc5, 0xbe, 0xfe,
                                          0x58, 0xde, 0xfa, 0xad, 0xfc, 0xad, 0x9b, 0x66, 0x2b, 0x32, 0x54, 0xc2, 0x08,
                                          0x54, 0x4c, 0xe1, 0xad, 0x96, 0x93, 0xf7, 0xd6, 0x9f, 0xbc, 0x7c, 0x73, 0x17,
                                          0xad, 0xdc, 0xf7, 0x57, 0xde, 0x50, 0x4e, 0x48, 0x4e, 0x6c, 0x6a, 0x9f, 0xdd,
                                          0x79, 0xf2, 0x42, 0xd9, 0x76, 0x5a, 0x25, 0x76, 0xa8, 0xa0, 0xc3, 0xf6};
    TEST_ASSERT_EQUAL_MEMORY(expected_preauth, p.hash, 64);
}

void test_build_session_setup()
{
    uint8_t tok[40];
    for (int i = 0; i < 40; i++)
    {
        tok[i] = (uint8_t)(i + 1);
    }
    uint8_t buf[256];
    size_t n = pc_smb2_build_session_setup(buf, sizeof(buf), 7, 0xDEADBEEFULL, SMB2_NEGOTIATE_SIGNING_ENABLED, tok,
                                           sizeof(tok));
    TEST_ASSERT_EQUAL_size_t(64 + 24 + 40, n);

    Smb2Header h;
    TEST_ASSERT_TRUE(pc_smb2_parse_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT16(SMB2_SESSION_SETUP, h.command);
    TEST_ASSERT_EQUAL_HEX64(0xDEADBEEFULL, h.session_id); // echoes the server SessionId
    TEST_ASSERT_EQUAL_HEX64(7, h.message_id);

    const uint8_t *b = buf + 64;
    TEST_ASSERT_EQUAL_UINT16(25, r16(b + 0)); // StructureSize
    TEST_ASSERT_EQUAL_HEX8(SMB2_NEGOTIATE_SIGNING_ENABLED, b[3]);
    TEST_ASSERT_EQUAL_UINT16(64 + 24, r16(b + 12)); // SecurityBufferOffset = 88
    TEST_ASSERT_EQUAL_UINT16(40, r16(b + 14));      // SecurityBufferLength
    TEST_ASSERT_EQUAL_MEMORY(tok, buf + 88, 40);
    // overflow + empty token fail closed
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_session_setup(buf, 100, 7, 0, 0, tok, sizeof(tok)));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_session_setup(buf, sizeof(buf), 7, 0, 0, tok, 0));
}

// Build a well-formed SESSION_SETUP response message into m; returns its length.
static size_t build_ss_resp(uint8_t *m, uint64_t session_id, uint32_t status, uint16_t flags, const uint8_t *sec,
                            uint16_t sec_len)
{
    pc_smb2_build_header(m, 512, SMB2_SESSION_SETUP, 1, 6, 0, session_id);
    w32(m + 8, status); // Status (STATUS_MORE_PROCESSING_REQUIRED then SUCCESS)
    m[16] |= 0x01;      // SMB2_FLAGS_SERVER_TO_REDIR
    uint8_t *b = m + 64;
    memset(b, 0, 8);
    w16(b + 0, 9);     // StructureSize
    w16(b + 2, flags); // SessionFlags
    size_t total = 72; // header + 8-byte fixed body
    uint16_t off = 0;
    if (sec_len)
    {
        off = 72;
        memcpy(m + off, sec, sec_len);
        total = (size_t)off + sec_len;
    }
    w16(b + 4, off);     // SecurityBufferOffset
    w16(b + 6, sec_len); // SecurityBufferLength
    return total;
}

void test_parse_session_setup_response()
{
    const uint8_t tok[] = {0xa1, 0x05, 'c', 'h', 'a', 'l'};
    uint8_t m[256];
    size_t n =
        build_ss_resp(m, 0x1234ULL, SMB2_STATUS_MORE_PROCESSING_REQUIRED, SMB2_SESSION_FLAG_IS_GUEST, tok, sizeof(tok));

    Smb2Header h;
    TEST_ASSERT_TRUE(pc_smb2_parse_header(m, n, &h));
    TEST_ASSERT_EQUAL_HEX32(SMB2_STATUS_MORE_PROCESSING_REQUIRED, h.status);
    TEST_ASSERT_EQUAL_HEX64(0x1234ULL, h.session_id);

    Smb2SessionSetupResp r;
    TEST_ASSERT_TRUE(pc_smb2_parse_session_setup_response(m, n, &r));
    TEST_ASSERT_EQUAL_UINT16(SMB2_SESSION_FLAG_IS_GUEST, r.session_flags);
    TEST_ASSERT_EQUAL_UINT16(sizeof(tok), r.sec_buf_len);
    TEST_ASSERT_EQUAL_MEMORY(tok, r.sec_buf, sizeof(tok));

    // the final SUCCESS round carries no security buffer -> NULL, still valid
    n = build_ss_resp(m, 0x1234ULL, SMB2_STATUS_SUCCESS, 0, NULL, 0);
    TEST_ASSERT_TRUE(pc_smb2_parse_session_setup_response(m, n, &r));
    TEST_ASSERT_NULL(r.sec_buf);
    TEST_ASSERT_EQUAL_UINT16(0, r.sec_buf_len);
}

void test_session_setup_rejects()
{
    const uint8_t tok[] = {1, 2, 3, 4};
    uint8_t m[256];
    size_t n = build_ss_resp(m, 1, 0, 0, tok, sizeof(tok));
    Smb2SessionSetupResp r;
    uint8_t bad[256];

    memcpy(bad, m, n);
    w16(bad + 64, 8); // wrong StructureSize (must be 9)
    TEST_ASSERT_FALSE(pc_smb2_parse_session_setup_response(bad, n, &r));
    memcpy(bad, m, n);
    w16(bad + 12, (uint16_t)SMB2_READ); // wrong command
    TEST_ASSERT_FALSE(pc_smb2_parse_session_setup_response(bad, n, &r));
    memcpy(bad, m, n);
    w16(bad + 64 + 6, 5000); // SecurityBufferLength past the message
    TEST_ASSERT_FALSE(pc_smb2_parse_session_setup_response(bad, n, &r));
    TEST_ASSERT_FALSE(pc_smb2_parse_session_setup_response(m, 68, &r)); // truncated before the body
}

// A minimal NTLMSSP CHALLENGE (server type-2) with the given server challenge and a single-EOL
// target info, so the client can parse it out at the end of the SESSION_SETUP flow.
static size_t build_ntlmssp_challenge(uint8_t *m, const uint8_t sc[8])
{
    memset(m, 0, 52);
    const uint8_t sig[8] = {'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};
    memcpy(m, sig, 8);
    w32(m + 8, 2); // MessageType CHALLENGE
    w32(m + 20, NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_TARGET_INFO);
    memcpy(m + 24, sc, 8); // ServerChallenge
    w16(m + 40, 4);        // TargetInfoLen (a lone MsvAvEOL pair)
    w16(m + 42, 4);
    w32(m + 44, 48); // TargetInfoBufferOffset
    // m[48..51] = 00 00 00 00 (AvId=0, AvLen=0)
    return 52;
}

// End to end through the SESSION_SETUP framing: the client wraps an NTLMSSP NEGOTIATE in SPNEGO and
// frames it as a request; the server's SESSION_SETUP response carries a SPNEGO-wrapped CHALLENGE;
// the client unwinds framing -> SPNEGO -> NTLMSSP and recovers the server challenge intact.
void test_session_setup_spnego_flow()
{
    uint8_t neg[64];
    size_t neg_n = pc_ntlmssp_build_negotiate(neg, sizeof(neg), NTLMSSP_CLIENT_DEFAULT_FLAGS);
    uint8_t spnego[128];
    size_t sp_n = pc_spnego_wrap_negotiate(neg, neg_n, spnego, sizeof(spnego));
    uint8_t req[256];
    size_t req_n = pc_smb2_build_session_setup(req, sizeof(req), 1, 0, SMB2_NEGOTIATE_SIGNING_ENABLED, spnego, sp_n);
    TEST_ASSERT_GREATER_THAN_size_t(0, req_n);
    TEST_ASSERT_EQUAL_MEMORY(spnego, req + 88, sp_n); // the token is framed at offset 88

    const uint8_t sc[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint8_t chal[64];
    size_t chal_n = build_ntlmssp_challenge(chal, sc);
    uint8_t srv_tok[128];
    size_t srv_n = pc_spnego_wrap_authenticate(chal, chal_n, srv_tok, sizeof(srv_tok)); // server NegTokenResp shape
    uint8_t resp[256];
    size_t pc_resp_n =
        build_ss_resp(resp, 0xABCDULL, SMB2_STATUS_MORE_PROCESSING_REQUIRED, 0, srv_tok, (uint16_t)srv_n);

    Smb2SessionSetupResp r;
    TEST_ASSERT_TRUE(pc_smb2_parse_session_setup_response(resp, pc_resp_n, &r));
    const uint8_t *ct = NULL;
    size_t cl = 0;
    TEST_ASSERT_TRUE(pc_spnego_parse_response(r.sec_buf, r.sec_buf_len, &ct, &cl));
    NtlmChallenge nch;
    TEST_ASSERT_TRUE(pc_ntlmssp_parse_challenge(ct, cl, &nch));
    TEST_ASSERT_EQUAL_MEMORY(sc, nch.server_challenge, 8); // survived framing -> SPNEGO -> NTLMSSP
}

void test_build_tree_connect()
{
    const uint8_t path[] = {'\\', 0, '\\', 0, 's', 0, 'r', 0, 'v', 0, '\\', 0, 's', 0, 'h', 0}; // \\srv\sh
    uint8_t buf[128];
    size_t n = pc_smb2_build_tree_connect(buf, sizeof(buf), 2, 0xABCDULL, path, sizeof(path));
    TEST_ASSERT_EQUAL_size_t(64 + 8 + sizeof(path), n);

    Smb2Header h;
    TEST_ASSERT_TRUE(pc_smb2_parse_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT16(SMB2_TREE_CONNECT, h.command);
    TEST_ASSERT_EQUAL_HEX64(0xABCDULL, h.session_id);

    const uint8_t *b = buf + 64;
    TEST_ASSERT_EQUAL_UINT16(9, r16(b + 0));            // StructureSize
    TEST_ASSERT_EQUAL_UINT16(72, r16(b + 4));           // PathOffset
    TEST_ASSERT_EQUAL_UINT16(sizeof(path), r16(b + 6)); // PathLength
    TEST_ASSERT_EQUAL_MEMORY(path, buf + 72, sizeof(path));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_tree_connect(buf, 60, 2, 0, path, sizeof(path))); // overflow
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_tree_connect(buf, sizeof(buf), 2, 0, path, 0));   // empty path
}

static size_t build_tc_resp(uint8_t *m, uint32_t tree_id, uint8_t share_type, uint32_t maximal_access)
{
    pc_smb2_build_header(m, 128, SMB2_TREE_CONNECT, 1, 2, tree_id, 0xABCD);
    m[16] |= 0x01; // SERVER_TO_REDIR
    uint8_t *b = m + 64;
    memset(b, 0, 16);
    w16(b + 0, 16);    // StructureSize
    b[2] = share_type; // ShareType
    w32(b + 12, maximal_access);
    return 64 + 16;
}

void test_parse_tree_connect_response()
{
    uint8_t m[128];
    size_t n = build_tc_resp(m, 0x777, SMB2_SHARE_TYPE_DISK, 0x001f01ff);
    Smb2Header h;
    TEST_ASSERT_TRUE(pc_smb2_parse_header(m, n, &h));
    TEST_ASSERT_EQUAL_HEX32(0x777, h.tree_id); // TreeId comes from the header

    Smb2TreeConnectResp r;
    TEST_ASSERT_TRUE(pc_smb2_parse_tree_connect_response(m, n, &r));
    TEST_ASSERT_EQUAL_HEX8(SMB2_SHARE_TYPE_DISK, r.share_type);
    TEST_ASSERT_EQUAL_HEX32(0x001f01ff, r.maximal_access);

    uint8_t bad[128];
    memcpy(bad, m, n);
    w16(bad + 64, 15); // wrong StructureSize (must be 16)
    TEST_ASSERT_FALSE(pc_smb2_parse_tree_connect_response(bad, n, &r));
    TEST_ASSERT_FALSE(pc_smb2_parse_tree_connect_response(m, 70, &r)); // truncated
}

void test_build_create()
{
    const uint8_t name[] = {'a', 0, '.', 0, 'n', 0, 'c', 0}; // "a.nc" UTF-16LE
    uint8_t buf[256];
    size_t n = pc_smb2_build_create(buf, sizeof(buf), 3, 0xAAAA, 0x777, SMB2_FILE_GENERIC_READ, SMB2_FILE_SHARE_READ,
                                    SMB2_FILE_OPEN, SMB2_FILE_NON_DIRECTORY_FILE, name, sizeof(name));
    TEST_ASSERT_EQUAL_size_t(64 + 56 + sizeof(name), n);

    Smb2Header h;
    TEST_ASSERT_TRUE(pc_smb2_parse_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT16(SMB2_CREATE, h.command);
    TEST_ASSERT_EQUAL_HEX32(0x777, h.tree_id);

    const uint8_t *b = buf + 64;
    TEST_ASSERT_EQUAL_UINT16(57, r16(b + 0)); // StructureSize
    TEST_ASSERT_EQUAL_UINT32(2, r32(b + 4));  // ImpersonationLevel
    TEST_ASSERT_EQUAL_UINT32(SMB2_FILE_GENERIC_READ, r32(b + 24));
    TEST_ASSERT_EQUAL_UINT32(SMB2_FILE_SHARE_READ, r32(b + 32));
    TEST_ASSERT_EQUAL_UINT32(SMB2_FILE_OPEN, r32(b + 36));
    TEST_ASSERT_EQUAL_UINT32(SMB2_FILE_NON_DIRECTORY_FILE, r32(b + 40));
    TEST_ASSERT_EQUAL_UINT16(120, r16(b + 44));          // NameOffset
    TEST_ASSERT_EQUAL_UINT16(sizeof(name), r16(b + 46)); // NameLength
    TEST_ASSERT_EQUAL_MEMORY(name, buf + 120, sizeof(name));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_create(buf, 100, 3, 0, 0, 0, 0, 0, 0, name, sizeof(name))); // overflow
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_create(buf, sizeof(buf), 3, 0, 0, 0, 0, 0, 0, name, 0));    // empty name
}

static size_t build_create_resp(uint8_t *m, const uint8_t fid[16], uint64_t eof, uint32_t action)
{
    pc_smb2_build_header(m, 256, SMB2_CREATE, 1, 3, 0x777, 0xAAAA);
    m[16] |= 0x01;
    uint8_t *b = m + 64;
    memset(b, 0, 88);
    w16(b + 0, 89);     // StructureSize
    w32(b + 4, action); // CreateAction
    w64(b + 48, eof);   // EndofFile
    w32(b + 56, 0x80);  // FileAttributes = NORMAL
    memcpy(b + 64, fid, 16);
    return 64 + 88;
}

void test_parse_create_response()
{
    uint8_t fid[16];
    for (int i = 0; i < 16; i++)
    {
        fid[i] = (uint8_t)(0xF0 + i);
    }
    uint8_t m[256];
    size_t n = build_create_resp(m, fid, 0x123456789ULL, 1);

    Smb2CreateResp r;
    TEST_ASSERT_TRUE(pc_smb2_parse_create_response(m, n, &r));
    TEST_ASSERT_EQUAL_MEMORY(fid, r.file_id, 16);
    TEST_ASSERT_EQUAL_HEX64(0x123456789ULL, r.end_of_file);
    TEST_ASSERT_EQUAL_UINT32(1, r.create_action);
    TEST_ASSERT_EQUAL_HEX32(0x80, r.file_attributes);

    uint8_t bad[256];
    memcpy(bad, m, n);
    w16(bad + 64, 88); // wrong StructureSize (must be 89)
    TEST_ASSERT_FALSE(pc_smb2_parse_create_response(bad, n, &r));
    TEST_ASSERT_FALSE(pc_smb2_parse_create_response(m, 100, &r)); // truncated
}

void test_close_roundtrip()
{
    uint8_t fid[16];
    for (int i = 0; i < 16; i++)
    {
        fid[i] = (uint8_t)(i + 1);
    }
    uint8_t buf[128];
    size_t n = pc_smb2_build_close(buf, sizeof(buf), 4, 0xAAAA, 0x777, fid);
    TEST_ASSERT_EQUAL_size_t(64 + 24, n);

    Smb2Header h;
    TEST_ASSERT_TRUE(pc_smb2_parse_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT16(SMB2_CLOSE, h.command);
    const uint8_t *b = buf + 64;
    TEST_ASSERT_EQUAL_UINT16(24, r16(b + 0));                                // StructureSize
    TEST_ASSERT_EQUAL_MEMORY(fid, b + 8, 16);                                // FileId
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_close(buf, 80, 4, 0, 0, fid)); // overflow

    uint8_t m[128];
    pc_smb2_build_header(m, 128, SMB2_CLOSE, 1, 4, 0x777, 0xAAAA);
    m[16] |= 0x01;
    uint8_t *rb = m + 64;
    memset(rb, 0, 60);
    w16(rb + 0, 60);         // StructureSize
    w64(rb + 48, 0x4000ULL); // EndofFile
    w32(rb + 56, 0x80);      // FileAttributes
    Smb2CloseResp r;
    TEST_ASSERT_TRUE(pc_smb2_parse_close_response(m, 64 + 60, &r));
    TEST_ASSERT_EQUAL_HEX64(0x4000ULL, r.end_of_file);
    TEST_ASSERT_EQUAL_HEX32(0x80, r.file_attributes);
    uint8_t bad[128];
    memcpy(bad, m, 64 + 60);
    w16(bad + 64, 59); // wrong StructureSize (must be 60)
    TEST_ASSERT_FALSE(pc_smb2_parse_close_response(bad, 64 + 60, &r));
}

void test_build_read()
{
    uint8_t fid[16];
    for (int i = 0; i < 16; i++)
    {
        fid[i] = (uint8_t)(0xC0 + i);
    }
    uint8_t buf[128];
    size_t n = pc_smb2_build_read(buf, sizeof(buf), 5, 0xAAAA, 0x777, fid, 0x10000, 0x1000ULL);
    TEST_ASSERT_EQUAL_size_t(64 + 48 + 1, n);

    Smb2Header h;
    TEST_ASSERT_TRUE(pc_smb2_parse_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT16(SMB2_READ, h.command);
    TEST_ASSERT_EQUAL_HEX32(0x777, h.tree_id);

    const uint8_t *b = buf + 64;
    TEST_ASSERT_EQUAL_UINT16(49, r16(b + 0));                                      // StructureSize
    TEST_ASSERT_EQUAL_HEX8(80, b[2]);                                              // Padding = header + 16
    TEST_ASSERT_EQUAL_UINT32(0x10000, r32(b + 4));                                 // Length
    TEST_ASSERT_EQUAL_HEX64(0x1000ULL, r64(b + 8));                                // Offset
    TEST_ASSERT_EQUAL_MEMORY(fid, b + 16, 16);                                     // FileId
    TEST_ASSERT_EQUAL_UINT32(1, r32(b + 32));                                      // MinimumCount
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_read(buf, 100, 5, 0, 0, fid, 1, 0)); // overflow
}

static size_t build_read_resp(uint8_t *m, const uint8_t *data, uint32_t data_len)
{
    pc_smb2_build_header(m, 512, SMB2_READ, 1, 5, 0x777, 0xAAAA);
    m[16] |= 0x01;
    uint8_t *b = m + 64;
    memset(b, 0, 16);
    w16(b + 0, 17); // StructureSize
    b[2] = 80;      // DataOffset = header + 16
    w32(b + 4, data_len);
    size_t total = 80;
    if (data_len)
    {
        memcpy(m + 80, data, data_len);
        total = 80 + data_len;
    }
    return total;
}

void test_parse_read_response()
{
    const uint8_t data[] = "N123 G01 X10 Y20\r\n";
    uint8_t m[512];
    size_t n = build_read_resp(m, data, sizeof(data));

    Smb2ReadResp r;
    TEST_ASSERT_TRUE(pc_smb2_parse_read_response(m, n, &r));
    TEST_ASSERT_EQUAL_UINT32(sizeof(data), r.data_len);
    TEST_ASSERT_EQUAL_MEMORY(data, r.data, sizeof(data));

    // empty read (EOF) -> NULL, still valid
    n = build_read_resp(m, NULL, 0);
    TEST_ASSERT_TRUE(pc_smb2_parse_read_response(m, n, &r));
    TEST_ASSERT_NULL(r.data);
    TEST_ASSERT_EQUAL_UINT32(0, r.data_len);

    // rejects
    n = build_read_resp(m, data, sizeof(data));
    uint8_t bad[512];
    memcpy(bad, m, n);
    w16(bad + 64, 16); // wrong StructureSize (must be 17)
    TEST_ASSERT_FALSE(pc_smb2_parse_read_response(bad, n, &r));
    memcpy(bad, m, n);
    w32(bad + 64 + 4, 9000); // DataLength past the message
    TEST_ASSERT_FALSE(pc_smb2_parse_read_response(bad, n, &r));
    TEST_ASSERT_FALSE(pc_smb2_parse_read_response(m, 70, &r)); // truncated before the body
}

void test_build_write()
{
    uint8_t fid[16];
    for (int i = 0; i < 16; i++)
    {
        fid[i] = (uint8_t)(i + 1);
    }
    const uint8_t data[] = "O0001 (PART)\r\n";
    uint8_t buf[256];
    size_t n = pc_smb2_build_write(buf, sizeof(buf), 6, 0xAAAA, 0x777, fid, data, sizeof(data), 0x800ULL);
    TEST_ASSERT_EQUAL_size_t(64 + 48 + sizeof(data), n);

    Smb2Header h;
    TEST_ASSERT_TRUE(pc_smb2_parse_header(buf, n, &h));
    TEST_ASSERT_EQUAL_UINT16(SMB2_WRITE, h.command);

    const uint8_t *b = buf + 64;
    TEST_ASSERT_EQUAL_UINT16(49, r16(b + 0));           // StructureSize
    TEST_ASSERT_EQUAL_UINT16(112, r16(b + 2));          // DataOffset
    TEST_ASSERT_EQUAL_UINT32(sizeof(data), r32(b + 4)); // Length
    TEST_ASSERT_EQUAL_HEX64(0x800ULL, r64(b + 8));      // Offset
    TEST_ASSERT_EQUAL_MEMORY(fid, b + 16, 16);          // FileId
    TEST_ASSERT_EQUAL_MEMORY(data, buf + 112, sizeof(data));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_write(buf, 100, 6, 0, 0, fid, data, sizeof(data), 0)); // overflow
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_write(buf, sizeof(buf), 6, 0, 0, fid, data, 0, 0));    // empty data
}

void test_parse_write_response()
{
    uint8_t m[128];
    pc_smb2_build_header(m, 128, SMB2_WRITE, 1, 6, 0x777, 0xAAAA);
    m[16] |= 0x01;
    uint8_t *b = m + 64;
    memset(b, 0, 16);
    w16(b + 0, 17);   // StructureSize
    w32(b + 4, 4096); // Count

    Smb2WriteResp r;
    TEST_ASSERT_TRUE(pc_smb2_parse_write_response(m, 64 + 16, &r));
    TEST_ASSERT_EQUAL_UINT32(4096, r.count);

    uint8_t bad[128];
    memcpy(bad, m, 64 + 16);
    w16(bad + 64, 16); // wrong StructureSize (must be 17)
    TEST_ASSERT_FALSE(pc_smb2_parse_write_response(bad, 64 + 16, &r));
    TEST_ASSERT_FALSE(pc_smb2_parse_write_response(m, 70, &r)); // truncated
}

// ---- fail-closed guards: the null / out-of-range / wrong-command sides the happy paths skip ----

void test_transport_rejects_null_and_oversize()
{
    const uint8_t msg[] = {1, 2, 3, 4, 5};
    uint8_t out[16];
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_transport_frame(NULL, sizeof(out), msg, sizeof(msg)));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_transport_frame(out, sizeof(out), NULL, sizeof(msg)));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_transport_frame(out, sizeof(out), msg, 0x01000000)); // past 24 bits
    TEST_ASSERT_EQUAL_UINT32(0, pc_smb2_transport_len(NULL, 4));
    uint8_t pre[4] = {0x00, 0x00, 0x00, 0x05};
    TEST_ASSERT_EQUAL_UINT32(0, pc_smb2_transport_len(pre, 3)); // shorter than the prefix
    TEST_ASSERT_EQUAL_UINT32(5, pc_smb2_transport_len(pre, 4));
}

void test_build_header_rejects()
{
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_header(NULL, 64, SMB2_NEGOTIATE, 1, 0, 0, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_header(buf, 63, SMB2_NEGOTIATE, 1, 0, 0, 0));
}

void test_parse_header_null_args()
{
    uint8_t buf[64];
    pc_smb2_build_header(buf, sizeof(buf), SMB2_NEGOTIATE, 1, 0, 0, 0);
    Smb2Header h;
    TEST_ASSERT_FALSE(pc_smb2_parse_header(NULL, 64, &h));
    TEST_ASSERT_FALSE(pc_smb2_parse_header(buf, 64, NULL));
}

void test_build_negotiate_null_args()
{
    uint8_t gid[16] = {0};
    uint8_t buf[160];
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_negotiate(NULL, sizeof(buf), gid, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_negotiate(buf, sizeof(buf), NULL, 0));
}

void test_parse_negotiate_response_null_and_low_offset()
{
    const uint8_t token[] = {1, 2, 3, 4};
    uint8_t m[256];
    size_t n = build_neg_resp(m, SMB2_DIALECT_0202, token, sizeof(token));
    Smb2NegotiateResp r;
    TEST_ASSERT_FALSE(pc_smb2_parse_negotiate_response(NULL, n, &r));
    TEST_ASSERT_FALSE(pc_smb2_parse_negotiate_response(m, n, NULL));

    uint8_t bad[256];
    memcpy(bad, m, n);
    bad[0] = 0x00; // broken ProtocolId -> the header parse fails
    TEST_ASSERT_FALSE(pc_smb2_parse_negotiate_response(bad, n, &r));

    memcpy(bad, m, n);
    w16(bad + 64 + 56, 10); // SecurityBufferOffset inside the 64-byte header, though still within the message
    TEST_ASSERT_FALSE(pc_smb2_parse_negotiate_response(bad, n, &r));
}

void test_build_session_setup_null_args()
{
    uint8_t tok[8] = {0};
    uint8_t buf[256];
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_session_setup(NULL, sizeof(buf), 1, 0, 0, tok, sizeof(tok)));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_session_setup(buf, sizeof(buf), 1, 0, 0, NULL, sizeof(tok)));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_session_setup(buf, sizeof(buf), 1, 0, 0, tok, 0x10000)); // > 0xFFFF
}

void test_parse_session_setup_null_and_low_offset()
{
    const uint8_t tok[] = {1, 2, 3, 4};
    uint8_t m[256];
    size_t n = build_ss_resp(m, 1, 0, 0, tok, sizeof(tok));
    Smb2SessionSetupResp r;
    TEST_ASSERT_FALSE(pc_smb2_parse_session_setup_response(NULL, n, &r));
    TEST_ASSERT_FALSE(pc_smb2_parse_session_setup_response(m, n, NULL));

    uint8_t bad[256];
    memcpy(bad, m, n);
    bad[0] = 0x00; // broken ProtocolId
    TEST_ASSERT_FALSE(pc_smb2_parse_session_setup_response(bad, n, &r));

    memcpy(bad, m, n);
    w16(bad + 64 + 4, 10); // SecurityBufferOffset inside the header
    TEST_ASSERT_FALSE(pc_smb2_parse_session_setup_response(bad, n, &r));
}

void test_build_tree_connect_null_args()
{
    const uint8_t path[8] = {0};
    uint8_t buf[128];
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_tree_connect(NULL, sizeof(buf), 2, 0, path, sizeof(path)));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_tree_connect(buf, sizeof(buf), 2, 0, NULL, sizeof(path)));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_tree_connect(buf, sizeof(buf), 2, 0, path, 0x10000)); // > 0xFFFF
}

void test_parse_tree_connect_null_and_command()
{
    uint8_t m[128];
    size_t n = build_tc_resp(m, 0x777, SMB2_SHARE_TYPE_DISK, 0x001f01ff);
    Smb2TreeConnectResp r;
    TEST_ASSERT_FALSE(pc_smb2_parse_tree_connect_response(NULL, n, &r));
    TEST_ASSERT_FALSE(pc_smb2_parse_tree_connect_response(m, n, NULL));

    uint8_t bad[128];
    memcpy(bad, m, n);
    bad[0] = 0x00; // broken ProtocolId
    TEST_ASSERT_FALSE(pc_smb2_parse_tree_connect_response(bad, n, &r));
    memcpy(bad, m, n);
    w16(bad + 12, (uint16_t)SMB2_CREATE); // wrong command
    TEST_ASSERT_FALSE(pc_smb2_parse_tree_connect_response(bad, n, &r));
}

void test_build_create_null_args()
{
    const uint8_t name[8] = {0};
    uint8_t buf[256];
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_create(NULL, sizeof(buf), 3, 0, 0, 0, 0, 0, 0, name, sizeof(name)));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_create(buf, sizeof(buf), 3, 0, 0, 0, 0, 0, 0, NULL, sizeof(name)));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_create(buf, sizeof(buf), 3, 0, 0, 0, 0, 0, 0, name, 0x10000));
}

void test_parse_create_null_and_command()
{
    uint8_t fid[16] = {0};
    uint8_t m[256];
    size_t n = build_create_resp(m, fid, 0x100ULL, 1);
    Smb2CreateResp r;
    TEST_ASSERT_FALSE(pc_smb2_parse_create_response(NULL, n, &r));
    TEST_ASSERT_FALSE(pc_smb2_parse_create_response(m, n, NULL));

    uint8_t bad[256];
    memcpy(bad, m, n);
    bad[0] = 0x00; // broken ProtocolId
    TEST_ASSERT_FALSE(pc_smb2_parse_create_response(bad, n, &r));
    memcpy(bad, m, n);
    w16(bad + 12, (uint16_t)SMB2_CLOSE); // wrong command
    TEST_ASSERT_FALSE(pc_smb2_parse_create_response(bad, n, &r));
}

// A well-formed CLOSE response message (header + the fixed 60-byte body).
static size_t build_close_resp(uint8_t *m, uint64_t eof)
{
    pc_smb2_build_header(m, 128, SMB2_CLOSE, 1, 4, 0x777, 0xAAAA);
    m[16] |= 0x01; // SERVER_TO_REDIR
    uint8_t *b = m + 64;
    memset(b, 0, 60);
    w16(b + 0, 60);    // StructureSize
    w64(b + 48, eof);  // EndofFile
    w32(b + 56, 0x80); // FileAttributes
    return 64 + 60;
}

void test_build_close_null_args()
{
    uint8_t fid[16] = {0};
    uint8_t buf[128];
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_close(NULL, sizeof(buf), 4, 0, 0, fid));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_close(buf, sizeof(buf), 4, 0, 0, NULL));
}

void test_parse_close_null_command_and_truncated()
{
    uint8_t m[128];
    size_t n = build_close_resp(m, 0x4000ULL);
    Smb2CloseResp r;
    TEST_ASSERT_FALSE(pc_smb2_parse_close_response(NULL, n, &r));
    TEST_ASSERT_FALSE(pc_smb2_parse_close_response(m, n, NULL));

    uint8_t bad[128];
    memcpy(bad, m, n);
    bad[0] = 0x00; // broken ProtocolId
    TEST_ASSERT_FALSE(pc_smb2_parse_close_response(bad, n, &r));
    memcpy(bad, m, n);
    w16(bad + 12, (uint16_t)SMB2_READ); // wrong command
    TEST_ASSERT_FALSE(pc_smb2_parse_close_response(bad, n, &r));
    TEST_ASSERT_FALSE(pc_smb2_parse_close_response(m, 64 + 59, &r)); // one byte short of the 60-byte body
}

void test_build_read_null_args()
{
    uint8_t fid[16] = {0};
    uint8_t buf[128];
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_read(NULL, sizeof(buf), 5, 0, 0, fid, 16, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_read(buf, sizeof(buf), 5, 0, 0, NULL, 16, 0));
}

void test_parse_read_null_command_and_low_offset()
{
    const uint8_t data[] = {9, 8, 7, 6};
    uint8_t m[512];
    size_t n = build_read_resp(m, data, sizeof(data));
    Smb2ReadResp r;
    TEST_ASSERT_FALSE(pc_smb2_parse_read_response(NULL, n, &r));
    TEST_ASSERT_FALSE(pc_smb2_parse_read_response(m, n, NULL));

    uint8_t bad[512];
    memcpy(bad, m, n);
    bad[0] = 0x00; // broken ProtocolId
    TEST_ASSERT_FALSE(pc_smb2_parse_read_response(bad, n, &r));
    memcpy(bad, m, n);
    w16(bad + 12, (uint16_t)SMB2_WRITE); // wrong command
    TEST_ASSERT_FALSE(pc_smb2_parse_read_response(bad, n, &r));
    memcpy(bad, m, n);
    bad[64 + 2] = 40; // DataOffset inside the 64-byte header, though the data still fits the message
    TEST_ASSERT_FALSE(pc_smb2_parse_read_response(bad, n, &r));
}

void test_build_write_null_args()
{
    uint8_t fid[16] = {0};
    const uint8_t data[8] = {0};
    uint8_t buf[256];
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_write(NULL, sizeof(buf), 6, 0, 0, fid, data, sizeof(data), 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_write(buf, sizeof(buf), 6, 0, 0, NULL, data, sizeof(data), 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_write(buf, sizeof(buf), 6, 0, 0, fid, NULL, sizeof(data), 0));
    size_t over32 = (size_t)0xFFFFFFFFULL;
    over32 += 1; // 0x1_0000_0000 on a 64-bit size_t; wraps to 0 (the empty-data guard) on a 32-bit one
    TEST_ASSERT_EQUAL_size_t(0, pc_smb2_build_write(buf, sizeof(buf), 6, 0, 0, fid, data, over32, 0));
}

void test_parse_write_null_and_command()
{
    uint8_t m[128];
    pc_smb2_build_header(m, 128, SMB2_WRITE, 1, 6, 0x777, 0xAAAA);
    m[16] |= 0x01;
    memset(m + 64, 0, 16);
    w16(m + 64, 17);       // StructureSize
    w32(m + 64 + 4, 4096); // Count
    Smb2WriteResp r;
    TEST_ASSERT_FALSE(pc_smb2_parse_write_response(NULL, 64 + 16, &r));
    TEST_ASSERT_FALSE(pc_smb2_parse_write_response(m, 64 + 16, NULL));

    uint8_t bad[128];
    memcpy(bad, m, 64 + 16);
    bad[0] = 0x00; // broken ProtocolId
    TEST_ASSERT_FALSE(pc_smb2_parse_write_response(bad, 64 + 16, &r));
    memcpy(bad, m, 64 + 16);
    w16(bad + 12, (uint16_t)SMB2_READ); // wrong command
    TEST_ASSERT_FALSE(pc_smb2_parse_write_response(bad, 64 + 16, &r));
}

// SMB2 message signing (MS-SMB2 §3.1.4.1 / §3.1.5.1): sign sets the SIGNED flag + writes the HMAC-SHA256
// signature; the signature matches a Python-computed reference; verify accepts it, leaves the message
// unchanged, and rejects tampering / a wrong key / a short message.
void test_smb2_signing()
{
    uint8_t msg[72];
    memset(msg, 0, sizeof(msg));
    msg[0] = 0xFE;
    msg[1] = 'S';
    msg[2] = 'M';
    msg[3] = 'B';   // ProtocolId
    msg[4] = 64;    // StructureSize (LE)
    msg[12] = 5;    // Command (LE)
    msg[14] = 1;    // CreditRequest (LE)
    msg[24] = 1;    // MessageId (LE)
    msg[36] = 5;    // TreeId (LE)
    msg[40] = 7;    // SessionId (LE)
    msg[64] = 0xDE; // body
    msg[65] = 0xAD;
    msg[66] = 0xBE;
    msg[67] = 0xEF;
    msg[68] = 0x01;
    msg[69] = 0x02;
    msg[70] = 0x03;
    msg[71] = 0x04;

    uint8_t key[16];
    for (int i = 0; i < 16; i++)
    {
        key[i] = (uint8_t)(i + 1); // 01..10
    }

    pc_smb2_sign(key, msg, sizeof(msg));
    TEST_ASSERT_EQUAL_HEX8(0x08, msg[16]); // SMB2_FLAGS_SIGNED now set

    // The Signature matches the reference HMAC-SHA256(key, message)[:16] (Python hashlib/hmac).
    const uint8_t expect[16] = {0xf7, 0xc0, 0xb1, 0x28, 0x7f, 0x6f, 0x6c, 0xd2,
                                0xaa, 0xbf, 0x30, 0x48, 0xa3, 0x1d, 0x16, 0xa7};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, msg + 48, 16);

    // Verify accepts the freshly-signed message and restores it unchanged.
    uint8_t before[72];
    memcpy(before, msg, sizeof(msg));
    TEST_ASSERT_TRUE(pc_smb2_verify(key, msg, sizeof(msg)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, msg, sizeof(msg));

    // A tampered body byte, a wrong key, and a too-short message all fail closed.
    msg[70] ^= 0x01;
    TEST_ASSERT_FALSE(pc_smb2_verify(key, msg, sizeof(msg)));
    msg[70] ^= 0x01;
    uint8_t wrong[16];
    memcpy(wrong, key, 16);
    wrong[0] ^= 0xFF;
    TEST_ASSERT_FALSE(pc_smb2_verify(wrong, msg, sizeof(msg)));
    TEST_ASSERT_FALSE(pc_smb2_verify(key, msg, 63));
    pc_smb2_sign(key, msg, 63); // too short: a no-op, must not corrupt memory
}

// SMB 3.x AES-128-CMAC signing (MS-SMB2 §3.1.4.1): same framing as HMAC signing, but the Signature is
// the full AES-CMAC tag. The reference tag is impacket's crypto.AES_CMAC over the same message + key.
void test_smb2_signing_cmac()
{
    uint8_t msg[72];
    memset(msg, 0, sizeof(msg));
    msg[0] = 0xFE;
    msg[1] = 'S';
    msg[2] = 'M';
    msg[3] = 'B';
    msg[4] = 64;
    msg[12] = 5;
    msg[14] = 1;
    msg[24] = 1;
    msg[36] = 5;
    msg[40] = 7;
    msg[64] = 0xDE;
    msg[65] = 0xAD;
    msg[66] = 0xBE;
    msg[67] = 0xEF;
    msg[68] = 0x01;
    msg[69] = 0x02;
    msg[70] = 0x03;
    msg[71] = 0x04;

    uint8_t key[16];
    for (int i = 0; i < 16; i++)
    {
        key[i] = (uint8_t)(i + 1); // 01..10
    }

    pc_smb2_sign_cmac(key, msg, sizeof(msg));
    TEST_ASSERT_EQUAL_HEX8(0x08, msg[16]); // SMB2_FLAGS_SIGNED set

    // The Signature matches the reference AES-CMAC(key, message) (impacket crypto.AES_CMAC).
    const uint8_t expect[16] = {0xac, 0x3a, 0x6f, 0x5c, 0xce, 0x50, 0x05, 0x84,
                                0x6d, 0x09, 0xcf, 0xa6, 0x43, 0x3c, 0x02, 0x1f};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, msg + 48, 16);

    // Verify accepts the freshly-signed message and restores it unchanged.
    uint8_t before[72];
    memcpy(before, msg, sizeof(msg));
    TEST_ASSERT_TRUE(pc_smb2_verify_cmac(key, msg, sizeof(msg)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, msg, sizeof(msg));

    // A CMAC signature must not verify under the HMAC verifier (the two MACs are distinct).
    TEST_ASSERT_FALSE(pc_smb2_verify(key, msg, sizeof(msg)));

    // A tampered body byte, a wrong key, and a too-short message all fail closed.
    msg[70] ^= 0x01;
    TEST_ASSERT_FALSE(pc_smb2_verify_cmac(key, msg, sizeof(msg)));
    msg[70] ^= 0x01;
    uint8_t wrong[16];
    memcpy(wrong, key, 16);
    wrong[0] ^= 0xFF;
    TEST_ASSERT_FALSE(pc_smb2_verify_cmac(wrong, msg, sizeof(msg)));
    TEST_ASSERT_FALSE(pc_smb2_verify_cmac(key, msg, 63));
    pc_smb2_sign_cmac(key, msg, 63); // too short: a no-op
}

// ---- SMB 3.x transport encryption (TRANSFORM_HEADER + AES-128/256-GCM and AES-128/256-CCM) --------------

// Known-answer vectors from pyca/cryptography (AES-CCM tag_length=16, AES-256-GCM), SMB nonce lengths
// (11 CCM / 12 GCM), 32-byte AAD, 50-byte payload - an independent reference for the CCM + 256-GCM crypto.
static const uint8_t kat_ccm128_key[16] = {0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
                                           0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f};
static const uint8_t kat_ccm128_nonce[11] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a};
static const uint8_t kat_ccm128_aad[32] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a,
                                           0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95,
                                           0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f};
static const uint8_t kat_ccm128_pt[50] = {0x03, 0x0a, 0x11, 0x18, 0x1f, 0x26, 0x2d, 0x34, 0x3b, 0x42, 0x49, 0x50, 0x57,
                                          0x5e, 0x65, 0x6c, 0x73, 0x7a, 0x81, 0x88, 0x8f, 0x96, 0x9d, 0xa4, 0xab, 0xb2,
                                          0xb9, 0xc0, 0xc7, 0xce, 0xd5, 0xdc, 0xe3, 0xea, 0xf1, 0xf8, 0xff, 0x06, 0x0d,
                                          0x14, 0x1b, 0x22, 0x29, 0x30, 0x37, 0x3e, 0x45, 0x4c, 0x53, 0x5a};
static const uint8_t kat_ccm128_ct[50] = {0xf5, 0xf9, 0xb8, 0x20, 0x1f, 0xbb, 0x50, 0x5c, 0xe8, 0x8b, 0xfa, 0xf3, 0x72,
                                          0xa9, 0xfd, 0x6d, 0x76, 0x09, 0x39, 0x87, 0x71, 0xe2, 0x16, 0x5b, 0x13, 0x53,
                                          0x83, 0xff, 0x4b, 0xc9, 0xf3, 0x00, 0xdd, 0x66, 0x94, 0xe3, 0x10, 0x6c, 0x5f,
                                          0xe8, 0xb5, 0xd0, 0x2f, 0x59, 0xf7, 0xe3, 0x68, 0x06, 0x9b, 0xd7};
static const uint8_t kat_ccm128_tag[16] = {0xb9, 0xf5, 0x12, 0x3c, 0x6d, 0x5e, 0xf5, 0x7f,
                                           0x2f, 0x2c, 0x3a, 0xbf, 0x06, 0x45, 0xbe, 0x62};

static const uint8_t kat_ccm256_key[32] = {0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
                                           0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55,
                                           0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f};
static const uint8_t kat_ccm256_nonce[11] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a};
static const uint8_t kat_ccm256_aad[32] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a,
                                           0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95,
                                           0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f};
static const uint8_t kat_ccm256_pt[50] = {0x03, 0x0a, 0x11, 0x18, 0x1f, 0x26, 0x2d, 0x34, 0x3b, 0x42, 0x49, 0x50, 0x57,
                                          0x5e, 0x65, 0x6c, 0x73, 0x7a, 0x81, 0x88, 0x8f, 0x96, 0x9d, 0xa4, 0xab, 0xb2,
                                          0xb9, 0xc0, 0xc7, 0xce, 0xd5, 0xdc, 0xe3, 0xea, 0xf1, 0xf8, 0xff, 0x06, 0x0d,
                                          0x14, 0x1b, 0x22, 0x29, 0x30, 0x37, 0x3e, 0x45, 0x4c, 0x53, 0x5a};
static const uint8_t kat_ccm256_ct[50] = {0x26, 0x95, 0x7c, 0x92, 0xbe, 0x2b, 0x92, 0x21, 0xaf, 0x93, 0x47, 0x0b, 0xe1,
                                          0x3a, 0x1b, 0x9e, 0x3a, 0x44, 0x15, 0x92, 0xeb, 0x1c, 0x14, 0xc3, 0x3b, 0x63,
                                          0x67, 0x02, 0x70, 0xaa, 0x65, 0xb2, 0x7f, 0xf4, 0x4d, 0x5a, 0x69, 0x27, 0xb6,
                                          0x1a, 0x2b, 0xa7, 0x20, 0xeb, 0x72, 0x29, 0xa5, 0x61, 0x76, 0x5c};
static const uint8_t kat_ccm256_tag[16] = {0x0d, 0xe9, 0x4d, 0x7d, 0xae, 0x97, 0x66, 0x24,
                                           0x7a, 0xa5, 0xb4, 0x0b, 0x61, 0x18, 0xe8, 0x2c};

static const uint8_t kat_gcm256_key[32] = {0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
                                           0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55,
                                           0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f};
static const uint8_t kat_gcm256_nonce[12] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b};
static const uint8_t kat_gcm256_aad[32] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a,
                                           0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95,
                                           0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f};
static const uint8_t kat_gcm256_pt[50] = {0x03, 0x0a, 0x11, 0x18, 0x1f, 0x26, 0x2d, 0x34, 0x3b, 0x42, 0x49, 0x50, 0x57,
                                          0x5e, 0x65, 0x6c, 0x73, 0x7a, 0x81, 0x88, 0x8f, 0x96, 0x9d, 0xa4, 0xab, 0xb2,
                                          0xb9, 0xc0, 0xc7, 0xce, 0xd5, 0xdc, 0xe3, 0xea, 0xf1, 0xf8, 0xff, 0x06, 0x0d,
                                          0x14, 0x1b, 0x22, 0x29, 0x30, 0x37, 0x3e, 0x45, 0x4c, 0x53, 0x5a};
static const uint8_t kat_gcm256_ct[50] = {0x7a, 0x30, 0x2c, 0xc9, 0x49, 0xb7, 0x39, 0x17, 0x95, 0xe4, 0x9f, 0x00, 0xbc,
                                          0xa6, 0x6a, 0xda, 0x76, 0x6d, 0x05, 0x41, 0x89, 0x87, 0xd7, 0xaf, 0xd3, 0xdc,
                                          0x70, 0x11, 0x1c, 0xa7, 0x67, 0x0c, 0x00, 0x1e, 0xe3, 0x03, 0x93, 0xf5, 0x4e,
                                          0xeb, 0x29, 0x42, 0xc8, 0xae, 0x1b, 0xab, 0xe0, 0xd8, 0x01, 0x21};
static const uint8_t kat_gcm256_tag[16] = {0x1e, 0xad, 0x4c, 0x0a, 0x8b, 0x61, 0x4c, 0x40,
                                           0x12, 0x9d, 0xa7, 0x46, 0x59, 0xcb, 0xdb, 0xe3};

// KAT the CCM (128/256) and detached 256-GCM primitives against the independent reference vectors, so the new
// crypto is verified beyond a self-consistent round trip (a KAT vs one's own round trip proves only that).
void test_smb3_cipher_kat()
{
    uint8_t ct[64];
    uint8_t tag[16];
    uint8_t pt[64];

    TEST_ASSERT_TRUE(
        pc_aesccm_seal_tag(kat_ccm128_key, 16, kat_ccm128_nonce, 11, kat_ccm128_aad, 32, kat_ccm128_pt, 50, ct, tag));
    TEST_ASSERT_EQUAL_MEMORY(kat_ccm128_ct, ct, 50);
    TEST_ASSERT_EQUAL_MEMORY(kat_ccm128_tag, tag, 16);
    TEST_ASSERT_TRUE(pc_aesccm_open_tag(kat_ccm128_key, 16, kat_ccm128_nonce, 11, kat_ccm128_aad, 32, kat_ccm128_ct, 50,
                                        kat_ccm128_tag, pt));
    TEST_ASSERT_EQUAL_MEMORY(kat_ccm128_pt, pt, 50);

    TEST_ASSERT_TRUE(
        pc_aesccm_seal_tag(kat_ccm256_key, 32, kat_ccm256_nonce, 11, kat_ccm256_aad, 32, kat_ccm256_pt, 50, ct, tag));
    TEST_ASSERT_EQUAL_MEMORY(kat_ccm256_ct, ct, 50);
    TEST_ASSERT_EQUAL_MEMORY(kat_ccm256_tag, tag, 16);
    TEST_ASSERT_TRUE(pc_aesccm_open_tag(kat_ccm256_key, 32, kat_ccm256_nonce, 11, kat_ccm256_aad, 32, kat_ccm256_ct, 50,
                                        kat_ccm256_tag, pt));
    TEST_ASSERT_EQUAL_MEMORY(kat_ccm256_pt, pt, 50);

    pc_aesgcm_seal(gcm_key(kat_gcm256_key), kat_gcm256_nonce, kat_gcm256_aad, 32, kat_gcm256_pt, 50, ct, tag);
    TEST_ASSERT_EQUAL_MEMORY(kat_gcm256_ct, ct, 50);
    TEST_ASSERT_EQUAL_MEMORY(kat_gcm256_tag, tag, 16);
    TEST_ASSERT_TRUE(pc_aesgcm_open(gcm_key(kat_gcm256_key), kat_gcm256_nonce, kat_gcm256_aad, 32, kat_gcm256_ct, 50,
                                    kat_gcm256_tag, pt));
    TEST_ASSERT_EQUAL_MEMORY(kat_gcm256_pt, pt, 50);

    // A flipped tag byte fails closed (CCM open recomputes the MAC over the plaintext).
    uint8_t bad[16];
    memcpy(bad, kat_ccm128_tag, 16);
    bad[0] ^= 0x01;
    TEST_ASSERT_FALSE(
        pc_aesccm_open_tag(kat_ccm128_key, 16, kat_ccm128_nonce, 11, kat_ccm128_aad, 32, kat_ccm128_ct, 50, bad, pt));
}

void test_smb3_derive_encryption_keys()
{
    uint8_t sk[16];
    for (int i = 0; i < 16; i++)
    {
        sk[i] = (uint8_t)(0x10 + i);
    }
    uint8_t preauth[64];
    for (int i = 0; i < 64; i++)
    {
        preauth[i] = (uint8_t)i;
    }

    uint8_t c2s[32] = {0}, s2c[32] = {0}, c2s2[32] = {0}, s2c2[32] = {0};
    // 128-bit cipher keys (AES-128 ciphers).
    TEST_ASSERT_TRUE(pc_smb3_derive_encryption_keys(sk, 0x0311, preauth, 16, c2s, s2c));
    TEST_ASSERT_TRUE(pc_smb3_derive_encryption_keys(sk, 0x0311, preauth, 16, c2s2, s2c2)); // deterministic
    TEST_ASSERT_EQUAL_MEMORY(c2s, c2s2, 16);
    TEST_ASSERT_EQUAL_MEMORY(s2c, s2c2, 16);
    TEST_ASSERT_TRUE(memcmp(c2s, s2c, 16) != 0); // the two directions use different labels

    // 256-bit cipher keys (AES-256 ciphers): 32 bytes, distinct directions, and different from the 128-bit key
    // (the [L] length field differs, so the whole KDF output differs).
    uint8_t c2s256[32] = {0}, s2c256[32] = {0};
    TEST_ASSERT_TRUE(pc_smb3_derive_encryption_keys(sk, 0x0311, preauth, 32, c2s256, s2c256));
    TEST_ASSERT_TRUE(memcmp(c2s256, s2c256, 32) != 0);
    TEST_ASSERT_TRUE(memcmp(c2s256, c2s, 16) != 0);

    // 3.1.1 requires the preauth hash; an invalid key length fails closed.
    TEST_ASSERT_FALSE(pc_smb3_derive_encryption_keys(sk, 0x0311, NULL, 16, c2s, s2c));
    TEST_ASSERT_FALSE(pc_smb3_derive_encryption_keys(sk, 0x0311, preauth, 24, c2s, s2c));
    // 3.0.2 uses the fixed contexts (no preauth) and still derives distinct keys.
    TEST_ASSERT_TRUE(pc_smb3_derive_encryption_keys(sk, 0x0302, NULL, 16, c2s, s2c));
    TEST_ASSERT_TRUE(memcmp(c2s, s2c, 16) != 0);
}

void test_smb3_encrypt_decrypt_roundtrip()
{
    uint8_t sk[16];
    for (int i = 0; i < 16; i++)
    {
        sk[i] = (uint8_t)(0xA0 + i);
    }
    uint8_t preauth[64] = {0};

    // Every SMB 3.1.1 cipher must round-trip through the TRANSFORM_HEADER codec.
    const uint16_t ciphers[4] = {SMB2_ENCRYPTION_AES128_GCM, SMB2_ENCRYPTION_AES256_GCM, SMB2_ENCRYPTION_AES128_CCM,
                                 SMB2_ENCRYPTION_AES256_CCM};
    for (int ci = 0; ci < 4; ci++)
    {
        const uint16_t cipher = ciphers[ci];
        const size_t klen = pc_smb2_cipher_key_len(cipher);
        uint8_t c2s[32], s2c[32];
        TEST_ASSERT_TRUE(pc_smb3_derive_encryption_keys(sk, 0x0311, preauth, klen, c2s, s2c));

        uint8_t msg[100];
        for (int i = 0; i < 100; i++)
        {
            msg[i] = (uint8_t)(i * 7 + 3);
        }
        uint8_t nonce[PC_SMB2_NONCE_FIELD_LEN] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 0, 0, 0};
        const uint64_t sid = 0x0011223344556677ULL;

        uint8_t enc[PC_SMB2_TRANSFORM_HDR_LEN + sizeof(msg)];
        size_t elen = pc_smb2_encrypt(cipher, c2s, nonce, sid, msg, sizeof(msg), enc, sizeof(enc));
        TEST_ASSERT_EQUAL_UINT32(PC_SMB2_TRANSFORM_HDR_LEN + sizeof(msg), elen);
        TEST_ASSERT_EQUAL_HEX8(0xFD, enc[0]);
        TEST_ASSERT_EQUAL_HEX8('S', enc[1]);
        TEST_ASSERT_EQUAL_HEX8('M', enc[2]);
        TEST_ASSERT_EQUAL_HEX8('B', enc[3]);
        TEST_ASSERT_EQUAL_MEMORY(nonce, enc + 20, PC_SMB2_NONCE_FIELD_LEN); // full 16-byte Nonce field
        TEST_ASSERT_EQUAL_HEX8(0x01, enc[42]);                              // Flags = Encrypted
        TEST_ASSERT_TRUE(memcmp(enc + PC_SMB2_TRANSFORM_HDR_LEN, msg, sizeof(msg)) != 0);

        uint8_t dec[sizeof(msg)];
        size_t dlen = pc_smb2_decrypt(cipher, c2s, enc, elen, dec, sizeof(dec));
        TEST_ASSERT_EQUAL_UINT32(sizeof(msg), dlen);
        TEST_ASSERT_EQUAL_MEMORY(msg, dec, sizeof(msg));

        // Decrypting with a different cipher id must fail (different key length / nonce length / construction).
        const uint16_t other =
            (cipher == SMB2_ENCRYPTION_AES128_GCM) ? SMB2_ENCRYPTION_AES128_CCM : SMB2_ENCRYPTION_AES128_GCM;
        TEST_ASSERT_EQUAL_UINT32(0, pc_smb2_decrypt(other, c2s, enc, elen, dec, sizeof(dec)));
    }
}

void test_smb3_decrypt_rejects_tamper()
{
    const uint16_t cipher = SMB2_ENCRYPTION_AES128_GCM;
    uint8_t sk[16] = {0};
    uint8_t preauth[64] = {0};
    uint8_t c2s[32], s2c[32];
    pc_smb3_derive_encryption_keys(sk, 0x0311, preauth, 16, c2s, s2c);

    uint8_t msg[40];
    memset(msg, 0x5A, sizeof(msg));
    uint8_t nonce[PC_SMB2_NONCE_FIELD_LEN] = {9, 9, 9, 9, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    uint8_t enc[PC_SMB2_TRANSFORM_HDR_LEN + sizeof(msg)];
    size_t elen = pc_smb2_encrypt(cipher, c2s, nonce, 42, msg, sizeof(msg), enc, sizeof(enc));
    uint8_t dec[sizeof(msg)];

    // Flip a ciphertext byte -> tag mismatch.
    uint8_t t1[sizeof(enc)];
    memcpy(t1, enc, elen);
    t1[PC_SMB2_TRANSFORM_HDR_LEN + 5] ^= 0x01;
    TEST_ASSERT_EQUAL_UINT32(0, pc_smb2_decrypt(cipher, c2s, t1, elen, dec, sizeof(dec)));
    // Flip a Signature (tag) byte.
    memcpy(t1, enc, elen);
    t1[4] ^= 0x01;
    TEST_ASSERT_EQUAL_UINT32(0, pc_smb2_decrypt(cipher, c2s, t1, elen, dec, sizeof(dec)));
    // Flip an AAD byte (SessionId, part of the header covered by the AEAD).
    memcpy(t1, enc, elen);
    t1[44] ^= 0x01;
    TEST_ASSERT_EQUAL_UINT32(0, pc_smb2_decrypt(cipher, c2s, t1, elen, dec, sizeof(dec)));
    // Wrong key.
    uint8_t wrong[32];
    memcpy(wrong, c2s, 32);
    wrong[0] ^= 0xFF;
    TEST_ASSERT_EQUAL_UINT32(0, pc_smb2_decrypt(cipher, wrong, enc, elen, dec, sizeof(dec)));
    // Bad ProtocolId, short input, and OriginalMessageSize mismatch.
    memcpy(t1, enc, elen);
    t1[0] ^= 0x01;
    TEST_ASSERT_EQUAL_UINT32(0, pc_smb2_decrypt(cipher, c2s, t1, elen, dec, sizeof(dec)));
    TEST_ASSERT_EQUAL_UINT32(0, pc_smb2_decrypt(cipher, c2s, enc, PC_SMB2_TRANSFORM_HDR_LEN - 1, dec, sizeof(dec)));
    TEST_ASSERT_EQUAL_UINT32(0, pc_smb2_decrypt(cipher, c2s, enc, elen, dec, sizeof(dec) - 1)); // out too small
    // An unrecognized cipher id fails closed (encrypt and decrypt).
    TEST_ASSERT_EQUAL_UINT32(0, pc_smb2_encrypt(0x00FF, c2s, nonce, 42, msg, sizeof(msg), enc, sizeof(enc)));
    TEST_ASSERT_EQUAL_UINT32(0, pc_smb2_decrypt(0x00FF, c2s, enc, elen, dec, sizeof(dec)));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_transport_frame);
    RUN_TEST(test_build_and_parse_header);
    RUN_TEST(test_parse_header_rejects);
    RUN_TEST(test_build_negotiate);
    RUN_TEST(test_parse_negotiate_response);
    RUN_TEST(test_parse_negotiate_response_rejects);
    RUN_TEST(test_build_negotiate_311);
    RUN_TEST(test_parse_negotiate_contexts);
    RUN_TEST(test_parse_negotiate_contexts_rejects);
    RUN_TEST(test_preauth_hash_chain);
    RUN_TEST(test_build_session_setup);
    RUN_TEST(test_parse_session_setup_response);
    RUN_TEST(test_session_setup_rejects);
    RUN_TEST(test_session_setup_spnego_flow);
    RUN_TEST(test_build_tree_connect);
    RUN_TEST(test_parse_tree_connect_response);
    RUN_TEST(test_build_create);
    RUN_TEST(test_parse_create_response);
    RUN_TEST(test_close_roundtrip);
    RUN_TEST(test_build_read);
    RUN_TEST(test_parse_read_response);
    RUN_TEST(test_build_write);
    RUN_TEST(test_parse_write_response);
    RUN_TEST(test_transport_rejects_null_and_oversize);
    RUN_TEST(test_build_header_rejects);
    RUN_TEST(test_parse_header_null_args);
    RUN_TEST(test_build_negotiate_null_args);
    RUN_TEST(test_parse_negotiate_response_null_and_low_offset);
    RUN_TEST(test_build_session_setup_null_args);
    RUN_TEST(test_parse_session_setup_null_and_low_offset);
    RUN_TEST(test_build_tree_connect_null_args);
    RUN_TEST(test_parse_tree_connect_null_and_command);
    RUN_TEST(test_build_create_null_args);
    RUN_TEST(test_parse_create_null_and_command);
    RUN_TEST(test_build_close_null_args);
    RUN_TEST(test_parse_close_null_command_and_truncated);
    RUN_TEST(test_build_read_null_args);
    RUN_TEST(test_parse_read_null_command_and_low_offset);
    RUN_TEST(test_build_write_null_args);
    RUN_TEST(test_parse_write_null_and_command);
    RUN_TEST(test_smb2_signing);
    RUN_TEST(test_smb2_signing_cmac);
    RUN_TEST(test_smb3_cipher_kat);
    RUN_TEST(test_smb3_derive_encryption_keys);
    RUN_TEST(test_smb3_encrypt_decrypt_roundtrip);
    RUN_TEST(test_smb3_decrypt_rejects_tamper);
    return UNITY_END();
}
