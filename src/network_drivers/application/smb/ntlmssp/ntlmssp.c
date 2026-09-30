// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntlmssp.c
 * @brief NTLMSSP message codec implementation (see ntlmssp.h). Little-endian; text UTF-16LE.
 */

#include "protocore_config.h" // the entry point: the widths

#if PROTOCORE_ENABLE_SMB

#include "memoria_operor/memoria_operor.h"
#include "network_drivers/application/smb/ntlmssp/ntlmssp.h"

#include "endian/endian.h"

static const uint8_t NTLMSSP_SIG[8] = {'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};

// Write a Len/MaxLen/BufferOffset field triplet at @p f.
static void wr_field(uint8_t *f, uint16_t len, uint32_t off)
{
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = f + 0, .val = len, .width = MMGR_ENDIAN_16);
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = f + 2, .val = len, .width = MMGR_ENDIAN_16); // MaxLen == Len
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = f + 4, .val = off, .width = MMGR_ENDIAN_32);
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

size_t protocore_ntlmssp_build_negotiate(uint8_t *work, uint8_t *buf, size_t cap, uint32_t flags)
{
    (void)work;

    if (!buf || cap < 32)
    {
        return 0;
    }
    EMBED_CALL(memor.set, MemoriaCfg, .dst = buf, .val = 0, .bytes = 32);
    EMBED_CALL(memor.cpy, MemoriaCfg, .dst = buf + 0, .src = NTLMSSP_SIG, .bytes = 8); // Signature
    // MessageType = NEGOTIATE
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = buf + 8, .val = 1, .width = MMGR_ENDIAN_32);
    // NegotiateFlags
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = buf + 12, .val = flags, .width = MMGR_ENDIAN_32);
    wr_field(buf + 16, 0, 32); // DomainNameFields (empty; offset = end of header)
    wr_field(buf + 24, 0, 32); // WorkstationFields (empty)
    return 32;
}

proto_bool protocore_ntlmssp_parse_challenge(uint8_t *work, const uint8_t *msg, size_t len, NtlmChallenge *out)
{
    (void)work;

    if (!msg || !out || len < 48) // through TargetInfoFields
    {
        return PROTO_FALSE;
    }
    if (EMBED_CALL(memor.cmp, MemoriaCfg, .src = msg, .other = NTLMSSP_SIG, .bytes = 8) != 0 ||
        (uint32_t)EMBED_CALL(parva_extremitas.rd, EndianCfg, .src = msg + 8, .width = MMGR_ENDIAN_32) != 2)
    {
        return PROTO_FALSE;
    }
    out->flags = (uint32_t)EMBED_CALL(parva_extremitas.rd, EndianCfg, .src = msg + 20, .width = MMGR_ENDIAN_32);
    EMBED_CALL(memor.cpy, MemoriaCfg, .dst = out->server_challenge, .src = msg + 24, .bytes = 8);
    uint16_t ti_len = (uint16_t)EMBED_CALL(parva_extremitas.rd, EndianCfg, .src = msg + 40, .width = MMGR_ENDIAN_16);
    uint32_t ti_off = (uint32_t)EMBED_CALL(parva_extremitas.rd, EndianCfg, .src = msg + 44, .width = MMGR_ENDIAN_32);
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

size_t protocore_ntlmssp_build_authenticate(uint8_t *work, uint8_t *buf, size_t cap, const uint8_t *lm_resp,
                                            size_t lm_len, const uint8_t *nt_resp, size_t nt_len, const char *domain,
                                            const char *user, const char *workstation, uint32_t flags,
                                            proto_bool with_mic)
{
    (void)work;

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

    EMBED_CALL(memor.set, MemoriaCfg, .dst = buf, .val = 0, .bytes = HDR);
    EMBED_CALL(memor.cpy, MemoriaCfg, .dst = buf + 0, .src = NTLMSSP_SIG, .bytes = 8); // Signature
    // MessageType = AUTHENTICATE
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = buf + 8, .val = 3, .width = MMGR_ENDIAN_32);
    if (with_mic)
    {
        // Version (offset 64): a plausible Windows build; servers do not validate the value. MIC (offset
        // 72) stays zero here - the caller writes it after taking HMAC-MD5 over the three messages.
        buf[64] = 6; // ProductMajorVersion
        buf[65] = 1; // ProductMinorVersion
        // ProductBuild
        EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = buf + 66, .val = 7601, .width = MMGR_ENDIAN_16);
        buf[71] = 15; // NTLMRevisionCurrent (NTLMSSP_REVISION_W2K3)
    }

    // Lay out the payload after the fixed header, then point each field at it.
    size_t off = HDR;
    size_t lm_off = off;
    if (lm_resp && lm_len)
    {
        EMBED_CALL(memor.cpy, MemoriaCfg, .dst = buf + off, .src = lm_resp, .bytes = lm_len);
    }
    off += lm_len;
    size_t nt_off = off;
    if (nt_resp && nt_len)
    {
        EMBED_CALL(memor.cpy, MemoriaCfg, .dst = buf + off, .src = nt_resp, .bytes = nt_len);
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
    // NegotiateFlags
    EMBED_CALL(parva_extremitas.wr, EndianCfg, .dst = buf + 60, .val = flags, .width = MMGR_ENDIAN_32);
    return total;
}

#endif // PROTOCORE_ENABLE_SMB
