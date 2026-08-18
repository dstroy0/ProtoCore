// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file spnego.c
 * @brief SPNEGO GSS-API DER wrapping implementation (see spnego.h). Definite-length DER; the
 *        nested lengths are computed bottom-up, then emitted forward with no temp buffers.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SMB

#include "mmgr/protomem.h"
#include "spnego.h"

PROTOCORE_BEGIN_DECLS

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
static proto_bool der_read(const uint8_t *buf, size_t len, size_t *pos, uint8_t *tag, size_t *clen, size_t *cstart)
{
    size_t p = *pos;
    if (p + 2 > len)
    {
        return PROTO_FALSE;
    }
    *tag = buf[p++];
    size_t l = buf[p++];
    if (l & 0x80)
    {
        size_t nb = l & 0x7f;
        if (nb == 0 || nb > 4 || p + nb > len)
        {
            return PROTO_FALSE; // indefinite / oversized length not supported
        }
        l = 0;
        for (size_t i = 0; i < nb; i++)
        {
            l = (l << 8) | buf[p++];
        }
    }
    if (p + l > len)
    {
        return PROTO_FALSE;
    }
    *cstart = p;
    *clen = l;
    *pos = p + l;
    return PROTO_TRUE;
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void spnego_wrap_negotiate(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *ntlm = Spnego.wrap_negotiate_args.ntlm;
    size_t protocore_ntlm_len = Spnego.wrap_negotiate_args.protocore_ntlm_len;
    uint8_t *out = Spnego.wrap_negotiate_args.out;
    size_t cap = Spnego.wrap_negotiate_args.cap;

    if (!ntlm)
    {
        Spnego.n = 0;
        return;
    }
    size_t octet = tlv_size(protocore_ntlm_len); // OCTET STRING(mechToken)
    size_t mt = tlv_size(octet);                 // [2] mechToken
    size_t seqof = tlv_size(sizeof(NTLM_OID));   // SEQUENCE OF { NTLM OID }
    size_t mtypes = tlv_size(seqof);             // [0] mechTypes
    size_t seq = tlv_size(mtypes + mt);          // SEQUENCE (NegTokenInit)
    size_t nti = tlv_size(seq);                  // [0] negTokenInit
    size_t ictbody = sizeof(SPNEGO_OID) + nti;
    size_t total = tlv_size(ictbody); // [APPLICATION 0] InitialContextToken
    if (!out || total > cap)
    {
        Spnego.n = 0;
        return;
    }

    size_t p = 0;
    wr_tag_len(out, &p, 0x60, ictbody);
    mem.cpy(out + p, SPNEGO_OID, sizeof(SPNEGO_OID));
    p += sizeof(SPNEGO_OID);
    wr_tag_len(out, &p, 0xa0, seq);              // [0] negTokenInit
    wr_tag_len(out, &p, 0x30, mtypes + mt);      // SEQUENCE
    wr_tag_len(out, &p, 0xa0, seqof);            // [0] mechTypes
    wr_tag_len(out, &p, 0x30, sizeof(NTLM_OID)); // SEQUENCE OF
    mem.cpy(out + p, NTLM_OID, sizeof(NTLM_OID));
    p += sizeof(NTLM_OID);
    wr_tag_len(out, &p, 0xa2, octet); // [2] mechToken
    wr_tag_len(out, &p, 0x04, protocore_ntlm_len);
    mem.cpy(out + p, ntlm, protocore_ntlm_len);
    p += protocore_ntlm_len;
    Spnego.n = p;
}

static void spnego_wrap_authenticate(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *ntlm = Spnego.wrap_authenticate_args.ntlm;
    size_t protocore_ntlm_len = Spnego.wrap_authenticate_args.protocore_ntlm_len;
    uint8_t *out = Spnego.wrap_authenticate_args.out;
    size_t cap = Spnego.wrap_authenticate_args.cap;

    if (!ntlm)
    {
        Spnego.n = 0;
        return;
    }
    size_t octet = tlv_size(protocore_ntlm_len); // OCTET STRING(responseToken)
    size_t rt = tlv_size(octet);                 // [2] responseToken
    size_t seq = tlv_size(rt);                   // SEQUENCE
    size_t total = tlv_size(seq);                // [1] NegTokenResp
    if (!out || total > cap)
    {
        Spnego.n = 0;
        return;
    }

    size_t p = 0;
    wr_tag_len(out, &p, 0xa1, seq);   // [1] NegTokenResp
    wr_tag_len(out, &p, 0x30, rt);    // SEQUENCE
    wr_tag_len(out, &p, 0xa2, octet); // [2] responseToken
    wr_tag_len(out, &p, 0x04, protocore_ntlm_len);
    mem.cpy(out + p, ntlm, protocore_ntlm_len);
    p += protocore_ntlm_len;
    Spnego.n = p;
}

static void spnego_parse_response(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *blob = Spnego.parse_response_args.blob;
    size_t len = Spnego.parse_response_args.len;
    const uint8_t **protocore_resp_token = Spnego.parse_response_args.protocore_resp_token;
    size_t *protocore_resp_len = Spnego.parse_response_args.protocore_resp_len;

    if (!blob || !protocore_resp_token || !protocore_resp_len)
    {
        Spnego.ok = PROTO_FALSE;
        return;
    }
    size_t pos = 0;
    size_t cstart;
    size_t clen;
    uint8_t tag;
    // [1] NegTokenResp
    if (!der_read(blob, len, &pos, &tag, &clen, &cstart) || tag != 0xa1)
    {
        Spnego.ok = PROTO_FALSE;
        return;
    }
    size_t neg_end = cstart + clen;
    size_t p = cstart;
    // SEQUENCE
    if (!der_read(blob, neg_end, &p, &tag, &clen, &cstart) || tag != 0x30)
    {
        Spnego.ok = PROTO_FALSE;
        return;
    }
    size_t seq_end = cstart + clen;
    p = cstart;
    // walk the fields for [2] responseToken
    while (p < seq_end)
    {
        if (!der_read(blob, seq_end, &p, &tag, &clen, &cstart))
        {
            Spnego.ok = PROTO_FALSE;
            return;
        }
        if (tag == 0xa2)
        {
            size_t q = cstart;
            size_t cs2;
            size_t cl2;
            uint8_t t2;
            if (!der_read(blob, cstart + clen, &q, &t2, &cl2, &cs2) || t2 != 0x04)
            {
                Spnego.ok = PROTO_FALSE;
                return;
            }
            *protocore_resp_token = blob + cs2;
            *protocore_resp_len = cl2;
            Spnego.ok = PROTO_TRUE;
            return;
        }
    }
    Spnego.ok = PROTO_FALSE;
}

SpnegoNs Spnego = {
    .wrap_negotiate = spnego_wrap_negotiate,
    .parse_response = spnego_parse_response,
    .wrap_authenticate = spnego_wrap_authenticate,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SMB
