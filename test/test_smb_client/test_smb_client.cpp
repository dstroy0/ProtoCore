// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the SMB2 client dialogue engine (network_drivers/application/smb/smb_client): smb_open drives the
// full NEGOTIATE -> two-round NTLMv2 SESSION_SETUP -> TREE_CONNECT -> CREATE handshake, and
// smb_close releases the handle. Exercised end to end on the host with a scripted mock SMB2 server
// (a send/recv seam), so no lwIP or real share is needed.

#include "crypto/hash/md.h"
#include "network_drivers/application/smb/ntlm.h"
#include "network_drivers/application/smb/ntlmssp.h"
#include "network_drivers/application/smb/smb2.h"
#include "network_drivers/application/smb/smb_client.h"
#include "network_drivers/application/smb/spnego.h"
#include <stdio.h>
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static void w16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void w32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
    {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}
static void w64(uint8_t *p, uint64_t v)
{
    w32(p, (uint32_t)v);
    w32(p + 4, (uint32_t)(v >> 32));
}
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

// A minimal NTLMSSP CHALLENGE (server type-2) with a timestamp+EOL target info.
static size_t pc_ntlmssp_challenge(uint8_t *m, const uint8_t sc[8])
{
    memset(m, 0, 64);
    const uint8_t sig[8] = {'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};
    memcpy(m, sig, 8);
    w32(m + 8, 2); // CHALLENGE
    w32(m + 20, NtlmsspFlags::NTLMSSP_NEGOTIATE_UNICODE | NtlmsspFlags::NTLMSSP_NEGOTIATE_NTLM |
                    NtlmsspFlags::NTLMSSP_NEGOTIATE_TARGET_INFO);
    memcpy(m + 24, sc, 8);
    // target info at 48: MsvAvTimestamp(7, 8 bytes) + MsvAvEOL
    uint8_t *ti = m + 48;
    w16(ti + 0, 7);
    w16(ti + 2, 8);
    w64(ti + 4, 0x01D000000000ULL); // a FILETIME
    w16(ti + 12, 0);
    w16(ti + 14, 0);
    uint16_t ti_len = 16;
    w16(m + 40, ti_len);
    w16(m + 42, ti_len);
    w32(m + 44, 48);
    return 48 + ti_len;
}

// Same CHALLENGE, but with a caller-supplied target-info blob (exercises find_av_timestamp's
// scan/skip/EOL branches and large target infos that overflow the client's crypto buffers).
static size_t pc_ntlmssp_challenge_ti(uint8_t *m, const uint8_t sc[8], const uint8_t *ti, size_t ti_len)
{
    memset(m, 0, 48);
    const uint8_t sig[8] = {'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};
    memcpy(m, sig, 8);
    w32(m + 8, 2); // CHALLENGE
    w32(m + 20, NtlmsspFlags::NTLMSSP_NEGOTIATE_UNICODE | NtlmsspFlags::NTLMSSP_NEGOTIATE_NTLM |
                    NtlmsspFlags::NTLMSSP_NEGOTIATE_TARGET_INFO);
    memcpy(m + 24, sc, 8);
    memcpy(m + 48, ti, ti_len);
    w16(m + 40, (uint16_t)ti_len);
    w16(m + 42, (uint16_t)ti_len);
    w32(m + 44, 48);
    return 48 + ti_len;
}

// Fault the mock injects on a chosen request number (1-based over the open handshake).
enum MockFault
{
    FAULT_NONE = 0,
    FAULT_DROP,       // append no response (the peer closes mid-handshake)
    FAULT_BAD_HEADER, // corrupt the response ProtocolId so pc_smb2_parse_header fails
    FAULT_BAD_BODY,   // corrupt the response body StructureSize so the body parser fails
};

// How the round-1 SESSION_SETUP security buffer is shaped, to drive the client's decode rejects.
enum SsSecBufMode
{
    SSBUF_NORMAL = 0, // a valid SPNEGO-wrapped NTLMSSP CHALLENGE
    SSBUF_EMPTY,      // no security buffer at all
    SSBUF_RAW_JUNK,   // bytes that are not a SPNEGO NegTokenResp
    SSBUF_SPNEGO_JUNK // a valid SPNEGO wrap whose inner token is not an NTLMSSP CHALLENGE
};

// The scripted mock SMB2 server: on each client request it appends the matching framed response.
struct Mock
{
    uint8_t rx[8192];
    size_t rx_len, rx_pos;
    int ss_round;
    uint64_t session_id;
    uint32_t tree_id;
    uint8_t file_id[16];
    uint64_t file_size;
    uint32_t auth_status; // status for the 2nd SESSION_SETUP (SUCCESS or a logon failure)
    uint32_t tc_status;   // TREE_CONNECT status
    uint32_t create_status;
    bool cut_after_negotiate; // simulate the peer closing mid-handshake
    int req_count;
    uint8_t file_data[8192]; // the "file" backing READ / WRITE
    size_t file_data_len;
    uint32_t ss1_status;    // status for the 1st SESSION_SETUP (default MORE_PROCESSING_REQUIRED)
    int fault_at_req;       // 1-based request number at which to inject fault_kind (0 = none)
    int fault_kind;         // MockFault
    int ss1_secbuf_mode;    // SsSecBufMode for the round-1 security buffer
    const uint8_t *chal_ti; // optional custom target-info for the round-1 CHALLENGE (null => default)
    size_t chal_ti_len;
    // Signing reference-peer state (MS-SMB2 §3.1.4.1 / §3.1.5.1). When require_signing is set the mock
    // advertises SIGNING_REQUIRED, derives the same NTLMv2 session key from the client's AUTHENTICATE,
    // signs every response, and verifies every signed request - so the whole session runs signed.
    bool require_signing;   // NEGOTIATE advertises SMB2_NEGOTIATE_SIGNING_REQUIRED and the mock signs
    bool signing;           // set once the session key has been derived (round-2 onward)
    uint8_t sign_key[16];   // the derived SMB 2.x signing key
    int bad_req_sigs;       // count of client requests that arrived unsigned or wrongly signed
    bool corrupt_read_sig;  // flip a byte of the signature on the READ response (tamper in transit)
    const SmbConfig *creds; // the credentials the mock uses to re-derive the session key
    // SMB 3.1.1 reference-peer state. When require_311 is set the mock answers NEGOTIATE with dialect
    // 0x0311 + preauth-integrity (SHA-512) and AES-CMAC signing contexts, maintains the same preauth-
    // integrity hash chain the client does, derives the SP800-108 signing key, and signs/verifies the
    // post-auth traffic with AES-CMAC - so the whole 3.1.1 session runs signed end to end.
    bool require_311;       // NEGOTIATE advertises 3.1.1 + SIGNING_REQUIRED and the mock signs with AES-CMAC
    Smb2SignAlgo sign_algo; // the signing algorithm in force once signing begins (CMAC for 3.1.1)
    SmbPreauth preauth;     // the running preauth-integrity hash (seeded on the NEGOTIATE request)
    // SMB 3.1.1 encryption reference-peer state. When require_encrypt is set the mock offers @ref cipher (any of
    // the four SMB 3.1.1 ciphers; defaults to AES-128-GCM), derives the same C2S/S2C cipher keys, flags the
    // session encrypt-required, DECRYPTS every TRANSFORM-wrapped request and ENCRYPTS every response - so the
    // post-auth session runs encrypted end to end.
    bool require_encrypt;
    // Model a share (not the global server) that requires encryption, like Samba `smb encrypt = required` on a
    // share: the mock negotiates a cipher + derives keys but does NOT set the session ENCRYPT_DATA flag, and
    // rejects an unencrypted TREE_CONNECT with ACCESS_DENIED. Only a client that forces encryption (cfg.encrypt)
    // can then reach the share - the exact case that needs client-forced encryption.
    bool encrypt_share_only;
    uint16_t cipher; // the Smb2Cipher to negotiate (0 => AES-128-GCM); selects key + nonce length
    bool enc_keys;   // cipher keys derived (round-2 onward)
    uint8_t enc_c2s[PC_SMB2_MAX_CIPHER_KEY_LEN]; // client->server key: the mock decrypts requests with it
    uint8_t enc_s2c[PC_SMB2_MAX_CIPHER_KEY_LEN]; // server->client key: the mock encrypts responses with it
    uint64_t enc_nonce;                          // the mock's monotonic response nonce
};

static void append_frame(Mock *m, const uint8_t *resp, size_t rlen)
{
    m->rx_len += pc_smb2_transport_frame(m->rx + m->rx_len, sizeof(m->rx) - m->rx_len, resp, rlen);
}

// Re-derive the SMB 2.x signing key (the NTLMv2 SessionBaseKey) from the client's round-2 SESSION_SETUP
// request the way a real server would: unwrap the SPNEGO AUTHENTICATE, take the NTProofStr (the first
// 16 bytes of the NtChallengeResponse), and HMAC-MD5 it under NTOWFv2 computed from the known creds.
static bool mock_derive_key(const uint8_t *msg, size_t mlen, const SmbConfig *cfg, uint8_t key[16])
{
    if (mlen < 88)
    {
        return false;
    }
    uint16_t sec_off = rd16(msg + 76); // SESSION_SETUP SecurityBufferOffset / Length
    uint16_t sec_len = rd16(msg + 78);
    if ((size_t)sec_off + sec_len > mlen)
    {
        return false;
    }
    const uint8_t *auth = nullptr;
    size_t auth_len = 0;
    if (!pc_spnego_parse_response(msg + sec_off, sec_len, &auth, &auth_len) || auth_len < 28)
    {
        return false;
    }
    uint16_t nt_len = rd16(auth + 20); // NtChallengeResponseFields: Len @20, BufferOffset @24
    uint32_t nt_off = rd32(auth + 24);
    if (nt_len < 16 || (size_t)nt_off + 16 > auth_len)
    {
        return false;
    }
    uint8_t nt_hash[16];
    uint8_t owf[16];
    pc_ntlm_nt_hash(cfg->pass, nt_hash);
    if (!pc_ntlm_ntowfv2(nt_hash, cfg->user, cfg->domain ? cfg->domain : "", owf))
    {
        return false;
    }
    pc_hmac_md5(owf, 16, auth + nt_off, 16, key); // SessionBaseKey = HMAC-MD5(NTOWFv2, NTProofStr)
    return true;
}

// Sign / verify a message with the mock's in-force algorithm (HMAC-SHA256 for SMB 2.x, AES-CMAC for 3.1.1).
static void mock_sign(const Mock *m, uint8_t *msg, size_t len)
{
    if (m->sign_algo == Smb2SignAlgo::AES_CMAC)
    {
        pc_smb2_sign_cmac(m->sign_key, msg, len);
    }
    else
    {
        pc_smb2_sign(m->sign_key, msg, len);
    }
}
static bool mock_verify(const Mock *m, uint8_t *msg, size_t len)
{
    return m->sign_algo == Smb2SignAlgo::AES_CMAC ? pc_smb2_verify_cmac(m->sign_key, msg, len)
                                                  : pc_smb2_verify(m->sign_key, msg, len);
}

// Build the SMB 3.1.1 NEGOTIATE response body (dialect 0x0311 + SIGNING_REQUIRED + a preauth-integrity
// SHA-512 context with a server salt + an AES-CMAC signing context) into resp; returns the total length.
static size_t build_neg_resp_311(uint8_t *resp, uint64_t msg_id, bool offer_encrypt, uint16_t cipher)
{
    pc_smb2_build_header(resp, PC_SMB_BUF + 128, Smb2Command::SMB2_NEGOTIATE, 1, msg_id, 0, 0);
    uint8_t *b = resp + 64;
    memset(b, 0, 64);
    w16(b + 0, 65);                                                // StructureSize
    w16(b + 2, Smb2SecurityMode::SMB2_NEGOTIATE_SIGNING_REQUIRED); // SecurityMode
    w16(b + 4, (uint16_t)Smb2Dialect::SMB2_DIALECT_0311);          // DialectRevision
    w16(b + 6, offer_encrypt ? 3 : 2);                             // NegotiateContextCount
    w16(b + 56, 0);                                                // SecurityBufferOffset
    w16(b + 58, 0);                                                // SecurityBufferLength
    const uint32_t ctx = 128;                                      // 8-aligned, right after the 64-byte body
    w32(b + 60, ctx);                                              // NegotiateContextOffset (from msg start)
    // Context 1 - PREAUTH_INTEGRITY_CAPABILITIES: SHA-512 + a 32-byte server salt.
    uint8_t *c = resp + ctx;
    w16(c + 0, Smb2NegotiateContextType::SMB2_PREAUTH_INTEGRITY_CAPABILITIES);
    w16(c + 2, 6 + 32); // DataLength
    w16(c + 8, 1);      // HashAlgorithmCount
    w16(c + 10, 32);    // SaltLength
    w16(c + 12, Smb2HashAlgorithm::SMB2_PREAUTH_INTEGRITY_SHA512);
    for (int i = 0; i < 32; i++)
    {
        c[14 + i] = (uint8_t)(0x40 + i); // deterministic server salt
    }
    // Context 2 - SIGNING_CAPABILITIES advertising AES-CMAC, 8-byte aligned after context 1 (46 -> 48).
    uint8_t *c2 = c + 48;
    w16(c2 + 0, Smb2NegotiateContextType::SMB2_SIGNING_CAPABILITIES);
    w16(c2 + 2, 4); // DataLength
    w16(c2 + 8, 1); // SigningAlgorithmCount
    w16(c2 + 10, Smb2SigningAlgorithm::SMB2_SIGNING_AES_CMAC);
    if (!offer_encrypt)
    {
        return ctx + 48 + 12; // 128 + 48 + 12 = 188
    }
    // Context 3 - ENCRYPTION_CAPABILITIES advertising the negotiated cipher, 8-byte aligned after context 2.
    uint8_t *c3 = c2 + 16;
    w16(c3 + 0, Smb2NegotiateContextType::SMB2_ENCRYPTION_CAPABILITIES);
    w16(c3 + 2, 4); // DataLength
    w16(c3 + 8, 1); // CipherCount
    w16(c3 + 10, cipher);
    return ctx + 48 + 16 + 12; // 204
}

static int mock_send(void *c, const uint8_t *d, size_t n)
{
    Mock *m = (Mock *)c;
    m->req_count++;
    const uint8_t *msg = d + 4; // skip the Direct-TCP prefix
    size_t mlen = n - 4;
    // SMB 3.x: a request wrapped in a TRANSFORM_HEADER (ProtocolId 0xFD 'S' 'M' 'B') is decrypted before
    // processing, using the same C2S key the client encrypted with (the mock is a reference peer).
    uint8_t plain[PC_SMB_BUF];
    bool req_enc = false;
    if (m->enc_keys && mlen >= PC_SMB2_TRANSFORM_HDR_LEN && msg[0] == 0xFD && msg[1] == 'S' && msg[2] == 'M' &&
        msg[3] == 'B')
    {
        size_t pl = pc_smb2_decrypt(m->cipher, m->enc_c2s, msg, mlen, plain, sizeof(plain));
        if (pl == 0)
        {
            return -1; // bad tag / not decryptable -> the client will see the connection drop
        }
        msg = plain;
        mlen = pl;
        req_enc = true;
    }
    Smb2Header h;
    if (!pc_smb2_parse_header(msg, mlen, &h))
    {
        return -1;
    }

    // SMB 3.1.1 preauth-integrity chain: fold each NEGOTIATE / SESSION_SETUP request as received, in the
    // same order the client folds it, so both sides reach the same final hash for key derivation.
    if (m->require_311)
    {
        if (h.command == Smb2Command::SMB2_NEGOTIATE)
        {
            pc_smb_preauth_init(&m->preauth);
        }
        if (h.command == Smb2Command::SMB2_NEGOTIATE || h.command == Smb2Command::SMB2_SESSION_SETUP)
        {
            pc_smb_preauth_update(&m->preauth, msg, mlen);
        }
    }

    // Once the session is signed, every request the client sends must carry a valid signature - unless it is
    // encrypted, in which case the AEAD tag (already verified on decrypt) is the integrity check, not a signature.
    if (m->signing && !req_enc)
    {
        uint8_t vbuf[PC_SMB_BUF];
        if (mlen <= sizeof(vbuf))
        {
            memcpy(vbuf, msg, mlen);
            if (!(vbuf[16] & Smb2HeaderFlags::SMB2_FLAGS_SIGNED) || !mock_verify(m, vbuf, mlen))
            {
                m->bad_req_sigs++;
            }
        }
    }

    uint8_t resp[PC_SMB_BUF + 128];
    memset(resp, 0, sizeof(resp));
    size_t rlen = 0;
    uint8_t *b = resp + 64;
    switch (h.command)
    {
    case Smb2Command::SMB2_NEGOTIATE:
        if (m->require_311)
        {
            if (m->cipher == 0)
            {
                m->cipher = Smb2Cipher::SMB2_ENCRYPTION_AES128_GCM; // default when a test does not pin one
            }
            rlen = build_neg_resp_311(resp, h.message_id, m->require_encrypt || m->encrypt_share_only,
                                      m->cipher); // dialect 0x0311 + preauth + signing (+ encryption)
            break;
        }
        pc_smb2_build_header(resp, sizeof(resp), Smb2Command::SMB2_NEGOTIATE, 1, h.message_id, 0, 0);
        w16(b + 0, 65); // StructureSize
        if (m->require_signing)
        {
            w16(b + 2, Smb2SecurityMode::SMB2_NEGOTIATE_SIGNING_REQUIRED); // SecurityMode
        }
        w16(b + 4, (uint16_t)Smb2Dialect::SMB2_DIALECT_0210); // DialectRevision
        rlen = 128;                                           // header + 64-byte fixed body, empty buffer
        break;
    case Smb2Command::SMB2_SESSION_SETUP: {
        pc_smb2_build_header(resp, sizeof(resp), Smb2Command::SMB2_SESSION_SETUP, 1, h.message_id, 0, m->session_id);
        w16(b + 0, 9); // StructureSize
        if (m->ss_round++ == 0)
        {
            w32(resp + 8, m->ss1_status);
            if (m->ss1_secbuf_mode == SSBUF_EMPTY)
            {
                w16(b + 4, 0); // SecurityBufferOffset
                w16(b + 6, 0); // SecurityBufferLength (nullptr sec_buf on the client)
                rlen = 72;
            }
            else
            {
                uint8_t chal[1024];
                uint8_t sctok[1200];
                const uint8_t sc[8] = {1, 2, 3, 4, 5, 6, 7, 8};
                size_t sc_n = 0;
                if (m->ss1_secbuf_mode == SSBUF_RAW_JUNK)
                {
                    memset(sctok, 0x77, 16); // not a SPNEGO NegTokenResp
                    sc_n = 16;
                }
                else if (m->ss1_secbuf_mode == SSBUF_SPNEGO_JUNK)
                {
                    uint8_t junk[16];
                    memset(junk, 0x55, sizeof(junk)); // wrapped, but too short/wrong to be an NTLMSSP CHALLENGE
                    sc_n = pc_spnego_wrap_authenticate(junk, sizeof(junk), sctok, sizeof(sctok));
                }
                else
                {
                    size_t chal_n = m->chal_ti ? pc_ntlmssp_challenge_ti(chal, sc, m->chal_ti, m->chal_ti_len)
                                               : pc_ntlmssp_challenge(chal, sc);
                    sc_n = pc_spnego_wrap_authenticate(chal, chal_n, sctok, sizeof(sctok)); // NegTokenResp shape
                }
                w16(b + 4, 72); // SecurityBufferOffset
                w16(b + 6, (uint16_t)sc_n);
                memcpy(resp + 72, sctok, sc_n);
                rlen = 72 + sc_n;
            }
        }
        else
        {
            // Round 2 carries the AUTHENTICATE: derive the shared signing key, then sign from here on.
            // SMB 3.1.1 folds this request (done above), then derives the AES-CMAC signing key from the
            // NTLMv2 session base key + the final preauth hash; SMB 2.x uses the base key with HMAC.
            uint8_t base_key[16];
            if (m->creds && mock_derive_key(msg, mlen, m->creds, base_key))
            {
                if (m->require_311)
                {
                    pc_smb3_derive_signing_key(base_key, (uint16_t)Smb2Dialect::SMB2_DIALECT_0311, m->preauth.hash,
                                               m->sign_key);
                    m->sign_algo = Smb2SignAlgo::AES_CMAC;
                    m->signing = true;
                    if (m->require_encrypt || m->encrypt_share_only)
                    {
                        // Derive the same cipher keys the client will. A globally-required server flags the
                        // session encrypt-required (the client then encrypts from TREE_CONNECT onward); a
                        // share-only requirement derives keys but leaves the flag clear, so only a client that
                        // forces encryption (cfg.encrypt) proceeds. This SS2 reply always stays plaintext.
                        pc_smb3_derive_encryption_keys(base_key, (uint16_t)Smb2Dialect::SMB2_DIALECT_0311,
                                                       m->preauth.hash, pc_smb2_cipher_key_len(m->cipher), m->enc_c2s,
                                                       m->enc_s2c);
                        m->enc_keys = true;
                        if (m->require_encrypt)
                        {
                            w16(b + 2, Smb2SessionFlags::SMB2_SESSION_FLAG_ENCRYPT_DATA); // SessionFlags
                        }
                    }
                }
                else if (m->require_signing)
                {
                    memcpy(m->sign_key, base_key, 16);
                    m->signing = true;
                }
            }
            w32(resp + 8, m->auth_status);
            rlen = 72; // header + 8-byte body, empty buffer
        }
        break;
    }
    case Smb2Command::SMB2_TREE_CONNECT:
        pc_smb2_build_header(resp, sizeof(resp), Smb2Command::SMB2_TREE_CONNECT, 1, h.message_id, m->tree_id,
                             m->session_id);
        // A share that requires encryption rejects an unencrypted TREE_CONNECT (ACCESS_DENIED) - exactly what a
        // real Samba `smb encrypt = required` share does, forcing the client to encrypt from here on.
        if (m->encrypt_share_only && !req_enc)
        {
            w32(resp + 8, 0xC0000022u); // STATUS_ACCESS_DENIED
        }
        else
        {
            w32(resp + 8, m->tc_status);
        }
        w16(b + 0, 16); // StructureSize
        b[2] = Smb2ShareType::SMB2_SHARE_TYPE_DISK;
        rlen = 64 + 16;
        break;
    case Smb2Command::SMB2_CREATE:
        pc_smb2_build_header(resp, sizeof(resp), Smb2Command::SMB2_CREATE, 1, h.message_id, m->tree_id, m->session_id);
        w32(resp + 8, m->create_status);
        w16(b + 0, 89); // StructureSize
        w32(b + 4, 1);  // CreateAction = FILE_OPENED
        w64(b + 48, m->file_size);
        memcpy(b + 64, m->file_id, 16);
        rlen = 64 + 88;
        break;
    case Smb2Command::SMB2_READ: {
        const uint8_t *rq = msg + 64; // READ request body
        uint32_t length = rd32(rq + 4);
        uint64_t off = rd64(rq + 8);
        pc_smb2_build_header(resp, sizeof(resp), Smb2Command::SMB2_READ, 1, h.message_id, m->tree_id, m->session_id);
        if (off >= m->file_data_len)
        {
            w32(resp + 8, Smb2Status::SMB2_STATUS_END_OF_FILE);
            w16(b + 0, 17); // StructureSize, no data
            rlen = 64 + 16;
        }
        else
        {
            uint32_t avail = (uint32_t)(m->file_data_len - off);
            uint32_t n2 = length < avail ? length : avail;
            w16(b + 0, 17); // StructureSize
            b[2] = 80;      // DataOffset (header + 16-byte body)
            w32(b + 4, n2); // DataLength
            memcpy(resp + 80, m->file_data + off, n2);
            rlen = 80 + n2;
        }
        break;
    }
    case Smb2Command::SMB2_WRITE: {
        const uint8_t *wq = msg + 64; // WRITE request body
        uint16_t data_off = rd16(wq + 2);
        uint32_t length = rd32(wq + 4);
        uint64_t off = rd64(wq + 8);
        if (off + length <= sizeof(m->file_data))
        {
            memcpy(m->file_data + off, msg + data_off, length);
            if (off + length > m->file_data_len)
            {
                m->file_data_len = (size_t)(off + length);
            }
        }
        pc_smb2_build_header(resp, sizeof(resp), Smb2Command::SMB2_WRITE, 1, h.message_id, m->tree_id, m->session_id);
        w16(b + 0, 17);     // StructureSize
        w32(b + 4, length); // Count
        rlen = 64 + 16;
        break;
    }
    case Smb2Command::SMB2_CLOSE:
        pc_smb2_build_header(resp, sizeof(resp), Smb2Command::SMB2_CLOSE, 1, h.message_id, m->tree_id, m->session_id);
        w16(b + 0, 60); // StructureSize
        rlen = 64 + 60;
        break;
    default:
        return -1;
    }
    resp[16] |= 0x01; // Smb2HeaderFlags::SMB2_FLAGS_SERVER_TO_REDIR
    bool drop = m->cut_after_negotiate && h.command != Smb2Command::SMB2_NEGOTIATE;
    if (m->fault_at_req == m->req_count)
    {
        if (m->fault_kind == FAULT_DROP)
        {
            drop = true;
        }
        else if (m->fault_kind == FAULT_BAD_HEADER)
        {
            resp[0] = 0x00; // break the ProtocolId magic (FE 53 4D 42) -> pc_smb2_parse_header fails
        }
        else if (m->fault_kind == FAULT_BAD_BODY)
        {
            w16(resp + 64, 0xFFFF); // break the body StructureSize -> the body parser fails
        }
    }
    // Fold the response into the preauth chain in the same order the client does: the NEGOTIATE response
    // and the round-1 SESSION_SETUP response (STATUS_MORE_PROCESSING). The round-2 response (SUCCESS) is
    // NOT folded - the key is already derived by then - so ss_round == 1 is exactly the round-1 case.
    if (m->require_311 && (h.command == Smb2Command::SMB2_NEGOTIATE ||
                           (h.command == Smb2Command::SMB2_SESSION_SETUP && m->ss_round == 1)))
    {
        pc_smb_preauth_update(&m->preauth, resp, rlen);
    }

    if (m->signing && !req_enc)
    {
        mock_sign(m, resp, rlen);
        if (m->corrupt_read_sig && h.command == Smb2Command::SMB2_READ)
        {
            resp[48] ^= 0xFF; // tamper: invalidate the signature after signing
        }
    }
    if (!drop)
    {
        if (req_enc)
        {
            // Encrypt the response with the S2C key + a fresh monotonic nonce (the client reads the nonce from
            // the TRANSFORM_HEADER, so it need not track ours). Encrypted replies are never separately signed.
            uint8_t enc[PC_SMB_BUF + 128];
            uint8_t nonce[PC_SMB2_NONCE_FIELD_LEN] = {0};
            uint64_t ctr = m->enc_nonce++;
            for (int i = 0; i < 8; i++)
            {
                nonce[i] = (uint8_t)(ctr >> (8 * i));
            }
            size_t el = pc_smb2_encrypt(m->cipher, m->enc_s2c, nonce, m->session_id, resp, rlen, enc, sizeof(enc));
            if (m->corrupt_read_sig && h.command == Smb2Command::SMB2_READ)
            {
                enc[PC_SMB2_TRANSFORM_HDR_LEN + 2] ^= 0xFF; // tamper a ciphertext byte -> the client's open fails
            }
            append_frame(m, enc, el);
        }
        else
        {
            append_frame(m, resp, rlen);
        }
    }
    return (int)n;
}

static int mock_recv(void *c, uint8_t *buf, size_t cap)
{
    Mock *m = (Mock *)c;
    if (m->rx_pos >= m->rx_len)
    {
        return 0; // peer has nothing more -> closed
    }
    size_t avail = m->rx_len - m->rx_pos;
    size_t take = avail < cap ? avail : cap;
    memcpy(buf, m->rx + m->rx_pos, take);
    m->rx_pos += take;
    return (int)take;
}

static Mock make_mock()
{
    Mock m;
    memset(&m, 0, sizeof(m));
    m.session_id = 0x1122334455667788ULL;
    m.tree_id = 0x00A1;
    for (int i = 0; i < 16; i++)
    {
        m.file_id[i] = (uint8_t)(0xE0 + i);
    }
    m.file_size = 4096;
    m.auth_status = Smb2Status::SMB2_STATUS_SUCCESS;
    m.tc_status = Smb2Status::SMB2_STATUS_SUCCESS;
    m.create_status = Smb2Status::SMB2_STATUS_SUCCESS;
    m.ss1_status = Smb2Status::SMB2_STATUS_MORE_PROCESSING_REQUIRED;
    return m;
}

static SmbConfig make_cfg()
{
    SmbConfig c;
    memset(&c, 0, sizeof(c));
    c.user = "operator";
    c.pass = "secretpassword";
    c.domain = "SHOP";
    c.workstation = "ESP32";
    c.share = "\\\\nc01\\programs";
    c.path = "A.NC";
    c.desired_access = Smb2Access::SMB2_FILE_GENERIC_READ;
    c.disposition = Smb2Disposition::SMB2_FILE_OPEN;
    return c;
}

void test_open_close_success()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    memset(&h, 0, sizeof(h));

    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_HEX64(m.session_id, h.session_id);
    TEST_ASSERT_EQUAL_HEX32(m.tree_id, h.tree_id);
    TEST_ASSERT_EQUAL_MEMORY(m.file_id, h.file_id, 16);
    TEST_ASSERT_EQUAL_HEX64(4096, h.file_size);
    TEST_ASSERT_EQUAL_UINT64(5, h.next_message_id);
    // NEGOTIATE + 2x SESSION_SETUP + TREE_CONNECT + CREATE = 5 requests
    TEST_ASSERT_EQUAL_INT(5, m.req_count);

    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_close(&h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT64(6, h.next_message_id);
    TEST_ASSERT_EQUAL_INT(6, m.req_count);
}

void test_auth_failure()
{
    Mock m = make_mock();
    m.auth_status = 0xC000006D; // STATUS_LOGON_FAILURE
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_AUTH, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_bad_share()
{
    Mock m = make_mock();
    m.tc_status = 0xC00000CC; // STATUS_BAD_NETWORK_NAME
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_create_not_found()
{
    Mock m = make_mock();
    m.create_status = 0xC0000034; // STATUS_OBJECT_NAME_NOT_FOUND
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_io_error()
{
    Mock m = make_mock();
    m.cut_after_negotiate = true; // server stops responding after NEGOTIATE
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_IO, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

void test_arg_validation()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    cfg.user = nullptr;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    cfg = make_cfg();
    cfg.path = nullptr;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

static SmbResult open_ok(Mock *m, SmbConfig *cfg, SmbHandle *h)
{
    memset(h, 0, sizeof(*h));
    return smb_open(cfg, h, mock_send, mock_recv, m);
}

// Read a 2000-byte file: spans multiple READ round trips (chunk_max < 2000).
void test_read_file()
{
    Mock m = make_mock();
    for (int i = 0; i < 2000; i++)
    {
        m.file_data[i] = (uint8_t)(i * 31 + 7);
    }
    m.file_data_len = 2000;
    m.file_size = 2000;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, open_ok(&m, &cfg, &h));

    uint8_t buf[2048];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_read(&h, 0, buf, 2000, &got, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(2000, got);
    TEST_ASSERT_EQUAL_MEMORY(m.file_data, buf, 2000);
}

// Read with a buffer larger than the file: stops at EOF (short read / STATUS_END_OF_FILE).
void test_read_past_eof()
{
    Mock m = make_mock();
    for (int i = 0; i < 100; i++)
    {
        m.file_data[i] = (uint8_t)i;
    }
    m.file_data_len = 100;
    m.file_size = 100;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, open_ok(&m, &cfg, &h));

    uint8_t buf[512];
    size_t got = 999;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_read(&h, 0, buf, sizeof(buf), &got, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(100, got);
    TEST_ASSERT_EQUAL_MEMORY(m.file_data, buf, 100);
}

// Write a 2000-byte file: spans multiple WRITE round trips; the cached file_size grows.
void test_write_file()
{
    Mock m = make_mock();
    m.file_data_len = 0;
    m.file_size = 0;
    SmbConfig cfg = make_cfg();
    cfg.desired_access = Smb2Access::SMB2_FILE_GENERIC_WRITE;
    cfg.disposition = Smb2Disposition::SMB2_FILE_OVERWRITE_IF;
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, open_ok(&m, &cfg, &h));

    uint8_t data[2000];
    for (int i = 0; i < 2000; i++)
    {
        data[i] = (uint8_t)(i * 13 + 3);
    }
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_write(&h, 0, data, sizeof(data), &wrote, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(2000, wrote);
    TEST_ASSERT_EQUAL_size_t(2000, m.file_data_len);
    TEST_ASSERT_EQUAL_MEMORY(data, m.file_data, 2000);
    TEST_ASSERT_EQUAL_HEX64(2000, h.file_size);
}

// Write then read back the same bytes through the mock (a byte-exact round trip).
void test_write_then_read_roundtrip()
{
    Mock m = make_mock();
    m.file_data_len = 0;
    m.file_size = 0;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, open_ok(&m, &cfg, &h));

    uint8_t data[1500];
    for (int i = 0; i < 1500; i++)
    {
        data[i] = (uint8_t)(i ^ 0x5A);
    }
    size_t wrote = 0, got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_write(&h, 0, data, sizeof(data), &wrote, mock_send, mock_recv, &m));
    uint8_t back[1500];
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_read(&h, 0, back, sizeof(back), &got, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(1500, got);
    TEST_ASSERT_EQUAL_MEMORY(data, back, 1500);
}

// ---- smb_open handshake error / edge paths (the negative sides the round-trip test skips) ----

// NEGOTIATE reply is malformed (bad StructureSize) -> protocol error out of smb_open.
void test_negotiate_malformed()
{
    Mock m = make_mock();
    m.fault_at_req = 1;
    m.fault_kind = FAULT_BAD_BODY;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// The peer closes before answering NEGOTIATE -> IO error.
void test_negotiate_dropped()
{
    Mock m = make_mock();
    m.fault_at_req = 1;
    m.fault_kind = FAULT_DROP;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_IO, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// SESSION_SETUP round 1 header is unparseable -> auth error (the parse side of the guard).
void test_session1_bad_header()
{
    Mock m = make_mock();
    m.fault_at_req = 2;
    m.fault_kind = FAULT_BAD_HEADER;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_AUTH, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// SESSION_SETUP round 1 returns SUCCESS instead of MORE_PROCESSING_REQUIRED -> auth error (the
// status side of the guard).
void test_session1_wrong_status()
{
    Mock m = make_mock();
    m.ss1_status = Smb2Status::SMB2_STATUS_SUCCESS;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_AUTH, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// SESSION_SETUP round 1 body is unparseable (bad StructureSize) -> protocol error (the parse side
// of the sec-buf guard).
void test_session1_bad_body()
{
    Mock m = make_mock();
    m.fault_at_req = 2;
    m.fault_kind = FAULT_BAD_BODY;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// SESSION_SETUP round 1 has no security buffer -> protocol error (the !sec_buf side).
void test_session1_no_secbuf()
{
    Mock m = make_mock();
    m.ss1_secbuf_mode = SSBUF_EMPTY;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// SESSION_SETUP round 1 security buffer is not a SPNEGO NegTokenResp -> protocol error.
void test_session1_bad_spnego()
{
    Mock m = make_mock();
    m.ss1_secbuf_mode = SSBUF_RAW_JUNK;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// SPNEGO unwraps but the inner token is not an NTLMSSP CHALLENGE -> protocol error.
void test_session1_bad_ntlmssp()
{
    Mock m = make_mock();
    m.ss1_secbuf_mode = SSBUF_SPNEGO_JUNK;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// The peer closes before answering SESSION_SETUP round 2 -> IO error.
void test_session2_dropped()
{
    Mock m = make_mock();
    m.fault_at_req = 3;
    m.fault_kind = FAULT_DROP;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_IO, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// SESSION_SETUP round 2 header is unparseable -> protocol error.
void test_session2_bad_header()
{
    Mock m = make_mock();
    m.fault_at_req = 3;
    m.fault_kind = FAULT_BAD_HEADER;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// The peer closes before answering TREE_CONNECT -> IO error.
void test_tree_dropped()
{
    Mock m = make_mock();
    m.fault_at_req = 4;
    m.fault_kind = FAULT_DROP;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_IO, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// TREE_CONNECT header is SUCCESS but the body is unparseable -> protocol error.
void test_tree_bad_body()
{
    Mock m = make_mock();
    m.fault_at_req = 4;
    m.fault_kind = FAULT_BAD_BODY;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// The peer closes before answering CREATE -> IO error.
void test_create_dropped()
{
    Mock m = make_mock();
    m.fault_at_req = 5;
    m.fault_kind = FAULT_DROP;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_IO, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// CREATE header is SUCCESS but the body is unparseable -> protocol error.
void test_create_bad_body()
{
    Mock m = make_mock();
    m.fault_at_req = 5;
    m.fault_kind = FAULT_BAD_BODY;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// A share path longer than the UTF-16LE scratch overflows in TREE_CONNECT -> overflow error.
void test_long_share_overflow()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    char share[300];
    memset(share, 'S', sizeof(share) - 1); // 299 chars -> 598 bytes UTF-16LE, over the 512-byte scratch
    share[sizeof(share) - 1] = 0;
    cfg.share = share;
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_OVERFLOW, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// A file path longer than the UTF-16LE scratch overflows in CREATE -> overflow error.
void test_long_path_overflow()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    char path[300];
    memset(path, 'P', sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    cfg.path = path;
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_OVERFLOW, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// A user longer than the NTOWFv2 scratch overflows during SESSION_SETUP -> overflow error.
void test_long_user_overflow()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    char user[300];
    memset(user, 'u', sizeof(user) - 1); // 299 chars -> pc_ntlm_ntowfv2 fails closed
    user[sizeof(user) - 1] = 0;
    cfg.user = user;
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_OVERFLOW, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// A CHALLENGE target-info so large the NTLMv2 response overflows nt_resp -> overflow error.
void test_challenge_ti_ntlmv2_overflow()
{
    Mock m = make_mock();
    uint8_t ti[500];
    memset(ti, 0, sizeof(ti)); // starts with an EOL pair -> find_av_timestamp returns a zero time
    m.chal_ti = ti;
    m.chal_ti_len = sizeof(ti); // 48 + 500 = 548 > the 512-byte nt_resp buffer
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_OVERFLOW, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// A smaller target-info: the NTLMv2 response fits but the NTLMSSP AUTHENTICATE overflows -> overflow.
void test_challenge_ti_authenticate_overflow()
{
    Mock m = make_mock();
    uint8_t ti[400];
    memset(ti, 0, sizeof(ti));
    m.chal_ti = ti;
    m.chal_ti_len = sizeof(ti); // nt_resp = 448 (fits 512); AUTHENTICATE = 64+448+identity > 512
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_OVERFLOW, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// Smaller still: AUTHENTICATE fits but its SPNEGO wrap overflows sp2 -> overflow error.
void test_challenge_ti_spnego_overflow()
{
    Mock m = make_mock();
    uint8_t ti[360];
    memset(ti, 0, sizeof(ti));
    m.chal_ti = ti;
    m.chal_ti_len = sizeof(ti); // AUTHENTICATE = 506 (fits 512); its SPNEGO wrap = 522 > 512
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_OVERFLOW, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// Target info that is only an EOL: find_av_timestamp breaks with a zero time; handshake still OK.
void test_av_eol_only()
{
    Mock m = make_mock();
    const uint8_t ti[4] = {0x00, 0x00, 0x00, 0x00}; // MsvAvEOL
    m.chal_ti = ti;
    m.chal_ti_len = sizeof(ti);
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// Target info that skips a non-timestamp pair and a wrong-length AvId 7 before the real 8-byte
// timestamp: exercises find_av_timestamp's skip and length-mismatch branches.
void test_av_skip_then_find()
{
    Mock m = make_mock();
    const uint8_t ti[] = {
        0x02, 0x00, 0x04, 0x00, 0xDE, 0xAD, 0xBE, 0xEF,                         // AvId 2, len 4 -> skipped
        0x07, 0x00, 0x04, 0x00, 0x11, 0x22, 0x33, 0x44,                         // AvId 7 but len 4 -> skipped
        0x07, 0x00, 0x08, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // AvId 7 len 8 -> time
        0x00, 0x00, 0x00, 0x00,                                                 // MsvAvEOL
    };
    m.chal_ti = ti;
    m.chal_ti_len = sizeof(ti);
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// A truncated AvId 7 whose 8-byte value runs past the blob end: the in-bounds check fails and
// find_av_timestamp falls through to a zero time; the handshake still succeeds.
void test_av_truncated_timestamp()
{
    Mock m = make_mock();
    const uint8_t ti[4] = {0x07, 0x00, 0x08, 0x00}; // AvId 7 len 8 header, but no value bytes
    m.chal_ti = ti;
    m.chal_ti_len = sizeof(ti);
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// ---- a canned single-response seam for smb_read / smb_write / smb_close error paths, driven on a
// hand-built handle (no open handshake needed). ----
struct Canned
{
    uint8_t resp[512];
    size_t pc_resp_len;
    size_t pos;
    bool short_send; // send returns a short count (1) instead of the true length
};

static int canned_send(void *c, const uint8_t *d, size_t n)
{
    Canned *cn = (Canned *)c;
    (void)d;
    return cn->short_send ? 1 : (int)n;
}

static int canned_recv(void *c, uint8_t *buf, size_t cap)
{
    Canned *cn = (Canned *)c;
    if (cn->pos >= cn->pc_resp_len)
    {
        return 0; // exhausted -> peer closed
    }
    size_t avail = cn->pc_resp_len - cn->pos;
    size_t take = avail < cap ? avail : cap;
    memcpy(buf, cn->resp + cn->pos, take);
    cn->pos += take;
    return (int)take;
}

// Build a 64-byte SMB2 response header (command + status) into msg; returns the body pointer.
static uint8_t *pc_resp_hdr(uint8_t *msg, Smb2Command cmd, uint32_t status)
{
    pc_smb2_build_header(msg, 64, cmd, 1, 5, 0x00A1, 0x1122334455667788ULL);
    w32(msg + 8, status);
    msg[16] |= 0x01; // server-to-redir
    return msg + 64;
}

static void canned_frame(Canned *cn, const uint8_t *msg, size_t mlen)
{
    cn->pc_resp_len = pc_smb2_transport_frame(cn->resp, sizeof(cn->resp), msg, mlen);
    cn->pos = 0;
}

static SmbHandle make_handle()
{
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    h.session_id = 0x1122334455667788ULL;
    h.tree_id = 0x00A1;
    for (int i = 0; i < 16; i++)
    {
        h.file_id[i] = (uint8_t)(0xE0 + i);
    }
    h.file_size = 4096;
    h.next_message_id = 5;
    return h;
}

void test_read_arg()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG,
                          smb_read(&h, 0, nullptr, sizeof(buf), &got, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG,
                          smb_read(&h, 0, buf, sizeof(buf), nullptr, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG,
                          smb_read(nullptr, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

// send returns a short count -> the round trip reports IO (smb_round_trip's send-fail path).
void test_read_send_io()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.short_send = true;
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_IO,
                          smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

// The peer sends nothing back -> IO error.
void test_read_recv_io()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn)); // pc_resp_len 0 -> recv returns 0
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_IO,
                          smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

// The READ reply header is unparseable -> protocol error.
void test_read_bad_header()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_READ, Smb2Status::SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    b[2] = 80;
    w32(b + 4, 0);
    msg[0] = 0x00; // corrupt ProtocolId
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL,
                          smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

// A READ status that is neither SUCCESS nor END_OF_FILE -> protocol error.
void test_read_status_error()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_READ, 0xC0000022); // STATUS_ACCESS_DENIED
    w16(b + 0, 17);
    b[2] = 80;
    w32(b + 4, 0);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL,
                          smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

// A READ reply whose body StructureSize is wrong -> protocol error.
void test_read_bad_body()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_READ, Smb2Status::SMB2_STATUS_SUCCESS);
    w16(b + 0, 99); // not 17
    b[2] = 80;
    w32(b + 4, 0);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL,
                          smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

// A READ reply that returns more data than requested -> protocol error (data_len > want).
void test_read_data_too_long()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[256] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_READ, Smb2Status::SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    b[2] = 80;       // DataOffset
    w32(b + 4, 100); // DataLength 100, but only 16 requested
    memset(msg + 80, 0xAB, 100);
    canned_frame(&cn, msg, 180);
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL,
                          smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

// A READ reply with zero data stops the loop cleanly at 0 bytes.
void test_read_zero_data()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_READ, Smb2Status::SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    b[2] = 80;
    w32(b + 4, 0); // DataLength 0
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 999;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_UINT32(0, got);
}

void test_write_arg()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG,
                          smb_write(&h, 0, nullptr, sizeof(data), &wrote, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG,
                          smb_write(&h, 0, data, sizeof(data), nullptr, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG,
                          smb_write(nullptr, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

// send returns a short count -> IO error (smb_write uses send_msg directly).
void test_write_send_io()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.short_send = true;
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_IO,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

// The peer sends nothing back -> IO error.
void test_write_recv_io()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_IO,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

// A reply whose transport length prefix exceeds the work buffer -> overflow error.
void test_write_recv_overflow()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.resp[0] = 0x00;
    cn.resp[1] = 0x00;
    cn.resp[2] = 0x20;
    cn.resp[3] = 0x00; // length 0x2000 = 8192 > PC_SMB_BUF (1024)
    cn.pc_resp_len = 4;
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_OVERFLOW,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

// A WRITE reply header is unparseable -> protocol error.
void test_write_bad_header()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_WRITE, Smb2Status::SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    w32(b + 4, 16); // Count
    msg[0] = 0x00;  // corrupt ProtocolId
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

// A WRITE status that is not SUCCESS -> protocol error.
void test_write_status_error()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_WRITE, 0xC0000022);
    w16(b + 0, 17);
    w32(b + 4, 16);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

// A WRITE reply body is unparseable (bad StructureSize) -> protocol error.
void test_write_bad_body()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_WRITE, Smb2Status::SMB2_STATUS_SUCCESS);
    w16(b + 0, 99); // not 17
    w32(b + 4, 16);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

// A WRITE that acknowledges zero bytes -> protocol error (no forward progress).
void test_write_zero_count()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_WRITE, Smb2Status::SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    w32(b + 4, 0); // Count 0
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

// A WRITE that claims more bytes than were sent -> protocol error (count > want).
void test_write_count_too_big()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_WRITE, Smb2Status::SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    w32(b + 4, 999); // Count > 16 requested
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t data[16] = {0};
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
}

void test_close_arg()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_close(nullptr, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_close(&h, nullptr, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_close(&h, canned_send, nullptr, &cn));
}

// send returns a short count -> IO error.
void test_close_send_io()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.short_send = true;
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_IO, smb_close(&h, canned_send, canned_recv, &cn));
}

// A reply whose transport length prefix exceeds the receive buffer -> overflow error. smb_close now shares
// the PC_SMB_BUF-sized s_smb.rx (via smb_round_trip, so encryption applies uniformly), so the length must
// exceed that, not the old 128-byte stack buffer.
void test_close_recv_overflow()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.resp[0] = 0x00;
    cn.resp[1] = 0xFF; // length ~16 MiB, far beyond PC_SMB_BUF -> recv_msg rejects it as overflow
    cn.resp[2] = 0xFF;
    cn.resp[3] = 0xFF;
    cn.pc_resp_len = 4;
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_OVERFLOW, smb_close(&h, canned_send, canned_recv, &cn));
}

// A reply with a zero-length transport prefix -> IO error.
void test_close_recv_zero_len()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.resp[0] = 0x00; // length 0
    cn.resp[1] = 0x00;
    cn.resp[2] = 0x00;
    cn.resp[3] = 0x00;
    cn.pc_resp_len = 4;
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_IO, smb_close(&h, canned_send, canned_recv, &cn));
}

// The transport prefix promises more bytes than the peer delivers -> IO error (truncated body).
void test_close_recv_trunc_body()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.resp[0] = 0x00;
    cn.resp[1] = 0x00;
    cn.resp[2] = 0x00;
    cn.resp[3] = 0x64;       // length 100
    cn.pc_resp_len = 4 + 40; // only 40 of the 100 body bytes follow
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_IO, smb_close(&h, canned_send, canned_recv, &cn));
}

// The CLOSE reply header is unparseable -> protocol error.
void test_close_bad_header()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_CLOSE, Smb2Status::SMB2_STATUS_SUCCESS);
    w16(b + 0, 60);
    msg[0] = 0x00; // corrupt ProtocolId
    canned_frame(&cn, msg, 124);
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_close(&h, canned_send, canned_recv, &cn));
}

// A CLOSE status that is not SUCCESS -> protocol error.
void test_close_status_error()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_CLOSE, 0xC0000022);
    w16(b + 0, 60);
    canned_frame(&cn, msg, 124);
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_close(&h, canned_send, canned_recv, &cn));
}

// A CLOSE reply body is unparseable (bad StructureSize) -> protocol error.
void test_close_bad_body()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_CLOSE, Smb2Status::SMB2_STATUS_SUCCESS);
    w16(b + 0, 99); // not 60
    canned_frame(&cn, msg, 124);
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_close(&h, canned_send, canned_recv, &cn));
}

// ---- the remaining negative sides of smb_open / smb_read / smb_write / smb_close guards ----

// Every other pointer smb_open's argument guard rejects (the existing coverage only nulls user/path).
void test_open_arg_remaining_nulls()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_open(nullptr, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_open(&cfg, nullptr, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_open(&cfg, &h, nullptr, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_open(&cfg, &h, mock_send, nullptr, &m));
    cfg = make_cfg();
    cfg.pass = nullptr;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    cfg = make_cfg();
    cfg.share = nullptr;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(0, m.req_count); // nothing ever went on the wire
}

// A null domain falls back to the empty NTLM domain; the handshake still completes.
void test_open_null_domain()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    cfg.domain = nullptr;
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_HEX64(m.session_id, h.session_id);
    TEST_ASSERT_EQUAL_HEX32(m.tree_id, h.tree_id);
}

// The TREE_CONNECT reply header is unparseable -> protocol error (the parse side of the guard;
// test_bad_share covers only the status side).
void test_tree_bad_header()
{
    Mock m = make_mock();
    m.fault_at_req = 4;
    m.fault_kind = FAULT_BAD_HEADER;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// The CREATE reply header is unparseable -> protocol error (the parse side of the guard;
// test_create_not_found covers only the status side).
void test_create_bad_header()
{
    Mock m = make_mock();
    m.fault_at_req = 5;
    m.fault_kind = FAULT_BAD_HEADER;
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
}

// smb_read / smb_write reject a null send or recv seam.
void test_read_write_null_seam()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    SmbHandle h = make_handle();
    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    size_t n = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_read(&h, 0, buf, sizeof(buf), &n, nullptr, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_read(&h, 0, buf, sizeof(buf), &n, canned_send, nullptr, &cn));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_write(&h, 0, buf, sizeof(buf), &n, nullptr, canned_recv, &cn));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_ARG, smb_write(&h, 0, buf, sizeof(buf), &n, canned_send, nullptr, &cn));
}

// A READ reply whose transport prefix exceeds the work buffer -> overflow mapped by smb_round_trip
// (smb_write / smb_close reach recv_msg directly, so this is the round-trip helper's own mapping).
void test_read_recv_overflow()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.resp[0] = 0x00;
    cn.resp[1] = 0x00;
    cn.resp[2] = 0x20;
    cn.resp[3] = 0x00; // length 0x2000 = 8192 > PC_SMB_BUF (1024)
    cn.pc_resp_len = 4;
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_OVERFLOW,
                          smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
}

// A READ answered with STATUS_END_OF_FILE ends the loop with whatever was read so far.
void test_read_eof_status()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_READ, Smb2Status::SMB2_STATUS_END_OF_FILE);
    w16(b + 0, 17);
    b[2] = 80;
    w32(b + 4, 0);
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle();
    uint8_t buf[16];
    size_t got = 999;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_read(&h, 0, buf, sizeof(buf), &got, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_UINT32(0, got);
    TEST_ASSERT_EQUAL_UINT64(6, h.next_message_id); // the request still consumed a MessageId
}

// A write that stays inside the cached file size leaves file_size alone (the false side of the grow guard).
void test_write_no_extend()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    uint8_t msg[128] = {0};
    uint8_t *b = pc_resp_hdr(msg, Smb2Command::SMB2_WRITE, Smb2Status::SMB2_STATUS_SUCCESS);
    w16(b + 0, 17);
    w32(b + 4, 16); // Count = the 16 bytes offered
    canned_frame(&cn, msg, 80);
    SmbHandle h = make_handle(); // file_size 4096
    uint8_t data[16];
    memset(data, 0x5A, sizeof(data));
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK,
                          smb_write(&h, 0, data, sizeof(data), &wrote, canned_send, canned_recv, &cn));
    TEST_ASSERT_EQUAL_UINT32(16, wrote);
    TEST_ASSERT_EQUAL_HEX64(4096, h.file_size); // unchanged: 0 + 16 <= 4096
}

// A reply whose Direct-TCP prefix does not start with 0x00 is not a valid frame -> IO error.
void test_close_bad_transport_prefix()
{
    Canned cn;
    memset(&cn, 0, sizeof(cn));
    cn.resp[0] = 0x01; // must be zero for Direct TCP
    cn.resp[1] = 0x00;
    cn.resp[2] = 0x00;
    cn.resp[3] = 0x50;
    cn.pc_resp_len = 4;
    SmbHandle h = make_handle();
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_IO, smb_close(&h, canned_send, canned_recv, &cn));
}

