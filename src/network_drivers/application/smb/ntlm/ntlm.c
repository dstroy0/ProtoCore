// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntlm.c
 * @brief NTLMv2 response computation (see ntlm.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SMB

#include "mmgr/protomem/protomem.h"
#include "network_drivers/application/smb/ntlm/ntlm.h"

#include "crypto/hash/md/md.h" // Md: MD4, MD5 and HMAC-MD5
#include "mmgr/secure/secure.h"    // the pool the digest borrow comes from
#include "mmgr/span/span.h"      // protocore_span, span.ok

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void ntlm_nt_hash(uint8_t *restrict work)
{
    (void)work;
    const char *password = Ntlm.nt_hash_args.password;
    uint8_t *nt_hash = Ntlm.nt_hash_args.nt_hash;

    size_t mark = protocore_secure_mark();
    protocore_span w = protocore_secure_span(PROTOCORE_MD_BORROW, 0);
    if (!span.ok(w))
    {
        protocore_secure_release(mark);
        return;
    }
    Md.md4_init(w.buf);
    for (const char *p = password; *p; p++)
    {
        uint8_t pair[2] = {(uint8_t)*p, 0}; // UTF-16LE (ASCII/UTF-8 code unit + high byte 0)
        Md.update_args.data = pair;
        Md.update_args.len = 2;
        Md.update(w.buf);
    }
    Md.final_args.out = nt_hash;
    Md.final(w.buf);
    protocore_secure_release(mark);
}

static void ntlm_ntowfv2(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *nt_hash = Ntlm.ntowfv2_args.nt_hash;
    const char *user = Ntlm.ntowfv2_args.user;
    const char *domain = Ntlm.ntowfv2_args.domain;
    uint8_t *owf = Ntlm.ntowfv2_args.owf;

    uint8_t buf[512]; // UTF-16LE of Uppercase(user) + domain; 256 chars max
    size_t n = 0;
    for (const char *p = user; *p; p++)
    {
        char up = *p;
        if (up >= 'a' && up <= 'z')
        {
            up = (char)(up - 32); // ASCII uppercase (only the user, per MS-NLMP)
        }
        if (n + 2 > sizeof(buf))
        {
            Ntlm.ok = PROTO_FALSE;
            return;
        }
        buf[n++] = (uint8_t)up;
        buf[n++] = 0;
    }
    for (const char *p = domain; *p; p++)
    {
        if (n + 2 > sizeof(buf))
        {
            Ntlm.ok = PROTO_FALSE;
            return;
        }
        buf[n++] = (uint8_t)*p;
        buf[n++] = 0;
    }
    size_t mark = protocore_secure_mark();
    protocore_span w = protocore_secure_span(PROTOCORE_MD_BORROW, 0);
    if (!span.ok(w))
    {
        protocore_secure_release(mark);
        Ntlm.ok = PROTO_FALSE;
        return;
    }
    Md.hmac_args.key = nt_hash;
    Md.hmac_args.key_len = 16;
    Md.hmac_args.msg = buf;
    Md.hmac_args.msg_len = n;
    Md.hmac_args.out = owf;
    Md.hmac_md5(w.buf);
    protocore_secure_release(mark);
    Ntlm.ok = PROTO_TRUE;
}

// HMAC-MD5 over a two-part message (the key here is always the 16-byte NTOWFv2, < 64 bytes,
// so no key-shortening is needed).
static void protocore_hmac_md5_2(const uint8_t key[16], const uint8_t *m1, size_t l1, const uint8_t *m2, size_t l2,
                                 uint8_t out[16])
{
    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; i++)
    {
        uint8_t k = (i < 16) ? key[i] : 0;
        ipad[i] = (uint8_t)(k ^ 0x36);
        opad[i] = (uint8_t)(k ^ 0x5c);
    }
    uint8_t inner[16];
    size_t mark = protocore_secure_mark();
    protocore_span w = protocore_secure_span(PROTOCORE_MD_BORROW, 0);
    if (!span.ok(w))
    {
        protocore_secure_release(mark);
        return;
    }
    Md.md5_init(w.buf);
    Md.update_args.data = ipad;
    Md.update_args.len = 64;
    Md.update(w.buf);
    Md.update_args.data = m1;
    Md.update_args.len = l1;
    Md.update(w.buf);
    if (m2 && l2)
    {
        Md.update_args.data = m2;
        Md.update_args.len = l2;
        Md.update(w.buf);
    }
    Md.final_args.out = inner;
    Md.final(w.buf);
    Md.md5_init(w.buf);
    Md.update_args.data = opad;
    Md.update_args.len = 64;
    Md.update(w.buf);
    Md.update_args.data = inner;
    Md.update_args.len = 16;
    Md.update(w.buf);
    Md.final_args.out = out;
    Md.final(w.buf);
    protocore_secure_release(mark);
}

