// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file smb_client.c
 * @brief SMB2 client dialogue engine (see smb_client.h). Drives the wire codecs through the real
 *        NEGOTIATE / NTLMv2 SESSION_SETUP / TREE_CONNECT / CREATE exchange over a send/recv seam.
 */

#include "smb_client.h"

#if PC_ENABLE_SMB

#include "ntlm.h"
#include "ntlmssp.h"
#include "smb2.h"
#include "spnego.h"
#include <string.h>

// Every request this engine builds has to fit the shared tx buffer, and the request builders report
// that by returning 0. Pin the relationship instead of leaving each `if (!mlen)` to hope for it:
// PC_SMB_BUF is a plain #ifndef with no floor, so without this a small override would silently turn
// every exchange into SMB_ERR_OVERFLOW at run time rather than failing the build.
//
// CREATE is the binding case - it frames a 64-byte SMB2 header + a 56-byte body around a path that
// utf16le() may fill to sizeof(s_smb.utf16) == PC_SMB_BUF/2, and the builders are handed
// sizeof(tx) - 4 (the 4-byte NetBIOS length prefix is written separately):
static_assert(64 + 56 + PC_SMB_BUF / 2 <= PC_SMB_BUF - 4,
              "PC_SMB_BUF is too small to frame a CREATE around a full-length path "
              "(64B header + 56B body + PC_SMB_BUF/2 path must fit PC_SMB_BUF-4)");
// WRITE is the other payload-bearing case; its chunk_max already backs off by 128 bytes.
static_assert(64 + 48 + (PC_SMB_BUF - 128) <= PC_SMB_BUF - 4,
              "PC_SMB_BUF is too small to frame a WRITE around a full chunk");
// SESSION_SETUP round 2 wraps the NTLMSSP AUTHENTICATE blob, capped at sizeof(s_smb.sp2).
static_assert(64 + 24 + PC_SMB_BUF / 2 <= PC_SMB_BUF - 4,
              "PC_SMB_BUF is too small to frame a SESSION_SETUP around a full security blob");

// ASCII/Latin-1 -> UTF-16LE (SMB paths are ASCII); returns byte length (2 * chars), 0 on null/overflow.
static size_t utf16le(const char *s, uint8_t *out, size_t cap)
{
    // Dead guard: utf16le is static with exactly two call sites (the share in smb_tree_connect and
    // the path in smb_create), both reached only through smb_open(), which rejects a null
    // cfg->share / cfg->path up front.
    if (!s) // GCOVR_EXCL_LINE - both callers are guarded by smb_open, see above
    {
        return 0; // GCOVR_EXCL_LINE - unreachable body of the guard above
    }
    size_t n = 0;
    for (; s[n]; n++)
    {
        if ((n * 2 + 2) > cap)
        {
            return 0;
        }
        out[n * 2] = (uint8_t)s[n];
        out[n * 2 + 1] = 0;
    }
    return n * 2;
}

// Find the MsvAvTimestamp (AvId 7) FILETIME in a CHALLENGE target-info blob; copy 8 bytes, else 0-fill.
static void find_av_timestamp(const uint8_t *ti, size_t ti_len, uint8_t out[8])
{
    memset(out, 0, 8);
    size_t p = 0;
    while (p + 4 <= ti_len)
    {
        uint16_t id = (uint16_t)(ti[p] | (ti[p + 1] << 8));
        uint16_t len = (uint16_t)(ti[p + 2] | (ti[p + 3] << 8));
        p += 4;
        if (id == 0) // MsvAvEOL
        {
            break;
        }
        if (id == 7 && len == 8 && p + 8 <= ti_len)
        {
            memcpy(out, ti + p, 8);
            return;
        }
        p += len;
    }
}

static proto_bool read_exact(SmbRecvFn recv, void *ctx, uint8_t *buf, size_t n)
{
    size_t got = 0;
    while (got < n)
    {
        int r = recv(ctx, buf + got, n - got);
        if (r <= 0)
        {
            return PROTO_FALSE;
        }
        got += (size_t)r;
    }
    return PROTO_TRUE;
}

// Frame the SMB2 message that sits at frame+4 (msg_len bytes) with the Direct-TCP prefix and send it.
static proto_bool send_msg(SmbSendFn send, void *ctx, uint8_t *frame, size_t msg_len)
{
    frame[0] = 0x00;
    frame[1] = (uint8_t)(msg_len >> 16);
    frame[2] = (uint8_t)(msg_len >> 8);
    frame[3] = (uint8_t)msg_len;
    size_t total = 4 + msg_len;
    return send(ctx, frame, total) == (int)total;
}

// Receive one Direct-TCP-framed SMB2 message into rx; return its length, -2 on overflow, -1 on IO.
static int recv_msg(SmbRecvFn recv, void *ctx, uint8_t *rx, size_t cap)
{
    uint8_t pre[4];
    if (!read_exact(recv, ctx, pre, 4) || pre[0] != 0x00)
    {
        return -1;
    }
    size_t len = ((size_t)pre[1] << 16) | ((size_t)pre[2] << 8) | pre[3];
    if (len == 0 || len > cap)
    {
        return len ? -2 : -1;
    }
    if (!read_exact(recv, ctx, rx, len))
    {
        return -1;
    }
    return (int)len;
}

