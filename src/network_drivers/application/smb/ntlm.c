// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntlm.c
 * @brief NTLMv2 response computation (see ntlm.h).
 */

#include "ntlm.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_SMB

#include "crypto/hash/md.h" // Md: MD4, MD5 and HMAC-MD5
#include "mmgr/secure.h"    // the pool the digest borrow comes from
#include "mmgr/span.h"      // protocore_span, span.ok

void protocore_ntlm_nt_hash(const char *password, uint8_t nt_hash[16])
{
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

proto_bool protocore_ntlm_ntowfv2(const uint8_t nt_hash[16], const char *user, const char *domain, uint8_t owf[16])
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
    size_t mark = protocore_secure_mark();
    protocore_span w = protocore_secure_span(PROTOCORE_MD_BORROW, 0);
    if (!span.ok(w))
    {
        protocore_secure_release(mark);
        return PROTO_FALSE;
    }
    Md.hmac_args.key = nt_hash;
    Md.hmac_args.key_len = 16;
    Md.hmac_args.msg = buf;
    Md.hmac_args.msg_len = n;
    Md.hmac_args.out = owf;
    Md.hmac_md5(w.buf);
    protocore_secure_release(mark);
    return PROTO_TRUE;
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

size_t protocore_ntlm_v2_response(const uint8_t owf[16], const uint8_t server_challenge[8],
                                  const uint8_t client_challenge[8], const uint8_t timestamp[8],
                                  const uint8_t *target_info, size_t ti_len, uint8_t *out, size_t out_cap,
                                  uint8_t session_key[16])
{
    const size_t temp_len = 2 + 6 + 8 + 8 + 4 + ti_len + 4; // MS-NLMP temp layout
    const size_t protocore_resp_len = 16 + temp_len;        // NTProofStr(16) + temp
    if (!out || protocore_resp_len > out_cap)
    {
        return 0;
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
    return protocore_resp_len;
}

size_t protocore_ntlm_set_mic_flag(const uint8_t *target_info, size_t ti_len, uint8_t *out, size_t out_cap)
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
    return ti_len + 8;
}

void protocore_ntlm_mic(const uint8_t session_key[16], const uint8_t *neg, size_t neg_len, const uint8_t *chal,
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

#endif // PROTOCORE_ENABLE_SMB