static void ntlm_v2_response(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *owf = Ntlm.v2_response_args.owf;
    const uint8_t *server_challenge = Ntlm.v2_response_args.server_challenge;
    const uint8_t *client_challenge = Ntlm.v2_response_args.client_challenge;
    const uint8_t *timestamp = Ntlm.v2_response_args.timestamp;
    const uint8_t *target_info = Ntlm.v2_response_args.target_info;
    size_t ti_len = Ntlm.v2_response_args.ti_len;
    uint8_t *out = Ntlm.v2_response_args.out;
    size_t out_cap = Ntlm.v2_response_args.out_cap;
    uint8_t *session_key = Ntlm.v2_response_args.session_key;

    const size_t temp_len = 2 + 6 + 8 + 8 + 4 + ti_len + 4; // MS-NLMP temp layout
    const size_t protocore_resp_len = 16 + temp_len;        // NTProofStr(16) + temp
    if (!out || protocore_resp_len > out_cap)
    {
        Ntlm.n = 0;
        return;
    }

    // Build temp in place at out+16, so the result is NTProofStr(16) || temp contiguously.
    uint8_t *temp = out + 16;
    size_t k = 0;
    temp[k++] = 0x01;        // Responserversion
    temp[k++] = 0x01;        // HiResponserversion
    mem.set(temp + k, 0, 6); // Z(6)
    k += 6;
    mem.cpy(temp + k, timestamp, 8);
    k += 8;
    mem.cpy(temp + k, client_challenge, 8);
    k += 8;
    mem.set(temp + k, 0, 4); // Z(4)
    k += 4;
    mem.cpy(temp + k, target_info, ti_len);
    k += ti_len;
    mem.set(temp + k, 0, 4); // Z(4) trailer; temp_len (line 83) already accounts for it, so k is done

    uint8_t ntproof[16];
    protocore_hmac_md5_2(owf, server_challenge, 8, temp, temp_len, ntproof);
    mem.cpy(out, ntproof, 16); // out = NTProofStr || temp
    if (session_key)
    {
        size_t mark = protocore_secure_mark();
        protocore_span w = protocore_secure_span(PROTOCORE_MD_BORROW, 0);
        if (span.ok(w))
        {
            Md.hmac_args.key = owf;
            Md.hmac_args.key_len = 16;
            Md.hmac_args.msg = ntproof;
            Md.hmac_args.msg_len = 16;
            Md.hmac_args.out = session_key;
            Md.hmac_md5(w.buf);
        }
        protocore_secure_release(mark);
    }
    Ntlm.n = protocore_resp_len;
}