// SMB dialogue working buffers, kept off the caller's stack: smb_open alone needs ~4 KB, which
// overflows the default 8 KB Arduino loopTask (seen on HW as "Stack canary watchpoint triggered").
// The client drives one sequential dialogue at a time (open -> read/write -> close), so a single
// owned working set is correct; it is not reentrant across two concurrent SMB connections.
typedef struct
{
    uint8_t tx[PC_SMB_BUF];
    uint8_t rx[PC_SMB_BUF];
    uint8_t nt_resp[PC_SMB_BUF / 2];
    uint8_t ntauth[PC_SMB_BUF / 2];
    uint8_t sp2[PC_SMB_BUF / 2];
    uint8_t utf16[PC_SMB_BUF / 2];
    uint8_t ti[PC_SMB_BUF / 2]; ///< the CHALLENGE target-info with the MsvAvFlags MIC bit set (NTLMv2 input)
} SmbClientCtx;
static SmbClientCtx s_smb;

// SMB message-signing state for a session: the algorithm (HMAC-SHA256 for SMB 2.x, AES-CMAC for
// SMB 3.x), the 16-byte signing key, and whether the server required signing. When active, every
// request this engine sends is signed in place and every response must carry a matching signature
// (MS-SMB2 §3.1.4.1 / §3.1.5.1).
typedef struct
{
    proto_bool active;
    Smb2SignAlgo algo;
    uint8_t key[16];
} SmbSign;

// SMB 3.x transport-encryption state for a session. Once @ref active, every request is wrapped in a
// TRANSFORM_HEADER (the negotiated AEAD: AES-128/256-GCM or AES-128/256-CCM) instead of signed, and every
// response must decrypt (MS-SMB2 §3.1.4.3/4). @ref available means the cipher keys were derived (the server
// negotiated a cipher) even before encryption is required. @ref cipher is the negotiated Smb2Cipher id (it
// selects the key + nonce length). @ref nonce is a monotonic per-session counter - it MUST never repeat for a
// given key, so callers persist it across requests (on the SmbHandle for read/write/close).
typedef struct
{
    proto_bool active;
    proto_bool available;
    uint16_t cipher;
    uint8_t c2s[PC_SMB2_MAX_CIPHER_KEY_LEN];
    uint8_t s2c[PC_SMB2_MAX_CIPHER_KEY_LEN];
    uint64_t session_id;
    uint64_t nonce;
} SmbCrypt;

// Sign / verify a message with the session's negotiated algorithm.
static void smb_apply_sign(const SmbSign *s, uint8_t *msg, size_t len)
{
    if (s->algo == SMB2_SIGN_ALGO_AES_CMAC)
    {
        pc_smb2_sign_cmac(s->key, msg, len);
    }
    else
    {
        pc_smb2_sign(s->key, msg, len);
    }
}
static proto_bool smb_check_sign(const SmbSign *s, uint8_t *msg, size_t len)
{
    return s->algo == SMB2_SIGN_ALGO_AES_CMAC ? pc_smb2_verify_cmac(s->key, msg, len)
                                              : pc_smb2_verify(s->key, msg, len);
}

// Send the framed message currently in s_smb.tx (mlen bytes at tx+4) and receive the reply into s_smb.rx.
// When @p sign is active the request is signed before sending and the response signature is verified
// (a missing or wrong signature fails closed as SMB_ERR_PROTOCOL). Returns the reply length (>=0), or
// -1 with *res set to the mapped IO / overflow / protocol error.
static int smb_round_trip(SmbSendFn send, SmbRecvFn recv, void *ctx, size_t mlen, const SmbSign *sign, SmbCrypt *crypt,
                          SmbResult *res)
{
    // Encrypted path (SMB 3.x): wrap the plaintext request (tx+4) in a TRANSFORM_HEADER into rx+4, send it,
    // receive the wrapped reply into rx, and decrypt it in place. Encryption supersedes signing (the AEAD tag
    // is the integrity check), so a message is never both encrypted and signed.
    if (crypt && crypt->active)
    {
        uint8_t nonce[PC_SMB2_NONCE_FIELD_LEN] = {0};
        const uint64_t ctr = crypt->nonce++; // unique per key: never reuse a nonce, so advance every message
        for (int i = 0; i < 8; i++)
        {
            nonce[i] = (uint8_t)(ctr >> (8 * i));
        }
        size_t tlen = pc_smb2_encrypt(crypt->cipher, crypt->c2s, nonce, crypt->session_id, s_smb.tx + 4, mlen,
                                      s_smb.rx + 4, sizeof(s_smb.rx) - 4);
        if (tlen == 0)
        {
            *res = SMB_ERR_OVERFLOW;
            return -1;
        }
        if (!send_msg(send, ctx, s_smb.rx, tlen))
        {
            *res = SMB_ERR_IO;
            return -1;
        }
        int rl = recv_msg(recv, ctx, s_smb.rx, sizeof(s_smb.rx));
        if (rl < 0)
        {
            *res = (rl == -2) ? SMB_ERR_OVERFLOW : SMB_ERR_IO;
            return -1;
        }
        // Decrypt in place: pc_smb2_decrypt GHASHes the whole ciphertext before the CTR pass, so an
        // out == in (backward-shifted) overlap is safe. Fails closed on a bad tag / non-TRANSFORM reply.
        size_t plen = pc_smb2_decrypt(crypt->cipher, crypt->s2c, s_smb.rx, (size_t)rl, s_smb.rx, sizeof(s_smb.rx));
        if (plen == 0)
        {
            *res = SMB_ERR_PROTOCOL;
            return -1;
        }
        return (int)plen;
    }

    if (sign && sign->active)
    {
        smb_apply_sign(sign, s_smb.tx + 4, mlen);
    }
    if (!send_msg(send, ctx, s_smb.tx, mlen))
    {
        *res = SMB_ERR_IO;
        return -1;
    }
    int rl = recv_msg(recv, ctx, s_smb.rx, sizeof(s_smb.rx));
    if (rl < 0)
    {
        *res = (rl == -2) ? SMB_ERR_OVERFLOW : SMB_ERR_IO;
        return -1;
    }
    if (sign && sign->active && !smb_check_sign(sign, s_smb.rx, (size_t)rl))
    {
        *res = SMB_ERR_PROTOCOL;
        return -1;
    }
    return rl;
}

