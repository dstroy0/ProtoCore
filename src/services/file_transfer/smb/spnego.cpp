// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file spnego.cpp
 * @brief SPNEGO GSS-API DER wrapping implementation (see spnego.h). Definite-length DER; the
 *        nested lengths are computed bottom-up, then emitted forward with no temp buffers.
 */

#include "spnego.h"

#if PC_ENABLE_SMB

#include <string.h>

// OID TLVs (tag + length + content).
static const uint8_t SPNEGO_OID[] = {0x06, 0x06, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x02}; // 1.3.6.1.5.5.2
static const uint8_t NTLM_OID[] = {0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04,
                                   0x01, 0x82, 0x37, 0x02, 0x02, 0x0a}; // 1.3.6.1.4.1.311.2.2.10

static size_t der_len_size(size_t n)
{
    return n < 0x80 ? 1 : n < 0x100 ? 2 : n < 0x10000 ? 3 : 4;
}
static size_t tlv_size(size_t clen)
{
    return 1 + der_len_size(clen) + clen;
}
// Write a tag + definite-length header for @p clen bytes of content; advance *p.
static void wr_tag_len(uint8_t *out, size_t *p, uint8_t tag, size_t clen)
{
    out[(*p)++] = tag;
    if (clen < 0x80)
    {
        out[(*p)++] = (uint8_t)clen;
    }
    else if (clen < 0x100)
    {
        out[(*p)++] = 0x81;
        out[(*p)++] = (uint8_t)clen;
    }
    else if (clen < 0x10000)
    {
        out[(*p)++] = 0x82;
        out[(*p)++] = (uint8_t)(clen >> 8);
        out[(*p)++] = (uint8_t)clen;
    }
    else
    {
        out[(*p)++] = 0x83;
        out[(*p)++] = (uint8_t)(clen >> 16);
        out[(*p)++] = (uint8_t)(clen >> 8);
        out[(*p)++] = (uint8_t)clen;
    }
}

// Read one DER TLV at *pos in [buf,buf+len): set tag / content length / content offset and advance
// *pos past the whole TLV. Definite short/long form only. Returns false on truncation / bad length.
static bool der_read(const uint8_t *buf, size_t len, size_t *pos, uint8_t *tag, size_t *clen, size_t *cstart)
{
    size_t p = *pos;
    if (p + 2 > len)
    {
        return false;
    }
    *tag = buf[p++];
    size_t l = buf[p++];
    if (l & 0x80)
    {
        size_t nb = l & 0x7f;
        if (nb == 0 || nb > 4 || p + nb > len)
        {
            return false; // indefinite / oversized length not supported
        }
        l = 0;
        for (size_t i = 0; i < nb; i++)
        {
            l = (l << 8) | buf[p++];
        }
    }
    if (p + l > len)
    {
        return false;
    }
    *cstart = p;
    *clen = l;
    *pos = p + l;
    return true;
}

size_t pc_spnego_wrap_negotiate(const uint8_t *ntlm, size_t pc_ntlm_len, uint8_t *out, size_t cap)
{
    if (!ntlm)
    {
        return 0;
    }
    size_t octet = tlv_size(pc_ntlm_len);      // OCTET STRING(mechToken)
    size_t mt = tlv_size(octet);               // [2] mechToken
    size_t seqof = tlv_size(sizeof(NTLM_OID)); // SEQUENCE OF { NTLM OID }
    size_t mtypes = tlv_size(seqof);           // [0] mechTypes
    size_t seq = tlv_size(mtypes + mt);        // SEQUENCE (NegTokenInit)
    size_t nti = tlv_size(seq);                // [0] negTokenInit
    size_t ictbody = sizeof(SPNEGO_OID) + nti;
    size_t total = tlv_size(ictbody); // [APPLICATION 0] InitialContextToken
    if (!out || total > cap)
    {
        return 0;
    }

    size_t p = 0;
    wr_tag_len(out, &p, 0x60, ictbody);
    memcpy(out + p, SPNEGO_OID, sizeof(SPNEGO_OID));
    p += sizeof(SPNEGO_OID);
    wr_tag_len(out, &p, 0xa0, seq);              // [0] negTokenInit
    wr_tag_len(out, &p, 0x30, mtypes + mt);      // SEQUENCE
    wr_tag_len(out, &p, 0xa0, seqof);            // [0] mechTypes
    wr_tag_len(out, &p, 0x30, sizeof(NTLM_OID)); // SEQUENCE OF
    memcpy(out + p, NTLM_OID, sizeof(NTLM_OID));
    p += sizeof(NTLM_OID);
    wr_tag_len(out, &p, 0xa2, octet); // [2] mechToken
    wr_tag_len(out, &p, 0x04, pc_ntlm_len);
    memcpy(out + p, ntlm, pc_ntlm_len);
    p += pc_ntlm_len;
    return p;
}

size_t pc_spnego_wrap_authenticate(const uint8_t *ntlm, size_t pc_ntlm_len, uint8_t *out, size_t cap)
{
    if (!ntlm)
    {
        return 0;
    }
    size_t octet = tlv_size(pc_ntlm_len); // OCTET STRING(responseToken)
    size_t rt = tlv_size(octet);          // [2] responseToken
    size_t seq = tlv_size(rt);            // SEQUENCE
    size_t total = tlv_size(seq);         // [1] NegTokenResp
    if (!out || total > cap)
    {
        return 0;
    }

    size_t p = 0;
    wr_tag_len(out, &p, 0xa1, seq);   // [1] NegTokenResp
    wr_tag_len(out, &p, 0x30, rt);    // SEQUENCE
    wr_tag_len(out, &p, 0xa2, octet); // [2] responseToken
    wr_tag_len(out, &p, 0x04, pc_ntlm_len);
    memcpy(out + p, ntlm, pc_ntlm_len);
    p += pc_ntlm_len;
    return p;
}

bool pc_spnego_parse_response(const uint8_t *blob, size_t len, const uint8_t **pc_resp_token, size_t *pc_resp_len)
{
    if (!blob || !pc_resp_token || !pc_resp_len)
    {
        return false;
    }
    size_t pos = 0;
    size_t cstart;
    size_t clen;
    uint8_t tag;
    // [1] NegTokenResp
    if (!der_read(blob, len, &pos, &tag, &clen, &cstart) || tag != 0xa1)
    {
        return false;
    }
    size_t neg_end = cstart + clen;
    size_t p = cstart;
    // SEQUENCE
    if (!der_read(blob, neg_end, &p, &tag, &clen, &cstart) || tag != 0x30)
    {
        return false;
    }
    size_t seq_end = cstart + clen;
    p = cstart;
    // walk the fields for [2] responseToken
    while (p < seq_end)
    {
        if (!der_read(blob, seq_end, &p, &tag, &clen, &cstart))
        {
            return false;
        }
        if (tag == 0xa2)
        {
            size_t q = cstart;
            size_t cs2;
            size_t cl2;
            uint8_t t2;
            if (!der_read(blob, cstart + clen, &q, &t2, &cl2, &cs2) || t2 != 0x04)
            {
                return false;
            }
            *pc_resp_token = blob + cs2;
            *pc_resp_len = cl2;
            return true;
        }
    }
    return false;
}

#endif // PC_ENABLE_SMB