static void ntlm_set_mic_flag(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *target_info = Ntlm.set_mic_flag_args.target_info;
    size_t ti_len = Ntlm.set_mic_flag_args.ti_len;
    uint8_t *out = Ntlm.set_mic_flag_args.out;
    size_t out_cap = Ntlm.set_mic_flag_args.out_cap;

    if (!target_info || !out)
    {
        Ntlm.n = 0;
        return;
    }
    // Walk the AV_PAIR list (AvId u16, AvLen u16, Value[AvLen]), copying it and, if an MsvAvFlags pair
    // (AvId 6) is present, OR-ing bit 0x2 into its 32-bit LE value in place. Track the EOL position so a
    // missing pair can be inserted there.
    if (ti_len > out_cap)
    {
        Ntlm.n = 0;
        return;
    }
    mem.cpy(out, target_info, ti_len);
    size_t p = 0;
    proto_bool found = PROTO_FALSE;
    size_t eol = ti_len; // offset of the MsvAvEOL header, if any
    while (p + 4 <= ti_len)
    {
        uint16_t id = (uint16_t)(out[p] | (out[p + 1] << 8));
        uint16_t len = (uint16_t)(out[p + 2] | (out[p + 3] << 8));
        if (id == 0) // MsvAvEOL
        {
            eol = p;
            break;
        }
        if (id == 6 && len == 4 && p + 8 <= ti_len)
        {
            out[p + 4] |= 0x02; // MsvAvFlags: set "MIC provided" in the low byte of the LE value
            found = PROTO_TRUE;
        }
        if (p + 4 + len < p + 4) // overflow guard
        {
            Ntlm.n = 0;
            return;
        }
        p += 4 + len;
    }
    if (found)
    {
        Ntlm.n = ti_len;
        return;
    }
    // Insert a fresh MsvAvFlags pair (AvId 6, AvLen 4, value 0x00000002). A well-formed list has an EOL
    // (AvId 0) terminator - splice the pair in just before it; a fixture without one (or that ran off the
    // end) gets the pair appended at the tail, matching the pre-MIC pass-through leniency (never fail here).
    if (ti_len + 8 > out_cap)
    {
        Ntlm.n = 0;
        return;
    }
    const size_t at = eol != ti_len ? eol : ti_len;
    if (at < ti_len)
    {
        mem.move(out + at + 8, out + at, ti_len - at); // shift the EOL (and anything after) up by 8
    }
    out[at + 0] = 0x06;
    out[at + 1] = 0x00;
    out[at + 2] = 0x04;
    out[at + 3] = 0x00;
    out[at + 4] = 0x02;
    out[at + 5] = 0x00;
    out[at + 6] = 0x00;
    out[at + 7] = 0x00;
    Ntlm.n = ti_len + 8;
}

static void ntlm_mic(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *session_key = Ntlm.mic_args.session_key;
    const uint8_t *neg = Ntlm.mic_args.neg;
    size_t neg_len = Ntlm.mic_args.neg_len;
    const uint8_t *chal = Ntlm.mic_args.chal;
    size_t chal_len = Ntlm.mic_args.chal_len;
    const uint8_t *auth = Ntlm.mic_args.auth;
    size_t auth_len = Ntlm.mic_args.auth_len;
    uint8_t *out = Ntlm.mic_args.out;

    // HMAC-MD5(session_key, neg || chal || auth), streamed. The key is 16 bytes (< 64), no shortening.
    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; i++)
    {
        uint8_t k = i < 16 ? session_key[i] : 0;
        ipad[i] = (uint8_t)(k ^ 0x36);
        opad[i] = (uint8_t)(k ^ 0x5c);
    }
    size_t mark = protocore_secure_mark();
    protocore_span w = protocore_secure_span(PROTOCORE_MD_BORROW, 0);
    if (!span.ok(w))
    {
        protocore_secure_release(mark);
        return;
    }
    uint8_t inner[16];
    Md.md5_init(w.buf);
    Md.update_args.data = ipad;
    Md.update_args.len = 64;
    Md.update(w.buf);
    Md.update_args.data = neg;
    Md.update_args.len = neg_len;
    Md.update(w.buf);
    Md.update_args.data = chal;
    Md.update_args.len = chal_len;
    Md.update(w.buf);
    Md.update_args.data = auth;
    Md.update_args.len = auth_len;
    Md.update(w.buf);
    Md.final_args.out = inner;
    Md.final(w.buf);
    Md.md5_init(w.buf);
    Md.update_args.data = opad;
    Md.update_args.len = 64;
    Md.update(w.buf);
    Md.update_args.data = inner;
    Md.update_args.len = 16;
    Md.update(w.buf);
    Md.final_args.out = out;
    Md.final(w.buf);
    protocore_secure_release(mark);
}

NtlmNs Ntlm = {
    .nt_hash = ntlm_nt_hash,
    .ntowfv2 = ntlm_ntowfv2,
    .v2_response = ntlm_v2_response,
    .set_mic_flag = ntlm_set_mic_flag,
    .mic = ntlm_mic,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SMB