// Step 1 - NEGOTIATE: advertise SMB 2.0.2 .. 3.1.1 (with the mandatory 3.1.1 preauth-integrity + an
// AES-CMAC signing context) and confirm the server's negotiate response parses. Reports the server's
// SecurityMode in *sec_mode and the chosen DialectRevision in *dialect (so smb_open picks HMAC vs
// AES-CMAC signing), and seeds + folds the NEGOTIATE request/response into the 3.1.1 preauth-integrity
// hash chain (MS-SMB2 §3.1.5.2). The salt is a fresh random blob; it lives only in the request bytes
// that feed the hash, so it needs no separate storage.
static SmbResult smb_negotiate(SmbSendFn send, SmbRecvFn recv, void *ctx, uint16_t *sec_mode, uint16_t *dialect,
                               uint16_t *cipher, SmbPreauth *preauth, const uint16_t *offer_ciphers, size_t offer_count)
{
    uint8_t guid[16];
    uint8_t salt[32];
    pc_platform_rand_fill(guid, 16);
    pc_platform_rand_fill(salt, sizeof(salt));
    size_t mlen = pc_smb2_build_negotiate_311(s_smb.tx + 4, sizeof(s_smb.tx) - 4, guid,
                                              Smb2SecurityMode::SMB2_NEGOTIATE_SIGNING_ENABLED, salt, sizeof(salt),
                                              offer_ciphers, offer_count);
    if (!mlen) // GCOVR_EXCL_LINE - the static_assert at the top of this file makes this unreachable
    {
        return SMB_ERR_OVERFLOW; // GCOVR_EXCL_LINE - unreachable body of the guard above
    }

    // Seed the preauth-integrity hash and fold the NEGOTIATE request (the bytes are final - NEGOTIATE is
    // never signed). The chain is only consumed when the server chooses 3.1.1, but folding is harmless
    // otherwise.
    pc_smb_preauth_init(preauth);
    pc_smb_preauth_update(preauth, s_smb.tx + 4, mlen);

    SmbResult rt = SMB_ERR_IO;
    // NEGOTIATE precedes authentication, so there is no session key yet: never signed.
    int rl = smb_round_trip(send, recv, ctx, mlen, NULL, NULL, &rt);
    if (rl < 0)
    {
        return rt;
    }
    pc_smb_preauth_update(preauth, s_smb.rx, (size_t)rl); // fold the NEGOTIATE response
    Smb2NegotiateResp neg;
    if (!pc_smb2_parse_negotiate_response(s_smb.rx, (size_t)rl, &neg))
    {
        return SMB_ERR_PROTOCOL;
    }
    *sec_mode = neg.security_mode;
    *dialect = neg.dialect;
    // The negotiated encryption cipher lives in the 3.1.1 ENCRYPTION_CAPABILITIES context. 0 = none offered /
    // accepted -> the session stays unencrypted. The server picks one of the ciphers we offered (any of the
    // four SMB 3.1.1 ciphers - AES-128/256-GCM, AES-128/256-CCM), in our preference order.
    *cipher = 0;
    if (neg.dialect == (uint16_t)SMB2_DIALECT_0311)
    {
        Smb2NegotiateContexts nc;
        if (pc_smb2_parse_negotiate_contexts(s_smb.rx, (size_t)rl, &nc) && nc.have_encryption)
        {
            *cipher = nc.cipher;
        }
    }
    return SMB_OK;
}