// ---- SMB 2.x message signing wired end to end (MS-SMB2 §3.1.4.1 / §3.1.5.1) ----

// The server requires signing: the full session runs signed. smb_open derives the key and signs the
// round-2 SESSION_SETUP, TREE_CONNECT, and CREATE; smb_read / smb_write / smb_close sign every request
// and verify every response. The mock, a reference peer sharing the derived key, confirms each request
// arrived correctly signed (bad_req_sigs == 0) and signs its own responses for the client to verify.
void test_signed_session_roundtrip()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    m.require_signing = true;
    m.creds = &cfg;
    for (int i = 0; i < 1200; i++)
    {
        m.file_data[i] = (uint8_t)(i * 17 + 5);
    }
    m.file_data_len = 1200;
    m.file_size = 1200;
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_TRUE(h.signing_active); // the session negotiated signing
    TEST_ASSERT_TRUE(m.signing);        // and the mock re-derived the same key

    uint8_t buf[1200];
    size_t got = 0; // spans two signed READ round trips (chunk_max < 1200)
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_read(&h, 0, buf, sizeof(buf), &got, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(1200, got);
    TEST_ASSERT_EQUAL_MEMORY(m.file_data, buf, 1200);

    uint8_t wr[500];
    for (int i = 0; i < 500; i++)
    {
        wr[i] = (uint8_t)(i ^ 0x3C);
    }
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_write(&h, 0, wr, sizeof(wr), &wrote, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(500, wrote);
    TEST_ASSERT_EQUAL_MEMORY(wr, m.file_data, 500);

    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_close(&h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(0, m.bad_req_sigs); // every signed request verified server-side
}

// A signed session where the server's READ response signature is tampered in transit: the client must
// reject it (a signing-required session never trusts an unverifiable response).
void test_signed_response_tampered()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    m.require_signing = true;
    m.creds = &cfg;
    m.corrupt_read_sig = true;
    for (int i = 0; i < 64; i++)
    {
        m.file_data[i] = (uint8_t)i;
    }
    m.file_data_len = 64;
    m.file_size = 64;
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m)); // handshake sigs valid
    uint8_t buf[64];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL,
                          smb_read(&h, 0, buf, sizeof(buf), &got, mock_send, mock_recv, &m));
}

