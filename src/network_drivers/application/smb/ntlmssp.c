// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntlmssp.c
 * @brief NTLMSSP message codec implementation (see ntlmssp.h). Little-endian; text UTF-16LE.
 */

#include "ntlmssp.h"

#if PC_ENABLE_SMB

#include <string.h>

#include "shared_primitives/endian.h"

static const uint8_t NTLMSSP_SIG[8] = {'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};

// Write a Len/MaxLen/BufferOffset field triplet at @p f.
static void wr_field(uint8_t *f, uint16_t len, uint32_t off)
{
    pc_wr16le(f + 0, len);
    pc_wr16le(f + 2, len); // MaxLen == Len
    pc_wr32le(f + 4, off);
}

size_t pc_ntlmssp_build_negotiate(uint8_t *buf, size_t cap, uint32_t flags)
{
    if (!buf || cap < 32)
    {
        return 0;
    }
    memset(buf, 0, 32);
    memcpy(buf + 0, NTLMSSP_SIG, 8); // Signature
    pc_wr32le(buf + 8, 1);           // MessageType = NEGOTIATE
    pc_wr32le(buf + 12, flags);      // NegotiateFlags
    wr_field(buf + 16, 0, 32);       // DomainNameFields (empty; offset = end of header)
    wr_field(buf + 24, 0, 32);       // WorkstationFields (empty)
    return 32;
}

proto_bool pc_ntlmssp_parse_challenge(const uint8_t *msg, size_t len, NtlmChallenge *out)
{
    if (!msg || !out || len < 48) // through TargetInfoFields
    {
        return PROTO_FALSE;
    }
    if (memcmp(msg, NTLMSSP_SIG, 8) != 0 || pc_rd32le(msg + 8) != 2)
    {
        return PROTO_FALSE;
    }
    out->flags = pc_rd32le(msg + 20);
    memcpy(out->server_challenge, msg + 24, 8);
    uint16_t ti_len = pc_rd16le(msg + 40);
    uint32_t ti_off = pc_rd32le(msg + 44);
    if (ti_len == 0)
    {
        out->target_info = NULL;
        out->target_info_len = 0;
        return PROTO_TRUE;
    }
    if ((size_t)ti_off + ti_len > len) // target info out of bounds -> fail closed
    {
        return PROTO_FALSE;
    }
    out->target_info = msg + ti_off;
    out->target_info_len = ti_len;
    return PROTO_TRUE;
}

// Append the UTF-16LE encoding of @p s to buf[at..]; returns the byte count (2 * strlen).
static size_t put_utf16le(uint8_t *buf, const char *s)
{
    size_t n = 0;
    if (s)
    {
        for (const char *p = s; *p; p++)
        {
            buf[n++] = (uint8_t)*p;
            buf[n++] = 0;
        }
    }
    return n;
}
static size_t utf16_len(const char *s)
{
    size_t n = 0;
    if (s)
    {
        while (s[n])
        {
            n++;
        }
    }
    return n * 2;
}

size_t pc_ntlmssp_build_authenticate(uint8_t *buf, size_t cap, const uint8_t *lm_resp, size_t lm_len,
                                     const uint8_t *nt_resp, size_t nt_len, const char *domain, const char *user,
                                     const char *workstation, uint32_t flags, proto_bool with_mic)
{
    // With a MIC the fixed part carries an 8-byte Version + a 16-byte MIC before the payload (MS-NLMP
    // §2.2.1.3); NTLMSSP_NEGOTIATE_VERSION must then be set so the server knows the Version is present.
    const size_t HDR = with_mic ? 88 : 64;
    if (with_mic)
    {
        flags |= NTLMSSP_NEGOTIATE_VERSION;
    }
    size_t dlen = utf16_len(domain);
    size_t ulen = utf16_len(user);
    size_t wlen = utf16_len(workstation);
    size_t total = HDR + lm_len + nt_len + dlen + ulen + wlen; // session key empty
    if (!buf || total > cap)
    {
        return 0;
    }

    memset(buf, 0, HDR);
    memcpy(buf + 0, NTLMSSP_SIG, 8); // Signature
    pc_wr32le(buf + 8, 3);           // MessageType = AUTHENTICATE
    if (with_mic)
    {
        // Version (offset 64): a plausible Windows build; servers do not validate the value. MIC (offset
        // 72) stays zero here - the caller writes it after taking HMAC-MD5 over the three messages.
        buf[64] = 6;               // ProductMajorVersion
        buf[65] = 1;               // ProductMinorVersion
        pc_wr16le(buf + 66, 7601); // ProductBuild
        buf[71] = 15;              // NTLMRevisionCurrent (NTLMSSP_REVISION_W2K3)
    }

    // Lay out the payload after the fixed header, then point each field at it.
    size_t off = HDR;
    size_t lm_off = off;
    if (lm_resp && lm_len)
    {
        memcpy(buf + off, lm_resp, lm_len);
    }
    off += lm_len;
    size_t nt_off = off;
    if (nt_resp && nt_len)
    {
        memcpy(buf + off, nt_resp, nt_len);
    }
    off += nt_len;
    size_t dom_off = off;
    off += put_utf16le(buf + off, domain);
    size_t usr_off = off;
    off += put_utf16le(buf + off, user);
    size_t wks_off = off;
    off += put_utf16le(buf + off, workstation);
    size_t key_off = off; // EncryptedRandomSessionKey empty

    wr_field(buf + 12, (uint16_t)lm_len, (uint32_t)lm_off); // LmChallengeResponseFields
    wr_field(buf + 20, (uint16_t)nt_len, (uint32_t)nt_off); // NtChallengeResponseFields
    wr_field(buf + 28, (uint16_t)dlen, (uint32_t)dom_off);  // DomainNameFields
    wr_field(buf + 36, (uint16_t)ulen, (uint32_t)usr_off);  // UserNameFields
    wr_field(buf + 44, (uint16_t)wlen, (uint32_t)wks_off);  // WorkstationFields
    wr_field(buf + 52, 0, (uint32_t)key_off);               // EncryptedRandomSessionKeyFields
    pc_wr32le(buf + 60, flags);                             // NegotiateFlags
    return total;
}

#endif // PC_ENABLE_SMB