// Steps 2-4 - NTLMv2 SESSION_SETUP: SPNEGO/NTLMSSP negotiate, compute the NTLMv2 response to the server
// challenge, then authenticate. Fills *session_id from the server's SessionId, threads the SESSION_SETUP
// messages through the 3.1.1 preauth-integrity chain (@p preauth, seeded by smb_negotiate), and - when
// the server required signing (@p want_signing) and the session is not GUEST/NULL - fills *sign with the
// per-dialect signer: HMAC-SHA256 over the NTLMv2 session key for SMB 2.x, or AES-CMAC over the
// SP800-108-derived signing key (from the final preauth hash) for SMB 3.x, so every later request signs.
static SmbResult smb_session_setup(const SmbConfig *cfg, const char *domain, proto_bool want_signing, uint16_t dialect,
                                   uint16_t cipher, SmbPreauth *preauth, SmbSendFn send, SmbRecvFn recv, void *ctx,
                                   uint64_t *session_id, SmbSign *sign, SmbCrypt *crypt)
{
    // 2. SESSION_SETUP round 1: NTLMSSP NEGOTIATE wrapped in SPNEGO
    uint8_t ntneg[64];
    uint8_t sp1[128];
    size_t ntneg_n = pc_ntlmssp_build_negotiate(ntneg, sizeof(ntneg), NtlmsspFlags::NTLMSSP_CLIENT_DEFAULT_FLAGS);
    size_t sp1_n = pc_spnego_wrap_negotiate(ntneg, ntneg_n, sp1, sizeof(sp1));
    size_t mlen = pc_smb2_build_session_setup(s_smb.tx + 4, sizeof(s_smb.tx) - 4, 1, 0,
                                              Smb2SecurityMode::SMB2_NEGOTIATE_SIGNING_ENABLED, sp1, sp1_n);
    if (!mlen) // GCOVR_EXCL_LINE - the static_assert at the top of this file makes this unreachable
    {
        return SMB_ERR_OVERFLOW; // GCOVR_EXCL_LINE - unreachable body of the guard above
    }
    pc_smb_preauth_update(preauth, s_smb.tx + 4, mlen); // fold SESSION_SETUP request 1 (unsigned)
    SmbResult rt = SMB_ERR_IO;
    // Round 1 precedes the session key, so it is never signed.
    int rl = smb_round_trip(send, recv, ctx, mlen, NULL, NULL, &rt);
    if (rl < 0)
    {
        return rt;
    }
    pc_smb_preauth_update(preauth, s_smb.rx, (size_t)rl); // fold SESSION_SETUP response 1
    Smb2Header h1;
    if (!pc_smb2_parse_header(s_smb.rx, (size_t)rl, &h1) ||
        h1.status != Smb2Status::SMB2_STATUS_MORE_PROCESSING_REQUIRED)
    {
        return SMB_ERR_AUTH;
    }
    *session_id = h1.session_id;
    Smb2SessionSetupResp ss1;
    if (!pc_smb2_parse_session_setup_response(s_smb.rx, (size_t)rl, &ss1) || !ss1.sec_buf)
    {
        return SMB_ERR_PROTOCOL;
    }
    const uint8_t *chal_tok = NULL;
    size_t chal_len = 0;
    if (!pc_spnego_parse_response(ss1.sec_buf, ss1.sec_buf_len, &chal_tok, &chal_len))
    {
        return SMB_ERR_PROTOCOL;
    }
    NtlmChallenge ch;
    if (!pc_ntlmssp_parse_challenge(chal_tok, chal_len, &ch))
    {
        return SMB_ERR_PROTOCOL;
    }

    // 3. Compute the NTLMv2 response and build the AUTHENTICATE with a MIC (MS-NLMP §3.1.5.1.2).
    uint8_t nt_hash[16];
    uint8_t owf[16];
    pc_ntlm_nt_hash(cfg->pass, nt_hash);
    if (!pc_ntlm_ntowfv2(nt_hash, cfg->user, domain, owf))
    {
        return SMB_ERR_OVERFLOW;
    }
    uint8_t cli_chal[8];
    uint8_t ts[8];
    uint8_t skey[16];
    pc_platform_rand_fill(cli_chal, 8);
    find_av_timestamp(ch.target_info, ch.target_info_len, ts);
    // Set the MsvAvFlags "MIC provided" bit in the target-info the NTLMv2 response is computed over, so a
    // server that enforces the MIC accepts it and verifies the digest attached below.
    size_t ti_len = pc_ntlm_set_mic_flag(ch.target_info, ch.target_info_len, s_smb.ti, sizeof(s_smb.ti));
    if (!ti_len)
    {
        return SMB_ERR_OVERFLOW;
    }
    size_t nt_len = pc_ntlm_v2_response(owf, ch.server_challenge, cli_chal, ts, s_smb.ti, ti_len, s_smb.nt_resp,
                                        sizeof(s_smb.nt_resp), skey);
    if (!nt_len)
    {
        return SMB_ERR_OVERFLOW;
    }
    size_t ntauth_n = pc_ntlmssp_build_authenticate(s_smb.ntauth, sizeof(s_smb.ntauth), NULL, 0, s_smb.nt_resp, nt_len,
                                                    domain, cfg->user, cfg->workstation, ch.flags, PROTO_TRUE);
    if (!ntauth_n)
    {
        return SMB_ERR_OVERFLOW;
    }
    // MIC = HMAC-MD5(session key, NEGOTIATE || CHALLENGE || AUTHENTICATE); write it into the zeroed field.
    uint8_t mic[PC_NTLMSSP_MIC_LEN];
    pc_ntlm_mic(skey, ntneg, ntneg_n, chal_tok, chal_len, s_smb.ntauth, ntauth_n, mic);
    memcpy(s_smb.ntauth + PC_NTLMSSP_MIC_OFFSET, mic, PC_NTLMSSP_MIC_LEN);
    size_t sp2_n = pc_spnego_wrap_authenticate(s_smb.ntauth, ntauth_n, s_smb.sp2, sizeof(s_smb.sp2));
    if (!sp2_n)
    {
        return SMB_ERR_OVERFLOW;
    }

    // 4. SESSION_SETUP round 2 (echo the server SessionId). This request completes authentication and is
    // folded into the preauth chain (unsigned), whose final value derives the SMB 3.x signing key.
    mlen = pc_smb2_build_session_setup(s_smb.tx + 4, sizeof(s_smb.tx) - 4, 2, *session_id,
                                       Smb2SecurityMode::SMB2_NEGOTIATE_SIGNING_ENABLED, s_smb.sp2, sp2_n);
    if (!mlen) // GCOVR_EXCL_LINE - the static_assert at the top of this file makes this unreachable
    {
        return SMB_ERR_OVERFLOW; // GCOVR_EXCL_LINE - unreachable body of the guard above
    }
    pc_smb_preauth_update(preauth, s_smb.tx + 4, mlen); // fold request 2 -> the key-derivation hash is now final

    // Select the session signer from the negotiated dialect: SMB 3.x (>= 3.0) signs with AES-CMAC over the
    // SP800-108-derived key (3.1.1 mixes in the preauth hash); SMB 2.x signs with HMAC-SHA256 over the
    // NTLMv2 session key. For SMB 2.x we sign request 2 with that key so a signing-required 2.x server
    // accepts it; SMB 3.x leaves request 2 unsigned (the derived key signs from TREE_CONNECT onward,
    // matching Windows / Samba / impacket).
    const Smb2SignAlgo algo =
        dialect >= (uint16_t)SMB2_DIALECT_0300 ? SMB2_SIGN_ALGO_AES_CMAC : SMB2_SIGN_ALGO_HMAC_SHA256;
    uint8_t sign_key[16];
    if (algo == SMB2_SIGN_ALGO_AES_CMAC)
    {
        const proto_bool is_311 = dialect == (uint16_t)SMB2_DIALECT_0311;
        pc_smb3_derive_signing_key(skey, dialect, is_311 ? preauth->hash : NULL, sign_key);
    }
    else
    {
        memcpy(sign_key, skey, sizeof(sign_key));
        if (want_signing)
        {
            pc_smb2_sign(skey, s_smb.tx + 4, mlen);
        }
    }

    rl = smb_round_trip(send, recv, ctx, mlen, NULL, NULL, &rt);
    if (rl < 0)
    {
        return rt;
    }
    Smb2Header h2;
    if (!pc_smb2_parse_header(s_smb.rx, (size_t)rl, &h2))
    {
        return SMB_ERR_PROTOCOL;
    }
    if (h2.status != Smb2Status::SMB2_STATUS_SUCCESS)
    {
        return SMB_ERR_AUTH;
    }
    // A GUEST or anonymous (NULL) session is never signed even if signing was negotiated (MS-SMB2
    // §3.2.5.3.1); anything else with the server requiring signing signs the rest of the session.
    Smb2SessionSetupResp ss2;
    proto_bool guest_or_null = PROTO_FALSE;
    uint16_t sess_flags = 0;
    if (pc_smb2_parse_session_setup_response(s_smb.rx, (size_t)rl, &ss2))
    {
        sess_flags = ss2.session_flags;
        guest_or_null = (sess_flags & (Smb2SessionFlags::SMB2_SESSION_FLAG_IS_GUEST |
                                       Smb2SessionFlags::SMB2_SESSION_FLAG_IS_NULL)) != 0;
    }
    sign->active = want_signing && !guest_or_null;
    sign->algo = algo;
    memcpy(sign->key, sign_key, sizeof(sign->key));

    // SMB 3.x transport encryption: derive the C2S/S2C cipher keys if the server negotiated a cipher (SMB 3.x,
    // non-guest), sized by the cipher (16 for -128, 32 for -256). A server that requires encryption globally
    // flags the session encrypt-required; a share can also require it (checked at TREE_CONNECT). But a share
    // that requires encryption rejects the unencrypted TREE_CONNECT before it can advertise the share flag, so
    // a caller that wants such a share sets cfg->encrypt to force encryption on from TREE_CONNECT (client-forced,
    // like smbclient -e; MS-SMB2 §3.2.4.1.5). Otherwise the keys sit ready (available) but inactive.
    crypt->active = PROTO_FALSE;
    crypt->available = PROTO_FALSE;
    crypt->cipher = 0;
    crypt->session_id = *session_id;
    crypt->nonce = 0;
    const size_t cipher_key_len = pc_smb2_cipher_key_len(cipher);
    if (cipher_key_len != 0 && dialect >= (uint16_t)SMB2_DIALECT_0300 && !guest_or_null)
    {
        const proto_bool is_311 = dialect == (uint16_t)SMB2_DIALECT_0311;
        crypt->available = pc_smb3_derive_encryption_keys(skey, dialect, is_311 ? preauth->hash : NULL, cipher_key_len,
                                                          crypt->c2s, crypt->s2c);
        if (crypt->available)
        {
            crypt->cipher = cipher;
            if ((sess_flags & Smb2SessionFlags::SMB2_SESSION_FLAG_ENCRYPT_DATA) || cfg->encrypt)
            {
                crypt->active = PROTO_TRUE;
            }
        }
    }
    return SMB_OK;
}