// When the server does not require signing the session stays unsigned (the client advertises only
// SIGNING_ENABLED): no signatures are attached and the mock never sees a signed request.
void test_unsigned_session_when_not_required()
{
    Mock m = make_mock(); // require_signing stays false
    SmbConfig cfg = make_cfg();
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_FALSE(h.signing_active);
    TEST_ASSERT_EQUAL_INT(0, m.bad_req_sigs);
}

// SMB 3.1.1 end to end: the mock negotiates dialect 0x0311 with the preauth-integrity + AES-CMAC
// contexts, so the client runs the full 3.1.1 handshake - offering 3.1.1, chaining the preauth hash
// across NEGOTIATE + both SESSION_SETUP rounds, deriving the SP800-108 signing key, and signing every
// post-auth request with AES-CMAC. The mock, chaining the identical preauth hash and deriving the same
// key, verifies each request (bad_req_sigs == 0) and CMAC-signs its own responses for the client to
// verify - a byte-exact write/read round trip proves the whole signed 3.1.1 session.
void test_open_signed_311_roundtrip()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    m.require_311 = true;
    m.creds = &cfg;
    for (int i = 0; i < 1400; i++)
    {
        m.file_data[i] = (uint8_t)(i * 23 + 11);
    }
    m.file_data_len = 1400;
    m.file_size = 1400;
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_TRUE(h.signing_active);
    TEST_ASSERT_EQUAL_INT(Smb2SignAlgo::AES_CMAC, h.signing_algo); // 3.1.1 signs with AES-CMAC
    TEST_ASSERT_TRUE(m.signing);
    TEST_ASSERT_EQUAL_INT(Smb2SignAlgo::AES_CMAC, m.sign_algo); // the mock derived the same CMAC key

    uint8_t buf[1400];
    size_t got = 0; // spans two CMAC-signed READ round trips (chunk_max < 1400)
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_read(&h, 0, buf, sizeof(buf), &got, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(1400, got);
    TEST_ASSERT_EQUAL_MEMORY(m.file_data, buf, 1400);

    uint8_t wr[700];
    for (int i = 0; i < 700; i++)
    {
        wr[i] = (uint8_t)(i ^ 0x5A);
    }
    size_t wrote = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_write(&h, 0, wr, sizeof(wr), &wrote, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(700, wrote);
    TEST_ASSERT_EQUAL_MEMORY(wr, m.file_data, 700);

    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_close(&h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(0, m.bad_req_sigs); // every AES-CMAC-signed request verified server-side
}

// A signed 3.1.1 session with the server's READ response CMAC tampered in transit: the client rejects it.
void test_signed_311_response_tampered()
{
    Mock m = make_mock();
    SmbConfig cfg = make_cfg();
    m.require_311 = true;
    m.creds = &cfg;
    m.corrupt_read_sig = true;
    for (int i = 0; i < 64; i++)
    {
        m.file_data[i] = (uint8_t)i;
    }
    m.file_data_len = 64;
    m.file_size = 64;
    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(Smb2SignAlgo::AES_CMAC, h.signing_algo);
    uint8_t buf[64];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL,
                          smb_read(&h, 0, buf, sizeof(buf), &got, mock_send, mock_recv, &m));
}

// SMB 3.1.1 with transport encryption: the whole post-auth session (TREE_CONNECT .. CLOSE) is AES-128-GCM
// TRANSFORM-wrapped. The mock is a reference peer - it offers GCM, derives the same C2S/S2C keys, decrypts
// every request and encrypts every response - so this exercises the client encrypt/decrypt wiring end to end.
// (Self-consistency only; real spec/attack conformance is the pentest harness + HW-vs-Samba.)
void test_open_encrypted_311_roundtrip()
{
    Mock m = make_mock();
    m.file_size = 300;
    SmbConfig cfg = make_cfg();
    cfg.desired_access = Smb2Access::SMB2_FILE_GENERIC_READ | Smb2Access::SMB2_FILE_GENERIC_WRITE;
    cfg.disposition = Smb2Disposition::SMB2_FILE_OPEN_IF;
    m.require_311 = true;
    m.require_encrypt = true;
    m.creds = &cfg;

    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_TRUE(h.encrypt_active);       // client turned encryption on from the session ENCRYPT_DATA flag
    TEST_ASSERT_EQUAL_INT(0, m.bad_req_sigs); // encrypted requests carry no signature; none should be flagged

    // Write a buffer then read it back byte-exact - every request and response TRANSFORM-wrapped.
    uint8_t data[250];
    for (int i = 0; i < 250; i++)
    {
        data[i] = (uint8_t)(0xC0 ^ (i * 7));
    }
    size_t wr = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_write(&h, 0, data, sizeof(data), &wr, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(sizeof(data), wr);

    uint8_t buf[256];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_read(&h, 0, buf, sizeof(data), &got, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_UINT32(sizeof(data), got);
    TEST_ASSERT_EQUAL_MEMORY(data, buf, sizeof(data));

    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_close(&h, mock_send, mock_recv, &m));
    TEST_ASSERT_EQUAL_INT(0, m.bad_req_sigs);
}

// A tampered (bit-flipped) encrypted READ response must fail the AEAD tag, so the client rejects it.
void test_encrypted_response_tampered()
{
    Mock m = make_mock();
    for (int i = 0; i < 100; i++)
    {
        m.file_data[i] = (uint8_t)i;
    }
    m.file_data_len = 100;
    m.file_size = 100;
    SmbConfig cfg = make_cfg();
    m.require_311 = true;
    m.require_encrypt = true;
    m.creds = &cfg;
    m.corrupt_read_sig = true; // repurposed here: flip a ciphertext byte of the READ response

    SmbHandle h;
    memset(&h, 0, sizeof(h));
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    TEST_ASSERT_TRUE(h.encrypt_active);
    uint8_t buf[128];
    size_t got = 0;
    TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_read(&h, 0, buf, 100, &got, mock_send, mock_recv, &m));
}

// Every SMB 3.1.1 cipher must drive the client encrypt/decrypt wiring end to end: for each of the four
// ciphers the mock offers it, derives the matching-length keys, and the whole post-auth session round-trips.
void test_open_encrypted_all_ciphers()
{
    const uint16_t ciphers[4] = {Smb2Cipher::SMB2_ENCRYPTION_AES128_GCM, Smb2Cipher::SMB2_ENCRYPTION_AES256_GCM,
                                 Smb2Cipher::SMB2_ENCRYPTION_AES128_CCM, Smb2Cipher::SMB2_ENCRYPTION_AES256_CCM};
    SmbConfig cfg = make_cfg();
    cfg.desired_access = Smb2Access::SMB2_FILE_GENERIC_READ | Smb2Access::SMB2_FILE_GENERIC_WRITE;
    cfg.disposition = Smb2Disposition::SMB2_FILE_OPEN_IF;
    for (int ci = 0; ci < 4; ci++)
    {
        Mock m = make_mock();
        m.require_311 = true;
        m.require_encrypt = true;
        m.cipher = ciphers[ci];
        m.creds = &cfg;

        SmbHandle h;
        memset(&h, 0, sizeof(h));
        char cmsg[48];
        snprintf(cmsg, sizeof(cmsg), "cipher 0x%04x", ciphers[ci]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(SmbResult::SMB_OK, smb_open(&cfg, &h, mock_send, mock_recv, &m), cmsg);
        TEST_ASSERT_TRUE(h.encrypt_active);
        TEST_ASSERT_EQUAL_UINT16(ciphers[ci], h.enc_cipher);
        TEST_ASSERT_EQUAL_INT(0, m.bad_req_sigs);

        uint8_t data[200];
        for (int i = 0; i < 200; i++)
        {
            data[i] = (uint8_t)(0x11 * ci + i * 5);
        }
        size_t wr = 0;
        TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_write(&h, 0, data, sizeof(data), &wr, mock_send, mock_recv, &m));
        TEST_ASSERT_EQUAL_UINT32(sizeof(data), wr);
        uint8_t buf[256];
        size_t got = 0;
        TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_read(&h, 0, buf, sizeof(data), &got, mock_send, mock_recv, &m));
        TEST_ASSERT_EQUAL_UINT32(sizeof(data), got);
        TEST_ASSERT_EQUAL_MEMORY(data, buf, sizeof(data));
        TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_close(&h, mock_send, mock_recv, &m));
    }
}

