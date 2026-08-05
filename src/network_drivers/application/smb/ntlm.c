// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntlm.c
 * @brief NTLMv2 response computation (see ntlm.h).
 */

#include "ntlm.h"

#if PC_ENABLE_SMB

#include "crypto/hash/md.h"
#include "mmgr/secure.h" // SecureScope: lifetime of the borrowed digest state

void pc_ntlm_nt_hash(const char *password, uint8_t nt_hash[16])
{
    size_t mark = pc_secure_mark();
    struct MdCtx *c = pc_md_wants();
    if (c == NULL)
    {
        pc_secure_release(mark);
        return;
    }
    pc_md4_init(c);
    for (const char *p = password; *p; p++)
    {
        uint8_t pair[2] = {(uint8_t)*p, 0}; // UTF-16LE (ASCII/UTF-8 code unit + high byte 0)
        pc_md4_update(c, pair, 2);
    }
    pc_md4_final(c, nt_hash);
    pc_secure_release(mark);
}

proto_bool pc_ntlm_ntowfv2(const uint8_t nt_hash[16], const char *user, const char *domain, uint8_t owf[16])
{
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
            return PROTO_FALSE;
        }
        buf[n++] = (uint8_t)up;
        buf[n++] = 0;
    }
    for (const char *p = domain; *p; p++)
    {
        if (n + 2 > sizeof(buf))
        {
            return PROTO_FALSE;
        }
        buf[n++] = (uint8_t)*p;
        buf[n++] = 0;
    }
    pc_hmac_md5(nt_hash, 16, buf, n, owf);
    return PROTO_TRUE;
}

// HMAC-MD5 over a two-part message (the key here is always the 16-byte NTOWFv2, < 64 bytes,
// so no key-shortening is needed).
static void pc_hmac_md5_2(const uint8_t key[16], const uint8_t *m1, size_t l1, const uint8_t *m2, size_t l2,
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
    size_t mark = pc_secure_mark();
    struct MdCtx *c = pc_md_wants();
    if (c == NULL)
    {
        pc_secure_release(mark);
        return;
    }
    pc_md5_init(c);
    pc_md5_update(c, ipad, 64);
    pc_md5_update(c, m1, l1);
    if (m2 && l2)
    {
        pc_md5_update(c, m2, l2);
    }
    pc_md5_final(c, inner);
    pc_md5_init(c);
    pc_md5_update(c, opad, 64);
    pc_md5_update(c, inner, 16);
    pc_md5_final(c, out);
    pc_secure_release(mark);
}

size_t pc_ntlm_v2_response(const uint8_t owf[16], const uint8_t server_challenge[8], const uint8_t client_challenge[8],
                           const uint8_t timestamp[8], const uint8_t *target_info, size_t ti_len, uint8_t *out,
                           size_t out_cap, uint8_t session_key[16])
{
    const size_t temp_len = 2 + 6 + 8 + 8 + 4 + ti_len + 4; // MS-NLMP temp layout
    const size_t pc_resp_len = 16 + temp_len;               // NTProofStr(16) + temp
    if (!out || pc_resp_len > out_cap)
    {
        return 0;
    }

    // Build temp in place at out+16, so the result is NTProofStr(16) || temp contiguously.
    uint8_t *temp = out + 16;
    size_t k = 0;
    temp[k++] = 0x01;       // Responserversion
    temp[k++] = 0x01;       // HiResponserversion
    memset(temp + k, 0, 6); // Z(6)
    k += 6;
    memcpy(temp + k, timestamp, 8);
    k += 8;
    memcpy(temp + k, client_challenge, 8);
    k += 8;
    memset(temp + k, 0, 4); // Z(4)
    k += 4;
    memcpy(temp + k, target_info, ti_len);
    k += ti_len;
    memset(temp + k, 0, 4); // Z(4) trailer; temp_len (line 83) already accounts for it, so k is done

    uint8_t ntproof[16];
    pc_hmac_md5_2(owf, server_challenge, 8, temp, temp_len, ntproof);
    memcpy(out, ntproof, 16); // out = NTProofStr || temp
    if (session_key)
    {
        pc_hmac_md5(owf, 16, ntproof, 16, session_key);
    }
    return pc_resp_len;
}

size_t pc_ntlm_set_mic_flag(const uint8_t *target_info, size_t ti_len, uint8_t *out, size_t out_cap)
{
    if (!target_info || !out)
    {
        return 0;
    }
    // Walk the AV_PAIR list (AvId u16, AvLen u16, Value[AvLen]), copying it and, if an MsvAvFlags pair
    // (AvId 6) is present, OR-ing bit 0x2 into its 32-bit LE value in place. Track the EOL position so a
    // missing pair can be inserted there.
    if (ti_len > out_cap)
    {
        return 0;
    }
    memcpy(out, target_info, ti_len);
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
            return 0;
        }
        p += 4 + len;
    }
    if (found)
    {
        return ti_len;
    }
    // Insert a fresh MsvAvFlags pair (AvId 6, AvLen 4, value 0x00000002). A well-formed list has an EOL
    // (AvId 0) terminator - splice the pair in just before it; a fixture without one (or that ran off the
    // end) gets the pair appended at the tail, matching the pre-MIC pass-through leniency (never fail here).
    if (ti_len + 8 > out_cap)
    {
        return 0;
    }
    const size_t at = eol != ti_len ? eol : ti_len;
    if (at < ti_len)
    {
        memmove(out + at + 8, out + at, ti_len - at); // shift the EOL (and anything after) up by 8
    }
    out[at + 0] = 0x06;
    out[at + 1] = 0x00;
    out[at + 2] = 0x04;
    out[at + 3] = 0x00;
    out[at + 4] = 0x02;
    out[at + 5] = 0x00;
    out[at + 6] = 0x00;
    out[at + 7] = 0x00;
    return ti_len + 8;
}

void pc_ntlm_mic(const uint8_t session_key[16], const uint8_t *neg, size_t neg_len, const uint8_t *chal,
                 size_t chal_len, const uint8_t *auth, size_t auth_len, uint8_t out[16])
{
    // HMAC-MD5(session_key, neg || chal || auth), streamed. The key is 16 bytes (< 64), no shortening.
    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; i++)
    {
        uint8_t k = i < 16 ? session_key[i] : 0;
        ipad[i] = (uint8_t)(k ^ 0x36);
        opad[i] = (uint8_t)(k ^ 0x5c);
    }
    size_t mark = pc_secure_mark();
    struct MdCtx *c = pc_md_wants();
    if (c == NULL)
    {
        pc_secure_release(mark);
        return;
    }
    uint8_t inner[16];
    pc_md5_init(c);
    pc_md5_update(c, ipad, 64);
    pc_md5_update(c, neg, neg_len);
    pc_md5_update(c, chal, chal_len);
    pc_md5_update(c, auth, auth_len);
    pc_md5_final(c, inner);
    pc_md5_init(c);
    pc_md5_update(c, opad, 64);
    pc_md5_update(c, inner, 16);
    pc_md5_final(c, out);
    pc_secure_release(mark);
}

#endif // PC_ENABLE_SMB