// Step 5 - TREE_CONNECT to \\server\share. Fills *tree_id.
static SmbResult smb_tree_connect(const SmbConfig *cfg, uint64_t session_id, const SmbSign *sign, SmbSendFn send,
                                  SmbRecvFn recv, void *ctx, uint32_t *tree_id, SmbCrypt *crypt)
{
    size_t utf16_n = utf16le(cfg->share, s_smb.utf16, sizeof(s_smb.utf16));
    if (!utf16_n)
    {
        return SMB_ERR_OVERFLOW;
    }
    size_t mlen = pc_smb2_build_tree_connect(s_smb.tx + 4, sizeof(s_smb.tx) - 4, 3, session_id, s_smb.utf16, utf16_n);
    if (!mlen) // GCOVR_EXCL_LINE - the static_assert at the top of this file makes this unreachable
    {
        return SMB_ERR_OVERFLOW; // GCOVR_EXCL_LINE - unreachable body of the guard above
    }
    SmbResult rt = SMB_ERR_IO;
    int rl = smb_round_trip(send, recv, ctx, mlen, sign, crypt, &rt);
    if (rl < 0)
    {
        return rt;
    }
    Smb2Header h3;
    Smb2TreeConnectResp tc;
    if (!pc_smb2_parse_header(s_smb.rx, (size_t)rl, &h3) || h3.status != Smb2Status::SMB2_STATUS_SUCCESS)
    {
        return SMB_ERR_PROTOCOL;
    }
    if (!pc_smb2_parse_tree_connect_response(s_smb.rx, (size_t)rl, &tc))
    {
        return SMB_ERR_PROTOCOL;
    }
    *tree_id = h3.tree_id;
    // A share flagged encrypt-data turns encryption on for everything from CREATE onward (MS-SMB2 §3.2.5.5),
    // provided the cipher keys were derived at session setup.
    if (crypt && crypt->available && (tc.share_flags & Smb2ShareFlags::SMB2_SHAREFLAG_ENCRYPT_DATA))
    {
        crypt->active = PROTO_TRUE;
    }
    return SMB_OK;
}