// A share that requires encryption (server negotiates a cipher but does NOT set the session ENCRYPT_DATA flag,
// and denies an unencrypted TREE_CONNECT) is reachable ONLY when the client forces encryption (cfg.encrypt) -
// the exact fix for the real Samba `smb encrypt = required` share. Without it the client fails at TREE_CONNECT.
void test_open_encrypted_share_requires_client_force()
{
    SmbConfig cfg = make_cfg();

    // No client-forced encryption: TREE_CONNECT goes out unencrypted -> ACCESS_DENIED -> open fails.
    {
        Mock m = make_mock();
        m.require_311 = true;
        m.encrypt_share_only = true;
        m.creds = &cfg;
        SmbHandle h;
        memset(&h, 0, sizeof(h));
        TEST_ASSERT_EQUAL_INT(SmbResult::SMB_ERR_PROTOCOL, smb_open(&cfg, &h, mock_send, mock_recv, &m));
    }

    // With cfg.encrypt the client encrypts from TREE_CONNECT on -> the share accepts -> open + read succeed.
    {
        Mock m = make_mock();
        for (int i = 0; i < 60; i++)
        {
            m.file_data[i] = (uint8_t)(i + 1);
        }
        m.file_data_len = 60;
        m.file_size = 60;
        m.require_311 = true;
        m.encrypt_share_only = true;
        m.creds = &cfg;

        SmbConfig ecfg = cfg;
        ecfg.encrypt = true;
        SmbHandle h;
        memset(&h, 0, sizeof(h));
        TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_open(&ecfg, &h, mock_send, mock_recv, &m));
        TEST_ASSERT_TRUE(h.encrypt_active); // forced on despite no session ENCRYPT_DATA flag
        uint8_t buf[64];
        size_t got = 0;
        TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_read(&h, 0, buf, 60, &got, mock_send, mock_recv, &m));
        TEST_ASSERT_EQUAL_UINT32(60, got);
        TEST_ASSERT_EQUAL_MEMORY(m.file_data, buf, 60);
        TEST_ASSERT_EQUAL_INT(SmbResult::SMB_OK, smb_close(&h, mock_send, mock_recv, &m));
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_open_close_success);
    RUN_TEST(test_auth_failure);
    RUN_TEST(test_bad_share);
    RUN_TEST(test_create_not_found);
    RUN_TEST(test_io_error);
    RUN_TEST(test_arg_validation);
    RUN_TEST(test_read_file);
    RUN_TEST(test_read_past_eof);
    RUN_TEST(test_write_file);
    RUN_TEST(test_write_then_read_roundtrip);
    RUN_TEST(test_negotiate_malformed);
    RUN_TEST(test_negotiate_dropped);
    RUN_TEST(test_session1_bad_header);
    RUN_TEST(test_session1_wrong_status);
    RUN_TEST(test_session1_bad_body);
    RUN_TEST(test_session1_no_secbuf);
    RUN_TEST(test_session1_bad_spnego);
    RUN_TEST(test_session1_bad_ntlmssp);
    RUN_TEST(test_session2_dropped);
    RUN_TEST(test_session2_bad_header);
    RUN_TEST(test_tree_dropped);
    RUN_TEST(test_tree_bad_body);
    RUN_TEST(test_create_dropped);
    RUN_TEST(test_create_bad_body);
    RUN_TEST(test_long_share_overflow);
    RUN_TEST(test_long_path_overflow);
    RUN_TEST(test_long_user_overflow);
    RUN_TEST(test_challenge_ti_ntlmv2_overflow);
    RUN_TEST(test_challenge_ti_authenticate_overflow);
    RUN_TEST(test_challenge_ti_spnego_overflow);
    RUN_TEST(test_av_eol_only);
    RUN_TEST(test_av_skip_then_find);
    RUN_TEST(test_av_truncated_timestamp);
    RUN_TEST(test_read_arg);
    RUN_TEST(test_read_send_io);
    RUN_TEST(test_read_recv_io);
    RUN_TEST(test_read_bad_header);
    RUN_TEST(test_read_status_error);
    RUN_TEST(test_read_bad_body);
    RUN_TEST(test_read_data_too_long);
    RUN_TEST(test_read_zero_data);
    RUN_TEST(test_write_arg);
    RUN_TEST(test_write_send_io);
    RUN_TEST(test_write_recv_io);
    RUN_TEST(test_write_recv_overflow);
    RUN_TEST(test_write_bad_header);
    RUN_TEST(test_write_status_error);
    RUN_TEST(test_write_bad_body);
    RUN_TEST(test_write_zero_count);
    RUN_TEST(test_write_count_too_big);
    RUN_TEST(test_close_arg);
    RUN_TEST(test_close_send_io);
    RUN_TEST(test_close_recv_overflow);
    RUN_TEST(test_close_recv_zero_len);
    RUN_TEST(test_close_recv_trunc_body);
    RUN_TEST(test_close_bad_header);
    RUN_TEST(test_close_status_error);
    RUN_TEST(test_close_bad_body);
    RUN_TEST(test_open_arg_remaining_nulls);
    RUN_TEST(test_open_null_domain);
    RUN_TEST(test_tree_bad_header);
    RUN_TEST(test_create_bad_header);
    RUN_TEST(test_read_write_null_seam);
    RUN_TEST(test_read_recv_overflow);
    RUN_TEST(test_read_eof_status);
    RUN_TEST(test_write_no_extend);
    RUN_TEST(test_close_bad_transport_prefix);
    RUN_TEST(test_signed_session_roundtrip);
    RUN_TEST(test_signed_response_tampered);
    RUN_TEST(test_unsigned_session_when_not_required);
    RUN_TEST(test_open_signed_311_roundtrip);
    RUN_TEST(test_signed_311_response_tampered);
    RUN_TEST(test_open_encrypted_311_roundtrip);
    RUN_TEST(test_open_encrypted_all_ciphers);
    RUN_TEST(test_open_encrypted_share_requires_client_force);
    RUN_TEST(test_encrypted_response_tampered);
    return UNITY_END();
}