// Step 6 - CREATE (open) the file; fills the handle h on success.
static SmbResult smb_create(const SmbConfig *cfg, SmbHandle *h, uint64_t session_id, uint32_t tree_id,
                            const SmbSign *sign, SmbCrypt *crypt, SmbSendFn send, SmbRecvFn recv, void *ctx)
{
    size_t utf16_n = utf16le(cfg->path, s_smb.utf16, sizeof(s_smb.utf16));
    if (!utf16_n)
    {
        return SMB_ERR_OVERFLOW;
    }
    size_t mlen =
        pc_smb2_build_create(s_smb.tx + 4, sizeof(s_smb.tx) - 4, 4, session_id, tree_id, cfg->desired_access,
                             Smb2ShareAccess::SMB2_FILE_SHARE_READ | Smb2ShareAccess::SMB2_FILE_SHARE_WRITE,
                             cfg->disposition, Smb2CreateOptions::SMB2_FILE_NON_DIRECTORY_FILE, s_smb.utf16, utf16_n);
    if (!mlen) // GCOVR_EXCL_LINE - the static_assert at the top of this file makes this unreachable
    {
        return SMB_ERR_OVERFLOW; // GCOVR_EXCL_LINE - unreachable body of the guard above
    }
    SmbResult rt = SMB_ERR_IO;
    int rl = smb_round_trip(send, recv, ctx, mlen, sign, crypt, &rt);
    if (rl < 0)
    {
        return rt;
    }
    Smb2Header h4;
    Smb2CreateResp cr;
    if (!pc_smb2_parse_header(s_smb.rx, (size_t)rl, &h4) || h4.status != Smb2Status::SMB2_STATUS_SUCCESS)
    {
        return SMB_ERR_PROTOCOL;
    }
    if (!pc_smb2_parse_create_response(s_smb.rx, (size_t)rl, &cr))
    {
        return SMB_ERR_PROTOCOL;
    }
    h->session_id = session_id;
    h->tree_id = tree_id;
    memcpy(h->file_id, cr.file_id, 16);
    h->file_size = cr.end_of_file;
    h->next_message_id = 5;
    h->signing_active = sign->active;
    h->signing_algo = sign->algo;
    memcpy(h->signing_key, sign->key, sizeof(h->signing_key));
    // Carry the encryption state onto the handle so read/write/close keep encrypting with the same keys and a
    // continuing nonce (the counter must not restart, or a nonce would repeat under the same key).
    h->encrypt_active = crypt->active;
    h->enc_cipher = crypt->cipher;
    memcpy(h->enc_c2s, crypt->c2s, sizeof(h->enc_c2s));
    memcpy(h->enc_s2c, crypt->s2c, sizeof(h->enc_s2c));
    h->enc_nonce = crypt->nonce;
    return SMB_OK;
}

SmbResult smb_open(const SmbConfig *cfg, SmbHandle *h, SmbSendFn send, SmbRecvFn recv, void *ctx)
{
    if (!cfg || !h || !send || !recv || !cfg->user || !cfg->pass || !cfg->share || !cfg->path)
    {
        return SMB_ERR_ARG;
    }

    const char *domain = cfg->domain ? cfg->domain : "";

    // Offer all four SMB 3.1.1 ciphers in preference order (a server selects the first it supports). cfg->
    // cipher_pref, when set, is moved to the front so a caller can pin a specific cipher (used to exercise
    // each one against a real server).
    uint16_t offer[PC_SMB2_MAX_OFFER_CIPHERS] = {
        Smb2Cipher::SMB2_ENCRYPTION_AES128_GCM, Smb2Cipher::SMB2_ENCRYPTION_AES256_GCM,
        Smb2Cipher::SMB2_ENCRYPTION_AES128_CCM, Smb2Cipher::SMB2_ENCRYPTION_AES256_CCM};
    if (cfg->cipher_pref != 0 && pc_smb2_cipher_key_len(cfg->cipher_pref) != 0)
    {
        for (size_t i = 1; i < PC_SMB2_MAX_OFFER_CIPHERS; i++)
        {
            if (offer[i] == cfg->cipher_pref)
            {
                for (size_t j = i; j > 0; j--)
                {
                    offer[j] = offer[j - 1];
                }
                offer[0] = cfg->cipher_pref;
                break;
            }
        }
    }

    uint16_t sec_mode = 0;
    uint16_t dialect = 0;
    uint16_t cipher = 0;
    SmbPreauth preauth;
    SmbResult r =
        smb_negotiate(send, recv, ctx, &sec_mode, &dialect, &cipher, &preauth, offer, PC_SMB2_MAX_OFFER_CIPHERS);
    if (r != SMB_OK)
    {
        return r;
    }
    // The client advertises SIGNING_ENABLED, so the session is signed exactly when the server requires it.
    proto_bool want_signing = (sec_mode & Smb2SecurityMode::SMB2_NEGOTIATE_SIGNING_REQUIRED) != 0;

    SmbSign sign = {PROTO_FALSE, SMB2_SIGN_ALGO_HMAC_SHA256, {0}};
    SmbCrypt crypt = {PROTO_FALSE, PROTO_FALSE, 0, {0}, {0}, 0, 0};
    uint64_t session_id = 0;
    r = smb_session_setup(cfg, domain, want_signing, dialect, cipher, &preauth, send, recv, ctx, &session_id, &sign,
                          &crypt);
    if (r != SMB_OK)
    {
        return r;
    }

    uint32_t tree_id = 0;
    r = smb_tree_connect(cfg, session_id, &sign, send, recv, ctx, &tree_id, &crypt);
    if (r != SMB_OK)
    {
        return r;
    }

    return smb_create(cfg, h, session_id, tree_id, &sign, &crypt, send, recv, ctx);
}

SmbResult smb_close(SmbHandle *h, SmbSendFn send, SmbRecvFn recv, void *ctx)
{
    if (!h || !send || !recv)
    {
        return SMB_ERR_ARG;
    }
    size_t mlen = pc_smb2_build_close(s_smb.tx + 4, sizeof(s_smb.tx) - 4, h->next_message_id, h->session_id, h->tree_id,
                                      h->file_id);
    if (!mlen) // GCOVR_EXCL_LINE - the static_assert at the top of this file makes this unreachable
    {
        return SMB_ERR_OVERFLOW; // GCOVR_EXCL_LINE - unreachable body of the guard above
    }
    SmbSign sign = {h->signing_active, h->signing_algo, {0}};
    memcpy(sign.key, h->signing_key, sizeof(sign.key));
    SmbCrypt crypt = {h->encrypt_active, h->encrypt_active, h->enc_cipher, {0}, {0}, h->session_id, h->enc_nonce};
    memcpy(crypt.c2s, h->enc_c2s, sizeof(crypt.c2s));
    memcpy(crypt.s2c, h->enc_s2c, sizeof(crypt.s2c));
    SmbResult rt = SMB_ERR_IO;
    int rl = smb_round_trip(send, recv, ctx, mlen, &sign, &crypt, &rt);
    h->enc_nonce = crypt.nonce; // persist the advanced nonce (must never repeat under the same key)
    if (rl < 0)
    {
        return rt;
    }
    Smb2Header hd;
    Smb2CloseResp cl;
    if (!pc_smb2_parse_header(s_smb.rx, (size_t)rl, &hd) || hd.status != Smb2Status::SMB2_STATUS_SUCCESS)
    {
        return SMB_ERR_PROTOCOL;
    }
    if (!pc_smb2_parse_close_response(s_smb.rx, (size_t)rl, &cl))
    {
        return SMB_ERR_PROTOCOL;
    }
    h->next_message_id++;
    return SMB_OK;
}

SmbResult smb_read(SmbHandle *h, uint64_t offset, uint8_t *out, size_t cap, size_t *out_len, SmbSendFn send,
                   SmbRecvFn recv, void *ctx)
{
    if (!h || !out || !out_len || !send || !recv)
    {
        return SMB_ERR_ARG;
    }
    *out_len = 0;
    SmbSign sign = {h->signing_active, h->signing_algo, {0}};
    memcpy(sign.key, h->signing_key, sizeof(sign.key));
    SmbCrypt crypt = {h->encrypt_active, h->encrypt_active, h->enc_cipher, {0}, {0}, h->session_id, h->enc_nonce};
    memcpy(crypt.c2s, h->enc_c2s, sizeof(crypt.c2s));
    memcpy(crypt.s2c, h->enc_s2c, sizeof(crypt.s2c));
    const size_t chunk_max = PC_SMB_BUF - 96; // room for the header + READ response body
    size_t total = 0;
    while (total < cap)
    {
        size_t want = cap - total;
        if (want > chunk_max)
        {
            want = chunk_max;
        }
        size_t mlen = pc_smb2_build_read(s_smb.tx + 4, sizeof(s_smb.tx) - 4, h->next_message_id, h->session_id,
                                         h->tree_id, h->file_id, (uint32_t)want, offset + total);
        if (!mlen) // GCOVR_EXCL_LINE - the static_assert at the top of this file makes this unreachable
        {
            return SMB_ERR_OVERFLOW; // GCOVR_EXCL_LINE - unreachable body of the guard above
        }
        SmbResult rt = SMB_ERR_IO;
        int rl = smb_round_trip(send, recv, ctx, mlen, &sign, &crypt, &rt);
        h->enc_nonce = crypt.nonce; // persist immediately so the nonce never repeats, even on an error return
        if (rl < 0)
        {
            return rt;
        }
        Smb2Header hd;
        if (!pc_smb2_parse_header(s_smb.rx, (size_t)rl, &hd))
        {
            return SMB_ERR_PROTOCOL;
        }
        h->next_message_id++;
        if (hd.status == Smb2Status::SMB2_STATUS_END_OF_FILE)
        {
            break;
        }
        if (hd.status != Smb2Status::SMB2_STATUS_SUCCESS)
        {
            return SMB_ERR_PROTOCOL;
        }
        Smb2ReadResp r;
        if (!pc_smb2_parse_read_response(s_smb.rx, (size_t)rl, &r) || r.data_len > want)
        {
            return SMB_ERR_PROTOCOL;
        }
        if (r.data_len == 0)
        {
            break;
        }
        memcpy(out + total, r.data, r.data_len);
        total += r.data_len;
        if (r.data_len < want)
        {
            break; // a short read means we reached the end of the file
        }
    }
    *out_len = total;
    return SMB_OK;
}

SmbResult smb_write(SmbHandle *h, uint64_t offset, const uint8_t *data, size_t len, size_t *written, SmbSendFn send,
                    SmbRecvFn recv, void *ctx)
{
    if (!h || !data || !written || !send || !recv)
    {
        return SMB_ERR_ARG;
    }
    *written = 0;
    SmbSign sign = {h->signing_active, h->signing_algo, {0}};
    memcpy(sign.key, h->signing_key, sizeof(sign.key));
    SmbCrypt crypt = {h->encrypt_active, h->encrypt_active, h->enc_cipher, {0}, {0}, h->session_id, h->enc_nonce};
    memcpy(crypt.c2s, h->enc_c2s, sizeof(crypt.c2s));
    memcpy(crypt.s2c, h->enc_s2c, sizeof(crypt.s2c));
    const size_t chunk_max = PC_SMB_BUF - 128; // room for the header + WRITE request body
    size_t total = 0;
    while (total < len)
    {
        size_t want = len - total;
        if (want > chunk_max)
        {
            want = chunk_max;
        }
        size_t mlen = pc_smb2_build_write(s_smb.tx + 4, sizeof(s_smb.tx) - 4, h->next_message_id, h->session_id,
                                          h->tree_id, h->file_id, data + total, want, offset + total);
        if (!mlen) // GCOVR_EXCL_LINE - the static_assert at the top of this file makes this unreachable
        {
            return SMB_ERR_OVERFLOW; // GCOVR_EXCL_LINE - unreachable body of the guard above
        }
        SmbResult rt = SMB_ERR_IO;
        int rl = smb_round_trip(send, recv, ctx, mlen, &sign, &crypt, &rt);
        h->enc_nonce = crypt.nonce; // persist immediately so the nonce never repeats, even on an error return
        if (rl < 0)
        {
            return rt;
        }
        Smb2Header hd;
        if (!pc_smb2_parse_header(s_smb.rx, (size_t)rl, &hd))
        {
            return SMB_ERR_PROTOCOL;
        }
        h->next_message_id++;
        if (hd.status != Smb2Status::SMB2_STATUS_SUCCESS)
        {
            return SMB_ERR_PROTOCOL;
        }
        Smb2WriteResp w;
        if (!pc_smb2_parse_write_response(s_smb.rx, (size_t)rl, &w) || w.count == 0 || w.count > want)
        {
            return SMB_ERR_PROTOCOL; // no progress or a bogus count
        }
        total += w.count;
    }
    if (offset + total > h->file_size)
    {
        h->file_size = offset + total;
    }
    *written = total;
    return SMB_OK;
}

#endif // PC_ENABLE_SMB
